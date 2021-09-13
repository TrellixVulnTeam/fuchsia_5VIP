// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/sshd-host/service.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fuchsia/boot/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/fdio/spawn.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <memory>
#include <vector>

#include <fbl/string_printf.h>
#include <fbl/unique_fd.h>

#include "src/lib/fsl/tasks/fd_waiter.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/sys/lib/chrealm/chrealm.h"

const auto kSshdPath = "/pkg/bin/sshd";
const char* kSshdArgv[] = {kSshdPath, "-ie", "-f", "/config/data/sshd_config", nullptr};

namespace sshd_host {
zx_status_t provision_authorized_keys_from_bootloader_file(
    std::shared_ptr<sys::ServiceDirectory> service_directory) {
  zx_status_t status;
  fuchsia::boot::ItemsSyncPtr boot_items;
  status = service_directory->Connect(boot_items.NewRequest());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Provisioning keys from boot item: failed to connect to boot items service: "
                   << zx_status_get_string(status);
    return status;
  }

  zx::vmo vmo;
  status = boot_items->GetBootloaderFile(kAuthorizedKeysBootloaderFileName, &vmo);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Provisioning keys from boot item: GetBootloaderFile failed with: "
                   << zx_status_get_string(status);
    return status;
  }

  if (!vmo.is_valid()) {
    FX_LOGS(INFO) << "Provisioning keys from boot item: bootloader file not found: "
                  << kAuthorizedKeysBootloaderFileName;
    return ZX_ERR_NOT_FOUND;
  }

  uint64_t size;
  status = vmo.get_property(ZX_PROP_VMO_CONTENT_SIZE, &size, sizeof(size));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Provisioning keys from boot item: unable to get file size: "
                   << zx_status_get_string(status);
    return status;
  }

  auto buffer = std::make_unique<uint8_t[]>(size);

  status = vmo.read(buffer.get(), 0, size);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Provisioning keys from boot item: failed to read file: "
                   << zx_status_get_string(status);
    return status;
  }

  if (mkdir(kSshDirectory, 0700) && errno != EEXIST) {
    FX_LOGS(ERROR) << "Provisioning keys from boot item: failed to create directory: "
                   << kSshDirectory << " Error: " << strerror(errno);
    return ZX_ERR_IO;
  }

  fbl::unique_fd kfd(open(kAuthorizedKeysPath, O_CREAT | O_EXCL | O_WRONLY));
  if (!kfd) {
    FX_LOGS(ERROR) << "Provisioning keys from boot item: open failed: " << kAuthorizedKeysPath
                   << " error: " << strerror(errno);
    return errno == EEXIST ? ZX_ERR_ALREADY_EXISTS : ZX_ERR_IO;
  }

  if (write(kfd.get(), buffer.get(), size) != static_cast<ssize_t>(size)) {
    FX_LOGS(ERROR) << "Provisioning keys from boot item: write failed: " << strerror(errno);
    return ZX_ERR_IO;
  }

  fsync(kfd.get());

  if (close(kfd.release())) {
    FX_LOGS(ERROR) << "Provisioning keys from boot item: close failed: " << strerror(errno);
    return ZX_ERR_IO;
  }

  FX_LOGS(INFO) << "Provisioning keys from boot item: authorized_keys provisioned";
  return ZX_OK;
}

zx_status_t make_child_job(const zx::job& parent, std::string name, zx::job* job) {
  zx_status_t s;
  if ((s = zx::job::create(parent, 0, job)) != ZX_OK) {
    FX_PLOGS(ERROR, s) << "Failed to create child job; parent = " << parent.get();
    return s;
  }

  if ((s = job->set_property(ZX_PROP_NAME, name.data(), name.size())) != ZX_OK) {
    FX_PLOGS(ERROR, s) << "Failed to set name of child job; job = " << job->get();
    return s;
  }
  if ((s = job->replace(kChildJobRights, job)) != ZX_OK) {
    FX_PLOGS(ERROR, s) << "Failed to set rights on child job; job = " << job->get();
    return s;
  }

  return ZX_OK;
}

Service::Service(uint16_t port) : port_(port) {
  sock_ = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
  if (sock_ < 0) {
    FX_LOGS(ERROR) << "Failed to create socket: " << strerror(errno);
    exit(1);
  }

  const struct sockaddr_in6 addr {
    .sin6_family = AF_INET6, .sin6_port = htons(port_), .sin6_addr = in6addr_any,
  };
  if (bind(sock_, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) < 0) {
    FX_LOGS(ERROR) << "Failed to bind to " << port_ << ": " << strerror(errno);
    exit(1);
  }

  FX_SLOG(INFO, "listen() for inbound SSH connections", "tag", "sshd-host", "port", (int)port_);
  if (listen(sock_, 10) < 0) {
    FX_LOGS(ERROR) << "Failed to listen: " << strerror(errno);
    exit(1);
  }

  std::string job_name = fxl::StringPrintf("tcp:%d", port);
  if (make_child_job(*zx::job::default_job(), job_name, &job_) != ZX_OK) {
    exit(1);
  }

  Wait();
}

