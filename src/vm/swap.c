//
// Created by tykimseoul on 2019-11-05.
//

#include "../vm/swap.h"

void swap_init() {
    swap_block = block_get_role(BLOCK_SWAP);
    ASSERT(swap_block);
    swap_map = bitmap_create(block_size(swap_block) / SECTORS_PER_PAGE);
    ASSERT(swap_map);
    lock_init(&swap_lock);
}

size_t swap_out_of_memory(void *frame) {
    ASSERT(swap_block && swap_map);
    lock_acquire(&swap_lock);
    size_t swap_slot = bitmap_scan_and_flip(swap_map, 0, 1, false);
    ASSERT(swap_slot != BITMAP_ERROR);
    for (size_t i = 0; i < SECTORS_PER_PAGE; i++) {
        block_write(swap_block, swap_slot * SECTORS_PER_PAGE + i, (uint8_t *) frame + i * BLOCK_SECTOR_SIZE);
    }
    lock_release(&swap_lock);
    return swap_slot;
}

void swap_into_memory(size_t idx, void *frame) {
    ASSERT(swap_block && swap_map);
    lock_acquire(&swap_lock);
    ASSERT(bitmap_test(swap_map, idx) != 0);
    bitmap_reset(swap_map, idx);
    for (size_t i = 0; i < SECTORS_PER_PAGE; i++) {
        block_write(swap_block, idx * SECTORS_PER_PAGE + i, (uint8_t *) frame + i * BLOCK_SECTOR_SIZE);
    }
    lock_release(&swap_lock);
}
