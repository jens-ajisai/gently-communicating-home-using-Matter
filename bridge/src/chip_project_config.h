/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 *    @file
 *          Example project configuration file for CHIP.
 *
 *          This is a place to put application or project-specific overrides
 *          to the default configuration values for general CHIP features.
 *
 */

#pragma once

#define CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT CONFIG_BRIDGE_MAX_DYNAMIC_ENDPOINTS_NUMBER

/*
 * Switching from Thread child to router may cause a few second packet stall.
 * Until this is improved in OpenThread we need to increase the retransmission
 * interval to survive the stall.
 */
#define CHIP_CONFIG_MRP_LOCAL_IDLE_RETRY_INTERVAL (1000_ms32)
#define CHIP_CONFIG_MRP_LOCAL_ACTIVE_RETRY_INTERVAL (1000_ms32)

/* Disable handling only single connection to avoid stopping Matter service BLE advertising
 * while other BLE bridge-related connections are open.
 */
#define CHIP_DEVICE_CONFIG_CHIPOBLE_SINGLE_CONNECTION 0
