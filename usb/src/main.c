/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 Peter Lawrence
 *
 * influenced by lrndis https://github.com/fetisov/lrndis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
*/

/*
This appears as either a RNDIS or CDC-ECM USB virtual network adapter; the OS picks its preference

RNDIS should be valid on Linux and Windows hosts, and CDC-ECM should be valid on Linux and macOS hosts

The MCU appears to the host as IP address 192.168.7.1, and provides a DHCP server, DNS server, and web server.
*/

/*
Some smartphones *may* work with this implementation as well, but likely have limited (broken) drivers,
and likely their manufacturer has not tested such functionality.  Some code workarounds could be tried:

The smartphone may only have an ECM driver, but refuse to automatically pick ECM (unlike the OSes above);
try modifying ./examples/devices/net_lwip_webserver/usb_descriptors.c so that CONFIG_ID_ECM is default.

The smartphone may be artificially picky about which Ethernet MAC address to recognize; if this happens,
try changing the first byte of tud_network_mac_address[] below from 0x02 to 0x00 (clearing bit 1).
*/

#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/regs/usb.h"
#include "hardware/structs/resets.h"
#include "hardware/structs/usb.h"
#include "hardware/structs/usb_dpram.h"
#include "hardware/irq.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"

#include "device/usbd.h"
#include "bsp/board_api.h"
#include "tusb.h"

#include "usb_descriptors.h"

#include "dhserver.h"
#include "dnserver.h"
#include "lwip/apps/httpd.h"
#include "lwip/ethip6.h"
#include "lwip/init.h"
#include "lwip/timeouts.h"


#define PIN_X_OUT 40
#define PIN_Y_OUT 41
#define PIN_JOYSTICK_BTN 20

#define PIN_NEXT_BTN 38
#define RESET 39

#define DEADZONE_RADIUS 500
#define MOVEMENT_SPEED 1

#define WIDTH 240
#define HEIGHT 320
#define CENTER_X (WIDTH / 2)
#define CENTER_Y (HEIGHT / 2)

// tft resolution: 240 x 320
uint16_t cursor_x = CENTER_X; 
uint16_t cursor_y = CENTER_Y;

//--------------------------------------------------------------------+
// Joystick and Buttons Implementation
//--------------------------------------------------------------------+

void init_joystick() {
    adc_init();
    
    adc_gpio_init(PIN_X_OUT);  // x-axis
    adc_gpio_init(PIN_Y_OUT);  // y-axis
    
    for (int i = 0; i < 20; i++) {
        adc_select_input(0);
        adc_read();
        adc_select_input(1);
        adc_read();
        sleep_ms(10);
    }
    
    gpio_init(PIN_JOYSTICK_BTN); // joystick button
    gpio_set_dir(PIN_JOYSTICK_BTN, GPIO_IN);
    gpio_pull_up(PIN_JOYSTICK_BTN);
}

void init_color_buttons() {
    // push buttons are in active low configuration for better reliability
    gpio_init(RESET);
    gpio_set_dir(RESET, GPIO_IN);
    
    gpio_init(PIN_NEXT_BTN);
    gpio_set_dir(PIN_NEXT_BTN, GPIO_IN);
}

void update_cursor() {
    // y-axis (swapped from x to conform with pcb design)
    adc_select_input(0);
    sleep_us(10);
    uint16_t adc_y = adc_read();
    
    // x-axis (swapped from y to conform with pcb design)
    adc_select_input(1);
    sleep_us(10);
    uint16_t adc_x = adc_read();
    
    // adc debugging - uncomment to print out adc values
    //printf("ADC - X: %d, Y: %d\n", adc_x, adc_y);
    
    static int16_t x_state = 0;
    static int16_t y_state = 0;
    
    // x-axis logic
    if (adc_x < 2048 - DEADZONE_RADIUS) {
        x_state = -1;
    } else if (adc_x > 2048 + DEADZONE_RADIUS) {
        x_state = 1;
    } else {
        x_state = 0;
    }
    
    // y-axis logic
    if (adc_y < 2048 - DEADZONE_RADIUS) {
        y_state = -1;
    } else if (adc_y > 2048 + DEADZONE_RADIUS) {
        y_state = 1;
    } else {
        y_state = 0;
    }    
    
    uint16_t new_x = cursor_x;
    uint16_t new_y = cursor_y;

    // add in bound checking?
    if (x_state == -1) new_x -= MOVEMENT_SPEED;
    if (x_state == 1) new_x += MOVEMENT_SPEED;
    if (y_state == -1) new_y -= MOVEMENT_SPEED;
    if (y_state == 1) new_y += MOVEMENT_SPEED;

    cursor_x = new_x;
    cursor_y = new_y;
    
    // checking if cursor is in bounds of the tft
    /*
    if (x_state == -1 && new_x > 0) new_x -= MOVEMENT_SPEED;
    if (x_state == 1 && new_x < WIDTH - MOVEMENT_SPEED) new_x += MOVEMENT_SPEED;
    if (y_state == -1 && new_y > 0) new_y -= MOVEMENT_SPEED;
    if (y_state == 1 && new_y < HEIGHT - MOVEMENT_SPEED) new_y += MOVEMENT_SPEED;
    
    if (!((new_x >= TASKBAR_X1 && new_x <= TASKBAR_X2) && (new_y >= TASKBAR_Y1 && new_y <= TASKBAR_Y2))) {
        cursor_x = new_x;
        cursor_y = new_y;
    }
    */
    
    // debug statement
    //printf("Cursor - X: %d, Y: %d\n", cursor_x, cursor_y);
}


