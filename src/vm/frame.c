//
// Created by tykimseoul on 2019-11-14.
//

#include "../userprog/pagedir.h"
#include "../vm/page.h"
#include <stdio.h>

void init_frame_sys() {
    lock_init(&frame_lock);
    list_init(&frame_table);
}

void *allocate_frame(void *upage, enum palloc_flags flags, bool in_swap) {
    void *frame = (void *) palloc_get_page(PAL_USER | flags);

    if (!frame) {
        //evict
        printf("eviction not yet\n");
//        struct frame_table_entry *victim = get_frame_victim();
//        evict_frame(victim);
//        frame = (void *) palloc_get_page(PAL_USER | flags);
        return NULL;
    }
    struct frame_table_entry *fte = add_to_frame_table(frame);
    if (!fte) {
        return NULL;
    }
    if (!in_swap) {
        struct supp_page_table_entry *spte = add_to_supp_page_table(fte, upage);
        if (!spte) {
            free(fte);
            return NULL;
        }
        bool installed = install_page(upage, frame, true);
        if (!installed) {
            free(fte);
            free(spte);
            return NULL;
        }
    }
    return frame;
}

struct frame_table_entry *add_to_frame_table(void *frame) {
    struct frame_table_entry *fte = malloc(sizeof(struct frame_table_entry));
    if (!fte) {
        return NULL;
    }
    fte->frame = frame;
    fte->owner = thread_current();
    lock_acquire(&frame_lock);
    list_push_back(&frame_table, &fte->frame_elem);
    lock_release(&frame_lock);
    return fte;
}

