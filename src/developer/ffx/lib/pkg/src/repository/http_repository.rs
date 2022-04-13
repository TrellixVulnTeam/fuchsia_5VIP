// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Tools to download a Fuchsia package from from a TUF repository.
//! See
//! - [Package](https://fuchsia.dev/fuchsia-src/concepts/packages/package?hl=en)
//! - [TUF](https://theupdateframework.io/)

use {
    crate::{
        repository::{Error, Repository, RepositoryBackend},
        resource::{Resource, ResourceRange},
    },
    anyhow::{anyhow, Context, Result},
    errors::ffx_bail,
    fidl_fuchsia_developer_ffx_ext::RepositorySpec,
    fuchsia_merkle::{Hash, MerkleTreeBuilder},
    fuchsia_pkg::{BlobInfo, MetaContents, MetaPackage, PackageManifestBuilder},
    futures::TryStreamExt,
    futures_lite::io::AsyncWriteExt,
    hyper::{
        body::HttpBody,
        client::{connect::Connect, Client},
        Body, StatusCode, Uri,
    },
    serde_json::Value,
    std::{
        fmt::Debug,
        fs::{metadata, File},
        path::{Path, PathBuf},
        time::SystemTime,
    },
    tuf::{
        interchange::Json,
        repository::{HttpRepositoryBuilder as TufHttpRepositoryBuilder, RepositoryProvider},
    },
    url::Url,
};

#[derive(Debug)]
pub struct HttpRepository<C> {
    client: Client<C, Body>,
    metadata_repo_url: Url,
    blob_repo_url: Url,
}

impl<C> HttpRepository<C> {
    pub fn new(
        client: Client<C, Body>,
        mut metadata_repo_url: Url,
        mut blob_repo_url: Url,
    ) -> Self {
        // `URL.join` treats urls with a trailing slash as a directory, and without as a file.
        // In the latter case, it will strip off the last segment before joining paths. Since the
        // metadata and blob url are directories, make sure they have a trailing slash.
        if !metadata_repo_url.path().ends_with('/') {
            metadata_repo_url.set_path(&format!("{}/", metadata_repo_url.path()));
        }

        if !blob_repo_url.path().ends_with('/') {
            blob_repo_url.set_path(&format!("{}/", blob_repo_url.path()));
        }

        Self { client, metadata_repo_url, blob_repo_url }
    }
}

impl<C> HttpRepository<C>
where
    C: Connect + Clone + Send + Sync + 'static,
{
    async fn fetch_from(
        &self,
        root: &Url,
        resource_path: &str,
        _range: ResourceRange,
    ) -> Result<Resource, Error> {
        let full_url = root.join(resource_path).map_err(|e| anyhow!(e))?;
        let uri = full_url.as_str().parse::<Uri>().map_err(|e| anyhow!(e))?;

        let resp = self
            .client
            .get(uri)
            .await
            .context(format!("fetching resource {}", full_url.as_str()))?;

        let body = match resp.status() {
            StatusCode::OK => resp.into_body(),
            StatusCode::NOT_FOUND => return Err(Error::NotFound),
            status_code => {
                return Err(Error::Other(anyhow!(
                    "Got error downloading resource, error is: {}",
                    status_code
                )))
            }
        };

        let content_len = body
            .size_hint()
            .exact()
            .ok_or_else(|| anyhow!("response did not include Content-Length"))?;

        Ok(Resource {
            content_len: content_len,
            total_len: content_len,
            stream: Box::pin(body.map_err(|e| std::io::Error::new(std::io::ErrorKind::Other, e))),
        })
    }
}

