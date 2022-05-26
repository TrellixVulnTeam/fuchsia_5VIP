// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::hash_map::HashMap;

use ethernet as eth;
use fidl_fuchsia_hardware_ethernet::Features;
use net_types::{ethernet::Mac, UnicastAddr};
use netstack3_core::{
    DeviceId, Entry, IdMapCollection, IdMapCollectionKey, Ipv4DeviceConfiguration,
    Ipv6DeviceConfiguration,
};

pub type BindingId = u64;

/// Keeps tabs on devices.
///
/// `Devices` keeps a list of devices that are installed in the netstack with
/// an associated netstack core ID `C` used to reference the device.
///
/// The type parameters `C` and `I` are for the core ID type and the extra
/// information associated with the device, respectively, and default to the
/// types used by `EventLoop` for brevity in the main use case. The type
/// parameters are there to allow testing without dependencies on `core`.
pub struct Devices<C: IdMapCollectionKey = DeviceId, I = DeviceSpecificInfo> {
    active_devices: IdMapCollection<C, DeviceInfo<C, I>>,
    // invariant: all values in id_map are valid keys in active_devices.
    id_map: HashMap<BindingId, C>,
    last_id: BindingId,
}

impl<C: IdMapCollectionKey, I> Default for Devices<C, I> {
    fn default() -> Self {
        Self { active_devices: IdMapCollection::new(), id_map: HashMap::new(), last_id: 0 }
    }
}

impl<C, I> Devices<C, I>
where
    C: IdMapCollectionKey + Clone + std::fmt::Debug,
{
    /// Allocates a new [`BindingId`].
    fn alloc_id(last_id: &mut BindingId) -> BindingId {
        *last_id += 1;
        *last_id
    }

    /// Adds a new active device.
    ///
    /// Adds a new active device if the informed `core_id` is valid (i.e., not
    /// currently tracked by [`Devices`]). A new [`BindingId`] will be allocated
    /// and a [`DeviceInfo`] struct will be created with the provided `info` and
    /// IDs.
    pub fn add_active_device<F: FnOnce(BindingId) -> I>(
        &mut self,
        core_id: C,
        info: F,
    ) -> Option<BindingId> {
        let Self { active_devices, id_map, last_id } = self;
        match active_devices.entry(core_id) {
            Entry::Occupied(_) => None,
            Entry::Vacant(entry) => {
                let id = Self::alloc_id(last_id);
                let core_id = entry.key().clone();
                assert_matches::assert_matches!(id_map.insert(id, core_id.clone()), None);
                let _: &mut DeviceInfo<_, _> =
                    entry.insert(DeviceInfo { id, core_id: core_id, info: info(id) });
                Some(id)
            }
        }
    }

    /// Removes a device from the internal list.
    ///
    /// Removes a device from the internal [`Devices`] list and returns the
    /// associated [`DeviceInfo`] if `id` is found or `None` otherwise.
    pub fn remove_device(&mut self, id: BindingId) -> Option<DeviceInfo<C, I>> {
        let Self { active_devices, id_map, last_id: _ } = self;
        id_map.remove(&id).and_then(|core_id| active_devices.remove(&core_id))
    }

    /// Gets an iterator over all tracked devices.
    #[cfg(test)]
    pub fn iter_devices(&self) -> impl Iterator<Item = &DeviceInfo<C, I>> {
        self.active_devices.iter()
    }

    /// Retrieve device with [`BindingId`].
    pub fn get_device(&self, id: BindingId) -> Option<&DeviceInfo<C, I>> {
        let Self { active_devices, id_map, last_id: _ } = self;
        id_map.get(&id).and_then(|device_id| active_devices.get(&device_id))
    }

    /// Retrieve mutable reference to device with [`BindingId`].
    pub fn get_device_mut(&mut self, id: BindingId) -> Option<&mut DeviceInfo<C, I>> {
        let Self { active_devices, id_map, last_id: _ } = self;
        id_map.get(&id).and_then(move |core_id| active_devices.get_mut(&core_id))
    }

    /// Retrieve associated `core_id` for [`BindingId`].
    pub fn get_core_id(&self, id: BindingId) -> Option<C> {
        self.id_map.get(&id).cloned()
    }

    /// Retrieve non-mutable reference to device by associated [`CoreId`] `id`.
    pub fn get_core_device(&self, id: C) -> Option<&DeviceInfo<C, I>> {
        self.active_devices.get(&id)
    }

    /// Retrieve mutable reference to device by associated [`CoreId`] `id`.
    pub fn get_core_device_mut(&mut self, id: C) -> Option<&mut DeviceInfo<C, I>> {
        self.active_devices.get_mut(&id)
    }

    /// Retrieve associated `binding_id` for `core_id`.
    pub fn get_binding_id(&self, core_id: C) -> Option<BindingId> {
        self.active_devices.get(&core_id).map(|d| d.id)
    }
}

