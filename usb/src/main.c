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
#include "hardware/timer.h"
#include "stdio.h"

#include "hardware/pwm.h"

//#include "device/usbd.h"
//#include "bsp/board_api.h"
#include "tusb.h"

#include "usb_descriptors.h"

// --- TFT Lab hardware pins reused ---
#define PIN_X_OUT          40  // Joystick Y -> ADC0
#define PIN_Y_OUT          41  // Joystick X -> ADC1
#define PIN_JOYSTICK_BTN   20  // Middle click (active-low)

#define PIN_LEFT_BTN       38  // Left click (active-low)
#define PIN_RIGHT_BTN      39  // Right click (active-low)

#define DEADZONE_RADIUS    500
#define MOVEMENT_SPEED     1

// pin was originally assigned to user led
#define USER_LED          2

#define RGB_R_PIN         3
#define RGB_G_PIN         4
#define RGB_B_PIN         5

static int duty_cycle = 0;
static int dir = 0;
static int color = 0;

//--------------------------------------------------------------------+
// Joystick and Buttons Implementation
//--------------------------------------------------------------------+

static void init_joystick(void)
{
    adc_init();
    adc_gpio_init(PIN_X_OUT);
    adc_gpio_init(PIN_Y_OUT);

    for (int i = 0; i < 20; i++) {
        adc_select_input(0); 
        adc_read();
        adc_select_input(1); 
        adc_read();
        sleep_ms(10);
    }
}

static void init_buttons(void)
{
    gpio_init(PIN_JOYSTICK_BTN);
    gpio_set_dir(PIN_JOYSTICK_BTN, GPIO_IN);
    gpio_pull_up(PIN_JOYSTICK_BTN);

    gpio_init(PIN_LEFT_BTN);
    gpio_set_dir(PIN_LEFT_BTN, GPIO_IN);
    

    gpio_init(PIN_RIGHT_BTN);
    gpio_set_dir(PIN_RIGHT_BTN, GPIO_IN);
    
}

static void read_joystick(int8_t *dx_out, int8_t *dy_out)
{
    adc_select_input(0);
    sleep_us(10);
    uint16_t adc_y = adc_read();

    adc_select_input(1);
    sleep_us(10);
    uint16_t adc_x = adc_read();

    int8_t dx = 0, dy = 0;

    if (adc_x < 2048 - DEADZONE_RADIUS) dx = -MOVEMENT_SPEED;
    else if (adc_x > 2048 + DEADZONE_RADIUS) dx = MOVEMENT_SPEED;

    if (adc_y < 2048 - DEADZONE_RADIUS) dy = -MOVEMENT_SPEED;
    else if (adc_y > 2048 + DEADZONE_RADIUS) dy = MOVEMENT_SPEED;

    dy = -dy; // push up -> cursor up

    *dx_out = dx;
    *dy_out = dy;
}

//--------------------------------------------------------------------+
// USER LED
//--------------------------------------------------------------------+
void init_user_led() {
    // initialize user led
    gpio_init(USER_LED);
    gpio_set_dir(USER_LED, true);
    gpio_put(USER_LED, 0); // initialize to low initially
}

void init_gpio_irq() {
    u_int32_t mask = (0b1 << PIN_LEFT_BTN) | (0b1 << PIN_JOYSTICK_BTN) | (0b1 << PIN_RIGHT_BTN);
    gpio_add_raw_irq_handler_masked(mask, gpio_isr);
    // enable BANK0 IRQ interrupt
    irq_set_enabled(IO_IRQ_BANK0, true);
    // enable the GPIO IRQ for both pins
    gpio_set_irq_enabled(PIN_LEFT_BTN, GPIO_IRQ_EDGE_RISE, true);
    gpio_set_irq_enabled(PIN_JOYSTICK_BTN, GPIO_IRQ_EDGE_RISE, true);
    gpio_set_irq_enabled(PIN_RIGHT_BTN, GPIO_IRQ_EDGE_RISE, true);
}

void gpio_isr() {
    if (gpio_get_irq_event_mask(PIN_LEFT_BTN) & GPIO_IRQ_EDGE_RISE) {
       gpio_acknowledge_irq(PIN_LEFT_BTN, GPIO_IRQ_EDGE_RISE);
       // handle the IRQ
       // turn on user light
       gpio_put(USER_LED, 1);

    } else if (gpio_get_irq_event_mask(PIN_JOYSTICK_BTN) &  GPIO_IRQ_EDGE_RISE) {
       gpio_acknowledge_irq(PIN_JOYSTICK_BTN, GPIO_IRQ_EDGE_RISE);
      // handle the IRQ
      gpio_put(USER_LED, 1);
    } else if (gpio_get_irq_event_mask(PIN_RIGHT_BTN) &  GPIO_IRQ_EDGE_RISE) {
       gpio_acknowledge_irq(PIN_RIGHT_BTN, GPIO_IRQ_EDGE_RISE);
      // handle the IRQ
      gpio_put(USER_LED, 1);
    }

    // flash the light on for 5ms
    // one shot timer
    init_led_timer();
}

