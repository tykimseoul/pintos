//
// Created by 김태윤 on 2019-11-12.
//

#include <list.h>
#include "../threads/synch.h"
#include "../threads/thread.h"
#include "../threads/palloc.h"
#include "../threads/vaddr.h"

static struct list supp_page_table;
static struct lock *page_lock;

struct supp_page_table_entry {
    uint32_t *user_vaddr;

    struct thread *owner;
    struct list_elem page_elem;
    bool in_frame;
    struct frame_table_entry *fte;
    size_t swap_slot;
};

void init_page_sys();

void add_to_supp_page_table(struct frame_table_entry *fte, void *upage);

struct supp_page_table_entry *get_spte(void *upage);

void free_page(struct supp_page_table_entry *spte);


static struct list frame_table;

struct frame_table_entry {
    void *frame;
    struct thread *owner;
    struct list_elem frame_elem;
    struct supp_page_table_entry *spte;
};

void init_frame_sys();

void *allocate_frame(void *upage, enum palloc_flags flags, bool in_swap);

void add_to_frame_table(void *frame);

struct frame_table_entry *get_fte_by_frame(void *frame);

struct frame_table_entry *get_fte_by_spte(struct supp_page_table_entry *spte);

struct frame_table_entry *get_frame_victim();

void free_frame(void *kpage);

void evict_frame(struct frame_table_entry *victim);

bool reclaim_frame(struct supp_page_table_entry *entry);
