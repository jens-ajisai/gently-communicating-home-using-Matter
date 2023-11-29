#
# Copyright (c) 2021 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

get_filename_component(MATTER_MODULE_ROOT $ENV{ZEPHYR_BASE}/../modules/lib/matter REALPATH)

# Set Kconfig root files that will be processed as a first Kconfig for used child images.
set(mcuboot_KCONFIG_ROOT ${MATTER_MODULE_ROOT}/config/nrfconnect/chip-module/Kconfig.mcuboot.root)
set(multiprotocol_rpmsg_KCONFIG_ROOT ${MATTER_MODULE_ROOT}/config/nrfconnect/chip-module/Kconfig.multiprotocol_rpmsg.root)

find_package(Zephyr HINTS $ENV{ZEPHYR_BASE})

project(feedback-light)

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
    src/led_strip.cpp
    src/zcl_callbacks.cpp
    src/zap-generated/IMClusterCommandHandler.cpp
    src/zap-generated/callback-stub.cpp
    ${COMMON_ROOT}/led_widget.cpp
)

chip_configure_data_model(app
    INCLUDE_SERVER
    BYPASS_IDL
    GEN_DIR src/zap-generated
    ZAP_FILE ${CMAKE_CURRENT_SOURCE_DIR}/src/feedback_light.zap
)
# NORDIC SDK APP END


# Compiler warnings to error
zephyr_library_compile_options(-Werror)