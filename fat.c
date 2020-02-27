/*
 * fat.c - minimal fat parser
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

#include "fat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* used as a helper to detect a cyclic fat chain */
#define CLUSTER_CHAIN_THRESHOLD 512

/* invalid cluster value */
#define INVALID_CLUSTER 0xffffffff

#define	FAT_ATTR_VOLUME_ID 0x08

#define FAT_ATTR_LONG_NAME \
(FAT_ATTR_READ_ONLY | FAT_ATTR_HIDDEN | FAT_ATTR_SYSTEM | FAT_ATTR_VOLUME_ID)

#define FAT_ATTR_VALID \
(FAT_ATTR_LONG_NAME | FAT_ATTR_ARCHIVE | FAT_ATTR_DIRECTORY)

/* fatfs_t object */
struct fat_fs {
	FILE *stream;
	off_t offset;
	char name[6], label[12];
	uint32_t fat_off;
	uint32_t fat_size;
	uint32_t root_dir_off;
	uint32_t root_dir_size;
	uint32_t root_dir_cluster;
	uint32_t data_area_off;
	uint32_t data_area_size;
	uint32_t num_total_sectors;
	uint32_t num_total_clusters;
	uint8_t sectors_per_cluster;
	uint16_t bytes_per_sector;
	int32_t end_of_file_value;
	int32_t bad_cluster_value;
	int32_t (*read_fat_entry)(struct fat_fs *fatfs, uint32_t index);
};

/* fatdir_t object */
struct fat_dir {
	fatfs_t *fatfs;

	union {
		struct {
			uint32_t first_cluster;
			uint32_t current_cluster;
			uint32_t cluster_offset;

			/* used to identify loop in the cluster chain */
			uint32_t cluster_counter;
			uint32_t saved_cluster;
		} by_clus;

		struct  {
			uint32_t reserved;
			uint32_t start_offset;
			uint32_t end_offset;
			uint32_t current_offset;
		} by_off;
	} loc;

	uint8_t no_more_entries;
	struct fat_dir_entry last_entry;
};

/* fatfile_t object */
struct fat_file {
	fatfs_t *fatfs;
	uint32_t filesize;
	uint32_t first_cluster;
	uint32_t current_cluster;
	uint32_t cluster_offset;

	/* mark end of file */
	uint8_t no_more_bytes;

	/* used to calc the current file offset */
	uint32_t cluster_counter;
};

#pragma pack(push, 1)
/* BIOS Parameter Block */
struct fat_bpb {
	uint8_t jump_boot[3];
	int8_t oem_name[8];
	uint16_t bytes_per_sector;
	uint8_t sectors_per_cluster;
	uint16_t num_reserved_sectors;
	uint8_t num_fats;
	uint16_t num_root_entries;
	uint16_t num_total_sectors_16;
	uint8_t media;
	uint16_t sectors_per_fat_16;
	uint16_t sectors_per_track;
	uint16_t num_heads;
	uint32_t num_hidden_sectors;
	uint32_t num_total_sectors_32;

	union {
	/* fat12 or fat16 */
		struct {
			uint8_t drive_num;
			uint8_t reserved;
			uint8_t boot_sig;
			uint32_t vol_serial;
			uint8_t label[11];
			uint8_t fstype[8];
		} fat_12_16;

	/* fat32 */
		struct {
			uint32_t sectors_per_fat_32;
			uint16_t extended_flags;
			uint16_t fs_version;
			uint32_t root_cluster;
			uint16_t fs_info;
			uint16_t backup_boot_sector;
			uint8_t reserved[12];
			uint8_t num_ph_drive;
			uint8_t reserved1;
			uint8_t boot_sig;
			uint32_t num_serial;
			uint8_t label[11];
			uint8_t fstype[8];
		} fat_32;
	} specific;
};

/* directory entry */
struct priv_fat_dir {
	union {
		/* generic entry */
		struct {
			uint8_t name[11];
			uint8_t attr;
			uint8_t res;
			uint8_t crt_time_tenth;
			uint16_t crt_time;
			uint16_t crt_date;
			uint16_t last_acc_date;
			uint16_t first_cluster_high;
			uint16_t wrt_time;
			uint16_t wrt_date;
			uint16_t first_cluster_low;
			uint32_t file_size;
		} gen;

