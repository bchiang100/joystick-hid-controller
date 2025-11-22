#pragma once

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------+
// TinyUSB configuration for Proton (RP2350 / RP2040-compatible port)
// Mouse-only HID device
//--------------------------------------------------------------------+

// MCU and OS
#define CFG_TUSB_MCU           OPT_MCU_RP2040     // RP2350 uses RP2040 TinyUSB port in pico-sdk
#define CFG_TUSB_OS            OPT_OS_PICO

// Root hub port 0 as DEVICE, Full Speed
#define CFG_TUSB_RHPORT0_MODE  (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

// Enable Device stack
#define CFG_TUD_ENABLED        1

// EP0 size
#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE 64
#endif

//--------------------------------------------------------------------+
// DEVICE CLASS CONFIGURATION
//--------------------------------------------------------------------+

// Enable only HID (mouse)
#define CFG_TUD_HID            1
#define CFG_TUD_CDC            0
#define CFG_TUD_MSC            0
#define CFG_TUD_MIDI           0
#define CFG_TUD_VENDOR         0

// HID interrupt EP buffer size
// Mouse report is small; 8 bytes is enough
#ifndef CFG_TUD_HID_EP_BUFSIZE
#define CFG_TUD_HID_EP_BUFSIZE 8
#endif

#ifdef __cplusplus
}
#endif
