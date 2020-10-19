// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/PlatformManager.h>

#include "generic_platform_manager_impl_fuchsia.ipp"
#include "configuration_manager_delegate_impl.h"
#include "connectivity_manager_delegate_impl.h"
#include "thread_stack_manager_delegate_impl.h"
// clang-format on

#include "src/connectivity/weave/weavestack/app.h"

using nl::Weave::DeviceLayer::ConfigurationManagerDelegateImpl;
using nl::Weave::DeviceLayer::ConfigurationMgrImpl;
using nl::Weave::DeviceLayer::ConnectivityManagerDelegateImpl;
using nl::Weave::DeviceLayer::ConnectivityMgrImpl;
using nl::Weave::DeviceLayer::ThreadStackManagerDelegateImpl;
using nl::Weave::DeviceLayer::ThreadStackMgrImpl;

int main(void) {
  weavestack::App app;
  zx_status_t status;

  ConfigurationMgrImpl().SetDelegate(std::make_unique<ConfigurationManagerDelegateImpl>());
  ConnectivityMgrImpl().SetDelegate(std::make_unique<ConnectivityManagerDelegateImpl>());
  ThreadStackMgrImpl().SetDelegate(std::make_unique<ThreadStackManagerDelegateImpl>());

  status = app.Init();
  if (status != ZX_OK) {
    return status;
  }

  status = app.Run();
  if (status != ZX_OK) {
    return status;
  }

  return ZX_OK;
}
