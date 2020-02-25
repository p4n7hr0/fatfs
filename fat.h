/*
 * fat.h - minimal fat parser
 * Copyright (C) 2020 p4n7hr0
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef FAT_H
#define FAT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

/* opaque types */
typedef struct fat_fs fatfs_t;
typedef struct fat_dir fatdir_t;
typedef struct fat_file fatfile_t;

/* directory entry attributes */
#define	FAT_ATTR_READ_ONLY 0x01
#define	FAT_ATTR_HIDDEN    0x02
#define	FAT_ATTR_SYSTEM    0x04
#define	FAT_ATTR_DIRECTORY 0x10
#define	FAT_ATTR_ARCHIVE   0x20

#define FAT_SEEK_SET       0
#define FAT_SEEK_END       1
#define FAT_SEEK_CUR       2

#define SHORT_NAME_SIZE    13
#define LONG_NAME_SIZE     256

/* fat directory entry */
struct fat_dir_entry {
	char     short_name[SHORT_NAME_SIZE];
	wchar_t  long_name[LONG_NAME_SIZE];
	uint32_t cluster;
	uint32_t attr;
	uint32_t size;
};

fatfs_t *
fat_mount(const char *filename, off_t offset);

void
fat_umount(fatfs_t *fatfs);

const char *
fat_getlabel(fatfs_t *fatfs);

fatdir_t *
fat_getroot(fatfs_t *fatfs);

fatdir_t *
fat_opendir(fatdir_t *fatdir, const char *name);

struct fat_dir_entry *
fat_readdir(fatdir_t *fatdir);

void
fat_rewinddir(fatdir_t *fatdir);

void
fat_closedir(fatdir_t *fatdir);

fatfile_t *
fat_open(fatdir_t *fatdir, const char *filename);

ssize_t
fat_read(fatfile_t *fatfile, void *buf, size_t nbyte);
/*
ssize_t
fat_write(fatfile_t *fatfile, void *buf, size_t nbyte);

off_t
fat_seek(fatfile_t *fatfile, off_t offset, int whence);
*/
void
fat_close(fatfile_t *fatfile);

#ifdef __cplusplus
}
#endif

#endif /* FAT_H */
