// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{Key, StorageManager};
use account_common::{AccountManagerError, ResultExt};
use anyhow::format_err;
use async_trait::async_trait;
use fidl_fuchsia_identity_account::Error as ApiError;
use fidl_fuchsia_io as fio;
use fuchsia_fs::directory::{DirEntry, DirentKind};
use futures::lock::Mutex;
use lazy_static::lazy_static;
use std::path::Path;

/// Path to the file containing the expected key.
const KEY_FILE_PATH: &str = "keyfile";
/// The subdirectory `InsecureKeyDirectoryStorageManager` serves via `get_root_dir`.
const CLIENT_ROOT_PATH: &str = "root";

lazy_static! {
    /// Expected `DirEntry`s for a `InsecureKeyDirectoryStorageManager` in the
    /// locked or available state.
    static ref EXPECTED_DIRECTORY_ENTRIES: Vec<DirEntry> = {
        let mut entries = vec![
            DirEntry { name: CLIENT_ROOT_PATH.to_string(), kind: DirentKind::Directory },
            DirEntry { name: KEY_FILE_PATH.to_string(), kind: DirentKind::File }
        ];
        // fuchsia_fs::directory::readdir sorts results, so by sorting the entries we can
        // do a direct comparison with the readdir results.
        entries.sort();
        entries
    };
}

/// An enumeration of the different internal states a
/// `InsecureKeyDirectoryStorageManager` may be in.
#[derive(Debug)]
enum StorageManagerState {
    /// The underlying directory is not initialized.
    Uninitialized,
    /// The underlying directory is initialized, but the manager is not giving
    /// out handles to it.
    Locked,
    /// The underlying directory is initialized and the manager is giving out
    /// handles via `get_root_directory`.
    Available,
}

/// A `StorageManager` that manages access to a directory it is given at
/// creation time.  `InsecureKeyDirectoryStorageManager` emulates, but does not
/// provide, secure storage accessed with a locking `Key`.  It is intended only
/// for use during testing.
///
/// `InsecureKeyDirectoryStorageManager` maintains a `root` subdirectory
/// in the managed directory for which it gives out handles through
/// `get_root_dir`.  It also maintains a `keyfile` file which contains the
/// correct key.
///
/// The contents of `managed_dir` appear as follows in each state:
///  * *Uninitialized* - `managed_dir` is empty
///  * *Locked* - `managed_dir` contains a `root` subdirectory and a `keyfile`
///               file that contains the correct key.
///  * *Available* - `managed_dir` appears the same as in the *Locked* state.
///
/// Note: The directory provided to `InsecureKeyDirectoryStorageManager` does
/// not need to be mounted to the component's namespace, the only requirement
/// is that it is accessible via a `DirectoryProxy`.
#[derive(Debug)]
pub struct InsecureKeyDirectoryStorageManager {
    /// The current state of the `InsecureKeyDirectoryStorageManager`.
    state: Mutex<StorageManagerState>,
    /// A handle to the root of the managed directory.
    managed_dir: fio::DirectoryProxy,
}

#[async_trait]
impl StorageManager for InsecureKeyDirectoryStorageManager {
    type Key = Key;

    async fn provision(&self, key: &Self::Key) -> Result<(), AccountManagerError> {
        let mut state_lock = self.state.lock().await;
        match *state_lock {
            StorageManagerState::Uninitialized => {
                self.store_correct_key(key).await?;
                fuchsia_fs::directory::create_directory_recursive(
                    &self.managed_dir,
                    CLIENT_ROOT_PATH,
                    fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
                )
                .await
                .map_err(|e| {
                    AccountManagerError::new(ApiError::Resource)
                        .with_cause(format_err!("Failed to create client root directory: {:?}", e))
                })?;
                *state_lock = StorageManagerState::Available;
                Ok(())
            }
            ref invalid_state @ _ => Err(AccountManagerError::new(ApiError::FailedPrecondition)
                .with_cause(format_err!(
                    "StorageManager provision called in the {:?} state",
                    invalid_state
                ))),
        }
    }

    async fn unlock(&self, key: &Self::Key) -> Result<(), AccountManagerError> {
        let mut state_lock = self.state.lock().await;
        match *state_lock {
            StorageManagerState::Locked => {
                self.check_unlock_key(key).await?;
                *state_lock = StorageManagerState::Available;
                Ok(())
            }
            ref invalid_state @ _ => Err(AccountManagerError::new(ApiError::FailedPrecondition)
                .with_cause(format_err!(
                    "StorageManager unlock called in the {:?} state",
                    invalid_state
                ))),
        }
    }

