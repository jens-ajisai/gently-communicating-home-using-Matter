cmake_minimum_required(VERSION 3.13)

add_executable(nfc_usb)

# Example source
target_sources(nfc_usb PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/logging.c
        ${CMAKE_CURRENT_LIST_DIR}/main.c
        ${CMAKE_CURRENT_LIST_DIR}/nfc_helper.c
        ${CMAKE_CURRENT_LIST_DIR}/nfc_task.c
        ${CMAKE_CURRENT_LIST_DIR}/uart_task.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_comm.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_descriptors_print.c
        # can use 'tinyusb_pico_pio_usb' library later when pico-sdk is updated
        ${PICO_TINYUSB_PATH}/src/portable/raspberrypi/pio_usb/dcd_pio_usb.c
        ${PICO_TINYUSB_PATH}/src/portable/raspberrypi/pio_usb/hcd_pio_usb.c
        )

# Make sure TinyUSB can find tusb_config.h
target_include_directories(nfc_usb PUBLIC
        ${CMAKE_CURRENT_LIST_DIR})

# use tinyusb implementation
target_compile_definitions(nfc_usb PRIVATE PIO_USB_USE_TINYUSB)

# In addition to pico_stdlib required for common PicoSDK functionality, add dependency on tinyusb_host
# for TinyUSB device support and tinyusb_board for the additional board support library used by the example
target_link_libraries(nfc_usb PUBLIC pico_stdlib pico_pio_usb tinyusb_host tinyusb_board hardware_uart)

pico_add_extra_outputs(nfc_usb)

# add url via pico_set_program_url
example_auto_set_url(nfc_usb)
