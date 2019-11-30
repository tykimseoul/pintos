//
// Created by tykimseoul on 2019-11-19.
//

#include "swap.h"

#define DBG false

void swap_init()
{
    swap_block = block_get_role(BLOCK_SWAP);
    ASSERT(swap_block);
    swap_map = bitmap_create(block_size(swap_block) / SECTORS_PER_PAGE);
    ASSERT(swap_map);
    lock_init(&swap_lock);
}

size_t swap_out_of_memory(void *kpage)
{
    if (DBG)
        printf("swapping out of memory\n");
    ASSERT(swap_block && swap_map);
    lock_acquire(&swap_lock);
    size_t swap_slot = bitmap_scan_and_flip(swap_map, 0, 1, false);
    ASSERT(swap_slot != BITMAP_ERROR);
    ASSERT(bitmap_test(swap_map, swap_slot) == 1);
    for (size_t i = 0; i < SECTORS_PER_PAGE; i++)
    {
        block_write(swap_block, swap_slot * SECTORS_PER_PAGE + i, (uint8_t *)kpage + i * BLOCK_SECTOR_SIZE);
    }
    lock_release(&swap_lock);
    return swap_slot;
}

void swap_into_memory(size_t idx, void *kpage)
{
    if (DBG)
        printf("swapping into memory\n");
    ASSERT(swap_block && swap_map);
    ASSERT(bitmap_test(swap_map, idx) != 0);
    lock_acquire(&swap_lock);
    bitmap_reset(swap_map, idx);
    for (size_t i = 0; i < SECTORS_PER_PAGE; i++)
    {
        block_read(swap_block, idx * SECTORS_PER_PAGE + i, (uint8_t *)kpage + i * BLOCK_SECTOR_SIZE);
    }
    lock_release(&swap_lock);
}

void free_swap(size_t idx)
{
    ASSERT(swap_block && swap_map);
    ASSERT(bitmap_test(swap_map, idx) != 0);
    lock_acquire(&swap_lock);
    bitmap_reset(swap_map, idx);
    lock_release(&swap_lock);
}
