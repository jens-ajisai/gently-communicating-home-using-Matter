#include <zephyr/kernel.h>

#if DT_NODE_HAS_COMPAT(DT_CHOSEN(zephyr_console), zephyr_cdc_acm_uart)

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usb_device.h>
LOG_MODULE_REGISTER(usb, CONFIG_MAIN_LOG_LEVEL);

#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

/* Delay for console initialization */
#define WAIT_FOR_CONSOLE_MSEC 100
#define WAIT_FOR_CONSOLE_DEADLINE_MSEC 5000

void initGpio() {
  int ret;

  if (!device_is_ready(led.port)) {
    return;
  }

  ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
  if (ret < 0) {
    return;
  }
}

int initUSB() {
  const struct device *console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
  uint32_t dtr = 0;
  uint32_t time = 0;

  initGpio();

  /* Enable the USB subsystem and associated HW */
  if (usb_enable(NULL)) {
    LOG_ERR("Failed to enable USB");
    return -1;
  } else {
    /* Wait for DTR flag or deadline (e.g. when USB is not connected) */
    while (!dtr && time < WAIT_FOR_CONSOLE_DEADLINE_MSEC) {
      uart_line_ctrl_get(console, UART_LINE_CTRL_DTR, &dtr);

      gpio_pin_toggle_dt(&led);
      /* Give CPU resources to low priority threads */
      k_sleep(K_MSEC(WAIT_FOR_CONSOLE_MSEC));
      time += WAIT_FOR_CONSOLE_MSEC;
    }
  }

  gpio_pin_set_dt(&led, 0);
  return 0;
}
#else
int initUSB() { return 0; }
#endif
