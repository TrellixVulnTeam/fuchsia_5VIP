// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/defer.h>
#include <lib/sync/completion.h>
#include <lib/syslog/global.h>

#include <gtest/gtest.h>

#include "device_interface.h"
#include "log.h"
#include "session.h"
#include "src/lib/testing/predicates/status.h"
#include "test_util.h"

// Enable timeouts only to test things locally, committed code should not use timeouts.
#define ENABLE_TIMEOUTS 0

#if ENABLE_TIMEOUTS
#define TEST_DEADLINE zx::deadline_after(zx::msec(5000))
#else
#define TEST_DEADLINE zx::time::infinite()
#endif

namespace network {
namespace testing {

using netdev::wire::RxFlags;

class NetworkDeviceTest : public ::testing::Test {
 public:
  void SetUp() override {
    fx_logger_config_t log_cfg = {
        .min_severity = FX_LOG_TRACE,
        .console_fd = dup(STDOUT_FILENO),
        .log_service_channel = ZX_HANDLE_INVALID,
        .tags = nullptr,
        .num_tags = 0,
    };
    fx_log_reconfigure(&log_cfg);
  }

  void TearDown() override { DiscardDeviceSync(); }

  void DiscardDeviceSync() {
    if (device_) {
      sync_completion_t completer;
      device_->Teardown([&completer, this]() {
        LOG_TRACE("Test: Teardown complete");
        device_ = nullptr;
        sync_completion_signal(&completer);
      });
      ASSERT_OK(sync_completion_wait_deadline(&completer, TEST_DEADLINE.get()));
    }
  }

  static zx_status_t WaitEvents(const zx::event& events, zx_signals_t signals, zx::time deadline) {
    zx_status_t status = events.wait_one(signals, deadline, nullptr);
    if (status == ZX_OK) {
      events.signal(signals, 0);
    }
    return status;
  }

  [[nodiscard]] zx_status_t WaitStart(zx::time deadline = TEST_DEADLINE) {
    return WaitEvents(impl_.events(), kEventStart, deadline);
  }

  [[nodiscard]] zx_status_t WaitStop(zx::time deadline = TEST_DEADLINE) {
    return WaitEvents(impl_.events(), kEventStop, deadline);
  }

  [[nodiscard]] zx_status_t WaitSessionStarted(zx::time deadline = TEST_DEADLINE) {
    return WaitEvents(impl_.events(), kEventSessionStarted, deadline);
  }

  [[nodiscard]] zx_status_t WaitTx(zx::time deadline = TEST_DEADLINE) {
    return WaitEvents(impl_.events(), kEventTx, deadline);
  }

  [[nodiscard]] zx_status_t WaitRxAvailable(zx::time deadline = TEST_DEADLINE) {
    return WaitEvents(impl_.events(), kEventRxAvailable, deadline);
  }

  [[nodiscard]] zx_status_t WaitPortActiveChanged(const FakeNetworkPortImpl& port,
                                                  zx::time deadline = TEST_DEADLINE) {
    return WaitEvents(port.events(), kEventPortActiveChanged, deadline);
  }

  async_dispatcher_t* dispatcher() {
    if (!loop_) {
      loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNeverAttachToThread);
      EXPECT_OK(loop_->StartThread("messenger-loop", nullptr));
    }
    return loop_->dispatcher();
  }

  fidl::WireSyncClient<netdev::Device> OpenConnection() {
    zx::status endpoints = fidl::CreateEndpoints<netdev::Device>();
    EXPECT_OK(endpoints.status_value());
    auto [client_end, server_end] = std::move(*endpoints);
    EXPECT_OK(device_->Bind(std::move(server_end)));
    return fidl::BindSyncClient(std::move(client_end));
  }

  zx_status_t CreateDevice() {
    if (device_) {
      return ZX_ERR_INTERNAL;
    }
    zx::status device = impl_.CreateChild(dispatcher());
    if (device.is_ok()) {
      device_ = std::move(device.value());
    }
    return device.status_value();
  }

  zx_status_t OpenSession(TestSession* session,
                          netdev::wire::SessionFlags flags = netdev::wire::SessionFlags::kPrimary,
                          uint16_t num_descriptors = kDefaultDescriptorCount,
                          uint64_t buffer_size = kDefaultBufferLength,
                          fidl::VectorView<netdev::wire::FrameType> frame_types =
                              fidl::VectorView<netdev::wire::FrameType>()) {
    // automatically increment to test_session_(a, b, c, etc...)
    char session_name[] = "test_session_a";
    session_name[strlen(session_name) - 1] = static_cast<char>('a' + session_counter_);
    session_counter_++;

    fidl::WireSyncClient connection = OpenConnection();
    return session->Open(connection, session_name, flags, num_descriptors, buffer_size,
                         std::move(frame_types));
  }

