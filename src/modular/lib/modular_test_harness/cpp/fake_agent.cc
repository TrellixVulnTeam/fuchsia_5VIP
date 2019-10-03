// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/modular_test_harness/cpp/fake_agent.h"

namespace modular {
namespace testing {

FakeAgent::FakeAgent(FakeComponent::Args args) : FakeComponent(std::move(args)) {}

FakeAgent::~FakeAgent() = default;

// static
std::unique_ptr<FakeAgent> FakeAgent::CreateWithDefaultOptions() {
  return std::make_unique<FakeAgent>(modular::testing::FakeComponent::Args{
      .url = modular_testing::TestHarnessBuilder::GenerateFakeUrl(),
      .sandbox_services = FakeAgent::GetDefaultSandboxServices()});
}

// static
std::vector<std::string> FakeAgent::GetDefaultSandboxServices() {
  return {fuchsia::modular::ComponentContext::Name_, fuchsia::modular::AgentContext::Name_};
}

// |modular::testing::FakeComponent|
void FakeAgent::OnCreate(fuchsia::sys::StartupInfo startup_info) {
  FakeComponent::OnCreate(std::move(startup_info));

  component_context()->svc()->Connect(modular_component_context_.NewRequest());
  component_context()->svc()->Connect(agent_context_.NewRequest());

  agent_ = std::make_unique<modular::Agent>(component_context()->outgoing(),
                                            /* on_terminate */
                                            [this] {
                                              Exit(0);
                                              // |OnDestroy| is invoked at this point.
                                            });
}

}  // namespace testing
}  // namespace modular
