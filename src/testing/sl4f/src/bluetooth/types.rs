// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use fidl_fuchsia_bluetooth_avdtp::PeerControllerProxy;
use fidl_fuchsia_bluetooth_avrcp::{
    AvcPanelCommand, BatteryStatus, CustomAttributeValue, CustomPlayerApplicationSetting,
    Equalizer, PlayStatus, PlaybackStatus, PlayerApplicationSettingAttributeId,
    PlayerApplicationSettings, RepeatStatusMode, ScanMode, ShuffleMode,
};
use fidl_fuchsia_bluetooth_gatt::{
    AttributePermissions, Characteristic, Descriptor, ReadByTypeResult, SecurityRequirements,
    ServiceInfo,
};
use fidl_fuchsia_bluetooth_sys::Peer;
//use num_traits::FromPrimitive;
use num_derive::FromPrimitive;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

/// Handling different sessions.
/// Key is a generic id that is generated by the tool that is associated with a remote peer.
/// Value is the controller associated with the remote peer.
pub type PeerFactoryMap = HashMap<String, PeerControllerProxy>;

/// BleScan result type
/// TODO(fxbug.dev/875): Add support for RemoteDevices when clone() is implemented
#[derive(Serialize, Clone, Debug)]
pub struct BleScanResponse {
    pub id: String,
    pub name: String,
    pub connectable: bool,
}

impl BleScanResponse {
    pub fn new(id: String, name: String, connectable: bool) -> BleScanResponse {
        BleScanResponse { id, name, connectable }
    }
}

/// BleAdvertise result type (only uuid)
/// TODO(fxbug.dev/875): Add support for AdvertisingData when clone() is implemented
#[derive(Serialize, Clone, Debug)]
pub struct BleAdvertiseResponse {
    pub name: Option<String>,
}

impl BleAdvertiseResponse {
    pub fn new(name: Option<String>) -> BleAdvertiseResponse {
        BleAdvertiseResponse { name }
    }
}

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct SecurityRequirementsContainer {
    pub encryption_required: bool,
    pub authentication_required: bool,
    pub authorization_required: bool,
}

impl SecurityRequirementsContainer {
    pub fn new(info: Option<Box<SecurityRequirements>>) -> SecurityRequirementsContainer {
        match info {
            Some(s) => {
                let sec = *s;
                SecurityRequirementsContainer {
                    encryption_required: sec.encryption_required,
                    authentication_required: sec.authentication_required,
                    authorization_required: sec.authorization_required,
                }
            }
            None => SecurityRequirementsContainer {
                encryption_required: false,
                authentication_required: false,
                authorization_required: false,
            },
        }
    }
}

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct AttributePermissionsContainer {
    pub read: SecurityRequirementsContainer,
    pub write: SecurityRequirementsContainer,
    pub update: SecurityRequirementsContainer,
}

impl AttributePermissionsContainer {
    pub fn new(
        info: Option<Box<AttributePermissions>>,
    ) -> Result<AttributePermissionsContainer, Error> {
        match info {
            Some(p) => {
                let perm = *p;
                Ok(AttributePermissionsContainer {
                    read: SecurityRequirementsContainer::new(perm.read),
                    write: SecurityRequirementsContainer::new(perm.write),
                    update: SecurityRequirementsContainer::new(perm.update),
                })
            }
            None => return Err(format_err!("Unable to get information of AttributePermissions.")),
        }
    }
}

// Discover Characteristic response to hold characteristic info
// as Characteristics are not serializable.
#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct GattcDiscoverDescriptorResponse {
    pub id: u64,
    pub permissions: Option<AttributePermissionsContainer>,
    pub uuid_type: String,
}

impl GattcDiscoverDescriptorResponse {
    pub fn new(info: Vec<Descriptor>) -> Vec<GattcDiscoverDescriptorResponse> {
        let mut res = Vec::new();
        for v in info {
            let copy = GattcDiscoverDescriptorResponse {
                id: v.id,
                permissions: match AttributePermissionsContainer::new(v.permissions) {
                    Ok(n) => Some(n),
                    Err(_) => None,
                },
                uuid_type: v.type_,
            };
            res.push(copy)
        }
        res
    }
}

