#include "../userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "../threads/interrupt.h"
#include "../threads/thread.h"
#include "../devices/shutdown.h"
#include "../devices/input.h"
#include "../threads/vaddr.h"
#include "../filesys/filesys.h"

static void syscall_handler(struct intr_frame *);

void syscall_init(void) {
    intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void syscall_handler(struct intr_frame *f UNUSED) {
    switch (*(int *) f->esp) {
        case SYS_HALT:
            halt();
            break;
        case SYS_EXIT: {
            check_address_validity(*((int *) f->esp + 1));
            exit(*((int *) f->esp + 1));
            break;
        }
        case SYS_EXEC: {
            check_address_validity(*((int *) f->esp + 1));
            f->eax = exec(*((int *) f->esp + 1));
            break;
        }
        case SYS_WAIT: {
            check_address_validity(*((int *) f->esp + 1));
            f->eax = wait(*((int *) f->esp + 1));
            break;
        }
        case SYS_CREATE: {
            check_address_validity(f->esp);
            const char *file = (const char *) *((int *) f->esp + 1);
            unsigned initial_size = (unsigned) *((int *) f->esp + 2);
            f->eax = create(file, initial_size);
            break;
        }
        case SYS_REMOVE: {
            check_address_validity(f->esp);
            const char *file = (const char *) *((int *) f->esp + 1);
            f->eax = remove(file);
            break;
        }
        case SYS_OPEN: {
            check_address_validity(f->esp);
            const char *file = (const char *) *((int *) f->esp + 1);
            f->eax = open(file);
            break;
        }
        case SYS_FILESIZE: {
            check_address_validity(f->esp);
            int fd = *((int *) f->esp + 1);
            f->eax = filesize(fd);
            break;
        }
        case SYS_READ: {
            check_address_validity(*((int *) f->esp + 1));
            check_address_validity(*((int *) f->esp + 2));
            check_address_validity(*((int *) f->esp + 3));
            int fd = *((int *) f->esp + 1);
            void *buffer = (void *) (*((int *) f->esp + 2));
            unsigned size = *((unsigned *) f->esp + 3);
            f->eax = read(fd, buffer, size);
            break;
        }
        case SYS_WRITE: {
            check_address_validity(f->esp);
            int fd = *((int *) f->esp + 1);
            void *buffer = (void *) (*((int *) f->esp + 2));
            unsigned size = *((unsigned *) f->esp + 3);
            f->eax = write(fd, buffer, size);
            break;
        }
        case SYS_SEEK: {
            check_address_validity(f->esp);
            int fd = *((int *) f->esp + 1);
            unsigned position = (unsigned) *((int *) f->esp + 2);
            seek(fd, position);
            break;
        }
        case SYS_TELL: {
            check_address_validity(f->esp);
            int fd = *((int *) f->esp + 1);
            f->eax = tell(fd);
            break;
        }
        case SYS_CLOSE: {
            check_address_validity(f->esp);
            int fd = *((int *) f->esp + 1);
            close(fd);
            break;
        }
    }
}

void halt(void) {
    shutdown_power_off();
}

void exit(int status) {
    printf("%s: exit(%d)\n", thread_name(), status);
    thread_current()->exit_status = status;
    for (int i = 2; i < FILE_MAX_COUNT; i++) {
        if (thread_current()->files[i] != NULL) {
            close(i);
        }
    }
    thread_exit();
}

pid_t exec(const char *cmd_line) {
    return process_execute(cmd_line);
}

int wait(pid_t pid) {
    return process_wait(pid);
}

bool create(const char *file, unsigned initial_size) {
    check_file_validity(file);
    return filesys_create(file, initial_size);
}

bool remove(const char *file) {
    check_file_validity(file);
    return filesys_remove(file);
}

int open(const char *file) {
    check_file_validity(file);
    struct file *open_file = filesys_open(file);
    if (open_file) {
        for (int i = 2; i < FILE_MAX_COUNT; i++) {
            if (thread_current()->files[i] == NULL) {
                thread_current()->files[i] = open_file;
                return i;
            }
        }
    }
    return -1;
}

int filesize(int fd) {
    struct file *for_size = thread_current()->files[fd];
    check_file_validity(for_size);
    return file_length(for_size);
}

int read(int fd, void *buffer, unsigned size) {
    if (fd == 0) {
        return (int) input_getc();
    } else if (fd == 1) {
        return 0;
    } else {
        struct file *reading_file = thread_current()->files[fd];
        check_file_validity(reading_file);
        return file_read(reading_file, buffer, size);
    }
}

int write(int fd, const void *buffer, unsigned size) {
    if (fd == 0) {
        return -1;
    } else if (fd == 1) {
        putbuf(buffer, size);
        return size;
    } else {
        struct file *writing_file = thread_current()->files[fd];
        check_file_validity(writing_file);
        return file_write(writing_file, buffer, size);
    }
}

void seek(int fd, unsigned position) {
    struct file *seeking_file = thread_current()->files[fd];
    check_file_validity(seeking_file);
    file_seek(seeking_file, position);
}

unsigned tell(int fd) {
    struct file *telling_file = thread_current()->files[fd];
    check_file_validity(telling_file);
    return file_tell(telling_file);
}

void close(int fd) {
    struct file *closing_file = thread_current()->files[fd];
    check_file_validity(closing_file);
    thread_current()->files[fd] = NULL;
    file_close(closing_file);
}

void check_address_validity(void *address) {
    if (!is_user_vaddr(address)) exit(-1);
}

void check_file_validity(struct file *file1) {
    if (!file1) exit(-1);
}

