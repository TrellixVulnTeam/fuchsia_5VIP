// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_driver.h"
#include "magma_util/dlog.h"
#include "msd_intel_device.h"

MsdIntelDriver::MsdIntelDriver() { magic_ = kMagic; }

MsdIntelDriver* MsdIntelDriver::Create()
{
    auto drv = new MsdIntelDriver();
    if (!drv) {
        DLOG("Failed to allocate MsdIntelDriver");
        return nullptr;
    }
    return drv;
}

void MsdIntelDriver::Destroy(MsdIntelDriver* drv) { delete drv; }

MsdIntelDevice* MsdIntelDriver::CreateDevice(void* device)
{
    auto dev = new MsdIntelDevice();
    if (!dev) {
        DLOG("Failed to allocate MsdIntelDevice");
        return nullptr;
    }
    return dev;
}

//////////////////////////////////////////////////////////////////////////////

msd_driver* msd_driver_create(void) { return MsdIntelDriver::Create(); }

void msd_driver_destroy(msd_driver* drv) { MsdIntelDriver::Destroy(MsdIntelDriver::cast(drv)); }

msd_device* msd_driver_create_device(msd_driver* drv, void* device)
{
    return MsdIntelDriver::cast(drv)->CreateDevice(device);
}

void msd_driver_destroy_device(msd_device* dev) { delete MsdIntelDevice::cast(dev); }