    async fn lock(&self) -> Result<(), AccountManagerError> {
        let mut state_lock = self.state.lock().await;
        match *state_lock {
            StorageManagerState::Available => {
                *state_lock = StorageManagerState::Locked;
                Ok(())
            }
            ref invalid_state @ _ => Err(AccountManagerError::new(ApiError::FailedPrecondition)
                .with_cause(format_err!(
                    "StorageManager lock called in the {:?} state",
                    invalid_state
                ))),
        }
    }

    async fn destroy(&self) -> Result<(), AccountManagerError> {
        let mut state_lock = self.state.lock().await;
        match *state_lock {
            StorageManagerState::Locked => {
                fuchsia_fs::directory::remove_dir_recursive(&self.managed_dir, CLIENT_ROOT_PATH)
                    .await
                    .map_err(|e| {
                        AccountManagerError::new(ApiError::Unknown).with_cause(format_err!(
                            "Failed to destroy client root directory: {:?}",
                            e
                        ))
                    })?;

                let remove_result = self
                    .managed_dir
                    .unlink(KEY_FILE_PATH, fio::UnlinkOptions::EMPTY)
                    .await
                    .map_err(|e| {
                        AccountManagerError::new(ApiError::Resource)
                            .with_cause(format_err!("Failed to destroy keyfile: {:?}", e))
                    })?;
                if let Err(remove_status) = remove_result {
                    return Err(AccountManagerError::new(ApiError::Resource).with_cause(
                        format_err!("Failed to destroy keyfile: {:?}", remove_status),
                    ));
                }

                *state_lock = StorageManagerState::Uninitialized;
                Ok(())
            }
            ref invalid_state @ _ => Err(AccountManagerError::new(ApiError::FailedPrecondition)
                .with_cause(format_err!(
                    "StorageManager destroy called in the {:?} state",
                    invalid_state
                ))),
        }
    }

    async fn get_root_dir(&self) -> Result<fio::DirectoryProxy, AccountManagerError> {
        let state_lock = self.state.lock().await;
        match *state_lock {
            StorageManagerState::Available => fuchsia_fs::open_directory(
                &self.managed_dir,
                Path::new(CLIENT_ROOT_PATH),
                fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            )
            .account_manager_error(ApiError::Resource),
            ref invalid_state @ _ => Err(AccountManagerError::new(ApiError::FailedPrecondition)
                .with_cause(format_err!(
                    "StorageManager get_root_dir called in the {:?} state",
                    invalid_state
                ))),
        }
    }
}

impl InsecureKeyDirectoryStorageManager {
    /// Create a new `InsecureKeyDirectoryStorageManager` that manages the given
    /// directory.  The directory must be either empty or previously managed
    /// by a `InsecureKeyDirectoryStorageManager`.
    #[allow(dead_code)]
    pub async fn new(managed_dir: fio::DirectoryProxy) -> Result<Self, AccountManagerError> {
        // check internal state of filesystem to derive state
        let dir_entries = fuchsia_fs::directory::readdir(&managed_dir)
            .await
            .account_manager_error(ApiError::Resource)?;

        let state = if dir_entries.as_slice() == EXPECTED_DIRECTORY_ENTRIES.as_slice() {
            StorageManagerState::Locked
        } else if dir_entries.is_empty() {
            StorageManagerState::Uninitialized
        } else {
            return Err(AccountManagerError::new(ApiError::Internal)
                .with_cause(format_err!("Cannot determine StorageManager state")));
        };
        Ok(InsecureKeyDirectoryStorageManager { state: Mutex::new(state), managed_dir })
    }

