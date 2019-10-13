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
    int *call_number = *(int *) f->esp;
//    printf("system call: %d\n", call_number);
//    hex_dump(f->esp, f->esp, 300, 1);

    switch (*(int *) f->esp) {
        case SYS_HALT:
            halt();
            break;
        case SYS_EXIT:
            check_valid_address(*((int *) f->esp + 1));
            exit(*((int *) f->esp + 1));
            break;
        case SYS_EXEC: {
            check_valid_address(*((int *) f->esp + 1));
            exec(*((int *) f->esp + 1));
            break;
        }
        case SYS_WAIT:
            check_valid_address(*((int *) f->esp + 1));
            wait(*((int *) f->esp + 1));
            break;
        case SYS_CREATE: {
            const char *file = (const char *) *((int *) f->esp + 1);
            unsigned initial_size = (unsigned) *((int *) f->esp + 2);
            check_valid_address(file);
            check_valid_address(initial_size);
            f->eax = create(file, initial_size);
            break;
        }
        case SYS_REMOVE:
            break;
        case SYS_OPEN:
            break;
        case SYS_FILESIZE:
            break;
        case SYS_READ: {
            check_valid_address(f->esp);
            int fd = *((int *) f->esp + 1);
            void *buffer = (void *) (*((int *) f->esp + 2));
            unsigned size = *((unsigned *) f->esp + 3);
            f->eax = read(fd, buffer, size);
            break;
        }
        case SYS_WRITE: {
            check_valid_address(f->esp);
            int fd = *((int *) f->esp + 1);
            void *buffer = (void *) (*((int *) f->esp + 2));
            unsigned size = *((unsigned *) f->esp + 3);
            f->eax = write(fd, buffer, size);
            break;
        }
        case SYS_SEEK:
            break;
        case SYS_TELL:
            break;
        case SYS_CLOSE:
            break;
    }
}

void halt(void) {
    shutdown_power_off();
}

void exit(int status) {
    printf("%s: exit(%d)\n", thread_name(), status);
    thread_current()->exit_status = status;
    thread_exit();
}

pid_t exec(const char *cmd_line) {
    return process_execute(cmd_line);
}

int wait(pid_t pid) {
    return process_wait(pid);
}

bool create(const char *file, unsigned initial_size) {
    if (file == NULL) {
        exit(-1);
    }
    return filesys_create(file, initial_size);
}

bool remove(const char *file) {}

int open(const char *file) {}

int filesize(int fd) {}

int read(int fd, void *buffer, unsigned size) {
    if (fd == 0) {
        return (int) input_getc();
    } else if (fd == 1) {
        return 0;
    }
}

int write(int fd, const void *buffer, unsigned size) {
    if (fd == 1) {
        putbuf(buffer, size);
        return size;
    }
    return -1;
}

void seek(int fd, unsigned position) {}

unsigned tell(int fd) {}

void close(int fd) {}

void check_valid_address(void *address) {
    if (!is_user_vaddr(address)) {
        exit(-1);
    }
}

