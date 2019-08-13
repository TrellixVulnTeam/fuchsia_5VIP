// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/active_page_manager_container.h"

#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/function.h>

#include <string>
#include <utility>

#include "src/ledger/bin/app/active_page_manager.h"
#include "src/ledger/bin/app/page_usage_listener.h"
#include "src/ledger/bin/app/types.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/lib/fxl/logging.h"

namespace ledger {

ActivePageManagerContainer::ActivePageManagerContainer(
    std::string ledger_name, storage::PageId page_id,
    std::vector<PageUsageListener*> page_usage_listeners)
    : ledger_name_(std::move(ledger_name)),
      page_id_(std::move(page_id)),
      page_usage_listeners_(std::move(page_usage_listeners)),
      weak_factory_(this) {
  token_manager_.set_on_empty([this] { OnInternallyUnused(); });
}

ActivePageManagerContainer::~ActivePageManagerContainer() = default;

void ActivePageManagerContainer::set_on_empty(fit::closure on_empty_callback) {
  on_empty_callback_ = std::move(on_empty_callback);
}

void ActivePageManagerContainer::BindPage(fidl::InterfaceRequest<Page> page_request,
                                          fit::function<void(Status)> callback) {
  if (!has_external_requests_) {
    has_external_requests_ = true;
    for (const auto& page_usage_listener : page_usage_listeners_) {
      page_usage_listener->OnExternallyUsed(ledger_name_, page_id_);
    }
  }

  if (status_ != Status::OK) {
    callback(status_);
    return;
  }
  auto page_impl = std::make_unique<PageImpl>(page_id_, std::move(page_request));
  if (active_page_manager_) {
    active_page_manager_->AddPageImpl(std::move(page_impl), std::move(callback));
    return;
  }
  page_impls_.emplace_back(std::move(page_impl), std::move(callback));
}

void ActivePageManagerContainer::NewInternalRequest(
    fit::function<void(Status, ExpiringToken, ActivePageManager*)> callback) {
  if (status_ != Status::OK) {
    callback(status_, fit::defer<fit::closure>([] {}), nullptr);
    return;
  }

  if (active_page_manager_) {
    if (token_manager_.IsEmpty()) {
      for (PageUsageListener* page_usage_listener : page_usage_listeners_) {
        page_usage_listener->OnInternallyUsed(ledger_name_, page_id_);
      }
    }
    callback(status_, token_manager_.CreateToken(), active_page_manager_.get());
    return;
  }

  internal_request_callbacks_.push_back(std::move(callback));
}

void ActivePageManagerContainer::SetActivePageManager(
    Status status, std::unique_ptr<ActivePageManager> active_page_manager) {
  TRACE_DURATION("ledger", "active_page_manager_container_set_page_manager");

  FXL_DCHECK(!active_page_manager_is_set_);
  FXL_DCHECK((status != Status::OK) == !active_page_manager);
  FXL_DCHECK(token_manager_.IsEmpty());
  status_ = status;
  active_page_manager_ = std::move(active_page_manager);
  active_page_manager_is_set_ = true;

  for (auto& [page_impl, callback] : page_impls_) {
    if (active_page_manager_) {
      active_page_manager_->AddPageImpl(std::move(page_impl), std::move(callback));
    } else {
      callback(status_);
    }
  }
  page_impls_.clear();

  if (!internal_request_callbacks_.empty()) {
    if (!active_page_manager_) {
      for (auto& internal_request_callback : internal_request_callbacks_) {
        internal_request_callback(status_, ExpiringToken(), nullptr);
      }
    } else {
      for (PageUsageListener* page_usage_listener : page_usage_listeners_) {
        page_usage_listener->OnInternallyUsed(ledger_name_, page_id_);
      }
      for (auto& internal_request_callback : internal_request_callbacks_) {
        internal_request_callback(status_, token_manager_.CreateToken(),
                                  active_page_manager_.get());
      }
    }
    internal_request_callbacks_.clear();
  }

  if (active_page_manager_) {
    active_page_manager_->set_on_empty(
        [this] { OnExternallyUnused(/*conditionally_check_empty=*/true); });
  } else {
    OnExternallyUnused(/*conditionally_check_empty=*/false);
  }
}

bool ActivePageManagerContainer::PageConnectionIsOpen() {
  return (active_page_manager_ && !active_page_manager_->IsEmpty()) || !page_impls_.empty();
}

void ActivePageManagerContainer::OnExternallyUnused(bool conditionally_check_empty) {
  if (has_external_requests_) {
    auto weak_this = weak_factory_.GetWeakPtr();
    for (PageUsageListener* page_usage_listener : page_usage_listeners_) {
      // This might delete the ActivePageManagerContainer object.
      page_usage_listener->OnExternallyUnused(ledger_name_, page_id_);
      if (!weak_this) {
        return;
      }
    }
    has_external_requests_ = false;
    if (conditionally_check_empty) {
      CheckEmpty();
      return;
    }
  }
  CheckEmpty();
}

void ActivePageManagerContainer::OnInternallyUnused() {
  auto weak_this = weak_factory_.GetWeakPtr();
  for (PageUsageListener* page_usage_listener : page_usage_listeners_) {
    // This might delete the ActivePageManagerContainer object.
    page_usage_listener->OnInternallyUnused(ledger_name_, page_id_);
    if (!weak_this) {
      return;
    }
  }
  CheckEmpty();
}

void ActivePageManagerContainer::CheckEmpty() {
  // The ActivePageManagerContainer is not considered empty until
  // |SetActivePageManager| has been called.
  if (on_empty_callback_ && !has_external_requests_ && token_manager_.IsEmpty() &&
      active_page_manager_is_set_ && (!active_page_manager_ || active_page_manager_->IsEmpty())) {
    on_empty_callback_();
  }
}

}  // namespace ledger
