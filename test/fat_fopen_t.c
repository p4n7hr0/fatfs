/*
 * fat_fopen_t.c
 * functions: fat_mount, fat_umount, fat_getlabel, fat_fopen, fat_fclose,
 *            fat_error
 */

#include "fat.h"
#include <stdio.h>
#include <stdlib.h>

static int
test_openfile(fatfs_t *pfatfs)
{
	/* open r */
	fatfile_t *pfatfile = fat_fopen(pfatfs, L"/FIRST.txt", "r");
	fprintf(stderr, "fat_fopen: /FIRST.txt: error=%d\n", fat_error(pfatfs));

	if (!pfatfile)
		return -1;

	fat_fclose(pfatfile);

	/* open r+ */
	pfatfile = fat_fopen(pfatfs, L"/FIRST.txt", "r+");
	fprintf(stderr, "fat_fopen: /FIRST.txt: error=%d\n", fat_error(pfatfs));

	if (!pfatfile)
		return -1;

	fat_fclose(pfatfile);

	/* open for w */
	pfatfile = fat_fopen(pfatfs, L"/Second_File_Using_Long_Name.txt", "w");
	fprintf(stderr, "fat_fopen: /Second_File_Using_Long_Name.txt: error=%d\n",
	        fat_error(pfatfs));

	if (!pfatfile)
		return -1;

	fat_fclose(pfatfile);

	/* open for w+ */
	pfatfile = fat_fopen(pfatfs, L"/Second_File_Using_Long_Name.txt", "w+");
	fprintf(stderr, "fat_fopen: /Second_File_Using_Long_Name.txt: error=%d\n",
	        fat_error(pfatfs));

	if (!pfatfile)
		return -1;

	fat_fclose(pfatfile);

	/* open for w+x */
	pfatfile = fat_fopen(pfatfs, L"/FIRST.txt", "w+x");
	fprintf(stderr, "fat_fopen: /FIRST.txt: error=%d\n", fat_error(pfatfs));

	if (!pfatfile)
		return -1;

	fat_fclose(pfatfile);

	/* open for w+x */
	pfatfile = fat_fopen(pfatfs, L"/FIRST.txt", "wx");
	fprintf(stderr, "fat_fopen: /FIRST.txt: error=%d\n", fat_error(pfatfs));

	if (!pfatfile)
		return -1;

	fat_fclose(pfatfile);

	/* open for a */
	pfatfile = fat_fopen(pfatfs, L"/Second_File_Using_Long_Name.txt", "a");
	fprintf(stderr, "fat_fopen: /Second_File_Using_Long_Name.txt: error=%d\n",
	        fat_error(pfatfs));

	if (!pfatfile)
		return -1;

	fat_fclose(pfatfile);

	/* open for a+ */
	pfatfile = fat_fopen(pfatfs, L"/Second_File_Using_Long_Name.txt", "a+");
	fprintf(stderr, "fat_fopen: /Second_File_Using_Long_Name.txt: error=%d\n",
	        fat_error(pfatfs));

	if (!pfatfile)
		return -1;

	fat_fclose(pfatfile);

	/* test inexistent file */
	pfatfile = fat_fopen(pfatfs, L"/nofile.txt", "r");
	fprintf(stderr, "fat_fopen: /nofile.txt: error=%d\n", fat_error(pfatfs));

	if (pfatfile)
		return -1;

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

		errnum = test_openfile(pfatfs);
		fat_umount(pfatfs);

		if (errnum)
			return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