init_led_timer(){
  int ALARM_NUM1 = 1;
  hw_set_bits(&timer0_hw->inte, 1u << ALARM_NUM1);

  int ALARM_IRQ1 = timer_hardware_alarm_get_irq_num(timer0_hw, ALARM_NUM1);
  irq_set_exclusive_handler(ALARM_IRQ1, led_isr);

  irq_set_enabled(ALARM_IRQ1, true);

  uint32_t delay1 = 5000;
  uint64_t target1 = timer0_hw->timerawl + delay1;

  timer0_hw->alarm[ALARM_NUM1] = (uint32_t) target1;
}

led_isr(){
  hw_clear_bits(&timer0_hw->intr, 1u << 1);

  uint32_t mask = 1u << USER_LED;
  gpio_put(USER_LED, 0);
}


//--------------------------------------------------------------------+
// Blinking Task
//--------------------------------------------------------------------+

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+

/* Blink pattern
 * - 250 ms  : device not mounted
 * - 1000 ms : device mounted
 * - 2500 ms : device is suspended
 */
enum  {
  BLINK_NOT_MOUNTED = 5000,
  BLINK_MOUNTED = 10000,
  BLINK_SUSPENDED = 20000,
};

static uint32_t blink_interval_us = BLINK_NOT_MOUNTED;

void hid_task(void);

void init_pwm_static(uint32_t period, uint32_t duty_cycle) {
    // fill in
    gpio_set_function(3, GPIO_FUNC_PWM);
    gpio_set_function(4, GPIO_FUNC_PWM);
    gpio_set_function(5, GPIO_FUNC_PWM);

    uint slice_num1 = pwm_gpio_to_slice_num(3);
    uint slice_num2 = pwm_gpio_to_slice_num(4);

    pwm_set_clkdiv(slice_num1, 150);
    pwm_set_clkdiv(slice_num2, 150);

    pwm_set_wrap(slice_num1, period-1);
    pwm_set_wrap(slice_num2, period-1);

    pwm_set_chan_level(slice_num1, 1, duty_cycle);
    pwm_set_chan_level(slice_num2, 0, duty_cycle);
    pwm_set_chan_level(slice_num2, 1, duty_cycle);

    pwm_set_enabled(slice_num1, true);
    pwm_set_enabled(slice_num2, true);
}

void pwm_breathing() {
    // fill in
    uint slice_num1 = pwm_gpio_to_slice_num(3);

    pwm_clear_irq(slice_num1);

    if(dir == 0 && duty_cycle == 100){
        color++;
        color %= 3;
    }

    if(duty_cycle == 100 && dir == 0){
        dir = 1;
    }else if(duty_cycle == 0 && dir == 1){
        dir = 0;
    }

    if(dir == 0){
        duty_cycle++;
    }else{
        duty_cycle--;
    }

    int slice_num = (color == 1 || color == 2) ? 2 : 1;

    // set the chosen color's duty cycle to the ratio of the duty cycle in terms of frequency
    uint16_t current_period = pwm_hw->slice[slice_num].top;

    pwm_set_gpio_level(color + 3, (duty_cycle * current_period / 100));
}

void init_pwm_irq() {
    // fill in
    uint slice_num1 = pwm_gpio_to_slice_num(3);
    uint slice_num2 = pwm_gpio_to_slice_num(4);

    //why don't we do the process for slice 11

    pwm_irqn_set_slice_enabled(0, slice_num1, true);

    irq_set_exclusive_handler(PWM_IRQ_WRAP_0, pwm_breathing);
    
    irq_set_enabled(PWM_IRQ_WRAP_0, true);

    // get current period of PWM slice associated with GP37
    uint16_t current_period = pwm_hw->slice[slice_num1].top;

    duty_cycle = 100;
    dir = 1;

    pwm_set_chan_level(slice_num1, 1, current_period);
    pwm_set_chan_level(slice_num2, 0, current_period);
    pwm_set_chan_level(slice_num2, 1, current_period);

}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
  blink_interval_us = BLINK_MOUNTED;
  
  uint slice_num1 = pwm_gpio_to_slice_num(3);
  uint slice_num2 = pwm_gpio_to_slice_num(4);
  pwm_set_wrap(slice_num1, blink_interval_us-1);
  pwm_set_wrap(slice_num2, blink_interval_us-1);
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
  blink_interval_us = BLINK_NOT_MOUNTED;

  uint slice_num1 = pwm_gpio_to_slice_num(3);
  uint slice_num2 = pwm_gpio_to_slice_num(4);
  pwm_set_wrap(slice_num1, blink_interval_us-1);
  pwm_set_wrap(slice_num2, blink_interval_us-1);
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
  (void) remote_wakeup_en;
  blink_interval_us = BLINK_SUSPENDED;

  uint slice_num1 = pwm_gpio_to_slice_num(3);
  uint slice_num2 = pwm_gpio_to_slice_num(4);
  pwm_set_wrap(slice_num1, blink_interval_us-1);
  pwm_set_wrap(slice_num2, blink_interval_us-1);
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
  blink_interval_us = tud_mounted() ? BLINK_MOUNTED : BLINK_NOT_MOUNTED;

  uint slice_num1 = pwm_gpio_to_slice_num(3);
  uint slice_num2 = pwm_gpio_to_slice_num(4);
  pwm_set_wrap(slice_num1, blink_interval_us-1);
  pwm_set_wrap(slice_num2, blink_interval_us-1);
}



