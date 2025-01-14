// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/fragment-device.h>
#include <lib/fdf/cpp/channel.h>
#include <lib/fdf/cpp/protocol.h>
#include <lib/fdf/env.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <lib/syslog/logger.h>
#include <lib/zx/process.h>
#include <stdarg.h>
#include <stdio.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>

#include <utility>

#include <fbl/auto_lock.h>

#include "src/devices/bin/driver_host/composite_device.h"
#include "src/devices/bin/driver_host/devfs_vnode.h"
#include "src/devices/bin/driver_host/driver_host.h"
#include "src/devices/bin/driver_host/proxy_device.h"
#include "src/devices/bin/driver_host/scheduler_profile.h"

namespace fio = fuchsia_io;

// These are the API entry-points from drivers
// They must take the internal::api_lock before calling internal::* internals
//
// Driver code MUST NOT directly call internal::* APIs

// LibDriver Device Interface
//

#define ALLOWED_FLAGS                                                        \
  (DEVICE_ADD_NON_BINDABLE | DEVICE_ADD_INSTANCE | DEVICE_ADD_MUST_ISOLATE | \
   DEVICE_ADD_ALLOW_MULTI_COMPOSITE)

namespace internal {

static_assert(static_cast<uint8_t>(fuchsia_device::wire::DevicePowerState::kDevicePowerStateD0) ==
              DEV_POWER_STATE_D0);
static_assert(static_cast<uint8_t>(fuchsia_device::wire::DevicePowerState::kDevicePowerStateD1) ==
              DEV_POWER_STATE_D1);
static_assert(static_cast<uint8_t>(fuchsia_device::wire::DevicePowerState::kDevicePowerStateD2) ==
              DEV_POWER_STATE_D2);
static_assert(
    static_cast<uint8_t>(fuchsia_device::wire::DevicePowerState::kDevicePowerStateD3Hot) ==
    DEV_POWER_STATE_D3HOT);
static_assert(
    static_cast<uint8_t>(fuchsia_device::wire::DevicePowerState::kDevicePowerStateD3Cold) ==
    DEV_POWER_STATE_D3COLD);

const device_power_state_info_t kDeviceDefaultPowerStates[2] = {
    {.state_id = DEV_POWER_STATE_D0}, {.state_id = DEV_POWER_STATE_D3COLD}};

const device_performance_state_info_t kDeviceDefaultPerfStates[1] = {
    {.state_id = DEV_PERFORMANCE_STATE_P0},
};

const zx_device::SystemPowerStateMapping kDeviceDefaultStateMapping = []() {
  zx_device::SystemPowerStateMapping states_mapping{};
  for (auto& entry : states_mapping) {
    entry.dev_state = fuchsia_device::wire::DevicePowerState::kDevicePowerStateD3Cold;
    entry.wakeup_enable = false;
  }
  return states_mapping;
}();
}  // namespace internal

