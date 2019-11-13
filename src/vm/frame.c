//
// Created by tykimseoul on 2019-11-05.
//

#include "../vm/frame_page.h"
#include "../userprog/pagedir.h"

struct lock frame_lock;

void init_frame_sys() {
    lock_init(&frame_lock);
    list_init(&frame_table);
}

void *allocate_frame(void *upage, enum palloc_flags flags, bool in_swap) {
    void *frame = (void *) palloc_get_page(PAL_USER | flags);

    if (!frame) {
        //evict
        struct frame_table_entry *victim = get_frame_victim();
        evict_frame(victim);
        frame = (void *) palloc_get_page(PAL_USER | flags);
    }
    add_to_frame_table(frame);
    if (!in_swap)
        add_to_supp_page_table(frame, upage);
    return frame;
}

void add_to_frame_table(void *frame) {
    struct frame_table_entry *fte = malloc(sizeof(struct frame_table_entry));
    fte->frame = frame;
    fte->owner = thread_current();
    lock_acquire(&frame_lock);
    list_push_back(&frame_table, &fte->frame_elem);
    lock_release(&frame_lock);
}

struct frame_table_entry *get_fte_by_frame(void *frame) {
    struct list_elem *e;
    for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e)) {
        struct frame_table_entry *fte = list_entry(e, struct frame_table_entry, frame_elem);
        if (fte->frame == frame) {
            return fte;
        }
    }
    return NULL;
}

struct frame_table_entry *get_fte_by_spte(struct supp_page_table_entry *spte) {
    struct list_elem *e;
    for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e)) {
        struct frame_table_entry *fte = list_entry(e, struct frame_table_entry, frame_elem);
        if (fte->spte == spte) {
            return fte;
        }
    }
    return NULL;
}

struct frame_table_entry *get_frame_victim() {
    struct frame_table_entry *victim = list_entry(list_pop_front(&frame_table),
    struct frame_table_entry, frame_elem);
    return victim;
}

void free_frame(void *kpage) {
    ASSERT(is_kernel_vaddr(kpage));
    ASSERT(pg_ofs(kpage) == 0);

    struct supp_page_table_entry *spte = get_spte(kpage);
    struct frame_table_entry *fte = get_fte_by_spte(spte);
    if (!lock_held_by_current_thread(&frame_lock)) {
        lock_acquire(&frame_lock);
    }
    if (fte->frame) {
        list_remove(&fte->frame_elem);
        palloc_free_page(kpage);
    }
    lock_release(&frame_lock);
}

void evict_frame(struct frame_table_entry *victim) {
    //copy the contents of this frame to a swap slot and get its index
    size_t swap_slot = swap_out_of_memory(victim->frame);

    //point the supplementary page table entry to the new swap slot instead of the frame we are evicting
    struct supp_page_table_entry *victim_spte = victim->spte;
    victim_spte->in_frame = false;
    victim_spte->swap_slot = swap_slot;

    //free the frame for others to use
    pagedir_clear_page(victim->owner->pagedir, victim->spte->user_vaddr);
}

bool reclaim_frame(struct supp_page_table_entry *entry) {
    void *new_frame = allocate_frame(entry->user_vaddr, PAL_USER, true);
    if (!new_frame) {
        return false;
    }
    bool success =pagedir_set_page(thread_current()->pagedir, entry->user_vaddr, new_frame, true);
    if (!success) {
        //free page
        return false;
    }
    swap_into_memory(entry->swap_slot, new_frame);
    //redefine pointing
    entry->in_frame = true;
    struct frame_table_entry *fte = get_fte_by_frame(new_frame);
    entry->fte = fte;
    fte->spte = entry;
    return true;
}