 protected:
  FakeNetworkDeviceImpl impl_;
  std::unique_ptr<async::Loop> loop_;
  int8_t session_counter_ = 0;
  std::unique_ptr<NetworkDeviceInterface> device_;
};

void PrintVec(const std::string& name, const std::vector<uint8_t>& vec) {
  printf("Vec %s: ", name.c_str());
  for (const auto& x : vec) {
    printf("%02X ", x);
  }
  printf("\n");
}

TEST_F(NetworkDeviceTest, CanCreate) { ASSERT_OK(CreateDevice()); }

TEST_F(NetworkDeviceTest, GetInfo) {
  impl_.info().min_rx_buffer_length = 2048;
  impl_.info().min_tx_buffer_length = 60;
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
  fidl::WireResult rsp = connection.GetInfo();
  ASSERT_OK(rsp.status());
  auto& info = rsp.value().info;
  EXPECT_EQ(info.tx_depth, impl_.info().tx_depth * 2);
  EXPECT_EQ(info.rx_depth, impl_.info().rx_depth * 2);
  EXPECT_EQ(info.min_rx_buffer_length, impl_.info().min_rx_buffer_length);
  EXPECT_EQ(info.min_tx_buffer_length, impl_.info().min_tx_buffer_length);
  EXPECT_EQ(info.max_buffer_length, impl_.info().max_buffer_length);
  EXPECT_EQ(info.min_tx_buffer_tail, impl_.info().tx_tail_length);
  EXPECT_EQ(info.min_tx_buffer_head, impl_.info().tx_head_length);
  EXPECT_EQ(info.descriptor_version, NETWORK_DEVICE_DESCRIPTOR_VERSION);
  EXPECT_EQ(info.buffer_alignment, impl_.info().buffer_alignment);
  EXPECT_EQ(info.min_descriptor_length, sizeof(buffer_descriptor_t) / sizeof(uint64_t));
  EXPECT_EQ(info.class_, netdev::wire::DeviceClass::kEthernet);
  EXPECT_EQ(info.tx_accel.count(), impl_.info().tx_accel_count);
  EXPECT_EQ(info.rx_accel.count(), impl_.info().rx_accel_count);

  const auto& port_info = impl_.port0().port_info();
  EXPECT_EQ(info.rx_types.count(), port_info.rx_types_count);
  for (size_t i = 0; i < info.rx_types.count(); i++) {
    EXPECT_EQ(static_cast<uint8_t>(info.rx_types.at(i)), port_info.rx_types_list[i]);
  }
  EXPECT_EQ(info.tx_types.count(), port_info.tx_types_count);
  for (size_t i = 0; i < info.tx_types.count(); i++) {
    EXPECT_EQ(static_cast<uint8_t>(info.tx_types.at(i).type), port_info.tx_types_list[i].type);
    EXPECT_EQ(info.tx_types.at(i).features, port_info.tx_types_list[i].features);
    EXPECT_EQ(static_cast<uint32_t>(info.tx_types.at(i).supported_flags),
              port_info.tx_types_list[i].supported_flags);
  }
}

TEST_F(NetworkDeviceTest, MinReportedBufferAlignment) {
  // Tests that device creation is rejected with an invalid buffer_alignment value.
  impl_.info().buffer_alignment = 0;
  ASSERT_STATUS(CreateDevice(), ZX_ERR_NOT_SUPPORTED);
}

TEST_F(NetworkDeviceTest, InvalidRxThreshold) {
  // Tests that device creation is rejected with an invalid rx_threshold value.
  impl_.info().rx_threshold = impl_.info().rx_depth + 1;
  ASSERT_STATUS(CreateDevice(), ZX_ERR_NOT_SUPPORTED);
}

TEST_F(NetworkDeviceTest, OpenSession) {
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  for (uint16_t i = 0; i < 16; i++) {
    session.ResetDescriptor(i);
    session.SendRx(i);
  }
  session.SetPaused(false);
  ASSERT_OK(WaitStart());
  ASSERT_OK(WaitRxAvailable());
}

TEST_F(NetworkDeviceTest, RxBufferBuild) {
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  session.SetPaused(false);
  ASSERT_OK(WaitStart());
  constexpr size_t kDescTests = 3;
  // send three Rx descriptors:
  // - A simple descriptor with just data length
  // - A descriptor with head and tail removed
  // - A chained descriptor with simple data lengths.
  uint16_t all_descs[kDescTests + 1] = {0, 1, 2};
  session.ResetDescriptor(0);
  auto* desc = session.ResetDescriptor(1);
  desc->head_length = 16;
  desc->tail_length = 32;
  desc->data_length -= desc->head_length + desc->tail_length;
  desc = session.ResetDescriptor(2);
  desc->data_length = 10;
  desc->chain_length = 2;
  desc->nxt = 3;
  desc = session.ResetDescriptor(3);
  desc->data_length = 20;
  desc->chain_length = 1;
  desc->nxt = 4;
  desc = session.ResetDescriptor(4);
  desc->data_length = 30;
  desc->chain_length = 0;
  size_t sent;
  ASSERT_OK(session.SendRx(all_descs, kDescTests, &sent));
  ASSERT_EQ(sent, kDescTests);
  ASSERT_OK(WaitRxAvailable());
  RxReturnTransaction return_session(&impl_);
  // load the buffers from the fake device implementation and check them.
  // We call "pop_back" on the buffer list because network_device feeds Rx buffers in a LIFO order.
  // check first descriptor:
  auto rx = impl_.rx_buffers().pop_back();
  ASSERT_TRUE(rx);
  ASSERT_EQ(rx->buff().data.parts_count, 1u);
  ASSERT_EQ(rx->buff().data.parts_list[0].offset, session.descriptor(0)->offset);
  ASSERT_EQ(rx->buff().data.parts_list[0].length, kDefaultBufferLength);
  rx->return_buffer().length = 64;
  rx->return_buffer().meta.flags = static_cast<uint32_t>(RxFlags::kRxAccel0);
  return_session.Enqueue(std::move(rx));
  // check second descriptor:
  rx = impl_.rx_buffers().pop_back();
  ASSERT_TRUE(rx);
  ASSERT_EQ(rx->buff().data.parts_count, 1u);
  desc = session.descriptor(1);
  ASSERT_EQ(rx->buff().data.parts_list[0].offset, desc->offset + desc->head_length);
  ASSERT_EQ(rx->buff().data.parts_list[0].length,
            kDefaultBufferLength - desc->head_length - desc->tail_length);
  rx->return_buffer().length = 15;
  rx->return_buffer().meta.flags = static_cast<uint32_t>(RxFlags::kRxAccel1);
  return_session.Enqueue(std::move(rx));
  // check third descriptor:
  rx = impl_.rx_buffers().pop_back();
  ASSERT_TRUE(rx);
  ASSERT_EQ(rx->buff().data.parts_count, 3u);
  auto* d0 = session.descriptor(2);
  auto* d1 = session.descriptor(3);
  auto* d2 = session.descriptor(4);
  ASSERT_EQ(rx->buff().data.parts_list[0].offset, d0->offset);
  ASSERT_EQ(rx->buff().data.parts_list[0].length, d0->data_length);
  ASSERT_EQ(rx->buff().data.parts_list[1].offset, d1->offset);
  ASSERT_EQ(rx->buff().data.parts_list[1].length, d1->data_length);
  ASSERT_EQ(rx->buff().data.parts_list[2].offset, d2->offset);
  ASSERT_EQ(rx->buff().data.parts_list[2].length, d2->data_length);
  // set the total length up to a part of the middle buffer:
  rx->return_buffer().length = 25;
  rx->return_buffer().meta.flags = static_cast<uint32_t>(RxFlags::kRxAccel2);
  return_session.Enqueue(std::move(rx));
  // ensure no more rx buffers were actually returned:
  ASSERT_TRUE(impl_.rx_buffers().is_empty());
  // commit the returned buffers
  return_session.Commit();
  // check that all descriptors were returned to the queue:
  size_t read_back;
  ASSERT_OK(session.FetchRx(all_descs, kDescTests + 1, &read_back));
  ASSERT_EQ(read_back, kDescTests);
  EXPECT_EQ(all_descs[0], 0u);
  EXPECT_EQ(all_descs[1], 1u);
  EXPECT_EQ(all_descs[2], 2u);
  // finally check all the stuff that was returned:
  // check returned first descriptor:
  desc = session.descriptor(0);
  EXPECT_EQ(desc->offset, session.canonical_offset(0));
  EXPECT_EQ(desc->chain_length, 0u);
  EXPECT_EQ(desc->inbound_flags, static_cast<uint32_t>(RxFlags::kRxAccel0));
  EXPECT_EQ(desc->head_length, 0u);
  EXPECT_EQ(desc->data_length, 64u);
  EXPECT_EQ(desc->tail_length, 0u);
  // check returned second descriptor:
  desc = session.descriptor(1);
  EXPECT_EQ(desc->offset, session.canonical_offset(1));
  EXPECT_EQ(desc->chain_length, 0u);
  EXPECT_EQ(desc->inbound_flags, static_cast<uint32_t>(RxFlags::kRxAccel1));
  EXPECT_EQ(desc->head_length, 16u);
  EXPECT_EQ(desc->data_length, 15u);
  EXPECT_EQ(desc->tail_length, 32u);
  // check returned third descriptor and the chained ones:
  desc = session.descriptor(2);
  EXPECT_EQ(desc->offset, session.canonical_offset(2));
  EXPECT_EQ(desc->chain_length, 2u);
  EXPECT_EQ(desc->nxt, 3u);
  EXPECT_EQ(desc->inbound_flags, static_cast<uint32_t>(RxFlags::kRxAccel2));
  EXPECT_EQ(desc->head_length, 0u);
  EXPECT_EQ(desc->data_length, 10u);
  EXPECT_EQ(desc->tail_length, 0u);
  desc = session.descriptor(3);
  EXPECT_EQ(desc->offset, session.canonical_offset(3));
  EXPECT_EQ(desc->chain_length, 1u);
  EXPECT_EQ(desc->nxt, 4u);
  EXPECT_EQ(desc->inbound_flags, 0u);
  EXPECT_EQ(desc->head_length, 0u);
  EXPECT_EQ(desc->data_length, 15u);
  EXPECT_EQ(desc->tail_length, 0u);
  desc = session.descriptor(4);
  EXPECT_EQ(desc->offset, session.canonical_offset(4));
  EXPECT_EQ(desc->chain_length, 0u);
  EXPECT_EQ(desc->inbound_flags, 0u);
  EXPECT_EQ(desc->head_length, 0u);
  EXPECT_EQ(desc->data_length, 0u);
  EXPECT_EQ(desc->tail_length, 0u);
}

TEST_F(NetworkDeviceTest, TxBufferBuild) {
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  session.SetPaused(false);
  ASSERT_OK(WaitStart());
  constexpr size_t kDescTests = 3;
  // send three Rx descriptors:
  // - A simple descriptor with just data length
  // - A descriptor with head and tail removed
  // - A chained descriptor with simple data lengths.
  uint16_t all_descs[kDescTests + 1] = {0, 1, 2};
  session.ResetDescriptor(0);
  auto* desc = session.ResetDescriptor(1);
  desc->head_length = 16;
  desc->tail_length = 32;
  desc->data_length -= desc->head_length + desc->tail_length;
  desc = session.ResetDescriptor(2);
  desc->data_length = 10;
  desc->chain_length = 2;
  desc->nxt = 3;
  desc = session.ResetDescriptor(3);
  desc->data_length = 20;
  desc->chain_length = 1;
  desc->nxt = 4;
  desc = session.ResetDescriptor(4);
  desc->data_length = 30;
  desc->chain_length = 0;
  size_t sent;
  ASSERT_OK(session.SendTx(all_descs, kDescTests, &sent));
  ASSERT_EQ(sent, kDescTests);
  ASSERT_OK(WaitTx());
  TxReturnTransaction return_session(&impl_);
  // load the buffers from the fake device implementation and check them.
  auto tx = impl_.tx_buffers().pop_front();
  ASSERT_TRUE(tx);
  ASSERT_EQ(tx->buff().data.parts_count, 1u);
  ASSERT_EQ(tx->buff().data.parts_list[0].offset, session.descriptor(0)->offset);
  ASSERT_EQ(tx->buff().data.parts_list[0].length, kDefaultBufferLength);
  return_session.Enqueue(std::move(tx));
  // check second descriptor:
  tx = impl_.tx_buffers().pop_front();
  ASSERT_TRUE(tx);
  ASSERT_EQ(tx->buff().data.parts_count, 1u);
  desc = session.descriptor(1);
  ASSERT_EQ(tx->buff().data.parts_list[0].offset, desc->offset + desc->head_length);
  ASSERT_EQ(tx->buff().data.parts_list[0].length,
            kDefaultBufferLength - desc->head_length - desc->tail_length);
  tx->set_status(ZX_ERR_UNAVAILABLE);
  return_session.Enqueue(std::move(tx));
  // check third descriptor:
  tx = impl_.tx_buffers().pop_front();
  ASSERT_TRUE(tx);
  ASSERT_EQ(tx->buff().data.parts_count, 3u);
  auto* d0 = session.descriptor(2);
  auto* d1 = session.descriptor(3);
  auto* d2 = session.descriptor(4);
  ASSERT_EQ(tx->buff().data.parts_list[0].offset, d0->offset);
  ASSERT_EQ(tx->buff().data.parts_list[0].length, d0->data_length);
  ASSERT_EQ(tx->buff().data.parts_list[1].offset, d1->offset);
  ASSERT_EQ(tx->buff().data.parts_list[1].length, d1->data_length);
  ASSERT_EQ(tx->buff().data.parts_list[2].offset, d2->offset);
  ASSERT_EQ(tx->buff().data.parts_list[2].length, d2->data_length);
  tx->set_status(ZX_ERR_NOT_SUPPORTED);
  return_session.Enqueue(std::move(tx));
  // ensure no more tx buffers were actually enqueued:
  ASSERT_TRUE(impl_.tx_buffers().is_empty());
  // commit the returned buffers
  return_session.Commit();
  // check that all descriptors were returned to the queue:
  size_t read_back;

  ASSERT_OK(session.FetchTx(all_descs, kDescTests + 1, &read_back));
  ASSERT_EQ(read_back, kDescTests);
  EXPECT_EQ(all_descs[0], 0u);
  EXPECT_EQ(all_descs[1], 1u);
  EXPECT_EQ(all_descs[2], 2u);
  // check the status of the returned descriptors
  desc = session.descriptor(0);
  EXPECT_EQ(desc->return_flags, 0u);
  desc = session.descriptor(1);
  EXPECT_EQ(desc->return_flags,
            static_cast<uint32_t>(netdev::wire::TxReturnFlags::kTxRetError |
                                  netdev::wire::TxReturnFlags::kTxRetNotAvailable));
  desc = session.descriptor(2);
  EXPECT_EQ(desc->return_flags,
            static_cast<uint32_t>(netdev::wire::TxReturnFlags::kTxRetError |
                                  netdev::wire::TxReturnFlags::kTxRetNotSupported));
}

TEST_F(NetworkDeviceTest, SessionEpitaph) {
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitStart());
  ASSERT_OK(session.Close());
  // closing the session should cause a stop:
  ASSERT_OK(WaitStop());
  // wait for epitaph to show up in channel
  ASSERT_OK(session.session().channel().wait_one(ZX_CHANNEL_READABLE, TEST_DEADLINE, nullptr));
  fidl_epitaph_t epitaph;
  uint32_t actual_bytes;
  ASSERT_OK(session.session().channel().read(0, &epitaph, nullptr, sizeof(epitaph), 0,
                                             &actual_bytes, nullptr));
  ASSERT_EQ(actual_bytes, sizeof(epitaph));
  ASSERT_EQ(epitaph.error, ZX_ERR_CANCELED);
  // also the channel must be closed after:
  ASSERT_OK(session.session().channel().wait_one(ZX_CHANNEL_PEER_CLOSED, TEST_DEADLINE, nullptr));
}