#[async_trait::async_trait]
impl<C> RepositoryBackend for HttpRepository<C>
where
    C: Connect + Clone + Debug + Send + Sync + 'static,
{
    fn spec(&self) -> RepositorySpec {
        RepositorySpec::Http {
            metadata_repo_url: self.metadata_repo_url.as_str().to_owned(),
            blob_repo_url: self.blob_repo_url.as_str().to_owned(),
        }
    }

    async fn fetch_metadata(
        &self,
        resource_path: &str,
        range: ResourceRange,
    ) -> Result<Resource, Error> {
        self.fetch_from(&self.metadata_repo_url, resource_path, range).await
    }

    async fn fetch_blob(
        &self,
        resource_path: &str,
        range: ResourceRange,
    ) -> Result<Resource, Error> {
        self.fetch_from(&self.blob_repo_url, resource_path, range).await
    }

    fn get_tuf_repo(&self) -> Result<Box<(dyn RepositoryProvider<Json> + 'static)>, Error> {
        Ok(Box::new(
            TufHttpRepositoryBuilder::<_, Json>::new(
                self.metadata_repo_url.clone().into(),
                self.client.clone(),
            )
            .build(),
        ) as Box<dyn RepositoryProvider<Json>>)
    }

    async fn blob_modification_time(&self, _path: &str) -> Result<Option<SystemTime>> {
        Ok(None)
    }
}

/// Download a package from a TUF repo.
///
/// `tuf_url`: The URL of the TUF repo.
/// `blob_url`: URL of Blobs Server.
/// `target_path`: Target path for the package to download.
/// `output_path`: Local path to save the downloaded package.
pub async fn package_download<C>(
    client: Client<C, Body>,
    tuf_url: String,
    blob_url: String,
    target_path: String,
    output_path: impl AsRef<Path>,
) -> Result<()>
where
    C: Connect + Clone + Debug + Send + Sync + 'static,
{
    let output_path = output_path.as_ref();

    let backend =
        Box::new(HttpRepository::new(client, Url::parse(&tuf_url)?, Url::parse(&blob_url)?));
    let repo = Repository::new("repo", backend).await?;

    let desc = repo
        .get_target_description(&target_path)
        .await?
        .context("missing target description here")?;

    let merkle = desc.custom().get("merkle").context("missing merkle")?;

    let meta_far_hash = if let Value::String(hash) = merkle {
        hash.parse()?
    } else {
        ffx_bail!("[Error] Merkle field is not a String. {:#?}", desc);
    };

    if !output_path.exists() {
        async_fs::create_dir_all(output_path).await?;
    }

    if output_path.is_file() {
        ffx_bail!("Download path is pointing to a file: {}", output_path.display());
    }

    let output_blobs_dir = output_path.join("blobs");
    if !output_blobs_dir.exists() {
        async_fs::create_dir_all(&output_blobs_dir).await?;
    }

    if output_blobs_dir.is_file() {
        ffx_bail!("Download path is pointing to a file: {}", output_blobs_dir.display());
    }

    // Download the meta.far and write it into the `blobs/` directory.
    let meta_far_path =
        download_blob_to_destination(&repo, &output_blobs_dir, &meta_far_hash).await?;

    // FIXME(http://fxbug.dev/97061): When this function was written, we downloaded the meta.far
    // blob to a toplevel file `meta.far`, rather than writing it into the `blobs/` directory. Lets
    // preserve this behavior for now until we can change downstream users from relying on this
    // functionality.
    std::fs::copy(&meta_far_path, output_path.join("meta.far"))?;

    let mut archive = File::open(&meta_far_path)?;
    let mut meta_far = fuchsia_archive::Reader::new(&mut archive)?;
    let meta_contents = meta_far.read_file("meta/contents")?;
    let meta_contents = MetaContents::deserialize(meta_contents.as_slice())?.into_contents();
    let meta_package = meta_far.read_file("meta/package")?;
    let meta_package = MetaPackage::deserialize(meta_package.as_slice())?;

    // Download all the blobs.
    let mut tasks = Vec::new();
    for hash in meta_contents.values() {
        tasks.push(download_blob_to_destination(&repo, &output_blobs_dir, hash));
    }

    futures::future::join_all(tasks).await;

    // Build the PackageManifest of this package.
    let mut builder = PackageManifestBuilder::new(meta_package);

    builder = builder.add_blob(BlobInfo {
        source_path: meta_far_path
            .to_str()
            .with_context(|| format!("Path is not valid UTF-8: {}", meta_far_path.display()))?
            .to_string(),
        path: "meta/".to_owned(),
        merkle: meta_far_hash,
        size: metadata(&meta_far_path)?.len(),
    });

    for (blob_path, hash) in meta_contents.iter() {
        let source_path = output_blobs_dir.join(&hash.to_string()).canonicalize()?;

        builder = builder.add_blob(BlobInfo {
            source_path: source_path
                .to_str()
                .with_context(|| format!("Path is not valid UTF-8: {}", source_path.display()))?
                .to_string(),
            path: blob_path.to_string(),
            merkle: hash.clone(),
            size: metadata(&source_path)?.len(),
        });
    }

    let package_manifest = builder.build();
    let package_manifest_path = output_path.join("package_manifest.json");
    let mut file = async_fs::File::create(package_manifest_path).await?;
    file.write_all(serde_json::to_string(&package_manifest)?.as_bytes()).await?;
    file.sync_all().await?;
    Ok(())
}