__EXPORT zx_status_t device_add_from_driver(zx_driver_t* drv, zx_device_t* parent,
                                            device_add_args_t* args, zx_device_t** out) {
  zx_status_t r;
  fbl::RefPtr<zx_device_t> dev;

  if (!parent) {
    return ZX_ERR_INVALID_ARGS;
  }
  ZX_DEBUG_ASSERT_MSG(parent && parent->magic == DEV_MAGIC, "Dev pointer '%p' is not a real device",
                      parent);

  fbl::RefPtr<zx_device> parent_ref(parent);

  if (!args || args->version != DEVICE_ADD_ARGS_VERSION) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (!args->ops || args->ops->version != DEVICE_OPS_VERSION) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (args->flags & ~ALLOWED_FLAGS) {
    return ZX_ERR_INVALID_ARGS;
  }
  if ((args->flags & DEVICE_ADD_INSTANCE) && (args->flags & (DEVICE_ADD_MUST_ISOLATE))) {
    return ZX_ERR_INVALID_ARGS;
  }

  // If the device will be added in the same driver_host and visible,
  // we can connect the client immediately after adding the device.
  // Otherwise we will pass this channel to the devcoordinator via internal::device_add.
  zx::channel client_remote(args->client_remote);
  if (args->flags & DEVICE_ADD_MUST_ISOLATE && client_remote.is_valid()) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx::vmo inspect(args->inspect_vmo);

  auto api_ctx = internal::ContextForApi();
  fbl::AutoLock lock(&api_ctx->api_lock());

  fbl::RefPtr<Driver> driver;
  auto* current = static_cast<Driver*>(const_cast<void*>(fdf_env_get_current_driver()));
  if (current != nullptr && current->zx_driver() == drv) {
    // We try retrieve the current driver instance from the driver runtime first. If we are
    // currently in a driver hook such as |bind| or |create| this will yield us the correct
    // driver. It should also yeild us the correct driver in most other cases, however it's
    // possible that it will yield the wrong driver if a device is added inside of a banjo call -
    // this is why we also double check that the zx_driver objects line up.
    driver = fbl::RefPtr<Driver>(current);
  } else {
    // Otherwise we fall back to assuming the driver is not in bind or create, and therefore the
    // device being added is from the same driver instance as the parent. This can incorrectly
    // occur if the driver adds a device to it's original parent inside of a dedicated thread it
    // spawned.
    driver = parent->driver;
  }

  r = api_ctx->DeviceCreate(std::move(driver), args->name, args->ctx, args->ops, &dev);
  if (r != ZX_OK) {
    return r;
  }
  if (args->proto_id) {
    dev->set_protocol_id(args->proto_id);
    dev->set_protocol_ops(args->proto_ops);
  }
  if (args->fidl_protocol_offers) {
    dev->set_fidl_offers({args->fidl_protocol_offers, args->fidl_protocol_offer_count});
  }
  if (args->fidl_service_offers) {
    dev->set_fidl_service_offers({args->fidl_service_offers, args->fidl_service_offer_count});
  }
  if (args->flags & DEVICE_ADD_NON_BINDABLE) {
    dev->set_flag(DEV_FLAG_UNBINDABLE);
  }
  if (args->flags & DEVICE_ADD_ALLOW_MULTI_COMPOSITE) {
    dev->set_flag(DEV_FLAG_ALLOW_MULTI_COMPOSITE);
  }

  if (!args->power_states) {
    // TODO(fxbug.dev/34081): Remove when all drivers declare power states
    // Temporarily allocate working and non-working power states
    r = dev->SetPowerStates(internal::kDeviceDefaultPowerStates,
                            std::size(internal::kDeviceDefaultPowerStates));
  } else {
    r = dev->SetPowerStates(args->power_states, args->power_state_count);
  }
  if (r != ZX_OK) {
    return r;
  }

  if (args->performance_states && (args->performance_state_count != 0)) {
    r = dev->SetPerformanceStates(args->performance_states, args->performance_state_count);
  } else {
    r = dev->SetPerformanceStates(internal::kDeviceDefaultPerfStates,
                                  std::size(internal::kDeviceDefaultPerfStates));
  }

  if (r != ZX_OK) {
    return r;
  }

  // Set default system to device power state mapping. This can be later
  // updated by the system power manager.
  r = dev->SetSystemPowerStateMapping(internal::kDeviceDefaultStateMapping);
  if (r != ZX_OK) {
    return r;
  }

  fidl::ClientEnd<fio::Directory> outgoing_dir(zx::channel(args->outgoing_dir_channel));
  // The outgoing directory can be used for either out-of-process FIDL protocols,
  // or in-process runtime protocols.
  if (outgoing_dir) {
    if ((args->fidl_protocol_offer_count > 0) || (args->fidl_service_offer_count > 0)) {
      if (!(args->flags & DEVICE_ADD_MUST_ISOLATE)) {
        // It is only valid to provide fidl protocols if child is meant to be spawned in another
        // driver host.
        return ZX_ERR_INVALID_ARGS;
      }
    }
    if (args->runtime_service_offer_count > 0) {
      if (args->flags & DEVICE_ADD_MUST_ISOLATE) {
        // Runtime protocols are only supported in-process.
        return ZX_ERR_INVALID_ARGS;
      }
      dev->set_runtime_outgoing_dir(std::move(outgoing_dir));
    }
  }

  // out must be set before calling DeviceAdd().
  // DeviceAdd() may result in child devices being created before it returns,
  // and those children may call ops on the device before device_add() returns.
  // This leaked-ref will be accounted below.
  if (out) {
    *out = dev.get();
  }
  if (args->flags & DEVICE_ADD_MUST_ISOLATE) {
    r = api_ctx->DeviceAdd(dev, parent_ref, args, std::move(inspect), std::move(outgoing_dir));
  } else if (args->flags & DEVICE_ADD_INSTANCE) {
    dev->set_flag(DEV_FLAG_INSTANCE | DEV_FLAG_UNBINDABLE);
    // Set props and proxy args to null just in case:
    args->str_prop_count = 0;
    args->prop_count = 0;
    args->proxy_args = nullptr;
    r = api_ctx->DeviceAdd(dev, parent_ref, args, zx::vmo(), fidl::ClientEnd<fio::Directory>());
  } else {
    args->proxy_args = nullptr;
    r = api_ctx->DeviceAdd(dev, parent_ref, args, std::move(inspect),
                           fidl::ClientEnd<fio::Directory>());
  }
  if (r != ZX_OK) {
    if (out) {
      *out = nullptr;
    }
    dev.reset();
  }

  if (dev && client_remote.is_valid()) {
    async::PostTask(
        internal::ContextForApi()->loop().dispatcher(),
        [dev, client_remote = std::move(client_remote)]() mutable {
          // This needs to be called async because it would otherwise re-entrantly call
          // back into the driver.
          internal::ContextForApi()->DeviceConnect(
              dev, fio::wire::OpenFlags::kRightReadable | fio::wire::OpenFlags::kRightWritable,
              std::move(client_remote));
        });

    // Leak the reference that was written to |out|, it will be recovered in device_remove().
    // For device instances we mimic the behavior of |open| by not leaking the reference,
    // effectively passing owenership to the new connection.
    if (!(args->flags & DEVICE_ADD_INSTANCE)) {
      __UNUSED auto ptr = fbl::ExportToRawPtr(&dev);
    }
  } else {
    // Leak the reference that was written to |out|, it will be recovered in device_remove().
    __UNUSED auto ptr = fbl::ExportToRawPtr(&dev);
  }

  return r;
}

