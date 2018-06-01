// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "gtest/gtest.h"
#include "lib/async/cpp/future.h"
#include "peridot/bin/user_runner/story_runner/story_storage.h"
#include "peridot/lib/ledger_client/page_id.h"
#include "peridot/lib/testing/test_with_ledger.h"

using fuchsia::modular::ModuleData;
using fuchsia::modular::ModuleDataPtr;

namespace modular {
namespace {

class StoryStorageTest : public testing::TestWithLedger {
 protected:
  std::unique_ptr<StoryStorage> CreateStorage(std::string page_id) {
    return std::make_unique<StoryStorage>(ledger_client(), MakePageId(page_id));
  }
};

ModuleData Clone(const ModuleData& data) {
  ModuleData dup;
  data.Clone(&dup);
  return dup;
}

TEST_F(StoryStorageTest, ReadModuleData_NonexistentModule) {
  auto storage = CreateStorage("page");

  bool read_done{};
  fidl::VectorPtr<fidl::StringPtr> path;
  path.push_back("a");
  storage->ReadModuleData(path)->Then([&](ModuleDataPtr data) {
    read_done = true;
    ASSERT_FALSE(data);
  });

  RunLoopUntil([&] { return read_done; });
}

TEST_F(StoryStorageTest, ReadAllModuleData_Empty) {
  auto storage = CreateStorage("page");

  bool read_done{};
  fidl::VectorPtr<ModuleData> all_module_data;
  storage->ReadAllModuleData()->Then([&](fidl::VectorPtr<ModuleData> data) {
    read_done = true;
    all_module_data = std::move(data);
  });

  RunLoopUntil([&] { return read_done; });
  ASSERT_TRUE(all_module_data);
  EXPECT_EQ(0u, all_module_data->size());
}

TEST_F(StoryStorageTest, WriteReadModuleData) {
  // Write and then read some ModuleData entries. We expect to get the same data
  // back.
  auto storage = CreateStorage("page");

  bool got_notification{};
  storage->set_on_module_data_updated(
      [&](ModuleData) { got_notification = true; });

  ModuleData module_data1;
  module_data1.module_url = "url1";
  module_data1.module_path.push_back("path1");
  storage->WriteModuleData(Clone(module_data1));

  ModuleData module_data2;
  module_data2.module_url = "url2";
  module_data2.module_path.push_back("path2");
  storage->WriteModuleData(Clone(module_data2));

  // We don't need to explicitly wait on WriteModuleData() because the
  // implementation: 1) serializes all storage operations and 2) guarantees the
  // WriteModuleData() action is finished only once the data has been written.
  ModuleData read_data1;
  bool read1_done{};
  storage->ReadModuleData(module_data1.module_path)
      ->Then([&](ModuleDataPtr data) {
        read1_done = true;
        ASSERT_TRUE(data);
        read_data1 = std::move(*data);
      });

  ModuleData read_data2;
  bool read2_done{};
  storage->ReadModuleData(module_data2.module_path)
      ->Then([&](ModuleDataPtr data) {
        read2_done = true;
        ASSERT_TRUE(data);
        read_data2 = std::move(*data);
      });

  RunLoopUntil([&] { return read1_done && read2_done; });
  EXPECT_EQ(module_data1, read_data1);
  EXPECT_EQ(module_data2, read_data2);

  // Read the same data back with ReadAllModuleData().
  fidl::VectorPtr<ModuleData> all_module_data;
  storage->ReadAllModuleData()->Then([&](fidl::VectorPtr<ModuleData> data) {
    all_module_data = std::move(data);
  });
  RunLoopUntil([&] { return !!all_module_data; });
  EXPECT_EQ(2u, all_module_data->size());
  EXPECT_EQ(module_data1, all_module_data->at(0));
  EXPECT_EQ(module_data2, all_module_data->at(1));

  // At no time should we have gotten a notification about ModuleData records
  // from this storage instance.
  EXPECT_FALSE(got_notification);
}

TEST_F(StoryStorageTest, UpdateModuleData) {
  // Call UpdateModuleData() on a record that doesn't exist yet.
  auto storage = CreateStorage("page");

  // We're going to observe changes on another storage instance, which
  // simulates another device.
  auto other_storage = CreateStorage("page");
  bool got_notification{};
  ModuleData notified_module_data;
  other_storage->set_on_module_data_updated([&](ModuleData data) {
    got_notification = true;
    notified_module_data = std::move(data);
  });

  fidl::VectorPtr<fidl::StringPtr> path;
  path.push_back("a");

  // Case 1: Don't mutate anything.
  bool update_done{};
  storage
      ->UpdateModuleData(path, [](ModuleDataPtr* ptr) { EXPECT_FALSE(*ptr); })
      ->Then([&] { update_done = true; });
  RunLoopUntil([&] { return update_done; });

  bool read_done{};
  ModuleData read_data;
  storage->ReadModuleData(path)->Then([&](ModuleDataPtr data) {
    read_done = true;
    EXPECT_FALSE(data);
  });
  RunLoopUntil([&] { return read_done; });
  // Since nothing changed, we should not have seen a notification.
  EXPECT_FALSE(got_notification);

  // Case 2: Initialize an otherwise empty record.
  update_done = false;
  storage
      ->UpdateModuleData(path,
                         [&](ModuleDataPtr* ptr) {
                           EXPECT_FALSE(*ptr);

                           *ptr = ModuleData::New();
                           (*ptr)->module_path = path.Clone();
                           (*ptr)->module_url = "foobar";
                         })
      ->Then([&] { update_done = true; });
  RunLoopUntil([&] { return update_done; });

  read_done = false;
  storage->ReadModuleData(path)->Then([&](ModuleDataPtr data) {
    read_done = true;
    ASSERT_TRUE(data);
    EXPECT_EQ(path, data->module_path);
    EXPECT_EQ("foobar", data->module_url);
  });
  RunLoopUntil([&] { return read_done; });
  // Now something changed, so we should see a notification.
  EXPECT_TRUE(got_notification);
  EXPECT_EQ("foobar", notified_module_data.module_url);

  // Case 3: Leave alone an existing record.
  got_notification = false;
  storage->UpdateModuleData(path,
                            [&](ModuleDataPtr* ptr) { EXPECT_TRUE(*ptr); });

  read_done = false;
  storage->ReadModuleData(path)->Then([&](ModuleDataPtr data) {
    read_done = true;
    ASSERT_TRUE(data);
    EXPECT_EQ("foobar", data->module_url);
  });
  RunLoopUntil([&] { return read_done; });
  // Now something changed, so we should see a notification.
  EXPECT_FALSE(got_notification);

  // Case 4: Mutate an existing record.
  storage->UpdateModuleData(path, [&](ModuleDataPtr* ptr) {
    EXPECT_TRUE(*ptr);
    (*ptr)->module_url = "baz";
  });

  read_done = false;
  storage->ReadModuleData(path)->Then([&](ModuleDataPtr data) {
    read_done = true;
    ASSERT_TRUE(data);
    EXPECT_EQ("baz", data->module_url);
  });
  RunLoopUntil([&] { return read_done; });
  // Now something changed, so we should see a notification.
  EXPECT_TRUE(got_notification);
  EXPECT_EQ("baz", notified_module_data.module_url);
}

}  // namespace
}  // namespace modular
