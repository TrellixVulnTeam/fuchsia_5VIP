// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::event::{Event, Publisher};
use crate::message::base::MessengerType;
use crate::service;
use anyhow::{format_err, Error};
use fidl::endpoints::{DiscoverableProtocolMarker, ProtocolMarker, Proxy};
use fuchsia_async as fasync;
use fuchsia_component::client::{connect_to_protocol, connect_to_protocol_at_path};
use fuchsia_zircon as zx;
use futures::future::{BoxFuture, OptionFuture};
use glob::glob;
use std::fmt::Debug;
use std::future::Future;

pub type GenerateService =
    Box<dyn Fn(&str, zx::Channel) -> BoxFuture<'static, Result<(), Error>> + Send + Sync>;

/// A wrapper around service operations, allowing redirection to a nested
/// environment.
pub struct ServiceContext {
    generate_service: Option<GenerateService>,
    delegate: Option<service::message::Delegate>,
}

impl ServiceContext {
    pub(crate) fn new(
        generate_service: Option<GenerateService>,
        delegate: Option<service::message::Delegate>,
    ) -> Self {
        Self { generate_service, delegate }
    }

    async fn make_publisher(&self) -> Option<Publisher> {
        let maybe: OptionFuture<_> = self
            .delegate
            .as_ref()
            .map(|delegate| Publisher::create(delegate, MessengerType::Unbound))
            .into();
        maybe.await
    }

    /// Connect to a service with the given ProtocolMarker.
    ///
    /// If a GenerateService was specified at creation, the name of the service marker will be used
    /// to generate a service.
    pub(crate) async fn connect<P: DiscoverableProtocolMarker>(
        &self,
    ) -> Result<ExternalServiceProxy<P::Proxy>, Error> {
        let proxy = if let Some(generate_service) = &self.generate_service {
            let (client, server) = zx::Channel::create()?;
            ((generate_service)(P::PROTOCOL_NAME, server)).await?;
            P::Proxy::from_channel(fasync::Channel::from_channel(client)?)
        } else {
            connect_to_protocol::<P>()?
        };

        Ok(ExternalServiceProxy::new(proxy, self.make_publisher().await))
    }

    pub(crate) async fn connect_with_publisher<P: DiscoverableProtocolMarker>(
        &self,
        publisher: Publisher,
    ) -> Result<ExternalServiceProxy<P::Proxy>, Error> {
        let proxy = if let Some(generate_service) = &self.generate_service {
            let (client, server) = zx::Channel::create()?;
            ((generate_service)(P::PROTOCOL_NAME, server)).await?;
            P::Proxy::from_channel(fasync::Channel::from_channel(client)?)
        } else {
            connect_to_protocol::<P>()?
        };

        Ok(ExternalServiceProxy::new(proxy, Some(publisher)))
    }

    /// Connect to a service with the given name and ProtocolMarker.
    ///
    /// If a GenerateService was specified at creation, the given name will be used to generate a
    /// service.
    pub(crate) async fn connect_named<P: ProtocolMarker>(
        &self,
        service_name: &str,
    ) -> Result<ExternalServiceProxy<P::Proxy>, Error> {
        if let Some(generate_service) = &self.generate_service {
            let (client, server) = zx::Channel::create()?;
            if (generate_service)(service_name, server).await.is_err() {
                return Err(format_err!("Could not handl service {:?}", service_name));
            }

            Ok(ExternalServiceProxy::new(
                P::Proxy::from_channel(fasync::Channel::from_channel(client)?),
                self.make_publisher().await,
            ))
        } else {
            Err(format_err!("No service generator"))
        }
    }

    /// Connect to a service at the given path and ProtocolMarker.
    ///
    /// If a GenerateService was specified at creation, the name of the service marker will be used
    /// to generate a service and the path will be ignored.
    pub(crate) async fn connect_path<P: ProtocolMarker>(
        &self,
        path: &str,
    ) -> Result<ExternalServiceProxy<P::Proxy>, Error> {
        let (proxy, server) = fidl::endpoints::create_proxy::<P>()?;
        fdio::service_connect(path, server.into_channel())?;

        Ok(ExternalServiceProxy::new(proxy, self.make_publisher().await))
    }

    /// Connect to a service by discovering a hardware device at the given glob-style pattern.
    ///
    /// The first discovered path will be used to connected.
    ///
    /// If a GenerateService was specified at creation, the name of the service marker will be used
    /// to generate a service and the path will be ignored.
    pub(crate) async fn connect_device_path<P: DiscoverableProtocolMarker>(
        &self,
        glob_pattern: &str,
    ) -> Result<ExternalServiceProxy<P::Proxy>, Error> {
        if self.generate_service.is_some() {
            // If a generate_service is already specified, just connect through there
            return self.connect::<P>().await;
        }

        let found_path = glob(glob_pattern)?
            .filter_map(|entry| entry.ok())
            .next()
            .ok_or_else(|| format_err!("failed to enumerate devices"))?;

        let path_str =
            found_path.to_str().ok_or_else(|| format_err!("failed to convert path to str"))?;

        Ok(ExternalServiceProxy::new(
            connect_to_protocol_at_path::<P>(path_str)?,
            self.make_publisher().await,
        ))
    }

