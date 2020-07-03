/*
 * fat_truncate_t.c
 * functions: fat_mount, fat_umount, fat_getlabel, fat_fopen, fat_fclose,
 *            fat_error, fat_fseek, fat_ftell, fat_truncate
 */

#include "fat.h"
#include <stdio.h>
#include <stdlib.h>

#define FIRSTFILE  L"/FIRST.txt"
#define SECONDFILE L"Second_File_Using_Long_Name.txt"

static fatoff_t
fat_get_filesize(fatfs_t *pfatfs, const wchar_t *filepath)
{
	fatoff_t offset = 0;
	fatfile_t *pfatfile = fat_fopen(pfatfs, filepath, "r");
	if (fat_fseek(pfatfile, 0, FAT_SEEK_END)) {
		fprintf(stderr, "fat_get_filesize: fat_fseek: error=%d\n",
		        fat_error(pfatfs));
		return -1;
	}

	offset = fat_ftell(pfatfile);
	fat_fclose(pfatfile);
	return offset;
}

static int
test_truncate(fatfs_t *pfatfs, const wchar_t *filepath)
{

	fprintf(stderr, "%ls: filesize=%lld\n", filepath,
	        fat_get_filesize(pfatfs, filepath));

	/* truncate */
	if (fat_truncate(pfatfs, filepath, 0)) {
		fprintf(stderr, "fat_truncate: error=%d\n", fat_error(pfatfs));
		return -1;
	}

	fprintf(stderr, "%ls: filesize=%lld\n", filepath,
	        fat_get_filesize(pfatfs, filepath));

	/* truncate */
	if (fat_truncate(pfatfs, filepath, 1024)) {
		fprintf(stderr, "fat_truncate: error=%d\n", fat_error(pfatfs));
		return -1;
	}

	fprintf(stderr, "%ls: filesize=%lld\n", filepath,
	        fat_get_filesize(pfatfs, filepath));

	/* noent */
	if (fat_truncate(pfatfs, L"FFFF.txt", 1024) != -1) {
		fprintf(stderr, "fat_truncate: error=%d\n", fat_error(pfatfs));
		return -1;
	}

	/* invalid argument */
	if (fat_truncate(pfatfs, filepath, -1) != -1) {
		fprintf(stderr, "fat_truncate: error=%d\n", fat_error(pfatfs));
		return -1;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	int errnum;
	fatfs_t *pfatfs = NULL;

	for (int i = 1; i < argc; i++) {
		errnum = fat_mount(&pfatfs, argv[i], 0);

		if (errnum) {
			fprintf(stderr, "fat_mount: %s: error=%d\n", argv[i], errnum);
			return EXIT_FAILURE;
		}

		fprintf(stderr, "fat_mount: %s: disk label: %ls\n", argv[i],
		        fat_getlabel(pfatfs));

		if ((errnum = test_truncate(pfatfs, FIRSTFILE)) == 0) {
			errnum = test_truncate(pfatfs, SECONDFILE);
		}

		fat_umount(pfatfs);

		if (errnum)
			return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
