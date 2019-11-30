//
// Created by tykimseoul on 2019-11-14.
//

#include "../userprog/pagedir.h"
#include "../vm/page.h"
#include "../threads/vaddr.h"
#include "../vm/swap.h"
#include <stdlib.h>
#include <stdio.h>

void init_frame_sys()
{
    lock_init(&frame_alloc_lock);
    lock_init(&frame_free_lock);
    list_init(&frame_table);
}

struct frame_table_entry *get_fte_by_spte(struct supp_page_table_entry *spte);

void *allocate_frame(void *upage, enum palloc_flags flags, bool writable)
{
    lock_acquire(&frame_alloc_lock);
    void *frame = (void *)palloc_get_page(flags);

    //null is different from 0
    if (!frame || frame == 0)
    {
        //evict
        bool eviction_success = evict_frame();
        if (eviction_success)
        {
            frame = (void *)palloc_get_page(flags);
            ASSERT(frame != NULL && frame != 0);
        }
        else
        {
            lock_release(&frame_alloc_lock);
            PANIC("Evict failed.T-T");
        }
    }
    if (!frame || frame == 0)
    {
        lock_release(&frame_alloc_lock);
        PANIC("cannot allocate frame\n");
    }
    struct frame_table_entry *fte = add_to_frame_table(frame);
    if (!fte)
    {
        printf("failed to make fte for some reason\n");
        palloc_free_page(frame);
        lock_release(&frame_alloc_lock);
        return NULL;
    }
    lock_release(&frame_alloc_lock);
    return frame;
}

struct frame_table_entry *add_to_frame_table(void *frame)
{
    struct frame_table_entry *fte = malloc(sizeof(struct frame_table_entry));
    if (!fte)
    {
        printf("adding to frame table FAILED\n");
        palloc_free_page(frame);
        return NULL;
    }
    if (frame == 0 || frame == NULL)
    {
        PANIC("cannot add null frame to frame table\n");
    }
    fte->frame = frame;
    fte->pinned = true;
    fte->owner = thread_current();
    list_push_back(&frame_table, &fte->frame_elem);
    return fte;
}

struct frame_table_entry *get_fte_by_frame(void *frame)
{
    struct list_elem *e;
    for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e))
    {
        struct frame_table_entry *fte = list_entry(e, struct frame_table_entry, frame_elem);
        if (fte->frame == frame)
        {
            return fte;
        }
    }
    return NULL;
}

struct frame_table_entry *get_fte_by_spte(struct supp_page_table_entry *spte)
{
    struct list_elem *e;
    for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e))
    {
        struct frame_table_entry *fte = list_entry(e, struct frame_table_entry, frame_elem);
        if (spte->fte == fte)
        {
            return fte;
        }
    }
    return NULL;
}

bool evict_frame()
{
    struct frame_table_entry *victim_fte = get_frame_victim();
    struct supp_page_table_entry *victim_spte = get_spte_from_fte(&victim_fte->owner->spt, victim_fte);
    ASSERT(victim_spte);

    //set the swap slot at index idx to be the data in the frame
    size_t idx = swap_out_of_memory(victim_fte->frame);

    victim_spte->status = IN_SWAP;
    victim_spte->swap_slot = idx;

    lock_acquire(&frame_free_lock);
    free_frame(victim_fte->frame);
    lock_release(&frame_free_lock);
    victim_spte->fte = NULL;

    return true;
}

void free_frame(void *kpage)
{
    ASSERT(lock_held_by_current_thread(&frame_free_lock));
    ASSERT(is_kernel_vaddr(kpage));
    ASSERT(pg_ofs(kpage) == 0);

    struct frame_table_entry *fte = get_fte_by_frame(kpage);
    if (fte->frame)
    {
        list_remove(&fte->frame_elem);
        struct supp_page_table_entry *spte = get_spte_from_fte(&fte->owner->spt, fte);
        if (!spte)
        {
            PANIC("a frame you're freeing does not have a corresponding spte\n");
        }
        uint32_t *pd = fte->owner->pagedir;
        void *uaddr = spte->user_vaddr;
        pagedir_clear_page(pd, uaddr);
        palloc_free_page(kpage);
        free(fte);
    }
}

struct frame_table_entry *get_frame_victim()
{
    struct list_elem *e;
    for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e))
    {
        struct frame_table_entry *victim = list_entry(e, struct frame_table_entry, frame_elem);
        if (!victim->pinned)
        {
            return victim;
        }
    }
    return NULL;
}

void set_frame_pin(void *kpage, bool pin)
{
    lock_acquire(&frame_alloc_lock);

    struct frame_table_entry *target = get_fte_by_frame(kpage);
    if (!target)
    {
        PANIC("Frame to pin/unpin does not exist!\n");
        return;
    }
    target->pinned = pin;

    lock_release(&frame_alloc_lock);
}

void pin_frame(void *kpage)
{
    set_frame_pin(kpage, true);
}

void unpin_frame(void *kpage)
{
    set_frame_pin(kpage, false);
}