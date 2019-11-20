//
// Created by tykimseoul on 2019-11-14.
//

#include "page.h"
#include "../threads/vaddr.h"

void init_page_sys() {
    lock_init(&page_lock);
    list_init(&supp_page_table);
}

struct supp_page_table_entry *get_spte(void *upage) {
    struct list_elem *e;
    for (e = list_begin(&supp_page_table); e != list_end(&supp_page_table); e = list_next(e)) {
        struct supp_page_table_entry *entry = list_entry(e, struct supp_page_table_entry, page_elem);
        if (entry->user_vaddr == upage) {
            return entry;
        }
    }
    return NULL;
}

struct supp_page_table_entry *add_to_supp_page_table(struct frame_table_entry *fte, void *upage, bool writable) {
    struct supp_page_table_entry *spte = malloc(sizeof(struct supp_page_table_entry));
    if (!spte) {
        return NULL;
    }
    spte->owner = thread_current();
    spte->user_vaddr = pg_round_down(upage);
    spte->in_frame = true;
    spte->writable = writable;
    spte->fte = fte;
    lock_acquire(&page_lock);
    list_push_back(&supp_page_table, &spte->page_elem);
    lock_release(&page_lock);
    return spte;
}