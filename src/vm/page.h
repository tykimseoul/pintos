//
// Created by tykimseoul on 2019-11-14.
//

#include <list.h>
#include "../threads/synch.h"
#include "../threads/thread.h"
#include "../vm/frame.h"
#include "../filesys/off_t.h"
#include "../filesys/file.h"

static struct list supp_page_table;
static struct lock page_lock;

enum page_status
{
    IN_FRAME,
    IN_SWAP,
    FSYS,
    ALLZERO
};

struct supp_page_table_entry
{
    uint32_t *user_vaddr;
    enum page_status status;

    struct thread *owner;
    struct list_elem page_elem;
    bool writable;
    struct frame_table_entry *fte;
    size_t swap_slot;

    bool dirty;
    struct file *file;
    off_t ofs;
    uint32_t read_bytes;
    uint32_t zero_bytes;
};

void init_page_sys();

struct supp_page_table_entry *make_spte(void *frame, void *upage, bool writable);

struct supp_page_table_entry *make_spte_filesys(void *page, struct file *file,
                                                off_t offset, uint32_t read_bytes, uint32_t zero_bytes,
                                                bool writable);

struct supp_page_table_entry *add_to_supp_page_table(struct frame_table_entry *fte, void *upage, bool writable);

struct supp_page_table_entry *get_spte(void *upage);

struct supp_page_table_entry *get_spte_from_fte(struct frame_table_entry *fte);

bool load_page_from_swap(struct supp_page_table_entry *spte);

void free_page(struct supp_page_table_entry *spte);