//--------------------------------------------------------------------+
// USB HID
//--------------------------------------------------------------------+

static void send_hid_report(uint32_t btn)
{
  // skip if hid is not ready yet
  if ( !tud_hid_ready() ) return;

  int8_t dx=0, dy=0;
  read_joystick(&dx, &dy);

  bool left   = (gpio_get(PIN_LEFT_BTN) == 0);
  bool right  = (gpio_get(PIN_RIGHT_BTN) == 0);
  bool middle = (gpio_get(PIN_JOYSTICK_BTN) == 0);

  uint8_t buttons = 0;
  if (left)   buttons |= 0x01;
  if (right)  buttons |= 0x02;
  if (middle) buttons |= 0x04;

  tud_hid_mouse_report(0, buttons, dx, dy, 0, 0);
}


// Every 10ms, we will sent 1 report for each HID profile (keyboard, mouse etc ..)
// tud_hid_report_complete_cb() is used to send the next report after previous one is complete

void init_hid_task_timer() {
  // Poll every 10ms

  int ALARM_NUM0 = 0;
  hw_set_bits(&timer0_hw->inte, 1u << ALARM_NUM0);

  int ALARM_IRQ0 = timer_hardware_alarm_get_irq_num(timer0_hw, ALARM_NUM0);
  irq_set_exclusive_handler(ALARM_IRQ0, hid_report_isr);

  irq_set_enabled(ALARM_IRQ0, true);

  uint32_t delay0 = 10000;
  uint64_t target0 = timer0_hw->timerawl + delay0;

  timer0_hw->alarm[ALARM_NUM0] = (uint32_t) target0;

}

void hid_report_isr(){
  // acknowledge interrupt
  hw_clear_bits(&timer0_hw->intr, 1u << 0);

  // send hid report
  uint32_t const any_pressed = (gpio_get(PIN_LEFT_BTN) & gpio_get(PIN_RIGHT_BTN) & gpio_get(PIN_JOYSTICK_BTN)) == 0;

  // Remote wakeup
  if ( tud_suspended() && any_pressed)
  {
    // Wake up host if we are in suspend mode
    // and REMOTE_WAKEUP feature is enabled by host
    tud_remote_wakeup();
  }else
  {
    // Send the 1st of report chain, the rest will be sent by tud_hid_report_complete_cb()
    send_hid_report(any_pressed);
  }

  // reset timer
  uint32_t delay0 = 10000;
  uint64_t target0 = timer0_hw->timerawl + delay0;
  timer0_hw->alarm[0] = (uint32_t) target0;
}

// removed
// Invoked when sent REPORT successfully to host
// Application can use this to send the next report
// Note: For composite reports, report[0] is report ID
//void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len)

// not needed
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
// void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize)


//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+
/*
init_led_timer(){
  int ALARM_NUM1 = 1;
  hw_set_bits(&timer0_hw->inte, 1u << ALARM_NUM1);

  int ALARM_IRQ1 = timer_hardware_alarm_get_irq_num(timer0_hw, ALARM_NUM1);
  irq_set_exclusive_handler(ALARM_IRQ1, led_isr);

  irq_set_enabled(ALARM_IRQ1, true);

  uint32_t delay1 = blink_interval_us;
  uint64_t target1 = timer0_hw->timerawl + delay1;

  timer0_hw->alarm[ALARM_NUM1] = (uint32_t) target1;
}

led_isr(){
  hw_clear_bits(&timer0_hw->intr, 1u << 1);

  uint32_t mask = 1u << USER_LED;
  gpio_xor_mask(mask);

  uint32_t delay1 = blink_interval_us;
  uint64_t target1 = timer0_hw->timerawl + delay1;
  timer0_hw->alarm[1] = (uint32_t) target1;
}
*/

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
  init_buttons();

  init_hid_task_timer();
  
  init_user_led();
  init_gpio_irq();

  init_pwm_static(blink_interval_us, blink_interval_us / 2); // Start out with 500/1000, 50%
  init_pwm_irq(); // Initialize PWM IRQ for variable duty cycle

  while (1) {
    // USB: Process USB tasks if any
    // This will handle USB events like setup requests, data transfers, etc.
    tud_task();

    //led_blinking_task();

    //hid_task();

    // A good practice would be to call this periodically, or 
    // have the RP2350's second core handle it.  
    // Reduce infinite loops!
  }

  return 0;
}