/// Download a blob from the repository and save it to the given
/// destination
/// `path`: Path on the server from which to download the package.
/// `repo`: A [Repository] instance.
/// `destination`: Local path to save the downloaded package.
async fn download_blob_to_destination(
    repo: &Repository,
    dir: &Path,
    blob: &Hash,
) -> Result<PathBuf> {
    let blob_str = blob.to_string();
    let path = dir.join(&blob_str);

    // If the local path already exists, check if has the correct merkle. If so, exit early.
    match async_fs::File::open(&path).await {
        Ok(mut file) => {
            let hash = fuchsia_merkle::from_async_read(&mut file).await?.root();
            if blob == &hash {
                return Ok(path);
            }
        }
        Err(err) => {
            if err.kind() != std::io::ErrorKind::NotFound {
                return Err(err.into());
            }
        }
    };

    // Otherwise download the blob into a temporary directory, and validate that it has the right
    // hash.
    let mut resource =
        repo.fetch_blob(&blob_str).await.with_context(|| format!("fetching {}", blob))?;

    let (file, temp_path) = tempfile::NamedTempFile::new_in(dir)?.into_parts();
    let mut file = async_fs::File::from(file);

    let mut merkle_builder = MerkleTreeBuilder::new();

    while let Some(chunk) = resource.stream.try_next().await? {
        merkle_builder.write(&chunk);
        file.write_all(&chunk).await?;
    }

    let hash = merkle_builder.finish().root();

    // Error out if the merkle doesn't match what we expected.
    if blob == &hash {
        // Flush the file to make sure all the bytes got written to disk.
        file.flush().await?;
        drop(file);

        temp_path.persist(&path)?;

        Ok(path)
    } else {
        Err(anyhow!("invalid merkle: expected {:?}, got {:?}", blob, hash))
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::{
            manager::RepositoryManager,
            server::RepositoryServer,
            test_utils::{make_pm_repository, PKG1_BIN_HASH},
        },
        camino::Utf8Path,
        fuchsia_async as fasync,
        fuchsia_hyper::new_https_client,
        std::{fs::create_dir, net::Ipv4Addr, sync::Arc},
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_download_package() {
        let tmp = tempfile::tempdir().unwrap();
        let dir = Utf8Path::from_path(tmp.path()).unwrap();

        let repo = make_pm_repository("tuf", dir.join("repo")).await;

        let manager = RepositoryManager::new();
        manager.add(Arc::new(repo));

        let addr = (Ipv4Addr::LOCALHOST, 0).into();
        let (server_fut, _, server) =
            RepositoryServer::builder(addr, Arc::clone(&manager)).start().await.unwrap();

        // Run the server in the background.
        let task = fasync::Task::local(server_fut);

        let tuf_url = server.local_url() + "/tuf";
        let blob_url = server.local_url() + "/tuf/blobs";

        let result_dir = dir.join("results");
        create_dir(&result_dir).unwrap();
        let client = new_https_client();
        let result = package_download(
            client,
            tuf_url,
            blob_url,
            String::from("package1"),
            result_dir.clone(),
        )
        .await;

        result.unwrap();

        let result_package_manifest =
            std::fs::read_to_string(result_dir.join("package_manifest.json")).unwrap();
        assert!(result_package_manifest.contains(PKG1_BIN_HASH));
        assert!(result_package_manifest.contains("meta/"));

        // Signal the server to shutdown.
        server.stop();

        // Wait for the server to actually shut down.
        task.await;
    }
}
