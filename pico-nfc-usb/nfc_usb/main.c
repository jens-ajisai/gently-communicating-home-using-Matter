#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/board.h"
#include "pico/util/queue.h"

#include "pio_usb.h"
#include "tusb.h"

#include "nfc_helper.h"
#include "nfc_task.h"
#include "uart_task.h"
#include "usb_comm.h"
#include "usb_descriptors_print.h"

#include "logging.h"

#define HOST_PIN_DP 2  // Pin used as D+ for host, D- = D+ + 1
#define RX_QUEUE_LENGTH (256)

queue_t cmd_queue;
tusb_desc_device_t desc_device;
bool usb_attached = false;

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+
void led_blinking_task(void);

/*------------- MAIN -------------*/
int main(void) {
  board_init();

  logging("TinyUSB Bare API Example\r\n");

  pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
  pio_cfg.pin_dp = HOST_PIN_DP;

// This detection would only work if wifi is used, but fails if using a pico-w without wifi ...
// I use a pico.
#if defined(CYW43_HOST_NAME)
  /* https://github.com/sekigon-gonnoc/Pico-PIO-USB/issues/46 */
  pio_cfg.sm_tx = 3;
  pio_cfg.sm_rx = 2;
  pio_cfg.sm_eop = 3;
  pio_cfg.pio_rx_num = 0;
  pio_cfg.pio_tx_num = 1;
  pio_cfg.tx_ch = 9;
#endif /* CYW43_HOST_NAME */

  tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);

  // init host stack on configured roothub port
  tuh_init(BOARD_TUH_RHPORT);
  while (1) {
    sleep_ms(100);

    // tinyusb host task
    tuh_task();
    led_blinking_task();
    if (usb_attached) {
      nfc_task();
    }
  }
  return 0;
}

/*------------- TinyUSB Callbacks -------------*/

// Invoked when device is mounted (configured)
void tuh_mount_cb(uint8_t daddr) {
  logging("Device attached, address = %d\r\n", daddr);

  // Get Device Descriptor
  tuh_descriptor_get_device(daddr, &desc_device, 18, print_device_descriptor, 0);
  usb_attached = true;

  // initialize the queue that connects uart with nfc task.
  queue_init(&cmd_queue, sizeof(enum commands), RX_QUEUE_LENGTH);
  // nfc tasks gets the queue where it reads commands from and a function to send commands to nfc module.
  nfc_task_init(&cmd_queue, send_frame);
  // uart gets the queue where to write commands to.
  init_uart(&cmd_queue);
  // usb communication gets a function to send the received data to for further processing.
  usb_comm_init(nfc_handleMessage);
}

/// Invoked when device is unmounted (bus reset/unplugged)
void tuh_umount_cb(uint8_t daddr) {
  logging("Device removed, address = %d\r\n", daddr);
  stop_receiver(daddr);
  usb_attached = false;
}

//--------------------------------------------------------------------+
// Blinking Task
//--------------------------------------------------------------------+
void led_blinking_task(void) {
  const uint32_t interval_ms = 1000;
  static uint32_t start_ms = 0;

  static bool led_state = false;

  // Blink every interval ms
  if (board_millis() - start_ms < interval_ms) return;  // not enough time
  start_ms += interval_ms;

  board_led_write(led_state);
  led_state = 1 - led_state;  // toggle
}