    /// Stores the correct key.
    async fn store_correct_key(&self, key: &Key) -> Result<(), AccountManagerError> {
        let serialized_key = serde_json::to_string(key).map_err(|e| {
            AccountManagerError::new(ApiError::Internal)
                .with_cause(format_err!("Failed to serialize correct key: {:?}", e))
        })?;
        let key_file = fuchsia_fs::open_file(
            &self.managed_dir,
            &Path::new(KEY_FILE_PATH),
            fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .map_err(|e| {
            AccountManagerError::new(ApiError::Resource)
                .with_cause(format_err!("Failed to open keyfile while saving key: {:?}", e))
        })?;
        fuchsia_fs::write_file(&key_file, &serialized_key).await.map_err(|e| {
            AccountManagerError::new(ApiError::Resource)
                .with_cause(format_err!("Failed to write key to keyfile: {:?}", e))
        })
    }

    /// Verify if the given key is the correct key needed for unlock.
    async fn check_unlock_key(&self, key: &Key) -> Result<(), AccountManagerError> {
        let file_proxy = fuchsia_fs::open_file(
            &self.managed_dir,
            &Path::new(KEY_FILE_PATH),
            fio::OpenFlags::RIGHT_READABLE,
        )
        .map_err(|e| {
            AccountManagerError::new(ApiError::Resource)
                .with_cause(format_err!("Failed to open keyfile: {:?}", e))
        })?;
        let serialized_correct_key = fuchsia_fs::read_file(&file_proxy).await.map_err(|e| {
            AccountManagerError::new(ApiError::Resource)
                .with_cause(format_err!("Failed to read keyfile: {:?}", e))
        })?;
        let correct_key = serde_json::from_str(&serialized_correct_key).map_err(|e| {
            AccountManagerError::new(ApiError::Internal)
                .with_cause(format_err!("Failed to deserialize correct key: {:?}", e))
        })?;
        if key == &correct_key {
            Ok(())
        } else {
            Err(AccountManagerError::new(ApiError::FailedAuthentication))
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fuchsia_async as fasync;
    use futures::prelude::*;
    use tempfile::TempDir;

    lazy_static! {
        static ref CUSTOM_KEY_CONTENTS: [u8; 32] = [8u8; 32];
        static ref CUSTOM_KEY: Key = Key::Key256Bit(CUSTOM_KEY_CONTENTS.clone());
    }

    /// Creates a TempDir and DirectoryProxy handle to it.  The TempDir must
    /// be kept in scope for the duration that DirectoryProxy is used.
    fn create_temp_directory() -> (TempDir, fio::DirectoryProxy) {
        let temp_dir = TempDir::new().unwrap();
        let dir_proxy = fuchsia_fs::directory::open_in_namespace(
            temp_dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .unwrap();
        (temp_dir, dir_proxy)
    }

    async fn create_file_with_content(dir: &fio::DirectoryProxy, path: &str, content: &str) {
        let file = fuchsia_fs::open_file(
            dir,
            Path::new(path),
            fio::OpenFlags::RIGHT_WRITABLE | fio::OpenFlags::CREATE,
        )
        .unwrap();
        fuchsia_fs::write_file(&file, content).await.unwrap();
    }

    async fn assert_directory_empty(dir: &fio::DirectoryProxy) {
        let dir_entries = fuchsia_fs::directory::readdir(dir).await.unwrap();
        assert!(dir_entries.is_empty());
    }

    async fn assert_file_contents(dir: &fio::DirectoryProxy, path: &str, content: &str) {
        let file =
            fuchsia_fs::open_file(dir, Path::new(path), fio::OpenFlags::RIGHT_READABLE).unwrap();
        let file_content = fuchsia_fs::read_file(&file).await.unwrap();
        assert_eq!(content, &file_content);
    }

    /// Runs a test multiple times - once for each valid Key variation.
    async fn run_with_key_variations<F, Fut>(test_fn: F)
    where
        F: Fn(Key) -> Fut,
        Fut: Future<Output = ()>,
    {
        test_fn(Key::NoSpecifiedKey).await;
        test_fn(Key::Key256Bit(CUSTOM_KEY_CONTENTS.clone())).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_directory_storage_manager_destroy_removes_files() {
        run_with_key_variations(|key| async move {
            let (_dir, dir_proxy) = create_temp_directory();
            let manager = InsecureKeyDirectoryStorageManager::new(dir_proxy).await.unwrap();
            manager.provision(&key).await.unwrap();

            // Write some data to directories
            let data_dir = manager.get_root_dir().await.unwrap();
            create_file_with_content(&data_dir, "test-data-file", "test-data-content").await;
            // Drop the data dir reference - this is a precondition of calling lock().
            std::mem::drop(data_dir);

            manager.lock().await.unwrap();
            manager.destroy().await.unwrap();
            manager.provision(&key).await.unwrap();

            // Files should no longer exist.
            let data_dir = manager.get_root_dir().await.unwrap();
            assert_directory_empty(&data_dir).await;
        })
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_directory_storage_manager_files_persist_across_lock() {
        run_with_key_variations(|key| async move {
            let (_dir, dir_proxy) = create_temp_directory();
            let manager = InsecureKeyDirectoryStorageManager::new(dir_proxy).await.unwrap();
            manager.provision(&key).await.unwrap();

            // Write some data to directories
            let data_dir = manager.get_root_dir().await.unwrap();
            create_file_with_content(&data_dir, "test-data-file", "test-data-content").await;
            std::mem::drop(data_dir);

            manager.lock().await.unwrap();
            manager.unlock(&key).await.unwrap();

            // Files should still exist.
            let data_dir = manager.get_root_dir().await.unwrap();
            assert_file_contents(&data_dir, "test-data-file", "test-data-content").await;
        })
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_directory_storage_manager_files_persist_across_instances() {
        run_with_key_variations(|key| async move {
            let (dir, dir_proxy) = create_temp_directory();
            let manager = InsecureKeyDirectoryStorageManager::new(dir_proxy).await.unwrap();
            manager.provision(&key).await.unwrap();

            // Write some data to directories
            let data_dir = manager.get_root_dir().await.unwrap();
            create_file_with_content(&data_dir, "test-data-file", "test-data-content").await;
            std::mem::drop(data_dir);
            std::mem::drop(manager);

            // Create a new manager with the same directory.
            let new_dir_proxy = fuchsia_fs::directory::open_in_namespace(
                dir.path().to_str().unwrap(),
                fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            )
            .unwrap();

            let new_manager = InsecureKeyDirectoryStorageManager::new(new_dir_proxy).await.unwrap();
            new_manager.unlock(&key).await.unwrap();

            // Files should still exist.
            let data_dir = new_manager.get_root_dir().await.unwrap();
            assert_file_contents(&data_dir, "test-data-file", "test-data-content").await;
        })
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_directory_storage_manager_unlock_failed_authentication() {
        // Key256Bit given when NoSpecifiedKey expected
        let (_dir, dir_proxy) = create_temp_directory();
        let manager = InsecureKeyDirectoryStorageManager::new(dir_proxy).await.unwrap();
        manager.provision(&Key::NoSpecifiedKey).await.unwrap();
        manager.lock().await.unwrap();
        assert_eq!(
            manager.unlock(&*CUSTOM_KEY).await.unwrap_err().api_error,
            ApiError::FailedAuthentication
        );

        // NoSpecifiedKey given when Key256Bit expected
        let (_dir, dir_proxy) = create_temp_directory();
        let manager = InsecureKeyDirectoryStorageManager::new(dir_proxy).await.unwrap();
        manager.provision(&*CUSTOM_KEY).await.unwrap();
        manager.lock().await.unwrap();
        assert_eq!(
            manager.unlock(&Key::NoSpecifiedKey).await.unwrap_err().api_error,
            ApiError::FailedAuthentication
        );

        // Wrong Key256Bit given
        let (_dir, dir_proxy) = create_temp_directory();
        let manager = InsecureKeyDirectoryStorageManager::new(dir_proxy).await.unwrap();
        manager.provision(&*CUSTOM_KEY).await.unwrap();
        manager.lock().await.unwrap();
        let wrong_key = [99; 32];
        assert_eq!(
            manager.unlock(&Key::Key256Bit(wrong_key)).await.unwrap_err().api_error,
            ApiError::FailedAuthentication
        );

        // Key of all zeros given when NoSpecifiedKey expected
        let (_dir, dir_proxy) = create_temp_directory();
        let manager = InsecureKeyDirectoryStorageManager::new(dir_proxy).await.unwrap();
        manager.provision(&Key::NoSpecifiedKey).await.unwrap();
        manager.lock().await.unwrap();
        assert_eq!(
            manager.unlock(&Key::Key256Bit([0; 32])).await.unwrap_err().api_error,
            ApiError::FailedAuthentication
        );

        // NoSpecifiedKey given when key [0; 32] expected
        let (_dir, dir_proxy) = create_temp_directory();
        let manager = InsecureKeyDirectoryStorageManager::new(dir_proxy).await.unwrap();
        manager.provision(&Key::Key256Bit([0; 32])).await.unwrap();
        manager.lock().await.unwrap();
        assert_eq!(
            manager.unlock(&Key::NoSpecifiedKey).await.unwrap_err().api_error,
            ApiError::FailedAuthentication
        );
    }
}
