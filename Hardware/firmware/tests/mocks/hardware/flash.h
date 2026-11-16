#ifndef TEST_HARDWARE_FLASH_H
#define TEST_HARDWARE_FLASH_H

#include <stddef.h>
#include <stdint.h>

// Used by config_store.c. tests/config_store_test.c defines the functions and
// the two variables, which place a fake 8 KB of config sectors in host memory.
#define FLASH_SECTOR_SIZE      4096u
#define FLASH_PAGE_SIZE        256u
#define PICO_FLASH_SIZE_BYTES  (2u * 1024u * 1024u)

extern uintptr_t mock_xip_base;
extern uintptr_t mock_image_end;
#define XIP_BASE                mock_xip_base
// The firmware reads the config sectors through the untranslated alias; on the
// host both are the fake flash.
#define XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE mock_xip_base
#define CFG_IMAGE_END_OFFSET()  mock_image_end

// GCC syntax in config_store.c; MSVC has no __attribute__.
#ifndef __GNUC__
#define __attribute__(x)
#endif

void flash_range_erase(uint32_t flash_offs, size_t count);
void flash_range_program(uint32_t flash_offs, const uint8_t *data, size_t count);

#endif