//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+

/* Blink pattern
 * - 250 ms  : device not mounted
 * - 1000 ms : device mounted
 * - 2500 ms : device is suspended
 */
enum  {
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED = 1000,
  BLINK_SUSPENDED = 2500,
};

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;

void led_blinking_task(void);
void hid_task(void);



//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
  blink_interval_ms = BLINK_MOUNTED;
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
  blink_interval_ms = BLINK_NOT_MOUNTED;
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
  (void) remote_wakeup_en;
  blink_interval_ms = BLINK_SUSPENDED;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
  blink_interval_ms = tud_mounted() ? BLINK_MOUNTED : BLINK_NOT_MOUNTED;
}



//--------------------------------------------------------------------+
// USB HID
//--------------------------------------------------------------------+

static void send_hid_report(uint8_t report_id, uint32_t btn)
{
  // skip if hid is not ready yet
  if ( !tud_hid_ready() ) return;

  // general example for multiple hid devices
  switch(report_id)
  {
    case REPORT_ID_KEYBOARD:
    {
      // use to avoid send multiple consecutive zero report for keyboard
      static bool has_keyboard_key = false;

      if ( btn )
      {
        uint8_t keycode[6] = { 0 };
        keycode[0] = HID_KEY_A;

        tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, keycode);
        has_keyboard_key = true;
      }else
      {
        // send empty key report if previously has key pressed
        if (has_keyboard_key) tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, NULL);
        has_keyboard_key = false;
      }
    }
    break;

    case REPORT_ID_MOUSE:
    {
      int8_t const delta = 5;
    


      // no button, right + down, no scroll, no pan
      tud_hid_mouse_report(REPORT_ID_MOUSE, 0x00, delta, delta, 0, 0);
    }
    break;

    case REPORT_ID_CONSUMER_CONTROL:
    {
      // use to avoid send multiple consecutive zero report
      static bool has_consumer_key = false;

      if ( btn )
      {
        // volume down
        uint16_t volume_down = HID_USAGE_CONSUMER_VOLUME_DECREMENT;
        tud_hid_report(REPORT_ID_CONSUMER_CONTROL, &volume_down, 2);
        has_consumer_key = true;
      }else
      {
        // send empty key report (release key) if previously has key pressed
        uint16_t empty_key = 0;
        if (has_consumer_key) tud_hid_report(REPORT_ID_CONSUMER_CONTROL, &empty_key, 2);
        has_consumer_key = false;
      }
    }
    break;

    case REPORT_ID_GAMEPAD:
    {
      // use to avoid send multiple consecutive zero report for keyboard
      static bool has_gamepad_key = false;

      hid_gamepad_report_t report =
      {
        .x   = 0, .y = 0, .z = 0, .rz = 0, .rx = 0, .ry = 0,
        .hat = 0, .buttons = 0
      };

      if ( btn )
      {
        report.hat = GAMEPAD_HAT_UP;
        report.buttons = GAMEPAD_BUTTON_A;
        tud_hid_report(REPORT_ID_GAMEPAD, &report, sizeof(report));

        has_gamepad_key = true;
      }else
      {
        report.hat = GAMEPAD_HAT_CENTERED;
        report.buttons = 0;
        if (has_gamepad_key) tud_hid_report(REPORT_ID_GAMEPAD, &report, sizeof(report));
        has_gamepad_key = false;
      }
    }
    break;

    default: break;
  }
}


// Every 10ms, we will sent 1 report for each HID profile (keyboard, mouse etc ..)
// tud_hid_report_complete_cb() is used to send the next report after previous one is complete
void hid_task(void)
{
  // Poll every 10ms
  const uint32_t interval_ms = 10;
  static uint32_t start_ms = 0;

  if ( board_millis() - start_ms < interval_ms) return; // not enough time
  start_ms += interval_ms;

  uint32_t const btn = board_button_read();

  // Remote wakeup
  if ( tud_suspended() && btn )
  {
    // Wake up host if we are in suspend mode
    // and REMOTE_WAKEUP feature is enabled by host
    tud_remote_wakeup();
  }else
  {
    // Send the 1st of report chain, the rest will be sent by tud_hid_report_complete_cb()
    send_hid_report(REPORT_ID_KEYBOARD, btn);
  }
}

