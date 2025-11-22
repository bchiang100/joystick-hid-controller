#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"

#include "tusb.h"
#include "class/hid/hid_device.h"   // HID device API
#include "tusb_config.h"
#include "usb_descriptors.h"

// --- TFT Lab hardware pins reused ---
#define PIN_X_OUT          40  // Joystick Y -> ADC0
#define PIN_Y_OUT          41  // Joystick X -> ADC1
#define PIN_JOYSTICK_BTN   20  // Middle click (active-low)
#define PIN_LEFT_BTN       38  // Left click (active-low)
#define PIN_RIGHT_BTN      39  // Right click (active-low)

#define DEADZONE_RADIUS    500
#define MOVEMENT_SPEED     2

static void init_joystick(void);
static void init_buttons(void);
static void read_joystick(int8_t *dx, int8_t *dy);
static void hid_mouse_task(void);

int main(void)
{
    stdio_init_all();   // pico-sdk init (NOT TinyUSB board BSP)

    init_joystick();
    init_buttons();

    tusb_init();        // TinyUSB device stack init

    while (1)
    {
        tud_task();        // TinyUSB device task
        hid_mouse_task();  // send mouse reports
        sleep_ms(1);
    }
}

static void init_joystick(void)
{
    adc_init();
    adc_gpio_init(PIN_X_OUT);
    adc_gpio_init(PIN_Y_OUT);

    for (int i = 0; i < 20; i++) {
        adc_select_input(0); adc_read();
        adc_select_input(1); adc_read();
        sleep_ms(5);
    }
}

static void init_buttons(void)
{
    gpio_init(PIN_JOYSTICK_BTN);
    gpio_set_dir(PIN_JOYSTICK_BTN, GPIO_IN);
    gpio_pull_up(PIN_JOYSTICK_BTN);

    gpio_init(PIN_LEFT_BTN);
    gpio_set_dir(PIN_LEFT_BTN, GPIO_IN);
    gpio_pull_up(PIN_LEFT_BTN);

    gpio_init(PIN_RIGHT_BTN);
    gpio_set_dir(PIN_RIGHT_BTN, GPIO_IN);
    gpio_pull_up(PIN_RIGHT_BTN);
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

static void hid_mouse_task(void)
{
    const uint32_t interval_ms = 10;
    static uint32_t last_ms = 0;

    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    if (now_ms - last_ms < interval_ms) return;
    last_ms = now_ms;

    if (!tud_hid_ready()) return;  // macro -> tud_hid_n_ready(0)

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

// TinyUSB HID callbacks
uint16_t tud_hid_get_report_cb(uint8_t instance,
                               uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t* buffer, uint16_t reqlen)
{
    (void) instance; (void) report_id; (void) report_type;
    (void) buffer; (void) reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance,
                           uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const* buffer, uint16_t bufsize)
{
    (void) instance; (void) report_id; (void) report_type;
    (void) buffer; (void) bufsize;
}
