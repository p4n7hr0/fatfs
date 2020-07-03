/*
 * fat_mount_t.c
 * functions: fat_mount, fat_umount, fat_getlabel
 */

#include "fat.h"
#include <stdio.h>
#include <stdlib.h>

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
		fat_umount(pfatfs);
	}

	return EXIT_SUCCESS;
}