// Discover Characteristic response to hold characteristic info
// as Characteristics are not serializable.
#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct GattcDiscoverCharacteristicResponse {
    pub id: u64,
    pub properties: u32,
    pub permissions: Option<AttributePermissionsContainer>,
    pub uuid_type: String,
    pub descriptors: Vec<GattcDiscoverDescriptorResponse>,
}

impl GattcDiscoverCharacteristicResponse {
    pub fn new(info: Vec<Characteristic>) -> Vec<GattcDiscoverCharacteristicResponse> {
        let mut res = Vec::new();
        for v in info {
            let copy = GattcDiscoverCharacteristicResponse {
                id: v.id,
                properties: v.properties,
                permissions: match AttributePermissionsContainer::new(v.permissions) {
                    Ok(n) => Some(n),
                    Err(_) => None,
                },
                uuid_type: v.type_,
                descriptors: {
                    match v.descriptors {
                        Some(d) => GattcDiscoverDescriptorResponse::new(d),
                        None => Vec::new(),
                    }
                },
            };
            res.push(copy)
        }
        res
    }
}

/// BleConnectPeripheral response (aka ServiceInfo)
/// TODO(fxbug.dev/875): Add support for ServiceInfo when clone(), serialize(), derived
#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct BleConnectPeripheralResponse {
    pub id: u64,
    pub primary: bool,
    pub uuid_type: String,
}

impl BleConnectPeripheralResponse {
    pub fn new(info: Vec<ServiceInfo>) -> Vec<BleConnectPeripheralResponse> {
        let mut res = Vec::new();
        for v in info {
            let copy =
                BleConnectPeripheralResponse { id: v.id, primary: v.primary, uuid_type: v.type_ };
            res.push(copy)
        }
        res
    }
}

#[derive(Clone, Debug, Serialize)]
pub struct SerializablePeer {
    pub address: Option<[u8; 6]>,
    pub appearance: Option<u32>,
    pub device_class: Option<u32>,
    pub id: Option<u64>,
    pub name: Option<String>,
    pub connected: Option<bool>,
    pub bonded: Option<bool>,
    pub rssi: Option<i8>,
    pub services: Option<Vec<[u8; 16]>>,
    pub technology: Option<u32>,
    pub tx_power: Option<i8>,
}

impl From<&Peer> for SerializablePeer {
    fn from(peer: &Peer) -> Self {
        let services = match &peer.services {
            Some(s) => {
                let mut service_list = Vec::new();
                for item in s {
                    service_list.push(item.value);
                }
                Some(service_list)
            }
            None => None,
        };
        SerializablePeer {
            address: peer.address.map(|a| a.bytes),
            appearance: peer.appearance.map(|a| a.into_primitive() as u32),
            device_class: peer.device_class.map(|d| d.value),
            id: peer.id.map(|i| i.value),
            name: peer.name.clone(),
            connected: peer.connected,
            bonded: peer.bonded,
            rssi: peer.rssi,
            services: services,
            technology: peer.technology.map(|t| t as u32),
            tx_power: peer.tx_power,
        }
    }
}

#[derive(Clone, Debug, Serialize)]
pub struct SerializableReadByTypeResult {
    pub id: Option<u64>,
    pub value: Option<Vec<u8>>,
}

impl SerializableReadByTypeResult {
    pub fn new(result: &ReadByTypeResult) -> Self {
        SerializableReadByTypeResult { id: result.id.clone(), value: result.value.clone() }
    }
}

#[derive(Clone, Debug, Serialize, Eq, Copy)]
pub struct CustomPlayStatus {
    pub song_length: Option<u32>,
    pub song_position: Option<u32>,
    pub playback_status: Option<u8>,
}