Service::~Service() {
  for (auto& waiter : process_waiters_) {
    zx_status_t s;
    if ((s = zx_task_kill(waiter->object())) != ZX_OK) {
      FX_PLOGS(ERROR, s) << "Failed kill child task";
    }
    if ((s = zx_handle_close(waiter->object())) != ZX_OK) {
      FX_PLOGS(ERROR, s) << "Failed close child handle";
    }
  }
}

void Service::Wait() {
  waiter_.Wait(
      [this](zx_status_t /*success*/, uint32_t /*events*/) {
        struct sockaddr_in6 peer_addr {};
        socklen_t peer_addr_len = sizeof(peer_addr);
        FX_SLOG(INFO, "Waiting for next connection", "tag", "sshd-host");
        int conn = accept(sock_, reinterpret_cast<struct sockaddr*>(&peer_addr), &peer_addr_len);
        if (conn < 0) {
          if (errno == EPIPE) {
            FX_LOGS(ERROR) << "The netstack died. Terminating.";
            exit(1);
          } else {
            FX_LOGS(ERROR) << "Failed to accept: " << strerror(errno);
            // Wait for another connection.
            Wait();
          }
          return;
        }
        std::string peer_name = "unknown";
        char host[32];
        char port[16];
        if (getnameinfo(reinterpret_cast<struct sockaddr*>(&peer_addr), peer_addr_len, host,
                        sizeof(host), port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
          peer_name = fxl::StringPrintf("%s:%s", host, port);
        }
        Launch(conn, peer_name);
        Wait();
      },
      sock_, POLLIN);
}

void Service::Launch(int conn, const std::string& peer_name) {
  FX_SLOG(INFO, "accepted connection", "tag", "sshd-host", "remote", peer_name.c_str());
  // Create a new job to run the child in.
  zx::job child_job;

  if (make_child_job(job_, peer_name, &child_job) != ZX_OK) {
    shutdown(conn, SHUT_RDWR);
    close(conn);
    FX_LOGS(ERROR) << "Child job creation failed, connection closed";
    return;
  }

  // Launch process with chrealm so that it gets /svc of sys realm
  const std::vector<fdio_spawn_action_t> actions{
      // Transfer the socket as stdin and stdout
      {.action = FDIO_SPAWN_ACTION_CLONE_FD, .fd = {.local_fd = conn, .target_fd = STDIN_FILENO}},
      {.action = FDIO_SPAWN_ACTION_TRANSFER_FD,
       .fd = {.local_fd = conn, .target_fd = STDOUT_FILENO}},
      // Clone this process' stderr.
      {.action = FDIO_SPAWN_ACTION_CLONE_FD,
       .fd = {.local_fd = STDERR_FILENO, .target_fd = STDERR_FILENO}},
  };
  constexpr uint32_t kSpawnFlags =
      FDIO_SPAWN_CLONE_JOB | FDIO_SPAWN_DEFAULT_LDSVC | FDIO_SPAWN_CLONE_UTC_CLOCK;
  zx::process process;
  std::string error;
  zx_status_t status =
      chrealm::SpawnBinaryInRealmAsync("/hub", kSshdArgv, child_job.get(), kSpawnFlags, actions,
                                       process.reset_and_get_address(), &error);
  if (status < 0) {
    shutdown(conn, SHUT_RDWR);
    close(conn);
    FX_LOGS(ERROR) << "Error from chrealm: " << error;
    return;
  }

  std::unique_ptr<async::Wait> waiter =
      std::make_unique<async::Wait>(process.get(), ZX_PROCESS_TERMINATED);
  waiter->set_handler([this, process = std::move(process), job = std::move(child_job)](
                          async_dispatcher_t*, async::Wait*, zx_status_t /*status*/,
                          const zx_packet_signal_t* /*signal*/) mutable {
    ProcessTerminated(std::move(process), std::move(job));
  });
  waiter->Begin(async_get_default_dispatcher());
  process_waiters_.push_back(std::move(waiter));
}

void Service::ProcessTerminated(zx::process process, zx::job job) {
  zx_status_t s;

  // Kill the process and the job.
  if ((s = process.kill()) != ZX_OK) {
    FX_PLOGS(ERROR, s) << "Failed to kill child process";
  }
  if ((s = job.kill()) != ZX_OK) {
    FX_PLOGS(ERROR, s) << "Failed to kill child job";
  }

  // Find the waiter.
  auto i = std::find_if(
      process_waiters_.begin(), process_waiters_.end(),
      [&process](const std::unique_ptr<async::Wait>& w) { return w->object() == process.get(); });
  // And remove it.
  if (i != process_waiters_.end()) {
    process_waiters_.erase(i);
  }
}
}  // namespace sshd_host
