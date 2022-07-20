// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/basemgr/session_context_impl.h"

#include <lib/syslog/cpp/macros.h>

#include "src/modular/bin/basemgr/sessions.h"
#include "src/modular/lib/common/async_holder.h"
#include "src/modular/lib/common/teardown.h"
#include "src/modular/lib/modular_config/modular_config_constants.h"
#include "src/modular/lib/pseudo_dir/pseudo_dir_utils.h"

namespace modular {

SessionContextImpl::SessionContextImpl(
    fuchsia::sys::Launcher* const launcher,
    fuchsia::modular::session::AppConfig sessionmgr_app_config,
    const modular::ModularConfigAccessor* const config_accessor,
    std::optional<fuchsia::ui::views::ViewToken> view_token,
    std::optional<fuchsia::ui::views::ViewCreationToken> view_creation_token,
    scenic::ViewRefPair view_ref_pair, fuchsia::sys::ServiceList v2_services_for_sessionmgr,
    fidl::InterfaceRequest<fuchsia::io::Directory> svc_from_v1_sessionmgr_request,
    OnSessionShutdownCallback on_session_shutdown)
    : session_context_binding_(this),
      on_session_shutdown_(std::move(on_session_shutdown)),
      weak_factory_(this) {
  // Exactly one of the tokens must be present.
  // If view_token is present, use Gfx.  Otherwise, use Flatland.
  FX_CHECK(view_token.has_value() != view_creation_token.has_value());

  sessions::ReportNewSessionToCobalt();

  // Create a PseudoDir containing startup.config. This directory will be injected into
  // sessionmgr's namespace and sessionmgr will read its configurations from there.
  auto config_namespace = CreateAndServeConfigNamespace(config_accessor->GetConfigAsJsonString());

  // Launch Sessionmgr in the current environment.
  sessionmgr_app_ = std::make_unique<AppClient<fuchsia::modular::Lifecycle>>(
      launcher, std::move(sessionmgr_app_config), sessions::kSessionDirectoryPath,
      /*additional_services=*/nullptr, std::move(config_namespace));

  // Initialize the Sessionmgr service.
  sessionmgr_app_->services().ConnectToService(sessionmgr_.NewRequest());
  if (view_creation_token.has_value()) {
    sessionmgr_->Initialize(sessions::kSessionId, session_context_binding_.NewBinding(),
                            std::move(v2_services_for_sessionmgr),
                            std::move(svc_from_v1_sessionmgr_request),
                            std::move(*view_creation_token));
  } else {
    sessionmgr_->InitializeLegacy(sessions::kSessionId, session_context_binding_.NewBinding(),
                                  std::move(v2_services_for_sessionmgr),
                                  std::move(svc_from_v1_sessionmgr_request), std::move(*view_token),
                                  std::move(view_ref_pair.control_ref),
                                  std::move(view_ref_pair.view_ref));
  }

  sessionmgr_app_->SetAppErrorHandler([weak_this = weak_factory_.GetWeakPtr()] {
    if (!weak_this) {
      return;
    }
    FX_LOGS(ERROR) << "Sessionmgr seems to have crashed unexpectedly. "
                   << "Shutting down.";
    weak_this->Shutdown(ShutDownReason::CRITICAL_FAILURE, [] {});
  });
}

fuchsia::sys::FlatNamespacePtr SessionContextImpl::CreateAndServeConfigNamespace(
    std::string config_contents) {
  zx::channel config_request_channel;
  zx::channel config_dir_channel;

  FX_CHECK(zx::channel::create(0u, &config_request_channel, &config_dir_channel) == ZX_OK);

  // Host the config file in a PseudoDir
  config_dir_ = modular::MakeFilePathWithContents(modular_config::kStartupConfigFilePath,
                                                  std::move(config_contents));
  config_dir_->Serve(fuchsia::io::OpenFlags::RIGHT_READABLE, std::move(config_request_channel));

  auto flat_namespace = std::make_unique<fuchsia::sys::FlatNamespace>();
  flat_namespace->paths.push_back(modular_config::kOverriddenConfigDir);
  flat_namespace->directories.push_back(std::move(config_dir_channel));

  return flat_namespace;
}

void SessionContextImpl::Shutdown(ShutDownReason reason, fit::function<void()> callback) {
  shutdown_callbacks_.push_back(std::move(callback));
  if (shutdown_callbacks_.size() > 1) {
    FX_LOGS(INFO) << "fuchsia::modular::internal::SessionContext::Shutdown() "
                     "already called, queuing callback while shutdown is in progress.";
    return;
  }

  FX_LOGS(INFO) << "Shutting down sessionmgr.";

  // Close the SessionContext channel to ensure no more requests from the
  // channel are processed.
  session_context_binding_.Unbind();

  sessionmgr_app_->Teardown(kSessionmgrTimeout, [weak_this = weak_factory_.GetWeakPtr(), reason] {
    if (!weak_this) {
      return;
    }

    auto shutdown_callbacks = std::move(weak_this->shutdown_callbacks_);
    auto on_session_shutdown = std::move(weak_this->on_session_shutdown_);
    on_session_shutdown(reason);

    for (const auto& callback : shutdown_callbacks) {
      callback();
    }
  });
}

void SessionContextImpl::Restart() {
  Shutdown(ShutDownReason::CLIENT_REQUEST, [] {});
}

void SessionContextImpl::RestartDueToCriticalFailure() {
  Shutdown(ShutDownReason::CRITICAL_FAILURE, [] {});
}

}  // namespace modular
