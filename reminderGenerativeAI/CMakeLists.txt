# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(dmic)

target_sources(app PRIVATE 
    src/main.c 
    src/button.c 
    src/reminder.c 
    src/recorder.c 
    src/persistence/persistence.c
)


target_sources_ifdef(CONFIG_WIFI
         app PRIVATE
         src/net/wifi/wifi.c
         src/ai/socket_common.c
         src/ai/whisper.c
         src/ai/completions.c
         )

set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated/)

generate_inc_file_for_target(
    app
    src/ai/Baltimore\ CyberTrust\ Root.cer
    ${gen_dir}/https-cert.der.inc
    )
