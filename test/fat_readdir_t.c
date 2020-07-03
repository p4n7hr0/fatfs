/*
 * fat_readdir_t.c
 * functions: fat_mount, fat_umount, fat_getlabel, fat_opendir, fat_closedir,
 *            fat_error, fat_readdir
 */

#include "fat.h"
#include <stdio.h>
#include <stdlib.h>

static int
test_readdir(fatfs_t *pfatfs)
{
	struct fatdirent *dp;

	/* open root */
	fatdir_t *pfatdir = fat_opendir(pfatfs, L"/");
	fprintf(stderr, "fat_opendir: rootdir: error=%d\n", fat_error(pfatfs));

	if (!pfatdir)
		return -1;

	/* list files */
	while ((dp = fat_readdir(pfatdir))) {
		fprintf(stderr, "fat_readdir: %d: %ls\n", fat_error(pfatfs),dp->d_name);
	}

	fat_closedir(pfatdir);
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

		errnum = test_readdir(pfatfs);
		fat_umount(pfatfs);

		if (errnum)
			return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
