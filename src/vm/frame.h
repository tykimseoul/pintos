//
// Created by tykimseoul on 2019-11-14.
//
#include <list.h>
#include "../threads/synch.h"
#include "../threads/thread.h"
#include "../threads/palloc.h"

static struct list frame_table;
struct lock frame_lock;

struct frame_table_entry {
    void *frame;
    struct thread *owner;
    struct list_elem frame_elem;
};

void init_frame_sys();

void *allocate_frame(void *upage, enum palloc_flags flags, bool in_swap);

struct frame_table_entry *add_to_frame_table(void *frame);

struct frame_table_entry *get_fte_by_frame(void *frame);

struct frame_table_entry *get_fte_by_spte(struct supp_page_table_entry *spte);

struct frame_table_entry *get_frame_victim();

void free_frame(void *kpage);

void evict_frame(struct frame_table_entry *victim);

bool reclaim_frame(struct supp_page_table_entry *entry);
