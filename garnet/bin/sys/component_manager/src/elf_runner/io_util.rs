// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{err_msg, format_err, Error},
    fdio,
    fidl::endpoints::create_proxy,
    fidl::endpoints::Proxy,
    fidl_fuchsia_io::{
        DirectoryEvent, DirectoryProxy, FileProxy, NodeInfo, NodeProxy, MODE_TYPE_DIRECTORY,
        MODE_TYPE_FILE, OPEN_FLAG_DESCRIBE, OPEN_RIGHT_READABLE,
    },
    fuchsia_async as fasync, fuchsia_zircon as zx,
    fuchsia_zircon::HandleBased,
    futures::TryStreamExt,
    std::ffi::CString,
    std::path::PathBuf,
    std::ptr,
};

/// open_node will return a NodeProxy opened to the node at the given path relative to the
/// given directory, or return an error if no such node exists (or some other FIDL error was
/// encountered).
pub async fn open_node<'a>(
    dir: &'a DirectoryProxy,
    path: &'a PathBuf,
    mode: u32,
) -> Result<NodeProxy, Error> {
    let dir_clone = clone_directory(dir)?;

    let path = path.to_str().ok_or(err_msg("path is invalid"))?;

    let flags = if mode == MODE_TYPE_DIRECTORY {
        OPEN_RIGHT_READABLE
    } else {
        OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE
    };

    let (new_node, server_end) = create_proxy()?;
    dir_clone.open(flags, mode, path, server_end)?;

    // TODO: this was added to debug something. Is it worth keeping this check?
    // check that the open was successful
    let mut event_stream = dir_clone.take_event_stream();
    if let Some(DirectoryEvent::OnOpen_ { s, info }) = await!(event_stream.try_next())? {
        if s != zx::sys::ZX_OK {
            return Err(format_err!("open failed: {}: {:?}", s, info));
        }
    } else {
        return Err(format_err!("no event generated by open"));
    }

    Ok(new_node)
}

/// open_directory will open a directory node at the given path relative to the given directory. If
/// the opened node is not a directory, an error will be returned.
pub async fn open_directory<'a>(
    dir: &'a DirectoryProxy,
    path: &'a PathBuf,
) -> Result<DirectoryProxy, Error> {
    let node = await!(open_node(dir, path, MODE_TYPE_DIRECTORY))?;
    match await!(node.describe()).map_err(|e| format_err!("describe failed! {}", e))? {
        NodeInfo::Directory { .. } => node_to_directory(node),
        _ => Err(format_err!(
            "{} is not a directory",
            path.to_str().ok_or(err_msg("path isn't a directory, and path isn't valid unicode"))?
        )),
    }
}

/// open_file will open a file node at the given path relative to the given directory. If the
/// opened node is not a file, an error will be returned.
pub async fn open_file<'a>(dir: &'a DirectoryProxy, path: &'a PathBuf) -> Result<FileProxy, Error> {
    let node = await!(open_node(dir, path, MODE_TYPE_FILE))?;
    match await!(node.describe()).map_err(|e| format_err!("describe failed! {}", e))? {
        NodeInfo::File { .. } => node_to_file(node),
        _ => Err(format_err!(
            "{} is not a file",
            path.to_str().ok_or(err_msg("path isn't a file, and path isn't valid unicode"))?
        )),
    }
}

// TODO: this function will block on the FDIO calls. This should be rewritten/wrapped/whatever to
// be asynchronous.
/// open_absolute_node will return a NodeProxy to the given path by using the default namespace
/// stored in fdio. The path argument must be an absolute path.
pub fn open_absolute_node(path: &str) -> Result<NodeProxy, Error> {
    let mut ns_ptr: *mut fdio::fdio_sys::fdio_ns_t = ptr::null_mut();
    let status = unsafe { fdio::fdio_sys::fdio_ns_get_installed(&mut ns_ptr) };
    if status != zx::sys::ZX_OK {
        return Err(format_err!("fdio_ns_get_installed error: {}", status));
    }

    let (proxy_chan, server_end) = zx::Channel::create()
        .map_err(|status| format_err!("zx::Channel::create error: {}", status))?;

    let cstr = CString::new(path)?;
    let status = unsafe {
        fdio::fdio_sys::fdio_ns_connect(
            ns_ptr,
            cstr.as_ptr(),
            OPEN_RIGHT_READABLE,
            server_end.into_raw(),
        )
    };
    if status != zx::sys::ZX_OK {
        return Err(format_err!("fdio_ns_connect error: {}", status));
    }

    return Ok(NodeProxy::new(fasync::Channel::from_channel(proxy_chan)?));
}

/// open_absolute_directory will open a NodeProxy to the given path, call describe on the
/// node, and return a DirectoryProxy if the node is indeed a directory. The path argument must be
/// an absolute path.
pub async fn open_absolute_directory(path: &str) -> Result<DirectoryProxy, Error> {
    let node = open_absolute_node(path)?;
    match await!(node.describe())? {
        NodeInfo::Directory { .. } => node_to_directory(node),
        _ => Err(format_err!("{} is not a directory", path)),
    }
}

/// open_absolute_file will open a NodeProxy to the given path, call describe on the node,
/// and return a FileProxy if the node is indeed a file. The path argument must be an absolute
/// path.
pub async fn open_absolute_file(path: &str) -> Result<FileProxy, Error> {
    let node = open_absolute_node(path)?;
    match await!(node.describe())? {
        NodeInfo::File { .. } => node_to_file(node),
        _ => Err(format_err!("{} is not a file", path)),
    }
}

/// node_to_directory will convert the given NodeProxy into a DirectoryProxy. This is unsafe if the
/// type of the node is not checked first.
pub fn node_to_directory(node: NodeProxy) -> Result<DirectoryProxy, Error> {
    let node_chan = node.into_channel().map_err(|e| format_err!("{:?}", e))?;
    Ok(DirectoryProxy::from_channel(node_chan))
}

/// node_to_file will convert the given NodeProxy into a FileProxy. This is unsafe if the
/// type of the node is not checked first.
pub fn node_to_file(node: NodeProxy) -> Result<FileProxy, Error> {
    let node_chan = node.into_channel().map_err(|e| format_err!("{:?}", e))?;
    Ok(FileProxy::from_channel(node_chan))
}

/// clone_directory will create a clone of the given DirectoryProxy by calling its clone function.
pub fn clone_directory(dir: &DirectoryProxy) -> Result<DirectoryProxy, Error> {
    let (node_clone, server_end) = create_proxy()?;
    dir.clone(OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE, server_end)?;
    node_to_directory(node_clone)
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_async as fasync};

    #[test]
    fn open_absolute_directory_test() {
        let mut executor = fasync::Executor::new().unwrap();

        executor
            .run_singlethreaded(open_absolute_directory("/pkgfs/packages/component_manager_tests"))
            .unwrap();
    }

    #[test]
    fn open_file_test() {
        let mut executor = fasync::Executor::new().unwrap();

        executor.run_singlethreaded(
            async {
                let dir = await!(open_absolute_directory("/pkgfs/packages/fortune/0/bin")).unwrap();
                let path = PathBuf::from("fortune");
                await!(open_file(&dir, &path)).unwrap()
            },
        );
    }
}
