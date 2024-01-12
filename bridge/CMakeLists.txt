#
# Copyright (c) 2023 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

get_filename_component(MATTER_MODULE_ROOT $ENV{ZEPHYR_BASE}/../modules/lib/matter REALPATH)

# Set Kconfig root files that will be processed as a first Kconfig for used child images.
set(mcuboot_KCONFIG_ROOT "${MATTER_MODULE_ROOT}/config/nrfconnect/chip-module/Kconfig.mcuboot.root")
# used for Matter over Thread
set(multiprotocol_rpmsg_KCONFIG_ROOT "${MATTER_MODULE_ROOT}/config/nrfconnect/chip-module/Kconfig.multiprotocol_rpmsg.root")
list(APPEND multiprotocol_rpmsg_b0n_OVERLAY_CONFIG overlay-minimal-size.conf)
# used for Matter over Wi-Fi
set(hci_rpmsg_KCONFIG_ROOT "${MATTER_MODULE_ROOT}/config/nrfconnect/chip-module/Kconfig.hci_rpmsg.root")
list(APPEND hci_rpmsg_b0n_OVERLAY_CONFIG overlay-minimal-size.conf)

find_package(Zephyr HINTS $ENV{ZEPHYR_BASE})

project(matter-bridge)

set(COMMON_ROOT src/common)
set(NLIO_ROOT ${MATTER_MODULE_ROOT}/third_party/nlio/repo)
include(${MATTER_MODULE_ROOT}/config/nrfconnect/app/enable-gnu-std.cmake)
include(${MATTER_MODULE_ROOT}/src/app/chip_data_model.cmake)

# NORDIC SDK APP START
target_include_directories(app PRIVATE
    src
    ${COMMON_ROOT}
    ${NLIO_ROOT}/include
    ${MATTER_MODULE_ROOT}/zzz_generated/app-common
)

target_sources(app PRIVATE
    src/app_task.cpp
    src/main.cpp
    src/util.cpp
    src/bridge/bridge_manager.cpp
    src/bridge/matter_device.cpp
    src/bridge/matter_device_ble.cpp
    src/bridge/matter_device_fixed.cpp
    src/bridge/ble_connectivity_manager.cpp
    src/bridge/ble_device.cpp
    src/bridge/oob_exchange_manager.cpp
    src/uart/nfc_uart.cpp
    src/zap-generated/IMClusterCommandHandler.cpp
    src/zap-generated/callback-stub.cpp
    ${COMMON_ROOT}/led_widget.cpp
)

target_sources_ifdef(CONFIG_WIFI
        app PRIVATE
        src/reminders/ai/completions.c
        src/reminders/ai/socket_common.c
        src/reminders/ai/whisper.c
        src/reminders/persistence/persistence.c
        src/reminders/recorder.c
        src/reminders/reminder.c
        src/reminders/reminders_app.c
        src/display.c
)

set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated/)

generate_inc_file_for_target(
    app
    src/reminders/ai/Baltimore\ CyberTrust\ Root.cer
    ${gen_dir}/https-cert.der.inc
)

target_sources_ifndef(CONFIG_USB_DEVICE_INITIALIZE_AT_BOOT
    app PRIVATE
    src/usb/usb.cpp
)

target_sources_ifdef(CONFIG_SHELL
    app PRIVATE
    src/bridge_shell.cpp
)

if(CONFIG_CHIP_OTA_REQUESTOR)
    target_sources(app PRIVATE ${COMMON_ROOT}/src/ota_util.cpp)
endif()

chip_configure_data_model(app
    INCLUDE_SERVER
    BYPASS_IDL
    GEN_DIR src/zap-generated
    ZAP_FILE ${CMAKE_CURRENT_SOURCE_DIR}/src/bridge.zap
)
# NORDIC SDK APP END

# Requires fixing a warning in zephyr/include/zephyr/net/ieee802154_pkt.h
#   Add cast to line 72 (at least ncs v2.4.1,v2.5.0):
#     return (struct net_pkt_cb_ieee802154*) net_pkt_cb(pkt);
#zephyr_library_compile_options(-std=gnu++20 -Wno-volatile -Werror)
zephyr_library_compile_options(-std=gnu++20 -Wno-volatile)