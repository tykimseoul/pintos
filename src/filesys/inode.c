#include "../filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "../filesys/cache.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define DIRECT_BLOCKS_COUNT 123
#define INDIRECT_BLOCKS_PER_SECTOR 128
#define MAX_DEPTH 2

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk {
    block_sector_t direct_blocks[DIRECT_BLOCKS_COUNT];
    block_sector_t indirect_block;
    block_sector_t doubly_indirect_block;
    InodeType type;
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
};

static inline int min(int a, int b) {
    return a < b ? a : b;
}

static bool allocate_inode(struct inode_disk *inode, off_t file_length);

static bool free_inode(struct inode_disk *inode);

static block_sector_t singly_indirect_inode(block_sector_t indirect, int idx) {
    idx -= DIRECT_BLOCKS_COUNT;
    ASSERT(0 <= idx && idx < INDIRECT_BLOCKS_PER_SECTOR);

    block_sector_t buffer[INDIRECT_BLOCKS_PER_SECTOR];
    read_buffer_cache(indirect, buffer);
    return buffer[idx];
}

static block_sector_t doubly_indirect_inode(block_sector_t doubly_indirect, int idx) {
    idx -= (DIRECT_BLOCKS_COUNT + INDIRECT_BLOCKS_PER_SECTOR);
    ASSERT(0 <= idx && idx < INDIRECT_BLOCKS_PER_SECTOR * INDIRECT_BLOCKS_PER_SECTOR);

    block_sector_t buffer[INDIRECT_BLOCKS_PER_SECTOR];
    read_buffer_cache(doubly_indirect, buffer);
    return singly_indirect_inode(buffer[idx / 128], idx % 128);
}

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t bytes_to_sectors(off_t size) {
    return DIV_ROUND_UP(size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
};

bool is_directory(const struct inode *inode) {
    return inode->data.type == DIRECTORY;
}

bool is_removed(const struct inode *inode){
    return inode->removed;
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t byte_to_sector(const struct inode *inode, off_t pos) {
    ASSERT(inode != NULL);
    if (pos < inode->data.length) {
        int idx = pos / BLOCK_SECTOR_SIZE;
        if (idx < DIRECT_BLOCKS_COUNT) {
            return inode->data.direct_blocks[idx];
        } else if (idx < DIRECT_BLOCKS_COUNT + INDIRECT_BLOCKS_PER_SECTOR) {
            return singly_indirect_inode(inode->data.indirect_block, idx);
        } else {
            return doubly_indirect_inode(inode->data.doubly_indirect_block, idx);
        }
    } else {
        return -1;
    }
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void inode_init(void) {
    list_init(&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool inode_create(block_sector_t sector, off_t length, InodeType type) {
    struct inode_disk *disk_inode = NULL;
    bool success = false;

    ASSERT(length >= 0);

    /* If this assertion fails, the inode structure is not exactly
       one sector in size, and you should fix that. */
    ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE);

    disk_inode = calloc(1, sizeof *disk_inode);
    if (disk_inode != NULL) {
        size_t sectors = bytes_to_sectors(length);
        disk_inode->type = type;
        disk_inode->length = length;
        disk_inode->magic = INODE_MAGIC;
        if (allocate_inode(disk_inode, disk_inode->length)) {
            write_buffer_cache(sector, disk_inode);
            success = true;
        }
        free(disk_inode);
    }
    return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *inode_open(block_sector_t sector) {
    struct list_elem *e;
    struct inode *inode;

    /* Check whether this inode is already open. */
    for (e = list_begin(&open_inodes); e != list_end(&open_inodes);
         e = list_next(e)) {
        inode = list_entry(e,
        struct inode, elem);
        if (inode->sector == sector) {
            inode_reopen(inode);
            return inode;
        }
    }

    /* Allocate memory. */
    inode = malloc(sizeof *inode);
    if (inode == NULL)
        return NULL;

    /* Initialize. */
    list_push_front(&open_inodes, &inode->elem);
    inode->sector = sector;
    inode->open_cnt = 1;
    inode->deny_write_cnt = 0;
    inode->removed = false;
    read_buffer_cache(inode->sector, &inode->data);
    return inode;
}

/* Reopens and returns INODE. */
struct inode *inode_reopen(struct inode *inode) {
    if (inode != NULL)
        inode->open_cnt++;
    return inode;
}

/* Returns INODE's inode number. */
block_sector_t inode_get_inumber(const struct inode *inode) {
    return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void inode_close(struct inode *inode) {
    /* Ignore null pointer. */
    if (inode == NULL)
        return;

    /* Release resources if this was the last opener. */
    if (--inode->open_cnt == 0) {
        /* Remove from inode list and release lock. */
        list_remove(&inode->elem);

        /* Deallocate blocks if removed. */
        if (inode->removed) {
            free_map_release(inode->sector, 1);
            free_inode(&inode->data);
        }

        free(inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void inode_remove(struct inode *inode) {
    ASSERT(inode != NULL);
    inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t inode_read_at(struct inode *inode, void *buffer_, off_t size, off_t offset) {
    uint8_t *buffer = buffer_;
    off_t bytes_read = 0;
    uint8_t *bounce = NULL;

    while (size > 0) {
        /* Disk sector to read, starting byte offset within sector. */
        block_sector_t sector_idx = byte_to_sector(inode, offset);
        int sector_ofs = offset % BLOCK_SECTOR_SIZE;

        /* Bytes left in inode, bytes left in sector, lesser of the two. */
        off_t inode_left = inode_length(inode) - offset;
        int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
        int min_left = inode_left < sector_left ? inode_left : sector_left;

        /* Number of bytes to actually copy out of this sector. */
        int chunk_size = size < min_left ? size : min_left;
        if (chunk_size <= 0)
            break;

        if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
            /* Read full sector directly into caller's buffer. */
            read_buffer_cache(sector_idx, buffer + bytes_read);
        } else {
            /* Read sector into bounce buffer, then partially copy
               into caller's buffer. */
            if (bounce == NULL) {
                bounce = malloc(BLOCK_SECTOR_SIZE);
                if (bounce == NULL)
                    break;
            }
            read_buffer_cache(sector_idx, bounce);
            memcpy(buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }

        /* Advance. */
        size -= chunk_size;
        offset += chunk_size;
        bytes_read += chunk_size;
    }
    free(bounce);

    return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t inode_write_at(struct inode *inode, const void *buffer_, off_t size, off_t offset) {
    const uint8_t *buffer = buffer_;
    off_t bytes_written = 0;
    uint8_t *bounce = NULL;

    if (inode->deny_write_cnt)
        return 0;
    if (byte_to_sector(inode, offset + size - 1) == -1u) {
        //extend file system
        bool success = allocate_inode(&inode->data, offset + size);
        if (!success) return 0;

        inode->data.length = offset + size;
        write_buffer_cache(inode->sector, &inode->data);
    }

    while (size > 0) {
        /* Sector to write, starting byte offset within sector. */
        block_sector_t sector_idx = byte_to_sector(inode, offset);
        int sector_ofs = offset % BLOCK_SECTOR_SIZE;

        /* Bytes left in inode, bytes left in sector, lesser of the two. */
        off_t inode_left = inode_length(inode) - offset;
        int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
        int min_left = inode_left < sector_left ? inode_left : sector_left;

        /* Number of bytes to actually write into this sector. */
        int chunk_size = size < min_left ? size : min_left;
        if (chunk_size <= 0)
            break;

        if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
            /* Write full sector directly to disk. */
            write_buffer_cache(sector_idx, buffer + bytes_written);
        } else {
            /* We need a bounce buffer. */
            if (bounce == NULL) {
                bounce = malloc(BLOCK_SECTOR_SIZE);
                if (bounce == NULL)
                    break;
            }

            /* If the sector contains data before or after the chunk
               we're writing, then we need to read in the sector
               first.  Otherwise we start with a sector of all zeros. */
            if (sector_ofs > 0 || chunk_size < sector_left)
                read_buffer_cache(sector_idx, bounce);
            else
                memset(bounce, 0, BLOCK_SECTOR_SIZE);
            memcpy(bounce + sector_ofs, buffer + bytes_written, chunk_size);
            write_buffer_cache(sector_idx, bounce);
        }

        /* Advance. */
        size -= chunk_size;
        offset += chunk_size;
        bytes_written += chunk_size;
    }
    free(bounce);

    return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void inode_deny_write(struct inode *inode) {
    inode->deny_write_cnt++;
    ASSERT(inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void inode_allow_write(struct inode *inode) {
    ASSERT(inode->deny_write_cnt > 0);
    ASSERT(inode->deny_write_cnt <= inode->open_cnt);
    inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t inode_length(const struct inode *inode) {
    return inode->data.length;
}

static void allocate_indirect_inode(block_sector_t *sector, int num_sectors, int depth) {
    ASSERT(depth <= MAX_DEPTH);

    static char empty_sector[BLOCK_SECTOR_SIZE];

    if (depth == 0) {
        if (*sector == 0) {
            bool success = free_map_allocate(1, sector);
            if(!success){
                PANIC("free map allocate failed 1\n");
            }
            write_buffer_cache(*sector, empty_sector);
        }
        return;
    }

    if (*sector == 0) {
        bool success = free_map_allocate(1, sector);
        if(!success){
            PANIC("free map allocate failed 2\n");
        }
        write_buffer_cache(*sector, empty_sector);
    }

    block_sector_t indirect_block[INDIRECT_BLOCKS_PER_SECTOR];
    read_buffer_cache(*sector, indirect_block);

//    write_buffer_cache(indirect_block, empty_sector);

    int size_unit = depth == 1 ? 1 : INDIRECT_BLOCKS_PER_SECTOR;
    int length = DIV_ROUND_UP(num_sectors, size_unit);

    for (int i = 0; i < length; i++) {
        int size = min(num_sectors, size_unit);
        allocate_indirect_inode(&indirect_block[i], size, depth - 1);
        num_sectors -= size;
    }

    ASSERT(num_sectors == 0);
    write_buffer_cache(*sector, &indirect_block);
}

static bool allocate_inode(struct inode_disk *inode, off_t file_length) {
    static char empty_sector[BLOCK_SECTOR_SIZE];

    if (file_length < 0) return false;

    int sectors = bytes_to_sectors(file_length);
    int length = min(sectors, DIRECT_BLOCKS_COUNT);

    for (int i = 0; i < length; i++) {
        if (inode->direct_blocks[i] == 0) {
            bool success = free_map_allocate(1, &inode->direct_blocks[i]);
            if(!success){
                PANIC("free map allocate failed 3\n");
            }
            write_buffer_cache(inode->direct_blocks[i], empty_sector);
        }
    }
    sectors -= length;
    if (sectors == 0) return true;

    length = min(sectors, INDIRECT_BLOCKS_PER_SECTOR);
    allocate_indirect_inode(&inode->indirect_block, length, 1);
    sectors -= length;
    if (sectors == 0) return true;

    length = min(sectors, INDIRECT_BLOCKS_PER_SECTOR * INDIRECT_BLOCKS_PER_SECTOR);
    allocate_indirect_inode(&inode->doubly_indirect_block, length, 2);
    sectors -= length;
    if (sectors == 0) return true;

    ASSERT(sectors == 0);
    return false;
}

static void free_indirect_inode(block_sector_t sector, int num_sectors, int depth) {
    ASSERT(depth <= MAX_DEPTH);

    if (depth == 0) {
        free_map_release(sector, 1);
        return;
    }

    block_sector_t indirect_block[INDIRECT_BLOCKS_PER_SECTOR];
    read_buffer_cache(sector, indirect_block);

    int size_unit = depth == 1 ? 1 : INDIRECT_BLOCKS_PER_SECTOR;
    int length = DIV_ROUND_UP(num_sectors, size_unit);

    for (int i = 0; i < length; i++) {
        int size = min(num_sectors, size_unit);
        free_indirect_inode(indirect_block[i], size, depth - 1);
        num_sectors -= size;
    }

    ASSERT(num_sectors == 0);
    if (sector != 0) {
        free_map_release(sector, 1);
    }
}

static bool free_inode(struct inode_disk *inode) {
    int file_length = inode->length;
    if (file_length < 0) return false;

    int sectors = bytes_to_sectors(file_length);
    int length = min(sectors, DIRECT_BLOCKS_COUNT);

    for (int i = 0; i < length; i++) {
        free_map_release(inode->direct_blocks[i], 1);
    }
    sectors -= length;

    length = min(sectors, INDIRECT_BLOCKS_PER_SECTOR);
    if (length > 0) {
        free_indirect_inode(inode->indirect_block, length, 1);
        sectors -= length;
    }

    length = min(sectors, INDIRECT_BLOCKS_PER_SECTOR * INDIRECT_BLOCKS_PER_SECTOR);
    if (length > 0) {
        free_indirect_inode(inode->doubly_indirect_block, length, 2);
        sectors -= length;
    }

    ASSERT(sectors == 0);
    return true;
}
