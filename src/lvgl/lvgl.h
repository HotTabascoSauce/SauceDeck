#ifndef STREAMDECK_LVGL_COMPAT_H
#define STREAMDECK_LVGL_COMPAT_H

#include <stdint.h>

typedef struct {
  struct {
    uint32_t cf;
    uint32_t magic;
    uint32_t w;
    uint32_t h;
  } header;
  uint32_t data_size;
  const void *data;
} lv_image_dsc_t;

#define LV_COLOR_FORMAT_RGB565 0
#define LV_IMAGE_HEADER_MAGIC 0
#define LV_ATTRIBUTE_LARGE_CONST

#endif