    pub(crate) async fn wrap_proxy<P: Proxy>(&self, proxy: P) -> ExternalServiceProxy<P> {
        ExternalServiceProxy::new(proxy, self.make_publisher().await)
    }
}

/// Definition of events related to external api calls outside of the setting service.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum ExternalServiceEvent {
    /// Event sent when an external service proxy is created. Contains
    /// the protocol name.
    Created(&'static str),

    /// Event sent when a call is made on an external service proxy.
    /// Contains the protocol name and the stringified request.
    ApiCall(&'static str, String),

    /// Event sent when a non-error response is received on an external
    /// service proxy. Contains the protocol name and the response.
    ApiResponse(&'static str, String),

    /// Event sent when an error is received on an external service proxy.
    /// Contains the protocol name and the error message.
    ApiError(&'static str, String),

    /// Event sent when an external service proxy is closed. Contains the
    /// protocol name.
    Closed(&'static str),
}

/// A wrapper around a proxy, used to track disconnections.
///
/// This wraps any type implementing `Proxy`. Whenever any call returns a
/// `ClientChannelClosed` error, this wrapper publishes a closed event for
/// the wrapped proxy.
#[derive(Clone, Debug)]
pub struct ExternalServiceProxy<P>
where
    P: Proxy,
{
    proxy: P,
    publisher: Option<Publisher>,
}

impl<P> ExternalServiceProxy<P>
where
    P: Proxy,
{
    pub(crate) fn new(proxy: P, publisher: Option<Publisher>) -> Self {
        Self { proxy, publisher }
    }

    /// Handle the `result` of the event sent via the call or call_async methods
    /// and send the corresponding information to be logged to inspect.
    fn inspect_result<T>(&self, result: &Result<T, fidl::Error>)
    where
        T: std::fmt::Debug,
    {
        if let Some(p) = self.publisher.as_ref() {
            if let Err(fidl::Error::ClientChannelClosed { .. }) = result {
                p.send_event(Event::ExternalServiceEvent(ExternalServiceEvent::Closed(
                    P::Protocol::DEBUG_NAME,
                )));
            } else if let Err(e) = result {
                p.send_event(Event::ExternalServiceEvent(ExternalServiceEvent::ApiError(
                    P::Protocol::DEBUG_NAME,
                    format!("{e:?}"),
                )));
            } else {
                let payload = result.as_ref().expect("Could not extract external api call result");
                p.send_event(Event::ExternalServiceEvent(ExternalServiceEvent::ApiResponse(
                    P::Protocol::DEBUG_NAME,
                    format!("{payload:?}"),
                )));
            }
        }
    }

    /// Make a call to a synchronous API of the wrapped proxy. This should not be called directly,
    /// only from the call macro.
    pub(crate) fn call<T, F>(&self, func: F, arg_str: String) -> Result<T, fidl::Error>
    where
        F: FnOnce(&P) -> Result<T, fidl::Error>,
        T: std::fmt::Debug,
    {
        if let Some(p) = self.publisher.as_ref() {
            p.send_event(Event::ExternalServiceEvent(ExternalServiceEvent::ApiCall(
                P::Protocol::DEBUG_NAME,
                arg_str,
            )));
        }
        let result = func(&self.proxy);
        self.inspect_result(&result);
        result
    }

    /// Make a call to an asynchronous API of the wrapped proxy. This should not be called directly,
    /// only from the call_async macro.
    pub(crate) async fn call_async<T, F, Fut>(
        &self,
        func: F,
        arg_str: String,
    ) -> Result<T, fidl::Error>
    where
        F: FnOnce(&P) -> Fut,
        Fut: Future<Output = Result<T, fidl::Error>>,
        T: std::fmt::Debug,
    {
        if let Some(p) = self.publisher.as_ref() {
            p.send_event(Event::ExternalServiceEvent(ExternalServiceEvent::ApiCall(
                P::Protocol::DEBUG_NAME,
                arg_str,
            )));
        }
        let result = func(&self.proxy).await;
        self.inspect_result(&result);
        result
    }
}

/// Helper macro to simplify calls to proxy objects.
#[macro_export]
macro_rules! call {
    ($proxy:expr => $($call:tt)+) => {
        {
            let arg_string = $crate::format_call!($($call)+);
            $proxy.call(|p| p.$($call)+, arg_string)
        }
    };
}

/// Helper macro to simplify async calls to proxy objects.
#[macro_export]
macro_rules! call_async {
    ($proxy:expr => $($call:tt)+) => {
        {
            let arg_string = $crate::format_call!($($call)+);
            $proxy.call_async(|p| p.$($call)+, arg_string)
        }
    };
}

/// Helper macro to parse and stringify the arguments to `call` and `call_async`.
#[macro_export]
macro_rules! format_call {
    ($fn_name:ident($($arg:expr),*)) => {
        {
            let mut s = format!("{}(", stringify!($fn_name));
            $(
                s += &format!("{:?}", $arg);
            )*

            s += ")";
            s
        }
    };
}
