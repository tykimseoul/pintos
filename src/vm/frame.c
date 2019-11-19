//
// Created by tykimseoul on 2019-11-14.
//

#include "../userprog/pagedir.h"
#include "../vm/page.h"
#include "../threads/vaddr.h"
#include <stdio.h>

void init_frame_sys() {
    lock_init(&frame_lock);
    list_init(&frame_table);
}
struct frame_table_entry *get_fte_by_spte(struct supp_page_table_entry *spte);

void *allocate_frame(void *upage, enum palloc_flags flags, bool in_swap) {
    lock_acquire(&frame_lock);
    void *frame = (void *) palloc_get_page(PAL_USER | flags);

    if (!frame) {
        //evict
        printf("eviction not yet\n");
//        struct frame_table_entry *victim = get_frame_victim();
//        evict_frame(victim);
//        frame = (void *) palloc_get_page(PAL_USER | flags);
        lock_release(&frame_lock);
        return NULL;
    }
    struct frame_table_entry *fte = add_to_frame_table(frame);
    if (!fte) {
        lock_release(&frame_lock);
        return NULL;
    }
    if (!in_swap) {
        struct supp_page_table_entry *spte = add_to_supp_page_table(fte, upage);
        if (!spte) {
            free(fte);
            lock_release(&frame_lock);
            return NULL;
        }
        bool installed = install_page(upage, frame, true);
        if (!installed) {
            free(fte);
            free(spte);
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
        return NULL;
    }
    fte->frame = frame;
    fte->owner = thread_current();
    list_push_back(&frame_table, &fte->frame_elem);
    return fte;
}

struct frame_table_entry *get_fte_by_frame(void *frame){
    struct list_elem *e;
    for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e)){
        struct frame_table_entry *fte = list_entry(e, struct frame_table_entry, frame_elem);
        if (fte->frame == frame){
            return fte;
        }
    }
    return NULL;
}

struct frame_table_entry *get_fte_by_spte(struct supp_page_table_entry *spte){
    struct list_elem *e;
    for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e)){
        struct frame_table_entry *fte = list_entry(e, struct frame_table_entry, frame_elem);
        if (spte->fte == fte){
            return fte;
        }
    }
    return NULL;
}

void free_frame(void *kpage){
    ASSERT(is_kernel_vaddr(kpage));
    ASSERT(pg_ofs(kpage) == 0);

    if (!lock_held_by_current_thread(&frame_lock)){
        lock_acquire(&frame_lock);
    }
    struct supp_page_table_entry *spte = get_spte(kpage);
    struct frame_table_entry *fte = get_fte_by_spte(spte);
    if (fte->frame){
        list_remove(&fte->frame_elem);
        palloc_free_page(kpage);
        free(fte);
    }
    lock_release(&frame_lock);
}
