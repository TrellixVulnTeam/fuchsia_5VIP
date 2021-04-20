// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::manifest::{
        done_time, flash_partition, map_fidl_error, stage_file, verify_variable_value, Flash,
    },
    anyhow::{anyhow, bail, Context as _, Result},
    async_trait::async_trait,
    chrono::Utc,
    ffx_core::ffx_bail,
    ffx_flash_args::{FlashCommand, OemFile},
    fidl::endpoints::create_endpoints,
    fidl_fuchsia_developer_bridge::{FastbootProxy, RebootListenerMarker, RebootListenerRequest},
    futures::prelude::*,
    futures::try_join,
    serde::{Deserialize, Serialize},
    std::{io::Write, path::Path},
};

pub(crate) const MISSING_PRODUCT: &str = "Manifest does not contain product";
pub(crate) const MULTIPLE_PRODUCT: &str =
    "Multiple products found in manifest. Please specify a product";
pub(crate) const DEFAULT_PRODUCT: &str = "fuchsia";

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub(crate) struct Product {
    pub(crate) name: String,
    pub(crate) bootloader_partitions: Vec<Partition>,
    pub(crate) partitions: Vec<Partition>,
    pub(crate) oem_files: Vec<OemFile>,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub(crate) struct Partition(
    String,
    String,
    #[serde(default)] Option<String>,
    #[serde(default)] Option<String>,
);

impl Partition {
    pub(crate) fn name(&self) -> &str {
        self.0.as_str()
    }

    pub(crate) fn file(&self) -> &str {
        self.1.as_str()
    }

    pub(crate) fn variable(&self) -> Option<&str> {
        self.2.as_ref().map(|s| s.as_str())
    }

    pub(crate) fn variable_value(&self) -> Option<&str> {
        self.3.as_ref().map(|s| s.as_str())
    }
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub(crate) struct FlashManifest(pub(crate) Vec<Product>);

#[async_trait]
impl Flash for FlashManifest {
    async fn flash<W>(
        &self,
        writer: &mut W,
        fastboot_proxy: FastbootProxy,
        cmd: FlashCommand,
    ) -> Result<()>
    where
        W: Write + Send,
    {
        let path = Path::new(&cmd.manifest)
            .canonicalize()
            .context("Getting absolute path of flashing manifest")?;
        let product = match cmd.product {
            Some(p) => match self.0.iter().find(|product| product.name == p) {
                Some(res) => res,
                None => ffx_bail!("{} {}", MISSING_PRODUCT, p),
            },
            None => {
                if self.0.len() == 1 {
                    &self.0[0]
                } else {
                    match self.0.iter().find(|product| product.name == DEFAULT_PRODUCT) {
                        Some(res) => res,
                        None => ffx_bail!("{}", MULTIPLE_PRODUCT),
                    }
                }
            }
        };
        flash_partitions(writer, &path, &product.bootloader_partitions, &fastboot_proxy).await?;
        if product.bootloader_partitions.len() > 0 {
            write!(writer, "Rebooting to bootloader... ")?;
            writer.flush()?;
            let (reboot_client, reboot_server) = create_endpoints::<RebootListenerMarker>()?;
            let mut stream = reboot_server.into_stream()?;
            let start_time = Utc::now();
            try_join!(
                fastboot_proxy.reboot_bootloader(reboot_client).map_err(map_fidl_error),
                async move {
                    if let Some(RebootListenerRequest::OnReboot { control_handle: _ }) =
                        stream.try_next().await?
                    {
                        Ok(())
                    } else {
                        bail!("Did not receive reboot signal");
                    }
                }
            )
            .and_then(|(reboot, _)| {
                let d = Utc::now().signed_duration_since(start_time);
                log::debug!("Reboot duration: {:.2}s", (d.num_milliseconds() / 1000));
                done_time(writer, d)?;
                reboot.map_err(|e| anyhow!("failed booting to bootloader: {:?}", e))
            })?;
        }
        flash_partitions(writer, &path, &product.partitions, &fastboot_proxy).await?;
        for oem_file in &product.oem_files {
            stage_file(writer, &path, oem_file.file(), &fastboot_proxy).await?;
            writeln!(writer, "Sending command \"{}\"", oem_file.command())?;
            fastboot_proxy.oem(oem_file.command()).await?.map_err(|_| {
                anyhow!("There was an error sending oem command \"{}\"", oem_file.command())
            })?;
        }
        for oem_file in &cmd.oem_stage {
            stage_file(writer, &path, oem_file.file(), &fastboot_proxy).await?;
            writeln!(writer, "Sending command \"{}\"", oem_file.command())?;
            fastboot_proxy.oem(oem_file.command()).await?.map_err(|_| {
                anyhow!("There was an error sending oem command \"{}\"", oem_file.command())
            })?;
        }
        fastboot_proxy
            .erase("misc")
            .await?
            .map_err(|_| anyhow!("Could not erase misc partition"))?;
        fastboot_proxy.set_active("a").await?.map_err(|_| anyhow!("Could not set active slot"))?;
        fastboot_proxy.continue_boot().await?.map_err(|_| anyhow!("Could not reboot device"))?;
        writeln!(writer, "Continuing to boot - this could take awhile")?;
        Ok(())
    }
}

pub(crate) async fn flash_partitions<W: Write + Send>(
    writer: &mut W,
    path: &Path,
    partitions: &Vec<Partition>,
    fastboot_proxy: &FastbootProxy,
) -> Result<()> {
    for partition in partitions {
        match (partition.variable(), partition.variable_value()) {
            (Some(var), Some(value)) => {
                if verify_variable_value(var, value, fastboot_proxy).await? {
                    flash_partition(
                        writer,
                        &path,
                        partition.name(),
                        partition.file(),
                        fastboot_proxy,
                    )
                    .await?;
                }
            }
            _ => {
                flash_partition(writer, &path, partition.name(), partition.file(), fastboot_proxy)
                    .await?
            }
        }
    }
    Ok(())
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;
    use crate::test::setup;
    use regex::Regex;
    use serde_json::from_str;
    use tempfile::NamedTempFile;

    const SIMPLE_MANIFEST: &'static str = r#"[
            {
                "name": "zedboot",
                "bootloader_partitions": [],
                "partitions": [
                    ["test1", "path1"],
                    ["test2", "path2"],
                    ["test3", "path3"],
                    ["test4", "path4"],
                    ["test5", "path5"]
                ],
                "oem_files": []
            }
    ]"#;

    const MANIFEST: &'static str = r#"[
        {
            "name": "zedboot",
            "bootloader_partitions": [
                ["test1", "path1"],
                ["test2", "path2"]
            ],
            "partitions": [
                ["test1", "path1"],
                ["test2", "path2"],
                ["test3", "path3"],
                ["test4", "path4"],
                ["test5", "path5"]
            ],
            "oem_files": [
                ["test1", "path1"],
                ["test2", "path2"]
            ]
        },
        {
            "name": "product",
            "bootloader_partitions": [],
            "partitions": [
                ["test10", "path10"],
                ["test20", "path20"],
                ["test30", "path30"]
            ],
            "oem_files": []
        }
    ]"#;

    const CONDITIONAL_MANIFEST: &'static str = r#"[
        {
            "name": "zedboot",
            "bootloader_partitions": [
                ["btest1", "bpath1", "var1", "value1"],
                ["btest2", "bpath2", "var2", "value2"],
                ["btest3", "bpath3", "var3", "value3"]
            ],
            "partitions": [],
            "oem_files": []
        }
    ]"#;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_deserializing_should_work() -> Result<()> {
        let v: FlashManifest = from_str(MANIFEST)?;
        let zedboot: &Product = &v.0[0];
        assert_eq!("zedboot", zedboot.name);
        assert_eq!(2, zedboot.bootloader_partitions.len());
        let bootloader_expected = [["test1", "path1"], ["test2", "path2"]];
        for x in 0..bootloader_expected.len() {
            assert_eq!(zedboot.bootloader_partitions[x].name(), bootloader_expected[x][0]);
            assert_eq!(zedboot.bootloader_partitions[x].file(), bootloader_expected[x][1]);
        }
        assert_eq!(5, zedboot.partitions.len());
        let expected = [
            ["test1", "path1"],
            ["test2", "path2"],
            ["test3", "path3"],
            ["test4", "path4"],
            ["test5", "path5"],
        ];
        for x in 0..expected.len() {
            assert_eq!(zedboot.partitions[x].name(), expected[x][0]);
            assert_eq!(zedboot.partitions[x].file(), expected[x][1]);
        }
        assert_eq!(2, zedboot.oem_files.len());
        let oem_files_expected = [["test1", "path1"], ["test2", "path2"]];
        for x in 0..oem_files_expected.len() {
            assert_eq!(zedboot.oem_files[x].command(), oem_files_expected[x][0]);
            assert_eq!(zedboot.oem_files[x].file(), oem_files_expected[x][1]);
        }
        let product: &Product = &v.0[1];
        assert_eq!("product", product.name);
        assert_eq!(0, product.bootloader_partitions.len());
        assert_eq!(3, product.partitions.len());
        let expected2 = [["test10", "path10"], ["test20", "path20"], ["test30", "path30"]];
        for x in 0..expected2.len() {
            assert_eq!(product.partitions[x].name(), expected2[x][0]);
            assert_eq!(product.partitions[x].file(), expected2[x][1]);
        }
        assert_eq!(0, product.oem_files.len());
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_no_product_should_succeed_if_one_product() -> Result<()> {
        let v: FlashManifest = from_str(SIMPLE_MANIFEST)?;
        let tmp_file = NamedTempFile::new().expect("tmp access failed");
        let tmp_file_name = tmp_file.path().to_string_lossy().to_string();
        let (_, proxy) = setup();
        let mut writer = Vec::<u8>::new();
        v.flash(&mut writer, proxy, FlashCommand { manifest: tmp_file_name, ..Default::default() })
            .await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_no_product_should_fail_if_multiple_products() -> Result<()> {
        let v: FlashManifest = from_str(MANIFEST)?;
        let tmp_file = NamedTempFile::new().expect("tmp access failed");
        let tmp_file_name = tmp_file.path().to_string_lossy().to_string();
        let (_, proxy) = setup();
        let mut writer = Vec::<u8>::new();
        assert!(v
            .flash(
                &mut writer,
                proxy,
                FlashCommand { manifest: tmp_file_name, ..Default::default() },
            )
            .await
            .is_err());
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_no_product_should_fail_if_product_missing() -> Result<()> {
        let tmp_file = NamedTempFile::new().expect("tmp access failed");
        let tmp_file_name = tmp_file.path().to_string_lossy().to_string();
        let v: FlashManifest = from_str(MANIFEST)?;
        let (_, proxy) = setup();
        let mut writer = Vec::<u8>::new();
        assert!(v
            .flash(
                &mut writer,
                proxy,
                FlashCommand {
                    manifest: tmp_file_name,
                    product: Some("Unknown".to_string()),
                    ..Default::default()
                }
            )
            .await
            .is_err());
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_should_succeed_if_product_found() -> Result<()> {
        let v: FlashManifest = from_str(MANIFEST)?;
        let tmp_file = NamedTempFile::new().expect("tmp access failed");
        let tmp_file_name = tmp_file.path().to_string_lossy().to_string();
        let (_, proxy) = setup();
        let mut writer = Vec::<u8>::new();
        v.flash(
            &mut writer,
            proxy,
            FlashCommand {
                manifest: tmp_file_name,
                product: Some("product".to_string()),
                ..Default::default()
            },
        )
        .await?;
        let output = String::from_utf8(writer).expect("utf-8 string");
        for partition in &v.0[1].partitions {
            let name_listing = Regex::new(&partition.name()).expect("test regex");
            let path_listing = Regex::new(&partition.file()).expect("test regex");
            assert_eq!(name_listing.find_iter(&output).count(), 1);
            assert_eq!(path_listing.find_iter(&output).count(), 1);
        }
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_oem_file_should_be_staged_from_command() -> Result<()> {
        let v: FlashManifest = from_str(SIMPLE_MANIFEST)?;
        let (state, proxy) = setup();
        let test_oem_cmd = "test-oem-cmd";
        let tmp_file = NamedTempFile::new().expect("tmp access failed");
        let tmp_file_name = tmp_file.path().to_string_lossy().to_string();
        let test_staged_file = format!("{},{}", test_oem_cmd, tmp_file_name).parse::<OemFile>()?;
        let manifest_file = NamedTempFile::new().expect("tmp access failed");
        let manifest_file_name = manifest_file.path().to_string_lossy().to_string();
        let mut writer = Vec::<u8>::new();
        v.flash(
            &mut writer,
            proxy,
            FlashCommand {
                manifest: manifest_file_name,
                oem_stage: vec![test_staged_file],
                ..Default::default()
            },
        )
        .await?;
        let state = state.lock().unwrap();
        assert_eq!(1, state.staged_files.len());
        assert_eq!(1, state.oem_commands.len());
        assert_eq!(test_oem_cmd, state.oem_commands[0]);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_should_upload_conditional_partitions_that_match() -> Result<()> {
        let v: FlashManifest = from_str(CONDITIONAL_MANIFEST)?;
        let tmp_file = NamedTempFile::new().expect("tmp access failed");
        let tmp_file_name = tmp_file.path().to_string_lossy().to_string();
        let (state, proxy) = setup();
        {
            let mut state = state.lock().unwrap();
            state.variables.push("not_value3".to_string());
            state.variables.push("value2".to_string());
            state.variables.push("not_value1".to_string());
        }
        let mut writer = Vec::<u8>::new();
        v.flash(
            &mut writer,
            proxy,
            FlashCommand {
                manifest: tmp_file_name,
                product: Some("zedboot".to_string()),
                ..Default::default()
            },
        )
        .await?;
        let output = String::from_utf8(writer).expect("utf-8 string");
        for (i, partition) in v.0[0].bootloader_partitions.iter().enumerate() {
            let name_listing = Regex::new(&partition.name()).expect("test regex");
            let path_listing = Regex::new(&partition.file()).expect("test regex");
            let expected = if i == 1 { 1 } else { 0 };
            assert_eq!(name_listing.find_iter(&output).count(), expected);
            println!("{}", partition.file());
            assert_eq!(path_listing.find_iter(&output).count(), expected);
        }
        Ok(())
    }
}
