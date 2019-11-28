//
// Created by tykimseoul on 2019-11-14.
//

#include "../userprog/pagedir.h"
#include "../vm/page.h"
#include "../threads/vaddr.h"
#include "../vm/swap.h"
#include <stdlib.h>
#include <stdio.h>

#define DBG true

void init_frame_sys() {
    lock_init(&frame_lock);
    list_init(&frame_table);
}

struct frame_table_entry *get_fte_by_spte(struct supp_page_table_entry *spte);

void *allocate_frame(void *upage, enum palloc_flags flags, bool writable) {
    lock_acquire(&frame_lock);
    if (DBG) printf("ALLOCATE FRAME------------- at %p\n", upage);
    void *frame = (void *) palloc_get_page(flags);
    if (DBG) printf(">>>>>>>>>>>>PALLOC HERE at kpage: %p<<<<<<<<<<<<<<\n", frame);

    //null is different from 0
    if (!frame || frame == 0) {
        //evict
        if (DBG) printf("no frame, trying to evict...\n");
        bool eviction_success = evict_frame();
        lock_acquire(&frame_lock);
        if (eviction_success) {
            if (DBG) printf("evict success... getting new frame\n");
            frame = (void *) palloc_get_page(flags);
            if (DBG) printf("got new frame: %p!\n", frame);
            ASSERT(frame != NULL && frame != 0);
        } else {
            PANIC("Evict failed.T-T");
        }
    }
    if (DBG) printf("got frame, adding to frame table...\n");
    if (!frame || frame == 0) {
        printf("hi\n");
        PANIC("cannot allocate frame\n");
    }
    struct frame_table_entry *fte = add_to_frame_table(frame);
    if (!fte) {
        printf("failed to make fte for some reason\n");
        // free(fte);
        palloc_free_page(frame);
        lock_release(&frame_lock);
        return NULL;
    }
    if (DBG) printf("allocate frame: %p success\n\n", frame);
    if (lock_held_by_current_thread(&frame_lock))
        lock_release(&frame_lock);
    return frame;
}

struct frame_table_entry *add_to_frame_table(void *frame) {
    struct frame_table_entry *fte = malloc(sizeof(struct frame_table_entry));
    if (!fte) {
        printf("adding to frame table FAILED\n");
        // free(fte);
        palloc_free_page(frame);
        return NULL;
    }
    if (frame == 0 || frame == NULL) {
        PANIC("cannot add null frame to frame table\n");
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
    if (DBG) printf("no fte... :(\n");
    return NULL;
}

struct frame_table_entry *get_fte_by_spte(struct supp_page_table_entry *spte) {
    struct list_elem *e;
    for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e)) {
        struct frame_table_entry *fte = list_entry(e, struct frame_table_entry, frame_elem);
        if (spte->fte == fte) {
            return fte;
        }
    }
    return NULL;
}

bool evict_frame() {
    if (DBG) printf("\nEVICTING--------------------------------\n");
    struct frame_table_entry *victim_fte = get_frame_victim();
    if (DBG) printf("got frame victim: %p\n", victim_fte->frame);
    struct supp_page_table_entry *victim_spte = get_spte_from_fte(&victim_fte->owner->spt, victim_fte);
    ASSERT(victim_spte);

    //set the swap slot at index idx to be the data in the frame
    size_t idx = swap_out_of_memory(victim_fte->frame);
    if (DBG) printf("successfully swapped out of memory at index: %d\n", idx);

    victim_spte->status = IN_SWAP;
    victim_spte->swap_slot = idx;
    free_frame(victim_fte->frame);
    victim_spte->fte = NULL;
    if (DBG) printf("setting page status: in swap\n");

    if (DBG) printf("eviction success\n\n");
    return true;
}

void free_frame(void *kpage) {
    if (DBG) printf("FREEING FRAME>>>>>>>>>>\n");
    ASSERT(is_kernel_vaddr(kpage));
    ASSERT(pg_ofs(kpage) == 0);

    if (!lock_held_by_current_thread(&frame_lock)) {
        lock_acquire(&frame_lock);
    }
    struct frame_table_entry *fte = get_fte_by_frame(kpage);
    if (fte->frame) {
        list_remove(&fte->frame_elem);
        struct supp_page_table_entry *spte = get_spte_from_fte(&fte->owner->spt, fte);
        uint32_t *pd = fte->owner->pagedir;
        void *uaddr = spte->user_vaddr;
        palloc_free_page(kpage);
        pagedir_clear_page(pd, uaddr);
        if (DBG) printf("after freeing frame, uaddr: %p, %p\n", spte, spte->user_vaddr);
        free(fte);
    }
    lock_release(&frame_lock);
}

struct frame_table_entry *get_frame_victim() {
    struct frame_table_entry *victim = list_entry(list_front(&frame_table),
    struct frame_table_entry, frame_elem);
    if (!victim) {
        PANIC("really?\n");
    }
    return victim;
}