// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/trace_event/internal/trace_writer.h"

#include <thread>
#include <vector>

#include "apps/tracing/lib/trace_event/internal/trace_reader.h"
#include "gtest/gtest.h"

namespace tracing {
namespace {

template <size_t size_of_memory_in_bytes>
struct TracingControllingFixture : public ::testing::Test {
  TracingControllingFixture() {
    tracing::internal::StartTracing(memory_, sizeof(memory_), {""});
  }

  ~TracingControllingFixture() { tracing::internal::StopTracing(); }

  void PrintTrace() {
    internal::MemoryInputReader input_reader(memory_, sizeof(memory_));
    internal::RecordPrinter printer;
    internal::TraceReader trace_reader;
    trace_reader.VisitEachRecord(input_reader, printer);
  }

  char memory_[size_of_memory_in_bytes] = {0};
};

TEST(FieldTest, GetSet) {
  uint64_t value(0);

  internal::Field<0, 0>::Set(value, uint8_t(1));
  internal::Field<1, 1>::Set(value, uint8_t(1));
  internal::Field<2, 2>::Set(value, uint8_t(1));
  internal::Field<3, 3>::Set(value, uint8_t(1));
  internal::Field<4, 4>::Set(value, uint8_t(1));
  internal::Field<5, 5>::Set(value, uint8_t(1));
  internal::Field<6, 6>::Set(value, uint8_t(1));
  internal::Field<7, 7>::Set(value, uint8_t(1));

  EXPECT_EQ(uint8_t(-1), value);
  value = 0;
  internal::Field<0, 2>::Set(value, uint8_t(7));
  EXPECT_EQ(uint8_t(7), value);
  internal::Field<0, 2>::Set(value, uint8_t(0));
  EXPECT_EQ(uint8_t(0), value);
}

using NamePoolTest = TracingControllingFixture<1024>;

TEST_F(NamePoolTest, RegistrationAndRetrieval) {
  const char* t1 = "test";
  const char* t2 = "test";
  const char* t3 = "different";
  auto ref1 = tracing::internal::RegisterString(t1);
  auto ref2 = tracing::internal::RegisterString(t2);
  auto ref3 = tracing::internal::RegisterString(t3);
  EXPECT_TRUE(ref1.encoded == ref2.encoded);
  EXPECT_TRUE(ref1.encoded != ref3.encoded);
  EXPECT_EQ(ref1.encoded, tracing::internal::RegisterString(t1).encoded);
  EXPECT_EQ(ref2.encoded, tracing::internal::RegisterString(t2).encoded);
  EXPECT_EQ(ref3.encoded, tracing::internal::RegisterString(t3).encoded);

  PrintTrace();
}

using BulkNamePoolTest = TracingControllingFixture<1024 * 1024>;

TEST_F(BulkNamePoolTest, BulkRegistrationAndRetrieval) {
  std::map<char*, tracing::internal::StringRef> ids;

  for (size_t i = 1; i < 4095; i++) {
    char* s = static_cast<char*>(malloc(1));
    EXPECT_NE(0, (ids[s] = tracing::internal::RegisterString(s)).encoded);
  }

  for (const auto& pair : ids) {
    EXPECT_EQ(pair.second.encoded,
              tracing::internal::RegisterString(pair.first).encoded);
    free(pair.first);
  }
}

using RegisterThreadTest = TracingControllingFixture<10124>;

TEST_F(RegisterThreadTest, Registration) {
  EXPECT_NE(0, tracing::internal::RegisterCurrentThread().index);
  PrintTrace();
}

TEST_F(RegisterThreadTest, RegistrationForMultipleThreads) {
  EXPECT_NE(0, tracing::internal::RegisterCurrentThread().index);

  std::vector<std::thread> threads;
  for (unsigned int i = 0; i < 10; i++)
    threads.emplace_back([]() {
      EXPECT_NE(0, tracing::internal::RegisterCurrentThread().index);
    });

  for (auto& thread : threads) {
    if (thread.joinable())
      thread.join();
  }

  PrintTrace();
}

using WriterTest = TracingControllingFixture<1024 * 1024>;

TEST_F(WriterTest, EventRecording) {
  internal::TraceDurationBegin("name", "cat");
  internal::TraceDurationEnd("name", "cat");
  internal::TraceAsyncBegin("name", "cat", 42);
  internal::TraceAsyncInstant("name", "cat", 42);
  internal::TraceAsyncEnd("name", "cat", 42);

  PrintTrace();
}

TEST_F(WriterTest, EventRecordingMultiThreaded) {
  std::vector<std::thread> threads;
  for (size_t i = 0; i < 10; i++)
    threads.emplace_back([]() {
      internal::TraceDurationBegin("name", "cat");
      internal::TraceDurationEnd("name", "cat");
      internal::TraceAsyncBegin("name", "cat", 42);
      internal::TraceAsyncInstant("name", "cat", 42);
      internal::TraceAsyncEnd("name", "cat", 42);
    });

  for (auto& thread : threads)
    if (thread.joinable())
      thread.join();

  PrintTrace();
}

TEST_F(WriterTest, EventRecordingWithArguments) {
  int i = 0;

  internal::TraceDurationBegin(
      "name", "cat", internal::MakeArgument("int32", int32_t(42)),
      internal::MakeArgument("int64", int64_t(-42)),
      internal::MakeArgument("double", 42.),
      internal::MakeArgument("cstring", "constant"),
      internal::MakeArgument("dstring", std::string("dynamic")),
      internal::MakeArgument("pointer", &i),
      internal::MakeArgument("koid", internal::Koid(1 << 10)));

  internal::TraceDurationEnd(
      "name", "cat", internal::MakeArgument("int32", int32_t(42)),
      internal::MakeArgument("int64", int64_t(-42)),
      internal::MakeArgument("double", 42.),
      internal::MakeArgument("cstring", "constant"),
      internal::MakeArgument("dstring", std::string("dynamic")),
      internal::MakeArgument("pointer", &i),
      internal::MakeArgument("koid", internal::Koid(1 << 10)));

  internal::TraceAsyncBegin(
      "name", "cat", 42, internal::MakeArgument("int32", int32_t(42)),
      internal::MakeArgument("int64", int64_t(-42)),
      internal::MakeArgument("double", 42.),
      internal::MakeArgument("cstring", "constant"),
      internal::MakeArgument("dstring", std::string("dynamic")),
      internal::MakeArgument("pointer", &i),
      internal::MakeArgument("koid", internal::Koid(1 << 10)));

  internal::TraceAsyncInstant(
      "name", "cat", 42, internal::MakeArgument("int32", int32_t(42)),
      internal::MakeArgument("int64", int64_t(-42)),
      internal::MakeArgument("double", 42.),
      internal::MakeArgument("cstring", "constant"),
      internal::MakeArgument("dstring", std::string("dynamic")),
      internal::MakeArgument("pointer", &i),
      internal::MakeArgument("koid", internal::Koid(1 << 10)));

  internal::TraceAsyncEnd(
      "name", "cat", 42, internal::MakeArgument("int32", int32_t(42)),
      internal::MakeArgument("int64", int64_t(-42)),
      internal::MakeArgument("double", 42.),
      internal::MakeArgument("cstring", "constant"),
      internal::MakeArgument("dstring", std::string("dynamic")),
      internal::MakeArgument("pointer", &i),
      internal::MakeArgument("koid", internal::Koid(1 << 10)));

  PrintTrace();
}

TEST_F(WriterTest, EventRecordingWithArgumentsMultiThreaded_DISABLED) {
  std::vector<std::thread> threads;

  for (size_t i = 0; i < 10; i++) {
    threads.emplace_back([]() {
      int i = 0;

      internal::TraceDurationBegin(
          "name", "cat", internal::MakeArgument("int32", int32_t(42)),
          internal::MakeArgument("int64", int64_t(-42)),
          internal::MakeArgument("double", 42.),
          internal::MakeArgument("cstring", "constant"),
          internal::MakeArgument("dstring", std::string("dynamic")),
          internal::MakeArgument("pointer", &i),
          internal::MakeArgument("koid", internal::Koid(1 << 10)));

      internal::TraceDurationEnd(
          "name", "cat", internal::MakeArgument("int32", int32_t(42)),
          internal::MakeArgument("int64", int64_t(-42)),
          internal::MakeArgument("double", 42.),
          internal::MakeArgument("cstring", "constant"),
          internal::MakeArgument("dstring", std::string("dynamic")),
          internal::MakeArgument("pointer", &i),
          internal::MakeArgument("koid", internal::Koid(1 << 10)));

      internal::TraceAsyncBegin(
          "name", "cat", 42, internal::MakeArgument("int32", int32_t(42)),
          internal::MakeArgument("int64", int64_t(-42)),
          internal::MakeArgument("double", 42.),
          internal::MakeArgument("cstring", "constant"),
          internal::MakeArgument("dstring", std::string("dynamic")),
          internal::MakeArgument("pointer", &i),
          internal::MakeArgument("koid", internal::Koid(1 << 10)));

      internal::TraceAsyncInstant(
          "name", "cat", 42, internal::MakeArgument("int32", int32_t(42)),
          internal::MakeArgument("int64", int64_t(-42)),
          internal::MakeArgument("double", 42.),
          internal::MakeArgument("cstring", "constant"),
          internal::MakeArgument("dstring", std::string("dynamic")),
          internal::MakeArgument("pointer", &i),
          internal::MakeArgument("koid", internal::Koid(1 << 10)));

      internal::TraceAsyncEnd(
          "name", "cat", 42, internal::MakeArgument("int32", int32_t(42)),
          internal::MakeArgument("int64", int64_t(-42)),
          internal::MakeArgument("double", 42.),
          internal::MakeArgument("cstring", "constant"),
          internal::MakeArgument("dstring", std::string("dynamic")),
          internal::MakeArgument("pointer", &i),
          internal::MakeArgument("koid", internal::Koid(1 << 10)));
    });
  }

  for (auto& thread : threads)
    if (thread.joinable())
      thread.join();

  PrintTrace();
}

}  // namespace
}  // namespace trace_event
