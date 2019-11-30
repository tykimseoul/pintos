//
// Created by tykimseoul on 2019-11-14.
//

#include "page.h"
#include "../threads/vaddr.h"
#include "../vm/swap.h"
#include "../userprog/pagedir.h"

struct supp_page_table_entry *make_spte(struct list *spt, void *frame, void *upage, bool writable)
{
    ASSERT(frame != 0);
    struct frame_table_entry *fte = get_fte_by_frame(frame);
    ASSERT(fte);
    struct supp_page_table_entry *spte = get_spte_from_fte(spt, fte);
    if (!spte)
    {
        spte = add_to_supp_page_table(spt, fte, upage, writable);

        if (!spte)
        {
            printf("making spte failed.\n");
            free(spte);
            free(fte);
            palloc_free_page(frame);

            return NULL;
        }
        bool installed = install_page(upage, frame, writable);
        if (!installed)
        {
            printf("install failed\n");
            free(fte);
            free(spte);
            palloc_free_page(frame);

            return NULL;
        }
    }
    return spte;
}

struct supp_page_table_entry *make_spte_filesys(struct list *spt, void *upage, struct file *file,
                                                off_t offset, uint32_t read_bytes, uint32_t zero_bytes,
                                                bool writable, bool from_syscall)
{

    void *user_vaddr = pg_round_down(upage);

    struct supp_page_table_entry *spte = malloc(sizeof(struct supp_page_table_entry));
    if (!spte)
    {
        printf("making spte(filesys) failed\n");
        free(spte);

        return NULL;
    }

    spte->user_vaddr = user_vaddr;
    if (from_syscall)
    {
        if (spte->user_vaddr == 0 && spte->user_vaddr == NULL)
            return NULL;
    }
    else
    {
        ASSERT(spte->user_vaddr != 0 && spte->user_vaddr != NULL);
    }

    spte->status = FSYS;

    spte->owner = thread_current();
    spte->writable = writable;
    spte->fte = NULL;
    spte->swap_slot = -1;

    spte->dirty = false;
    spte->file = file;
    spte->ofs = offset;
    spte->read_bytes = read_bytes;
    spte->zero_bytes = zero_bytes;

    list_push_back(spt, &spte->page_elem);

    if (!spte)
    {
        printf("making spte(filesys) failed.\n");
        free(spte);
        return NULL;
    }

    return spte;
}

struct supp_page_table_entry *get_spte(struct list *spt, void *upage)
{
    struct list_elem *e;
    for (e = list_begin(spt); e != list_end(spt); e = list_next(e))
    {
        struct supp_page_table_entry *entry = list_entry(e,
                                                         struct supp_page_table_entry, page_elem);
        if (entry->user_vaddr == upage)
        {
            return entry;
        }
    }
    return NULL;
}

struct supp_page_table_entry *add_to_supp_page_table(struct list *spt, struct frame_table_entry *fte, void *upage, bool writable)
{
    struct supp_page_table_entry *spte = malloc(sizeof(struct supp_page_table_entry));
    if (!spte)
    {
        free(spte);
        return NULL;
    }
    spte->owner = thread_current();
    spte->user_vaddr = pg_round_down(upage);
    ASSERT(spte->user_vaddr != 0 && spte->user_vaddr != NULL);

    spte->status = IN_FRAME;
    spte->writable = writable;
    spte->fte = fte;
    spte->dirty = false;

    list_push_back(spt, &spte->page_elem);
    return spte;
}

struct supp_page_table_entry *get_spte_from_fte(struct list *spt, struct frame_table_entry *fte)
{
    struct list_elem *e;
    for (e = list_begin(spt); e != list_end(spt); e = list_next(e))
    {
        struct supp_page_table_entry *entry = list_entry(e, struct supp_page_table_entry, page_elem);
        if (entry->fte == fte)
        {
            return entry;
        }
    }
    return NULL;
}

bool load_page(struct list *spt, struct supp_page_table_entry *spte)
{
    bool success = false;
    if (!spte)
        return success;

    switch (spte->status)
    {
    case IN_FRAME:
    {
        success = true;
        break;
    }
    case IN_SWAP:
    {
        success = load_page_from_swap(spt, spte);
        break;
    }
    case FSYS:
    {
        success = load_page_from_filesys(spte);
        break;
    }
    default:
    {
        //if the page in spte is either in frame or allzero,
        //it shouldn't have faulted.
        success = false;
        break;
    }
    }
    unpin_page(spte);
    return success;
}

bool load_page_from_swap(struct list *spt, struct supp_page_table_entry *spte)
{
    ASSERT(spte->status == IN_SWAP);
    void *new_frame = allocate_frame(spte->user_vaddr, PAL_USER, spte->writable);
    if (!new_frame)
    {
        return false;
    }
    swap_into_memory(spte->swap_slot, new_frame);

    bool installed = install_page(spte->user_vaddr, new_frame, spte->writable);
    if (!installed)
    {
        printf("install failed\n");
        free(spte->fte);
        free(spte);
        palloc_free_page(new_frame);

        return NULL;
    }

    spte->status = IN_FRAME;
    spte->fte = get_fte_by_frame(new_frame);
    spte->swap_slot = -1;

    return true;
}

bool load_page_from_filesys(struct supp_page_table_entry *spte)
{
    void *upage = spte->user_vaddr;

    void *frame = allocate_frame(upage, PAL_USER | PAL_ZERO, spte->writable);
    if (!frame || frame == 0)
    {
        exit(-1);
    }

    file_seek(spte->file, spte->ofs);
    if (file_read(spte->file, frame, spte->read_bytes) != (off_t)spte->read_bytes)
    {
        lock_acquire(&frame_free_lock);
        list_remove(&get_fte_by_frame(frame)->frame_elem);
        free(frame);
        lock_release(&frame_free_lock);
        exit(-1);
    }
    memset(frame + spte->read_bytes, 0, spte->zero_bytes);

    bool installed = install_page(upage, frame, spte->writable);
    if (!installed)
    {
        free_page(spte);
        exit(-1);
    }

    spte->fte = get_fte_by_frame(frame);

    spte->status = IN_FRAME;

    spte->dirty = false;
    return true;
}

void free_page(struct supp_page_table_entry *spte)
{
    list_remove(&spte->page_elem);

    uint8_t *kpage = pagedir_get_page(spte->owner->pagedir, spte->user_vaddr);
    uint32_t *pd = spte->owner->pagedir;
    void *uaddr = spte->user_vaddr;

    lock_acquire(&frame_free_lock);
    free_frame(spte->fte->frame);
    lock_release(&frame_free_lock);
    free(spte);
    pagedir_clear_page(pd, uaddr);
}

void pin_page(struct supp_page_table_entry *spte)
{
    if (!spte)
    {
        return;
    }

    ASSERT(spte->status == IN_FRAME);
    pin_frame(spte->fte->frame);
}

void unpin_page(struct supp_page_table_entry *spte)
{
    if (spte == NULL)
    {
        PANIC("Page to unpin does not exist.");
    }

    if (spte->status == IN_FRAME)
    {
        unpin_frame(spte->fte->frame);
    }
}