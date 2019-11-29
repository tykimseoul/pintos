//
// Created by 김태윤 on 2019-11-28.
//
#include <list.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>

typedef int mapid_t;

struct mmap_entry{
    mapid_t id;
    struct list_elem mmap_elem;
    struct file *file;
    void *upage;
    size_t file_size;
};