	/* long name entry */
		struct {
			uint8_t ord;
			uint8_t name1[10];
			uint8_t attr;
			uint8_t res1;
			uint8_t chksum;
			uint8_t name2[12];
			uint8_t res2[2];
			uint8_t name3[4];
		} ln;
	} type;
};

#pragma pack(pop)


static int32_t
fatfs_read(fatfs_t *fatfs, void *buf, int32_t size)
{
	size_t nbytes = 0;
	if (size < 0)
		return -1;

	nbytes = fread(buf, 1, size, fatfs->stream);
	if (ferror(fatfs->stream))
		return -1;

	return (int32_t)nbytes;
}
/*
static int32_t
fatfs_write(fatfs_t *fatfs, void *buf, int32_t size)
{
	size_t nbytes = 0;
	if (size < 0)
		return -1;

	nbytes = fwrite(buf, 1, size, fatfs->stream);
	if (ferror(fatfs->stream))
		return -1;

	return (int32_t)nbytes;
}
*/

static int32_t
fatfs_seek(fatfs_t *fatfs, off_t offset, int whence)
{
	return fseeko(fatfs->stream, fatfs->offset + offset, whence);
}

static int32_t
fatfs_read_fat12(fatfs_t *fatfs, uint32_t index)
{
	uint16_t value = 0;
	if (index > fatfs->num_total_clusters)
		return INVALID_CLUSTER;
	if (fatfs_seek(fatfs, fatfs->fat_off + index + (index / 2), SEEK_SET))
		return INVALID_CLUSTER;
	if (fatfs_read(fatfs, &value, sizeof(value)) < (int32_t)sizeof(value))
		return INVALID_CLUSTER;
	return (index & 1) ? (value >> 4) : (value & 0xfff);
}

static int32_t
fatfs_read_fat16(fatfs_t *fatfs, uint32_t index)
{
	uint16_t value = 0;
	if (index > fatfs->num_total_clusters)
		return INVALID_CLUSTER;
	if (fatfs_seek(fatfs, fatfs->fat_off + (index * 2), SEEK_SET))
		return INVALID_CLUSTER;
	if (fatfs_read(fatfs, &value, sizeof(value)) < (int32_t)sizeof(value))
		return INVALID_CLUSTER;
	return value;
}

static int32_t
fatfs_read_fat32(fatfs_t *fatfs, uint32_t index)
{
	uint32_t value = 0;
	if (index > fatfs->num_total_clusters)
		return INVALID_CLUSTER;
	if (fatfs_seek(fatfs, fatfs->fat_off + (index * 4), SEEK_SET))
		return INVALID_CLUSTER;
	if (fatfs_read(fatfs, &value, sizeof(value)) < (int32_t)sizeof(value))
		return INVALID_CLUSTER;
	return value & 0x0fffffff;
}

static inline void
remove_trailing_spaces(char *b, size_t size)
{
	for (size_t i = size-1; i != 0; i--) {
		if (b[i] == ' ')
			b[i] = '\0';
		else
			break;
	}
}

static inline void
set_label(fatfs_t *fatfs, void *label)
{
	memcpy(fatfs->label, label, sizeof(fatfs->label)-1);
	remove_trailing_spaces(fatfs->label, sizeof(fatfs->label)-1);
	fatfs->label[sizeof(fatfs->label)-1] = '\0';
}

