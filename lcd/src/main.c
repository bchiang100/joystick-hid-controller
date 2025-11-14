#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/adc.h"
#include "lcd.h"
#include <stdio.h>
#include <string.h>
#include <math.h>   

/****************************************** */
#define PIN_SDI    19
#define PIN_CS     17
#define PIN_SCK    18
#define PIN_DC     16
#define PIN_nRESET 15

#define PIN_X_OUT 40
#define PIN_Y_OUT 41
#define PIN_JOYSTICK_BTN 20

#define MOVEMENT_SPEED 1
// tft resolution: 240 x 320
uint16_t cursor_x = 120; 
uint16_t cursor_y = 160;

// previous position to erase old cursor
uint16_t prev_x = 120;
uint16_t prev_y = 160;


bool is_drawing = true;

// Uncomment the following #define when 
// you are ready to run Step 3.

// WARNING: The process will take a VERY 
// long time as it compiles and uploads 
// all the image frames into the uploaded 
// binary!  Expect to wait 5 minutes.
//#define DRAW

/****************************************** */
#ifdef DRAW
#endif
/****************************************** */

void init_spi_lcd() {
    gpio_set_function(PIN_CS, GPIO_FUNC_SIO);
    gpio_set_function(PIN_DC, GPIO_FUNC_SIO);
    gpio_set_function(PIN_nRESET, GPIO_FUNC_SIO);

    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_set_dir(PIN_DC, GPIO_OUT);
    gpio_set_dir(PIN_nRESET, GPIO_OUT);

    gpio_put(PIN_CS, 1); // CS high
    gpio_put(PIN_DC, 0); // DC low
    gpio_put(PIN_nRESET, 1); // nRESET high

    // initialize SPI1 with 48 MHz clock
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SDI, GPIO_FUNC_SPI);
    spi_init(spi0, 100 * 1000 * 1000);
    spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
}

void init_joystick() {
    adc_init();
    adc_set_temp_sensor_enabled(false);
    
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
    gpio_pull_up(PIN_JOYSTICK_BTN); // button is now active low
}

void update_cursor() {
    // x-axis
    adc_select_input(0);  // channel 0
    sleep_us(10);
    uint16_t adc_x = adc_read();
    
    // y-axis
    adc_select_input(1);  // channel 1
    sleep_us(10);
    uint16_t adc_y = adc_read();
    
    // adc debugging
    printf("ADC - X: %d, Y: %d\n", adc_x, adc_y);
    
    static int16_t x_state = 0;  // -1=left, 0=neutral, 1=right
    static int16_t y_state = 0;  // -1=up, 0=neutral, 1=down
    
    // x-axis logic
    if (adc_x < 1900) {
        x_state = -1;  // moves left
    } else if (adc_x > 2120) {
        x_state = 1;   // moves right
    } else {
        x_state = 0;
    }

    // y-axis logic
    if (adc_y < 1900) {
        y_state = -1;  // moves up
    } else if (adc_y > 2120) {
        y_state = 1;   // moves down
    } else {
        y_state = 0;
    }
    
    // Apply movement based on state
    if (x_state == -1 && cursor_x > 0) cursor_x -= MOVEMENT_SPEED;
    if (x_state == 1 && cursor_x < 239) cursor_x += MOVEMENT_SPEED;
    if (y_state == -1 && cursor_y > 0) cursor_y -= MOVEMENT_SPEED;
    if (y_state == 1 && cursor_y < 319) cursor_y += MOVEMENT_SPEED;
    
    // debug statement
    printf("Cursor - X: %d, Y: %d\n", cursor_x, cursor_y);
}

int main() {
    stdio_init_all();

    init_spi_lcd();

    LCD_Setup();
    LCD_Clear(0x0000); // Clear the screen to black

    #ifndef DRAW

    init_joystick();

    #define WHITE 0xFFFF
    #define RED   0xF800

    while(1) {
        update_cursor();
        
        if (gpio_get(20) == 0) {
            is_drawing = !is_drawing;
            printf("Drawing mode: %d\n", is_drawing);
            sleep_ms(500);  // debouncing delay

        }
        
        if (is_drawing) {
            LCD_DrawPoint(cursor_x, cursor_y, WHITE);
        } else {
            // erases previous cursor position
            if (prev_x != cursor_x || prev_y != cursor_y) {
                LCD_DrawPoint(prev_x, prev_y, 0);
            }
            // draws new cursor position (red cursor when not drawing)
            LCD_DrawPoint(cursor_x, cursor_y, RED);
        }
        
        // updates positions
        prev_x = cursor_x;
        prev_y = cursor_y;

        sleep_ms(10);
    }
    #endif
    for(;;);
}