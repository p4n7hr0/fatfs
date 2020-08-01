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
#include <wchar.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/errno.h>

#define _fseek64 fseeko

/* invalid cluster value */
#define INVALID_CLUSTER ((fatclus_t)-1)

/* end-of-file */
#define END_OF_FILE ((fatclus_t)-1)

/* fatfile mode */
#define FAT_FILE_MODE_READ   1
#define FAT_FILE_MODE_WRITE  2
#define FAT_FILE_MODE_APPEND 4

/* fat types */
#define FAT_TYPE_12 1
#define FAT_TYPE_16 2
#define FAT_TYPE_32 3

/* privdirent attributes */
#define	FAT_ATTR_READ_ONLY 0x01
#define	FAT_ATTR_HIDDEN    0x02
#define	FAT_ATTR_SYSTEM    0x04
#define	FAT_ATTR_VOLUME_ID 0x08
#define	FAT_ATTR_DIRECTORY 0x10
#define	FAT_ATTR_ARCHIVE   0x20
#define FAT_ATTR_LONG_NAME \
(FAT_ATTR_READ_ONLY | FAT_ATTR_HIDDEN | FAT_ATTR_SYSTEM | FAT_ATTR_VOLUME_ID)

/* logical block on data area (including fat12/16 root directory) */
typedef struct _fatblock {
	fatoff_t curoff;
	fatoff_t endoff;
	fatclus_t cluster;
	fatclus_t clsinit; /* first cluster on chain */
	fatoff_t index;    /* zero based */
} fatblock_t;

/* fatfs_t */
struct fatfs {
	FILE *stream;
	fatoff_t offset;
	fatoff_t volsize;

	int32_t type;
	int32_t errnum;
	wchar_t *label;

	fatoff_t fat_first_off;
	fatoff_t fat_active_off;
	fatoff_t fat_size_bytes;
	uint8_t  fat_num;

	fatblock_t root_block;
	fatoff_t data_start_off;

	fatclus_t max_cluster_num;
	uint32_t bytes_per_cluster;

	fatclus_t first_free_cluster;
	fatclus_t num_of_free_clusters;

	fatclus_t (*readfat)(struct fatfs *, fatclus_t);
	int       (*writefat)(struct fatfs *, fatclus_t, fatclus_t);
	fatclus_t (*readfatbuf)(void *data, size_t size, fatclus_t cluster);
};

/* fatdir_t */
struct fatdir {
	fatfs_t *pfatfs;
	fatoff_t privoff;
	fatblock_t block;
	long position;
	struct fatdirent data;
};

/* fatfile_t */
struct fatfile {
	fatfs_t *pfatfs;
	fatoff_t privoff;
	fatblock_t block;
	fatoff_t filesize;
	fatoff_t oversize;
	uint8_t mode;
};

#pragma pack(push, 1)
/* BIOS Parameter Block */
struct fat_bpb {
	uint8_t jump_boot[3];
	uint8_t oem_name[8];
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

#define PRIVDIR_LFN_NAME1 5
#define PRIVDIR_LFN_NAME2 6
#define PRIVDIR_LFN_NAME3 2

/* fat directory entry */
struct privdirent {
	union {
		/* generic entry */
		struct {
			uint8_t name_8dot3[11];
			uint8_t attribute;
			uint8_t zero1;
			uint8_t crt_time_tenth;
			uint16_t crt_time;
			uint16_t crt_date;
			uint16_t lst_acc_date;
			uint16_t first_cluster_high;
			uint16_t wrt_time;
			uint16_t wrt_date;
			uint16_t first_cluster_low;
			uint32_t file_size;
		} gen;

