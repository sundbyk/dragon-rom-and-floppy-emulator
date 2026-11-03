#ifndef __AUTOBOOT_H
#define __AUTOBOOT_H

#include <stdint.h>
#include "ff.h"

// rom is the 16K cartridge image with the DOS rom in it, fil the open disk image
void autoboot_patch(uint8_t *rom, FIL *fil, const char *image_name, uint32_t header_length, uint32_t track_size);

#endif
