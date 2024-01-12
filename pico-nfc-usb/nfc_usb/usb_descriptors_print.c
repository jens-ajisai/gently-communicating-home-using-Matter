#include <stdint.h>

#include "logging.h"
#include "tusb.h"
#include "usb_comm.h"

// English
#define LANGUAGE_ID 0x0409

// tuh_descriptor_get_device does not give back the device descriptor in its callback.
// Therefore it must be a global variable.
extern tusb_desc_device_t desc_device;

static void print_utf16(uint16_t* temp_buf, size_t buf_len);
static void parse_config_descriptor(uint8_t dev_addr, tusb_desc_configuration_t const* desc_cfg);

//--------------------------------------------------------------------+
// Device Descriptor
//--------------------------------------------------------------------+
void print_device_descriptor(tuh_xfer_t* xfer) {
  if (XFER_RESULT_SUCCESS != xfer->result) {
    logging("Failed to get device descriptor\r\n");
    return;
  }

  uint8_t const daddr = xfer->daddr;

  logging("Device %u: ID %04x:%04x\r\n", daddr, desc_device.idVendor, desc_device.idProduct);
  logging("Device Descriptor:\r\n");
  logging("  bLength             %u\r\n", desc_device.bLength);
  logging("  bDescriptorType     %u\r\n", desc_device.bDescriptorType);
  logging("  bcdUSB              %04x\r\n", desc_device.bcdUSB);
  logging("  bDeviceClass        %u\r\n", desc_device.bDeviceClass);
  logging("  bDeviceSubClass     %u\r\n", desc_device.bDeviceSubClass);
  logging("  bDeviceProtocol     %u\r\n", desc_device.bDeviceProtocol);
  logging("  bMaxPacketSize0     %u\r\n", desc_device.bMaxPacketSize0);
  logging("  idVendor            0x%04x\r\n", desc_device.idVendor);
  logging("  idProduct           0x%04x\r\n", desc_device.idProduct);
  logging("  bcdDevice           %04x\r\n", desc_device.bcdDevice);

  // Get String descriptor using Sync API
  uint16_t temp_buf[128];

  logging("  iManufacturer       %u     ", desc_device.iManufacturer);
  if (XFER_RESULT_SUCCESS ==
      tuh_descriptor_get_manufacturer_string_sync(daddr, LANGUAGE_ID, temp_buf, sizeof(temp_buf))) {
    print_utf16(temp_buf, TU_ARRAY_SIZE(temp_buf));
  }
  logging("\r\n");

  logging("  iProduct            %u     ", desc_device.iProduct);
  if (XFER_RESULT_SUCCESS ==
      tuh_descriptor_get_product_string_sync(daddr, LANGUAGE_ID, temp_buf, sizeof(temp_buf))) {
    print_utf16(temp_buf, TU_ARRAY_SIZE(temp_buf));
  }
  logging("\r\n");

  logging("  iSerialNumber       %u     ", desc_device.iSerialNumber);
  if (XFER_RESULT_SUCCESS ==
      tuh_descriptor_get_serial_string_sync(daddr, LANGUAGE_ID, temp_buf, sizeof(temp_buf))) {
    print_utf16(temp_buf, TU_ARRAY_SIZE(temp_buf));
  }
  logging("\r\n");

  logging("  bNumConfigurations  %u\r\n", desc_device.bNumConfigurations);

  // Get configuration descriptor with sync API
  if (XFER_RESULT_SUCCESS ==
      tuh_descriptor_get_configuration_sync(daddr, 0, temp_buf, sizeof(temp_buf))) {
    parse_config_descriptor(daddr, (tusb_desc_configuration_t*)temp_buf);
  } else {
    logging("tuh_descriptor_get_configuration_sync failed.\r\n");
  }
  logging("\r\n");
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+
static uint16_t count_interface_total_len(tusb_desc_interface_t const* desc_itf, uint8_t itf_count,
                                          uint16_t max_len);

static void parse_config_descriptor(uint8_t dev_addr, tusb_desc_configuration_t const* desc_cfg) {
  uint8_t const* desc_end = ((uint8_t const*)desc_cfg) + tu_le16toh(desc_cfg->wTotalLength);

  if (TUSB_DESC_CONFIGURATION == tu_desc_type(desc_cfg)) {
    logging("Configuration Descriptor:\r\n");
    logging("  bLength             %u\r\n", desc_cfg->bLength);
    logging("  bDescriptorType     %u\r\n", desc_cfg->bDescriptorType);
    logging("  wTotalLength        %u\r\n", desc_cfg->wTotalLength);
    logging("  bNumInterfaces      %u\r\n", desc_cfg->bNumInterfaces);
    logging("  bConfigurationValue %u\r\n", desc_cfg->bConfigurationValue);
    logging("  iConfiguration      %u\r\n", desc_cfg->iConfiguration);
    logging("  bmAttributes        %u\r\n", desc_cfg->bmAttributes);
    logging("  bMaxPower           %u\r\n", desc_cfg->bMaxPower);
    logging("\r\n");
  }

  uint8_t const* p_desc = tu_desc_next(desc_cfg);

  // parse each interfaces
  while (p_desc < desc_end) {
    uint8_t assoc_itf_count = 1;

    // Class will always starts with Interface Association (if any) and then Interface descriptor
    if (TUSB_DESC_INTERFACE_ASSOCIATION == tu_desc_type(p_desc)) {
      tusb_desc_interface_assoc_t const* desc_iad = (tusb_desc_interface_assoc_t const*)p_desc;
      assoc_itf_count = desc_iad->bInterfaceCount;

      logging("USB Interface Association Descriptor:\r\n");
      logging("  bLength             %u\r\n", desc_iad->bLength);
      logging("  bDescriptorType     %u\r\n", desc_iad->bDescriptorType);
      logging("  bFirstInterface     %u\r\n", desc_iad->bFirstInterface);
      logging("  bInterfaceCount     %u\r\n", desc_iad->bInterfaceCount);
      logging("  bFunctionClass      %u\r\n", desc_iad->bFunctionClass);
      logging("  bFunctionSubClass   %u\r\n", desc_iad->bFunctionSubClass);
      logging("  bFunctionProtocol   %u\r\n", desc_iad->bFunctionProtocol);
      logging("  iFunction           %u\r\n", desc_iad->iFunction);
      logging("\r\n");

      p_desc = tu_desc_next(p_desc);  // next to Interface
    }

    // must be interface from now
    if (TUSB_DESC_INTERFACE != tu_desc_type(p_desc)) return;
    tusb_desc_interface_t const* desc_itf = (tusb_desc_interface_t const*)p_desc;

    uint16_t const drv_len =
        count_interface_total_len(desc_itf, assoc_itf_count, (uint16_t)(desc_end - p_desc));

    // probably corrupted descriptor
    if (drv_len < sizeof(tusb_desc_interface_t)) return;

    logging("Interface Descriptor:\r\n");
    logging("  bLength             %u\r\n", desc_itf->bLength);
    logging("  bDescriptorType     %u\r\n", desc_itf->bDescriptorType);
    logging("  bInterfaceNumber    %u\r\n", desc_itf->bInterfaceNumber);
    logging("  bAlternateSetting   %u\r\n", desc_itf->bAlternateSetting);
    logging("  bNumEndpoints       %u\r\n", desc_itf->bNumEndpoints);
    logging("  bInterfaceClass     %u\r\n", desc_itf->bInterfaceClass);
    logging("  bInterfaceSubClass  %u\r\n", desc_itf->bInterfaceSubClass);
    logging("  bInterfaceProtocol  %u\r\n", desc_itf->bInterfaceProtocol);
    logging("  iInterface          %u\r\n", desc_itf->iInterface);
    logging("\r\n");

    open_interface(dev_addr, desc_itf, drv_len);

    // next Interface or IAD descriptor
    p_desc += drv_len;
  }
}

static uint16_t count_interface_total_len(tusb_desc_interface_t const* desc_itf, uint8_t itf_count,
                                          uint16_t max_len) {
  uint8_t const* p_desc = (uint8_t const*)desc_itf;
  uint16_t len = 0;

  while (itf_count--) {
    // Next on interface desc
    len += tu_desc_len(desc_itf);
    p_desc = tu_desc_next(p_desc);

    while (len < max_len) {
      // return on IAD regardless of itf count
      if (tu_desc_type(p_desc) == TUSB_DESC_INTERFACE_ASSOCIATION) return len;

      if ((tu_desc_type(p_desc) == TUSB_DESC_INTERFACE) &&
          ((tusb_desc_interface_t const*)p_desc)->bAlternateSetting == 0) {
        break;
      }

      len += tu_desc_len(p_desc);
      p_desc = tu_desc_next(p_desc);
    }
  }

  return len;
}

//--------------------------------------------------------------------+
// String Descriptor Helper
//--------------------------------------------------------------------+

static void _convert_utf16le_to_utf8(const uint16_t* utf16, size_t utf16_len, uint8_t* utf8,
                                     size_t utf8_len) {
  // TODO: Check for runover.
  (void)utf8_len;
  // Get the UTF-16 length out of the data itself.

  for (size_t i = 0; i < utf16_len; i++) {
    uint16_t chr = utf16[i];
    if (chr < 0x80) {
      *utf8++ = chr & 0xffu;
    } else if (chr < 0x800) {
      *utf8++ = (uint8_t)(0xC0 | (chr >> 6 & 0x1F));
      *utf8++ = (uint8_t)(0x80 | (chr >> 0 & 0x3F));
    } else {
      // TODO: Verify surrogate.
      *utf8++ = (uint8_t)(0xE0 | (chr >> 12 & 0x0F));
      *utf8++ = (uint8_t)(0x80 | (chr >> 6 & 0x3F));
      *utf8++ = (uint8_t)(0x80 | (chr >> 0 & 0x3F));
    }
    // TODO: Handle UTF-16 code points that take two entries.
  }
}

// Count how many bytes a utf-16-le encoded string will take in utf-8.
static int _count_utf8_bytes(const uint16_t* buf, size_t len) {
  size_t total_bytes = 0;
  for (size_t i = 0; i < len; i++) {
    uint16_t chr = buf[i];
    if (chr < 0x80) {
      total_bytes += 1;
    } else if (chr < 0x800) {
      total_bytes += 2;
    } else {
      total_bytes += 3;
    }
    // TODO: Handle UTF-16 code points that take two entries.
  }
  return (int)total_bytes;
}

static void print_utf16(uint16_t* temp_buf, size_t buf_len) {
  size_t utf16_len = ((temp_buf[0] & 0xff) - 2) / sizeof(uint16_t);
  size_t utf8_len = (size_t)_count_utf8_bytes(temp_buf + 1, utf16_len);
  _convert_utf16le_to_utf8(temp_buf + 1, utf16_len, (uint8_t*)temp_buf, sizeof(uint16_t) * buf_len);
  ((uint8_t*)temp_buf)[utf8_len] = '\0';

  logging((char*)temp_buf);
}
