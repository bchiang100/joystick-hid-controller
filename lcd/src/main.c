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
#define PIN_nRESET 20

#define MOVEMENT_SPEED 2
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
#include "images.h"
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
    spi_set_format(spi1, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
}

void init_joystick() {
    adc_init();
    
    adc_gpio_init(26);  // x-axis
    adc_gpio_init(27);  // y-axis
    
    gpio_init(15); // joystick button
    gpio_set_dir(15, GPIO_IN);
}

void update_cursor() {
    // x-axis
    adc_select_input(0);  // channel 0
    uint16_t adc_x = adc_read();
    
    // y-axis
    adc_select_input(1);  // channel 1
    uint16_t adc_y = adc_read();
    
    // schmitt trigger like implementation w/ hysterisis
    #define THRES_LOW   1800
    #define THRES_HIGH  2300
    
    // state variables to keep track due to hysterisis
    static int8_t x_state = 0;  // -1=left, 0=neutral, 1=right
    static int8_t y_state = 0;  // -1=up, 0=neutral, 1=down
    
    // x-axis logic
    if (adc_x < THRES_LOW) {
        x_state = -1;  // moves left
    } else if (adc_x > THRES_HIGH) {
        x_state = 1;   // moves right
    } else {
        // within deadzone range
        if (x_state == -1 && adc_x > THRES_HIGH) {
            x_state = 0;
        } else if (x_state == 1 && adc_x < THRES_LOW) {
            x_state = 0;
        }
    }
    
    // y-axis logic
    if (adc_y < THRES_LOW) {
        y_state = -1;  // moves up
    } else if (adc_y > THRES_HIGH) {
        y_state = 1;   // moves down
    } else {
        // within deadzone range
        if (y_state == -1 && adc_y > THRES_HIGH) {
            y_state = 0;
        } else if (y_state == 1 && adc_y < THRES_LOW) {
            y_state = 0;
        }
    }
    
    // Apply movement based on state
    if (x_state == -1) cursor_x -= MOVEMENT_SPEED;
    if (x_state == 1)  cursor_x += MOVEMENT_SPEED;
    if (y_state == -1) cursor_y -= MOVEMENT_SPEED;
    if (y_state == 1)  cursor_y += MOVEMENT_SPEED;
    
    // keeps cursor within bounds
    if (cursor_x > 240) cursor_x = 0;
    if (cursor_x >= 240) cursor_x = 239;
    if (cursor_y > 320) cursor_y = 0;
    if (cursor_y >= 320) cursor_y = 319;
}

Picture* load_image(const char* image_data);
void free_image(Picture* pic);

int main() {
    stdio_init_all();

    init_spi_lcd();

    LCD_Setup();
    LCD_Clear(0x0000); // Clear the screen to black

    #ifndef DRAW
    

    #define WHITE 0xFFFF
    #define RED   0xF800
    
    LCD_DrawPoint(120, 160, WHITE);
    LCD_DrawPoint(100, 100, RED);
    LCD_DrawPoint(140, 200, RED);
    LCD_DrawPoint(210, 50, RED);
    
    while(1) {
        sleep_ms(1000);
    }
    
    #endif
    
    /*
        Now, for some more fun!

        Uncomment the DRAW #define at the top of main.c 
        to run this section.
        
        We've converted a popular GIF into a series of images, 
        and stored each of those frames in its own C array.  
        Look at the lab for the script we wrote to do this.

        This is an example of how you can draw a very large picture, 
        but notice how slow the DRAW is, even at 100 MHz.  
        The LCD_DrawPicture function is not really intended for such 
        large images, but it will be very helpful for smaller ones, 
        like scary monsters and nice sprites in a game.
    */ 

    #ifdef DRAW
    Picture* frame_pic = NULL;
    int frame_index = 0;
    while (1) { // Loop forever
        // Get the next frame from the array
        frame_pic = load_image(mystery_frames[frame_index]);
    
        if (frame_pic) {
            // Draw the frame to the top-left corner of the screen
            LCD_DrawPicture(0, 0, frame_pic);
            
            // Free the Picture struct (not the pixel data)
            free_image(frame_pic);
        }
    
        // Move to the next frame, looping back to the start
        frame_index++;
        if (frame_index >= mystery_frame_count) {
            frame_index = 0;
        }
    
        // Add a small delay to control DRAW speed
        sleep_ms(1); // Adjust delay as needed
    }
    #endif

    for(;;);
}