		/* long name entry */
		struct {
			uint8_t ordinal;
			uint16_t name1[PRIVDIR_LFN_NAME1];
			uint8_t attribute;
			uint8_t zero1;
			uint8_t chksum;
			uint16_t name2[PRIVDIR_LFN_NAME2];
			uint8_t zero2[2];
			uint16_t name3[PRIVDIR_LFN_NAME3];
		} lfn;
	} type;
};

#pragma pack(pop)

/* read nbytes from offset */
static size_t
fatfs_read_from_offset(fatfs_t *pfatfs, void *buf, size_t nbytes, fatoff_t offset)
{
	size_t nread = 0;
	pfatfs->errnum = FAT_ERR_IO;

	/* check negative */
	if (offset < 0)
		return 0;
	/* check wraparound */
	if (((fatoff_t)(offset + nbytes)) < 0)
		return 0;
	/* check volume bounds */
	if ((fatoff_t)(offset + nbytes) > pfatfs->volsize)
		return 0;

	if (_fseek64(pfatfs->stream, pfatfs->offset + offset, SEEK_SET))
		return 0;

	nread = fread(buf, 1, nbytes, pfatfs->stream);
	if (!ferror(pfatfs->stream))
		pfatfs->errnum = FAT_ERR_SUCCESS;

	return nread;
}

/* write nbytes to offset */
static size_t
fatfs_write_to_offset(fatfs_t *pfatfs, void *buf, size_t nbytes, fatoff_t offset)
{
	size_t nread = 0;
	pfatfs->errnum = FAT_ERR_IO;

	/* check negative */
	if (offset < 0)
		return 0;
	/* check wraparound */
	if (((fatoff_t)(offset + nbytes)) < 0)
		return 0;
	/* check volume bounds */
	if ((fatoff_t)(offset + nbytes) > pfatfs->volsize)
		return 0;

	if (_fseek64(pfatfs->stream, pfatfs->offset + offset, SEEK_SET))
		return 0;

	nread = fwrite(buf, 1, nbytes, pfatfs->stream);
	if (!ferror(pfatfs->stream))
		pfatfs->errnum = FAT_ERR_SUCCESS;

	return nread;
}

static inline int
fatfs_isvalid_cluster(fatfs_t *pfatfs, fatclus_t cluster)
{
	if ((cluster < 2) || (cluster > pfatfs->max_cluster_num))
		return 0;

	return 1;
}

static fatclus_t
fatfs_read_fat12(fatfs_t *pfatfs, fatclus_t cluster)
{
	int16_t value = 0;

	if (fatfs_read_from_offset(pfatfs, &value, sizeof(value),
		pfatfs->fat_active_off + cluster + (cluster / 2)) < sizeof(value))
		return INVALID_CLUSTER;

	return (cluster & 1) ? (value >> 4) : (value & 0xfff);
}

static int
fatfs_write_fat12(fatfs_t *pfatfs, fatclus_t cluster, fatclus_t value)
{
	int16_t entry = 0;
	value &= 0xfff;

	/* read entry */
	if (fatfs_read_from_offset(pfatfs, &entry, sizeof(entry),
		pfatfs->fat_active_off + cluster + (cluster / 2)) < sizeof(entry))
		return -1;

	/* update value */
	if (cluster & 1)
		entry = (entry & 0xf) | (value << 4);
	else
		entry = (entry & 0xf000) | value;

	/* update all fats  */
	for (uint8_t i = 0; i < pfatfs->fat_num; i++) {
		fatoff_t fatoff = pfatfs->fat_first_off + (i * pfatfs->fat_size_bytes);

		if (fatfs_write_to_offset(pfatfs, &entry, sizeof(entry),
			fatoff + cluster + (cluster / 2)) < sizeof(entry))
			return -1;
	}

	return 0;
}

static fatclus_t
fatfs_read_fat16(fatfs_t *pfatfs, fatclus_t cluster)
{
	int16_t value = 0;

	if (fatfs_read_from_offset(pfatfs, &value, sizeof(value),
		pfatfs->fat_active_off + (cluster * 2)) < sizeof(value))
		return INVALID_CLUSTER;

	return value;
}

static int
fatfs_write_fat16(fatfs_t *pfatfs, fatclus_t cluster, fatclus_t value)
{
	int16_t entry = (int16_t) value;

	/* update all fats  */
	for (uint8_t i = 0; i < pfatfs->fat_num; i++) {
		fatoff_t fatoff = pfatfs->fat_first_off + (i * pfatfs->fat_size_bytes);

		if (fatfs_write_to_offset(pfatfs, &entry, sizeof(entry),
			fatoff + (cluster * 2)) < sizeof(entry))
			return -1;
	}

	return 0;
}

static fatclus_t
fatfs_read_fat32(fatfs_t *pfatfs, fatclus_t cluster)
{
	int32_t value = 0;

	if (fatfs_read_from_offset(pfatfs, &value, sizeof(value),
		pfatfs->fat_active_off + (cluster * 4)) < sizeof(value))
		return INVALID_CLUSTER;

	return value & 0x0fffffff;
}

static int
fatfs_write_fat32(fatfs_t *pfatfs, fatclus_t cluster, fatclus_t value)
{
	int32_t entry = (int32_t) value;

	/* update all fats  */
	for (uint8_t i = 0; i < pfatfs->fat_num; i++) {
		fatoff_t fatoff = pfatfs->fat_first_off + (i * pfatfs->fat_size_bytes);

		if (fatfs_write_to_offset(pfatfs, &entry, sizeof(entry),
			fatoff + (cluster * 4)) < sizeof(entry))
			return -1;
	}

	return 0;
}

static fatclus_t
fatfs_safe_readfat(fatfs_t *pfatfs, fatclus_t cluster)
{
	fatclus_t nextcluster;

	/* validate cluster */
	if (!fatfs_isvalid_cluster(pfatfs, cluster))
		return INVALID_CLUSTER;

	nextcluster = pfatfs->readfat(pfatfs, cluster);

	/* validate cluster */
	if (!fatfs_isvalid_cluster(pfatfs, nextcluster))
		return INVALID_CLUSTER;

	return nextcluster;
}

static int
fatfs_safe_writefat(fatfs_t *pfatfs, fatclus_t cluster, fatclus_t value)
{
	/* validate cluster */
	if (!fatfs_isvalid_cluster(pfatfs, cluster))
		return -1;

	return pfatfs->writefat(pfatfs, cluster, value);
}

static fatclus_t
readfatbuf_12(void *data, size_t size, fatclus_t cluster)
{
	int16_t value;
	int8_t *d = (int8_t *) data;

	if (((ssize_t) size) < 0)
		return INVALID_CLUSTER;

	if (cluster + (cluster / 2) > (fatoff_t) size)
		return INVALID_CLUSTER;

	value = *((int16_t *)(d + cluster + (cluster / 2)));
	value = (cluster & 1) ? (value >> 4) : (value & 0xfff);
	return value;
}

static fatclus_t
readfatbuf_16(void *data, size_t size, fatclus_t cluster)
{
	int16_t *d = (int16_t *) data;

	if (((ssize_t) size) < 0)
		return INVALID_CLUSTER;

	if ((cluster + 1) * 2 > (fatoff_t) size)
		return INVALID_CLUSTER;

	return d[cluster];
}

static fatclus_t
readfatbuf_32(void *data, size_t size, fatclus_t cluster)
{
	int32_t *d = (int32_t *) data;

	if (((ssize_t) size) < 0)
		return INVALID_CLUSTER;

	if ((cluster + 1) * 4 > (fatoff_t) size)
		return INVALID_CLUSTER;

	return (d[cluster] & 0x0fffffff);
}

static fatoff_t
fatfs_clus2off(fatfs_t *pfatfs, fatclus_t cluster)
{
	return pfatfs->data_start_off + ((cluster - 2) * pfatfs->bytes_per_cluster);
}

static int
fatfs_find_free_clusters(fatfs_t *pfatfs)
{
	#define FATBUFSZ 516
	uint8_t fatbuf[FATBUFSZ];

	fatclus_t fatoff = pfatfs->fat_active_off;
	fatoff_t max = pfatfs->max_cluster_num;
	fatclus_t clusperbuf = (pfatfs->type == FAT_TYPE_32) ? (FATBUFSZ / 4) :
	                       (pfatfs->type == FAT_TYPE_16) ? (FATBUFSZ / 2) :
                           ((FATBUFSZ * 2) / 3);

	for (fatoff_t i = 0; i < (pfatfs->fat_size_bytes / FATBUFSZ); i++) {
		fatfs_read_from_offset(pfatfs, fatbuf, FATBUFSZ, fatoff + (FATBUFSZ*i));

		/* check err */
		if (pfatfs->errnum)
			return -1;

		/* loop around clusters in fatbuf  */
		for (fatclus_t j = 0; (j < clusperbuf) && (max >= 0); j++, max--) {
			fatclus_t next = pfatfs->readfatbuf(fatbuf, FATBUFSZ, j);

			/* skip used */
			if (next)
				continue;

			/* set first free */
			if (!pfatfs->first_free_cluster)
				pfatfs->first_free_cluster = (i * clusperbuf) + j;

			pfatfs->num_of_free_clusters++;
		}
	}

	return 0;
}

static fatclus_t
fatfs_allocate_cluster(fatfs_t *pfatfs)
{
	fatclus_t nextfree;

	if (pfatfs->num_of_free_clusters == 0) {
		pfatfs->errnum = FAT_ERR_FULLDISK;
		return INVALID_CLUSTER;
	}

	nextfree = pfatfs->first_free_cluster;
	pfatfs->num_of_free_clusters--;
	pfatfs->first_free_cluster = INVALID_CLUSTER;

	/* find next free */
	for (fatclus_t num = nextfree + 1; num <= pfatfs->max_cluster_num; num++) {
		fatclus_t next = pfatfs->readfat(pfatfs, num);

		if (next == 0) {
			pfatfs->first_free_cluster = num;
			break;
		}
	}

	/* no free ahead, reset values */
	if (pfatfs->first_free_cluster == INVALID_CLUSTER)
		fatfs_find_free_clusters(pfatfs);

	return nextfree;
}

static int
fatfs_release_cluster(fatfs_t *pfatfs, fatclus_t cluster)
{
	return fatfs_safe_writefat(pfatfs, cluster, 0);
}

static int
fatfs_link_cluster(fatfs_t *pfatfs, fatclus_t cluster, fatclus_t clus2link)
{
	/* avoid to write an arbitrary value */
	if (clus2link != END_OF_FILE) {
		if (!fatfs_isvalid_cluster(pfatfs, clus2link))
			return -1;
	}

	return fatfs_safe_writefat(pfatfs, cluster, clus2link);
}

static int
fatfs_goto_next_block(fatfs_t *pfatfs, fatblock_t *pblock)
{
	pblock->cluster = fatfs_safe_readfat(pfatfs, pblock->cluster);
	if (pblock->cluster == INVALID_CLUSTER)
		return -1;

	pblock->curoff = fatfs_clus2off(pfatfs, pblock->cluster);
	pblock->endoff = pblock->curoff + pfatfs->bytes_per_cluster;
	pblock->index++;
	return 0;
}

/* read nbytes from fatblock_t */
static size_t
fatfs_read_from_block(fatfs_t *pfatfs, void *buf, size_t nbytes,
                      fatblock_t *pblock)
{
	size_t total_read = 0;

	while ((total_read < nbytes)) {

		/* calc current slice */
		size_t slice_size = pblock->endoff - pblock->curoff;
		if (slice_size > (nbytes - total_read))
			slice_size = (nbytes - total_read);

		/* read */
		size_t nread = fatfs_read_from_offset(pfatfs, (char *)buf + total_read,
		                                      slice_size, pblock->curoff);

		total_read += nread;
		if (pfatfs->errnum)
			break;

		/* inc offset */
		pblock->curoff += nread;

		/* if necessary, goto next block */
		if (pblock->curoff == pblock->endoff) {
			if (fatfs_goto_next_block(pfatfs, pblock) < 0)
				break;
		}
	}

	return total_read;
}

static inline int
fatfs_advance_block(fatfs_t *pfatfs, fatblock_t *pblock)
{
	fatclus_t current = pblock->cluster;

	/* go to next block */
	if (fatfs_goto_next_block(pfatfs, pblock) < 0) {
		pblock->cluster = current;

		/* if no more blocks, allocate one */
		fatclus_t newcluster = fatfs_allocate_cluster(pfatfs);

		/* volume is full */
		if (newcluster == INVALID_CLUSTER)
			return -1;

		/* set EOF */
		if (fatfs_link_cluster(pfatfs, newcluster, END_OF_FILE))
			return -1;

		/* link one another */
		if (fatfs_link_cluster(pfatfs, pblock->cluster, newcluster))
			return -1;

		/* go to new block */
		if (fatfs_goto_next_block(pfatfs, pblock) < 0)
			return -1;
	}

	return 0;
}

/* write nbytes to fatblock_t */
static size_t
fatfs_write_to_block(fatfs_t *pfatfs, void *buf, size_t nbytes,
                     fatblock_t *pblock)
{
	size_t total_write = 0;

	while ((total_write < nbytes)) {

		/* no more bytes in this block  */
		if (pblock->curoff == pblock->endoff) {
			if (fatfs_advance_block(pfatfs, pblock))
				break;
		}

		/* calc current slice */
		size_t slice_size = pblock->endoff - pblock->curoff;
		if (slice_size > (nbytes - total_write))
			slice_size = (nbytes - total_write);

		/* write */
		size_t nwrite = fatfs_write_to_offset(pfatfs, (char *)buf + total_write,
		                                      slice_size, pblock->curoff);

		total_write += nwrite;
		if (pfatfs->errnum)
			break;

		/* inc offset */
		pblock->curoff += nwrite;
	}

	return total_write;
}

/* read privdirent from fatblock_t */
static int
privdirent_read_from_block(fatfs_t *pfatfs, struct privdirent *pprivdir,
                           fatblock_t *pblock)
{
	size_t size = sizeof(*pprivdir);
	if (fatfs_read_from_block(pfatfs, pprivdir, size, pblock) != size)
		return -1;

	return 0;
}

static void
fatfs_fatblock_init(fatfs_t *pfatfs, fatblock_t *pblock, fatclus_t clsfirst)
{
	pblock->curoff = fatfs_clus2off(pfatfs, clsfirst);
	pblock->endoff = pblock->curoff + pfatfs->bytes_per_cluster;
	pblock->cluster = clsfirst;
	pblock->clsinit = clsfirst;
	pblock->index = 0;
}

static inline fatoff_t
fatfs_block_get_startoff(fatfs_t *pfatfs, fatblock_t *pblock)
{
	fatoff_t startoff = pblock->endoff - pfatfs->bytes_per_cluster;

	/* root directory for fat12/16 */
	if ((pblock->cluster == INVALID_CLUSTER) &&
		(pblock->index == 0)) {
		startoff = pfatfs->fat_first_off;
		startoff += pfatfs->fat_num * pfatfs->fat_size_bytes;
	}

	return startoff;
}

static int
fatfs_decrement_block_offset(fatfs_t *pfatfs, fatblock_t *pblock, fatoff_t off)
{
	fatoff_t startoff = fatfs_block_get_startoff(pfatfs, pblock);

	/* max decrement is one block */
	if (off > pfatfs->bytes_per_cluster)
		return -1;
	/* if block is on chain, initial cluster must be valid */
	if (pblock->clsinit != INVALID_CLUSTER) {
		if (!fatfs_isvalid_cluster(pfatfs, pblock->clsinit))
			return -1;
	}

	/* offset in the previous block */
	if ((pblock->curoff - off) < startoff) {
		if (pblock->index == 0)
			return -1;

		/* find the previous cluster number */
		fatclus_t clsnum = pblock->clsinit;
		for (fatoff_t i = 0; i < pblock->index - 1; i++) {
			clsnum = fatfs_safe_readfat(pfatfs, clsnum);
			if (clsnum == INVALID_CLUSTER)
				return -1;
		}

		off += startoff - pblock->curoff;

		/* update block */
		pblock->index--;
		pblock->cluster = clsnum;
		pblock->endoff = fatfs_clus2off(pfatfs, clsnum);
		pblock->endoff += pfatfs->bytes_per_cluster;
		pblock->curoff = pblock->endoff;
	}

	pblock->curoff -= off;
	return 0;
}

/* write privdirent to fatblock_t */
/*
static int
privdirent_write_to_block(fatfs_t *pfatfs, struct privdirent *pprivdir,
                          fatblock_t *pblock)
{
	size_t size = sizeof(*pprivdir);
	if (fatfs_write_to_block(pfatfs, pprivdir, size, pblock) != size)
		return -1;

	return 0;
}
*/

static int
check_cyclic_fat(fatfs_t *pfatfs, fatclus_t first_cluster)
{
	fatblock_t block;
	fatclus_t saved_cluster = 0;

	fatfs_fatblock_init(pfatfs, &block, first_cluster);

	/* follow every cluster number in the FAT chain */
	for (fatclus_t i = 0; i <= pfatfs->max_cluster_num; i++) {
		/* if some cluster number repeat, this is a cyclic chain */
		if (block.cluster == saved_cluster)
			return 1;

		/* from time to time save the current cluster */
		if (!(i & 0xff))
			saved_cluster = block.cluster;

		/* goto next */
		if (fatfs_goto_next_block(pfatfs, &block) < 0)
			return 0;
	}

	/* no end-of-file means cyclic chain */
	return 1;
}

static int
fatdirent_load_lfn(fatfs_t *pfatfs, struct fatdirent *pdirent,
                   fatblock_t *pblock)
{
	fatblock_t block;
	struct privdirent privdir;
	size_t max_lfn_entries = FAT_MAX_NAME / 13;
	wchar_t *pwsz = &pdirent->d_name[0];

	/* copy fatblock_t, avoid to change the caller structure */
	memcpy(&block, pblock, sizeof(block));

	/* initialize */
	memset(pdirent->d_name, 0, sizeof(pdirent->d_name));
	memset(&privdir, 0, sizeof(privdir));

	/* retrieve lfn entries */
	for (size_t ord = 1; ord <= max_lfn_entries; ord++) {
		if (fatfs_decrement_block_offset(pfatfs, &block, 64))
			return -1;

		/* read entry */
		if(privdirent_read_from_block(pfatfs, &privdir, &block))
			return -1;

		/* check attr */
		if (privdir.type.lfn.attribute != FAT_ATTR_LONG_NAME)
			return -1;
		/* check lfn ordinal */
		if ((privdir.type.lfn.ordinal != 0x40) &&
			((privdir.type.lfn.ordinal & (~0x40)) != ord))
			return -1;

		/* copy lfn */
		for (size_t i = 0; i < PRIVDIR_LFN_NAME1; i++)
			pwsz[i] = (wchar_t) privdir.type.lfn.name1[i];
		pwsz += PRIVDIR_LFN_NAME1;

		for (size_t i = 0; i < PRIVDIR_LFN_NAME2; i++)
			pwsz[i] = (wchar_t) privdir.type.lfn.name2[i];
		pwsz += PRIVDIR_LFN_NAME2;

		for (size_t i = 0; i < PRIVDIR_LFN_NAME3; i++)
			pwsz[i] = (wchar_t) privdir.type.lfn.name3[i];
		pwsz += PRIVDIR_LFN_NAME3;

		/* if last, break */
		if (privdir.type.lfn.ordinal & 0x40)
			break;
	}

	return 0;
}

static inline void
fatdirent_load_lfn_from_8dot3(struct fatdirent *pdirent,
                              struct privdirent *pprivdir)
{
	memset(pdirent->d_name, 0, sizeof(pdirent->d_name));
	wchar_t *pwsz = &pdirent->d_name[0];