TEST_F(NetworkDeviceTest, SessionPauseUnpause) {
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
  TestSession session;
  // pausing and unpausing the session makes the device start and stop:
  ASSERT_OK(OpenSession(&session));
  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitStart());
  ASSERT_OK(session.SetPaused(true));
  ASSERT_OK(WaitStop());
  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitStart());
  ASSERT_OK(session.SetPaused(true));
  ASSERT_OK(WaitStop());
}

TEST_F(NetworkDeviceTest, TwoSessionsTx) {
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
  TestSession session_a;
  ASSERT_OK(OpenSession(&session_a));
  TestSession session_b;
  ASSERT_OK(OpenSession(&session_b));
  session_a.SetPaused(false);
  ASSERT_OK(WaitSessionStarted());
  session_b.SetPaused(false);
  ASSERT_OK(WaitSessionStarted());
  ASSERT_OK(WaitStart());
  // send something from each session, both should succeed:
  std::vector<uint8_t> sent_buff_a({1, 2, 3, 4});
  std::vector<uint8_t> sent_buff_b({5, 6});
  session_a.SendTxData(0, sent_buff_a);
  ASSERT_OK(WaitTx());
  session_b.SendTxData(1, sent_buff_b);
  ASSERT_OK(WaitTx());
  // wait until we have two frames waiting:
  auto buff_a = impl_.tx_buffers().pop_front();
  auto buff_b = impl_.tx_buffers().pop_front();
  std::vector<uint8_t> data_a;
  std::vector<uint8_t> data_b;
  auto vmo_provider = impl_.VmoGetter();
  ASSERT_OK(buff_a->GetData(&data_a, vmo_provider));
  ASSERT_OK(buff_b->GetData(&data_b, vmo_provider));
  // can't rely on ordering here:
  if (data_a.size() != sent_buff_a.size()) {
    std::swap(buff_a, buff_b);
    std::swap(data_a, data_b);
  }
  PrintVec("data_a", data_a);
  PrintVec("data_b", data_b);
  ASSERT_EQ(data_a, sent_buff_a);
  ASSERT_EQ(data_b, sent_buff_b);
  // return both buffers and ensure they get to the correct sessions:
  buff_a->set_status(ZX_OK);
  buff_b->set_status(ZX_ERR_UNAVAILABLE);
  TxReturnTransaction tx_ret(&impl_);
  tx_ret.Enqueue(std::move(buff_a));
  tx_ret.Enqueue(std::move(buff_b));
  tx_ret.Commit();

  uint16_t rd;
  ASSERT_OK(session_a.FetchTx(&rd));
  ASSERT_EQ(rd, 0u);
  ASSERT_OK(session_b.FetchTx(&rd));
  ASSERT_EQ(rd, 1u);
  ASSERT_EQ(session_a.descriptor(0)->return_flags, 0u);
  ASSERT_EQ(session_b.descriptor(1)->return_flags,
            static_cast<uint32_t>(netdev::wire::TxReturnFlags::kTxRetError |
                                  netdev::wire::TxReturnFlags::kTxRetNotAvailable));
}