static int
fatfs_parse_bpb(fatfs_t *fatfs)
{
	struct fat_bpb bpb;

	if (!fatfs)
		return -1;

	/* adjust file pointer */
	if (fatfs_seek(fatfs, 0, SEEK_SET))
		return -1;
	/* read bpb */
	if (fatfs_read(fatfs, &bpb, sizeof(bpb)) < (int32_t)sizeof(bpb))
		return -1;

	fatfs->sectors_per_cluster = bpb.sectors_per_cluster;
	fatfs->bytes_per_sector = bpb.bytes_per_sector;

	/* check */
	if ((bpb.bytes_per_sector != 512)
		&& (bpb.bytes_per_sector != 1024)
		&& (bpb.bytes_per_sector != 2048)
		&& (bpb.bytes_per_sector != 4096))
		return -1;

	if (bpb.sectors_per_cluster == 0)
		return -1;

	/* size of fat area */
	fatfs->fat_size = (bpb.sectors_per_fat_16) ? bpb.sectors_per_fat_16 :
		bpb.specific.fat_32.sectors_per_fat_32;
	fatfs->fat_size *= bpb.bytes_per_sector;

	/* more check */
	if (!fatfs->fat_size)
		return -1;

	/* offset to first fat */
	fatfs->fat_off = bpb.num_reserved_sectors * bpb.bytes_per_sector;
	/* size of root dir */
	fatfs->root_dir_size = bpb.num_root_entries * 32;
	/* offset to root dir */
	fatfs->root_dir_off = fatfs->fat_off + (fatfs->fat_size * bpb.num_fats);
	fatfs->root_dir_cluster = 0; /* only for fat32 */

	/* number of sectors in filesystem */
	fatfs->num_total_sectors = (bpb.num_total_sectors_16) ?
		bpb.num_total_sectors_16 : bpb.num_total_sectors_32;

	/* more check */
	if (!fatfs->num_total_sectors)
		return -1;

	/* data area offset and size */
	fatfs->data_area_off = fatfs->root_dir_off + fatfs->root_dir_size;
	fatfs->data_area_size = (fatfs->num_total_sectors * bpb.bytes_per_sector)
		- fatfs->data_area_off;

	/* num of clusters in data area */
	fatfs->num_total_clusters = fatfs->data_area_size / bpb.bytes_per_sector
		/ bpb.sectors_per_cluster;

	/* more check */
	if ((fatfs->fat_off >= fatfs->root_dir_off)
		|| (fatfs->root_dir_off > fatfs->data_area_off))
		return -1;

	/* fat type */
	if (fatfs->num_total_clusters < 4085) {
		set_label(fatfs, &bpb.specific.fat_12_16.label[0]);
		strncpy(fatfs->name, "FAT12", sizeof(fatfs->name));
		fatfs->read_fat_entry = fatfs_read_fat12;
		fatfs->end_of_file_value = 0xfff;
		fatfs->bad_cluster_value = 0xff7;

	} else if (fatfs->num_total_clusters < 65525) {
		set_label(fatfs, &bpb.specific.fat_12_16.label[0]);
		strncpy(fatfs->name, "FAT16", sizeof(fatfs->name));
		fatfs->read_fat_entry = fatfs_read_fat16;
		fatfs->end_of_file_value = 0xffff;
		fatfs->bad_cluster_value = 0xfff7;

	} else {
		set_label(fatfs, &bpb.specific.fat_32.label[0]);
		strncpy(fatfs->name, "FAT32", sizeof(fatfs->name));
		fatfs->read_fat_entry = fatfs_read_fat32;
		fatfs->end_of_file_value = 0x0fffffff;
		fatfs->bad_cluster_value = 0x0ffffff7;
		/* set first cluster of root dir */
		fatfs->root_dir_cluster = bpb.specific.fat_32.root_cluster;
		if (bpb.specific.fat_32.extended_flags & 0x80) {
			if ((bpb.specific.fat_32.extended_flags & 0xf) > bpb.num_fats)
				return -1;
			/* adjust offset */
			fatfs->fat_off += fatfs->fat_size *
				(bpb.specific.fat_32.extended_flags & 0xf);
		}
	}

	return 0;
}

static inline int
fatdir_loc_by_clus(fatdir_t *fatdir)
{
	return (fatdir->loc.by_clus.first_cluster != INVALID_CLUSTER);
}

static inline off_t
fatdir_data_offset(fatdir_t *fatdir)
{
	/* loc by offset */
	off_t offset = fatdir->loc.by_off.current_offset;

	/* loc by cluster */
	if (fatdir_loc_by_clus(fatdir)) {
		offset = fatdir->fatfs->data_area_off;
		offset += (fatdir->loc.by_clus.current_cluster - 2)
			* fatdir->fatfs->sectors_per_cluster
			* fatdir->fatfs->bytes_per_sector;
		offset += fatdir->loc.by_clus.cluster_offset;
	}

	return offset;
}

static inline off_t
fatfile_data_offset(fatfile_t *fatfile)
{
	return fatfile->fatfs->data_area_off
	+ fatfile->cluster_offset
	+ ((fatfile->current_cluster - 2)
	* fatfile->fatfs->sectors_per_cluster
	* fatfile->fatfs->bytes_per_sector);
}

static inline off_t
fatfile_offset(fatfile_t *fatfile)
{
	return ((fatfile->cluster_counter
	* fatfile->fatfs->sectors_per_cluster
	* fatfile->fatfs->bytes_per_sector) + fatfile->cluster_offset);
}