	/* copy name */
	for (int i = 0; i < 8; i++) {
		if (pprivdir->type.gen.name_8dot3[i] == ' ')
			break;
		*pwsz = (wchar_t) pprivdir->type.gen.name_8dot3[i];
		pwsz++;
	}

	/* copy ext */
	if (pprivdir->type.gen.name_8dot3[8] != ' ') {
		*pwsz = (wchar_t) '.';
		pwsz++;

		for (int i = 8; i < 11; i++) {
			if (pprivdir->type.gen.name_8dot3[i] == ' ')
				break;
			*pwsz = pprivdir->type.gen.name_8dot3[i];
			pwsz++;
		}
	}
}

/* read fatdirent from fatblock_t */
static int
fatdirent_read_from_block(fatfs_t *pfatfs, struct fatdirent *pdirent,
                          fatblock_t *pblock)
{
	fatblock_t block;
	struct privdirent privdir;
	fatclus_t first_cluster = 0;

	memset(&privdir, 0, sizeof(privdir));

	while (1) {
		/* read next entry */
		if(privdirent_read_from_block(pfatfs, &privdir, pblock))
			return -1;

		first_cluster = ((privdir.type.gen.first_cluster_high << 16) |
			privdir.type.gen.first_cluster_low);

		/* no more entries */
		if (privdir.type.gen.name_8dot3[0] == 0x00)
			return -1;

		/* skip deleted entry */
		if (privdir.type.gen.name_8dot3[0] == 0xe5)
			continue;

		/* skip invalid */
		if (!fatfs_isvalid_cluster(pfatfs, first_cluster)) {
			/* empty files may have an invalid cluster number */
			if ((privdir.type.gen.file_size == 0) &&
				(privdir.type.gen.attribute & FAT_ATTR_ARCHIVE))
				break;

			continue;
		}

		/* file */
		if (privdir.type.gen.attribute & FAT_ATTR_ARCHIVE)
			break;

		/* directory */
		if (privdir.type.gen.attribute & FAT_ATTR_DIRECTORY)
			break;
	}

	/* copy block before decrement */
	memcpy(&block, pblock, sizeof(fatblock_t));
	if (fatfs_decrement_block_offset(pfatfs, &block, 32))
		return -1;

	pdirent->d_privoff = block.curoff;
	pdirent->d_cluster = first_cluster;
	pdirent->d_size = privdir.type.gen.file_size;
	pdirent->d_type = (privdir.type.gen.attribute & FAT_ATTR_DIRECTORY) ?
		FAT_TYPE_DIRECTORY : FAT_TYPE_ARCHIVE;

	/* load long name, skip '.', '..' */
	if (memcmp(privdir.type.gen.name_8dot3, ". ", 2) &&
		memcmp(privdir.type.gen.name_8dot3, ".. ", 3)) {
		if (!fatdirent_load_lfn(pfatfs, pdirent, pblock))
			return 0;
	}

	/* use 8dot3 */
	fatdirent_load_lfn_from_8dot3(pdirent, &privdir);
	return 0;
}

static int
fatdirent_find_entry(fatfs_t *pfatfs, struct fatdirent *pdirent,
                     fatblock_t *pblock,
                     const wchar_t *pwszname)
{
	/* search every entry from fatblock_t */
	while (!fatdirent_read_from_block(pfatfs, pdirent, pblock)) {
		/* compare name */
		if (!wcsncmp(pdirent->d_name, pwszname, sizeof(pdirent->d_name))) {
			/* entry found, check the fat chain */
			if (check_cyclic_fat(pfatfs, pdirent->d_cluster)) {
				pfatfs->errnum = FAT_ERR_LOOP;
				return -1;
			}

			return 0;
		}
	}

	return -1;
}

static void
split_path(wchar_t *path, wchar_t **ppdir, wchar_t **ppfile)
{
	wchar_t *dirpart, *filepart;

	/* find the last slash */
	dirpart = path;
	filepart = path + wcslen(path) - 1;
	while (filepart > dirpart) {
		if (*filepart == (wchar_t) '/') {
			*filepart = (wchar_t) '\0';
			filepart++;
			break;
		}

		filepart--;
	}

	/* filename on root directory */
	if (dirpart == filepart) {
		dirpart = L"/";
		if (*filepart == (wchar_t) '/')
			filepart++;
	}

	*ppdir = dirpart;
	*ppfile = (*filepart) ? (filepart) : NULL;
}

static void
fatfs_set_label_from_char(fatfs_t *pfatfs, char *label)
{
	pfatfs->label = calloc(1, (strlen(label) + 1) * sizeof(wchar_t));
	for (size_t i = 0; i < strlen(label); i++)
		pfatfs->label[i] = (wchar_t) label[i];
}

static inline void
remove_trailing_spaces_char(char *s)
{
	char *p = s + strlen(s) - 1;

	while (p > s) {
		if (p[0] != ' ')
			break;
		p[0] = '\0';
		p--;
	}
}

static int
fatfs_parse_bpb(fatfs_t *pfatfs)
{
	struct fat_bpb bpb;
	size_t nread;
	char label[12];

	memset(label, 0, sizeof(label));

	/* set offset */
	if (_fseek64(pfatfs->stream, pfatfs->offset, SEEK_SET))
		return -1;

	/* read bpb */
	nread = fread(&bpb, 1, sizeof(bpb), pfatfs->stream);
	if (ferror(pfatfs->stream) || (nread < sizeof(bpb)))
		return -1;

	/* check */
	if ((bpb.bytes_per_sector != 512)
		&& (bpb.bytes_per_sector != 1024)
		&& (bpb.bytes_per_sector != 2048)
		&& (bpb.bytes_per_sector != 4096))
		return -1;

	/* invalid value */
	if (!bpb.sectors_per_cluster)
		return -1;
	/* invalid value */
	if ((bpb.num_root_entries * 32) % bpb.bytes_per_sector)
		return -1;
	/* max count when fat mirroring is disabled */
	if (bpb.num_fats > 0xf)
		return -1;
	/* one of these must be set */
	if(!bpb.num_total_sectors_16 && !bpb.num_total_sectors_32)
		return -1;

	pfatfs->bytes_per_cluster = bpb.bytes_per_sector * bpb.sectors_per_cluster;
	pfatfs->volsize = (bpb.num_total_sectors_16) ? bpb.num_total_sectors_16 :
		bpb.num_total_sectors_32;
	pfatfs->volsize *= bpb.bytes_per_sector;

	/* check volsize */
	if ((pfatfs->offset + pfatfs->volsize) < 0)
		return -1;

	/* offset to first fat */
	pfatfs->fat_first_off = bpb.num_reserved_sectors * bpb.bytes_per_sector;
	pfatfs->fat_active_off = pfatfs->fat_first_off;
	pfatfs->fat_num = bpb.num_fats;

	/* fat32 */
	if (!bpb.sectors_per_fat_16) {
		pfatfs->type = FAT_TYPE_32;
		pfatfs->writefat = fatfs_write_fat32;
		pfatfs->readfat = fatfs_read_fat32;
		pfatfs->readfatbuf = readfatbuf_32;

		pfatfs->fat_size_bytes = bpb.specific.fat_32.sectors_per_fat_32 *
			bpb.bytes_per_sector;
		pfatfs->data_start_off = pfatfs->fat_first_off + (pfatfs->fat_num *
			pfatfs->fat_size_bytes);

		/* support fat32 without mirroring */
		if (bpb.specific.fat_32.extended_flags & 0x80) {
			if ((bpb.specific.fat_32.extended_flags & 0xf) >= bpb.num_fats)
				return -1;
			/* adjust active fat */
			pfatfs->fat_active_off += pfatfs->fat_size_bytes *
				(bpb.specific.fat_32.extended_flags & 0xf);
		}

		pfatfs->max_cluster_num = ((pfatfs->volsize - pfatfs->data_start_off) /
			pfatfs->bytes_per_cluster) + 1;

		/* validate root dir cluster */
		if (((fatclus_t)bpb.specific.fat_32.root_cluster < 2) ||
			((fatclus_t)bpb.specific.fat_32.root_cluster > pfatfs->max_cluster_num))
			return -1;

		/* check the fat chain (root) */
		if (check_cyclic_fat(pfatfs, bpb.specific.fat_32.root_cluster)) {
			pfatfs->errnum = FAT_ERR_LOOP;
			return -1;
		}

		fatfs_fatblock_init(pfatfs, &pfatfs->root_block,
		                    bpb.specific.fat_32.root_cluster);

		memcpy(label, bpb.specific.fat_32.label, sizeof(label) - 1);

	/* fat12, 16 */
	} else {
		pfatfs->type = FAT_TYPE_12;
		pfatfs->writefat = fatfs_write_fat12;
		pfatfs->readfat = fatfs_read_fat12;
		pfatfs->readfatbuf = readfatbuf_12;

		/* only case where the block is outside the file allocation table */
		pfatfs->fat_size_bytes = bpb.sectors_per_fat_16 * bpb.bytes_per_sector;
		pfatfs->root_block.cluster = INVALID_CLUSTER;
		pfatfs->root_block.clsinit = INVALID_CLUSTER;
		pfatfs->root_block.curoff = pfatfs->fat_first_off  +
			(pfatfs->fat_num * pfatfs->fat_size_bytes);
		pfatfs->root_block.endoff = pfatfs->root_block.curoff +
			bpb.num_root_entries * 32;
		pfatfs->root_block.index = 0;
		pfatfs->data_start_off = pfatfs->root_block.endoff;

		/* validate root dir offset */
		if ((pfatfs->root_block.curoff > pfatfs->volsize) ||
			(pfatfs->root_block.endoff >= pfatfs->volsize))
			return -1;

		pfatfs->max_cluster_num = ((pfatfs->volsize - pfatfs->data_start_off) /
			pfatfs->bytes_per_cluster) + 1;

		if (pfatfs->max_cluster_num > 4085) {
			pfatfs->type = FAT_TYPE_16;
			pfatfs->writefat = fatfs_write_fat16;
			pfatfs->readfat = fatfs_read_fat16;
			pfatfs->readfatbuf = readfatbuf_16;
		}

		memcpy(label, bpb.specific.fat_12_16.label, sizeof(label) - 1);
	}

	pfatfs->label = 0;
	remove_trailing_spaces_char(label);
	fatfs_set_label_from_char(pfatfs, label);
	return 0;
}

int
fat_mount(fatfs_t **ppfatfs, const char *filename, fatoff_t offset)
{
	int errnum;
	fatfs_t *pfatfs;

	/* sanity check */
	if (!ppfatfs || !filename || (offset < 0))
		return FAT_ERR_INVAL;

	/* open file */
	FILE *stream = fopen(filename, "r+b");
	if (!stream) {
		if (errno == EACCES)
			return FAT_ERR_ACCESS;

		else if (errno == EBUSY)
			return FAT_ERR_DEVBUSY;

		return FAT_ERR_IO;
	}

	/* alloc new fatfs */
	pfatfs = (fatfs_t *) calloc(1, sizeof(fatfs_t));
	if (!pfatfs) {
		fclose(stream);
		return FAT_ERR_ENOMEM;
	}

	pfatfs->stream = stream;
	pfatfs->offset = offset;

	/* parse bpb */
	if (fatfs_parse_bpb(pfatfs)) {
		errnum = (pfatfs->errnum) ? pfatfs->errnum : FAT_ERR_NOTFATFS;
		fat_umount(pfatfs);
		return errnum;
	}

	/* find free clusters */
	if (fatfs_find_free_clusters(pfatfs)) {
		errnum = pfatfs->errnum;
		fat_umount(pfatfs);
		return errnum;
	}

	*ppfatfs = pfatfs;
	return FAT_ERR_SUCCESS;
}

void
fat_umount(fatfs_t *pfatfs)
{
	if (pfatfs) {
		fclose(pfatfs->stream);
		free(pfatfs->label);
		free(pfatfs);
	}
}

wchar_t *
fat_getlabel(fatfs_t *pfatfs)
{
	return (pfatfs) ? (pfatfs->label) : NULL;
}

int
fat_error(fatfs_t *pfatfs)
{
	return (pfatfs) ? (pfatfs->errnum) : 0;
}

fatdir_t *
fat_opendir(fatfs_t *pfatfs, const wchar_t *path)
{
	fatblock_t block;
	wchar_t *pwsz, *curdir, *nextslash, *maxptr;
	struct fatdirent fatdirent;
	fatdir_t *pfatdir = NULL;
	fatoff_t privoff = 0;

	if (!pfatfs)
		return NULL;

	if (!path) {
		pfatfs->errnum = FAT_ERR_INVAL;
		return NULL;
	}

	if (!wcslen(path)) {
		pfatfs->errnum = FAT_ERR_NOENT;
		return NULL;
	}

	/* copy root dir block */
	memcpy(&block, &pfatfs->root_block, sizeof(block));
	privoff = pfatfs->root_block.curoff;

	/* copy the name */
	pwsz = wcsdup(path);
	if (!pwsz) {
		pfatfs->errnum = FAT_ERR_ENOMEM;
		return NULL;
	}

	maxptr = pwsz + wcslen(pwsz);

	/* remove the initial slash, if any */
	curdir = pwsz;
	if (curdir[0] == (wchar_t) '/')
		curdir++;

	/* for all dir names */
	while ((curdir < maxptr) && (curdir >= pwsz)) {
		nextslash = wcschr(curdir, (wchar_t) '/');
		if (nextslash)
			*nextslash = (wchar_t) '\0';

		/* entry not found, free and ret */
		if (fatdirent_find_entry(pfatfs, &fatdirent, &block, curdir) < 0) {
			if (!pfatfs->errnum)
				pfatfs->errnum = FAT_ERR_NOENT;
			goto _free_and_ret;
		}

		/* entry is not dir, free and ret */
		if (fatdirent.d_type != FAT_TYPE_DIRECTORY) {
			pfatfs->errnum = FAT_ERR_NOTDIR;
			goto _free_and_ret;
		}

		/* dir found, update block */
		fatfs_fatblock_init(pfatfs, &block, fatdirent.d_cluster);

		privoff = fatdirent.d_privoff;
		curdir = nextslash + 1;
	}

	/* allocate memory for fatdir */
	pfatdir = calloc(1, sizeof(*pfatdir));
	if (!pfatdir) {
		pfatfs->errnum = FAT_ERR_ENOMEM;
		goto _free_and_ret;
	}

	/* set values */
	pfatdir->pfatfs = pfatfs;
	pfatdir->privoff = privoff;
	pfatdir->position = 0;
	memcpy(&pfatdir->block, &block, sizeof(block));
	pfatfs->errnum = FAT_ERR_SUCCESS;

_free_and_ret:
	free(pwsz);
	return pfatdir;
}

struct fatdirent *
fat_readdir(fatdir_t *pfatdir)
{
	if (pfatdir) {
		if (!fatdirent_read_from_block(pfatdir->pfatfs, &pfatdir->data,
		    &pfatdir->block)) {
			pfatdir->position++;
			pfatdir->pfatfs->errnum = FAT_ERR_SUCCESS;
			return &pfatdir->data;
		}
	}

	return NULL;
}

long
fat_telldir(fatdir_t *pfatdir)
{
	if (pfatdir) {
		pfatdir->pfatfs->errnum = FAT_ERR_SUCCESS;
		return pfatdir->position;
	}

	return -1;
}

void
fat_seekdir(fatdir_t *pfatdir, long loc)
{
	if (!pfatdir)
		return;

	if (loc < 0) {
		pfatdir->pfatfs->errnum = FAT_ERR_INVAL;
		return;
	}

	pfatdir->pfatfs->errnum = FAT_ERR_SUCCESS;
	fat_rewinddir(pfatdir);
	for (long i = 0; i < loc; i++)
		fat_readdir(pfatdir);
}

void
fat_rewinddir(fatdir_t *pfatdir)
{
	/* sanity check */
	if (!pfatdir)
		return;

	pfatdir->position = 0;
	pfatdir->block.cluster = pfatdir->block.clsinit;
	pfatdir->block.index = 0;

	/* generic rewind */
	if (fatfs_isvalid_cluster(pfatdir->pfatfs, pfatdir->block.cluster)) {
		fatfs_fatblock_init(pfatdir->pfatfs, &pfatdir->block,
		                    pfatdir->block.clsinit);
		return;
	}

	/* rewind on fat12,fat16 root dir */
	memcpy(&pfatdir->block, &pfatdir->pfatfs->root_block, sizeof(fatblock_t));
}

void
fat_closedir(fatdir_t *pfatdir)
{
	free(pfatdir);
}

int
fat_mkdir(fatfs_t *pfatfs, const wchar_t *path)
{
	// TODO: implement
	return FAT_ERR_NOTIMPL;
}

int
fat_rmdir(fatfs_t *pfatfs, const wchar_t *path)
{
	// TODO: implement
	return FAT_ERR_NOTIMPL;
}

static inline int
parse_fopen_mode(const char *mode, uint8_t *oflag_mode, uint8_t *create,
                 uint8_t *trunc)
{
	*oflag_mode = 0;
	*create = 0;
	*trunc = 0;

	switch (*mode) {
		case 'a':
			*create = 1;
			*oflag_mode = FAT_FILE_MODE_APPEND;
			if (mode[1] == '+')
				*oflag_mode |= FAT_FILE_MODE_READ;
			break;

		case 'r':
			*oflag_mode = FAT_FILE_MODE_READ;
			if (mode[1] == '+')
				*oflag_mode |= FAT_FILE_MODE_WRITE;
			break;

		case 'w':
			*oflag_mode = FAT_FILE_MODE_WRITE;
			*create = 1;
			*trunc = 1;

			if (mode[1] == '+')
				*oflag_mode |= FAT_FILE_MODE_READ;

			if ((mode[1] == 'x') || ((strlen(mode) > 2) && (mode[2] == 'x')))
				*trunc = 0;

			break;

		default:
			return -1;
	}

	return 0;
}

static inline int
fatfs_privdirent_update_size(fatfs_t *pfatfs, fatoff_t privoff, fatoff_t size)
{
	struct privdirent privdir;

	/* read old directory entry */
	if (fatfs_read_from_offset(pfatfs, &privdir, sizeof(privdir),
	                           privoff) !=  sizeof(privdir))
			return -1;

	// TODO: update the write time
	privdir.type.gen.file_size = size;

	/* update */
	if (fatfs_write_to_offset(pfatfs, &privdir, sizeof(privdir),
	                          privoff) !=  sizeof(privdir))
			return -1;

	return 0;
}

static inline int
fatfs_privdirent_update_cluster(fatfs_t *pfatfs, fatoff_t privoff, fatclus_t cl)
{
	struct privdirent privdir;

	/* read old directory entry */
	if (fatfs_read_from_offset(pfatfs, &privdir, sizeof(privdir),
	                           privoff) !=  sizeof(privdir))
			return -1;

	// TODO: update the write time
	privdir.type.gen.first_cluster_low = (uint16_t) cl;
	privdir.type.gen.first_cluster_high = (cl >> 16);

	/* update */
	if (fatfs_write_to_offset(pfatfs, &privdir, sizeof(privdir),
	                          privoff) !=  sizeof(privdir))
			return -1;

	return 0;
}

static inline int
fatfs_fatfile_expand(fatfile_t *pfatfile, fatoff_t length)
{
	int error;
	size_t expsize, nwrite, zbsize;
	uint8_t zerobuf[2048];
	fatblock_t block;

	zbsize = sizeof(zerobuf);
	expsize = (size_t) (length - pfatfile->filesize);
	memset(zerobuf, 0, zbsize);

	/* go to end */
	if (fat_fseek(pfatfile, 0, FAT_SEEK_END))
		return -1;

	/* if file is empty */
	if (pfatfile->filesize == 0) {
		/* allocate one block */
		fatclus_t newclus = fatfs_allocate_cluster(pfatfile->pfatfs);
		if (newclus == INVALID_CLUSTER)
			return -1;

		/* set EOF */
		if (fatfs_link_cluster(pfatfile->pfatfs, newclus, END_OF_FILE))
			return -1;

		/* update privdir */
		if (fatfs_privdirent_update_cluster(pfatfile->pfatfs, pfatfile->privoff,
		                                    newclus))
			return -1;

		fatfs_fatblock_init(pfatfile->pfatfs, &pfatfile->block, newclus);
	}

	/* preserve the caller block */
	memcpy(&block, &pfatfile->block, sizeof(block));

	/* write zeros */
	error = -1;
	while (expsize) {
		nwrite = fatfs_write_to_block(pfatfile->pfatfs, zerobuf,
		                              (zbsize >= expsize) ? expsize : zbsize,
		                              &pfatfile->block);
		if (pfatfile->pfatfs->errnum)
			goto _restore_block_and_ret;

		expsize -= nwrite;
	}

	error = 0;
_restore_block_and_ret:
	memcpy(&pfatfile->block, &block, sizeof(block));
	return error;
}

static inline int
fatfs_fatfile_shrink(fatfile_t *pfatfile, fatoff_t length)
{
	fatblock_t block;
	fatclus_t cluster, lastvalid;

	/* adjust file pointer */
	if (fat_fseek(pfatfile, length, FAT_SEEK_SET))
		return -1;

	/* caller block is set to the last eof */
	memcpy(&block, &pfatfile->block, sizeof(block));

	lastvalid = pfatfile->block.cluster;
	cluster = pfatfile->block.cluster;

	/* release all clusters ahead */
	while (fatfs_goto_next_block(pfatfile->pfatfs, &pfatfile->block) == 0) {
		if (fatfs_release_cluster(pfatfile->pfatfs, cluster))
			break;
		cluster = pfatfile->block.cluster;
	}

	/* release the last in the chain */
	if (cluster != lastvalid)
		fatfs_release_cluster(pfatfile->pfatfs, cluster);
	/* set new eof */
	fatfs_link_cluster(pfatfile->pfatfs, lastvalid, END_OF_FILE);

	/* restore block */
	memcpy(&pfatfile->block, &block, sizeof(block));
	if (length == 0) {
		/* invalidate */
		pfatfile->block.cluster = INVALID_CLUSTER;
		pfatfile->block.clsinit = INVALID_CLUSTER;
		pfatfile->block.curoff = 0;
		pfatfile->block.endoff = 0;
		pfatfile->block.index = 0;
		fatfs_release_cluster(pfatfile->pfatfs, lastvalid);

		/* update the first cluster */
		if (fatfs_privdirent_update_cluster(pfatfile->pfatfs, pfatfile->privoff,
		                                    INVALID_CLUSTER))
			return -1;
	}

	return 0;
}


static int
fatfile_truncate(fatfile_t *pfatfile, fatoff_t len)
{
	if (len == pfatfile->filesize)
		return 0;

	if (len > pfatfile->filesize) {
		if (fatfs_fatfile_expand(pfatfile, len))
			return -1;

	} else {
		if (fatfs_fatfile_shrink(pfatfile, len))
			return -1;
	}

	pfatfile->filesize = len;
	return fatfs_privdirent_update_size(pfatfile->pfatfs,pfatfile->privoff,len);
}

fatfile_t *
fat_fopen(fatfs_t *pfatfs, const wchar_t *path, const char *mode)
{
	struct fatdirent *dp;
	fatdir_t *pfatdir = NULL;
	fatfile_t *pfatfile = NULL;
	wchar_t *pwsz, *dirpart, *filepart;
	uint8_t oflag_mode, create, trunc;

	/* sanity checks */
	if (!pfatfs)
		return NULL;

	pfatfs->errnum = FAT_ERR_SUCCESS;
	if (!path || !mode) {
		pfatfs->errnum = FAT_ERR_INVAL;
		return NULL;
	}

	/* parse mode */
	if (parse_fopen_mode(mode, &oflag_mode, &create, &trunc)) {
		pfatfs->errnum = FAT_ERR_INVAL;
		return NULL;
	}

	/* copy path */
	pwsz = wcsdup(path);
	if (!pwsz) {
		pfatfs->errnum = FAT_ERR_ENOMEM;
		return NULL;
	}

	/* split path */
	split_path(pwsz, &dirpart, &filepart);

	/* open directory */
	pfatdir = fat_opendir(pfatfs, dirpart);
	if (!pfatdir)
		goto _free_and_ret;

	/* if path ends with slash */
	if (!filepart) {
		pfatfs->errnum = FAT_ERR_ISDIR;
		goto _free_and_ret;
	}

	/* for each directory entry */
	while ((dp = fat_readdir(pfatdir))) {
		/* check the name */
		if (!wcsncmp(filepart, dp->d_name, sizeof(dp->d_name))) {

			/* is dir, return err */
			if (dp->d_type == FAT_TYPE_DIRECTORY) {
				pfatfs->errnum = FAT_ERR_ISDIR;
				goto _free_and_ret;
			}

			pfatfile = calloc(1, sizeof(*pfatfile));
			if (!pfatfile) {
				pfatfs->errnum = FAT_ERR_ENOMEM;
				goto _free_and_ret;
			}

			/* init the file structure */
			pfatfile->pfatfs = pfatfs;
			pfatfile->privoff = dp->d_privoff;
			pfatfile->block.cluster = INVALID_CLUSTER;
			pfatfile->block.clsinit = INVALID_CLUSTER;
			pfatfile->mode = oflag_mode;
			pfatfile->filesize = dp->d_size;
			pfatfile->oversize = 0;

			/* if file is not empty, it has a valid block */
			if (dp->d_size)
				fatfs_fatblock_init(pfatfile->pfatfs, &pfatfile->block,
				                    dp->d_cluster);
		}
	}

	/* if err, return */
	if (pfatfs->errnum)
		goto _free_and_ret;

	if (!pfatfile) {
		//if (!create) {
			pfatfs->errnum = FAT_ERR_NOENT;
		//	goto _free_and_ret;
		//}

		// TODO: create file

	} else if (trunc) {
		if (fatfile_truncate(pfatfile, 0)) {
			fat_fclose(pfatfile);
			pfatfile = NULL;
		}
	}

_free_and_ret:
	free(pwsz);
	fat_closedir(pfatdir);
	return pfatfile;
}

size_t
fat_fread(void *buf, size_t size, size_t nitems, fatfile_t *pfatfile)
{
	size_t bytes_to_read = size * nitems;

	/* sanity check */
	if (!pfatfile)
		return 0;

	if (!buf || ((bytes_to_read < size) || (bytes_to_read < nitems))) {
		pfatfile->pfatfs->errnum = FAT_ERR_INVAL;
		return 0;
	}

	/* above max size */
	if (bytes_to_read > UINT_MAX) {
		pfatfile->pfatfs->errnum = FAT_ERR_MAXSIZE;
		return 0;
	}

	pfatfile->pfatfs->errnum = FAT_ERR_SUCCESS;
	if (!bytes_to_read)
		return 0;

	/* check mode */
	if ((pfatfile->mode & FAT_FILE_MODE_READ) == 0) {
		pfatfile->pfatfs->errnum = FAT_ERR_WRONLY;
		return 0;
	}

	/* check file bounds */
	if ((fat_ftell(pfatfile) + (fatoff_t) bytes_to_read) > pfatfile->filesize)
		bytes_to_read = pfatfile->filesize - fat_ftell(pfatfile);

	return fatfs_read_from_block(pfatfile->pfatfs, buf, bytes_to_read,
	                             &pfatfile->block);
}

size_t
fat_fwrite(void *buf, size_t size, size_t nitems, fatfile_t *pfatfile)
{
	size_t bytes_to_write = size * nitems;
	size_t nwrite;

	/* sanity check */
	if (!pfatfile)
		return 0;

	if (!buf || ((bytes_to_write < size) || (bytes_to_write < nitems))) {
		pfatfile->pfatfs->errnum = FAT_ERR_INVAL;
		return 0;
	}

	/* above max size */
	if (bytes_to_write > UINT_MAX) {
		pfatfile->pfatfs->errnum = FAT_ERR_MAXSIZE;
		return 0;
	}

	pfatfile->pfatfs->errnum = FAT_ERR_SUCCESS;
	if (!bytes_to_write)
		return 0;

	/* check mode */
	if ((pfatfile->mode & (FAT_FILE_MODE_WRITE | FAT_FILE_MODE_APPEND)) == 0) {
		pfatfile->pfatfs->errnum = FAT_ERR_RDONLY;
		return 0;
	}

	if (pfatfile->mode & FAT_FILE_MODE_APPEND) {
		if(fat_fseek(pfatfile, 0, FAT_SEEK_END))
			return 0;
	}

	/* if necessary, commit out-of-bounds size */
	if (pfatfile->oversize) {
		if (fatfile_truncate(pfatfile, pfatfile->filesize + pfatfile->oversize))
			return 0;

		/* truncate does not update the file pointer
		   we should update it if mode != append */
		if ((pfatfile->mode & FAT_FILE_MODE_APPEND) == 0) {
			if(fat_fseek(pfatfile, 0, FAT_SEEK_END))
				return 0;
		}

	/* file is empty, allocate one block */
	} else if (pfatfile->filesize == 0) {
		if (fatfile_truncate(pfatfile, 1))
			return 0;
	}

	/* write bytes */
	nwrite = fatfs_write_to_block(pfatfile->pfatfs, buf, bytes_to_write,
	                              &pfatfile->block);

	/* if necessary, adjust filesize */
	fatoff_t curoff = fat_ftell(pfatfile);
	if (curoff > pfatfile->filesize) {
		fatfs_privdirent_update_size(pfatfile->pfatfs,pfatfile->privoff,curoff);
		pfatfile->filesize = curoff;
	}

	return nwrite;
}

int
fat_fseek(fatfile_t *pfatfile, fatoff_t offset, int whence)
{
	/* check */
	if (!pfatfile)
		return -1;

	/* adjust offset */
	pfatfile->pfatfs->errnum = FAT_ERR_SUCCESS;
	if (whence == FAT_SEEK_END)
		offset += pfatfile->filesize;
	else if (whence == FAT_SEEK_CUR)
		offset += fat_ftell(pfatfile);
	else if (whence != FAT_SEEK_SET) {
		pfatfile->pfatfs->errnum = FAT_ERR_INVAL;
		return -1;
	}

	/* check negative */
	if (offset < 0) {
		pfatfile->pfatfs->errnum = FAT_ERR_INVAL;
		return -1;
	}

	/* ensure block is on cluster chain */
	if (pfatfile->block.cluster != INVALID_CLUSTER) {
		/* rewind file */
		fatfs_fatblock_init(pfatfile->pfatfs, &pfatfile->block,
		                    pfatfile->block.clsinit);
		pfatfile->oversize = 0;

		/* advance blocks */
		fatoff_t nblks = (offset / pfatfile->pfatfs->bytes_per_cluster) - 1;
		if (offset > pfatfile->filesize) {
			nblks = (pfatfile->filesize / pfatfile->pfatfs->bytes_per_cluster);
			nblks -= 1;
		}

		for (fatoff_t i = 0; i < nblks; i++) {
			if (fatfs_goto_next_block(pfatfile->pfatfs, &pfatfile->block))
				break;
		}

		/* if err, return */
		if (pfatfile->pfatfs->errnum)
			return -1;

		/* advance offset */
		if (offset <= pfatfile->filesize) {
			pfatfile->block.curoff += offset - fat_ftell(pfatfile);
		} else
			pfatfile->block.curoff += pfatfile->filesize - fat_ftell(pfatfile);
	}

	pfatfile->oversize = offset - fat_ftell(pfatfile);
	return 0;
}

fatoff_t
fat_ftell(fatfile_t *pfatfile)
{
	fatoff_t offset = 0;

	if (!pfatfile)
		return -1;

	/* ensure block is on cluster chain */
	if (pfatfile->block.cluster != INVALID_CLUSTER) {
		offset = pfatfile->pfatfs->bytes_per_cluster;
		offset += pfatfile->block.curoff - pfatfile->block.endoff;
		offset += pfatfile->block.index * pfatfile->pfatfs->bytes_per_cluster;
		offset += pfatfile->oversize;
	}

	return offset;
}

void
fat_fclose(fatfile_t *pfatfile)
{
	free(pfatfile);
}

int
fat_truncate(fatfs_t *pfatfs, const wchar_t *filepath, fatoff_t length)
{
	int error = 0;
	fatfile_t *pfatfile;

	if (!pfatfs)
		return -1;

	if (!filepath || (length < 0)) {
		pfatfs->errnum = FAT_ERR_INVAL;
		return -1;
	}

	pfatfile = fat_fopen(pfatfs, filepath, "r+");
	if (!pfatfile)
		return -1;

	error = fatfile_truncate(pfatfile, length);
	fat_fclose(pfatfile);
	return error;
}

int
fat_unlink(fatfs_t *pfatfs, const wchar_t *path)
{
	return FAT_ERR_NOTIMPL;
}


