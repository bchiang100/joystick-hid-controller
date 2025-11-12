#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lcd.h"

// A simple header for our raw files.
typedef struct {
    uint32_t width;
    uint32_t height;
} RawImageHeader;

// Make it a nice struct with all the attributes...
typedef struct {
    unsigned int   width;
    unsigned int   height;
    unsigned int   bytes_per_pixel;
    unsigned char *pixel_data;
} MutablePicture;

Picture* load_image(const uint8_t* raw_data) {
    // This function takes a pointer to the C array data.
    MutablePicture* pic_mut = (MutablePicture*)malloc(sizeof(MutablePicture));
    if (!pic_mut) return NULL;

    // Read width and height from the first 8 bytes of the array.
    pic_mut->width  = *(const uint32_t*)&raw_data[0];
    pic_mut->height = *(const uint32_t*)&raw_data[4];
    pic_mut->bytes_per_pixel = 2; // 2 bytes = 16-bit pixel

    // The pixel data starts right after the 8-byte header
    // NOTE: We "cast away" const here because the Picture struct 
    // expects a non-const pointer, but we know we will only be 
    // reading from it.
    pic_mut->pixel_data = (unsigned char*)&raw_data[8];

    return (Picture*)pic_mut;
}

// Special free function for flash images, since pixel_data is not on the heap
void free_image(Picture* pic) {
    if (pic) {
        free((void*)pic);
    }
}