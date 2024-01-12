#pragma once
#include <stdint.h>

#include "tusb.h"

typedef void (*handle_message_t)(uint8_t* buf, uint16_t buf_len);

void usb_comm_init(handle_message_t handle_message);

void send_frame(uint8_t* send_buf, uint16_t send_buf_len);
void stop_receiver(uint8_t daddr);

// needs to be known by usb_descriptors_print.
// tiny_usb callbacks do not allow injecting this function.
void open_interface(uint8_t daddr, tusb_desc_interface_t const* desc_itf, uint16_t max_len);