TEST_F(NetworkDeviceTest, TwoSessionsRx) {
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
  TestSession session_a;
  ASSERT_OK(OpenSession(&session_a));
  TestSession session_b;
  ASSERT_OK(OpenSession(&session_b));
  ASSERT_OK(session_a.SetPaused(false));
  ASSERT_OK(WaitSessionStarted());
  ASSERT_OK(session_b.SetPaused(false));
  ASSERT_OK(WaitSessionStarted());
  ASSERT_OK(WaitStart());
  constexpr uint16_t kBufferCount = 5;
  constexpr size_t kDataLen = 15;
  uint16_t desc_buff[kBufferCount];
  for (uint16_t i = 0; i < kBufferCount; i++) {
    session_a.ResetDescriptor(i);
    session_b.ResetDescriptor(i);
    desc_buff[i] = i;
  }
  ASSERT_OK(session_a.SendRx(desc_buff, kBufferCount, nullptr));
  ASSERT_OK(session_b.SendRx(desc_buff, kBufferCount, nullptr));

  ASSERT_OK(WaitRxAvailable());
  auto vmo_provider = impl_.VmoGetter();
  RxReturnTransaction return_session(&impl_);
  for (uint16_t i = 0; i < kBufferCount; i++) {
    auto buff = impl_.rx_buffers().pop_front();
    std::vector<uint8_t> data(kDataLen, static_cast<uint8_t>(i));
    ASSERT_OK(buff->WriteData(data, vmo_provider));
    return_session.Enqueue(std::move(buff));
  }
  return_session.Commit();

  auto checker = [kBufferCount, kDataLen](TestSession* session) {
    uint16_t descriptors[kBufferCount];
    size_t rd;
    ASSERT_OK(session->FetchRx(descriptors, kBufferCount, &rd));
    ASSERT_EQ(rd, kBufferCount);
    for (uint32_t i = 0; i < kBufferCount; i++) {
      auto* desc = session->descriptor(descriptors[i]);
      ASSERT_EQ(desc->data_length, kDataLen);
      auto* data = session->buffer(desc->offset);
      for (uint32_t j = 0; j < kDataLen; j++) {
        ASSERT_EQ(*data, static_cast<uint8_t>(i));
        data++;
      }
    }
  };
  checker(&session_a);
  checker(&session_b);
}

TEST_F(NetworkDeviceTest, ListenSession) {
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
  TestSession session_a;
  ASSERT_OK(OpenSession(&session_a));
  TestSession session_b;
  ASSERT_OK(OpenSession(&session_b, netdev::wire::SessionFlags::kListenTx));
  ASSERT_OK(session_a.SetPaused(false));
  ASSERT_OK(WaitSessionStarted());
  ASSERT_OK(session_b.SetPaused(false));
  ASSERT_OK(WaitSessionStarted());
  ASSERT_OK(WaitStart());
  // Get an Rx descriptor ready on session b:
  session_b.ResetDescriptor(0);
  ASSERT_OK(session_b.SendRx(0));

  // send data from session a:
  std::vector<uint8_t> send_buff({1, 2, 3, 4});
  session_a.SendTxData(0, send_buff);
  ASSERT_OK(WaitTx());

  uint16_t desc_idx;
  ASSERT_OK(session_b.FetchRx(&desc_idx));
  ASSERT_EQ(desc_idx, 0u);
  auto* desc = session_b.descriptor(0);
  ASSERT_EQ(desc->data_length, send_buff.size());
  auto* data = session_b.buffer(desc->offset);
  ASSERT_EQ(std::basic_string_view(data, send_buff.size()),
            std::basic_string_view(send_buff.data(), send_buff.size()));
}

