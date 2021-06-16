// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Result};
use assembly_util::create_meta_package_file;
use fuchsia_hash::Hash;
use fuchsia_pkg::{CreationManifest, PackageManifest};
use std::collections::BTreeMap;
use std::fs::File;
use std::io::Write;
use std::path::Path;

/// The path to the static package index file in the `base` package.
const STATIC_PACKAGE_INDEX: &str = "data/static_packages";

/// The path to the cache package index file in the `base` package.
const CACHE_PACKAGE_INDEX: &str = "data/cache_packages";

/// A builder that constructs base packages.
#[derive(Default)]
pub struct BasePackageBuilder {
    // Maps the blob destination -> source.
    contents: BTreeMap<String, String>,
    base_packages: PackageList,
    cache_packages: PackageList,
}

impl BasePackageBuilder {
    /// Add all the blobs from `package` into the base package being built.
    pub fn add_files_from_package(&mut self, package: PackageManifest) {
        package.into_blobs().into_iter().filter(|b| b.path != "meta/").for_each(|b| {
            self.contents.insert(b.path, b.source_path);
        });
    }

    /// Add the `package` to the list of base packages, which is then added to
    /// base package as file `data/static_packages`.
    pub fn add_base_package(&mut self, package: PackageManifest) -> Result<()> {
        add_package_to(&mut self.base_packages, package)
    }

    /// Add the `package` to the list of cache packages, which is then added to
    /// base package as file `data/cache_packages`.
    pub fn add_cache_package(&mut self, package: PackageManifest) -> Result<()> {
        add_package_to(&mut self.cache_packages, package)
    }

    /// Build the base package and write the bytes to `out`.
    ///
    /// Intermediate files will be written to the directory specified by
    /// `gendir`.
    pub fn build(
        self,
        gendir: impl AsRef<Path>,
        out: &mut impl Write,
    ) -> Result<BasePackageBuildResults> {
        let Self { contents, base_packages, cache_packages } = self;

        // Capture the generated files
        let mut generated_files = BTreeMap::new();

        // Generate the base and cache package lists.
        let (dest, path) = write_index_file(&gendir, "base", STATIC_PACKAGE_INDEX, &base_packages)?;
        generated_files.insert(dest, path);

        let (dest, path) =
            write_index_file(&gendir, "cache", CACHE_PACKAGE_INDEX, &cache_packages)?;
        generated_files.insert(dest, path);

        // Construct the list of blobs in the base package that lives outside of the meta.far.
        let mut external_contents = contents;
        for (destination, source) in &generated_files {
            external_contents.insert(destination.clone(), source.clone());
        }

        let mut far_contents = BTreeMap::new();

        far_contents.insert(
            "meta/package".to_string(),
            create_meta_package_file(gendir, "system_image", "0")?,
        );

        // Build the base packages.
        let creation_manifest = CreationManifest::from_external_and_far_contents(
            external_contents.clone(),
            far_contents,
        )?;

        fuchsia_pkg::build(&creation_manifest, out)?;

        Ok(BasePackageBuildResults {
            contents: external_contents,
            base_packages,
            cache_packages,
            generated_files,
        })
    }
}

/// The results of building the `base` package.
///
/// These are based on the information that the builder is configured with, and
/// then augmented with the operations that the `BasePackageBuilder::build()`
/// fn performs, including an extra additions or removals.
///
/// This provides an audit trail of "what was created".
pub struct BasePackageBuildResults {
    // Maps the blob destination -> source.
    pub contents: BTreeMap<String, String>,
    pub base_packages: PackageList,
    pub cache_packages: PackageList,

    /// The paths to the files generated by the builder.
    pub generated_files: BTreeMap<String, String>,
}

// Pulls out the name and merkle from `package` and adds it to `packages` with a name to
// merkle mapping.
fn add_package_to(list: &mut PackageList, package: PackageManifest) -> Result<()> {
    let name = package.package_path()?.to_string();
    let meta_blob = package.into_blobs().into_iter().find(|blob| blob.path == "meta/");
    match meta_blob {
        Some(meta_blob) => Ok(list.insert(name, meta_blob.merkle)),
        _ => Err(anyhow!("Failed to add package {} to the list, unable to find meta blob", name)),
    }
}

/// Helper fn to handle the (repeated) process of writing a list of packages
/// out to the expected file, and returning a (destination, source) tuple
/// for inclusion in the base package's contents.
fn write_index_file(
    gendir: impl AsRef<Path>,
    name: &str,
    destination: impl AsRef<str>,
    package_list: &PackageList,
) -> Result<(String, String)> {
    // TODO(fxbug.dev/76326) Decide on a consistent pattern for using gendir and
    //   how intermediate files should be named and where in gendir they should
    //   be placed.
    //
    // For a file of destination "data/foo.txt", and a gendir of "assembly/gendir",
    //   this creates "assembly/gendir/data/foo.txt".
    let path = gendir.as_ref().join(destination.as_ref());
    let path_str = path
        .to_str()
        .ok_or(anyhow!(format!("package index path is not valid UTF-8: {}", path.display())))?;

    // Create any parent dirs necessary.
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent).context(format!(
            "Failed to create parent dir {} for {} in gendir",
            parent.display(),
            destination.as_ref()
        ))?;
    }

    let mut index_file = File::create(&path)
        .context(format!("Failed to create the {} packages index file: {}", name, path_str))?;

    package_list.write(&mut index_file).context(format!(
        "Failed to write the {} package index file: {}",
        name,
        path.display()
    ))?;

    Ok((destination.as_ref().to_string(), path_str.to_string()))
}

/// A list of mappings between package name and merkle, which can be written to
/// a file to be placed in the Base Package.
#[derive(Default, Debug)]
pub struct PackageList {
    // Map between package name and merkle.
    packages: BTreeMap<String, Hash>,
}

