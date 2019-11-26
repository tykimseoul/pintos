#include "../userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "../threads/interrupt.h"
#include "../threads/thread.h"
#include "../devices/shutdown.h"
#include "../devices/input.h"
#include "../threads/vaddr.h"
#include "../filesys/filesys.h"
#include "../threads/palloc.h"
#include "../vm/page.h"

#define USER_LOWER_BOUND 0x08048000

static void syscall_handler(struct intr_frame *);

static void can_i_read(void *uaddr, unsigned size);

static void can_i_write(void *uaddr, unsigned size);

struct lock file_lock;

// static int get_user(const uint8_t *uaddr);

// static bool put_user(uint8_t *udst, uint8_t byte);

// static void check_user(const uint8_t *uaddr);

void syscall_init(void)
{
    lock_init(&file_lock);
    intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void syscall_handler(struct intr_frame *f UNUSED)
{
#ifdef VM
    thread_current()->esp = (f->esp);
#endif
    switch (*(int *)f->esp)
    {
    case SYS_HALT:
        halt();
        break;
    case SYS_EXIT:
    {
        check_address_validity(*((int *)f->esp + 1));

        exit(*((int *)f->esp + 1));
        break;
    }
    case SYS_EXEC:
    {
        check_address_validity(*((int *)f->esp + 1));

        f->eax = exec(*((int *)f->esp + 1));
        break;
    }
    case SYS_WAIT:
    {
        check_address_validity(*((int *)f->esp + 1));

        f->eax = wait(*((int *)f->esp + 1));
        break;
    }
    case SYS_CREATE:
    {
        check_address_validity(f->esp);

        const char *file = (const char *)*((int *)f->esp + 1);
        unsigned initial_size = (unsigned)*((int *)f->esp + 2);

        lock_acquire(&file_lock);
        f->eax = create(file, initial_size);
        lock_release(&file_lock);
        break;
    }
    case SYS_REMOVE:
    {
        check_address_validity(f->esp);

        const char *file = (const char *)*((int *)f->esp + 1);

        f->eax = remove(file);
        break;
    }
    case SYS_OPEN:
    {
        check_address_validity(f->esp);

        const char *file = (const char *)*((int *)f->esp + 1);

        lock_acquire(&file_lock);
        // printf("opening file...\n");
        f->eax = open(file);
        lock_release(&file_lock);
        break;
    }
    case SYS_FILESIZE:
    {
        check_address_validity(f->esp);

        int fd = *((int *)f->esp + 1);

        f->eax = filesize(fd);
        break;
    }
    case SYS_READ:
    {
        check_address_validity(f->esp);
        check_address_validity(*((int *)f->esp + 1));
        check_address_validity(*((int *)f->esp + 2));
        check_address_validity(*((int *)f->esp + 3));

        int fd = *((int *)f->esp + 1);
        void *buffer = (void *)(*((int *)f->esp + 2));
        unsigned size = *((unsigned *)f->esp + 3);

        lock_acquire(&file_lock);
        // printf("reading file...\n");
        f->eax = read(fd, buffer, size);
        lock_release(&file_lock);
        break;
    }
    case SYS_WRITE:
    {
        check_address_validity(f->esp);

        int fd = *((int *)f->esp + 1);
        void *buffer = (void *)(*((int *)f->esp + 2));
        unsigned size = *((unsigned *)f->esp + 3);

        lock_acquire(&file_lock);
        f->eax = write(fd, buffer, size);
        lock_release(&file_lock);
        break;
    }
    case SYS_SEEK:
    {
        check_address_validity(f->esp);

        int fd = *((int *)f->esp + 1);

        unsigned position = (unsigned)*((int *)f->esp + 2);
        seek(fd, position);
        break;
    }
    case SYS_TELL:
    {
        check_address_validity(f->esp);

        int fd = *((int *)f->esp + 1);

        f->eax = tell(fd);
        break;
    }
    case SYS_CLOSE:
    {
        check_address_validity(f->esp);

        int fd = *((int *)f->esp + 1);

        close(fd);
        break;
    }
    }
}

void halt(void)
{
    shutdown_power_off();
}

void exit(int status)
{
    printf("%s: exit(%d)\n", thread_name(), status);
    thread_current()->exit_status = status;
    if (lock_held_by_current_thread(&file_lock))
    {
        lock_release(&file_lock);
    }
    for (int i = 2; i < FILE_MAX_COUNT; i++)
    {
        if (thread_current()->files[i] != NULL)
        {
            close(i);
        }
    }
    thread_exit();
}

pid_t exec(const char *cmd_line)
{
    // check_user((const uint8_t *)cmd_line);
    return process_execute(cmd_line);
}

int wait(pid_t pid)
{
    return process_wait(pid);
}

bool create(const char *file, unsigned initial_size)
{
    // check_user((const uint8_t *)file);
    check_file_validity(file);
    return filesys_create(file, initial_size);
}

bool remove(const char *file)
{
    // check_user((const uint8_t *)file);
    check_file_validity(file);
    return filesys_remove(file);
}

int open(const char *file)
{
    // check_file_validity(file);
    // struct file *open_file = filesys_open(file);
    // if (open_file)
    // {
    //     if (strcmp(thread_current()->name, file) == 0)
    //     {
    //         file_deny_write(open_file);
    //     }
    //     for (int i = 2; i < FILE_MAX_COUNT; i++)
    //     {
    //         if (thread_current()->files[i] == NULL)
    //         {
    //             thread_current()->files[i] = open_file;
    //             return i;
    //         }
    //     }
    // }
    // return -1;

    if (file == NULL)
    {
        return -1;
    }

    printf("file is: %s\n", file);
    struct file *opened_file = filesys_open(file);
    struct thread *t = thread_current();
    int fd;

    printf("opened file: %p\n", opened_file);
    check_file_validity(opened_file);

    for (fd = 2; fd < FILE_MAX_COUNT; fd++)
    {
        if (t->files[fd] == NULL)
            break;
    }
    if (fd == FILE_MAX_COUNT)
    {

        return -1;
    }
    else
    {
        if (strcmp(thread_current()->name, file) == 0)
        {
            file_deny_write(opened_file);
        }
        t->files[fd] = opened_file;
        // printf("file %p set at thread: %p\n", opened_file, t);
        return fd;
    }
}

int filesize(int fd)
{
    if (fd < 2)
    {
        return 0;
    }

    struct file *for_size = thread_current()->files[fd];
    check_file_validity(for_size);
    return file_length(for_size);
}

int read(int fd, void *buffer, unsigned size)
{

    if (fd >= FILE_MAX_COUNT || fd < 0)
    {
        return 0;
    }
    can_i_read(buffer, size);
    printf("yes i can\n\n");
    struct file *file;
    struct thread *t = thread_current();
    unsigned read_cnt = 0;

    if (fd == 0)
    {
        while (read_cnt <= size)
        {
            // read key by input_getc() and write it into buffer at appropriate position
            *(char *)(buffer + read_cnt++) = input_getc();
        }
        return read_cnt;
    }
    printf("reading from thread: %p\n", t);
    // get file from fd
    file = t->files[fd];

    if (file == NULL)
    {
        // lock_release(&file_lock);
        return 0;
    }
    printf("in read4\n\n");
    read_cnt = file_read(file, buffer, size);
    // lock_release(&file_lock);
    return (int)read_cnt;

    /*
    // check_user((const uint8_t *)buffer);
    // check_user((const uint8_t *)buffer + size - 1);
    int count = 0;
    void *buffer_rd = pg_round_down(buffer);
    void *buffer_page;
    struct thread *t = thread_current();

    unsigned readsize = (unsigned)(buffer_rd + PGSIZE - buffer);
    unsigned bytes_read = 0;
    bool read = true;

    can_i_read(buffer, size);

    for (buffer_page = buffer_rd; buffer_page <= buffer + size; buffer_page += PGSIZE)
    {
        struct supp_page_table_entry *spte = get_spte(&t->spt, buffer_page);
        if (!spte)
        {
            if (!is_user_vaddr(buffer_page) || buffer_page == 0xbfffe000)
            {
                exit(-1);
            }
            // printf("making a new frame in syscall...");
            void *new_frame = allocate_frame(buffer_page, PAL_USER | PAL_ZERO, true);
            make_spte(&t->spt, new_frame, buffer_page, true);
            count++;
        }
    }
    if (fd == 0)
    {
        while (count < size)
        {
            *((uint8_t *)(buffer + count)) = input_getc();
            count++;
        }
        // while (count < size)
        // {
        //     if (!put_user(*((uint8_t *)(buffer + count)), input_getc()))
        //     {
        //         // printf("in read5\n");
        //         if (lock_held_by_current_thread(&file_lock))
        //             lock_release(&file_lock);
        //         exit(-1);
        //     }
        //     count++;
        // }
        return size;
        
}
else if (fd == 1)
{
    return 0;
}
else if (size <= readsize)
{
    struct file *reading_file = thread_current()->files[fd];
    check_file_validity(reading_file);
    return file_read(reading_file, buffer, size);
}
else
{
    int result = 0;
    while (read)
    {
        struct file *reading_file = thread_current()->files[fd];
        check_file_validity(reading_file);
        bytes_read = file_read(reading_file, buffer, readsize);

        //couldn't read all the bytes
        if (bytes_read != readsize)
        {
            read = false;
        }

        size -= bytes_read;

        if (size == 0)
        {
            read = false;
        }
        else
        {
            buffer += bytes_read;
            if (size >= PGSIZE)
                readsize = PGSIZE;
            else
                readsize = size;
        }

        result += bytes_read;
    }
    return result;
}
*/
}

int write(int fd, const void *buffer, unsigned size)
{
    // check_user((const uint8_t *)buffer);
    // check_user((const uint8_t *)buffer + size - 1);
    can_i_write(buffer, size);

    if (fd >= FILE_MAX_COUNT || fd < 0)
    {
        return 0;
    }
    else if (fd == 1)
    {
        putbuf(buffer, size);
        return size;
    }
    else
    {
        struct file *writing_file = thread_current()->files[fd];
        check_file_validity(writing_file);
        printf("writing to file: %p\n\n", writing_file);
        return file_write(writing_file, buffer, size);
    }
}

void seek(int fd, unsigned position)
{
    struct file *seeking_file = thread_current()->files[fd];
    check_file_validity(seeking_file);
    file_seek(seeking_file, position);
}

unsigned tell(int fd)
{
    struct file *telling_file = thread_current()->files[fd];
    check_file_validity(telling_file);
    return file_tell(telling_file);
}

void close(int fd)
{
    struct file *closing_file = thread_current()->files[fd];
    check_file_validity(closing_file);
    file_close(closing_file);
    thread_current()->files[fd] = NULL;
}

void check_address_validity(void *address)
{
    // if (!(is_user_vaddr(address) && address > (void *)USER_LOWER_BOUND))
    if (!(is_user_vaddr(address)))
        exit(-1);
}

void check_file_validity(struct file *file1)
{
    if (!file1)
        exit(-1);
}

static void can_i_read(void *uaddr, unsigned size)
{
    void *ptr;
    struct thread *current = thread_current();
    for (ptr = pg_round_down(uaddr); ptr < uaddr + size; ptr += PGSIZE)
    {
        if (ptr == NULL || !is_user_vaddr(ptr) || ptr <= 0x08048000)
        {
            exit(-1);
        }
    }
}

static void can_i_write(void *uaddr, unsigned size)
{
    void *ptr;
    for (ptr = pg_round_down(uaddr); ptr < uaddr + size; ptr += PGSIZE)
    {
        if (ptr == NULL || !is_user_vaddr(ptr) || pagedir_get_page(thread_current()->pagedir, ptr) == NULL)
        {
            exit(-1);
        }
        struct supp_page_table_entry *spte = get_spte(&thread_current()->spt, ptr);
        if (!spte || spte->writable == false)
        {
            exit(-1);
        }
    }
}

// /* Reads a byte at user virtual address UADDR.
//    UADDR must be below PHYS_BASE.
//    Returns the byte value if successful, -1 if a segfault
//    occurred. */
// static int get_user(const uint8_t *uaddr)
// {
//     if (!((void *)uaddr < PHYS_BASE))
//     {
//         return -1;
//     }
//     int result;
//     asm("movl $1f, %0; movzbl %1, %0; 1:"
//         : "=&a"(result)
//         : "m"(*uaddr));
//     return result;
// }

// /* Writes BYTE to user address UDST.
//    UDST must be below PHYS_BASE.
//    Returns true if successful, false if a segfault occurred. */
// static bool
// put_user(uint8_t *udst, uint8_t byte)
// {
//     if (!((void *)udst < PHYS_BASE))
//     {
//         return false;
//     }
//     int error_code;
//     asm("movl $1f, %0; movb %b2, %1; 1:"
//         : "=&a"(error_code), "=m"(*udst)
//         : "q"(byte));
//     return error_code != -1;
// }

// static void check_user(const uint8_t *uaddr)
// {
//     if (get_user(uaddr) == -1)
//     {
//         if (lock_held_by_current_thread(&file_lock))
//         {
//             lock_release(&file_lock);
//         }
//         exit(-1);
//     }
// }
