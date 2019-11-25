#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "../threads/vaddr.h"
#include "../vm/page.h"

#include "../filesys/file.h"

/* Number of page faults processed. */
static long long page_fault_cnt;

static void kill(struct intr_frame *);

static void page_fault(struct intr_frame *);

/* Registers handlers for interrupts that can be caused by user
   programs.

   In a real Unix-like OS, most of these interrupts would be
   passed along to the user process in the form of signals, as
   described in [SV-386] 3-24 and 3-25, but we don't implement
   signals.  Instead, we'll make them simply kill the user
   process.

   Page faults are an exception.  Here they are treated the same
   way as other exceptions, but this will need to change to
   implement virtual memory.

   Refer to [IA32-v3a] section 5.15 "Exception and Interrupt
   Reference" for a description of each of these exceptions. */
void exception_init(void)
{
    /* These exceptions can be raised explicitly by a user program,
       e.g. via the INT, INT3, INTO, and BOUND instructions.  Thus,
       we set DPL==3, meaning that user programs are allowed to
       invoke them via these instructions. */
    intr_register_int(3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
    intr_register_int(4, 3, INTR_ON, kill, "#OF Overflow Exception");
    intr_register_int(5, 3, INTR_ON, kill,
                      "#BR BOUND Range Exceeded Exception");

    /* These exceptions have DPL==0, preventing user processes from
       invoking them via the INT instruction.  They can still be
       caused indirectly, e.g. #DE can be caused by dividing by
       0.  */
    intr_register_int(0, 0, INTR_ON, kill, "#DE Divide Error");
    intr_register_int(1, 0, INTR_ON, kill, "#DB Debug Exception");
    intr_register_int(6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
    intr_register_int(7, 0, INTR_ON, kill,
                      "#NM Device Not Available Exception");
    intr_register_int(11, 0, INTR_ON, kill, "#NP Segment Not Present");
    intr_register_int(12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
    intr_register_int(13, 0, INTR_ON, kill, "#GP General Protection Exception");
    intr_register_int(16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
    intr_register_int(19, 0, INTR_ON, kill,
                      "#XF SIMD Floating-Point Exception");

    /* Most exceptions can be handled with interrupts turned on.
       We need to disable interrupts for page faults because the
       fault address is stored in CR2 and needs to be preserved. */
    intr_register_int(14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* Prints exception statistics. */
void exception_print_stats(void)
{
    printf("Exception: %lld page faults\n", page_fault_cnt);
}

/* Handler for an exception (probably) caused by a user process. */
static void kill(struct intr_frame *f)
{
    /* This interrupt is one (probably) caused by a user process.
       For example, the process might have tried to access unmapped
       virtual memory (a page fault).  For now, we simply kill the
       user process.  Later, we'll want to handle page faults in
       the kernel.  Real Unix-like operating systems pass most
       exceptions back to the process via signals, but we don't
       implement them. */

    /* The interrupt frame's code segment value tells us where the
       exception originated. */
    switch (f->cs)
    {
    case SEL_UCSEG:
        /* User's code segment, so it's a user exception, as we
               expected.  Kill the user process.  */
        printf("%s: dying due to interrupt %#04x (%s).\n",
               thread_name(), f->vec_no, intr_name(f->vec_no));
        intr_dump_frame(f);
        thread_exit();

    case SEL_KCSEG:
        /* Kernel's code segment, which indicates a kernel bug.
               Kernel code shouldn't throw exceptions.  (Page faults
               may cause kernel exceptions--but they shouldn't arrive
               here.)  Panic the kernel to make the point.  */
        intr_dump_frame(f);
        PANIC("Kernel bug - unexpected interrupt in kernel");

    default:
        /* Some other code segment?  Shouldn't happen.  Panic the
               kernel. */
        printf("Interrupt %#04x (%s) in unknown segment %04x\n",
               f->vec_no, intr_name(f->vec_no), f->cs);
        thread_exit();
    }
}

/* Page fault handler.  This is a skeleton that must be filled in
   to implement virtual memory.  Some solutions to project 2 may
   also require modifying this code.

   At entry, the address that faulted is in CR2 (Control Register
   2) and information about the fault, formatted as described in
   the PF_* macros in exception.h, is in F's error_code member.  The
   example code here shows how to parse that information.  You
   can find more information about both of these in the
   description of "Interrupt 14--Page Fault Exception (#PF)" in
   [IA32-v3a] section 5.15 "Exception and Interrupt Reference". */
static void page_fault(struct intr_frame *f)
{
    bool not_present; /* True: not-present page, false: writing r/o page. */
    bool write;       /* True: access was write, false: access was read. */
    bool user;        /* True: access by user, false: access by kernel. */
    bool in_user_stack;
    bool contiguous;
    bool small_enough;
    void *fault_addr; /* Fault address. */

    /* Obtain faulting address, the virtual address that was
       accessed to cause the fault.  It may point to code or to
       data.  It is not necessarily the address of the instruction
       that caused the fault (that's f->eip).
       See [IA32-v2a] "MOV--Move to/from Control Registers" and
       [IA32-v3a] 5.15 "Interrupt 14--Page Fault Exception
       (#PF)". */
    asm("movl %%cr2, %0"
        : "=r"(fault_addr));

    /* Turn interrupts back on (they were only off so that we could
       be assured of reading CR2 before it changed). */
    intr_enable();

    /* Count page faults. */
    page_fault_cnt++;

    /* Determine cause. */
    not_present = (f->error_code & PF_P) == 0;
    write = (f->error_code & PF_W) != 0;
    user = (f->error_code & PF_U) != 0;

    // section 4.3.3
    void *esp = user ? f->esp : thread_current()->esp;

    //    contiguous = (uint32_t *) fault_addr >= (uint32_t * )(esp) - 32;
    contiguous = (esp <= fault_addr || fault_addr == f->esp - 4 || fault_addr == f->esp - 32);
    void *upage = pg_round_down(fault_addr);
    small_enough = PHYS_BASE - upage <= 1 << 23;
    //    small_enough = (PHYS_BASE - 0x800000 <= fault_addr && fault_addr < PHYS_BASE);
    in_user_stack = fault_addr > 0x08048000 && fault_addr < PHYS_BASE;

    bool load_success = false;

    printf("\nPAGE FAULT at %p: %s error %s page in %s context.\n",
           fault_addr,
           not_present ? "not present" : "rights violation",
           write ? "writing" : "reading",
           user ? "user" : "kernel");

    if (not_present && is_user_vaddr(fault_addr) && in_user_stack)
    {
        //valid user address
        struct supp_page_table_entry *spte = get_spte(upage);
        if (spte)
        {
            //entry exists, so load from somewhere
            printf("got spte\n");
            switch (spte->status)
            {
            case FSYS:
            {
                //the data required is in the filesys and is being accessed for the first time. Load it.
                printf("loading from filesys... spte: %p\n", upage);
                void *frame = allocate_frame(upage, PAL_USER | PAL_ZERO, spte->writable);
                printf("frame allocated: %p\n", frame);
                if (!frame)
                {
                    printf("allocation failed\n");
                    exit(-1);
                }

                printf("reading file: %p\n", spte->file);
                printf("seeking file to position: %d\n", (int)spte->ofs);
                file_seek(spte->file, spte->ofs);

                // read bytes from the file
                off_t no_read = file_read(spte->file, frame, (off_t)spte->read_bytes);
                printf("n_read: %d , read_bytes: %d\n", no_read, (int)spte->read_bytes);
                if (no_read != spte->read_bytes)
                {
                    free_frame(frame);
                    printf("failed to read file\n");
                    exit(-1);
                }

                // remain bytes are just zero
                ASSERT(spte->read_bytes + spte->zero_bytes == PGSIZE);
                memset(frame + no_read, 0, spte->zero_bytes);

                spte->status = IN_FRAME;
                printf("setting page status: in frame %d (in exception)\n", spte->status);

                spte->fte = get_fte_by_frame(frame);
                printf("putting in fte: %p\n        in spte: %p\n", spte->fte, spte);

                spte->dirty = false;

                printf("installing page\n");
                bool installed = install_page(upage, frame, spte->writable);
                if (!installed)
                {
                    printf("install failed\n");
                    free_page(spte);
                    exit(-1);
                }

                load_success = true;
                break;
            }
            case IN_SWAP:
            {
                //the frame is in swap, so reclaim
                printf("reclaiming...\n");
                load_success = load_page_from_swap(spte);
                break;
            }
            default:
            {
                //this should not happen
                printf("unrecognized page status: ");
                switch (spte->status)
                {
                case IN_FRAME:
                    printf("in frame %d\n", spte->status);
                    break;

                case IN_SWAP:
                    printf("in swap\n");
                    break;

                case FSYS:
                    printf("lazy\n");
                    break;

                case ALLZERO:
                    printf("all zero\n");
                    break;

                default:
                    printf("status garbage\n");
                }
                printf("retarded page: %p\n", upage);
                printf("retarded spte: %p\n", spte);
                printf("retarded fte: %p\n", spte->fte);
                printf("retarded frame: %p\n", spte->fte->frame);
                if (!spte->fte)
                {
                    printf("null fte\n");
                }
            }
            }
            if (load_success)
            {
                // printf("page load from swap success\n");
                return;
            }
            else
            {
                load_success = true;
                // printf("mmap not yet implemented\n");
            }
        }
        else
        {
            printf("no spte\n");
            printf("%d %d\n", contiguous, small_enough);
            if (contiguous && small_enough)
            {
                //grow the stack
                printf("growing stack...\n");
                void *frame = allocate_frame(upage, PAL_USER | PAL_ZERO, true);
                struct supp_page_table_entry *spte = make_spte(frame, upage, true);
                if (frame)
                {
                    load_success = true;
                    printf("stack growth success\n");
                }
                //            } else {
                //                exit(-1);
            }
        }
    }

    if (!load_success)
    {
        //        printf("%d %d %p\n", user, is_kernel_vaddr(fault_addr), fault_addr);
        //        if (!user) { // kernel mode
        //            printf("hihi\n");
        //            f->eip = (void *) f->eax;
        //            f->eax = 0xffffffff;
        //            return;
        //        }
        if (!user || is_kernel_vaddr(fault_addr) || !fault_addr)
        {
            // printf("exit 1\n");
            exit(-1);
        }
        if (user && !write && not_present)
        {
            // printf("exit2\n");
            exit(-1);
        }
        if (user && write && !not_present)
        {
            // printf("exit3\n");
            exit(-1);
        }
        printf("Page fault at %p: %s error %s page in %s context.\n",
               fault_addr,
               not_present ? "not present" : "rights violation",
               write ? "writing" : "reading",
               user ? "user" : "kernel");
        kill(f);
    }
}
