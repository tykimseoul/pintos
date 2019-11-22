//
// Created by tykimseoul on 2019-11-14.
//

#include "../userprog/pagedir.h"
#include "../vm/page.h"
#include "../threads/vaddr.h"
#include "../vm/swap.h"
#include <stdio.h>

void init_frame_sys() {
    lock_init(&frame_lock);
    list_init(&frame_table);
}

struct frame_table_entry *get_fte_by_spte(struct supp_page_table_entry *spte);

void *allocate_frame(void *upage, enum palloc_flags flags, bool writable) {
    lock_acquire(&frame_lock);
    void *frame = (void *) palloc_get_page(PAL_USER | flags);

    if (!frame) {
        //evict
        bool eviction_success = evict_frame();
        lock_acquire(&frame_lock);
        if (eviction_success) {
            frame = (void *) palloc_get_page(PAL_USER | flags);
        } else {
            PANIC("Evict failed.T-T");
        }
    }
    struct frame_table_entry *fte = add_to_frame_table(frame);
    if (!fte) {
        free(fte);
        palloc_free_page(frame);
        lock_release(&frame_lock);
        return NULL;
    }

    struct supp_page_table_entry *spte = get_spte_from_fte(fte);
    if (!spte) {
        spte = add_to_supp_page_table(fte, upage, writable);

        if (!spte) {
            free(spte);
            free(fte);
            palloc_free_page(frame);
            lock_release(&frame_lock);
            return NULL;
        }
        bool installed = install_page(upage, frame, writable);
        if (!installed) {
            free(fte);
            free(spte);
            palloc_free_page(frame);
            lock_release(&frame_lock);
            return NULL;
        }
    }
    lock_release(&frame_lock);
    return frame;
}

struct frame_table_entry *add_to_frame_table(void *frame) {
    struct frame_table_entry *fte = malloc(sizeof(struct frame_table_entry));
    if (!fte) {
        free(fte);
        palloc_free_page(frame);
        return NULL;
    }
    fte->frame = frame;
    fte->owner = thread_current();
    list_push_back(&frame_table, &fte->frame_elem);
    return fte;
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
        if (spte->in_frame && spte->fte == fte) {
            return fte;
        }

    }
    return NULL;
}

void free_frame(void *kpage) {
    ASSERT(is_kernel_vaddr(kpage));
    ASSERT(pg_ofs(kpage) == 0);

    if (!lock_held_by_current_thread(&frame_lock)) {
        lock_acquire(&frame_lock);
    }
    struct frame_table_entry *fte = get_fte_by_frame(kpage);
    if (fte->frame) {
        list_remove(&fte->frame_elem);
        palloc_free_page(kpage);
        free(fte);
    }
    lock_release(&frame_lock);
}

bool evict_frame() {
    struct frame_table_entry *victim_fte = get_frame_victim();
    struct supp_page_table_entry *victim_spte = get_spte_from_fte(victim_fte);
    ASSERT(victim_spte);

    size_t idx = swap_out_of_memory(victim_spte->user_vaddr);

    free_frame(victim_fte->frame);

    victim_spte->fte = NULL;
    victim_spte->in_frame = false;
    victim_spte->swap_slot = idx;

    return true;
}

struct frame_table_entry *get_frame_victim() {
    struct frame_table_entry *victim = list_entry(list_front(&frame_table), struct frame_table_entry, frame_elem);
    return victim;
}