/*
  Utilizing code from the Adafruit BNO08x Arduino Library by Bryan Siepert 
  for Adafruit Industries. Found here:
  https://github.com/adafruit/Adafruit_BNO08x

  License:
  Written by Bryan Siepert for Adafruit Industries. 
  MIT license, check license.txt for more information.
  All text above must be included in any redistribution.

  Note: Text above is the complete README.md of the repository.
  I added a copy of the readme to doc/Adafruit_BNO08x/README.md. 
*/

#include <sh2/sh2_hal.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

static const struct device *i2c_dev;

#if DT_NODE_HAS_STATUS(DT_ALIAS(xiao_i2c), okay)
#define I2C_DEV_NODE DT_ALIAS(xiao_i2c)
#else
#error "Please set the correct I2C device"
#endif

#define BNO08x_I2CADDR_DEFAULT 0x4A
#define I2C_MAX_BUFFER_SIZE 256

static int i2chal_open(sh2_Hal_t *self);
static void i2chal_close(sh2_Hal_t *self);
static int i2chal_read(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len, uint32_t *t_us);
static int i2chal_write(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len);
static uint32_t hal_getTimeUs(sh2_Hal_t *self);

/************** I2C interface **************/

// This function initializes communications with the device.  It
// can initialize any GPIO pins and peripheral devices used to
// interface with the sensor hub.
// It should also perform a reset cycle on the sensor hub to
// ensure communications start from a known state.
static int i2chal_open(sh2_Hal_t *self) {
  LOG_DBG("i2chal_open enter.");

  uint8_t softreset_pkt[] = {5, 0, 1, 0, 1};
  bool success = false;

  i2c_dev = DEVICE_DT_GET(I2C_DEV_NODE);
  if (i2c_dev == NULL) {
    /* No valid device found */
    return -1;
  }
  LOG_DBG("i2chal_open device available.");

  for (uint8_t attempts = 0; attempts < 5; attempts++) {
    LOG_DBG("i2chal_open write attempt %d.", attempts);
    if (!i2c_write(i2c_dev, softreset_pkt, 5, BNO08x_I2CADDR_DEFAULT)) {
      success = true;
      break;
    }
    k_sleep(K_MSEC(30));
  }

  if (!success) return -1;

  k_sleep(K_MSEC(300));
  LOG_DBG("i2chal_open exit success.");
  return 0;
}

// This function completes communications with the sensor hub.
// It should put the device in reset then de-initialize any
// peripherals or hardware resources that were used.
static void i2chal_close(sh2_Hal_t *self) { i2c_dev = NULL; }

// This function supports reading data from the sensor hub.
// It will be called frequently to service the device.
//
// If the HAL has received a full SHTP transfer, this function
// should load the data into pBuffer, set the timestamp to the
// time the interrupt was detected, and return the non-zero length
// of data in this transfer.
//
// If the HAL has not recevied a full SHTP transfer, this function
// should return 0.
//
// Because this function is called regularly, it can be used to
// perform other housekeeping operations.  (In the case of UART
// interfacing, bytes transmitted are staggered in time and this
// function can be used to keep the transmission flowing.)
static int i2chal_read(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len, uint32_t *t_us) {
  //  LOG_DBG("i2chal_read enter.");

  uint8_t header[4];
  if (i2c_read(i2c_dev, header, 4, BNO08x_I2CADDR_DEFAULT)) {
    return 0;
  }

  // Determine amount to read
  uint16_t packet_size = (uint16_t)header[0] | (uint16_t)header[1] << 8;
  // Unset the "continue" bit
  packet_size &= ~0x8000;

  //  LOG_DBG("Read SHTP header. Packet size: %d, buffer size: %d.", packet_size, len);

  size_t i2c_buffer_max = I2C_MAX_BUFFER_SIZE;

  if (packet_size > len) {
    // packet wouldn't fit in our buffer
    return 0;
  }
  // the number of non-header bytes to read
  uint16_t cargo_remaining = packet_size;
  static uint8_t i2c_buffer[I2C_MAX_BUFFER_SIZE];
  uint16_t read_size;
  uint16_t cargo_read_amount = 0;
  bool first_read = true;

  while (cargo_remaining > 0) {
    if (first_read) {
      read_size = MIN(i2c_buffer_max, (size_t)cargo_remaining);
    } else {
      read_size = MIN(i2c_buffer_max, (size_t)cargo_remaining + 4);
    }

    //    LOG_DBG("Reading from I2C: %d. Remaining to read: %d", read_size, cargo_remaining);

    if (i2c_read(i2c_dev, i2c_buffer, read_size, BNO08x_I2CADDR_DEFAULT)) {
      return 0;
    }

    if (first_read) {
      // The first time we're saving the "original" header, so include it in the
      // cargo count
      cargo_read_amount = read_size;
      memcpy(pBuffer, i2c_buffer, cargo_read_amount);
      first_read = false;
    } else {
      // this is not the first read, so copy from 4 bytes after the beginning of
      // the i2c buffer to skip the header included with every new i2c read and
      // don't include the header in the amount of cargo read
      cargo_read_amount = read_size - 4;
      memcpy(pBuffer, i2c_buffer + 4, cargo_read_amount);
    }
    // advance our pointer by the amount of cargo read
    pBuffer += cargo_read_amount;
    // mark the cargo as received
    cargo_remaining -= cargo_read_amount;
  }

  return packet_size;
}

// This function supports writing data to the sensor hub.
// It is called each time the application has a block of data to
// transfer to the device.
//
// If the device isn't ready to receive data, this function can
// return 0 without performing the transmit function.
//
// If the transmission can be started, this function needs to
// copy the data from pBuffer and return the number of bytes
// accepted.  It need not block.  The actual transmission of
// the data can continue after this function returns.
static int i2chal_write(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len) {
  size_t i2c_buffer_max = I2C_MAX_BUFFER_SIZE;

  LOG_DBG("Write packet size I2C: %d. max buffer size: %d", len, i2c_buffer_max);

  uint16_t write_size = MIN(i2c_buffer_max, len);
  if (i2c_write(i2c_dev, pBuffer, write_size, BNO08x_I2CADDR_DEFAULT)) {
    return 0;
  }

  return write_size;
}

// This function should return a 32-bit value representing a
// microsecond counter.  The count may roll over after 2^32
// microseconds.
static uint32_t hal_getTimeUs(sh2_Hal_t *self) {
  int64_t t = k_uptime_get() * 1000;
  LOG_DBG("hal_getTimeUs %llu", t);
  return (uint32_t)t;
}
