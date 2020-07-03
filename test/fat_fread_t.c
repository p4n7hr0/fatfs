/*
 * fat_fread_t.c
 * functions: fat_mount, fat_umount, fat_getlabel, fat_fopen, fat_fclose,
 *            fat_error, fat_fread
 */

#include "fat.h"
#include <stdio.h>
#include <stdlib.h>

#define FIRSTFILE  L"/FIRST.txt"
#define SECONDFILE L"Second_File_Using_Long_Name.txt"

static int
test_readfile(fatfs_t *pfatfs, const wchar_t *filepath)
{
	size_t nread = 0;
	char buf[256];

	/* open */
	fatfile_t *pfatfile = fat_fopen(pfatfs, filepath, "r+");
	fprintf(stderr, "fat_fopen: %ls: error=%d\n", filepath, fat_error(pfatfs));

	if (!pfatfile)
		return -1;

	while ((nread = fat_fread(buf, 1, sizeof(buf), pfatfile))) {
		if (fat_error(pfatfs)) {
			fprintf(stderr, "fat_fread: error=%d\n", fat_error(pfatfs));
			break;
		}

		fprintf(stderr, "=> chunk\n");
		fwrite(buf, 1, nread, stdout);
	}

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

		if ((errnum = test_readfile(pfatfs, FIRSTFILE)) == 0) {
			errnum = test_readfile(pfatfs, SECONDFILE);
		}

		fat_umount(pfatfs);
		if (errnum)
			return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

