//
// Created by tykimseoul on 2019-12-02.
//

#include <debug.h>
#include <string.h>
#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "threads/synch.h"

#define BUFFER_CACHE_SIZE 64

struct buffer_cache_entry {
    bool occupied;  // true only if this entry is valid cache entry

    block_sector_t disk_sector;
    uint8_t buffer[BLOCK_SECTOR_SIZE];

    bool dirty;     // dirty bit
};

static struct buffer_cache_entry cache[BUFFER_CACHE_SIZE];

static struct lock cache_lock;

static struct buffer_cache_entry *evict_buffer_cache(void);

static struct buffer_cache_entry *get_cache_victim();

static struct buffer_cache_entry *get_bce(block_sector_t sector);

static struct buffer_cache_entry *allocate_buffer_cache();


void init_buffer_cache(void) {
    lock_init(&cache_lock);

    for (int i = 0; i < BUFFER_CACHE_SIZE; ++i) {
        cache[i].occupied = false;
    }
}

static void write_to_disk(struct buffer_cache_entry *bce) {
    ASSERT(lock_held_by_current_thread(&cache_lock));
    ASSERT(bce != NULL && bce->occupied);

    if (bce->dirty) {
        block_write(fs_device, bce->disk_sector, bce->buffer);
        bce->dirty = false;
    }
}

void flush_buffer_cache(void) {
    lock_acquire(&cache_lock);

    for (int i = 0; i < BUFFER_CACHE_SIZE; i++) {
        if (cache[i].occupied) {
            write_to_disk(&(cache[i]));
        }
    }

    lock_release(&cache_lock);
}

void read_buffer_cache(block_sector_t sector, void *target) {
    lock_acquire(&cache_lock);
    struct buffer_cache_entry *bce = get_bce(sector);
    if (!bce) {
        bce = allocate_buffer_cache();
        ASSERT(bce != NULL && bce->occupied == false);

        bce->occupied = true;
        bce->disk_sector = sector;
        bce->dirty = false;
        block_read(fs_device, sector, bce->buffer);
    }

    // copy from entry to target
    memcpy(target, bce->buffer, BLOCK_SECTOR_SIZE);

    lock_release(&cache_lock);
}

void write_buffer_cache(block_sector_t sector, void *source) {
    lock_acquire(&cache_lock);

    struct buffer_cache_entry *bce = get_bce(sector);
    if (!bce) {
        bce = allocate_buffer_cache();
        ASSERT(bce != NULL && bce->occupied == false);

        bce->occupied = true;
        bce->disk_sector = sector;
        bce->dirty = false;
        block_read(fs_device, sector, bce->buffer);
    }

    bce->dirty = true;
    memcpy(bce->buffer, source, BLOCK_SECTOR_SIZE);

    lock_release(&cache_lock);
}

static struct buffer_cache_entry *get_bce(block_sector_t sector) {
    for (int i = 0; i < BUFFER_CACHE_SIZE; ++i) {
        if (cache[i].occupied && cache[i].disk_sector == sector) {
            // cache hit.
            return &(cache[i]);
        }
    }
    return NULL;
}

static struct buffer_cache_entry *evict_buffer_cache(void) {
    ASSERT(lock_held_by_current_thread(&cache_lock));

    struct buffer_cache_entry *slot = get_cache_victim();
    if (slot->dirty) {
        // write back into disk
        write_to_disk(slot);
    }

    slot->occupied = false;
    return slot;
}

static struct buffer_cache_entry *allocate_buffer_cache(){
    for (int i = 0; i < BUFFER_CACHE_SIZE; i++) {
        if (!cache[i].occupied) {
            return &(cache[i]);
        }
    }
    return evict_buffer_cache();
}

static struct buffer_cache_entry *get_cache_victim() {
    for (int i = 0; i < BUFFER_CACHE_SIZE; i++) {
        if (cache[i].occupied) {
            return &(cache[i]);
        }
    }
    return NULL;
}