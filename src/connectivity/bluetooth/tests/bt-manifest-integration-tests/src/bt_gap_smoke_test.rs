// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::DiscoverableProtocolMarker,
    fidl_fuchsia_bluetooth_bredr::{ProfileMarker, ProfileProxy},
    fidl_fuchsia_bluetooth_gatt as fbgatt, fidl_fuchsia_bluetooth_gatt2 as fbgatt2,
    fidl_fuchsia_bluetooth_le as fble,
    fidl_fuchsia_bluetooth_sys::{
        AccessMarker, AccessProxy, BootstrapMarker, BootstrapProxy, ConfigurationMarker,
        ConfigurationProxy, HostWatcherMarker, HostWatcherProxy, PairingMarker, PairingProxy,
    },
    fidl_fuchsia_device::{NameProviderMarker, NameProviderRequestStream},
    fidl_fuchsia_io as fio,
    fidl_fuchsia_stash::SecureStoreMarker,
    fuchsia_component::server::ServiceFs,
    fuchsia_component_test::{
        Capability, ChildOptions, LocalComponentHandles, RealmBuilder, Ref, Route,
    },
    futures::{channel::mpsc, SinkExt, StreamExt},
    realmbuilder_mock_helpers::{mock_dev, provide_bt_gap_uses},
    std::sync::Arc,
    tracing::info,
    vfs::{directory::entry::DirectoryEntry, pseudo_directory},
};

const BT_GAP_URL: &str = "fuchsia-pkg://fuchsia.com/bt-gap-smoke-test#meta/bt-gap.cm";

const SECURE_STORE_URL: &str = "fuchsia-pkg://fuchsia.com/bt-gap-smoke-test#meta/stash_secure.cm";

// Component monikers.
const BT_GAP_MONIKER: &str = "bt-gap";
const MOCK_PROVIDER_MONIKER: &str = "mock-provider";
const MOCK_CLIENT_MONIKER: &str = "mock-client";
const MOCK_DEV_MONIKER: &str = "mock-dev";
const SECURE_STORE_MONIKER: &str = "fake-secure-store";

/// The different events generated by this test.
/// Note: In order to prevent the component under test from terminating, any FIDL request or
/// Proxy is preserved.
enum Event {
    Profile(Option<ProfileProxy>),
    GattServer(Option<fbgatt::Server_Proxy>),
    Gatt2Server(Option<fbgatt2::Server_Proxy>),
    LeCentral(Option<fble::CentralProxy>),
    LePeripheral(Option<fble::PeripheralProxy>),
    Access(Option<AccessProxy>),
    HostWatcher(Option<HostWatcherProxy>),
    Bootstrap(Option<BootstrapProxy>),
    Config(Option<ConfigurationProxy>),
    Pairing(Option<PairingProxy>),
    NameProvider(Option<NameProviderRequestStream>),
    // bt-gap will not start up without a working SecureStore, so instead of just notifying the
    // test of requests, we also forward along the requests to a working implementation. As such,
    // there is no need to hold on to the requests in the Event.
    SecureStore,
}

/// The expected number of FIDL capability events.
const NUMBER_OF_EVENTS: usize = 12;

impl From<NameProviderRequestStream> for Event {
    fn from(src: NameProviderRequestStream) -> Self {
        Self::NameProvider(Some(src))
    }
}

// So that Event can be generically constructed in provide_bt_gap_uses.
impl From<SecureStoreMarker> for Event {
    fn from(_src: SecureStoreMarker) -> Self {
        Self::SecureStore
    }
}

/// An empty dev/bt-host pseudo-directory.
pub fn dev_bt_host() -> Arc<dyn DirectoryEntry> {
    pseudo_directory! {
        "class" => pseudo_directory! {
            "bt-host" => pseudo_directory! {}
        }
    }
}

