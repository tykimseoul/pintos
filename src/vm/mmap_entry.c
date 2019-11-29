//
// Created by 김태윤 on 2019-11-28.
//

#include "mmap_entry.h"
#include "../vm/page.h"
#include "../vm/swap.h"

static struct mmap_entry *get_mmap_entry(struct list *mmap_table, mapid_t id) {
    struct list_elem *e;
    for (e = list_begin(mmap_table); e != list_end(mmap_table); e = list_next(e)) {
        struct mmap_entry *entry = list_entry(e,
        struct mmap_entry, mmap_elem);
        if (entry->id == id) {
            return entry;
        }
    }
    return NULL;
}

static void unmap(struct thread *t, void *page, struct file *f, int ofs, size_t bytes) {
    struct list *spt = &t->spt;
    uint32_t pagedir = t->pagedir;
    struct supp_page_table_entry *spte = get_spte(spt, page);
    if (!spte) {
        PANIC("unmap failed\n");
    }

    switch (spte->status) {
        case IN_FRAME: {
            ASSERT(spte->fte->frame != NULL);
            bool dirty = spte->dirty || pagedir_is_dirty(pagedir, spte->user_vaddr) || pagedir_is_dirty(pagedir, spte->fte->frame);
            if (dirty) {
                file_write_at(f, spte->user_vaddr, bytes, ofs);
            }
            lock_acquire(&frame_free_lock);
            free_frame(spte->fte->frame);
            lock_release(&frame_free_lock);
            break;
        }
        case IN_SWAP: {
            bool dirty = spte->dirty || pagedir_is_dirty(pagedir, spte->user_vaddr);
            if (dirty) {
                void *tmp = palloc_get_page(0);
                swap_into_memory(spte->swap_slot, tmp);
                file_write_at(f, tmp, PGSIZE, ofs);
                palloc_free_page(tmp);
            } else {
                free_swap(spte->swap_slot);
            }
            break;
        }
        case FSYS: {
            break;
        }
        default:
            PANIC("unrecognized state\n");
    }
    list_remove(&spte->page_elem);
}