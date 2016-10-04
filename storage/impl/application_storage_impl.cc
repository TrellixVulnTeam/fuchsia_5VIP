// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/impl/application_storage_impl.h"

#include "apps/ledger/glue/crypto/base64.h"
#include "apps/ledger/storage/impl/page_storage_impl.h"
#include "lib/ftl/files/directory.h"
#include "lib/ftl/files/path.h"
#include "lib/ftl/logging.h"

namespace storage {

ApplicationStorageImpl::ApplicationStorageImpl(
    ftl::RefPtr<ftl::TaskRunner> task_runner,
    std::string storage_dir)
    : task_runner_(std::move(task_runner)), storage_dir_(storage_dir) {}

ApplicationStorageImpl::~ApplicationStorageImpl() {}

std::unique_ptr<PageStorage> ApplicationStorageImpl::CreatePageStorage(
    const PageId& page_id) {
  std::string path = GetPathFor(page_id);
  if (!files::CreateDirectory(path)) {
    FTL_LOG(ERROR) << "Failed to create the storage directory in " << path;
    return nullptr;
  }
  return std::unique_ptr<PageStorage>(
      new PageStorageImpl(GetPathFor(page_id), page_id));
}

void ApplicationStorageImpl::GetPageStorage(
    const PageId& page_id,
    const std::function<void(std::unique_ptr<PageStorage>)>& callback) {
  std::string path = GetPathFor(page_id);
  if (files::IsDirectory(path)) {
    task_runner_->PostTask([this, callback, page_id]() {
      callback(std::unique_ptr<PageStorage>(
          new PageStorageImpl(GetPathFor(page_id), page_id)));
    });
    return;
  }
  // TODO(nellyv): Maybe the page exists but is not synchronized, yet. We need
  // to check in the cloud.
  task_runner_->PostTask([callback]() { callback(nullptr); });
}

bool ApplicationStorageImpl::DeletePageStorage(const PageId& page_id) {
  // TODO(nellyv): We need to synchronize the page deletion with the cloud.
  std::string path = GetPathFor(page_id);
  if (!files::IsDirectory(path)) {
    return false;
  }
  if (!files::DeletePath(path, true)) {
    FTL_LOG(ERROR) << "Unable to delete: " << path;
    return false;
  }
  return true;
}

std::string ApplicationStorageImpl::GetPathFor(const PageId& page_id) {
  std::string encoded;
  glue::Base64Encode(page_id, &encoded);
  return storage_dir_ + "/" + encoded;
}

}  // namespace storage
