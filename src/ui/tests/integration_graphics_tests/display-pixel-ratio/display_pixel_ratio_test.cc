// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <memory>
#include <vector>

#include <gtest/gtest.h>
#include <test/accessibility/cpp/fidl.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/testing/ui_test_manager/ui_test_manager.h"
#include "src/ui/testing/util/gfx_test_view.h"

namespace integration_tests {
namespace {

constexpr auto kViewProvider = "view-provider";
constexpr float kEpsilon = 0.005f;

struct DisplayProperties {
  // Arbitrarily-chosen value.
  float display_pixel_density = 0.f;

  // This is the scale value that should result from a pixel density of |display_pixel_density|.
  // Calculated in DisplayMetrics
  // (https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/lib/scene_management/src/display_metrics.rs).
  float expected_scale = 0.f;

  DisplayProperties(float display_pixel_density, float expected_scale)
      : display_pixel_density(display_pixel_density), expected_scale(expected_scale) {}
};

// Returns a list of display pixel densities with its corresponding expected scale value.
static std::vector<DisplayProperties> GetPixelDensityToScaleValues() {
  const float kAstroDisplayPixelDensity = 4.1668f;
  const float kAstroExpectedScale = 1.2549f;
  const float kSherlockDisplayPixelDensity = 5.2011f;
  const float kSherlockExpectedScale = 1.f;

  std::vector<DisplayProperties> pixel_density;
  pixel_density.emplace_back(kAstroDisplayPixelDensity, kAstroExpectedScale);
  pixel_density.emplace_back(kSherlockDisplayPixelDensity, kSherlockExpectedScale);
  return pixel_density;
}

}  // namespace

using component_testing::ChildRef;
using component_testing::Directory;
using component_testing::ParentRef;
using component_testing::Protocol;
using component_testing::Realm;
using component_testing::Route;

// This test verifies that Root Presenter and Scene Manager propagate
// 'config/data/display_pixel_density' correctly.
class DisplayPixelRatioTest
    : public gtest::RealLoopFixture,
      public ::testing::WithParamInterface<
          std::tuple<ui_testing::UITestRealm::SceneOwnerType, DisplayProperties>> {
 protected:
  // |testing::Test|
  void SetUp() override {
    ui_testing::UITestRealm::Config config;
    config.scene_owner = std::get<0>(GetParam());  // scene owner.
    config.ui_to_client_services = {fuchsia::ui::scenic::Scenic::Name_};
    config.display_pixel_density = std::get<1>(GetParam()).display_pixel_density;
    config.display_usage = "close";
    ui_test_manager_ = std::make_unique<ui_testing::UITestManager>(std::move(config));

    // Build realm.
    FX_LOGS(INFO) << "Building realm";
    realm_ = std::make_unique<Realm>(ui_test_manager_->AddSubrealm());

    // Add a test view provider.
    test_view_ = std::make_unique<ui_testing::GfxTestView>(
        dispatcher(), /* content = */ ui_testing::TestView::ContentType::COORDINATE_GRID);
    realm_->AddLocalChild(kViewProvider, test_view_.get());
    realm_->AddRoute(Route{.capabilities = {Protocol{fuchsia::ui::app::ViewProvider::Name_}},
                           .source = ChildRef{kViewProvider},
                           .targets = {ParentRef()}});
    realm_->AddRoute(Route{.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_}},
                           .source = ParentRef(),
                           .targets = {ChildRef{kViewProvider}}});

    ui_test_manager_->BuildRealm();
    realm_exposed_services_ = ui_test_manager_->CloneExposedServicesDirectory();

    // Attach view, and wait for it to render.
    ui_test_manager_->InitializeScene();
    RunLoopUntil([this]() { return ui_test_manager_->ClientViewIsRendering(); });

    // Get display's width and height.
    auto [width, height] = ui_test_manager_->GetDisplayDimensions();

    display_width_ = static_cast<double>(width);
    display_height_ = static_cast<double>(height);
    FX_LOGS(INFO) << "Got display_width = " << display_width_
                  << " and display_height = " << display_height_;
  }

  float ClientViewScaleFactor() { return ui_test_manager_->ClientViewScaleFactor(); }

  ui_testing::Screenshot TakeScreenshot() { return ui_test_manager_->TakeScreenshot(); }

  std::unique_ptr<ui_testing::TestView> test_view_;
  double display_width_ = 0;
  double display_height_ = 0;

 private:
  std::unique_ptr<ui_testing::UITestManager> ui_test_manager_;

  std::unique_ptr<sys::ServiceDirectory> realm_exposed_services_;
  std::unique_ptr<Realm> realm_;
};

INSTANTIATE_TEST_SUITE_P(
    DisplayPixelRatioTestWithParams, DisplayPixelRatioTest,
    testing::Combine(::testing::Values(ui_testing::UITestRealm::SceneOwnerType::ROOT_PRESENTER,
                                       ui_testing::UITestRealm::SceneOwnerType::SCENE_MANAGER),
                     testing::ValuesIn(GetPixelDensityToScaleValues())));

// This test leverage the coordinate test view to ensure that display pixel ratio is working
// properly.
// ___________________________________
// |                |                |
// |     BLACK      |        BLUE    |
// |           _____|_____           |
// |___________|  GREEN  |___________|
// |           |_________|           |
// |                |                |
// |      RED       |     MAGENTA    |
// |________________|________________|
TEST_P(DisplayPixelRatioTest, TestScale) {
  auto expected_scale = std::get<1>(GetParam()).expected_scale;
  EXPECT_NEAR(ClientViewScaleFactor(), 1.0f / expected_scale, kEpsilon);

  EXPECT_NEAR(test_view_->width() / display_width_, expected_scale, kEpsilon);
  EXPECT_NEAR(test_view_->height() / display_height_, expected_scale, kEpsilon);

  // The drawn content should cover the screen's display.
  auto data = TakeScreenshot();

  // Check pixel content at all four corners.
  EXPECT_EQ(data.GetPixelAt(0, 0), ui_testing::Screenshot::kBlack);                 // Top left
  EXPECT_EQ(data.GetPixelAt(0, data.height() - 1), ui_testing::Screenshot::kBlue);  // Bottom left
  EXPECT_EQ(data.GetPixelAt(data.width() - 1, 0), ui_testing::Screenshot::kRed);    // Top right
  EXPECT_EQ(data.GetPixelAt(data.width() - 1, data.height() - 1),
            ui_testing::Screenshot::kMagenta);  // Bottom right

  // Check pixel content at center of each rectangle.
  EXPECT_EQ(data.GetPixelAt(data.width() / 4, data.height() / 4),
            ui_testing::Screenshot::kBlack);  // Top left
  EXPECT_EQ(data.GetPixelAt(data.width() / 4, (3 * data.height()) / 4),
            ui_testing::Screenshot::kBlue);  // Bottom left
  EXPECT_EQ(data.GetPixelAt((3 * data.width()) / 4, data.height() / 4),
            ui_testing::Screenshot::kRed);  // Top right
  EXPECT_EQ(data.GetPixelAt((3 * data.width()) / 4, (3 * data.height()) / 4),
            ui_testing::Screenshot::kMagenta);  // Bottom right
  EXPECT_EQ(data.GetPixelAt(data.width() / 2, data.height() / 2),
            ui_testing::Screenshot::kGreen);  // Center
}

}  // namespace integration_tests
