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

#include <wchar.h>
#include <stdint.h>

typedef int64_t fatoff_t;
typedef int32_t fatclus_t;

typedef struct fatfs fatfs_t;
typedef struct fatdir fatdir_t;
typedef struct fatfile fatfile_t;

/* seek pos */
#define FAT_SEEK_SET       0
#define FAT_SEEK_END       1
#define FAT_SEEK_CUR       2

/* fatdirent type */
#define	FAT_TYPE_DIRECTORY 1
#define	FAT_TYPE_ARCHIVE   2

/* max file name */
#define FAT_MAX_NAME       260

struct fatdirent {
	fatoff_t      d_privoff;
	fatclus_t     d_cluster;
	fatoff_t      d_size;
	unsigned char d_type;
	wchar_t       d_name[FAT_MAX_NAME+1];
};

/* errors */
enum {
	FAT_ERR_SUCCESS = 0,  /* success */
	FAT_ERR_NOENT,        /* path does not exist */
	FAT_ERR_INVAL,        /* invalid argument */
	FAT_ERR_ENOMEM,       /* allocation error */
	FAT_ERR_NOTFATFS,     /* invalid filesystem */
	FAT_ERR_ACCESS,       /* fat_mount: access denied for filename */
	FAT_ERR_DEVBUSY,      /* fat_mount: device is busy */
	FAT_ERR_NOTDIR,       /* a component of the path is not a directory */
	FAT_ERR_ISDIR,        /* path is a directory */
	FAT_ERR_WRONLY,       /* write-only file */
	FAT_ERR_RDONLY,       /* read-only file  */
	FAT_ERR_MAXSIZE,      /* read/write size is above UINT_MAX */
	FAT_ERR_FULLDISK,     /* disk is full */
	FAT_ERR_IO,           /* I/O error */
	FAT_ERR_NOTIMPL,      /* function is not implemented */
};

/* file system operations */
int
fat_mount(fatfs_t **ppfatfs, const char *filename, fatoff_t offset);

void
fat_umount(fatfs_t *pfatfs);

wchar_t *
fat_getlabel(fatfs_t *pfatfs);

int
fat_error(fatfs_t *pfatfs);

/* directory operations */
fatdir_t *
fat_opendir(fatfs_t *pfatfs, const wchar_t *path);

struct fatdirent *
fat_readdir(fatdir_t *pfatdir);

long
fat_telldir(fatdir_t *pfatdir);

void
fat_seekdir(fatdir_t *pfatdir, long loc);

void
fat_rewinddir(fatdir_t *pfatdir);

void
fat_closedir(fatdir_t *pfatdir);

int
fat_mkdir(fatfs_t *pfatfs, const wchar_t *path);

int
fat_rmdir(fatfs_t *pfatfs, const wchar_t *path);

/* file operations */
fatfile_t *
fat_fopen(fatfs_t *pfatfs, const wchar_t *path, const char *mode);

size_t
fat_fread(void *buf, size_t size, size_t nitems, fatfile_t *pfatfile);

size_t
fat_fwrite(void *buf, size_t size, size_t nitems, fatfile_t *pfatfile);

int
fat_fseek(fatfile_t *pfatfile, fatoff_t offset, int whence);

fatoff_t
fat_ftell(fatfile_t *pfatfile);

void
fat_fclose(fatfile_t *pfatfile);

int
fat_truncate(fatfs_t *pfatfs, const wchar_t *filepath, fatoff_t length);

int
fat_unlink(fatfs_t *pfatfs, const wchar_t *path);

#ifdef __cplusplus
}
#endif

#endif /* FAT_H */