__EXPORT void device_init_reply(zx_device_t* dev, zx_status_t status,
                                const device_init_reply_args_t* args) {
  ZX_DEBUG_ASSERT_MSG(dev && dev->magic == DEV_MAGIC, "Dev pointer '%p' is not a real device", dev);
  fbl::AutoLock lock(&internal::ContextForApi()->api_lock());
  fbl::RefPtr<zx_device_t> dev_ref(dev);
  internal::ContextForApi()->DeviceInitReply(dev_ref, status, args);
}

__EXPORT zx_status_t device_rebind(zx_device_t* dev) {
  ZX_DEBUG_ASSERT_MSG(dev && dev->magic == DEV_MAGIC, "Dev pointer '%p' is not a real device", dev);
  fbl::AutoLock lock(&internal::ContextForApi()->api_lock());
  fbl::RefPtr<zx_device_t> dev_ref(dev);
  return internal::ContextForApi()->DeviceRebind(dev_ref);
}

__EXPORT void device_async_remove(zx_device_t* dev) {
  ZX_DEBUG_ASSERT_MSG(dev && dev->magic == DEV_MAGIC, "Dev pointer '%p' is not a real device", dev);
  fbl::AutoLock lock(&internal::ContextForApi()->api_lock());
  // The leaked reference in device_add_from_driver() will be recovered when
  // DriverManagerRemove() completes. We can't drop it here as we are just
  // scheduling the removal, and do not know when that will happen.
  fbl::RefPtr<zx_device_t> dev_ref(dev);
  internal::ContextForApi()->DeviceRemove(dev_ref, true /* unbind_self */);
}

