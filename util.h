#ifndef __UTIL_H
#define __UTIL_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "stm32f4xx.h"

#include "ff.h"

#include "defines.h"

void delay_us(const uint16_t us);
void delay_ms(const uint16_t ms);
void blink_debug_led(int delay);

uint32_t suffix_match(char *name, char *suffix);
FRESULT load_rom(char *fname, char* highbuffer);
uint32_t load_directory(char *dirname, unsigned char *buffer, uint32_t with_up);
uint32_t menu_entry_ordinal(uint32_t index);
uint32_t is_disk_image(char *name);
uint32_t is_image_file(FILINFO *fno);
uint32_t is_folder(FILINFO *fno);
uint32_t is_cas_file(FILINFO *fno);
const char *load_cas(char *fname, uint8_t *out, uint8_t *end);
FRESULT readdir_image(DIR *dir, FILINFO *fno);
FRESULT readdir_menu(DIR *dir, FILINFO *fno);
FRESULT find_menu_entry(DIR *dir, char *path, uint32_t ordinal, FILINFO *fno, int32_t *images_before);

#endif
