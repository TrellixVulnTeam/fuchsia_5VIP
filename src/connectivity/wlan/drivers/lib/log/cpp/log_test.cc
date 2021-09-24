// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <gtest/gtest.h>
#include <wlan/drivers/log.h>

namespace wlan::drivers {

#define kDebugTag "dtag"
#define kTraceTag "ttag"

// Enable all debug levels (i.e. TRACE and above) to allow for full functional testing.
#define WLAN_DRIVER_LOG_LEVEL wlan::drivers::Log::kTRACE
class LogTest : public ::testing::Test {
 public:
  void SetUp() override {
    flag_ = FX_LOG_NONE;
    tag_.clear();
  }

  void ZxlogfEtcOverride(fx_log_severity_t flag, const char* tag, ...) {
    ASSERT_NE(FX_LOG_NONE, flag);
    flag_ = flag;
    if (tag != nullptr) {
      tag_.assign(tag);
    }
  }

  void Validate(fx_log_severity_t flag, const char* tag = nullptr) const {
    ASSERT_EQ(flag_, flag);
    if (tag != nullptr) {
      ASSERT_STREQ(tag_.c_str(), tag);
    }
  }

  bool LogInvoked() const { return (flag_ != FX_LOG_NONE); }

 private:
  fx_log_severity_t flag_;
  std::string tag_;
};

TEST(FilterTest, SingleBit) {
  Log::SetFilter(0x2);
  EXPECT_TRUE(Log::IsFilterOn(0x2));
  EXPECT_FALSE(Log::IsFilterOn(~0x2));

  Log::SetFilter(0x8000);
  EXPECT_TRUE(Log::IsFilterOn(0x8000));
  EXPECT_FALSE(Log::IsFilterOn(~0x8000));
}

TEST(FilterTest, MultiBit) {
  Log::SetFilter(0xF);
  EXPECT_TRUE(Log::IsFilterOn(0x1));
  EXPECT_TRUE(Log::IsFilterOn(0x2));
  EXPECT_TRUE(Log::IsFilterOn(0x4));
  EXPECT_TRUE(Log::IsFilterOn(0x8));
  EXPECT_FALSE(Log::IsFilterOn(~0xF));
}

// Ensure no crashes when going via the DDK library.
TEST_F(LogTest, Sanity) {
  lerror("error %s", "test");
  lwarn("warn %s", "test");
  linfo("info %s", "test");
  Log::SetFilter(0x3);
  ldebug(0x1, kDebugTag, "debug %s", "test");
  ltrace(0x2, kTraceTag, "trace %s", "test");
  lthrottle_error("error throttle %s", "test");
  lthrottle_warn("warn throttle %s", "test");
  lthrottle_info("info throttle %s", "test");
  lthrottle_debug(0x1, kDebugTag, "debug trottle %s", "test");
  lthrottle_trace(0x2, kTraceTag, "trace throttle %s", "test");
}

// The following override is done to validate the right set of flag and tag is getting passed along.
// Avoid adding tests that require calls to go via DDK library below this.
#ifdef zxlogf_etc
#undef zxlogf_etc
#define zxlogf_etc(flag, tag...) ZxlogfEtcOverride(flag, tag)
#endif

TEST_F(LogTest, Error) {
  lerror("error %s", "test");
  Validate(DDK_LOG_ERROR);
}

TEST_F(LogTest, Warn) {
  lwarn("warn %s", "test");
  Validate(DDK_LOG_WARNING);
}

TEST_F(LogTest, Info) {
  linfo("info %s", "test");
  Validate(DDK_LOG_INFO);
}

TEST_F(LogTest, DebugFiltered) {
  Log::SetFilter(0);
  ldebug(0x1, kDebugTag, "debug %s", "test");
  ASSERT_FALSE(LogInvoked());
}

TEST_F(LogTest, DebugNotFiltered) {
  Log::SetFilter(0x1);
  ldebug(0x1, kDebugTag, "debug %s", "test");
  ASSERT_TRUE(LogInvoked());
  Validate(DDK_LOG_DEBUG, kDebugTag);
}

TEST_F(LogTest, TraceFiltered) {
  Log::SetFilter(0);
  ltrace(0x2, kTraceTag, "trace %s", "test");
  ASSERT_FALSE(LogInvoked());
}

TEST_F(LogTest, TraceNotFiltered) {
  Log::SetFilter(0x2);
  ltrace(0x2, kTraceTag, "trace %s", "test");
  ASSERT_TRUE(LogInvoked());
  Validate(DDK_LOG_TRACE, kTraceTag);
}

// Throttle macro tests
TEST_F(LogTest, ThrottleError) {
  lthrottle_error("error throttle %s", "test");
  Validate(DDK_LOG_ERROR);
}

TEST_F(LogTest, ThrottleWarn) {
  lthrottle_warn("warn throttle %s", "test");
  Validate(DDK_LOG_WARNING);
}

TEST_F(LogTest, ThrottleInfo) {
  lthrottle_info("info throttle %s", "test");
  Validate(DDK_LOG_INFO);
}

TEST_F(LogTest, ThrottleDebugFiltered) {
  Log::SetFilter(0);
  lthrottle_debug(0x1, kDebugTag, "debug throttle %s", "test");
  ASSERT_FALSE(LogInvoked());
}

TEST_F(LogTest, ThrottleDebugNotFiltered) {
  Log::SetFilter(0x1);
  lthrottle_debug(0x1, kDebugTag, "debug throttle %s", "test");
  ASSERT_TRUE(LogInvoked());
  Validate(DDK_LOG_DEBUG, kDebugTag);
}

TEST_F(LogTest, ThrottleTraceFiltered) {
  Log::SetFilter(0);
  lthrottle_trace(0x2, kTraceTag, "trace throttle %s", "test");
  ASSERT_FALSE(LogInvoked());
}

TEST_F(LogTest, ThrottleTraceNotFiltered) {
  Log::SetFilter(0x2);
  lthrottle_trace(0x2, kTraceTag, "trace throttle %s", "test");
  ASSERT_TRUE(LogInvoked());
  Validate(DDK_LOG_TRACE, kTraceTag);
}

// Tests for WLAN_DRIVER_LOG_LEVEL symbol
#undef WLAN_DRIVER_LOG_LEVEL
#define WLAN_DRIVER_LOG_LEVEL wlan::drivers::Log::kERROR
TEST_F(LogTest, LevelError) {
  lwarn("warn %s", "test");
  linfo("info %s", "test");
  Log::SetFilter(0x3);
  ldebug(0x1, kDebugTag, "debug %s", "test");
  ltrace(0x2, kTraceTag, "trace %s", "test");
  ASSERT_FALSE(LogInvoked());

  lerror("error %s", "test");
  Validate(DDK_LOG_ERROR);
}

#undef WLAN_DRIVER_LOG_LEVEL
#define WLAN_DRIVER_LOG_LEVEL wlan::drivers::Log::kWARNING
TEST_F(LogTest, LevelWarn) {
  linfo("info %s", "test");
  Log::SetFilter(0x3);
  ldebug(0x1, kDebugTag, "debug %s", "test");
  ltrace(0x2, kTraceTag, "trace %s", "test");
  ASSERT_FALSE(LogInvoked());

  lerror("error %s", "test");
  Validate(DDK_LOG_ERROR);
  lwarn("warn %s", "test");
  Validate(DDK_LOG_WARNING);
}

#undef WLAN_DRIVER_LOG_LEVEL
#define WLAN_DRIVER_LOG_LEVEL wlan::drivers::Log::kINFO
TEST_F(LogTest, LevelInfo) {
  Log::SetFilter(0x3);
  ldebug(0x1, kDebugTag, "debug %s", "test");
  ltrace(0x2, kTraceTag, "trace %s", "test");
  ASSERT_FALSE(LogInvoked());

  lerror("error %s", "test");
  Validate(DDK_LOG_ERROR);
  lwarn("warn %s", "test");
  Validate(DDK_LOG_WARNING);
  linfo("info %s", "test");
  Validate(DDK_LOG_INFO);
}

#undef WLAN_DRIVER_LOG_LEVEL
#define WLAN_DRIVER_LOG_LEVEL wlan::drivers::Log::kDEBUG
TEST_F(LogTest, LevelDebug) {
  Log::SetFilter(0x3);
  ltrace(0x2, kTraceTag, "trace %s", "test");
  ASSERT_FALSE(LogInvoked());

  lerror("error %s", "test");
  Validate(DDK_LOG_ERROR);
  lwarn("warn %s", "test");
  Validate(DDK_LOG_WARNING);
  linfo("info %s", "test");
  Validate(DDK_LOG_INFO);
  ldebug(0x1, kDebugTag, "debug %s", "test");
  Validate(DDK_LOG_DEBUG, kDebugTag);
}

#undef WLAN_DRIVER_LOG_LEVEL
#define WLAN_DRIVER_LOG_LEVEL wlan::drivers::Log::kTRACE
TEST_F(LogTest, LevelTrace) {
  Log::SetFilter(0x3);
  lerror("error %s", "test");
  Validate(DDK_LOG_ERROR);
  lwarn("warn %s", "test");
  Validate(DDK_LOG_WARNING);
  linfo("info %s", "test");
  Validate(DDK_LOG_INFO);
  ldebug(0x1, kDebugTag, "debug %s", "test");
  Validate(DDK_LOG_DEBUG, kDebugTag);
  ltrace(0x2, kTraceTag, "trace %s", "test");
  Validate(DDK_LOG_TRACE, kTraceTag);
}

// Tests for WLAN_DRIVER_LOG_LEVEL for throttle macros
#undef WLAN_DRIVER_LOG_LEVEL
#define WLAN_DRIVER_LOG_LEVEL wlan::drivers::Log::kERROR
TEST_F(LogTest, ThrottleLevelError) {
  lthrottle_warn("warn throttle %s", "test");
  lthrottle_info("info throttle %s", "test");
  Log::SetFilter(0x3);
  lthrottle_debug(0x1, kDebugTag, "debug throttle %s", "test");
  lthrottle_trace(0x2, kTraceTag, "trace throttle %s", "test");
  ASSERT_FALSE(LogInvoked());

  lthrottle_error("error throttle %s", "test");
  Validate(DDK_LOG_ERROR);
}

#undef WLAN_DRIVER_LOG_LEVEL
#define WLAN_DRIVER_LOG_LEVEL wlan::drivers::Log::kWARNING
TEST_F(LogTest, ThrottleLevelWarn) {
  lthrottle_info("info throttle %s", "test");
  Log::SetFilter(0x3);
  lthrottle_debug(0x1, kDebugTag, "debug throttle %s", "test");
  lthrottle_trace(0x2, kTraceTag, "trace throttle %s", "test");
  ASSERT_FALSE(LogInvoked());

  lthrottle_error("error throttle %s", "test");
  Validate(DDK_LOG_ERROR);
  lthrottle_warn("warn throttle %s", "test");
  Validate(DDK_LOG_WARNING);
}

#undef WLAN_DRIVER_LOG_LEVEL
#define WLAN_DRIVER_LOG_LEVEL wlan::drivers::Log::kINFO
TEST_F(LogTest, ThrottleLevelInfo) {
  Log::SetFilter(0x3);
  lthrottle_debug(0x1, kDebugTag, "debug throttle %s", "test");
  lthrottle_trace(0x2, kTraceTag, "trace throttle %s", "test");
  ASSERT_FALSE(LogInvoked());

  lthrottle_error("error throttle %s", "test");
  Validate(DDK_LOG_ERROR);
  lthrottle_warn("warn throttle %s", "test");
  Validate(DDK_LOG_WARNING);
  lthrottle_info("info throttle %s", "test");
  Validate(DDK_LOG_INFO);
}

#undef WLAN_DRIVER_LOG_LEVEL
#define WLAN_DRIVER_LOG_LEVEL wlan::drivers::Log::kDEBUG
TEST_F(LogTest, ThrottleLevelDebug) {
  Log::SetFilter(0x3);
  lthrottle_trace(0x2, kTraceTag, "trace throttle %s", "test");
  ASSERT_FALSE(LogInvoked());

  lthrottle_error("error throttle %s", "test");
  Validate(DDK_LOG_ERROR);
  lthrottle_warn("warn throttle %s", "test");
  Validate(DDK_LOG_WARNING);
  lthrottle_info("info throttle %s", "test");
  Validate(DDK_LOG_INFO);
  lthrottle_debug(0x1, kDebugTag, "debug throttle %s", "test");
  Validate(DDK_LOG_DEBUG, kDebugTag);
}

#undef WLAN_DRIVER_LOG_LEVEL
#define WLAN_DRIVER_LOG_LEVEL wlan::drivers::Log::kTRACE
TEST_F(LogTest, ThrottleLevelTrace) {
  Log::SetFilter(0x3);
  lthrottle_error("error throttle %s", "test");
  Validate(DDK_LOG_ERROR);
  lthrottle_warn("warn throttle %s", "test");
  Validate(DDK_LOG_WARNING);
  lthrottle_info("info throttle %s", "test");
  Validate(DDK_LOG_INFO);
  lthrottle_debug(0x1, kDebugTag, "debug throttle %s", "test");
  Validate(DDK_LOG_DEBUG, kDebugTag);
  lthrottle_trace(0x2, kTraceTag, "trace throttle %s", "test");
  Validate(DDK_LOG_TRACE, kTraceTag);
}

}  // namespace wlan::drivers
