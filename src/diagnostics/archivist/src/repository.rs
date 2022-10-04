// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        container::ComponentDiagnostics,
        error::Error,
        events::types::UniqueKey,
        identity::ComponentIdentity,
        inspect::container::{InspectArtifactsContainer, UnpopulatedInspectDataContainer},
        logs::{
            budget::BudgetManager,
            container::LogsArtifactsContainer,
            debuglog::{DebugLog, DebugLogBridge, KERNEL_IDENTITY},
            error::LogsError,
            listener::Listener,
            multiplex::{Multiplexer, MultiplexerHandle},
        },
        ImmutableString,
    },
    async_lock::{Mutex, RwLock},
    diagnostics_data::LogsData,
    diagnostics_hierarchy::{
        trie::{self, TrieIterableNode},
        InspectHierarchyMatcher,
    },
    fidl::prelude::*,
    fidl_fuchsia_diagnostics::{
        self, LogInterestSelector, LogSettingsMarker, LogSettingsRequest, LogSettingsRequestStream,
        Selector, StreamMode,
    },
    fidl_fuchsia_io as fio,
    fidl_fuchsia_logger::{LogMarker, LogRequest, LogRequestStream},
    fuchsia_async as fasync, fuchsia_fs, fuchsia_inspect as inspect,
    futures::channel::mpsc,
    futures::prelude::*,
    lazy_static::lazy_static,
    selectors,
    std::{
        collections::{BTreeMap, HashMap},
        sync::{
            atomic::{AtomicUsize, Ordering},
            Arc,
        },
    },
    tracing::{debug, error, warn},
};

lazy_static! {
    static ref CONNECTION_ID: AtomicUsize = AtomicUsize::new(0);
}

/// DataRepo holds all diagnostics data and is a singleton wrapped by multiple
/// [`pipeline::Pipeline`]s in a given Archivist instance.
#[derive(Clone)]
pub struct DataRepo {
    inner: Arc<RwLock<DataRepoState>>,
}

impl std::ops::Deref for DataRepo {
    type Target = RwLock<DataRepoState>;
    fn deref(&self) -> &Self::Target {
        &*self.inner
    }
}

#[cfg(test)]
impl Default for DataRepo {
    fn default() -> Self {
        let budget = BudgetManager::new(crate::constants::LEGACY_DEFAULT_MAXIMUM_CACHED_LOGS_BYTES);
        DataRepo { inner: DataRepoState::new(budget, &Default::default()) }
    }
}

impl DataRepo {
    pub fn new(logs_budget: &BudgetManager, parent: &fuchsia_inspect::Node) -> Self {
        DataRepo { inner: DataRepoState::new(logs_budget.clone(), parent) }
    }

    /// Drain the kernel's debug log. The returned future completes once
    /// existing messages have been ingested.
    pub async fn drain_debuglog<K>(self, klog_reader: K)
    where
        K: DebugLog + Send + Sync + 'static,
    {
        debug!("Draining debuglog.");
        let container = self.write().await.get_log_container(KERNEL_IDENTITY.clone()).await;
        let mut kernel_logger = DebugLogBridge::create(klog_reader);
        let mut messages = match kernel_logger.existing_logs().await {
            Ok(messages) => messages,
            Err(e) => {
                error!(%e, "failed to read from kernel log, important logs may be missing");
                return;
            }
        };
        messages.sort_by_key(|m| m.timestamp());
        for message in messages {
            container.ingest_message(message).await;
        }

        let res = kernel_logger
            .listen()
            .try_for_each(|message| async {
                container.ingest_message(message).await;
                Ok(())
            })
            .await;
        if let Err(e) = res {
            error!(%e, "failed to drain kernel log, important logs may be missing");
        }
    }

    /// Spawn a task to handle requests from components reading the shared log.
    pub fn handle_log(
        self,
        stream: LogRequestStream,
        sender: mpsc::UnboundedSender<fasync::Task<()>>,
    ) {
        if let Err(e) = sender.clone().unbounded_send(fasync::Task::spawn(async move {
            if let Err(e) = self.handle_log_requests(stream, sender).await {
                warn!("error handling Log requests: {}", e);
            }
        })) {
            warn!("Couldn't queue listener task: {:?}", e);
        }
    }