TEST_F(NetworkDeviceTest, ClosingPrimarySession) {
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
  TestSession session_a;
  ASSERT_OK(OpenSession(&session_a));
  TestSession session_b;
  ASSERT_OK(OpenSession(&session_b));
  ASSERT_OK(session_a.SetPaused(false));
  ASSERT_OK(WaitSessionStarted());
  ASSERT_OK(session_b.SetPaused(false));
  ASSERT_OK(WaitSessionStarted());
  // send one buffer on each session
  auto* d = session_a.ResetDescriptor(0);
  d->data_length = kDefaultBufferLength / 2;
  session_b.ResetDescriptor(1);
  ASSERT_OK(session_a.SendRx(0));
  ASSERT_OK(session_b.SendRx(1));
  ASSERT_OK(WaitRxAvailable());
  // impl_ now owns session_a's RxBuffer
  auto rx_buff = impl_.rx_buffers().pop_front();
  ASSERT_EQ(rx_buff->buff().data.parts_list[0].length, kDefaultBufferLength / 2);
  // let's close session_a, it should not be closed until we return the buffers
  ASSERT_OK(session_a.Close());
  ASSERT_EQ(session_a.session().channel().wait_one(ZX_CHANNEL_PEER_CLOSED,
                                                   zx::deadline_after(zx::msec(20)), nullptr),
            ZX_ERR_TIMED_OUT);
  // and now return data.
  rx_buff->return_buffer().length = 5;
  RxReturnTransaction rx_transaction(&impl_);
  rx_transaction.Enqueue(std::move(rx_buff));
  rx_transaction.Commit();

  // Session a should be closed...
  ASSERT_OK(session_a.WaitClosed(TEST_DEADLINE));
  /// ...and Session b should still receive the data.
  uint16_t desc;
  ASSERT_OK(session_b.FetchRx(&desc));
  ASSERT_EQ(desc, 1u);
  ASSERT_EQ(session_b.descriptor(1)->data_length, 5u);
}

TEST_F(NetworkDeviceTest, DelayedStart) {
  ASSERT_OK(CreateDevice());
  impl_.set_auto_start(false);
  fidl::WireSyncClient connection = OpenConnection();
  TestSession session_a;
  ASSERT_OK(OpenSession(&session_a));
  ASSERT_OK(session_a.SetPaused(false));
  ASSERT_OK(WaitSessionStarted());
  // we're dealing starting the device, so the start signal must've been triggered.
  ASSERT_OK(WaitStart());
  // But we haven't actually called the callback.
  // We should be able to pause and unpause session_a while we're still holding the device.
  // we can send Tx data and it won't reach the device until TriggerStart is called.
  session_a.ResetDescriptor(0);
  ASSERT_OK(session_a.SendTx(0));
  ASSERT_OK(session_a.SetPaused(true));
  ASSERT_OK(session_a.SetPaused(false));
  ASSERT_OK(WaitSessionStarted());
  ASSERT_TRUE(impl_.tx_buffers().is_empty());
  ASSERT_TRUE(impl_.TriggerStart());
  ASSERT_OK(WaitTx());
  ASSERT_FALSE(impl_.tx_buffers().is_empty());
  impl_.ReturnAllTx();

  // pause the session again and wait for stop.
  ASSERT_OK(session_a.SetPaused(true));
  ASSERT_OK(WaitStop());
  // Then unpause and re-pause the session:
  ASSERT_OK(session_a.SetPaused(false));
  ASSERT_OK(WaitSessionStarted());
  ASSERT_OK(WaitStart());
  // Pause the session once again, we haven't called TriggerStart yet.
  ASSERT_OK(session_a.SetPaused(true));

  // As soon as we call TriggerStart, stop must be called, but not before
  ASSERT_STATUS(WaitStop(zx::deadline_after(zx::msec(20))), ZX_ERR_TIMED_OUT);
  ASSERT_TRUE(impl_.TriggerStart());
  ASSERT_OK(WaitStop());
}

TEST_F(NetworkDeviceTest, DelayedStop) {
  ASSERT_OK(CreateDevice());
  impl_.set_auto_stop(false);
  fidl::WireSyncClient connection = OpenConnection();
  TestSession session_a;
  ASSERT_OK(OpenSession(&session_a));
  ASSERT_OK(session_a.SetPaused(false));
  ASSERT_OK(WaitSessionStarted());
  ASSERT_OK(WaitStart());

  ASSERT_OK(session_a.SetPaused(true));
  ASSERT_OK(WaitStop());
  // Unpause the session again, we haven't called TriggerStop yet
  ASSERT_OK(session_a.SetPaused(false));
  ASSERT_OK(WaitSessionStarted());
  // As soon as we call TriggerStop, start must be called, but not before
  ASSERT_STATUS(WaitStart(zx::deadline_after(zx::msec(20))), ZX_ERR_TIMED_OUT);
  ASSERT_TRUE(impl_.TriggerStop());
  ASSERT_OK(WaitStart());

  // With the session running, send down a tx frame and then close the session.
  // The session should NOT be closed until we actually call TriggerStop.
  session_a.ResetDescriptor(0);
  ASSERT_OK(session_a.SendTx(0));
  ASSERT_OK(WaitTx());
  ASSERT_OK(session_a.Close());
  ASSERT_OK(WaitStop());
  // Session must not have been closed yet:
  ASSERT_EQ(session_a.session().channel().wait_one(ZX_CHANNEL_PEER_CLOSED,
                                                   zx::deadline_after(zx::msec(20)), nullptr),
            ZX_ERR_TIMED_OUT);
  ASSERT_TRUE(impl_.TriggerStop());
  ASSERT_OK(session_a.WaitClosed(TEST_DEADLINE));
}

TEST_F(NetworkDeviceTest, ReclaimBuffers) {
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
  TestSession session_a;
  ASSERT_OK(OpenSession(&session_a));
  ASSERT_OK(session_a.SetPaused(false));
  ASSERT_OK(WaitStart());
  session_a.ResetDescriptor(0);
  session_a.ResetDescriptor(1);
  ASSERT_OK(session_a.SendRx(0));
  ASSERT_OK(session_a.SendTx(1));
  ASSERT_OK(WaitTx());
  ASSERT_OK(WaitRxAvailable());
  ASSERT_EQ(impl_.tx_buffers().size_slow(), 1u);
  ASSERT_EQ(impl_.rx_buffers().size_slow(), 1u);
  ASSERT_OK(session_a.SetPaused(true));
  ASSERT_OK(WaitStop());
  impl_.tx_buffers().clear();
  impl_.rx_buffers().clear();

  // check that the tx buffer was reclaimed
  uint16_t desc;
  ASSERT_OK(session_a.FetchTx(&desc));
  ASSERT_EQ(desc, 1u);
  // check that the return flags reflect the error
  ASSERT_EQ(session_a.descriptor(1)->return_flags,
            static_cast<uint32_t>(netdev::wire::TxReturnFlags::kTxRetError |
                                  netdev::wire::TxReturnFlags::kTxRetNotAvailable));

  // Unpause the session again and fetch rx buffers to confirm that the Rx buffer was reclaimed
  ASSERT_OK(session_a.SetPaused(false));
  ASSERT_OK(WaitStart());
  ASSERT_OK(WaitRxAvailable());
  ASSERT_EQ(impl_.rx_buffers().size_slow(), 1u);
}