impl CustomPlayStatus {
    pub fn new(status: &PlayStatus) -> Self {
        let playback_status = match status.playback_status {
            Some(p) => Some(p as u8),
            None => None,
        };
        CustomPlayStatus {
            song_length: status.song_length,
            song_position: status.song_position,
            playback_status: playback_status,
        }
    }
}

impl From<CustomPlayStatus> for PlayStatus {
    fn from(status: CustomPlayStatus) -> Self {
        let playback_status = match status.playback_status {
            Some(0) => Some(PlaybackStatus::Stopped),
            Some(1) => Some(PlaybackStatus::Playing),
            Some(2) => Some(PlaybackStatus::Paused),
            Some(3) => Some(PlaybackStatus::FwdSeek),
            Some(4) => Some(PlaybackStatus::RevSeek),
            Some(255) => Some(PlaybackStatus::Error),
            None => None,
            _ => panic!("Unknown playback status!"),
        };
        PlayStatus {
            song_length: status.song_length,
            song_position: status.song_position,
            playback_status: playback_status,
            ..PlayStatus::EMPTY
        }
    }
}

impl From<PlayStatus> for CustomPlayStatus {
    fn from(status: PlayStatus) -> Self {
        CustomPlayStatus {
            song_length: status.song_length,
            song_position: status.song_position,
            playback_status: match status.playback_status {
                Some(p) => Some(p as u8),
                None => None,
            },
        }
    }
}

impl PartialEq for CustomPlayStatus {
    fn eq(&self, other: &CustomPlayStatus) -> bool {
        self.song_length == other.song_length
            && self.song_position == other.song_position
            && self.playback_status == other.playback_status
    }
}
#[derive(Copy, Clone, Debug, FromPrimitive, Serialize, Deserialize)]
#[repr(u8)]
pub enum CustomAvcPanelCommand {
    Select = 0,
    Up = 1,
    Down = 2,
    Left = 3,
    Right = 4,
    RootMenu = 9,
    ContentsMenu = 11,
    FavoriteMenu = 12,
    Exit = 13,
    OnDemandMenu = 14,
    AppsMenu = 15,
    Key0 = 32,
    Key1 = 33,
    Key2 = 34,
    Key3 = 35,
    Key4 = 36,
    Key5 = 37,
    Key6 = 38,
    Key7 = 39,
    Key8 = 40,
    Key9 = 41,
    Dot = 42,
    Enter = 43,
    ChannelUp = 48,
    ChannelDown = 49,
    ChannelPrevious = 50,
    InputSelect = 52,
    Info = 53,
    Help = 54,
    PageUp = 55,
    PageDown = 56,
    Lock = 58,
    Power = 64,
    VolumeUp = 65,
    VolumeDown = 66,
    Mute = 67,
    Play = 68,
    Stop = 69,
    Pause = 70,
    Record = 71,
    Rewind = 72,
    FastForward = 73,
    Eject = 74,
    Forward = 75,
    Backward = 76,
    List = 77,
    F1 = 113,
    F2 = 114,
    F3 = 115,
    F4 = 116,
    F5 = 117,
    F6 = 118,
    F7 = 119,
    F8 = 120,
    F9 = 121,
    Red = 122,
    Green = 123,
    Blue = 124,
    Yellow = 125,
}