    /// Handle requests to `fuchsia.logger.Log`. All request types read the
    /// whole backlog from memory, `DumpLogs(Safe)` stops listening after that.
    async fn handle_log_requests(
        self,
        mut stream: LogRequestStream,
        mut sender: mpsc::UnboundedSender<fasync::Task<()>>,
    ) -> Result<(), LogsError> {
        let connection_id = CONNECTION_ID.fetch_add(1, Ordering::Relaxed);
        while let Some(request) = stream.next().await {
            let request = request.map_err(|source| LogsError::HandlingRequests {
                protocol: LogMarker::PROTOCOL_NAME,
                source,
            })?;

            let (listener, options, dump_logs, selectors) = match request {
                LogRequest::ListenSafe { log_listener, options, .. } => {
                    (log_listener, options, false, None)
                }
                LogRequest::DumpLogsSafe { log_listener, options, .. } => {
                    (log_listener, options, true, None)
                }

                LogRequest::ListenSafeWithSelectors {
                    log_listener, options, selectors, ..
                } => (log_listener, options, false, Some(selectors)),
            };

            let listener = Listener::new(listener, options)?;
            let mode =
                if dump_logs { StreamMode::Snapshot } else { StreamMode::SnapshotThenSubscribe };
            let logs = self.logs_cursor(mode, None).await;
            if let Some(s) = selectors {
                self.write().await.update_logs_interest(connection_id, s).await;
            }

            sender.send(listener.spawn(logs, dump_logs)).await.ok();
        }
        self.write().await.finish_interest_connection(connection_id).await;
        Ok(())
    }

    pub async fn handle_log_settings(
        self,
        mut stream: LogSettingsRequestStream,
    ) -> Result<(), LogsError> {
        let connection_id = CONNECTION_ID.fetch_add(1, Ordering::Relaxed);
        while let Some(request) = stream.next().await {
            let request = request.map_err(|source| LogsError::HandlingRequests {
                protocol: LogSettingsMarker::PROTOCOL_NAME,
                source,
            })?;
            match request {
                LogSettingsRequest::RegisterInterest { selectors, .. } => {
                    self.write().await.update_logs_interest(connection_id, selectors).await;
                }
            }
        }
        self.write().await.finish_interest_connection(connection_id).await;

        Ok(())
    }

    pub async fn logs_cursor(
        &self,
        mode: StreamMode,
        selectors: Option<Vec<Selector>>,
    ) -> impl Stream<Item = Arc<LogsData>> + Send + 'static {
        let mut repo = self.write().await;
        let (mut merged, mpx_handle) = Multiplexer::new();
        if let Some(selectors) = selectors {
            merged.set_selectors(selectors);
        }
        repo.data_directories
            .iter()
            .filter_map(|(_, c)| c)
            .filter_map(|c| {
                c.logs_cursor(mode).map(|cursor| (c.identity.relative_moniker.clone(), cursor))
            })
            .for_each(|(n, c)| {
                mpx_handle.send(n, c);
            });
        repo.logs_multiplexers.add(mode, mpx_handle).await;
        merged.set_on_drop_id_sender(repo.logs_multiplexers.cleanup_sender());

        merged
    }

    /// Returns `true` if a container exists for the requested `identity` and that container either
    /// corresponds to a running component or we've decided to still retain it.
    pub async fn is_live(&self, identity: &ComponentIdentity) -> bool {
        let this = self.read().await;
        if let Some(containers) = this.data_directories.get(&identity.unique_key().into()) {
            let diagnostics_containers = containers.get_values();
            diagnostics_containers.len() == 1 && diagnostics_containers[0].should_retain().await
        } else {
            false
        }
    }

    /// Stop accepting new messages, ensuring that pending Cursors return Poll::Ready(None) after
    /// consuming any messages received before this call.
    pub async fn terminate_logs(&self) {
        let mut repo = self.write().await;
        for container in repo.data_directories.iter().filter_map(|(_, v)| v) {
            container.terminate_logs();
        }
        repo.logs_multiplexers.terminate().await;
    }
}

