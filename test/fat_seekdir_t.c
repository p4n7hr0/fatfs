/*
 * fat_seekdir_t.c
 * functions: fat_mount, fat_umount, fat_getlabel, fat_opendir, fat_closedir,
 *            fat_error, fat_readdir, fat_telldir, fat_seekdir, fat_rewinddir
 */

#include "fat.h"
#include <stdio.h>
#include <stdlib.h>

static int
test_seekdir(fatfs_t *pfatfs)
{
	struct fatdirent *dp;

	/* open root */
	fatdir_t *pfatdir = fat_opendir(pfatfs, L"/");
	fprintf(stderr, "fat_opendir: rootdir: error=%d\n", fat_error(pfatfs));

	if (!pfatdir)
		return -1;

	/* seekdir [1] */
	fat_seekdir(pfatdir, 1);
	if (fat_error(pfatfs))
		return -1;

	fprintf(stderr, "fat_seekdir(pfatdir, 1): error=%d\n", fat_error(pfatfs));
	dp = fat_readdir(pfatdir);
	fprintf(stderr, "fat_readdir: %ls: error=%d\n", (dp) ? dp->d_name : L"NULL",
	        fat_error(pfatfs));
	fprintf(stderr, "fat_telldir: %ld\n", fat_telldir(pfatdir));

	/* seekdir [2] */
	fat_seekdir(pfatdir, 2);
	if (fat_error(pfatfs))
		return -1;

	fprintf(stderr, "fat_seekdir(pfatdir, 2): error=%d\n", fat_error(pfatfs));
	dp = fat_readdir(pfatdir);
	fprintf(stderr, "fat_readdir: %ls: error=%d\n", (dp) ? dp->d_name : L"NULL",
	        fat_error(pfatfs));
	fprintf(stderr, "fat_telldir: %ld\n", fat_telldir(pfatdir));

	/* seekdir [20] */
	fat_seekdir(pfatdir, 20);
	if (fat_error(pfatfs))
		return -1;

	fprintf(stderr, "fat_seekdir(pfatdir, 20): error=%d\n", fat_error(pfatfs));
	dp = fat_readdir(pfatdir);
	fprintf(stderr, "fat_readdir: %ls: error=%d\n", (dp) ? dp->d_name : L"NULL",
	        fat_error(pfatfs));
	fprintf(stderr, "fat_telldir: %ld\n", fat_telldir(pfatdir));

	/* seekdir [0] */
	fat_seekdir(pfatdir, 0);
	if (fat_error(pfatfs))
		return -1;

	fprintf(stderr, "fat_seekdir(pfatdir, 0): error=%d\n", fat_error(pfatfs));
	dp = fat_readdir(pfatdir);
	fprintf(stderr, "fat_readdir: %ls: error=%d\n", (dp) ? dp->d_name : L"NULL",
	        fat_error(pfatfs));
	fprintf(stderr, "fat_telldir: %ld\n", fat_telldir(pfatdir));

	/* seekdir [-2] */
	fat_seekdir(pfatdir, -2);
	if (!fat_error(pfatfs))
		return -1;

	fprintf(stderr, "fat_seekdir(pfatdir, -2): error=%d\n", fat_error(pfatfs));
	dp = fat_readdir(pfatdir);
	fprintf(stderr, "fat_readdir: %ls: error=%d\n", (dp) ? dp->d_name : L"NULL",
	        fat_error(pfatfs));
	fprintf(stderr, "fat_telldir: %ld\n", fat_telldir(pfatdir));

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

		errnum = test_seekdir(pfatfs);
		fat_umount(pfatfs);

		if (errnum)
			return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

