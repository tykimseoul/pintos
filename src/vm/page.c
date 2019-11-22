//
// Created by tykimseoul on 2019-11-14.
//

#include "page.h"
#include "../threads/vaddr.h"
#include "../vm/swap.h"
#include "../userprog/pagedir.h"

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
        free(spte);
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

struct supp_page_table_entry *get_spte_from_fte(struct frame_table_entry *fte) {
    struct list_elem *e;
    for (e = list_begin(&supp_page_table); e != list_end(&supp_page_table); e = list_next(e)) {
        struct supp_page_table_entry *entry = list_entry(e, struct supp_page_table_entry, page_elem);
        if (entry->in_frame && entry->fte == fte) {
            return entry;
        }
    }
    return NULL;
}

bool load_page_from_swap(struct supp_page_table_entry *spte) {
    ASSERT(!spte->in_frame);
    void *new_frame = allocate_frame(spte->user_vaddr, PAL_USER, spte->writable);
    if (!new_frame) {
        return false;
    }
    swap_into_memory(spte->swap_slot, spte->user_vaddr);
    spte->in_frame = true;
    spte->fte=get_fte_by_frame(new_frame);
    spte->swap_slot=NULL;
    return true;
}

void free_page(struct supp_page_table_entry *spte) {
    if (!lock_held_by_current_thread(&page_lock)) {
        lock_acquire(&page_lock);
    }
    list_remove(&spte->page_elem);
    uint8_t *kpage = pagedir_get_page(thread_current()->pagedir, spte->user_vaddr);
    pagedir_clear_page(thread_current()->pagedir, spte->user_vaddr);
    free_frame(spte->fte->frame);
    free(spte);
    lock_release(&page_lock);
}