pub struct DataRepoState {
    pub data_directories: trie::Trie<String, ComponentDiagnostics>,
    inspect_node: inspect::Node,

    /// A reference to the budget manager, kept to be passed to containers.
    logs_budget: BudgetManager,
    /// The current global interest in logs, as defined by the last client to send us selectors.
    logs_interest: Vec<LogInterestSelector>,
    /// BatchIterators for logs need to be made aware of new components starting and their logs.
    logs_multiplexers: MultiplexerBroker,

    /// Interest registrations that we have received through fuchsia.logger.Log/ListWithSelectors
    /// or through fuchsia.logger.LogSettings/RegisterInterest.
    interest_registrations: BTreeMap<usize, Vec<LogInterestSelector>>,
}

impl DataRepoState {
    fn new(logs_budget: BudgetManager, parent: &fuchsia_inspect::Node) -> Arc<RwLock<Self>> {
        Arc::new(RwLock::new(Self {
            inspect_node: parent.create_child("sources"),
            data_directories: trie::Trie::new(),
            logs_budget,
            logs_interest: vec![],
            logs_multiplexers: MultiplexerBroker::new(),
            interest_registrations: BTreeMap::new(),
        }))
    }

    pub async fn mark_stopped(&mut self, key: &UniqueKey) {
        if let Some(containers) = self.data_directories.get_mut(key) {
            let diagnostics_containers = containers.get_values_mut();
            if diagnostics_containers.len() == 1 {
                diagnostics_containers[0].mark_stopped().await;
            }
        }
    }

    /// Returns a container for logs artifacts, constructing one and adding it to the trie if
    /// necessary.
    pub async fn get_log_container(
        &mut self,
        identity: ComponentIdentity,
    ) -> Arc<LogsArtifactsContainer> {
        let trie_key: Vec<_> = identity.unique_key().into();

        // we use a macro instead of a closure to avoid lifetime issues
        macro_rules! insert_component {
            () => {{
                let mut to_insert =
                    ComponentDiagnostics::empty(Arc::new(identity), &self.inspect_node);
                let logs = to_insert
                    .logs(&self.logs_budget, &self.logs_interest, &mut self.logs_multiplexers)
                    .await;
                self.data_directories.insert(trie_key, to_insert);
                logs
            }};
        }

        match self.data_directories.get_mut(&trie_key) {
            Some(component) => match &mut component.get_values_mut()[..] {
                [] => insert_component!(),
                [existing] => {
                    existing
                        .logs(&self.logs_budget, &self.logs_interest, &mut self.logs_multiplexers)
                        .await
                }
                _ => unreachable!("invariant: each trie node has 0-1 entries"),
            },
            None => insert_component!(),
        }
    }

    async fn update_logs_interest(
        &mut self,
        connection_id: usize,
        selectors: Vec<LogInterestSelector>,
    ) {
        let previous_selectors =
            self.interest_registrations.insert(connection_id, selectors).unwrap_or_default();
        // unwrap safe, we just inserted.
        let new_selectors = self.interest_registrations.get(&connection_id).unwrap();
        for (_, dir) in self.data_directories.iter() {
            if let Some(dir) = dir {
                if let Some(logs) = &dir.logs {
                    logs.update_interest(new_selectors, &previous_selectors).await;
                }
            }
        }
    }

    pub async fn finish_interest_connection(&mut self, connection_id: usize) {
        let selectors = self.interest_registrations.remove(&connection_id);
        if let Some(selectors) = selectors {
            for (_, dir) in self.data_directories.iter() {
                if let Some(dir) = dir {
                    if let Some(logs) = &dir.logs {
                        logs.reset_interest(&selectors).await;
                    }
                }
            }
        }
    }

