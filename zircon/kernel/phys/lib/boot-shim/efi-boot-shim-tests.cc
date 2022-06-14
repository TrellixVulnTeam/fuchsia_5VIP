// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-shim/efi-boot-shim.h>

#include <efi/types.h>
#include <zxtest/zxtest.h>

namespace {

constexpr efi_memory_descriptor kTestEfiMemoryMap[] = {
    {
        .Type = 0x3,
        .PhysicalStart = 0,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x7,
        .PhysicalStart = 0x1000,
        .VirtualStart = 0,
        .NumberOfPages = 134,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x87000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x7,
        .PhysicalStart = 0x88000,
        .VirtualStart = 0,
        .NumberOfPages = 24,
        .Attribute = 0xf,
    },
    {
        .Type = 0x7,
        .PhysicalStart = 0x100000,
        .VirtualStart = 0,
        .NumberOfPages = 1792,
        .Attribute = 0xf,
    },
    {
        .Type = 0xa,
        .PhysicalStart = 0x800000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x7,
        .PhysicalStart = 0x808000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0xa,
        .PhysicalStart = 0x80b000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x7,
        .PhysicalStart = 0x80c000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0xa,
        .PhysicalStart = 0x810000,
        .VirtualStart = 0,
        .NumberOfPages = 240,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x900000,
        .VirtualStart = 0,
        .NumberOfPages = 3072,
        .Attribute = 0xf,
    },
    {
        .Type = 0x7,
        .PhysicalStart = 0x1500000,
        .VirtualStart = 0,
        .NumberOfPages = 470860,
        .Attribute = 0xf,
    },
    {
        .Type = 0x2,
        .PhysicalStart = 0x7444c000,
        .VirtualStart = 0,
        .NumberOfPages = 19982,
        .Attribute = 0xf,
    },
    {
        .Type = 0x7,
        .PhysicalStart = 0x7925a000,
        .VirtualStart = 0,
        .NumberOfPages = 11460,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7bf1e000,
        .VirtualStart = 0,
        .NumberOfPages = 32,
        .Attribute = 0xf,
    },
    {
        .Type = 0x7,
        .PhysicalStart = 0x7bf3e000,
        .VirtualStart = 0,
        .NumberOfPages = 9900,
        .Attribute = 0xf,
    },
    {
        .Type = 0x1,
        .PhysicalStart = 0x7e5ea000,
        .VirtualStart = 0,
        .NumberOfPages = 209,
        .Attribute = 0xf,
    },
    {
        .Type = 0x7,
        .PhysicalStart = 0x7e6bb000,
        .VirtualStart = 0,
        .NumberOfPages = 74,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7e705000,
        .VirtualStart = 0,
        .NumberOfPages = 30,
        .Attribute = 0xf,
    },
    {
        .Type = 0x7,
        .PhysicalStart = 0x7e723000,
        .VirtualStart = 0,
        .NumberOfPages = 14,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7e731000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x7,
        .PhysicalStart = 0x7e732000,
        .VirtualStart = 0,
        .NumberOfPages = 12,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7e73e000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x2,
        .PhysicalStart = 0x7e73f000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7e740000,
        .VirtualStart = 0,
        .NumberOfPages = 1868,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7ee8c000,
        .VirtualStart = 0,
        .NumberOfPages = 34,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7eeae000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7eeb3000,
        .VirtualStart = 0,
        .NumberOfPages = 50,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7eee5000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7eee9000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7eeec000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7eef0000,
        .VirtualStart = 0,
        .NumberOfPages = 15,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7eeff000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7ef01000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7ef04000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7ef07000,
        .VirtualStart = 0,
        .NumberOfPages = 14,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7ef15000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7ef1c000,
        .VirtualStart = 0,
        .NumberOfPages = 13,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7ef29000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7ef2a000,
        .VirtualStart = 0,
        .NumberOfPages = 29,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7ef47000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7ef4a000,
        .VirtualStart = 0,
        .NumberOfPages = 23,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7ef61000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7ef63000,
        .VirtualStart = 0,
        .NumberOfPages = 13,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7ef70000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7ef72000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7ef74000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7ef76000,
        .VirtualStart = 0,
        .NumberOfPages = 11,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7ef81000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7ef83000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7ef88000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7ef8a000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7ef91000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7ef96000,
        .VirtualStart = 0,
        .NumberOfPages = 12,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7efa2000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7efa3000,
        .VirtualStart = 0,
        .NumberOfPages = 27,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7efbe000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7efc1000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7efc5000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7efc6000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7efc7000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7efc9000,
        .VirtualStart = 0,
        .NumberOfPages = 18,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7efdb000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7efdc000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7efe5000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7efed000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7eff1000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7eff6000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7eff9000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7effa000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7effb000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7effc000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7f000000,
        .VirtualStart = 0,
        .NumberOfPages = 513,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7f201000,
        .VirtualStart = 0,
        .NumberOfPages = 50,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7f233000,
        .VirtualStart = 0,
        .NumberOfPages = 11,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7f23e000,
        .VirtualStart = 0,
        .NumberOfPages = 10,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7f248000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7f24c000,
        .VirtualStart = 0,
        .NumberOfPages = 22,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7f262000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7f267000,
        .VirtualStart = 0,
        .NumberOfPages = 11,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7f272000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7f274000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7f275000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7f277000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7f27a000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7f27c000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7f27f000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7f281000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7f282000,
        .VirtualStart = 0,
        .NumberOfPages = 34,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7f2a4000,
        .VirtualStart = 0,
        .NumberOfPages = 12,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7f2b0000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7f2b1000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7f2b2000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7f2b3000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7f2b4000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7f2b6000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7f2bc000,
        .VirtualStart = 0,
        .NumberOfPages = 1024,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7f6bc000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7f6c0000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7f6c1000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7f6c3000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7f6c5000,
        .VirtualStart = 0,
        .NumberOfPages = 11,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7f6d0000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7f6d2000,
        .VirtualStart = 0,
        .NumberOfPages = 0,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7f6d5000,
        .VirtualStart = 0,
        .NumberOfPages = 538,
        .Attribute = 0xf,
    },
    {
        .Type = 0x6,
        .PhysicalStart = 0x7f8ef000,
        .VirtualStart = 0,
        .NumberOfPages = 256,
        .Attribute = 0x800000000000000f,
    },
    {
        .Type = 0x5,
        .PhysicalStart = 0x7f9ef000,
        .VirtualStart = 0,
        .NumberOfPages = 256,
        .Attribute = 0x800000000000000f,
    },
    {
        .Type = 0,
        .PhysicalStart = 0x7faef000,
        .VirtualStart = 0,
        .NumberOfPages = 128,
        .Attribute = 0xf,
    },
    {
        .Type = 0x9,
        .PhysicalStart = 0x7fb6f000,
        .VirtualStart = 0,
        .NumberOfPages = 16,
        .Attribute = 0xf,
    },
    {
        .Type = 0xa,
        .PhysicalStart = 0x7fb7f000,
        .VirtualStart = 0,
        .NumberOfPages = 128,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7fbff000,
        .VirtualStart = 0,
        .NumberOfPages = 513,
        .Attribute = 0xf,
    },
    {
        .Type = 0x7,
        .PhysicalStart = 0x7fe00000,
        .VirtualStart = 0,
        .NumberOfPages = 107,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7fe6b000,
        .VirtualStart = 0,
        .NumberOfPages = 32,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7fe8b000,
        .VirtualStart = 0,
        .NumberOfPages = 26,
        .Attribute = 0xf,
    },
    {
        .Type = 0x4,
        .PhysicalStart = 0x7fea5000,
        .VirtualStart = 0,
        .NumberOfPages = 33,
        .Attribute = 0xf,
    },
    {
        .Type = 0x3,
        .PhysicalStart = 0x7fec6000,
        .VirtualStart = 0,
        .NumberOfPages = 22,
        .Attribute = 0xf,
    },
    {
        .Type = 0x6,
        .PhysicalStart = 0x7fedc000,
        .VirtualStart = 0,
        .NumberOfPages = 132,
        .Attribute = 0x800000000000000f,
    },
    {
        .Type = 0xa,
        .PhysicalStart = 0x7ff60000,
        .VirtualStart = 0,
        .NumberOfPages = 160,
        .Attribute = 0xf,
    },
    {
        .Type = 0x7,
        .PhysicalStart = 0x100000000,
        .VirtualStart = 0,
        .NumberOfPages = 262144,
        .Attribute = 0xf,
    },
    {
        .Type = 0x1,
        .PhysicalStart = 0x140000000,
        .VirtualStart = 0,
        .NumberOfPages = 26,
        .Attribute = 0xf,
    },
    {
        .Type = 0x7,
        .PhysicalStart = 0x14001a000,
        .VirtualStart = 0,
        .NumberOfPages = 1310694,
        .Attribute = 0xf,
    },
    {
        .Type = 0,
        .PhysicalStart = 0xb0000000,
        .VirtualStart = 0,
        .NumberOfPages = 65536,
        .Attribute = 0x1,
    },
};

constexpr zbi_mem_range_t kTestZbiMemRanges[] = {
    {
        .paddr = 0x1000,
        .length = 0x86000,
        .type = ZBI_MEM_RANGE_RAM,
    },
    {
        .paddr = 0x88000,
        .length = 0x18000,
        .type = ZBI_MEM_RANGE_RAM,
    },
    {
        .paddr = 0x100000,
        .length = 0x700000,
        .type = ZBI_MEM_RANGE_RAM,
    },
    {
        .paddr = 0x810000,
        .length = 0xf0000,
        .type = ZBI_MEM_RANGE_RESERVED,
    },
    {
        .paddr = 0x900000,
        .length = 0x7de31000,
        .type = ZBI_MEM_RANGE_RAM,
    },
    {
        .paddr = 0x7e732000,
        .length = 0xc000,
        .type = ZBI_MEM_RANGE_RAM,
    },
    {
        .paddr = 0x7e740000,
        .length = 0x76e000,
        .type = ZBI_MEM_RANGE_RAM,
    },
    {
        .paddr = 0x7eeb3000,
        .length = 0x32000,
        .type = ZBI_MEM_RANGE_RAM,
    },
    {
        .paddr = 0x7eef0000,
        .length = 0xf000,
        .type = ZBI_MEM_RANGE_RAM,
    },
    {
        .paddr = 0x7ef07000,
        .length = 0xe000,
        .type = ZBI_MEM_RANGE_RAM,
    },
    {
        .paddr = 0x7ef1c000,
        .length = 0xd000,
        .type = ZBI_MEM_RANGE_RAM,
    },
    {
        .paddr = 0x7ef2a000,
        .length = 0x1d000,
        .type = ZBI_MEM_RANGE_RAM,
    },
    {
        .paddr = 0x7ef4a000,
        .length = 0x17000,
        .type = ZBI_MEM_RANGE_RAM,
    },
    {
        .paddr = 0x7ef63000,
        .length = 0xd000,
        .type = ZBI_MEM_RANGE_RAM,
    },
    {
        .paddr = 0x7ef76000,
        .length = 0xb000,
        .type = ZBI_MEM_RANGE_RAM,
    },
    {
        .paddr = 0x7ef96000,
        .length = 0xc000,
        .type = ZBI_MEM_RANGE_RAM,
    },
    {
        .paddr = 0x7efa3000,
        .length = 0x1b000,
        .type = ZBI_MEM_RANGE_RAM,
    },
    {
        .paddr = 0x7efc9000,
        .length = 0x12000,
        .type = ZBI_MEM_RANGE_RAM,
    },
    {
        .paddr = 0x7f000000,
        .length = 0x248000,
        .type = ZBI_MEM_RANGE_RAM,
    },
    {
        .paddr = 0x7f24c000,
        .length = 0x16000,
        .type = ZBI_MEM_RANGE_RAM,
    },
    {
        .paddr = 0x7f267000,
        .length = 0xb000,
        .type = ZBI_MEM_RANGE_RAM,
    },
    {
        .paddr = 0x7f282000,
        .length = 0x2e000,
        .type = ZBI_MEM_RANGE_RAM,
    },
    {
        .paddr = 0x7f2bc000,
        .length = 0x400000,
        .type = ZBI_MEM_RANGE_RAM,
    },
    {
        .paddr = 0x7f6c5000,
        .length = 0xb000,
        .type = ZBI_MEM_RANGE_RAM,
    },
    {
        .paddr = 0x7f6d5000,
        .length = 0x21a000,
        .type = ZBI_MEM_RANGE_RAM,
    },
    {
        .paddr = 0x7f8ef000,
        .length = 0x310000,
        .type = ZBI_MEM_RANGE_RESERVED,
    },
    {
        .paddr = 0x7fbff000,
        .length = 0x2dd000,
        .type = ZBI_MEM_RANGE_RAM,
    },
    {
        .paddr = 0x7fedc000,
        .length = 0x124000,
        .type = ZBI_MEM_RANGE_RESERVED,
    },
    {
        .paddr = 0xb0000000,
        .length = 0x10000000,
        .type = ZBI_MEM_RANGE_RESERVED,
    },
    {
        .paddr = 0x100000000,
        .length = 0x180000000,
        .type = ZBI_MEM_RANGE_RAM,
    },
};

TEST(BootShimTests, EfiBootShimMemConfig) {
  alignas(ZBI_ALIGNMENT) std::byte buffer[sizeof(kTestEfiMemoryMap)];
  memcpy(buffer, kTestEfiMemoryMap, sizeof(kTestEfiMemoryMap));
  cpp20::span<zbi_mem_range_t> mem_config = boot_shim::EfiBootShimLoader::ConvertMemoryMap(
      cpp20::span(buffer), sizeof(efi_memory_descriptor));
  ASSERT_EQ(mem_config.size(), std::size(kTestZbiMemRanges));
  for (size_t i = 0; i < std::size(kTestZbiMemRanges); ++i) {
    EXPECT_EQ(mem_config[i].paddr, kTestZbiMemRanges[i].paddr, "%zu", i);
    EXPECT_EQ(mem_config[i].length, kTestZbiMemRanges[i].length, "%zu", i);
    EXPECT_EQ(mem_config[i].type, kTestZbiMemRanges[i].type, "%zu", i);
  }
}

}  // namespace