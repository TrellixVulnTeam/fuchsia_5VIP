// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A tool to:
//! - acquire and display product bundle information (metadata)
//! - acquire related data files, such as disk partition images (data)

use {
    anyhow::{Context, Result},
    ffx_config::sdk::Sdk,
    ffx_core::ffx_plugin,
    ffx_product_bundle_args::{
        CreateCommand, GetCommand, ListCommand, ProductBundleCommand, SubCommand,
    },
    pbms::{get_pbms, get_product_data},
    sdk_metadata::Metadata,
    std::io::{stdout, Write},
};

mod create;

/// Provide functionality to list product-bundle metadata, fetch metadata, and
/// pull images and related data.
#[ffx_plugin("product-bundle.experimental")]
pub async fn product_bundle(cmd: ProductBundleCommand) -> Result<()> {
    let sdk = ffx_config::get_sdk().await.context("config get sdk.")?;
    product_bundle_plugin_impl(sdk, cmd, &mut stdout()).await
}

/// Dispatch to a sub-command.
pub async fn product_bundle_plugin_impl<W>(
    sdk: Sdk,
    command: ProductBundleCommand,
    writer: &mut W,
) -> Result<()>
where
    W: Write + Sync,
{
    match &command.sub {
        SubCommand::List(cmd) => pb_list(sdk, writer, &cmd).await?,
        SubCommand::Get(cmd) => pb_get(sdk, writer, &cmd).await?,
        SubCommand::Create(cmd) => pb_create(&cmd).await?,
    }
    Ok(())
}

/// `ffx product-bundle list` sub-command.
async fn pb_list<W: Write + Sync>(_sdk: Sdk, writer: &mut W, cmd: &ListCommand) -> Result<()> {
    let entries = get_pbms(/*update_metadata=*/ !cmd.cached, /*verbose=*/ false, writer)
        .await
        .context("list pbms")?;
    let mut entry_names = entries
        .iter()
        .map(|entry| {
            match entry {
                Metadata::ProductBundleV1(bundle) => bundle.name.to_owned(),
                _ => {
                    // Returning an error would prevent all values from being
                    // displayed. This is a more graceful failure. This message
                    // is vague because the data may be newer or older (so
                    // asking that they update ffx isn't necessarily the right
                    // choice).
                    "[Unknown ProductBundle, may need another ffx version]".to_string()
                }
            }
        })
        .collect::<Vec<_>>();
    entry_names.sort();
    for entry in entry_names {
        writeln!(writer, "{}", entry)?;
    }
    Ok(())
}

/// `ffx product-bundle get` sub-command.
async fn pb_get<W: Write + Sync>(_sdk: Sdk, writer: &mut W, cmd: &GetCommand) -> Result<()> {
    get_product_data(&cmd.product_bundle_name, cmd.verbose, writer).await
}

/// `ffx product-bundle create` sub-command.
async fn pb_create(cmd: &CreateCommand) -> Result<()> {
    create::create_product_bundle(cmd).await
}

#[cfg(test)]
mod test {}