    pub async fn add_inspect_artifacts(
        &mut self,
        identity: ComponentIdentity,
        directory_proxy: fio::DirectoryProxy,
    ) -> Result<(), Error> {
        let inspect_container =
            InspectArtifactsContainer { component_diagnostics_proxy: directory_proxy };
        self.insert_inspect_artifact_container(inspect_container, identity).await
    }

    // Inserts an InspectArtifactsContainer into the data repository.
    async fn insert_inspect_artifact_container(
        &mut self,
        inspect_container: InspectArtifactsContainer,
        identity: ComponentIdentity,
    ) -> Result<(), Error> {
        let unique_key: Vec<_> = identity.unique_key().into();
        let diag_repo_entry_opt = self.data_directories.get_mut(&unique_key);

        match diag_repo_entry_opt {
            Some(diag_repo_entry) => {
                let diag_repo_entry_values: &mut [ComponentDiagnostics] =
                    diag_repo_entry.get_values_mut();

                match &mut *diag_repo_entry_values {
                    [] => {
                        // An entry with no values implies that the somehow we observed the
                        // creation of a component lower in the topology before observing this
                        // one. If this is the case, just instantiate as though it's our first
                        // time encountering this moniker segment.
                        self.data_directories.insert(
                            unique_key,
                            ComponentDiagnostics::new_with_inspect(
                                Arc::new(identity),
                                inspect_container,
                                &self.inspect_node,
                            ),
                        )
                    }
                    [existing_diagnostics_artifact_container] => {
                        // Races may occur between synthesized and real diagnostics_ready
                        // events, so we must handle de-duplication here.
                        // TODO(fxbug.dev/52047): Remove once caching handles ordering issues.
                        if existing_diagnostics_artifact_container.inspect.is_none() {
                            // This is expected to be the most common case. We've encountered the
                            // diagnostics_ready event for a component that has already been
                            // observed to be started/existing. We now must update the diagnostics
                            // artifact container with the inspect artifacts that accompanied the
                            // diagnostics_ready event.
                            existing_diagnostics_artifact_container.inspect =
                                Some(inspect_container);
                        }
                    }
                    _ => {
                        return Err(Error::MultipleArtifactContainers(unique_key));
                    }
                }
            }
            // This case is expected to be uncommon; we've encountered a diagnostics_ready
            // event before a start or existing event!
            None => self.data_directories.insert(
                unique_key,
                ComponentDiagnostics::new_with_inspect(
                    Arc::new(identity),
                    inspect_container,
                    &self.inspect_node,
                ),
            ),
        }
        Ok(())
    }

    /// Return all of the DirectoryProxies that contain Inspect hierarchies
    /// which contain data that should be selected from.
    pub fn fetch_inspect_data(
        &self,
        component_selectors: &Option<Vec<Selector>>,
        moniker_to_static_matcher_map: Option<&HashMap<ImmutableString, InspectHierarchyMatcher>>,
    ) -> Vec<UnpopulatedInspectDataContainer> {
        return self
            .data_directories
            .iter()
            .filter_map(|(_, diagnostics_artifacts_container_opt)| {
                let (diagnostics_artifacts_container, inspect_artifacts) =
                    match &diagnostics_artifacts_container_opt {
                        Some(diagnostics_artifacts_container) => {
                            match &diagnostics_artifacts_container.inspect {
                                Some(inspect_artifacts) => {
                                    (diagnostics_artifacts_container, inspect_artifacts)
                                }
                                None => return None,
                            }
                        }
                        None => return None,
                    };

                let optional_hierarchy_matcher = match moniker_to_static_matcher_map {
                    Some(map) => {
                        match map.get(
                            diagnostics_artifacts_container
                                .identity
                                .relative_moniker
                                .join("/")
                                .as_str(),
                        ) {
                            Some(inspect_matcher) => Some(inspect_matcher),
                            // Return early if there were static selectors, and none were for this
                            // moniker.
                            None => return None,
                        }
                    }
                    None => None,
                };

                // Verify that the dynamic selectors contain an entry that applies to
                // this moniker as well.
                if !match component_selectors {
                    Some(component_selectors) => component_selectors.iter().any(|s| {
                        selectors::match_component_moniker_against_selector(
                            &diagnostics_artifacts_container.identity.relative_moniker,
                            s,
                        )
                        .ok()
                        .unwrap_or(false)
                    }),
                    None => true,
                } {
                    return None;
                }

                // This artifact contains inspect and matches a passed selector.
                fuchsia_fs::clone_directory(
                    &inspect_artifacts.component_diagnostics_proxy,
                    fio::OpenFlags::CLONE_SAME_RIGHTS,
                )
                .ok()
                .map(|directory| UnpopulatedInspectDataContainer {
                    identity: diagnostics_artifacts_container.identity.clone(),
                    component_diagnostics_proxy: directory,
                    inspect_matcher: optional_hierarchy_matcher.cloned(),
                })
            })
            .collect();
    }
}