__EXPORT void device_unbind_reply(zx_device_t* dev) {
  ZX_DEBUG_ASSERT_MSG(dev && dev->magic == DEV_MAGIC, "Dev pointer '%p' is not a real device", dev);
  fbl::AutoLock lock(&internal::ContextForApi()->api_lock());
  fbl::RefPtr<zx_device_t> dev_ref(dev);
  internal::ContextForApi()->DeviceUnbindReply(dev_ref);
}

__EXPORT void device_suspend_reply(zx_device_t* dev, zx_status_t status, uint8_t out_state) {
  ZX_DEBUG_ASSERT_MSG(dev && dev->magic == DEV_MAGIC, "Dev pointer '%p' is not a real device", dev);
  fbl::AutoLock lock(&internal::ContextForApi()->api_lock());
  fbl::RefPtr<zx_device_t> dev_ref(dev);
  internal::ContextForApi()->DeviceSuspendReply(dev_ref, status, out_state);
}

__EXPORT void device_resume_reply(zx_device_t* dev, zx_status_t status, uint8_t out_power_state,
                                  uint32_t out_perf_state) {
  ZX_DEBUG_ASSERT_MSG(dev && dev->magic == DEV_MAGIC, "Dev pointer '%p' is not a real device", dev);
  fbl::AutoLock lock(&internal::ContextForApi()->api_lock());
  fbl::RefPtr<zx_device_t> dev_ref(dev);
  internal::ContextForApi()->DeviceResumeReply(dev_ref, status, out_power_state, out_perf_state);
}

__EXPORT zx_status_t device_get_profile(zx_device_t* dev, uint32_t priority, const char* name,
                                        zx_handle_t* out_profile) {
  if (dev) {
    ZX_DEBUG_ASSERT_MSG(dev && dev->magic == DEV_MAGIC, "Dev pointer '%p' is not a real device",
                        dev);
  }

  return internal::get_scheduler_profile(priority, name, out_profile);
}

__EXPORT zx_status_t device_get_deadline_profile(zx_device_t* device, uint64_t capacity,
                                                 uint64_t deadline, uint64_t period,
                                                 const char* name, zx_handle_t* out_profile) {
  if (device) {
    ZX_DEBUG_ASSERT_MSG(device && device->magic == DEV_MAGIC,
                        "Dev pointer '%p' is not a real device", device);
  }
  return internal::get_scheduler_deadline_profile(capacity, deadline, period, name, out_profile);
}

__EXPORT zx_status_t device_set_profile_by_role(zx_device_t* device, zx_handle_t thread,
                                                const char* role, size_t role_size) {
  if (device) {
    ZX_DEBUG_ASSERT_MSG(device && device->magic == DEV_MAGIC,
                        "Dev pointer '%p' is not a real device", device);
  }
  return internal::set_scheduler_profile_by_role(thread, role, role_size);
}

struct GenericProtocol {
  const void* ops;
  void* ctx;
};

__EXPORT zx_status_t device_get_protocol(const zx_device_t* dev, uint32_t proto_id, void* out) {
  ZX_DEBUG_ASSERT_MSG(dev && dev->magic == DEV_MAGIC, "Dev pointer '%p' is not a real device", dev);
  auto proto = static_cast<GenericProtocol*>(out);
  if (dev->ops()->get_protocol) {
    return dev->ops()->get_protocol(dev->ctx(), proto_id, out);
  }
  if ((proto_id == dev->protocol_id()) && (dev->protocol_ops() != nullptr)) {
    proto->ops = dev->protocol_ops();
    proto->ctx = dev->ctx();
    return ZX_OK;
  }
  return ZX_ERR_NOT_SUPPORTED;
}