static void
fatdir_increment_loc(fatdir_t *fatdir)
{
	if (fatdir_loc_by_clus(fatdir)) {
		uint32_t max_cluster_off = (fatdir->fatfs->sectors_per_cluster
			* fatdir->fatfs->bytes_per_sector);
		fatdir->loc.by_clus.cluster_offset += 32;

		/* if cluster end, read next cluster from fat */
		if (fatdir->loc.by_clus.cluster_offset == max_cluster_off) {
			int32_t curr_cluster = fatdir->loc.by_clus.current_cluster;
			int32_t next_cluster = fatdir->fatfs->read_fat_entry(fatdir->fatfs,
				curr_cluster);

			/* TODO: check error (INVALID_CLUSTER) */

			/* if end-of-file, set end mark */
			if (next_cluster == fatdir->fatfs->end_of_file_value)
				fatdir->no_more_entries = 1;

			else {
				/* update position */
				fatdir->loc.by_clus.cluster_counter++;
				fatdir->loc.by_clus.current_cluster = next_cluster;
				fatdir->loc.by_clus.cluster_offset = 0;

				/* try to identify loop in the cluster chain */
				if (fatdir->loc.by_clus.saved_cluster == (uint32_t) next_cluster)
					fatdir->no_more_entries = 1;
				if (fatdir->loc.by_clus.cluster_counter == fatdir->fatfs->num_total_clusters)
					fatdir->no_more_entries = 1;
				if (fatdir->loc.by_clus.cluster_counter == CLUSTER_CHAIN_THRESHOLD)
					fatdir->loc.by_clus.saved_cluster = next_cluster;
			}
		}

	} else {
		fatdir->loc.by_off.current_offset += 32;
		if (fatdir->loc.by_off.current_offset == fatdir->loc.by_off.end_offset)
			fatdir->no_more_entries = 1;
	}
}

static void
fatfile_increment_cluster(fatfile_t *fatfile)
{
	if (fatfile_offset(fatfile) < fatfile->filesize) {
		int32_t curr_cluster = fatfile->current_cluster;
		int32_t next_cluster = fatfile->fatfs->read_fat_entry(fatfile->fatfs,
			curr_cluster);

		/* TODO: check error (INVALID_CLUSTER) */

		/* if end-of-file, set end mark */
		if (next_cluster == fatfile->fatfs->end_of_file_value)
			fatfile->no_more_bytes = 1;

		else {
			/* update position */
			fatfile->cluster_counter++;
			fatfile->current_cluster = next_cluster;
			fatfile->cluster_offset = 0;
		}
	}
}

static int
fatdir_read_priv_fat_dir(fatdir_t *fatdir, struct priv_fat_dir *priv_fat_dir)
{
	/* check end flag */
	if (fatdir->no_more_entries)
		return -1;

	/* adjust offset */
	if(fatfs_seek(fatdir->fatfs, fatdir_data_offset(fatdir), SEEK_SET))
		return -1;
	/* read entry */
	if (fatfs_read(fatdir->fatfs, priv_fat_dir, 32) < 32)
		return -1;
	/* all free from this */
	if (priv_fat_dir->type.gen.name[0] == 0x00) {
		fatdir->no_more_entries = 1;
		return -1;
	}

	/* increment location */
	fatdir_increment_loc(fatdir);
	return 0;
}

static inline void
fatdir_clean_last_entry(fatdir_t *fatdir)
{
	memset(&fatdir->last_entry, 0, sizeof(fatdir->last_entry));
}

static inline size_t
fatdir_last_entry_ln_cnt_lz(fatdir_t *fatdir)
{
	size_t ln_cnt_lz = 0;
	/* count leading zeros in fatdir->last_entry.long_name */
	for (size_t i = 0; i < LONG_NAME_SIZE - 1; i++) {
		if (fatdir->last_entry.long_name[i] == 0x00)
			ln_cnt_lz += 1;
		else
			break;
	}

	return ln_cnt_lz;
}

static inline size_t
fat_ln_strnlen(uint16_t *s, size_t max)
{
	size_t n = 0;
	for (size_t i = 0; i < max; i++) {
		if ((s[i] != 0) && (s[i] != 0xffff))
			n++;

		else break;
	}

	return n;
}