impl PackageList {
    /// Add a new package with `name` and `merkle`.
    pub fn insert(&mut self, name: impl AsRef<str>, merkle: Hash) {
        self.packages.insert(name.as_ref().to_string(), merkle);
    }

    /// Generate the file to be placed in the Base Package.
    pub fn write(&self, out: &mut impl Write) -> Result<()> {
        for (name, merkle) in self.packages.iter() {
            writeln!(out, "{}={}", name, merkle)?;
        }
        Ok(())
    }
}

#[cfg(test)]
impl PartialEq<Vec<(String, Hash)>> for PackageList {
    fn eq(&self, other: &Vec<(String, Hash)>) -> bool {
        if self.packages.len() == other.len() {
            for item in other {
                match self.packages.get(&item.0) {
                    Some(hash) => {
                        if hash != &item.1 {
                            return false;
                        }
                    }
                    None => {
                        return false;
                    }
                }
            }
            return true;
        }
        return false;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_archive::Reader;
    use serde_json::json;
    use std::io::Cursor;
    use std::path::Path;
    use tempfile::{NamedTempFile, TempDir};

    #[test]
    fn package_list() {
        let mut out: Vec<u8> = Vec::new();
        let mut packages = PackageList::default();
        packages.insert("package0", Hash::from([0u8; 32]));
        packages.insert("package1", Hash::from([17u8; 32]));
        packages.write(&mut out).unwrap();
        assert_eq!(
            out,
            b"package0=0000000000000000000000000000000000000000000000000000000000000000\n\
                    package1=1111111111111111111111111111111111111111111111111111111111111111\n"
        );
    }

    #[test]
    fn build() {
        // Build the base package with an extra file, a base package, and a cache package.
        let mut far_bytes: Vec<u8> = Vec::new();
        let mut builder = BasePackageBuilder::default();
        let test_file = NamedTempFile::new().unwrap();
        builder.add_files_from_package(generate_test_manifest(
            "package",
            "0",
            Some(test_file.path()),
        ));
        builder.add_base_package(generate_test_manifest("base_package", "0", None)).unwrap();
        builder.add_cache_package(generate_test_manifest("cache_package", "0", None)).unwrap();

        let gen_dir = TempDir::new().unwrap();
        let build_results = builder.build(&gen_dir.path(), &mut far_bytes).unwrap();

        // The following asserts lead up to the final one, catching earlier failure points where it
        // can be more obvious as to why the test is failing, as the hashes themselves are opaque.

        // Verify the package list intermediate structures.
        assert_eq!(
            build_results.base_packages,
            vec![("base_package/0".to_string(), Hash::from([0u8; 32]))]
        );
        assert_eq!(
            build_results.cache_packages,
            vec![("cache_package/0".to_string(), Hash::from([0u8; 32]))]
        );

        // Inspect the generated files to verify their contents.
        let gen_static_index = build_results.generated_files.get("data/static_packages").unwrap();
        assert_eq!(
            std::fs::read_to_string(gen_static_index).unwrap(),
            "base_package/0=0000000000000000000000000000000000000000000000000000000000000000\n"
        );

        let gen_cache_index = build_results.generated_files.get("data/cache_packages").unwrap();
        assert_eq!(
            std::fs::read_to_string(gen_cache_index).unwrap(),
            "cache_package/0=0000000000000000000000000000000000000000000000000000000000000000\n"
        );

        // Validate that the generated files are in the contents.
        for (generated_file, _) in &build_results.generated_files {
            assert!(
                build_results.contents.contains_key(generated_file),
                "Unable to find generated file in base package contents: {}",
                generated_file
            );
        }

        // Read the output and ensure it contains the right files (and their hashes)
        let mut far_reader = Reader::new(Cursor::new(far_bytes)).unwrap();
        let package = far_reader.read_file("meta/package").unwrap();
        assert_eq!(package, br#"{"name":"system_image","version":"0"}"#);
        let contents = far_reader.read_file("meta/contents").unwrap();
        let contents = std::str::from_utf8(&contents).unwrap();
        let expected_contents = "\
            data/cache_packages=812e4a20eeea95c8591d82c6efe33a9fef0f76d0ea085babc01e83a47c57f44e\n\
            data/file.txt=15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b\n\
            data/static_packages=2d86ccb37d003499bdc7bdd933428f4b83d9ed224d1b64ad90dc257d22cff460\n\
        "
        .to_string();
        assert_eq!(contents, expected_contents);
    }

    // Generates a package manifest to be used for testing. The `name` is used in the blob file
    // names to make each manifest somewhat unique. If supplied, `file_path` will be used as the
    // non-meta-far blob source path, which allows the tests to use a real file.
    fn generate_test_manifest(
        name: &str,
        version: &str,
        file_path: Option<&Path>,
    ) -> PackageManifest {
        let meta_source = format!("path/to/{}/meta.far", name);
        let file_source = match file_path {
            Some(path) => path.to_string_lossy().into_owned(),
            _ => format!("path/to/{}/file.txt", name),
        };
        serde_json::from_value::<PackageManifest>(json!(
            {
                "version": "1",
                "package": {
                    "name": name,
                    "version": version,
                },
                "blobs": [
                    {
                        "source_path": meta_source,
                        "path": "meta/",
                        "merkle":
                            "0000000000000000000000000000000000000000000000000000000000000000",
                        "size": 1
                    },

                    {
                        "source_path": file_source,
                        "path": "data/file.txt",
                        "merkle":
                            "1111111111111111111111111111111111111111111111111111111111111111",
                        "size": 1
                    },
                ]
            }
        ))
        .expect("valid json")
    }
}