/// Device specific iformation.
#[derive(Debug)]
pub enum DeviceSpecificInfo {
    Ethernet(EthernetInfo),
    Loopback(LoopbackInfo),
}

impl DeviceSpecificInfo {
    pub fn common_info(&self) -> &CommonInfo {
        match self {
            Self::Ethernet(i) => &i.common_info,
            Self::Loopback(i) => &i.common_info,
        }
    }
}

/// Information common to all devices.
#[derive(Debug)]
pub struct CommonInfo {
    pub mtu: u32,
    pub admin_enabled: bool,
    pub events: super::InterfaceEventProducer,
    pub name: String,

    pub ipv4_config: Ipv4DeviceConfiguration,
    pub ipv6_config: Ipv6DeviceConfiguration,
}

/// Loopback device information.
#[derive(Debug)]
pub struct LoopbackInfo {
    pub common_info: CommonInfo,
}

/// Ethernet device information.
#[derive(Debug)]
pub struct EthernetInfo {
    pub common_info: CommonInfo,
    pub client: eth::Client,
    pub mac: UnicastAddr<Mac>,
    pub features: Features,
    pub phy_up: bool,
}

impl From<EthernetInfo> for DeviceSpecificInfo {
    fn from(i: EthernetInfo) -> DeviceSpecificInfo {
        DeviceSpecificInfo::Ethernet(i)
    }
}

/// Device information kept by [`Devices`].
#[derive(Debug, PartialEq)]
pub struct DeviceInfo<C = DeviceId, I = DeviceSpecificInfo> {
    id: BindingId,
    core_id: C,
    info: I,
}

impl<C, I> DeviceInfo<C, I>
where
    C: Clone,
{
    pub fn core_id(&self) -> C {
        self.core_id.clone()
    }

    pub fn info(&self) -> &I {
        &self.info
    }

    pub fn info_mut(&mut self) -> &mut I {
        &mut self.info
    }
}

#[cfg(test)]
mod tests {
    use assert_matches::assert_matches;
    use std::collections::HashSet;

    use super::*;

    type TestDevices = Devices<MockDeviceId, u64>;

    #[derive(Copy, Clone, Eq, PartialEq, Debug)]
    struct MockDeviceId(usize);

    impl IdMapCollectionKey for MockDeviceId {
        const VARIANT_COUNT: usize = 1;

        fn get_variant(&self) -> usize {
            0
        }

        fn get_id(&self) -> usize {
            self.0 as usize
        }
    }

    #[test]
    fn test_add_remove_active_device() {
        let mut d = TestDevices::default();
        let core_a = MockDeviceId(1);
        let core_b = MockDeviceId(2);
        let a = d.add_active_device(core_a, |id| id + 10).expect("can add device");
        let b = d.add_active_device(core_b, |id| id + 20).expect("can add device");
        assert_ne!(a, b, "allocated same id");
        assert_eq!(d.add_active_device(core_a, |id| id + 10), None, "can't add same id again");
        // check that ids are incrementing
        assert_eq!(d.last_id, 2);

        // check that devices are correctly inserted and carry the core id.
        assert_eq!(d.get_device(a).unwrap().core_id, core_a);
        assert_eq!(d.get_device(b).unwrap().core_id, core_b);
        assert_eq!(d.get_core_id(a).unwrap(), core_a);
        assert_eq!(d.get_core_id(b).unwrap(), core_b);
        assert_eq!(d.get_binding_id(core_a).unwrap(), a);
        assert_eq!(d.get_binding_id(core_b).unwrap(), b);

        // check that we can retrieve both devices by the core id:
        assert_matches!(d.get_core_device_mut(core_a), Some(_));
        assert_matches!(d.get_core_device_mut(core_b), Some(_));

        // remove both devices
        let info_a = d.remove_device(a).expect("can remove device");
        let info_b = d.remove_device(b).expect("can remove device");
        assert_eq!(info_a.info, a + 10);
        assert_eq!(info_b.info, b + 20);
        assert_eq!(info_a.core_id, core_a);
        assert_eq!(info_b.core_id, core_b);
        // removing device again will fail
        assert_eq!(d.remove_device(a), None);

        // retrieving the devices now should fail:
        assert_eq!(d.get_device(a), None);
        assert_eq!(d.get_core_id(a), None);
        assert_eq!(d.get_core_device_mut(core_a), None);

        assert!(d.active_devices.is_empty());
        assert!(d.id_map.is_empty());
    }

    #[test]
    fn test_iter() {
        let mut d = TestDevices::default();
        let core_a = MockDeviceId(1);
        let a = d.add_active_device(core_a, |id| id + 10).unwrap();
        assert_eq!(d.iter_devices().map(|d| d.id).collect::<HashSet<_>>(), HashSet::from([a]));

        let core_b = MockDeviceId(2);
        let b = d.add_active_device(core_b, |id| id + 20).unwrap();
        assert_eq!(d.iter_devices().map(|d| d.id).collect::<HashSet<_>>(), HashSet::from([a, b]));
    }
}
