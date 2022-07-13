// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A5_A5_GPIO_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A5_A5_GPIO_H_

#define A5_GPIOE_COUNT 2
#define A5_GPIOH_COUNT 5
#define A5_GPIOD_COUNT 16
#define A5_GPIOB_COUNT 14
#define A5_GPIOX_COUNT 20
#define A5_GPIOC_COUNT 11
#define A5_GPIOT_COUNT 14
#define A5_GPIOZ_COUNT 16

#define A5_GPIOB_START 0
#define A5_GPIOC_START A5_GPIOB_COUNT
#define A5_GPIOE_START (A5_GPIOC_START + A5_GPIOC_COUNT)
#define A5_GPIOH_START (A5_GPIOE_START + A5_GPIOE_COUNT)
#define A5_GPIOD_START (A5_GPIOH_START + A5_GPIOH_COUNT)
#define A5_GPIOT_START (A5_GPIOD_START + A5_GPIOD_COUNT)
#define A5_GPIOX_START (A5_GPIOT_START + A5_GPIOT_COUNT)
#define A5_GPIOZ_START (A5_GPIOX_START + A5_GPIOX_COUNT)

#define A5_GPIOB(n) (A5_GPIOB_START + n)
#define A5_GPIOC(n) (A5_GPIOC_START + n)
#define A5_GPIOE(n) (A5_GPIOE_START + n)
#define A5_GPIOH(n) (A5_GPIOH_START + n)
#define A5_GPIOD(n) (A5_GPIOD_START + n)
#define A5_GPIOT(n) (A5_GPIOT_START + n)
#define A5_GPIOX(n) (A5_GPIOX_START + n)
#define A5_GPIOZ(n) (A5_GPIOZ_START + n)

#define A5_PREG_PAD_GPIOZ_I 0x0030
#define A5_PREG_PAD_GPIOZ_O 0x0031
#define A5_PREG_PAD_GPIOZ_OEN 0x0032
#define A5_PREG_PAD_GPIOZ_PULL_EN 0x0033
#define A5_PREG_PAD_GPIOZ_PULL_UP 0x0034
#define A5_PREG_PAD_GPIOZ_LOCK 0x0035
#define A5_PREG_PAD_GPIOZ_PROT 0x0036
#define A5_PREG_PAD_GPIOZ_DS 0x0037

#define A5_PREG_PAD_GPIOX_I 0x0040
#define A5_PREG_PAD_GPIOX_O 0x0041
#define A5_PREG_PAD_GPIOX_OEN 0x0042
#define A5_PREG_PAD_GPIOX_PULL_EN 0x0043
#define A5_PREG_PAD_GPIOX_PULL_UP 0x0044
#define A5_PREG_PAD_GPIOX_LOCK 0x0045
#define A5_PREG_PAD_GPIOX_PROT 0x0046
#define A5_PREG_PAD_GPIOX_DS 0x0047
#define A5_PREG_PAD_GPIOX_DS_EXT 0x0048

#define A5_PREG_PAD_GPIOT_I 0x0050
#define A5_PREG_PAD_GPIOT_O 0x0051
#define A5_PREG_PAD_GPIOT_OEN 0x0052
#define A5_PREG_PAD_GPIOT_PULL_EN 0x0053
#define A5_PREG_PAD_GPIOT_PULL_UP 0x0054
#define A5_PREG_PAD_GPIOT_LOCK 0x0055
#define A5_PREG_PAD_GPIOT_PROT 0x0056
#define A5_PREG_PAD_GPIOT_DS 0x0057

#define A5_PREG_PAD_GPIOD_I 0x0060
#define A5_PREG_PAD_GPIOD_O 0x0061
#define A5_PREG_PAD_GPIOD_OEN 0x0062
#define A5_PREG_PAD_GPIOD_PULL_EN 0x0063
#define A5_PREG_PAD_GPIOD_PULL_UP 0x0064
#define A5_PREG_PAD_GPIOD_LOCK 0x0065
#define A5_PREG_PAD_GPIOD_PROT 0x0066
#define A5_PREG_PAD_GPIOD_DS 0x0067

