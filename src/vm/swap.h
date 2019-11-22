//
// Created by tykimseoul on 2019-11-19.
//
#include <bitmap.h>
#include "../threads/synch.h"
#include "../devices/block.h"
#include "../threads/vaddr.h"

#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

struct lock swap_lock;

struct block *swap_block;

struct bitmap *swap_map;

void swap_init(void);

size_t swap_out_of_memory(void *upage);

void swap_into_memory(size_t idx, void *upage);

