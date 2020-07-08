/*
 * fat_fwrite_t.c
 * functions: fat_mount, fat_umount, fat_getlabel, fat_fopen, fat_fclose,
 *            fat_error, fat_fseek, fat_ftell, fat_fwrite
 */

#include "fat.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#define FIRSTFILE  L"/FIRST.txt"
#define SECONDFILE L"Second_File_Using_Long_Name.txt"

static int
test_writefile(fatfs_t *pfatfs, const wchar_t *filepath)
{
	size_t nwrite = 0;

	/* open */
	fatfile_t *pfatfile = fat_fopen(pfatfs, filepath, "a+");
	fprintf(stderr, "fat_fopen: %ls: error=%d\n", filepath, fat_error(pfatfs));

	if (!pfatfile)
		return -1;

	if (fat_fseek(pfatfile, 4096*2, FAT_SEEK_END)) {
		fprintf(stderr, "test_writefile: fat_fseek: error=%d\n",
		        fat_error(pfatfs));

		fat_fclose(pfatfile);
		return -1;
	}

	nwrite = fat_fwrite("another line!\n", 1, 14, pfatfile);
	if (fat_error(pfatfs)) {
		fat_fclose(pfatfile);
		return -1;
	}

	fprintf(stderr, "fat_fwrite: n=%zu\n", nwrite);
	fprintf(stderr, "fat_ftell=%" PRId64 "d\n", fat_ftell(pfatfile));
	fat_fclose(pfatfile);
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

		if ((errnum = test_writefile(pfatfs, FIRSTFILE)) == 0) {
			errnum = test_writefile(pfatfs, SECONDFILE);
		}

		fat_umount(pfatfs);
		if (errnum)
			return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