/// Represents a fake bt-gap client that connects to the services it exposes.
async fn mock_client(
    mut sender: mpsc::Sender<Event>,
    handles: LocalComponentHandles,
) -> Result<(), Error> {
    let profile_svc = handles.connect_to_protocol::<ProfileMarker>()?;
    sender.send(Event::Profile(Some(profile_svc))).await.expect("failed sending ack to test");

    let gatt_server_svc = handles.connect_to_protocol::<fbgatt::Server_Marker>()?;
    sender
        .send(Event::GattServer(Some(gatt_server_svc)))
        .await
        .expect("failed sending ack to test");

    let gatt2_server_svc = handles.connect_to_protocol::<fbgatt2::Server_Marker>()?;
    sender
        .send(Event::Gatt2Server(Some(gatt2_server_svc)))
        .await
        .expect("failed sending ack to test");

    let le_central_svc = handles.connect_to_protocol::<fble::CentralMarker>()?;
    sender.send(Event::LeCentral(Some(le_central_svc))).await.expect("failed sending ack to test");

    let le_peripheral_svc = handles.connect_to_protocol::<fble::PeripheralMarker>()?;
    sender
        .send(Event::LePeripheral(Some(le_peripheral_svc)))
        .await
        .expect("failed sending ack to test");

    let access_svc = handles.connect_to_protocol::<AccessMarker>()?;
    sender.send(Event::Access(Some(access_svc))).await.expect("failed sending ack to test");

    let bootstrap_svc = handles.connect_to_protocol::<BootstrapMarker>()?;
    sender.send(Event::Bootstrap(Some(bootstrap_svc))).await.expect("failed sending ack to test");

    let configuration_svc = handles.connect_to_protocol::<ConfigurationMarker>()?;
    sender.send(Event::Config(Some(configuration_svc))).await.expect("failed sending ack to test");

    let host_watcher_svc = handles.connect_to_protocol::<HostWatcherMarker>()?;
    sender
        .send(Event::HostWatcher(Some(host_watcher_svc)))
        .await
        .expect("failed sending ack to test");

    let pairing_svc = handles.connect_to_protocol::<PairingMarker>()?;
    sender.send(Event::Pairing(Some(pairing_svc))).await.expect("failed sending ack to test");

    Ok(())
}

/// The component mock that provides the services used by bt-gap.
async fn mock_provider(
    sender: mpsc::Sender<Event>,
    handles: LocalComponentHandles,
) -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    provide_bt_gap_uses(&mut fs, &sender, &handles)?;
    let _ = fs.serve_connection(handles.outgoing_dir.into_channel())?;
    fs.collect::<()>().await;
    Ok(())
}

// Helper for the common case of routing between bt-gap and its mock client.
async fn route_from_bt_gap_to_mock_client<S: DiscoverableProtocolMarker>(builder: &RealmBuilder) {
    let _ = builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<S>())
                .from(Ref::child(BT_GAP_MONIKER))
                .to(Ref::child(MOCK_CLIENT_MONIKER)),
        )
        .await
        .expect(&format!("failed routing {} from bt-gap to mock client", S::PROTOCOL_NAME));
}

