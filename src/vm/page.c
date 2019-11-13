//
// Created by tykimseoul on 2019-11-05.
//

#include "../vm/frame_page.h"

void init_page_sys() {
    lock_init(&page_lock);
    list_init(&supp_page_table);
}

void add_to_supp_page_table(struct frame_table_entry *fte, void *upage) {
    struct supp_page_table_entry *spte = malloc(sizeof(struct supp_page_table_entry));
    spte->owner = thread_current();
    spte->user_vaddr = upage;
    spte->in_frame = true;
    spte->fte = fte;
    fte->spte = spte;
}

struct supp_page_table_entry *get_spte(void *upage) {
    struct thread *current = thread_current();

    struct list_elem *e;
    for (e = list_begin(&supp_page_table); e != list_end(&supp_page_table); e = list_next(e)) {
        struct supp_page_table_entry *entry = list_entry(e, struct supp_page_table_entry, page_elem);
        if (entry->user_vaddr == upage && entry->owner == current) {
            return entry;
        }
    }
    return NULL;
}

void free_page(struct supp_page_table_entry *spte) {
    if (!lock_held_by_current_thread(&page_lock)) {
        lock_acquire(&page_lock);
    }
    list_remove(&spte->page_elem);
    lock_release(&page_lock);

    void *kpage = pagedir_get_page(thread_current()->pagedir, spte->user_vaddr);
    pagedir_clear_page(thread_current()->pagedir, spte->user_vaddr);
    free_frame(kpage);
    free(spte);
}