__EXPORT zx_status_t device_open_protocol_session_multibindable(const zx_device_t* dev,
                                                                uint32_t proto_id, void* out) {
  ZX_DEBUG_ASSERT_MSG(dev && dev->magic == DEV_MAGIC, "Dev pointer '%p' is not a real device", dev);
  if (dev->ops()->open_protocol_session_multibindable) {
    return dev->ops()->open_protocol_session_multibindable(dev->ctx(), proto_id, out);
  }
  return ZX_ERR_NOT_SUPPORTED;
}

__EXPORT zx_status_t device_close_protocol_session_multibindable(const zx_device_t* dev,
                                                                 void* proto) {
  ZX_DEBUG_ASSERT_MSG(dev && dev->magic == DEV_MAGIC, "Dev pointer '%p' is not a real device", dev);
  if (dev->ops()->close_protocol_session_multibindable) {
    return dev->ops()->close_protocol_session_multibindable(dev->ctx(), proto);
  }
  return ZX_ERR_NOT_SUPPORTED;
}

__EXPORT zx_off_t device_get_size(zx_device_t* dev) {
  ZX_DEBUG_ASSERT_MSG(dev && dev->magic == DEV_MAGIC, "Dev pointer '%p' is not a real device", dev);
  return dev->GetSizeOp();
}

__EXPORT zx_status_t device_service_connect(zx_device_t* dev, const char* service_name,
                                            fdf_handle_t channel) {
  ZX_DEBUG_ASSERT_MSG(dev && dev->magic == DEV_MAGIC, "Dev pointer '%p' is not a real device", dev);
  if (dev->ops()->service_connect) {
    return dev->ServiceConnectOp(service_name, channel);
  }
  return ZX_ERR_NOT_SUPPORTED;
}

__EXPORT zx_status_t device_connect_runtime_protocol(zx_device_t* dev, const char* service_name,
                                                     const char* protocol_name,
                                                     fdf_handle_t request) {
  ZX_DEBUG_ASSERT_MSG(dev && dev->magic == DEV_MAGIC, "Dev pointer '%p' is not a real device", dev);
  auto& outgoing = dev->runtime_outgoing_dir();
  if (!outgoing) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx::channel client_token, server_token;
  auto status = zx::channel::create(0, &client_token, &server_token);
  if (status != ZX_OK) {
    return status;
  }
  status = fdf::ProtocolConnect(std::move(client_token), fdf::Channel(request));
  if (status != ZX_OK) {
    return status;
  }

  fbl::StringBuffer<fuchsia_io::wire::kMaxPath> path;
  // We use "default" as the service instance, as that's what we expect the parent driver
  // to rename it to.
  path.AppendPrintf("svc/%s/default/%s", service_name, protocol_name);
  return fdio_service_connect_at(outgoing.channel().get(), path.c_str(), server_token.release());
}

// LibDriver Misc Interfaces

// Please do not use get_root_resource() in new code. See fxbug.dev/31358.
__EXPORT zx_handle_t get_root_resource() {
  return internal::ContextForApi()->root_resource().get();
}

__EXPORT zx_status_t load_firmware_from_driver(zx_driver_t* drv, zx_device_t* dev, const char* path,
                                               zx_handle_t* fw, size_t* size) {
  ZX_DEBUG_ASSERT_MSG(dev && dev->magic == DEV_MAGIC, "Dev pointer '%p' is not a real device", dev);
  fbl::AutoLock lock(&internal::ContextForApi()->api_lock());
  fbl::RefPtr<zx_device_t> dev_ref(dev);
  // TODO(bwb): Can we propogate zx::vmo further up?
  return internal::ContextForApi()->LoadFirmware(drv, dev_ref, path, fw, size);
}

__EXPORT void load_firmware_async_from_driver(zx_driver_t* drv, zx_device_t* dev, const char* path,
                                              load_firmware_callback_t callback, void* ctx) {
  ZX_DEBUG_ASSERT_MSG(dev && dev->magic == DEV_MAGIC, "Dev pointer '%p' is not a real device", dev);
  fbl::AutoLock lock(&internal::ContextForApi()->api_lock());
  fbl::RefPtr<zx_device_t> dev_ref(dev);
  internal::ContextForApi()->LoadFirmwareAsync(drv, dev_ref, path, callback, ctx);
}

