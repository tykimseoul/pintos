#include "../filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "../threads/thread.h"

/* A directory. */
struct dir {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
};

/* A single directory entry. */
struct dir_entry {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
};

bool parse_pathname(char *path, char *directory, char *filename) {
    int len = strlen(path) + 1;
    if (len <= 1) {
        return false;
    }
    char *dir_copy = (char *) calloc(1, len);
    strlcpy(dir_copy, path, len);
    char *last_slash = strrchr(dir_copy, (int) '/');

    if (!last_slash) {
        if (len > NAME_MAX + 1) {
            free(dir_copy);
            return false;
        }
        *directory = '\0';
        strlcpy(filename, path, len);
    } else {
        *last_slash = '\0';
        int dir_len = strlen(dir_copy) + 1;
        *last_slash = '/';

        if (len - dir_len > NAME_MAX + 1) {
            free(dir_copy);
            return false;
        }
        strlcpy(directory, dir_copy, dir_len + 1);
        strlcpy(filename, last_slash + 1, len - dir_len);
    }
    free(dir_copy);
    return true;
}

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool dir_create(block_sector_t sector, size_t entry_cnt) {
    bool success = inode_create(sector, entry_cnt * sizeof(struct dir_entry), DIRECTORY);
    if (!success) {
        return success;
    }

    struct dir *dir = dir_open(inode_open(sector));
    ASSERT(dir);
    struct dir_entry entry;
    entry.inode_sector = sector;
    if (inode_write_at(dir->inode, &entry, sizeof entry, 0) != sizeof entry) {
        success = false;
    }
    dir_close(dir);
    return success;
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *dir_open(struct inode *inode) {
    struct dir *dir = calloc(1, sizeof *dir);
    if (inode != NULL && dir != NULL) {
        dir->inode = inode;
        dir->pos = sizeof(struct dir_entry); //leave space for parent directory
        return dir;
    } else {
        inode_close(inode);
        free(dir);
        return NULL;
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *dir_open_root(void) {
    return dir_open(inode_open(ROOT_DIR_SECTOR));
}

struct dir *dir_open_path(const char *path) {
    int length = strlen(path);
    char dir_copy[length + 1];
    strlcpy(dir_copy, path, length + 1);

    struct dir *current_dir;
    if (path[0] == '/') {
        current_dir = dir_open_root();
    } else {
        struct thread *current_thread = thread_current();
        if (!current_thread->cwd) {
            current_dir = dir_open_root();
        } else {
            current_dir = dir_reopen(current_thread->cwd);
        }
    }

    char *token, *save_ptr;
    for (token = strtok_r(dir_copy, "/", &save_ptr); token != NULL; token = strtok_r(NULL, "/", &save_ptr)) {
        struct inode *inode = NULL;
        if (!dir_lookup(current_dir, token, &inode)) {
            dir_close(current_dir);
            return NULL;
        } else {
            struct dir *next = dir_open(inode);
            if (!next) {
                dir_close(current_dir);
                return NULL;
            }
            dir_close(current_dir);
            current_dir = next;
        }
    }
    if (is_removed(dir_get_inode(current_dir))) {
        dir_close(current_dir);
        printf("trying to open removed directory\n");
        return NULL;
    }
    return current_dir;
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *dir_reopen(struct dir *dir) {
    return dir_open(inode_reopen(dir->inode));
}

/* Destroys DIR and frees associated resources. */
void dir_close(struct dir *dir) {
    if (dir != NULL) {
        inode_close(dir->inode);
        free(dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *dir_get_inode(struct dir *dir) {
    ASSERT(dir);
    return dir->inode;
}

bool dir_is_empty(const struct dir *dir) {
    struct dir_entry entry;
    for (int ofs = sizeof(entry); inode_read_at(dir->inode, &entry, sizeof(entry), ofs) == sizeof(entry); ofs += sizeof(entry)) {
        if (entry.in_use) {
            return false;
        }
    }
    return true;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool lookup(const struct dir *dir, const char *name,
                   struct dir_entry *ep, off_t *ofsp) {
    struct dir_entry e;
    size_t ofs;

    ASSERT(dir != NULL);
    ASSERT(name != NULL);

    //start after parent directory
    for (ofs = sizeof(e); inode_read_at(dir->inode, &e, sizeof e, ofs) == sizeof e; ofs += sizeof e) {
        if (e.in_use && !strcmp(name, e.name)) {
            if (ep != NULL)
                *ep = e;
            if (ofsp != NULL)
                *ofsp = ofs;
            return true;
        }
    }
    return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool dir_lookup(const struct dir *dir, const char *name,
                struct inode **inode) {
    struct dir_entry e;

    ASSERT(dir != NULL);
    ASSERT(name != NULL);

    if (strcmp(name, ".") == 0) {
        *inode = inode_reopen(dir->inode);
    } else if (strcmp(name, "..") == 0) {
        inode_read_at(dir->inode, &e, sizeof(e), 0);
        *inode = inode_open(e.inode_sector);
    } else if (lookup(dir, name, &e, NULL)) {
        *inode = inode_open(e.inode_sector);
    } else {
        *inode = NULL;
    }

    return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool dir_add(struct dir *dir, const char *name, block_sector_t inode_sector, InodeType type) {
    struct dir_entry e;
    off_t ofs;
    bool success = false;

    ASSERT(dir != NULL);
    ASSERT(name != NULL);

    /* Check NAME for validity. */
    if (*name == '\0' || strlen(name) > NAME_MAX) {
        return false;
    }

    /* Check that NAME is not in use. */
    if (lookup(dir, name, NULL, NULL)){
        goto done;
    }

    // change the first directory to the parent directory
    if (type == DIRECTORY) {
        struct dir *child = dir_open(inode_open(inode_sector));
        if (!child)
            goto done;
        e.inode_sector = inode_get_inumber(dir_get_inode(dir));
        if (inode_write_at(child->inode, &e, sizeof(e), 0) != sizeof(e)) {
            dir_close(child);
            goto done;
        }
        dir_close(child);
    }

    /* Set OFS to offset of free slot.
       If there are no free slots, then it will be set to the
       current end-of-file.

       inode_read_at() will only return a short read at end of file.
       Otherwise, we'd need to verify that we didn't get a short
       read due to something intermittent such as low memory. */
    for (ofs = sizeof(e); inode_read_at(dir->inode, &e, sizeof e, ofs) == sizeof e;
         ofs += sizeof e)
        if (!e.in_use)
            break;

    /* Write slot. */
    e.in_use = true;
    strlcpy(e.name, name, sizeof e.name);
    e.inode_sector = inode_sector;
    success = inode_write_at(dir->inode, &e, sizeof e, ofs) == sizeof e;

    done:
    return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool dir_remove(struct dir *dir, const char *name) {
    struct dir_entry e;
    struct inode *inode = NULL;
    bool success = false;
    off_t ofs;

    ASSERT(dir != NULL);
    ASSERT(name != NULL);

    /* Find directory entry. */
    if (!lookup(dir, name, &e, &ofs))
        goto done;

    /* Open inode. */
    inode = inode_open(e.inode_sector);
    if (inode == NULL)
        goto done;

    if (is_directory(inode)) {
        struct dir *to_remove = dir_open(inode);
        bool is_emtpy = dir_is_empty(to_remove);
        dir_close(to_remove);
        if (!is_emtpy)
            goto done;
    }

    /* Erase directory entry. */
    e.in_use = false;
    if (inode_write_at(dir->inode, &e, sizeof e, ofs) != sizeof e)
        goto done;

    /* Remove inode. */
    inode_remove(inode);
    success = true;

    done:
    inode_close(inode);
    return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool dir_readdir(struct dir *dir, char name[NAME_MAX + 1]) {
    struct dir_entry e;

    while (inode_read_at(dir->inode, &e, sizeof e, dir->pos) == sizeof e) {
        dir->pos += sizeof e;
        if (e.in_use) {
            strlcpy(name, e.name, NAME_MAX + 1);
            return true;
        }
    }
    return false;
}