type LiveIteratorsMap = HashMap<usize, (StreamMode, MultiplexerHandle<Arc<LogsData>>)>;

/// Ensures that BatchIterators get access to logs from newly started components.
pub struct MultiplexerBroker {
    live_iterators: Arc<Mutex<LiveIteratorsMap>>,
    cleanup_sender: mpsc::UnboundedSender<usize>,
    _live_iterators_cleanup_task: fasync::Task<()>,
}

impl MultiplexerBroker {
    fn new() -> Self {
        let (cleanup_sender, mut receiver) = mpsc::unbounded();
        let live_iterators = Arc::new(Mutex::new(HashMap::new()));
        let live_iterators_clone = live_iterators.clone();
        Self {
            live_iterators,
            cleanup_sender,
            _live_iterators_cleanup_task: fasync::Task::spawn(async move {
                while let Some(id) = receiver.next().await {
                    live_iterators_clone.lock().await.remove(&id);
                }
            }),
        }
    }

    fn cleanup_sender(&self) -> mpsc::UnboundedSender<usize> {
        self.cleanup_sender.clone()
    }

    /// A new BatchIterator has been created and must be notified when future log containers are
    /// created.
    async fn add(&mut self, mode: StreamMode, recipient: MultiplexerHandle<Arc<LogsData>>) {
        match mode {
            // snapshot streams only want to know about what's currently available
            StreamMode::Snapshot => recipient.close(),
            StreamMode::SnapshotThenSubscribe | StreamMode::Subscribe => {
                self.live_iterators
                    .lock()
                    .await
                    .insert(recipient.multiplexer_id(), (mode, recipient));
            }
        }
    }

    /// Notify existing BatchIterators of a new logs container so they can include its messages
    /// in their results.
    pub async fn send(&mut self, container: &Arc<LogsArtifactsContainer>) {
        self.live_iterators.lock().await.retain(|_, (mode, recipient)| {
            recipient.send(container.identity.relative_moniker.clone(), container.cursor(*mode))
        });
    }