// Interface Used by DevHost RPC Layer

zx_status_t device_bind(const fbl::RefPtr<zx_device_t>& dev, const char* drv_libname) {
  fbl::AutoLock lock(&internal::ContextForApi()->api_lock());
  return internal::ContextForApi()->DeviceBind(dev, drv_libname);
}

zx_status_t device_unbind(const fbl::RefPtr<zx_device_t>& dev) {
  fbl::AutoLock lock(&internal::ContextForApi()->api_lock());
  return internal::ContextForApi()->DeviceUnbind(dev);
}

zx_status_t device_schedule_remove(const fbl::RefPtr<zx_device_t>& dev, bool unbind_self) {
  fbl::AutoLock lock(&internal::ContextForApi()->api_lock());
  return internal::ContextForApi()->ScheduleRemove(dev, unbind_self);
}

zx_status_t device_schedule_unbind_children(const fbl::RefPtr<zx_device_t>& dev) {
  fbl::AutoLock lock(&internal::ContextForApi()->api_lock());
  return internal::ContextForApi()->ScheduleUnbindChildren(dev);
}

zx_status_t device_open(const fbl::RefPtr<zx_device_t>& dev, fbl::RefPtr<zx_device_t>* out,
                        uint32_t flags) {
  fbl::AutoLock lock(&internal::ContextForApi()->api_lock());
  return internal::ContextForApi()->DeviceOpen(dev, out, flags);
}

// This function is intended to consume the reference produced by device_open()
zx_status_t device_close(fbl::RefPtr<zx_device_t> dev, uint32_t flags) {
  fbl::AutoLock lock(&internal::ContextForApi()->api_lock());
  return internal::ContextForApi()->DeviceClose(std::move(dev), flags);
}

__EXPORT zx_status_t device_get_metadata(zx_device_t* dev, uint32_t type, void* buf, size_t buflen,
                                         size_t* actual) {
  ZX_DEBUG_ASSERT_MSG(dev && dev->magic == DEV_MAGIC, "Dev pointer '%p' is not a real device", dev);
  fbl::AutoLock lock(&internal::ContextForApi()->api_lock());
  auto dev_ref = fbl::RefPtr(dev);
  return internal::ContextForApi()->GetMetadata(dev_ref, type, buf, buflen, actual);
}

__EXPORT zx_status_t device_get_metadata_size(zx_device_t* dev, uint32_t type, size_t* out_size) {
  ZX_DEBUG_ASSERT_MSG(dev && dev->magic == DEV_MAGIC, "Dev pointer '%p' is not a real device", dev);
  fbl::AutoLock lock(&internal::ContextForApi()->api_lock());
  auto dev_ref = fbl::RefPtr(dev);
  return internal::ContextForApi()->GetMetadataSize(dev_ref, type, out_size);
}

__EXPORT zx_status_t device_add_metadata(zx_device_t* dev, uint32_t type, const void* data,
                                         size_t length) {
  ZX_DEBUG_ASSERT_MSG(dev && dev->magic == DEV_MAGIC, "Dev pointer '%p' is not a real device", dev);
  fbl::AutoLock lock(&internal::ContextForApi()->api_lock());
  auto dev_ref = fbl::RefPtr(dev);
  return internal::ContextForApi()->AddMetadata(dev_ref, type, data, length);
}

__EXPORT zx_status_t device_add_composite(zx_device_t* dev, const char* name,
                                          const composite_device_desc_t* comp_desc) {
  ZX_DEBUG_ASSERT_MSG(dev && dev->magic == DEV_MAGIC, "Dev pointer '%p' is not a real device", dev);
  fbl::AutoLock lock(&internal::ContextForApi()->api_lock());
  auto dev_ref = fbl::RefPtr(dev);
  return internal::ContextForApi()->DeviceAddComposite(dev_ref, name, comp_desc);
}