TEST_F(NetworkDeviceTest, Teardown) {
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
  TestSession session_a;
  ASSERT_OK(OpenSession(&session_a));
  ASSERT_OK(session_a.SetPaused(false));
  ASSERT_OK(WaitSessionStarted());
  TestSession session_b;
  ASSERT_OK(OpenSession(&session_b));
  ASSERT_OK(session_b.SetPaused(false));
  ASSERT_OK(WaitSessionStarted());
  TestSession session_c;
  ASSERT_OK(OpenSession(&session_c));

  DiscardDeviceSync();
  session_a.WaitClosed(TEST_DEADLINE);
  session_b.WaitClosed(TEST_DEADLINE);
  session_c.WaitClosed(TEST_DEADLINE);
}

TEST_F(NetworkDeviceTest, TeardownWithReclaim) {
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
  TestSession session_a;
  ASSERT_OK(OpenSession(&session_a));
  ASSERT_OK(session_a.SetPaused(false));
  ASSERT_OK(WaitStart());
  session_a.ResetDescriptor(0);
  session_a.ResetDescriptor(1);
  ASSERT_OK(session_a.SendRx(0));
  ASSERT_OK(session_a.SendTx(1));
  ASSERT_OK(WaitTx());
  ASSERT_OK(WaitRxAvailable());
  ASSERT_EQ(impl_.tx_buffers().size_slow(), 1u);
  ASSERT_EQ(impl_.rx_buffers().size_slow(), 1u);

  DiscardDeviceSync();
  session_a.WaitClosed(TEST_DEADLINE);
}

TEST_F(NetworkDeviceTest, TxHeadLength) {
  constexpr uint16_t kHeadLength = 16;
  impl_.info().tx_head_length = kHeadLength;
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  ASSERT_OK(session.SetPaused(false));
  session.ZeroVmo();
  auto* desc = session.ResetDescriptor(0);
  desc->head_length = kHeadLength;
  desc->data_length = 1;
  *session.buffer(desc->offset + desc->head_length) = 0xAA;
  desc = session.ResetDescriptor(1);
  desc->head_length = kHeadLength * 2;
  desc->data_length = 1;
  *session.buffer(desc->offset + desc->head_length) = 0xBB;
  uint16_t descs[] = {0, 1};
  size_t sent;
  ASSERT_OK(session.SendTx(descs, 2, &sent));
  ASSERT_EQ(sent, 2u);
  ASSERT_OK(WaitTx());
  auto buffs = impl_.tx_buffers().begin();
  std::vector<uint8_t> data;

  auto vmo_provider = impl_.VmoGetter();
  // check first buffer
  ASSERT_EQ(buffs->buff().head_length, kHeadLength);
  ASSERT_OK(buffs->GetData(&data, vmo_provider));
  ASSERT_EQ(data.size(), kHeadLength + 1u);
  ASSERT_EQ(data[kHeadLength], 0xAA);
  buffs++;
  // check second buffer
  ASSERT_EQ(buffs->buff().head_length, kHeadLength);
  ASSERT_OK(buffs->GetData(&data, vmo_provider));
  ASSERT_EQ(data.size(), kHeadLength + 1u);
  ASSERT_EQ(data[kHeadLength], 0xBB);
  buffs++;
  ASSERT_EQ(buffs, impl_.tx_buffers().end());
}

TEST_F(NetworkDeviceTest, InvalidTxFrameType) {
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitStart());
  auto* desc = session.ResetDescriptor(0);
  desc->frame_type = static_cast<uint8_t>(netdev::wire::FrameType::kIpv4);
  ASSERT_OK(session.SendTx(0));
  // Session should be killed because of contract breach:
  ASSERT_OK(session.WaitClosed(TEST_DEADLINE));
  // We should NOT have received that frame:
  ASSERT_TRUE(impl_.tx_buffers().is_empty());
}

TEST_F(NetworkDeviceTest, RxFrameTypeFilter) {
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitStart());
  session.ResetDescriptor(0);
  ASSERT_OK(session.SendRx(0));
  ASSERT_OK(WaitRxAvailable());
  auto buff = impl_.rx_buffers().pop_front();
  buff->return_buffer().meta.frame_type = static_cast<uint8_t>(netdev::wire::FrameType::kIpv4);
  buff->return_buffer().length = 10;
  RxReturnTransaction rx_transaction(&impl_);
  rx_transaction.Enqueue(std::move(buff));
  rx_transaction.Commit();

  uint16_t ret_desc;
  ASSERT_EQ(session.FetchRx(&ret_desc), ZX_ERR_SHOULD_WAIT);
}

TEST_F(NetworkDeviceTest, ObserveStatus) {
  using netdev::wire::StatusFlags;
  ASSERT_OK(CreateDevice());
  zx::status endpoints = fidl::CreateEndpoints<netdev::StatusWatcher>();
  ASSERT_OK(endpoints.status_value());
  auto [client_end, server_end] = std::move(*endpoints);
  fidl::WireSyncClient watcher = fidl::BindSyncClient(std::move(client_end));
  ASSERT_OK(OpenConnection().GetStatusWatcher(std::move(server_end), 3).status());
  {
    fidl::WireResult result = watcher.WatchStatus();
    ASSERT_OK(result.status());
    ASSERT_EQ(result.value().device_status.mtu(), impl_.port0().status().mtu);
    ASSERT_TRUE(result.value().device_status.flags() & StatusFlags::kOnline);
  }
  // Set offline, then set online (watcher is buffered, we should be able to observe both).
  impl_.SetOnline(false);
  impl_.SetOnline(true);
  {
    fidl::WireResult result = watcher.WatchStatus();
    ASSERT_OK(result.status());
    ASSERT_EQ(result.value().device_status.mtu(), impl_.port0().status().mtu);
    ASSERT_FALSE(result.value().device_status.flags() & StatusFlags::kOnline);
  }
  {
    fidl::WireResult result = watcher.WatchStatus();
    ASSERT_OK(result.status());
    ASSERT_EQ(result.value().device_status.mtu(), impl_.port0().status().mtu);
    ASSERT_TRUE(result.value().device_status.flags() & StatusFlags::kOnline);
  }

  DiscardDeviceSync();

  // Watcher must be closed on teardown.
  ASSERT_OK(watcher.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, TEST_DEADLINE, nullptr));
}

