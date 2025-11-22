#include <string.h>
#include "pico/stdlib.h"
#include "pico/unique_id.h"

#include "tusb.h"
#include "class/hid/hid.h"
#include "usb_descriptors.h"

// Unique PID for mouse-only HID
#define USB_PID 0x4002

//--------------------------------------------------------------------+
// Device Descriptor
//--------------------------------------------------------------------+
static tusb_desc_device_t const desc_device =
{
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00, // per-interface
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = 0xCafe,   // lab VID
    .idProduct          = USB_PID,  // mouse-only PID
    .bcdDevice          = 0x0100,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01
};

uint8_t const * tud_descriptor_device_cb(void)
{
  return (uint8_t const *) &desc_device;
}

//--------------------------------------------------------------------+
// HID Report Descriptor (Mouse only)
//--------------------------------------------------------------------+
uint8_t const desc_hid_mouse_report[] =
{
  TUD_HID_REPORT_DESC_MOUSE()
};

uint8_t const * tud_hid_descriptor_report_cb(uint8_t instance)
{
  (void) instance;
  return desc_hid_mouse_report;
}

//--------------------------------------------------------------------+
// Configuration Descriptor (Mouse only)
//--------------------------------------------------------------------+
#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
#define EPNUM_MOUSE       0x81

uint8_t const desc_configuration[] =
{
  // Config number, interface count, string index, total length, attribute, power (mA)
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                        TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

  // HID mouse interface
  TUD_HID_DESCRIPTOR(ITF_NUM_MOUSE, 0, HID_ITF_PROTOCOL_MOUSE,
                     sizeof(desc_hid_mouse_report),
                     EPNUM_MOUSE, CFG_TUD_HID_EP_BUFSIZE, 10)
};

uint8_t const * tud_descriptor_configuration_cb(uint8_t index)
{
  (void) index;
  return desc_configuration;
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+
enum {
  STRID_LANGID = 0,
  STRID_MANUFACTURER,
  STRID_PRODUCT,
  STRID_SERIAL,
};

static char const *string_desc_arr[] =
{
  (const char[]) { 0x09, 0x04 }, // English (0x0409)
  "ECE362",                      // Manufacturer
  "Proton HID Mouse",            // Product
  NULL,                          // Serial generated below
};

static uint16_t _desc_str[32 + 1];

// Convert pico_unique_board_id_t to hex UTF-16 string
static size_t serial_to_utf16(uint16_t* out, size_t max_chars)
{
  pico_unique_board_id_t id;
  pico_get_unique_board_id(&id);

  // each byte -> 2 hex chars
  static const char hex[] = "0123456789ABCDEF";
  size_t n = 0;

  for (int i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES && (n + 2) <= max_chars; i++)
  {
    uint8_t b = id.id[i];
    out[n++] = hex[b >> 4];
    out[n++] = hex[b & 0x0F];
  }
  return n;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
  (void) langid;
  size_t chr_count;

  switch ( index )
  {
    case STRID_LANGID:
      memcpy(&_desc_str[1], string_desc_arr[0], 2);
      chr_count = 1;
      break;

    case STRID_SERIAL:
      chr_count = serial_to_utf16(_desc_str + 1, 32);
      break;

    default:
      if ( !(index < sizeof(string_desc_arr)/sizeof(string_desc_arr[0])) ) return NULL;

      const char *str = string_desc_arr[index];
      chr_count = strlen(str);

      size_t const max_count = sizeof(_desc_str)/sizeof(_desc_str[0]) - 1;
      if ( chr_count > max_count ) chr_count = max_count;

      for ( size_t i = 0; i < chr_count; i++ )
      {
        _desc_str[1+i] = str[i];
      }
      break;
  }

  _desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8 ) | (2*chr_count + 2));
  return _desc_str;
}