static void
fatdir_append_long_name(fatdir_t *fatdir, struct priv_fat_dir *priv_fat_dir)
{
	size_t wsz_name, ln_start_index = fatdir_last_entry_ln_cnt_lz(fatdir);
	wsz_name = fat_ln_strnlen((uint16_t *)priv_fat_dir->type.ln.name3, 2);
	ln_start_index -= wsz_name;
	for (size_t i = 0, j = ln_start_index; i < wsz_name * 2; i += 2, j++)
		fatdir->last_entry.long_name[j] = (wchar_t) priv_fat_dir->type.ln.name3[i];

	wsz_name = fat_ln_strnlen((uint16_t *)priv_fat_dir->type.ln.name2, 6);
	ln_start_index -= wsz_name;
	for (size_t i = 0, j = ln_start_index; i < wsz_name * 2; i += 2, j++)
		fatdir->last_entry.long_name[j] = (wchar_t) priv_fat_dir->type.ln.name2[i];

	wsz_name = fat_ln_strnlen((uint16_t *)priv_fat_dir->type.ln.name1, 5);
	ln_start_index -= wsz_name;
	for (size_t i = 0, j = ln_start_index; i < wsz_name * 2; i += 2, j++)
		fatdir->last_entry.long_name[j] = (wchar_t) priv_fat_dir->type.ln.name1[i];
}

static void
fatdir_set_last_entry(fatdir_t *fatdir, struct priv_fat_dir *priv_fat_dir)
{
	/* copy name */
	char *nptr = &fatdir->last_entry.short_name[0];
	for (int i = 0; i < 8; i++) {
		if (priv_fat_dir->type.gen.name[i] == ' ')
			break;
		*nptr = priv_fat_dir->type.gen.name[i];
		nptr++;
	}

	/* copy ext */
	if (priv_fat_dir->type.gen.name[8] != ' ') {
		*nptr = '.';
		nptr++;

		for (int i = 8; i < 11; i++) {
			if (priv_fat_dir->type.gen.name[i] == ' ')
				break;
			*nptr = priv_fat_dir->type.gen.name[i];
			nptr++;
		}
	}

	/* set attr, size, cluster */
	fatdir->last_entry.attr = priv_fat_dir->type.gen.attr;
	fatdir->last_entry.size = priv_fat_dir->type.gen.file_size;
	fatdir->last_entry.cluster = (priv_fat_dir->type.gen.first_cluster_high << 16)
		| priv_fat_dir->type.gen.first_cluster_low;

	/* adjust long name */
	size_t ln_start_index = fatdir_last_entry_ln_cnt_lz(fatdir);
	wchar_t *dst_ptr = &fatdir->last_entry.long_name[0];
	wchar_t *src_ptr = &fatdir->last_entry.long_name[ln_start_index];
	for (size_t i = 0; i < (LONG_NAME_SIZE - ln_start_index); i++)
		dst_ptr[i] = src_ptr[i];
}

fatfs_t *
fat_mount(const char *filename, off_t offset)
{
	fatfs_t *fatfs = NULL;

	/* sanity check */
	if (!filename)
		return NULL;

	/* open file */
	FILE *stream = fopen(filename, "r+b");
	if (!stream)
		return NULL;

	/* alloc new fatfs */
	fatfs = (fatfs_t *) calloc(1, sizeof(fatfs_t));
	if (!fatfs) {
		fclose(stream);
		return NULL;
	}

	fatfs->stream = stream;
	fatfs->offset = offset;

	/* parse bpb */
	if (fatfs_parse_bpb(fatfs)) {
		fat_umount(fatfs);
		return NULL;
	}

	return fatfs;
}

void
fat_umount(fatfs_t *fatfs)
{
	if (fatfs) {
		fclose(fatfs->stream);
		free(fatfs);
	}
}

const char *
fat_getlabel(fatfs_t *fatfs)
{
	return (fatfs) ? fatfs->label : NULL;
}

