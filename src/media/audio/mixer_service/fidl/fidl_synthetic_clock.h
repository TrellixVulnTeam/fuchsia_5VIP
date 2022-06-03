// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_MIXER_SERVICE_FIDL_FIDL_SYNTHETIC_CLOCK_H_
#define SRC_MEDIA_AUDIO_MIXER_SERVICE_FIDL_FIDL_SYNTHETIC_CLOCK_H_

#include <fidl/fuchsia.audio.mixer/cpp/wire.h>
#include <lib/sync/cpp/completion.h>
#include <zircon/errors.h>

#include <optional>
#include <unordered_map>
#include <unordered_set>

#include "src/media/audio/mixer_service/common/basic_types.h"
#include "src/media/audio/mixer_service/fidl/base_fidl_server.h"
#include "src/media/audio/mixer_service/fidl/clock_registry.h"
#include "src/media/audio/mixer_service/fidl/ptr_decls.h"

namespace media_audio_mixer_service {

class FidlSyntheticClock
    : public BaseFidlServer<FidlSyntheticClock, fuchsia_audio_mixer::SyntheticClock> {
 public:
  static std::shared_ptr<FidlSyntheticClock> Create(
      async_dispatcher_t* fidl_thread_dispatcher,
      fidl::ServerEnd<fuchsia_audio_mixer::SyntheticClock> server_end,
      std::shared_ptr<Clock> clock);

  // Implementation of fidl::WireServer<fuchsia_audio_mixer::SyntheticClock>.
  void Now(NowRequestView request, NowCompleter::Sync& completer) override;
  void SetRate(SetRateRequestView request, SetRateCompleter::Sync& completer) override;

 private:
  template <class ServerT, class ProtocolT>
  friend class BaseFidlServer;

  static inline const std::string_view Name = "FidlSyntheticClockRealm";

  explicit FidlSyntheticClock(std::shared_ptr<Clock> clock) : clock_(std::move(clock)) {}

  // In practice, this should be either a SyntheticClock or an UnadjustableClockWrapper around a
  // SyntheticClock.
  const std::shared_ptr<Clock> clock_;
};

class FidlSyntheticClockRealm
    : public BaseFidlServer<FidlSyntheticClockRealm, fuchsia_audio_mixer::SyntheticClockRealm>,
      public ClockRegistry {
 public:
  static std::shared_ptr<FidlSyntheticClockRealm> Create(
      async_dispatcher_t* fidl_thread_dispatcher,
      fidl::ServerEnd<fuchsia_audio_mixer::SyntheticClockRealm> server_end);

  // Implementation of fidl::WireServer<fuchsia_audio_mixer::SyntheticClockRealm>.
  void CreateClock(CreateClockRequestView request, CreateClockCompleter::Sync& completer) override;
  void ForgetClock(ForgetClockRequestView request, ForgetClockCompleter::Sync& completer) override;
  void ObserveClock(ObserveClockRequestView request,
                    ObserveClockCompleter::Sync& completer) override;
  void Now(NowRequestView request, NowCompleter::Sync& completer) override;
  void AdvanceBy(AdvanceByRequestView request, AdvanceByCompleter::Sync& completer) override;

  // Implementation of ClockRegistry.
  zx::clock CreateGraphControlled() override;
  std::shared_ptr<Clock> FindOrCreate(zx::clock zx_clock, std::string_view name,
                                      uint32_t domain) override;

 private:
  template <class ServerT, class ProtocolT>
  friend class BaseFidlServer;

  static inline const std::string_view Name = "FidlSyntheticClockRealm";

  FidlSyntheticClockRealm() = default;

  struct ClockInfo {
    std::shared_ptr<Clock> clock;
    std::unordered_set<std::shared_ptr<FidlSyntheticClock>> servers;
  };

  std::shared_ptr<SyntheticClockRealm> realm_ = SyntheticClockRealm::Create();
  std::unordered_map<zx_koid_t, ClockInfo> clocks_;
  uint64_t num_graph_controlled_ = 0;
};

}  // namespace media_audio_mixer_service

#endif  // SRC_MEDIA_AUDIO_MIXER_SERVICE_FIDL_FIDL_SYNTHETIC_CLOCK_H_