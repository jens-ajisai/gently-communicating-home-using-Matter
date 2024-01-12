#include "uart_task.h"

#include <stdio.h>

#include "hardware/irq.h"
#include "hardware/uart.h"
#include "pico/util/queue.h"

#include "logging.h"

#define UART_ID uart0
#define BAUD_RATE 115200

static queue_t* cmd_queue;

// RX interrupt handler
void on_uart_rx() {
  while (uart_is_readable(UART_ID)) {
    uint8_t ch = uart_getc(UART_ID);
    if (!queue_try_add(cmd_queue, &ch)) {
      logging("FIFO was full\n");
    }
  }
}

void init_uart(queue_t* queue) {
  cmd_queue = queue;

  // Set up our UART with a basic baud rate.
  uart_init(UART_ID, BAUD_RATE);

  // Set the TX and RX pins by using the function select on the GPIO
  // Set datasheet for more information on function select
  // gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
  // gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

  // Set UART flow control CTS/RTS, we don't want these, so turn them off
  uart_set_hw_flow(UART_ID, false, false);

  // Turn off FIFO's - we want to do this character by character
  uart_set_fifo_enabled(UART_ID, false);

  // Set up a RX interrupt
  // We need to set up the handler first
  // Select correct interrupt for the UART we are using
  int UART_IRQ = UART_ID == uart0 ? UART0_IRQ : UART1_IRQ;

  // And set up and enable the interrupt handlers
  irq_set_exclusive_handler(UART_IRQ, on_uart_rx);
  irq_set_enabled(UART_IRQ, true);

  // Now enable the UART to send interrupts - RX only
  uart_set_irq_enables(UART_ID, true, false);

  logging("Init uart complete\n");
}
