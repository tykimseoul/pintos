//
// Created by tykimseoul on 2019-11-14.
//

#include "page.h"
#include "../threads/vaddr.h"
#include "../vm/swap.h"
#include "../userprog/pagedir.h"

#define DBG false

void init_page_sys()
{
    // lock_init(&page_lock);
}

struct supp_page_table_entry *make_spte(struct list *spt, void *frame, void *upage, bool writable)
{
    if (DBG)
        printf("MAKING NEW SPTE------------- (at upage: %p)\n", upage);
    ASSERT(frame != 0);
    struct frame_table_entry *fte = get_fte_by_frame(frame);
    ASSERT(fte);
    struct supp_page_table_entry *spte = get_spte_from_fte(spt, fte);
    if (!spte)
    {
        if (DBG)
            printf("no spte exists, making new one\n");
        spte = add_to_supp_page_table(spt, fte, upage, writable);

        if (!spte)
        {
            printf("making spte failed.\n");
            free(spte);
            free(fte);
            palloc_free_page(frame);

            return NULL;
        }
        if (DBG)
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
    if (DBG)
        printf("got spte and installation success\n\n");
    return spte;
}

struct supp_page_table_entry *make_spte_filesys(struct list *spt, void *upage, struct file *file,
                                                off_t offset, uint32_t read_bytes, uint32_t zero_bytes,
                                                bool writable, bool from_syscall)
{
    // if (DBG)
    //     printf("MAKING NEW SPTE FILESYS-------------\n");

    void *user_vaddr = pg_round_down(upage);

    struct supp_page_table_entry *spte = malloc(sizeof(struct supp_page_table_entry));
    if (!spte)
    {
        printf("making spte(filesys) failed\n");
        free(spte);

        return NULL;
    }

    spte->user_vaddr = user_vaddr;
    if (from_syscall) {
        if (spte->user_vaddr == 0 && spte->user_vaddr == NULL) return NULL;
    } else {
        ASSERT(spte->user_vaddr != 0 && spte->user_vaddr != NULL);
    }
    if (DBG)
        printf("making spte(filesys) with uvaddr: %p\n", spte->user_vaddr);

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

    // if (DBG) printf("file: %p\n", spte->file);
    // if (DBG) printf("offs: %d\n", spte->ofs);
    // if (DBG) printf("read: %d\n", spte->read_bytes);
    // if (DBG) printf("zero: %d\n\n", spte->zero_bytes);

    // if (DBG) printf("inserting spte: %p\n      into spt: %p [%d]\n\n", spte, spt, list_size(spt));
    list_push_back(spt, &spte->page_elem);
    // if (DBG) printf("inserting spte: %p\n      into spt: %p [%d]\n\n", spte, spt, list_size(spt));

    if (!spte)
    {
        printf("making spte(filesys) failed.\n");
        free(spte);
        return NULL;
    }

    // if (DBG)
    //     printf("making spte filesys success\n\n");
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
        if (DBG)
            printf("not enough memory to make spte\n");
        free(spte);
        return NULL;
    }
    spte->owner = thread_current();
    spte->user_vaddr = pg_round_down(upage);
    ASSERT(spte->user_vaddr != 0 && spte->user_vaddr != NULL);
    // if (DBG) printf("creating spte at virtual address: %p\n\n", spte->user_vaddr);
    // if (DBG) printf("setting page status: in frame (in add to spte)\n");
    if (DBG)
        printf("putting in frame: %p\n         in spte: %p\n", fte->frame, spte);
    spte->status = IN_FRAME;
    spte->writable = writable;
    spte->fte = fte;
    spte->dirty = false;

    // if (DBG) printf("inserting spte: %p\n      into spt: %p [%d]\n\n", spte, spt, list_size(spt));
    list_push_back(spt, &spte->page_elem);
    // if (DBG) printf("inserting spte: %p\n      into spt: %p [%d]\n\n", spte, spt, list_size(spt));
    return spte;
}

struct supp_page_table_entry *get_spte_from_fte(struct list *spt, struct frame_table_entry *fte)
{
    struct list_elem *e;
    // if (DBG) printf("looping in table: %p [%d]\n", spt, list_size(spt));
    for (e = list_begin(spt); e != list_end(spt); e = list_next(e))
    {
        struct supp_page_table_entry *entry = list_entry(e,
                                                         struct supp_page_table_entry, page_elem);
        // if (DBG) printf("looping spte %p\n", entry);
        if (entry->fte == fte)
        {
            return entry;
        }
    }
    return NULL;
}

bool load_page_from_swap(struct list *spt, struct supp_page_table_entry *spte)
{
    ASSERT(spte->status == IN_SWAP);
    if (DBG)
        printf("LOADING FROM SWAP------------\n");
    void *new_frame = allocate_frame(spte->user_vaddr, PAL_USER, spte->writable);
    if (!new_frame)
    {
        if (DBG)
            printf("give me a frame bitch\n");
        return false;
    }
    if (DBG)
        printf("trying swap\n");
    swap_into_memory(spte->swap_slot, new_frame);
    if (DBG)
        printf("swap SUCCESS\n");

    if (DBG)
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

    if (DBG)
        printf("setting page status: in frame (in load from swap)\n");
    if (DBG)
        printf("putting in frame: %p\n         in spte: %p\n", new_frame, spte);
    spte->status = IN_FRAME;
    spte->fte = get_fte_by_frame(new_frame);
    spte->swap_slot = -1;

    return true;
}

void free_page(struct supp_page_table_entry *spte)
{
    if (DBG)
        printf("FREEING PAGE>>>>>>>>\n");
    list_remove(&spte->page_elem);
    uint8_t *kpage = pagedir_get_page(spte->owner->pagedir, spte->user_vaddr);
    uint32_t *pd = spte->owner->pagedir;
    void *uaddr = spte->user_vaddr;
    if (DBG)
        printf("trying to acquire free lock\n");
    lock_acquire(&frame_free_lock);
    free_frame(spte->fte->frame);
    lock_release(&frame_free_lock);
    free(spte);
    pagedir_clear_page(pd, uaddr);
}