    /// Notify all multiplexers to terminate their streams once sub streams have terminated.
    async fn terminate(&mut self) {
        for (_, (_, recipient)) in self.live_iterators.lock().await.drain() {
            recipient.close();
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{events::types::ComponentIdentifier, logs::stored_message::StoredMessage},
        diagnostics_hierarchy::trie::TrieIterableNode,
        diagnostics_log_encoding::{
            encode::Encoder, Argument, Record, Severity as StreamSeverity, Value,
        },
        selectors::{self, FastError},
        std::{io::Cursor, time::Duration},
    };

    const TEST_URL: &'static str = "fuchsia-pkg://test";

    #[fuchsia::test]
    async fn inspect_repo_disallows_duplicated_dirs() {
        let inspect_repo = DataRepo::default();
        let mut inspect_repo = inspect_repo.write().await;
        let moniker = vec!["a", "b", "foo.cmx"].into();
        let instance_id = "1234".to_string();

        let component_id = ComponentIdentifier::Legacy { instance_id, moniker };
        let identity = ComponentIdentity::from_identifier_and_url(component_id, TEST_URL);

        let (proxy, _) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
            .expect("create directory proxy");

        inspect_repo.add_inspect_artifacts(identity.clone(), proxy).await.expect("add to repo");

        let (proxy, _) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
            .expect("create directory proxy");

        inspect_repo.add_inspect_artifacts(identity.clone(), proxy).await.expect("add to repo");

        assert_eq!(
            inspect_repo
                .data_directories
                .get(&identity.unique_key().into())
                .unwrap()
                .get_values()
                .len(),
            1
        );
    }

    #[fuchsia::test]
    async fn data_repo_updates_existing_entry_to_hold_inspect_data() {
        let data_repo = DataRepo::default();
        let mut data_repo = data_repo.write().await;
        let moniker = vec!["a", "b", "foo.cmx"].into();
        let instance_id = "1234".to_string();

        let component_id = ComponentIdentifier::Legacy { instance_id, moniker };
        let identity = ComponentIdentity::from_identifier_and_url(component_id, TEST_URL);
        let (proxy, _) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
            .expect("create directory proxy");

        data_repo.add_inspect_artifacts(identity.clone(), proxy).await.expect("add to repo");

        assert_eq!(
            data_repo
                .data_directories
                .get(&identity.unique_key().into())
                .unwrap()
                .get_values()
                .len(),
            1
        );
        let entry =
            &data_repo.data_directories.get(&identity.unique_key().into()).unwrap().get_values()[0];
        assert!(entry.inspect.is_some());
        assert_eq!(entry.identity.url, TEST_URL);
    }

    #[fuchsia::test]
    async fn diagnostics_repo_cant_have_more_than_one_diagnostics_data_container_per_component() {
        let data_repo = DataRepo::default();
        let mut data_repo = data_repo.write().await;
        let moniker = vec!["a", "b", "foo.cmx"].into();
        let instance_id = "1234".to_string();

        let component_id = ComponentIdentifier::Legacy { instance_id, moniker };
        let identity = ComponentIdentity::from_identifier_and_url(component_id, TEST_URL);

        let (proxy, _) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
            .expect("create directory proxy");
        data_repo.add_inspect_artifacts(identity.clone(), proxy).await.expect("add to repo");

        let mutable_values = data_repo
            .data_directories
            .get_mut(&identity.unique_key().into())
            .unwrap()
            .get_values_mut();

        mutable_values
            .push(ComponentDiagnostics::empty(Arc::new(identity.clone()), &Default::default()));

        let (proxy, _) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
            .expect("create directory proxy");

        assert!(data_repo.add_inspect_artifacts(identity, proxy).await.is_err());
    }

    #[fuchsia::test]
    async fn data_repo_filters_inspect_by_selectors() {
        let data_repo = DataRepo::default();
        let realm_path = vec!["a".to_string(), "b".to_string()];
        let instance_id = "1234".to_string();

        let mut moniker = realm_path.clone();
        moniker.push("foo.cmx".to_string());
        let component_id = ComponentIdentifier::Legacy { instance_id, moniker: moniker.into() };
        let identity = ComponentIdentity::from_identifier_and_url(component_id, TEST_URL);

        data_repo
            .write()
            .await
            .add_inspect_artifacts(
                identity,
                fuchsia_fs::directory::open_in_namespace(
                    "/tmp",
                    fuchsia_fs::OpenFlags::RIGHT_READABLE,
                )
                .expect("open root"),
            )
            .await
            .expect("add inspect artifacts");

        let mut moniker = realm_path;
        moniker.push("foo2.cmx".to_string());
        let component_id2 = ComponentIdentifier::Legacy {
            instance_id: "12345".to_string(),
            moniker: moniker.into(),
        };
        let identity2 = ComponentIdentity::from_identifier_and_url(component_id2, TEST_URL);

        data_repo
            .write()
            .await
            .add_inspect_artifacts(
                identity2,
                fuchsia_fs::directory::open_in_namespace(
                    "/tmp",
                    fuchsia_fs::OpenFlags::RIGHT_READABLE,
                )
                .expect("open root"),
            )
            .await
            .expect("add inspect artifacts");

        assert_eq!(2, data_repo.read().await.fetch_inspect_data(&None, None).len());

        let selectors = Some(vec![
            selectors::parse_selector::<FastError>("a/b/foo.cmx:root").expect("parse selector")
        ]);
        assert_eq!(1, data_repo.read().await.fetch_inspect_data(&selectors, None).len());

        let selectors = Some(vec![
            selectors::parse_selector::<FastError>("a/b/f*.cmx:root").expect("parse selector")
        ]);
        assert_eq!(2, data_repo.read().await.fetch_inspect_data(&selectors, None).len());

        let selectors = Some(vec![
            selectors::parse_selector::<FastError>("foo.cmx:root").expect("parse selector")
        ]);
        assert_eq!(0, data_repo.read().await.fetch_inspect_data(&selectors, None).len());
    }

    #[fuchsia::test]
    async fn data_repo_filters_logs_by_selectors() {
        let repo = DataRepo::default();
        let foo_container = repo
            .write()
            .await
            .get_log_container(ComponentIdentity::from_identifier_and_url(
                ComponentIdentifier::parse_from_moniker("./foo").unwrap(),
                "fuchsia-pkg://foo",
            ))
            .await;
        let bar_container = repo
            .write()
            .await
            .get_log_container(ComponentIdentity::from_identifier_and_url(
                ComponentIdentifier::parse_from_moniker("./bar").unwrap(),
                "fuchsia-pkg://bar",
            ))
            .await;

        foo_container.ingest_message(make_message("a", 1)).await;
        bar_container.ingest_message(make_message("b", 2)).await;
        foo_container.ingest_message(make_message("c", 3)).await;

        let stream = repo.logs_cursor(StreamMode::Snapshot, None).await;

        let results =
            stream.map(|value| value.msg().unwrap().to_string()).collect::<Vec<_>>().await;
        assert_eq!(results, vec!["a".to_string(), "b".to_string(), "c".to_string()]);

        let filtered_stream = repo
            .logs_cursor(
                StreamMode::Snapshot,
                Some(vec![selectors::parse_selector::<FastError>("foo:root").unwrap()]),
            )
            .await;

        let results =
            filtered_stream.map(|value| value.msg().unwrap().to_string()).collect::<Vec<_>>().await;
        assert_eq!(results, vec!["a".to_string(), "c".to_string()]);
    }

    #[fuchsia::test]
    async fn multiplexer_broker_cleanup() {
        let repo = DataRepo::default();
        let stream = repo.logs_cursor(StreamMode::SnapshotThenSubscribe, None).await;

        assert_eq!(repo.read().await.logs_multiplexers.live_iterators.lock().await.len(), 1);

        // When the multiplexer goes away it must be forgotten by the broker.
        drop(stream);
        loop {
            fasync::Timer::new(Duration::from_millis(100)).await;
            if repo.read().await.logs_multiplexers.live_iterators.lock().await.len() == 0 {
                break;
            }
        }
    }

    fn make_message(msg: &str, timestamp: i64) -> StoredMessage {
        let record = Record {
            timestamp,
            severity: StreamSeverity::Debug,
            arguments: vec![
                Argument { name: "pid".to_string(), value: Value::UnsignedInt(1) },
                Argument { name: "tid".to_string(), value: Value::UnsignedInt(2) },
                Argument { name: "message".to_string(), value: Value::Text(msg.to_string()) },
            ],
        };
        let mut buffer = Cursor::new(vec![0u8; 1024]);
        let mut encoder = Encoder::new(&mut buffer);
        encoder.write_record(&record).unwrap();
        let encoded = &buffer.get_ref()[..buffer.position() as usize];
        StoredMessage::structured(encoded, Default::default()).unwrap()
    }
}