#define A5_PREG_PAD_GPIOE_I 0x0070
#define A5_PREG_PAD_GPIOE_O 0x0071
#define A5_PREG_PAD_GPIOE_OEN 0x0072
#define A5_PREG_PAD_GPIOE_PULL_EN 0x0073
#define A5_PREG_PAD_GPIOE_PULL_UP 0x0074
#define A5_PREG_PAD_GPIOE_LOCK 0x0075
#define A5_PREG_PAD_GPIOE_PROT 0x0076
#define A5_PREG_PAD_GPIOE_DS 0x0077

#define A5_PREG_PAD_GPIOC_I 0x0080
#define A5_PREG_PAD_GPIOC_O 0x0081
#define A5_PREG_PAD_GPIOC_OEN 0x0082
#define A5_PREG_PAD_GPIOC_PULL_EN 0x0083
#define A5_PREG_PAD_GPIOC_PULL_UP 0x0084
#define A5_PREG_PAD_GPIOC_LOCK 0x0085
#define A5_PREG_PAD_GPIOC_PROT 0x0086
#define A5_PREG_PAD_GPIOC_DS 0x0087

#define A5_PREG_PAD_GPIOB_I 0x0090
#define A5_PREG_PAD_GPIOB_O 0x0091
#define A5_PREG_PAD_GPIOB_OEN 0x0092
#define A5_PREG_PAD_GPIOB_PULL_EN 0x0093
#define A5_PREG_PAD_GPIOB_PULL_UP 0x0094
#define A5_PREG_PAD_GPIOB_LOCK 0x0095
#define A5_PREG_PAD_GPIOB_PROT 0x0096
#define A5_PREG_PAD_GPIOB_DS 0x0097

#define A5_PREG_PAD_GPIOH_I 0x00a0
#define A5_PREG_PAD_GPIOH_O 0x00a1
#define A5_PREG_PAD_GPIOH_OEN 0x00a2
#define A5_PREG_PAD_GPIOH_PULL_EN 0x00a3
#define A5_PREG_PAD_GPIOH_PULL_UP 0x00a4
#define A5_PREG_PAD_GPIOH_LOCK 0x00a5
#define A5_PREG_PAD_GPIOH_PROT 0x00a6
#define A5_PREG_PAD_GPIOH_DS 0x00a7

#define A5_PERIPHS_PIN_MUX_0 0x0000
#define A5_PERIPHS_PIN_MUX_1 0x0001
#define A5_PERIPHS_PIN_MUX_3 0x0003
#define A5_PERIPHS_PIN_MUX_4 0x0004
#define A5_PERIPHS_PIN_MUX_5 0x0005
#define A5_PERIPHS_PIN_MUX_6 0x0006
#define A5_PERIPHS_PIN_MUX_7 0x0007
#define A5_PERIPHS_PIN_MUX_9 0x0009
#define A5_PERIPHS_PIN_MUX_A 0x000a
#define A5_PERIPHS_PIN_MUX_B 0x000b
#define A5_PERIPHS_PIN_MUX_C 0x000c
#define A5_PERIPHS_PIN_MUX_G 0x0010
#define A5_PERIPHS_PIN_MUX_H 0x0011
#define A5_PERIPHS_PIN_MUX_I 0x0012
#define A5_PERIPHS_PIN_MUX_J 0x0013

#define A5_GPIOB_PIN_START 0
#define A5_GPIOC_PIN_START 14
#define A5_GPIOE_PIN_START 25
#define A5_GPIOH_PIN_START 27
#define A5_GPIOD_PIN_START 32
#define A5_GPIOT_PIN_START 48
#define A5_GPIOX_PIN_START 62
#define A5_GPIOZ_PIN_START 82

