#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/adc.h"
#include "lcd.h"
#include <stdio.h>

// TODO:
// initialize spi lcd: done
// initialize joystick: done
// add hysterisis deadzone logic: done
// add cursor and draw modes: done
// implement taskbar: done
// implement different colors: in progress
// add color selector: in progress
// bonus - add pen icon: not started

#define PIN_SDI    19
#define PIN_CS     17
#define PIN_SCK    18
#define PIN_DC     16
#define PIN_nRESET 15

#define PIN_X_OUT 40
#define PIN_Y_OUT 41
#define PIN_JOYSTICK_BTN 20
#define PIN_PREV_BTN 38
#define PIN_NEXT_BTN 39
#define SELECTOR_COLOR 0x01FF

#define DEADZONE_RADIUS 300
#define MOVEMENT_SPEED 1

#define WIDTH 240
#define HEIGHT 320
#define CENTER_X (WIDTH / 2)
#define CENTER_Y (HEIGHT / 2)

#define TASKBAR_X1 40
#define TASKBAR_X2 200
#define TASKBAR_Y1 0
#define TASKBAR_Y2 20

// Color definitions
#define BLACK   0x0000
#define GRAY    0x7BEF
#define DARK_RED   0x8000
#define RED     0xF800
#define ORANGE  0xFD20
#define YELLOW  0xFFE0
#define GREEN   0x07E0
#define CYAN    0x07FF
#define BLUE    0x001F
#define PURPLE  0x8010
#define MAGENTA 0xF81F
#define WHITE   0xFFFF
#define TAN     0xF5AD

#define NUM_COLORS 12

// tft resolution: 240 x 320
uint16_t cursor_x = CENTER_X; 
uint16_t cursor_y = CENTER_Y;

// previous position to erase old cursor
uint16_t prev_x = CENTER_X;
uint16_t prev_y = CENTER_Y;

bool is_drawing = false;
uint16_t current_color = WHITE;
int selected_color = 2;

// Color palette array
uint16_t colors[NUM_COLORS] = {
    BLACK, GRAY, DARK_RED, RED, ORANGE, YELLOW,
    GREEN, CYAN, BLUE, PURPLE, MAGENTA, WHITE
};

void init_spi_lcd() {
    gpio_set_function(PIN_CS, GPIO_FUNC_SIO);
    gpio_set_function(PIN_DC, GPIO_FUNC_SIO);
    gpio_set_function(PIN_nRESET, GPIO_FUNC_SIO);

    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_set_dir(PIN_DC, GPIO_OUT);
    gpio_set_dir(PIN_nRESET, GPIO_OUT);

    gpio_put(PIN_CS, 1);
    gpio_put(PIN_DC, 0);
    gpio_put(PIN_nRESET, 1);

    // initialize SPI0 (48 MHz clock)
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SDI, GPIO_FUNC_SPI);
    spi_init(spi0, 100 * 1000 * 1000);
    spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
}

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
    gpio_pull_up(PIN_JOYSTICK_BTN); // button is now active low
}

void init_color_buttons() {
    gpio_init(PIN_PREV_BTN);
    gpio_set_dir(PIN_PREV_BTN, GPIO_IN);
    //gpio_pull_up(PIN_PREV_BTN);  // Active low
    
    gpio_init(PIN_NEXT_BTN);
    gpio_set_dir(PIN_NEXT_BTN, GPIO_IN);
    //gpio_pull_up(PIN_NEXT_BTN);  // Active low
}

void update_cursor() {
    // x-axis
    adc_select_input(0);
    sleep_us(10);
    uint16_t adc_x = adc_read();
    
    // y-axis
    adc_select_input(1);
    sleep_us(10);
    uint16_t adc_y = adc_read();
    
    // adc debugging
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
    
    if (x_state == -1 && new_x > 0) new_x -= MOVEMENT_SPEED;
    if (x_state == 1 && new_x < WIDTH - MOVEMENT_SPEED) new_x += MOVEMENT_SPEED;
    if (y_state == -1 && new_y > 0) new_y += MOVEMENT_SPEED;
    if (y_state == 1 && new_y < HEIGHT - MOVEMENT_SPEED) new_y -= MOVEMENT_SPEED;
    
    if (!((new_x >= TASKBAR_X1 && new_x <= TASKBAR_X2) && (new_y >= TASKBAR_Y1 && new_y <= TASKBAR_Y2))) {
        cursor_x = new_x;
        cursor_y = new_y;
    }
    
    // debug statement
    //printf("Cursor - X: %d, Y: %d\n", cursor_x, cursor_y);
}

void draw_taskbar() {
    LCD_DrawFillRectangle(TASKBAR_X1, TASKBAR_Y1, TASKBAR_X2, TASKBAR_Y2, TAN);

    int box_width = 12;
    int box_height = 15;
    int start_x = TASKBAR_X1 + 5;
    int start_y = TASKBAR_Y1 + 2;
    
    for (int i = 0; i < NUM_COLORS; i++) {
        LCD_DrawFillRectangle(start_x + i*box_width, start_y, start_x + i*box_width + box_width - 2, start_y + box_height, colors[i]);
    }
    
    int sel_x = start_x + selected_color * box_width;
    
    LCD_DrawRectangle(sel_x - 2, start_y - 2, sel_x + box_width, start_y + box_height + 2, SELECTOR_COLOR);
    LCD_DrawRectangle(sel_x - 1, start_y - 1, sel_x + box_width - 1, start_y + box_height + 1, SELECTOR_COLOR);
}

void color_select() {
    static bool prev_next_pressed = false;
    static bool prev_prev_pressed = false;
    
    if (gpio_get(PIN_NEXT_BTN) == 0 && !prev_next_pressed) {
        selected_color++;
        if (selected_color >= NUM_COLORS) {
            selected_color = 0;
        }
        current_color = colors[selected_color];
        draw_taskbar();
        prev_next_pressed = true;
        sleep_ms(50);
    } else if (gpio_get(PIN_NEXT_BTN) == 1) {
        prev_next_pressed = false;
    }
    
    if (gpio_get(PIN_PREV_BTN) == 0 && !prev_prev_pressed) {
        selected_color--;
        if (selected_color < 0) {
            selected_color = NUM_COLORS - 1;
        }
        current_color = colors[selected_color];
        draw_taskbar();
        prev_prev_pressed = true;
        sleep_ms(50);
    } else if (gpio_get(PIN_PREV_BTN) == 1) {
        prev_prev_pressed = false;
    }
}

int main() {
    stdio_init_all();

    init_spi_lcd();

    LCD_Setup();
    LCD_Clear(0);

    init_joystick();
    init_color_buttons();
    draw_taskbar();

    while(1) {
        update_cursor();
        color_select();
        
        if (gpio_get(20) == 0) {
            is_drawing = !is_drawing;
            //printf("Drawing mode: %d\n", is_drawing);
            sleep_ms(500);
        }
        
        if (is_drawing) {
            LCD_DrawPoint(cursor_x, cursor_y, current_color);
            if (prev_x != cursor_x || prev_y != cursor_y) {
                LCD_DrawPoint(prev_x, prev_y, current_color);
            }
            LCD_DrawPoint(cursor_x, cursor_y, current_color);
        } else {
            if (prev_x != cursor_x || prev_y != cursor_y) {
                LCD_DrawPoint(prev_x, prev_y, 0);
            }
            LCD_DrawPoint(cursor_x, cursor_y, WHITE);
        }
        
        prev_x = cursor_x;
        prev_y = cursor_y;

        sleep_ms(10);
    }
}
