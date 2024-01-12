#include "usb_comm.h"

#include <stdint.h>

#include "logging.h"
#include "tusb.h"
#define BUF_COUNT 4
#define BUF_LEN (256)

static uint8_t g_ep_out_addr = 0;
static uint8_t g_daddr = 0;

static uint8_t buf_pool[BUF_COUNT][BUF_LEN];
static uint8_t buf_owner[BUF_COUNT] = {0};  // device address that owns buffer

static handle_message_t handle_message_f;

static uint8_t* get_xfer_buf(uint8_t daddr);
static void free_xfer_buf(uint8_t daddr);

static void recv_report_received(tuh_xfer_t* xfer);
static void recv_frame(uint8_t daddr, uint8_t ep_addr);
static void send_report_received(tuh_xfer_t* xfer);

void usb_comm_init(handle_message_t handle_message) { handle_message_f = handle_message; }

void send_frame(uint8_t* send_buf, uint16_t send_buf_len) {
  tuh_xfer_t xfer = {
      .daddr = g_daddr,
      .ep_addr = g_ep_out_addr,
      .buflen = send_buf_len,
      .buffer = send_buf,
      .complete_cb = send_report_received,
      .user_data = (uintptr_t)
          send_buf,  // since buffer is not available in callback, use user data to store the buffer
  };

  // submit transfer for this EP
  tuh_edpt_xfer(&xfer);

  logging("Listen to [dev %u: ep %02x, buf %p, len %d]\r\n", g_daddr, g_ep_out_addr, send_buf,
          send_buf_len);
}

void open_interface(uint8_t daddr, tusb_desc_interface_t const* desc_itf, uint16_t max_len) {
  // len = interface + hid + n*endpoints
  uint16_t const drv_len = (uint16_t)(sizeof(tusb_desc_interface_t) +
                                      desc_itf->bNumEndpoints * sizeof(tusb_desc_endpoint_t));

  uint8_t ep_in_addr = 0;
  uint8_t ep_out_addr = 0;

  // corrupted descriptor
  if (max_len < drv_len) return;

  uint8_t const* p_desc = (uint8_t const*)desc_itf;

  // Endpoint descriptor
  p_desc = tu_desc_next(p_desc);
  tusb_desc_endpoint_t const* desc_ep = (tusb_desc_endpoint_t const*)p_desc;

  for (int i = 0; i < desc_itf->bNumEndpoints; i++) {
    if (TUSB_DESC_ENDPOINT != desc_ep->bDescriptorType) return;

    logging("Endpoint Descriptor:\r\n");
    logging("  bLength             %u\r\n", desc_ep->bLength);
    logging("  bDescriptorType     %u\r\n", desc_ep->bDescriptorType);
    logging("  bEndpointAddress    %u\r\n", desc_ep->bEndpointAddress);
    logging("  wMaxPacketSize      %u\r\n", desc_ep->wMaxPacketSize);
    logging("  bInterval           %u\r\n", desc_ep->bInterval);
    logging("  xfer                %u\r\n", desc_ep->bmAttributes.xfer);
    logging("  sync                %u\r\n", desc_ep->bmAttributes.sync);
    logging("  usage               %u\r\n", desc_ep->bmAttributes.usage);
    logging("\r\n");

    if (tu_edpt_dir(desc_ep->bEndpointAddress) == TUSB_DIR_IN) {
      // skip if failed to open endpoint
      if (!tuh_edpt_open(daddr, desc_ep)) return;

      ep_in_addr = desc_ep->bEndpointAddress;
    }

    if (tu_edpt_dir(desc_ep->bEndpointAddress) == TUSB_DIR_OUT) {
      // skip if failed to open endpoint
      if (!tuh_edpt_open(daddr, desc_ep)) return;

      ep_out_addr = desc_ep->bEndpointAddress;
    }

    p_desc = tu_desc_next(p_desc);
    desc_ep = (tusb_desc_endpoint_t const*)p_desc;
  }

  recv_frame(daddr, ep_in_addr);
  g_ep_out_addr = ep_out_addr;
  g_daddr = daddr;
}

void stop_receiver(uint8_t daddr) { free_xfer_buf(daddr); }

static void recv_report_received(tuh_xfer_t* xfer) {
  // Note: not all field in xfer is available for use (i.e filled by tinyusb stack) in callback to
  // save sram For instance, xfer->buffer is NULL. We have used user_data to store buffer when
  // submitted callback
  uint8_t* buf = (uint8_t*)xfer->user_data;

  if (xfer->result == XFER_RESULT_SUCCESS) {
    logging("[dev %u: ep %02x, buf %p] Len: %d, Report:", xfer->daddr, xfer->ep_addr, buf,
            xfer->actual_len);
    for (uint32_t i = 0; i < xfer->actual_len; i++) {
      if (i % 16 == 0) logging("\r\n  ");
      logging("%02X ", buf[i]);
    }
    logging("\r\n");

    handle_message_f(buf, xfer->actual_len);
  }

  // continue to submit transfer, with updated buffer
  // other field remain the same
  xfer->buflen = BUF_LEN;
  xfer->buffer = buf;

  tuh_edpt_xfer(xfer);
}

static void recv_frame(uint8_t daddr, uint8_t ep_addr) {
  uint8_t* buf = get_xfer_buf(daddr);
  if (!buf) return;  // out of memory

  tuh_xfer_t xfer = {
      .daddr = daddr,
      .ep_addr = ep_addr,
      .buflen = BUF_LEN,
      .buffer = buf,
      .complete_cb = recv_report_received,
      .user_data = (uintptr_t)
          buf,  // since buffer is not available in callback, use user data to store the buffer
  };

  // submit transfer for this EP
  tuh_edpt_xfer(&xfer);
}

static void send_report_received(tuh_xfer_t* xfer) {
  // Note: not all field in xfer is available for use (i.e filled by tinyusb stack) in callback to
  // save sram For instance, xfer->buffer is NULL. We have used user_data to store buffer when
  // submitted callback
  uint8_t* buf = (uint8_t*)xfer->user_data;

  if (xfer->result == XFER_RESULT_SUCCESS) {
    logging("[dev %u: ep %02x, buf %p] Report:", xfer->daddr, xfer->ep_addr, buf);
    for (uint32_t i = 0; i < xfer->actual_len; i++) {
      if (i % 16 == 0) logging("\r\n  ");
      logging("%02X ", buf[i]);
    }
    logging("\r\n");
  }
}

//--------------------------------------------------------------------+
// Buffer helper
//--------------------------------------------------------------------+

// get an buffer from pool
static uint8_t* get_xfer_buf(uint8_t daddr) {
  for (size_t i = 0; i < BUF_COUNT; i++) {
    if (buf_owner[i] == 0) {
      buf_owner[i] = daddr;
      return buf_pool[i];
    }
  }

  // out of memory, increase BUF_COUNT
  return NULL;
}

// free all buffer owned by device
static void free_xfer_buf(uint8_t daddr) {
  for (size_t i = 0; i < BUF_COUNT; i++) {
    if (buf_owner[i] == daddr) buf_owner[i] = 0;
  }
}
