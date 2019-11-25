//
// Created by tykimseoul on 2019-11-14.
//

#include "page.h"
#include "../threads/vaddr.h"
#include "../vm/swap.h"
#include "../userprog/pagedir.h"

void init_page_sys()
{
    lock_init(&page_lock);
    list_init(&supp_page_table);
}

struct supp_page_table_entry *make_spte(void *frame, void *upage, bool writable)
{
    printf("MAKING NEW SPTE-------------\n");

    struct frame_table_entry *fte = get_fte_by_frame(frame);
    ASSERT(fte);
    struct supp_page_table_entry *spte = get_spte_from_fte(fte);
    if (!spte)
    {
        printf("no spte exists, making new one\n");

        spte = add_to_supp_page_table(fte, upage, writable);

        if (!spte)
        {
            printf("making spte failed.\n");
            free(spte);
            free(fte);
            palloc_free_page(frame);

            return NULL;
        }
        printf("installing page\n");
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
    printf("got spte and installation success\n\n");
    return spte;
}

struct supp_page_table_entry *make_spte_filesys(void *upage, struct file *file,
                                                off_t offset, uint32_t read_bytes, uint32_t zero_bytes,
                                                bool writable)
{
    printf("MAKING NEW SPTE FILESYS-------------\n");
    lock_acquire(&page_lock);
    struct supp_page_table_entry *spte = malloc(sizeof(struct supp_page_table_entry));
    lock_release(&page_lock);
    if (!spte)
    {
        free(spte);

        return NULL;
    }

    spte->user_vaddr = pg_round_down(upage);
    printf("setting page status: fsys\n");
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

    printf("file: %p\n", spte->file);
    printf("offs: %d\n", spte->ofs);
    printf("read: %d\n", spte->read_bytes);
    printf("zero: %d\n\n", spte->zero_bytes);

    lock_acquire(&page_lock);
    list_push_back(&supp_page_table, &spte->page_elem);
    lock_release(&page_lock);

    if (!spte)
    {
        printf("making spte failed.\n");
        free(spte);
        return NULL;
    }

    printf("making spte filesys success\n\n");
    return spte;
}

struct supp_page_table_entry *get_spte(void *upage)
{
    struct list_elem *e;
    for (e = list_begin(&supp_page_table); e != list_end(&supp_page_table); e = list_next(e))
    {
        struct supp_page_table_entry *entry = list_entry(e, struct supp_page_table_entry, page_elem);
        if (entry->user_vaddr == upage)
        {
            return entry;
        }
    }
    return NULL;
}

struct supp_page_table_entry *add_to_supp_page_table(struct frame_table_entry *fte, void *upage, bool writable)
{
    struct supp_page_table_entry *spte = malloc(sizeof(struct supp_page_table_entry));
    if (!spte)
    {
        free(spte);
        return NULL;
    }
    spte->owner = thread_current();
    spte->user_vaddr = pg_round_down(upage);
    printf("setting page status: in frame (in add to spte)\n");
    spte->status = IN_FRAME;
    spte->writable = writable;
    spte->fte = fte;
    spte->dirty = false;

    lock_acquire(&page_lock);
    list_push_back(&supp_page_table, &spte->page_elem);
    lock_release(&page_lock);
    return spte;
}

struct supp_page_table_entry *get_spte_from_fte(struct frame_table_entry *fte)
{
    struct list_elem *e;
    for (e = list_begin(&supp_page_table); e != list_end(&supp_page_table); e = list_next(e))
    {
        struct supp_page_table_entry *entry = list_entry(e, struct supp_page_table_entry, page_elem);
        if (entry->fte == fte)
        {
            return entry;
        }
    }
    return NULL;
}

bool load_page_from_swap(struct supp_page_table_entry *spte)
{
    ASSERT(spte->status == IN_SWAP);
    printf("LOADING FROM SWAP------------\n");
    void *new_frame = allocate_frame(spte->user_vaddr, PAL_USER, spte->writable);
    if (!new_frame)
    {
        return false;
    }
    swap_into_memory(spte->swap_slot, spte->user_vaddr);
    printf("setting page status: in frame (in load from swap)\n");
    spte->status = IN_FRAME;
    spte->fte = get_fte_by_frame(new_frame);
    spte->swap_slot = -1;

    printf("installing page\n");
    bool installed = install_page(spte->user_vaddr, new_frame, spte->writable);
    if (!installed)
    {
        printf("install failed\n");
        free(spte->fte);
        free(spte);
        palloc_free_page(new_frame);

        return NULL;
    }

    return true;
}

void free_page(struct supp_page_table_entry *spte)
{
    if (!lock_held_by_current_thread(&page_lock))
    {
        lock_acquire(&page_lock);
    }
    list_remove(&spte->page_elem);
    uint8_t *kpage = pagedir_get_page(thread_current()->pagedir, spte->user_vaddr);
    pagedir_clear_page(thread_current()->pagedir, spte->user_vaddr);
    free_frame(spte->fte->frame);
    free(spte);
    lock_release(&page_lock);
}