/// Tests that the v2 bt-gap component has the correct topology and verifies that it connects to and
/// provides the expected services.
#[fuchsia::test]
async fn bt_gap_component_topology() {
    info!("Starting bt-gap v2 smoke test...");

    let (sender, mut receiver) = mpsc::channel(0);
    let mock_provider_tx = sender.clone();
    let mock_client_tx = sender.clone();

    let builder = RealmBuilder::new().await.expect("Failed to create test realm builder");

    // Add bt-gap, the v2 component under test, to the topology.
    let bt_gap = builder
        .add_child(BT_GAP_MONIKER, BT_GAP_URL.to_string(), ChildOptions::new())
        .await
        .expect("Failed adding bt-gap to topology");

    // Implementation of the Secure Store service for use by bt-gap.
    let secure_store = builder
        .add_child(SECURE_STORE_MONIKER, SECURE_STORE_URL.to_string(), ChildOptions::new())
        .await
        .expect("Failed adding secure store fake to topology");

    // Provides a mock implementation of the services used by bt-gap, and notifies the test of
    // connections to these services.
    let mock_provider = builder
        .add_local_child(
            MOCK_PROVIDER_MONIKER,
            move |handles: LocalComponentHandles| {
                let sender = mock_provider_tx.clone();
                Box::pin(mock_provider(sender, handles))
            },
            ChildOptions::new(),
        )
        .await
        .expect("Failed adding mock provider to topology");

    // Provides a stub `/dev/bt-host` so bt-gap initialization doesn't fail.
    let mock_dev_child = builder
        .add_local_child(
            MOCK_DEV_MONIKER,
            move |handles: LocalComponentHandles| Box::pin(mock_dev(handles, dev_bt_host())),
            ChildOptions::new(),
        )
        .await
        .expect("Failed adding mock /dev provider to topology");

    // Mock bt-gap client that will request all the services exposed by bt-gap.
    let mock_client_child = builder
        .add_local_child(
            MOCK_CLIENT_MONIKER,
            move |handles: LocalComponentHandles| {
                let sender = mock_client_tx.clone();
                Box::pin(mock_client(sender, handles))
            },
            ChildOptions::new().eager(),
        )
        .await
        .expect("Failed adding bt-gap client mock to topology");

    // Add routes from bt-gap to the mock bt-gap client.
    route_from_bt_gap_to_mock_client::<ProfileMarker>(&builder).await;
    route_from_bt_gap_to_mock_client::<fbgatt::Server_Marker>(&builder).await;
    route_from_bt_gap_to_mock_client::<fbgatt2::Server_Marker>(&builder).await;
    route_from_bt_gap_to_mock_client::<fble::CentralMarker>(&builder).await;
    route_from_bt_gap_to_mock_client::<fble::PeripheralMarker>(&builder).await;
    route_from_bt_gap_to_mock_client::<AccessMarker>(&builder).await;
    route_from_bt_gap_to_mock_client::<BootstrapMarker>(&builder).await;
    route_from_bt_gap_to_mock_client::<ConfigurationMarker>(&builder).await;
    route_from_bt_gap_to_mock_client::<HostWatcherMarker>(&builder).await;
    route_from_bt_gap_to_mock_client::<PairingMarker>(&builder).await;

    // Add proxy route between secure store and mock provider
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<SecureStoreMarker>())
                .from(&secure_store)
                .to(&mock_provider),
        )
        .await
        .expect("Failed adding proxy route for Secure Store service");

    // Add routes for bt-gap's `use`s from mock-provider.
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<SecureStoreMarker>())
                .capability(Capability::protocol::<NameProviderMarker>())
                .from(&mock_provider)
                .to(&bt_gap),
        )
        .await
        .expect("Failed adding Secure Store and Name Provider route between mock and bt-gap");
    builder
        .add_route(
            Route::new()
                .capability(Capability::directory("dev").path("/dev").rights(fio::RW_STAR_DIR))
                .from(&mock_dev_child)
                .to(&bt_gap),
        )
        .await
        .expect("Failed adding route for bt-host device directory");
    // Proxy LogSink to children
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fidl_fuchsia_logger::LogSinkMarker>())
                .from(Ref::parent())
                .to(&bt_gap)
                .to(&secure_store)
                .to(&mock_provider)
                .to(&mock_client_child),
        )
        .await
        .expect("Failed adding LogSink route to test components");
    // Proxy temp storage to SecureStore impl
    builder
        .add_route(
            Route::new()
                .capability(Capability::storage("data"))
                .from(Ref::parent())
                .to(&secure_store),
        )
        .await
        .expect("Failed adding temp storage route to SecureStore component");
    let test_topology = builder.build().await.unwrap();

    // If the routing is correctly configured, we expect one of each `Event` to be sent (so, in
    // total, 10 events)
    let mut events = Vec::new();
    for i in 0..NUMBER_OF_EVENTS {
        let msg = format!("Unexpected error waiting for {:?} event", i);
        let event = receiver.next().await.expect(&msg);
        info!("Got event with discriminant: {:?}", std::mem::discriminant(&event));
        events.push(event);
    }
    assert_eq!(events.len(), NUMBER_OF_EVENTS);
    let discriminants: Vec<_> = events.iter().map(std::mem::discriminant).collect();
    for event in vec![
        Event::Profile(None),
        Event::GattServer(None),
        Event::Gatt2Server(None),
        Event::LeCentral(None),
        Event::LePeripheral(None),
        Event::Access(None),
        Event::Bootstrap(None),
        Event::Config(None),
        Event::HostWatcher(None),
        Event::Pairing(None),
        Event::NameProvider(None),
        Event::SecureStore,
    ] {
        let count = discriminants.iter().filter(|&&d| d == std::mem::discriminant(&event)).count();
        assert_eq!(
            count,
            1,
            "Found count of {} for discriminant {:?}, expected 1",
            count,
            std::mem::discriminant(&event)
        );
    }

    // Explicitly destroy the test realm so that components within this realm are shut down
    // correctly.
    test_topology.destroy().await.expect("Can destroy test realm");
    info!("Finished bt-gap smoke test");
}
