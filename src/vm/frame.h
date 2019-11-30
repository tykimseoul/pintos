//
// Created by tykimseoul on 2019-11-14.
//
#include <list.h>
#include "../threads/synch.h"
#include "../threads/thread.h"
#include "../threads/palloc.h"

static struct list frame_table;
struct lock frame_alloc_lock;
struct lock frame_free_lock;

struct frame_table_entry
{
    void *frame;
    struct thread *owner;
    struct list_elem frame_elem;
    bool pinned;
};

void init_frame_sys();

void *allocate_frame(void *upage, enum palloc_flags flags, bool writable);

struct frame_table_entry *add_to_frame_table(void *frame);

struct frame_table_entry *get_fte_by_frame(void *frame);

struct frame_table_entry *get_frame_victim();

void free_frame(void *kpage);

bool evict_frame();

bool reclaim_frame(struct supp_page_table_entry *entry);

void set_frame_pin(void *kpage, bool pin);

void pin_frame(void *kpage);

void unpin_frame(void *kpage);