__EXPORT bool driver_log_severity_enabled_internal(const zx_driver_t* drv, fx_log_severity_t flag) {
  if (drv != nullptr) {
    fbl::AutoLock lock(&internal::ContextForApi()->api_lock());
    return fx_logger_get_min_severity(drv->logger()) <= flag;
  } else {
    // If we have been invoked outside of the context of a driver, return true.
    // Typically, this is due to being run within a test.
    return true;
  }
}

__EXPORT zx_status_t driver_log_set_tags_internal(const zx_driver_t* drv, const char* const* tags,
                                                  size_t num_tags) {
  if (drv != nullptr) {
    fbl::AutoLock lock(&internal::ContextForApi()->api_lock());
    return drv->ReconfigureLogger(cpp20::span(tags, num_tags));
  } else {
    return ZX_ERR_INVALID_ARGS;
  }
}

__EXPORT void driver_logvf_internal(const zx_driver_t* drv, fx_log_severity_t flag, const char* tag,
                                    const char* file, int line, const char* msg, va_list args) {
  if (drv != nullptr && flag != DDK_LOG_SERIAL) {
    fbl::AutoLock lock(&internal::ContextForApi()->api_lock());
    fx_logger_logvf_with_source(drv->logger(), flag, tag, file, line, msg, args);
  } else {
    // If we have been invoked outside of the context of a driver, or if |flag|
    // is DDK_LOG_SERIAL, use vfprintf.
    vfprintf(stderr, msg, args);
    fputc('\n', stderr);
  }
}

__EXPORT void driver_logf_internal(const zx_driver_t* drv, fx_log_severity_t flag, const char* tag,
                                   const char* file, int line, const char* msg, ...) {
  va_list args;
  va_start(args, msg);
  driver_logvf_internal(drv, flag, tag, file, line, msg, args);
  va_end(args);
}

__EXPORT void device_fidl_transaction_take_ownership(fidl_txn_t* txn, device_fidl_txn_t* new_txn) {
  auto fidl_txn = FromDdkInternalTransaction(ddk::internal::Transaction::FromTxn(txn));

  ZX_ASSERT_MSG(std::holds_alternative<fidl::Transaction*>(fidl_txn),
                "Can only take ownership of transaction once\n");

  auto result = std::get<fidl::Transaction*>(fidl_txn)->TakeOwnership();
  auto new_ddk_txn = MakeDdkInternalTransaction(std::move(result));
  *new_txn = *new_ddk_txn.DeviceFidlTxn();
}

__EXPORT uint32_t device_get_fragment_count(zx_device_t* dev) {
  ZX_DEBUG_ASSERT_MSG(dev && dev->magic == DEV_MAGIC, "Dev pointer '%p' is not a real device", dev);
  if (!dev->is_composite()) {
    return 0;
  }
  return dev->composite()->GetFragmentCount();
}

__EXPORT void device_get_fragments(zx_device_t* dev, composite_device_fragment_t* comp_list,
                                   size_t comp_count, size_t* comp_actual) {
  ZX_DEBUG_ASSERT_MSG(dev && dev->magic == DEV_MAGIC, "Dev pointer '%p' is not a real device", dev);
  if (!dev->is_composite()) {
    ZX_DEBUG_ASSERT(comp_actual != nullptr);
    *comp_actual = 0;
    return;
  }
  return dev->composite()->GetFragments(comp_list, comp_count, comp_actual);
}