#define A5_GPIO_INT_EDGE_POLARITY 0x0
#define A5_GPIO_IRQ_0_1_PIN_FILTER_SELECT 0x1
#define A5_GPIO_IRQ_2_3_PIN_FILTER_SELECT 0x2
#define A5_GPIO_IRQ_4_5_PIN_FILTER_SELECT 0x3
#define A5_GPIO_IRQ_6_7_PIN_FILTER_SELECT 0x4
#define A5_GPIO_IRQ_8_9_PIN_FILTER_SELECT 0x5
#define A5_GPIO_IRQ_10_11_PIN_FILTER_SELECT 0x6

// GPIOD pin alternate functions
#define A5_GPIOD_12_I2C3_SDA_FN 2
#define A5_GPIOD_13_I2C3_SCL_FN 2
#define A5_GPIOD_14_I2C2_SDA_FN 2
#define A5_GPIOD_15_I2C2_SCL_FN 2

// GPIOB pin alternate functions
#define A5_GPIOB_0_EMMC_D0_FN 1
#define A5_GPIOB_1_EMMC_D1_FN 1
#define A5_GPIOB_2_EMMC_D2_FN 1
#define A5_GPIOB_3_EMMC_D3_FN 1
#define A5_GPIOB_4_EMMC_D4_FN 1
#define A5_GPIOB_5_EMMC_D5_FN 1
#define A5_GPIOB_6_EMMC_D6_FN 1
#define A5_GPIOB_7_EMMC_D7_FN 1
#define A5_GPIOB_8_EMMC_CLK_FN 1
#define A5_GPIOB_10_EMMC_CMD_FN 1
#define A5_GPIOB_11_EMMC_DS_FN 1

// GPIOC pin alternate functions
#define A5_GPIOC_2_TDMB_FS_1_FN 1
#define A5_GPIOC_3_TDMB_SCLK_1_FN 1
#define A5_GPIOC_4_MCLK_1_FN 1
#define A5_GPIOC_5_TDMB_D4_FN 1

// GPIOT pin alternate functions
#define A5_GPIOT_10_SPI_B_SS0_FN 0
#define A5_GPIOT_11_SPI_B_SCLK_FN 3
#define A5_GPIOT_12_SPI_B_MOSI_FN 3
#define A5_GPIOT_13_SPI_B_MISO_FN 3

// GPIOX pin alternate functions
#define A5_GPIOX_0_SDIO_D0_FN 1
#define A5_GPIOX_1_SDIO_D1_FN 1
#define A5_GPIOX_2_SDIO_D2_FN 1
#define A5_GPIOX_3_SDIO_D3_FN 1
#define A5_GPIOX_4_SDIO_CLK_FN 1
#define A5_GPIOX_5_SDIO_CMD_FN 1

// Ethernet
#define A5_ETH_MAC_INTR A5_GPIOZ(14)

// GPIOZ pin alternate functions
#define A5_GPIOZ_0_ETH_MDIO_FN 1
#define A5_GPIOZ_1_ETH_MDC_FN 1
#define A5_GPIOZ_2_ETH_RX_CLK_FN 1
#define A5_GPIOZ_3_ETH_RX_DV_FN 1
#define A5_GPIOZ_4_ETH_RXD0_FN 1
#define A5_GPIOZ_5_ETH_RXD1_FN 1
#define A5_GPIOZ_6_ETH_RXD2_FN 1
#define A5_GPIOZ_7_ETH_RXD3_FN 1
#define A5_GPIOZ_8_ETH_TX_CLK_FN 1
#define A5_GPIOZ_9_ETH_TX_EN_FN 1
#define A5_GPIOZ_10_ETH_TXD0_FN 1
#define A5_GPIOZ_11_ETH_TXD1_FN 1
#define A5_GPIOZ_12_ETH_TXD2_FN 1
#define A5_GPIOZ_13_ETH_TXD3_FN 1

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A5_A5_GPIO_H_