impl From<CustomAvcPanelCommand> for AvcPanelCommand {
    fn from(command: CustomAvcPanelCommand) -> Self {
        match command {
            CustomAvcPanelCommand::Select => AvcPanelCommand::Select,
            CustomAvcPanelCommand::Up => AvcPanelCommand::Up,
            CustomAvcPanelCommand::Down => AvcPanelCommand::Down,
            CustomAvcPanelCommand::Left => AvcPanelCommand::Left,
            CustomAvcPanelCommand::Right => AvcPanelCommand::Right,
            CustomAvcPanelCommand::RootMenu => AvcPanelCommand::RootMenu,
            CustomAvcPanelCommand::ContentsMenu => AvcPanelCommand::ContentsMenu,
            CustomAvcPanelCommand::FavoriteMenu => AvcPanelCommand::FavoriteMenu,
            CustomAvcPanelCommand::Exit => AvcPanelCommand::Exit,
            CustomAvcPanelCommand::OnDemandMenu => AvcPanelCommand::OnDemandMenu,
            CustomAvcPanelCommand::AppsMenu => AvcPanelCommand::AppsMenu,
            CustomAvcPanelCommand::Key0 => AvcPanelCommand::Key0,
            CustomAvcPanelCommand::Key1 => AvcPanelCommand::Key1,
            CustomAvcPanelCommand::Key2 => AvcPanelCommand::Key2,
            CustomAvcPanelCommand::Key3 => AvcPanelCommand::Key3,
            CustomAvcPanelCommand::Key4 => AvcPanelCommand::Key4,
            CustomAvcPanelCommand::Key5 => AvcPanelCommand::Key5,
            CustomAvcPanelCommand::Key6 => AvcPanelCommand::Key6,
            CustomAvcPanelCommand::Key7 => AvcPanelCommand::Key7,
            CustomAvcPanelCommand::Key8 => AvcPanelCommand::Key8,
            CustomAvcPanelCommand::Key9 => AvcPanelCommand::Key9,
            CustomAvcPanelCommand::Dot => AvcPanelCommand::Dot,
            CustomAvcPanelCommand::Enter => AvcPanelCommand::Enter,
            CustomAvcPanelCommand::ChannelUp => AvcPanelCommand::ChannelUp,
            CustomAvcPanelCommand::ChannelDown => AvcPanelCommand::ChannelDown,
            CustomAvcPanelCommand::ChannelPrevious => AvcPanelCommand::ChannelPrevious,
            CustomAvcPanelCommand::InputSelect => AvcPanelCommand::InputSelect,
            CustomAvcPanelCommand::Info => AvcPanelCommand::Info,
            CustomAvcPanelCommand::Help => AvcPanelCommand::Help,
            CustomAvcPanelCommand::PageUp => AvcPanelCommand::PageUp,
            CustomAvcPanelCommand::PageDown => AvcPanelCommand::PageDown,
            CustomAvcPanelCommand::Lock => AvcPanelCommand::Lock,
            CustomAvcPanelCommand::Power => AvcPanelCommand::Power,
            CustomAvcPanelCommand::VolumeUp => AvcPanelCommand::VolumeUp,
            CustomAvcPanelCommand::VolumeDown => AvcPanelCommand::VolumeDown,
            CustomAvcPanelCommand::Mute => AvcPanelCommand::Mute,
            CustomAvcPanelCommand::Play => AvcPanelCommand::Play,
            CustomAvcPanelCommand::Stop => AvcPanelCommand::Stop,
            CustomAvcPanelCommand::Pause => AvcPanelCommand::Pause,
            CustomAvcPanelCommand::Record => AvcPanelCommand::Record,
            CustomAvcPanelCommand::Rewind => AvcPanelCommand::Rewind,
            CustomAvcPanelCommand::FastForward => AvcPanelCommand::FastForward,
            CustomAvcPanelCommand::Eject => AvcPanelCommand::Eject,
            CustomAvcPanelCommand::Forward => AvcPanelCommand::Forward,
            CustomAvcPanelCommand::Backward => AvcPanelCommand::Backward,
            CustomAvcPanelCommand::List => AvcPanelCommand::List,
            CustomAvcPanelCommand::F1 => AvcPanelCommand::F1,
            CustomAvcPanelCommand::F2 => AvcPanelCommand::F2,
            CustomAvcPanelCommand::F3 => AvcPanelCommand::F3,
            CustomAvcPanelCommand::F4 => AvcPanelCommand::F4,
            CustomAvcPanelCommand::F5 => AvcPanelCommand::F5,
            CustomAvcPanelCommand::F6 => AvcPanelCommand::F6,
            CustomAvcPanelCommand::F7 => AvcPanelCommand::F7,
            CustomAvcPanelCommand::F8 => AvcPanelCommand::F8,
            CustomAvcPanelCommand::F9 => AvcPanelCommand::F9,
            CustomAvcPanelCommand::Red => AvcPanelCommand::Red,
            CustomAvcPanelCommand::Green => AvcPanelCommand::Green,
            CustomAvcPanelCommand::Blue => AvcPanelCommand::Blue,
            CustomAvcPanelCommand::Yellow => AvcPanelCommand::Yellow,
        }
    }
}