// Test that returning tx buffers in the body of QueueTx is allowed and works.
TEST_F(NetworkDeviceTest, ReturnTxInline) {
  impl_.set_auto_return_tx(true);
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitStart());
  session.ResetDescriptor(0x02);
  ASSERT_OK(session.SendTx(0x02));
  ASSERT_OK(WaitTx());
  uint16_t desc;
  ASSERT_OK(session.FetchTx(&desc));
  EXPECT_EQ(desc, 0x02);
}

// Test that opening a session with unknown Rx types will fail.
TEST_F(NetworkDeviceTest, RejectsInvalidRxTypes) {
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
  TestSession session;
  auto frame_type = netdev::wire::FrameType::kIpv4;
  ASSERT_STATUS(
      OpenSession(&session, netdev::wire::SessionFlags::kPrimary, kDefaultDescriptorCount,
                  kDefaultBufferLength,
                  fidl::VectorView<netdev::wire::FrameType>::FromExternal(&frame_type, 1)),
      ZX_ERR_INVALID_ARGS);
}

// Regression test for session name not respecting fidl::StringView lack of null termination
// character.
TEST_F(NetworkDeviceTest, SessionNameRespectsStringView) {
  ASSERT_OK(CreateDevice());
  // Cast to internal implementation to access methods directly.
  auto* dev = static_cast<internal::DeviceInterface*>(device_.get());

  netdev::wire::SessionInfo info;
  TestSession test_session;
  ASSERT_OK(test_session.Init(kDefaultDescriptorCount, kDefaultBufferLength));
  ASSERT_OK(test_session.GetInfo(&info));

  const char* name_str = "hello world";
  // String view only contains "hello".
  fidl::StringView name = fidl::StringView::FromExternal(name_str, 5u);

  zx::status response = dev->OpenSession(std::move(name), std::move(info));
  ASSERT_OK(response.status_value());

  const auto& session = dev->sessions_unsafe().front();

  ASSERT_STREQ("hello", session.name());
}

TEST_F(NetworkDeviceTest, RejectsSmallRxBuffers) {
  constexpr uint32_t kMinRxLength = 60;
  impl_.info().min_rx_buffer_length = kMinRxLength;
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitStart());
  auto* desc = session.ResetDescriptor(0);
  desc->data_length = kMinRxLength - 1;
  ASSERT_OK(session.SendRx(0));
  // Session should be killed because of contract breach:
  ASSERT_OK(session.WaitClosed(TEST_DEADLINE));
  // We should NOT have received that frame:
  ASSERT_TRUE(impl_.rx_buffers().is_empty());
}

TEST_F(NetworkDeviceTest, RejectsSmallTxBuffers) {
  constexpr uint32_t kMinTxLength = 60;
  impl_.info().min_tx_buffer_length = kMinTxLength;
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitStart());
  auto* desc = session.ResetDescriptor(0);
  desc->data_length = kMinTxLength - 1;
  ASSERT_OK(session.SendTx(0));
  // Session should be killed because of contract breach:
  ASSERT_OK(session.WaitClosed(TEST_DEADLINE));
  // We should NOT have received that frame:
  ASSERT_TRUE(impl_.tx_buffers().is_empty());
}

TEST_F(NetworkDeviceTest, RespectsRxThreshold) {
  constexpr uint64_t kReturnBufferSize = 1;
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
  TestSession session;
  uint16_t descriptor_count = impl_.info().rx_depth * 2;
  ASSERT_OK(OpenSession(&session, netdev::wire::SessionFlags::kPrimary, descriptor_count));

  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitStart());

  std::vector<uint16_t> descriptors;
  descriptors.reserve(descriptor_count);
  for (uint16_t i = 0; i < descriptor_count; i++) {
    session.ResetDescriptor(i);
    descriptors.push_back(i);
  }

  // Fill up to half depth one buffer at a time, waiting for each one to be observed by the device
  // driver implementation. The slow dripping of buffers will force the Rx queue to enter
  // steady-state so we're not racing the return buffer signals with the session started and device
  // started ones.
  uint16_t half_depth = impl_.info().rx_depth / 2;
  for (uint16_t i = 0; i < half_depth; i++) {
    ASSERT_OK(session.SendRx(descriptors[i]));
    ASSERT_OK(WaitRxAvailable());
    ASSERT_EQ(impl_.rx_buffers().size_slow(), i + 1u);
  }
  // Send the rest of the buffers.
  size_t actual;
  ASSERT_OK(
      session.SendRx(descriptors.data() + half_depth, descriptors.size() - half_depth, &actual));
  ASSERT_EQ(actual, descriptors.size() - half_depth);
  ASSERT_OK(WaitRxAvailable());
  ASSERT_EQ(impl_.rx_buffers().size_slow(), impl_.info().rx_depth);

  // Return the maximum number of buffers that we can return without hitting the threshold.
  for (uint16_t i = impl_.info().rx_depth - impl_.info().rx_threshold - 1; i != 0; i--) {
    RxReturnTransaction return_session(&impl_);
    return_session.EnqueueWithSize(impl_.rx_buffers().pop_front(), kReturnBufferSize);
    return_session.Commit();
    // Check that no more buffers are enqueued.
    ASSERT_STATUS(WaitRxAvailable(zx::time::infinite_past()), ZX_ERR_TIMED_OUT)
        << "remaining=" << i;
  }
  // Check again with some time slack for the last buffer.
  ASSERT_STATUS(WaitRxAvailable(zx::deadline_after(zx::msec(10))), ZX_ERR_TIMED_OUT);

  // Return one more buffer to cross the threshold.
  RxReturnTransaction return_session(&impl_);
  return_session.EnqueueWithSize(impl_.rx_buffers().pop_front(), kReturnBufferSize);
  return_session.Commit();
  ASSERT_OK(WaitRxAvailable());
  ASSERT_EQ(impl_.rx_buffers().size_slow(), impl_.info().rx_depth);
}

