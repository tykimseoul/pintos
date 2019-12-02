//
// Created by tykimseoul on 2019-12-02.
//

#include "devices/block.h"

void init_buffer_cache(void);

void flush_buffer_cache(void);

void read_buffer_cache(block_sector_t sector, void *target);

void write_buffer_cache(block_sector_t sector, void *source);
