cmake_minimum_required(VERSION 3.20.0)

set(EI_API_KEY_HEADER "x-api-key: __REPLACE_ME__")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(posture_checker)

FILE(GLOB sh2_sources ext/sh2/*.c)

target_include_directories(app PRIVATE
    ext
)

# It is expected that the following 3 items are configured
target_sources_ifdef(CONFIG_FILE_SYSTEM
         app PRIVATE
         src/persistence/persistence.cpp
         )

target_sources_ifdef(CONFIG_ML_APP_EI_DATA_FORWARDER
         app PRIVATE
         src/ei_data_forwarder.c
         )

target_sources_ifdef(CONFIG_BT
		     app PRIVATE 
         src/bluetooth/services/postureCheckerService.c
         src/bluetooth/services/timeService.c
         src/bt.c
         )
         
target_sources(app PRIVATE
  ${sh2_sources}
  src/sensors/driver/bno08x.c
  src/sensors/sensorAggregator.cpp
  src/sensors/sensorBno08x.cpp
  src/sensors/sensorFlex.cpp
  src/sensors/sensorLsm6dsl.cpp
  src/usb/usb.c
  src/nfc/nfc.c
  src/uart/uart.c
  src/shell_commands.cpp
  src/main.cpp
  )


zephyr_include_directories(config)

# Compiler warnings to error
# zephyr_library_compile_options(-Werror)