impl From<String> for CustomAvcPanelCommand {
    fn from(command: String) -> Self {
        match command.as_str() {
            "Select" => CustomAvcPanelCommand::Select,
            "Up" => CustomAvcPanelCommand::Up,
            "Down" => CustomAvcPanelCommand::Down,
            "Left" => CustomAvcPanelCommand::Left,
            "Right" => CustomAvcPanelCommand::Right,
            "RootMenu" => CustomAvcPanelCommand::RootMenu,
            "ContentsMenu" => CustomAvcPanelCommand::ContentsMenu,
            "FavoriteMenu" => CustomAvcPanelCommand::FavoriteMenu,
            "Exit" => CustomAvcPanelCommand::Exit,
            "OnDemandMenu" => CustomAvcPanelCommand::OnDemandMenu,
            "AppsMenu" => CustomAvcPanelCommand::AppsMenu,
            "Key0" => CustomAvcPanelCommand::Key0,
            "Key1" => CustomAvcPanelCommand::Key1,
            "Key2" => CustomAvcPanelCommand::Key2,
            "Key3" => CustomAvcPanelCommand::Key3,
            "Key4" => CustomAvcPanelCommand::Key4,
            "Key5" => CustomAvcPanelCommand::Key5,
            "Key6" => CustomAvcPanelCommand::Key6,
            "Key7" => CustomAvcPanelCommand::Key7,
            "Key8" => CustomAvcPanelCommand::Key8,
            "Key9" => CustomAvcPanelCommand::Key9,
            "Dot" => CustomAvcPanelCommand::Dot,
            "Enter" => CustomAvcPanelCommand::Enter,
            "ChannelUp" => CustomAvcPanelCommand::ChannelUp,
            "ChannelDown" => CustomAvcPanelCommand::ChannelDown,
            "ChannelPrevious" => CustomAvcPanelCommand::ChannelPrevious,
            "InputSelect" => CustomAvcPanelCommand::InputSelect,
            "Info" => CustomAvcPanelCommand::Info,
            "Help" => CustomAvcPanelCommand::Help,
            "PageUp" => CustomAvcPanelCommand::PageUp,
            "PageDown" => CustomAvcPanelCommand::PageDown,
            "Lock" => CustomAvcPanelCommand::Lock,
            "Power" => CustomAvcPanelCommand::Power,
            "VolumeUp" => CustomAvcPanelCommand::VolumeUp,
            "VolumeDown" => CustomAvcPanelCommand::VolumeDown,
            "Mute" => CustomAvcPanelCommand::Mute,
            "Play" => CustomAvcPanelCommand::Play,
            "Stop" => CustomAvcPanelCommand::Stop,
            "Pause" => CustomAvcPanelCommand::Pause,
            "Record" => CustomAvcPanelCommand::Record,
            "Rewind" => CustomAvcPanelCommand::Rewind,
            "FastForward" => CustomAvcPanelCommand::FastForward,
            "Eject" => CustomAvcPanelCommand::Eject,
            "Forward" => CustomAvcPanelCommand::Forward,
            "Backward" => CustomAvcPanelCommand::Backward,
            "List" => CustomAvcPanelCommand::List,
            "F1" => CustomAvcPanelCommand::F1,
            "F2" => CustomAvcPanelCommand::F2,
            "F3" => CustomAvcPanelCommand::F3,
            "F4" => CustomAvcPanelCommand::F4,
            "F5" => CustomAvcPanelCommand::F5,
            "F6" => CustomAvcPanelCommand::F6,
            "F7" => CustomAvcPanelCommand::F7,
            "F8" => CustomAvcPanelCommand::F8,
            "F9" => CustomAvcPanelCommand::F9,
            "Red" => CustomAvcPanelCommand::Red,
            "Green" => CustomAvcPanelCommand::Green,
            "Blue" => CustomAvcPanelCommand::Blue,
            "Yellow" => CustomAvcPanelCommand::Yellow,
            _invalid => panic!("Invalid CustomAvcPanelCommand command:{:?}", _invalid),
        }
    }
}
#[derive(Deserialize)]
pub struct AbsoluteVolumeCommand {
    pub absolute_volume: u8,
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub struct CustomPlayerApplicationSettingsAttributeIds {
    pub attribute_ids: Option<Vec<u8>>,
}

impl CustomPlayerApplicationSettingsAttributeIds {
    pub fn to_vec(&self) -> Vec<PlayerApplicationSettingAttributeId> {
        match &self.attribute_ids {
            Some(vec) => vec
                .into_iter()
                .map(|u8| match u8 {
                    1 => PlayerApplicationSettingAttributeId::Equalizer,
                    2 => PlayerApplicationSettingAttributeId::RepeatStatusMode,
                    3 => PlayerApplicationSettingAttributeId::ShuffleMode,
                    4 => PlayerApplicationSettingAttributeId::ScanMode,
                    invalid => panic!(
                        "Invalid value for PlayerApplicationSettingAttributeId {:?}",
                        invalid
                    ),
                })
                .collect(),
            None => Vec::new(),
        }
    }
}
#[derive(Clone, Debug, Serialize)]
pub enum CustomPlayerApplicationSettingsAttributeId {
    Equalizer = 1,
    RepeatStatusMode = 2,
    ShuffleMode = 3,
    ScanMode = 4,
}

impl From<u8> for CustomPlayerApplicationSettingsAttributeId {
    fn from(attribute_id: u8) -> CustomPlayerApplicationSettingsAttributeId {
        match attribute_id {
            1 => CustomPlayerApplicationSettingsAttributeId::Equalizer,
            2 => CustomPlayerApplicationSettingsAttributeId::RepeatStatusMode,
            3 => CustomPlayerApplicationSettingsAttributeId::ShuffleMode,
            4 => CustomPlayerApplicationSettingsAttributeId::ScanMode,
            _ => panic!("Invalid attribute id: {:?}", attribute_id),
        }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize, Eq, PartialEq)]
pub struct CustomPlayerApplicationSettings {
    pub equalizer: Option<CustomEqualizer>,
    pub repeat_status_mode: Option<CustomRepeatStatusMode>,
    pub shuffle_mode: Option<CustomShuffleMode>,
    pub scan_mode: Option<CustomScanMode>,
    pub custom_settings: Option<Vec<CustomCustomPlayerApplicationSetting>>,
}

#[derive(Clone, Debug, Serialize, Deserialize, Eq, PartialEq)]
pub struct CustomCustomPlayerApplicationSetting {
    pub attribute_id: Option<u8>,
    pub attribute_name: Option<String>,
    pub possible_values: Option<Vec<CustomCustomAttributeValue>>,
    pub current_value: Option<u8>,
}

#[derive(Clone, Debug, Serialize, Deserialize, Eq, PartialEq)]
pub struct CustomCustomAttributeValue {
    pub description: String,
    pub value: u8,
}
#[derive(Clone, Debug, Serialize, Deserialize, Eq, PartialEq, Copy)]
pub enum CustomEqualizer {
    Off = 1,
    On = 2,
}

#[derive(Clone, Debug, Serialize, Deserialize, Eq, PartialEq, Copy)]
pub enum CustomRepeatStatusMode {
    Off = 1,
    SingleTrackRepeat = 2,
    AllTrackRepeat = 3,
    GroupRepeat = 4,
}
#[derive(Clone, Debug, Serialize, Deserialize, Eq, PartialEq, Copy)]
pub enum CustomShuffleMode {
    Off = 1,
    AllTrackShuffle = 2,
    GroupShuffle = 3,
}
#[derive(Clone, Debug, Serialize, Deserialize, Eq, PartialEq, Copy)]
pub enum CustomScanMode {
    Off = 1,
    AllTrackScan = 2,
    GroupScan = 3,
}
#[derive(Clone, Debug, Serialize, Deserialize, Eq, PartialEq, Copy)]
pub enum CustomBatteryStatus {
    Normal = 0,
    Warning = 1,
    Critical = 2,
    External = 3,
    FullCharge = 4,
    Reserved = 5,
}

impl From<PlayerApplicationSettings> for CustomPlayerApplicationSettings {
    fn from(settings: PlayerApplicationSettings) -> CustomPlayerApplicationSettings {
        CustomPlayerApplicationSettings {
            equalizer: match settings.equalizer {
                Some(equalizer) => match equalizer {
                    Equalizer::Off => Some(CustomEqualizer::Off),
                    Equalizer::On => Some(CustomEqualizer::On),
                },
                None => None,
            },
            repeat_status_mode: match settings.repeat_status_mode {
                Some(repeat_status_mode) => match repeat_status_mode {
                    RepeatStatusMode::Off => Some(CustomRepeatStatusMode::Off),
                    RepeatStatusMode::SingleTrackRepeat => {
                        Some(CustomRepeatStatusMode::SingleTrackRepeat)
                    }
                    RepeatStatusMode::AllTrackRepeat => {
                        Some(CustomRepeatStatusMode::AllTrackRepeat)
                    }
                    RepeatStatusMode::GroupRepeat => Some(CustomRepeatStatusMode::GroupRepeat),
                },
                None => None,
            },
            shuffle_mode: match settings.shuffle_mode {
                Some(shuffle_mode) => match shuffle_mode {
                    ShuffleMode::Off => Some(CustomShuffleMode::Off),
                    ShuffleMode::AllTrackShuffle => Some(CustomShuffleMode::AllTrackShuffle),
                    ShuffleMode::GroupShuffle => Some(CustomShuffleMode::GroupShuffle),
                },
                None => None,
            },
            scan_mode: match settings.scan_mode {
                Some(scan_mode) => match scan_mode {
                    ScanMode::Off => Some(CustomScanMode::Off),
                    ScanMode::AllTrackScan => Some(CustomScanMode::AllTrackScan),
                    ScanMode::GroupScan => Some(CustomScanMode::GroupScan),
                },
                None => None,
            },
            custom_settings: match settings.custom_settings {
                Some(custom_settings_vec) => Some(
                    custom_settings_vec
                        .into_iter()
                        .map(|custom_settings| CustomCustomPlayerApplicationSetting {
                            attribute_id: custom_settings.attribute_id,
                            attribute_name: custom_settings.attribute_name,
                            possible_values: match custom_settings.possible_values {
                                Some(possible_values) => Some(
                                    possible_values
                                        .into_iter()
                                        .map(|possible_value| possible_value.into())
                                        .collect(),
                                ),
                                None => None,
                            },
                            current_value: custom_settings.current_value,
                        })
                        .collect(),
                ),
                None => None,
            },
        }
    }
}

impl From<CustomPlayerApplicationSettings> for PlayerApplicationSettings {
    fn from(settings: CustomPlayerApplicationSettings) -> PlayerApplicationSettings {
        PlayerApplicationSettings {
            equalizer: match settings.equalizer {
                Some(equalizer) => match equalizer {
                    CustomEqualizer::Off => Some(Equalizer::Off),
                    CustomEqualizer::On => Some(Equalizer::On),
                },
                None => None,
            },
            repeat_status_mode: match settings.repeat_status_mode {
                Some(repeat_status_mode) => match repeat_status_mode {
                    CustomRepeatStatusMode::Off => Some(RepeatStatusMode::Off),
                    CustomRepeatStatusMode::SingleTrackRepeat => {
                        Some(RepeatStatusMode::SingleTrackRepeat)
                    }
                    CustomRepeatStatusMode::AllTrackRepeat => {
                        Some(RepeatStatusMode::AllTrackRepeat)
                    }
                    CustomRepeatStatusMode::GroupRepeat => Some(RepeatStatusMode::GroupRepeat),
                },
                None => None,
            },
            shuffle_mode: match settings.shuffle_mode {
                Some(shuffle_mode) => match shuffle_mode {
                    CustomShuffleMode::Off => Some(ShuffleMode::Off),
                    CustomShuffleMode::AllTrackShuffle => Some(ShuffleMode::AllTrackShuffle),
                    CustomShuffleMode::GroupShuffle => Some(ShuffleMode::GroupShuffle),
                },
                None => None,
            },
            scan_mode: match settings.scan_mode {
                Some(scan_mode) => match scan_mode {
                    CustomScanMode::Off => Some(ScanMode::Off),
                    CustomScanMode::AllTrackScan => Some(ScanMode::AllTrackScan),
                    CustomScanMode::GroupScan => Some(ScanMode::GroupScan),
                },
                None => None,
            },
            custom_settings: match settings.custom_settings {
                Some(custom_settings_vec) => Some(
                    custom_settings_vec
                        .into_iter()
                        .map(|custom_settings| CustomPlayerApplicationSetting {
                            attribute_id: custom_settings.attribute_id,
                            attribute_name: custom_settings.attribute_name,
                            possible_values: match custom_settings.possible_values {
                                Some(possible_values) => Some(
                                    possible_values
                                        .into_iter()
                                        .map(|possible_value| possible_value.into())
                                        .collect(),
                                ),
                                None => None,
                            },
                            current_value: custom_settings.current_value,
                            ..CustomPlayerApplicationSetting::EMPTY
                        })
                        .collect(),
                ),
                None => None,
            },
            ..PlayerApplicationSettings::EMPTY
        }
    }
}

impl From<CustomCustomAttributeValue> for CustomAttributeValue {
    fn from(attribute_value: CustomCustomAttributeValue) -> CustomAttributeValue {
        CustomAttributeValue {
            description: attribute_value.description,
            value: attribute_value.value,
        }
    }
}

impl From<CustomAttributeValue> for CustomCustomAttributeValue {
    fn from(attribute_value: CustomAttributeValue) -> CustomCustomAttributeValue {
        CustomCustomAttributeValue {
            description: attribute_value.description,
            value: attribute_value.value,
        }
    }
}

impl From<BatteryStatus> for CustomBatteryStatus {
    fn from(status: BatteryStatus) -> Self {
        match status {
            BatteryStatus::Normal => CustomBatteryStatus::Normal,
            BatteryStatus::Warning => CustomBatteryStatus::Warning,
            BatteryStatus::Critical => CustomBatteryStatus::Critical,
            BatteryStatus::External => CustomBatteryStatus::External,
            BatteryStatus::FullCharge => CustomBatteryStatus::FullCharge,
            BatteryStatus::Reserved => CustomBatteryStatus::Reserved,
        }
    }
}

impl From<CustomBatteryStatus> for BatteryStatus {
    fn from(status: CustomBatteryStatus) -> Self {
        match status {
            CustomBatteryStatus::Normal => BatteryStatus::Normal,
            CustomBatteryStatus::Warning => BatteryStatus::Warning,
            CustomBatteryStatus::Critical => BatteryStatus::Critical,
            CustomBatteryStatus::External => BatteryStatus::External,
            CustomBatteryStatus::FullCharge => BatteryStatus::FullCharge,
            CustomBatteryStatus::Reserved => BatteryStatus::Reserved,
        }
    }
}
