/*
 * fat_fseek_t.c
 * functions: fat_mount, fat_umount, fat_getlabel, fat_fopen, fat_fclose,
 *            fat_error, fat_fseek, fat_ftell
 */

#include "fat.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

struct seekpos {
	fatoff_t off;
	int whence;
};

static int
test_seekfile(fatfs_t *pfatfs)
{
	int error = 0;
	struct seekpos validlist[] = {
		{0  , FAT_SEEK_END},
		{20 , FAT_SEEK_END},
		{50 , FAT_SEEK_CUR},
		{-30, FAT_SEEK_CUR},
		{-5 , FAT_SEEK_END},
		{3  , FAT_SEEK_SET},
		{20 , FAT_SEEK_SET},
	};

	struct seekpos invalidlist[] = {
		{-1      , FAT_SEEK_SET},
		{-1048576, FAT_SEEK_END},
	};

	/* open */
	fatfile_t *pfatfile = fat_fopen(pfatfs, L"/FIRST.txt", "r");
	fprintf(stderr, "fat_fopen: /FIRST.txt: error=%d\n", fat_error(pfatfs));

	if (!pfatfile)
		return -1;

	/* test valid seek */
	for (size_t i = 0; i < sizeof(validlist)/sizeof(validlist[0]); i++) {
		fprintf(stderr, "fat_fseek(%" PRId64 ", %d): ", validlist[i].off,
		        validlist[i].whence);

		if (fat_fseek(pfatfile, validlist[i].off, validlist[i].whence)) {
			error = fat_error(pfatfs);
			fprintf(stderr, "error=%d\n", error);
			goto _close_and_quit;
		}

		fprintf(stderr, "fat_ftell(pfatfile)=%" PRId64 "\n", fat_ftell(pfatfile));
	}

	/* test invalid seek */
	for (size_t i = 0; i < sizeof(invalidlist)/sizeof(invalidlist[0]); i++) {
		fprintf(stderr, "fat_fseek(%" PRId64 ", %d): ", invalidlist[i].off,
		        invalidlist[i].whence);

		if (fat_fseek(pfatfile,invalidlist[i].off,invalidlist[i].whence) != -1){
			error = -1;
			goto _close_and_quit;
		}

		fprintf(stderr, "error=%d fat_ftell(pfatfile)=%" PRId64 "\n",
		        fat_error(pfatfs), fat_ftell(pfatfile));
	}

_close_and_quit:
	fat_fclose(pfatfile);
	return error;
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

		errnum = test_seekfile(pfatfs);
		fat_umount(pfatfs);

		if (errnum)
			return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