__EXPORT zx_status_t device_get_fragment_protocol(zx_device_t* dev, const char* name,
                                                  uint32_t proto_id, void* out) {
  ZX_DEBUG_ASSERT_MSG(dev && dev->magic, "Dev pointer '%p' is not a real device", dev);
  if (!dev->is_composite()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_device_t* fragment;
  if (!dev->composite()->GetFragment(name, &fragment)) {
    return ZX_ERR_NOT_FOUND;
  }
  return device_get_protocol(fragment, proto_id, out);
}

__EXPORT zx_status_t device_get_fragment_metadata(zx_device_t* dev, const char* name, uint32_t type,
                                                  void* buf, size_t buflen, size_t* actual) {
  ZX_DEBUG_ASSERT_MSG(dev && dev->magic == DEV_MAGIC, "Dev pointer '%p' is not a real device", dev);
  if (!dev->is_composite()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_device_t* fragment;
  if (!dev->composite()->GetFragment(name, &fragment)) {
    return ZX_ERR_NOT_FOUND;
  }
  return device_get_metadata(fragment, type, buf, buflen, actual);
}

__EXPORT zx_status_t device_connect_fidl_protocol(zx_device_t* device, const char* protocol_name,
                                                  zx_handle_t request) {
  ZX_DEBUG_ASSERT_MSG(device && device->magic == DEV_MAGIC, "Dev pointer '%p' is not a real device",
                      device);
  if (!device->is_proxy()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return device->proxy()->ConnectToProtocol(protocol_name, zx::channel(request)).status_value();
}

__EXPORT zx_status_t device_connect_fragment_fidl_protocol(zx_device_t* device,
                                                           const char* fragment_name,
                                                           const char* protocol_name,
                                                           zx_handle_t request) {
  ZX_DEBUG_ASSERT_MSG(device && device->magic == DEV_MAGIC, "Dev pointer '%p' is not a real device",
                      device);
  if (!device->is_composite()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_device_t* fragment;
  if (!device->composite()->GetFragment(fragment_name, &fragment)) {
    return ZX_ERR_NOT_FOUND;
  }
  return device_connect_fidl_protocol(fragment, protocol_name, request);
}

__EXPORT zx_status_t device_get_variable(zx_device_t* device, const char* name, char* out,
                                         size_t out_size, size_t* size_actual) {
  if (device) {
    ZX_DEBUG_ASSERT_MSG(device && device->magic == DEV_MAGIC,
                        "Dev pointer '%p' is not a real device", device);
  }
  char* variable = getenv(name);
  if (variable == nullptr) {
    return ZX_ERR_NOT_FOUND;
  }
  size_t len = strlen(variable);
  if (size_actual) {
    *size_actual = len;
  }
  if (len > out_size) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  strncpy(out, variable, out_size);
  return ZX_OK;
}

__EXPORT zx_status_t device_add_group(zx_device_t* dev, const char* name,
                                      const device_group_desc_t* group_desc) {
  ZX_DEBUG_ASSERT_MSG(dev && dev->magic == DEV_MAGIC, "Dev pointer '%p' is not a real device", dev);
  fbl::AutoLock lock(&internal::ContextForApi()->api_lock());
  auto dev_ref = fbl::RefPtr(dev);
  return internal::ContextForApi()->DeviceAddGroup(dev_ref, name, group_desc);
}

__EXPORT zx_status_t device_connect_fidl_protocol2(zx_device_t* device, const char* service_name,
                                                   const char* protocol_name, zx_handle_t request) {
  ZX_DEBUG_ASSERT_MSG(device && device->magic == DEV_MAGIC, "Dev pointer '%p' is not a real device",
                      device);
  if (!device->is_proxy()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return device->proxy()
      ->ConnectToProtocol(service_name, protocol_name, zx::channel(request))
      .status_value();
}

__EXPORT zx_status_t device_connect_fragment_fidl_protocol2(zx_device_t* device,
                                                            const char* fragment_name,
                                                            const char* service_name,
                                                            const char* protocol_name,
                                                            zx_handle_t request) {
  ZX_DEBUG_ASSERT_MSG(device && device->magic == DEV_MAGIC, "Dev pointer '%p' is not a real device",
                      device);
  if (!device->is_composite()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_device_t* fragment;
  if (!device->composite()->GetFragment(fragment_name, &fragment)) {
    return ZX_ERR_NOT_FOUND;
  }
  return device_connect_fidl_protocol2(fragment, service_name, protocol_name, request);
}
