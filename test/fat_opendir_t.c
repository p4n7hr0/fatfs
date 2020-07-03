/*
 * fat_opendir_t.c
 * functions: fat_mount, fat_umount, fat_getlabel, fat_opendir, fat_closedir,
 *            fat_error
 */

#include "fat.h"
#include <stdio.h>
#include <stdlib.h>

static int
test_opendir(fatfs_t *pfatfs)
{
	/* open root */
	fatdir_t *pfatdir = fat_opendir(pfatfs, L"/");
	fprintf(stderr, "fat_opendir: rootdir: error=%d\n", fat_error(pfatfs));

	if (!pfatdir)
		return -1;

	fat_closedir(pfatdir);

	/* try to open an invalid path */
	pfatdir = fat_opendir(pfatfs, L"");
	fprintf(stderr, "fat_opendir: invalid 1: error=%d\n", fat_error(pfatfs));
	if (pfatdir) {
		fat_closedir(pfatdir);
		return -1;
	}

	/* try to open an invalid path */
	pfatdir = fat_opendir(pfatfs, NULL);
	fprintf(stderr, "fat_opendir: invalid 2: error=%d\n", fat_error(pfatfs));
	if (pfatdir) {
		fat_closedir(pfatdir);
		return -1;
	}

	/* try to open an invalid path */
	pfatdir = fat_opendir(pfatfs, (wchar_t *) "/path");
	fprintf(stderr, "fat_opendir: invalid 3: error=%d\n", fat_error(pfatfs));
	if (pfatdir) {
		fat_closedir(pfatdir);
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

		errnum = test_opendir(pfatfs);
		fat_umount(pfatfs);

		if (errnum)
			return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