// Invoked when sent REPORT successfully to host
// Application can use this to send the next report
// Note: For composite reports, report[0] is report ID
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len)
{
  (void) instance;
  (void) len;

  uint8_t next_report_id = report[0] + 1u;

  if (next_report_id < REPORT_ID_COUNT)
  {
    send_hid_report(next_report_id, board_button_read());
  }
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
  // TODO not Implemented
  (void) instance;
  (void) report_id;
  (void) report_type;
  (void) buffer;
  (void) reqlen;

  return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize)
{
  (void) instance;

  if (report_type == HID_REPORT_TYPE_OUTPUT)
  {
    // Set keyboard LED e.g Capslock, Numlock etc...
    if (report_id == REPORT_ID_KEYBOARD)
    {
      // bufsize should be (at least) 1
      if ( bufsize < 1 ) return;

      uint8_t const kbd_leds = buffer[0];

      if (kbd_leds & KEYBOARD_LED_CAPSLOCK)
      {
        // Capslock On: disable blink, turn led on
        blink_interval_ms = 0;
        board_led_write(true);
      }else
      {
        // Caplocks Off: back to normal blink
        board_led_write(false);
        blink_interval_ms = BLINK_MOUNTED;
      }
    }
  }
}


//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+
void led_blinking_task(void)
{
  static uint32_t start_ms = 0;
  static bool led_state = false;

  // blink is disabled
  if (!blink_interval_ms) return;

  // Blink every interval ms
  if ( board_millis() - start_ms < blink_interval_ms) return; // not enough time
  start_ms += blink_interval_ms;

  board_led_write(led_state);
  led_state = 1 - led_state; // toggle
}

/******************************************************************************************************/
/******************************************************************************************************/

// Each "platform" (like the RP2350) must define its own board_init() function.
// This comes from the datasheet's USB section: 
// https://github.com/raspberrypi/pico-examples/blob/master/usb/device/dev_lowlevel/dev_lowlevel.c#L183-L217
void board_init() {
  // Reset usb controller
  reset_unreset_block_num_wait_blocking(RESET_USBCTRL);
  
  // Clear any previous state in dpram just in case
  memset(usb_dpram, 0, sizeof(*usb_dpram));
  
  // Enable USB interrupt at processor
  irq_set_enabled(USBCTRL_IRQ, true);
  
  // Mux the controller to the onboard usb phy
  usb_hw->muxing = USB_USB_MUXING_TO_PHY_BITS | USB_USB_MUXING_SOFTCON_BITS;
  
  // Force VBUS detect so the device thinks it is plugged into a host
  usb_hw->pwr = USB_USB_PWR_VBUS_DETECT_BITS | USB_USB_PWR_VBUS_DETECT_OVERRIDE_EN_BITS;
  
  // Enable the USB controller in device mode.
  usb_hw->main_ctrl = USB_MAIN_CTRL_CONTROLLER_EN_BITS;
  
  // Enable an interrupt per EP0 transaction
  usb_hw->sie_ctrl = USB_SIE_CTRL_EP0_INT_1BUF_BITS;
  
  // Enable interrupts for when a buffer is done, when the bus is reset,
  
  // and when a setup packet is received
  usb_hw->inte = USB_INTS_BUFF_STATUS_BITS |
  USB_INTS_BUS_RESET_BITS |
  USB_INTS_SETUP_REQ_BITS;
  
  usb_hw_t *usb_hw_set = (usb_hw_t *)hw_set_alias_untyped(usb_hw);
  // Present full speed device by enabling pull up on DP
  usb_hw_set->sie_ctrl = USB_SIE_CTRL_PULLUP_EN_BITS;
}


int main(void) {
  stdio_init_all();

  /* initialize TinyUSB */
  board_init();
  printf("Proton USB has been initialized.\r\n");

  // init device stack on configured roothub port
  tud_init(BOARD_TUD_RHPORT);
  printf("tud_init initialized.\r\n");


  // implementing joystick and buttons
  init_joystick();
  init_color_buttons();

  while (1) {
    // USB: Process USB tasks if any
    // This will handle USB events like setup requests, data transfers, etc.
    tud_task();

    led_blinking_task();

    hid_task();

    // A good practice would be to call this periodically, or 
    // have the RP2350's second core handle it.  
    // Reduce infinite loops!
  }

  return 0;
}