fatdir_t *
fat_getroot(fatfs_t *fatfs)
{
	fatdir_t *rootdir = NULL;

	if (!fatfs)
		return NULL;

	rootdir = (fatdir_t *) calloc(1, sizeof(fatdir_t));
	if (!rootdir)
		return NULL;

	rootdir->fatfs = fatfs;
	/* fat32 */
	if (fatfs->root_dir_cluster) {
		rootdir->loc.by_clus.first_cluster = fatfs->root_dir_cluster;
		rootdir->loc.by_clus.current_cluster = fatfs->root_dir_cluster;
		rootdir->loc.by_clus.cluster_offset = 0;
		rootdir->loc.by_clus.cluster_counter = 0;
		rootdir->loc.by_clus.saved_cluster = 0;
	/* fat12 or fat16 */
	} else {
		rootdir->loc.by_off.reserved = INVALID_CLUSTER;
		rootdir->loc.by_off.start_offset = fatfs->root_dir_off;
		rootdir->loc.by_off.current_offset = fatfs->root_dir_off;
		rootdir->loc.by_off.end_offset = fatfs->root_dir_off
			+ fatfs->root_dir_size;
	}

	return rootdir;
}

fatdir_t *
fat_opendir(fatdir_t *fatdir, const char *name)
{
	fatdir_t dircopy, *d = NULL;

	if (!fatdir || !name)
		return NULL;

	/* copy dir */
	dircopy.fatfs = fatdir->fatfs;
	dircopy.loc = fatdir->loc;
	dircopy.no_more_entries = fatdir->no_more_entries;
	dircopy.last_entry = fatdir->last_entry;
	fat_rewinddir(&dircopy);

	do {
		/* search directory entry */
		if (!strncmp(dircopy.last_entry.short_name, name, SHORT_NAME_SIZE)) {
			d = (fatdir_t *) calloc(1, sizeof(fatdir_t));
			if (!d)
				return NULL;

			d->fatfs = dircopy.fatfs;
			d->loc.by_clus.first_cluster = dircopy.last_entry.cluster;
			d->loc.by_clus.current_cluster = dircopy.last_entry.cluster;
			d->loc.by_clus.cluster_offset = 0;
			d->loc.by_clus.cluster_counter = 0;
			d->loc.by_clus.saved_cluster = 0;
			break;
		}

	} while (fat_readdir(&dircopy));

	return d;
}

struct fat_dir_entry *
fat_readdir(fatdir_t *fatdir)
{
	struct priv_fat_dir priv_fat_dir;

	if (!fatdir)
		return NULL;

	/* clean cache */
	fatdir_clean_last_entry(fatdir);

	/* read entries */
	while (1) {
		/* if error, ret */
		if (fatdir_read_priv_fat_dir(fatdir, &priv_fat_dir))
			return NULL;
		/* long name */
		else if (priv_fat_dir.type.gen.attr == FAT_ATTR_LONG_NAME)
			fatdir_append_long_name(fatdir, &priv_fat_dir);
		/* volume id */
		else if (priv_fat_dir.type.gen.attr == FAT_ATTR_VOLUME_ID)
			continue;
		/* file, dir */
		else
			break;
	}

	fatdir_set_last_entry(fatdir, &priv_fat_dir);
	return &fatdir->last_entry;
}

void
fat_rewinddir(fatdir_t *fatdir)
{
	if (fatdir) {
		fatdir->no_more_entries = 0;

		/* by_clus */
		if (fatdir_loc_by_clus(fatdir)) {
			fatdir->loc.by_clus.current_cluster = fatdir->loc.by_clus.first_cluster;
			fatdir->loc.by_clus.cluster_offset = 0;
			fatdir->loc.by_clus.cluster_counter = 0;
			fatdir->loc.by_clus.saved_cluster = 0;
		/* by_off */
		} else {
			fatdir->loc.by_off.current_offset = fatdir->loc.by_off.start_offset;
		}
	}
}

void
fat_closedir(fatdir_t *fatdir)
{
	free(fatdir);
}

fatfile_t *
fat_open(fatdir_t *fatdir, const char *filename)
{
	fatdir_t dircopy;
	fatfile_t *fatfile = NULL;

	/* sanity check */
	if (!fatdir || !filename)
		return NULL;

	/* copy dir */
	dircopy.fatfs = fatdir->fatfs;
	dircopy.loc = fatdir->loc;
	dircopy.no_more_entries = fatdir->no_more_entries;
	dircopy.last_entry = fatdir->last_entry;
	fat_rewinddir(&dircopy);

	do {
		/* search directory entry */
		if (!strncmp(dircopy.last_entry.short_name, filename, SHORT_NAME_SIZE)) {
			fatfile = (fatfile_t *) calloc(1, sizeof(fatfile_t));
			if (!fatfile)
				return NULL;

			fatfile->fatfs = dircopy.fatfs;
			fatfile->first_cluster = dircopy.last_entry.cluster;
			fatfile->current_cluster = dircopy.last_entry.cluster;
			fatfile->cluster_offset = 0;
			fatfile->filesize = dircopy.last_entry.size;
			fatfile->cluster_counter = 0;
			fatfile->no_more_bytes = 0;
			break;
		}

	} while (fat_readdir(&dircopy));

	return fatfile;
}

