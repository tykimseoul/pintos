//
// Created by tykimseoul on 2019-11-14.
//

#include <list.h>
#include "../threads/synch.h"
#include "../threads/thread.h"
#include "../vm/frame.h"

static struct list supp_page_table;
static struct lock page_lock;

struct supp_page_table_entry {
    uint32_t *user_vaddr;

    struct thread *owner;
    struct list_elem page_elem;
    bool in_frame;
    struct frame_table_entry *fte;
    size_t swap_slot;
};

void init_page_sys();

struct supp_page_table_entry *add_to_supp_page_table(struct frame_table_entry *fte, void *upage);

struct supp_page_table_entry *get_spte(void *upage);

void free_page(struct supp_page_table_entry *spte);
