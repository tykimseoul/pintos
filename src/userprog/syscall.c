#include "../userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "../threads/interrupt.h"
#include "../threads/thread.h"
#include "../devices/shutdown.h"

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
            exit(0);
            break;
        case SYS_EXEC:
            break;
        case SYS_WAIT:
            break;
        case SYS_CREATE:
            break;
        case SYS_REMOVE:
            break;
        case SYS_OPEN:
            break;
        case SYS_FILESIZE:
            break;
        case SYS_READ:
            break;
        case SYS_WRITE:
            ;
            int fd = *((int*)f->esp + 1);
            void* buffer = (void*)(*((int*)f->esp + 2));
            unsigned size = *((unsigned*)f->esp + 3);
            f->eax = write(fd, buffer, size);
            break;
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
    thread_exit();
}

pid_t exec(const char *cmd_lime) {}

int wait(pid_t pid) {}

bool create(const char *file, unsigned initial_size) {}

bool remove(const char *file) {}

int open(const char *file) {}

int filesize(int fd) {}

int read(int fd, void *buffer, unsigned size) {}

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