ssize_t
fat_read(fatfile_t *fatfile, void *buf, size_t nbyte)
{
	ssize_t total_read = 0;
	uint32_t max_cluster_off;

	/* sanity check */
	if (!fatfile || !buf)
		return -1;
	/* no bytes to read */
	if (nbyte == 0)
		return 0;
	/* max bytes to read */
	if (nbyte > INT_MAX)
		nbyte = INT_MAX;

	max_cluster_off = (fatfile->fatfs->sectors_per_cluster
		* fatfile->fatfs->bytes_per_sector);

	while ((total_read < (ssize_t) nbyte)
		&& (fatfile_offset(fatfile) < fatfile->filesize)) {

		/* useful if there is more bytes than clusters */
		if (fatfile->no_more_bytes)
			break;

		/* calc current slice */
		uint32_t slice_size = max_cluster_off - fatfile->cluster_offset;
		if (slice_size > (nbyte - total_read))
			slice_size = (nbyte - total_read);
		/* avoid read beyond file size */
		if((fatfile_offset(fatfile) + slice_size) > fatfile->filesize)
			slice_size = fatfile->filesize - fatfile_offset(fatfile);

		/* adjust offset */
		if(fatfs_seek(fatfile->fatfs, fatfile_data_offset(fatfile), SEEK_SET))
			break;

		/* read bytes */
		int32_t bytes_read = fatfs_read(fatfile->fatfs,
			(uint8_t *)buf + total_read, slice_size);

		/* if err, break */
		if (bytes_read == -1)
			break;

		total_read += bytes_read;
		fatfile->cluster_offset += bytes_read;

		/* if cluster end, load next */
		if (fatfile->cluster_offset == max_cluster_off)
			fatfile_increment_cluster(fatfile);
	}

	return total_read;
}

/*
ssize_t
fat_write(fatfile_t *fatfile, void *buf, size_t nbyte)
{
	return 0;
}
*/

off_t
fat_seek(fatfile_t *fatfile, off_t offset, int whence)
{
	off_t old_off;
	uint32_t size_of_cluster;

	if (!fatfile)
		return -1;

	/* handle negative offset */
	if (offset < 0) {
		if (whence == FAT_SEEK_SET)
			return -1;
		else if (whence == FAT_SEEK_END)
			return fat_seek(fatfile, (off_t)fatfile->filesize + offset,
				FAT_SEEK_SET);
		else if (whence == FAT_SEEK_CUR)
			return fat_seek(fatfile, fatfile_offset(fatfile) + offset,
				FAT_SEEK_SET);
		else
			return -1;
	}

	/* handle positive offset */
	if (whence == FAT_SEEK_SET) {
		fatfile->current_cluster = fatfile->first_cluster;
		fatfile->cluster_offset = 0;
		fatfile->cluster_counter = 0;
		fatfile->no_more_bytes = 0;

	} else if (whence == FAT_SEEK_END) {
		offset += fatfile->filesize - fatfile_offset(fatfile);
		/* if file pointer is beyond end-of-file and beyond the new offset */
		if (offset < 0)
			return fat_seek(fatfile, offset, FAT_SEEK_CUR);

	} else if (whence != FAT_SEEK_CUR)
		return -1;

	old_off = fatfile_offset(fatfile);
	size_of_cluster = fatfile->fatfs->sectors_per_cluster
		* fatfile->fatfs->bytes_per_sector;

	/* adjust cluster */
	for (off_t i = 0; i < (offset / size_of_cluster); i++) {
		if (fatfile_offset(fatfile) >= fatfile->filesize)
			break;

		fatfile_increment_cluster(fatfile);
	}

	/* adjust offset */
	fatfile->cluster_offset += offset - (fatfile_offset(fatfile) - old_off);
	return fatfile_offset(fatfile);
}

void
fat_close(fatfile_t *fatfile)
{
	free(fatfile);
}