TEST_F(NetworkDeviceTest, RxQueueIdlesOnPausedSession) {
  ASSERT_OK(CreateDevice());

  struct {
    fbl::Mutex lock;
    std::optional<uint64_t> key __TA_GUARDED(lock);
  } observed_key;

  sync_completion_t completion;

  auto get_next_key = [&observed_key, &completion](zx::duration timeout) -> zx::status<uint64_t> {
    zx_status_t status = sync_completion_wait(&completion, timeout.get());
    fbl::AutoLock l(&observed_key.lock);
    std::optional k = observed_key.key;
    if (status != ZX_OK) {
      // Whenever wait fails, key must not have a value.
      EXPECT_EQ(k, std::nullopt);
      return zx::error(status);
    }
    sync_completion_reset(&completion);
    if (!k.has_value()) {
      return zx::error(ZX_ERR_BAD_STATE);
    }
    uint64_t key = *k;
    observed_key.key.reset();
    return zx::ok(key);
  };

  auto* dev_iface = static_cast<internal::DeviceInterface*>(device_.get());
  dev_iface->evt_rx_queue_packet = [&observed_key, &completion](uint64_t key) {
    fbl::AutoLock l(&observed_key.lock);
    std::optional k = observed_key.key;
    EXPECT_EQ(k, std::nullopt);
    observed_key.key = key;
    sync_completion_signal(&completion);
  };
  auto undo = fit::defer([dev_iface]() {
    // Clear event handler so we don't see any of the teardown.
    dev_iface->evt_rx_queue_packet = nullptr;
  });

  TestSession session;
  ASSERT_OK(OpenSession(&session));

  {
    zx::status key = get_next_key(zx::duration::infinite());
    ASSERT_OK(key.status_value());
    ASSERT_EQ(key.value(), internal::RxQueue::kSessionSwitchKey);
  }

  session.ResetDescriptor(0);
  // Make the FIFO readable.
  ASSERT_OK(session.SendRx(0));
  // It should not trigger any RxQueue events.
  {
    zx::status key = get_next_key(zx::msec(50));
    ASSERT_TRUE(key.is_error()) << "unexpected key value " << key.value();
    ASSERT_STATUS(key.status_value(), ZX_ERR_TIMED_OUT);
  }

  // Kill the session and check that we see a session switch again.
  ASSERT_OK(session.Close());
  {
    zx::status key = get_next_key(zx::duration::infinite());
    ASSERT_OK(key.status_value());
    ASSERT_EQ(key.value(), internal::RxQueue::kSessionSwitchKey);
  }
}

TEST_F(NetworkDeviceTest, RemovingPortCausesSessionToPause) {
  ASSERT_OK(CreateDevice());
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitStart());

  // Removing the port causes the session to pause, which should cause the data plane to stop.
  impl_.client().RemovePort(FakeNetworkDeviceImpl::kPort0);
  ASSERT_OK(WaitStop());
}

TEST_F(NetworkDeviceTest, OnlyReceiveOnSubscribedPorts) {
  ASSERT_OK(CreateDevice());
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitStart());
  std::array<uint16_t, 2> descriptors = {0, 1};

  for (auto desc : descriptors) {
    auto* descriptor = session.ResetDescriptor(desc);
    // Garble descriptor port.
    descriptor->port_id = MAX_PORTS - 1;
  }
  size_t actual;
  ASSERT_OK(session.SendRx(descriptors.data(), descriptors.size(), &actual));
  ASSERT_EQ(actual, descriptors.size());
  ASSERT_OK(WaitRxAvailable());
  ASSERT_EQ(impl_.rx_buffers().size_slow(), descriptors.size());
  RxReturnTransaction return_session(&impl_);
  for (size_t i = 0; i < descriptors.size(); i++) {
    auto rx_space = impl_.rx_buffers().pop_back();
    // Set the port ID to the index, we should expect the session to only see port0.
    uint8_t port_id = static_cast<uint8_t>(i);
    rx_space->return_buffer().meta.port = port_id;
    // Write some data so the buffer makes it into the session.
    ASSERT_OK(rx_space->WriteData(&port_id, sizeof(port_id), impl_.VmoGetter()));
    return_session.Enqueue(std::move(rx_space));
  }
  return_session.Commit();
  ASSERT_OK(session.FetchRx(descriptors.data(), descriptors.size(), &actual));
  // Only one of the descriptors makes it back into the session.
  ASSERT_EQ(actual, 1u);
  uint16_t returned = descriptors[0];
  ASSERT_EQ(session.descriptor(returned)->port_id, FakeNetworkDeviceImpl::kPort0);

  // The unused descriptor comes right back to us.
  ASSERT_OK(WaitRxAvailable());
  ASSERT_EQ(impl_.rx_buffers().size_slow(), 1u);
}

TEST_F(NetworkDeviceTest, SessionsAttachToPort) {
  ASSERT_OK(CreateDevice());
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  auto& port0 = impl_.port0();
  // Just opening a session doesn't attach to port 0.
  ASSERT_STATUS(WaitPortActiveChanged(port0, zx::deadline_after(zx::msec(20))), ZX_ERR_TIMED_OUT);
  ASSERT_FALSE(port0.active());

  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitPortActiveChanged(port0));
  ASSERT_TRUE(port0.active());

  ASSERT_OK(session.SetPaused(true));
  ASSERT_OK(WaitPortActiveChanged(port0));
  ASSERT_FALSE(port0.active());

  // Unpause the session once again, then observe that session detaches on destruction.
  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitPortActiveChanged(port0));
  ASSERT_TRUE(port0.active());

  ASSERT_OK(session.Close());
  ASSERT_OK(WaitPortActiveChanged(port0));
  ASSERT_FALSE(port0.active());
}

TEST_F(NetworkDeviceTest, RejectsInvalidPortIds) {
  ASSERT_OK(CreateDevice());
  {
    // Add a port with an invalid ID.
    FakeNetworkPortImpl fake_port;
    network_port_protocol_t proto = fake_port.protocol();
    impl_.client().AddPort(MAX_PORTS, proto.ctx, proto.ops);
    ASSERT_TRUE(fake_port.removed());
  }

  {
    // Add a port with a duplicate ID.
    FakeNetworkPortImpl fake_port;
    network_port_protocol_t proto = fake_port.protocol();
    impl_.client().AddPort(FakeNetworkDeviceImpl::kPort0, proto.ctx, proto.ops);
    ASSERT_TRUE(fake_port.removed());
  }
}

TEST_F(NetworkDeviceTest, TxOnUnattachedPort) {
  // Test that transmitting a frame to a port we're not attached to returns the buffer with an
  // error.
  ASSERT_OK(CreateDevice());
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitStart());
  constexpr uint16_t kDesc = 0;
  buffer_descriptor_t* desc = session.ResetDescriptor(kDesc);
  desc->port_id = MAX_PORTS - 1;
  ASSERT_OK(session.SendTx(kDesc));
  // Should be returned with an error.
  zx_signals_t observed;
  ASSERT_OK(session.tx_fifo().wait_one(ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED, zx::time::infinite(),
                                       &observed));
  ASSERT_EQ(observed & (ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED), ZX_FIFO_READABLE);
  uint16_t read_desc = 0xFFFF;
  ASSERT_OK(session.FetchTx(&read_desc));
  ASSERT_EQ(read_desc, kDesc);
  ASSERT_EQ(desc->return_flags,
            static_cast<uint32_t>(netdev::wire::TxReturnFlags::kTxRetError |
                                  netdev::wire::TxReturnFlags::kTxRetNotAvailable));
}

}  // namespace testing
}  // namespace network
