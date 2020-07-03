/*
 * parsefat.c
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
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <limits.h>

#define PROGRAM_NAME "parsefat"
#define PROGRAM_VERSION "0.1"

#define BUGON(exp) do { \
if ((exp)) { fprintf(stderr, "bug: %s: %s\n", __func__, #exp); exit(1); }\
} while(0)

static void
usage(void)
{
	fprintf(stderr, "Usage: %s [options] [disk]\n\n", PROGRAM_NAME);
	fprintf(stderr, "'disk' is a device or regular file\n\n");

	fprintf(stderr, "Standard options:\n");
	fprintf(stderr, "-h, --help       display this help and exit\n");
	fprintf(stderr, "--version        display version information and exit\n");
	fprintf(stderr, "--offset offset  choose the start offset (default=0)\n\n");

	fprintf(stderr, "Command options:\n");
	fprintf(stderr, "--read pathname\n");
	fprintf(stderr, "                 print the content of 'path' to stdout\n");
}

static void
parsefat_read_directory(fatdir_t *pfatdir)
{
	struct fatdirent *dp = NULL;

	fprintf(stdout, "%-35s %-11s %s\n", "[name]", "[type]", "[size]");
	while ((dp = fat_readdir(pfatdir))) {
		fprintf(stdout, "%-35ls %-11s  %ld\n", dp->d_name,
			(dp->d_type == FAT_TYPE_ARCHIVE) ? "file" : "directory",
			(long) dp->d_size);
	}
}

static void
parsefat_read_file(fatfile_t *fatfile)
{
	size_t nread = 0;
	unsigned char chunk[256];

	while ((nread = fat_fread(chunk, 1, sizeof(chunk), fatfile)))
		fwrite(chunk, 1, nread, stdout);
}

static void
parsefat_read(const wchar_t *path, const char *disk, off_t offset)
{
	int error;
	fatfs_t *pfatfs;
	fatdir_t *pfatdir;
	fatfile_t *pfatfile;

	/* mount */
	error = fat_mount(&pfatfs, disk, offset);
	if (error) {
		fprintf(stderr, "%s: fat_mount: %s: error=%d\n",
			PROGRAM_NAME, disk, error);
		return;
	}

	/* try to open path as dir */
	pfatdir = fat_opendir(pfatfs, path);
	if (pfatdir) {
		parsefat_read_directory(pfatdir);
		fat_closedir(pfatdir);

	/* try to open path as file */
	} else if (fat_error(pfatfs) == FAT_ERR_NOTDIR) {
		pfatfile = fat_fopen(pfatfs, path, "r");
		if (!pfatfile) {
			fprintf(stderr, "%s: fat_fopen: %ls: error=%d\n",
				PROGRAM_NAME, path, fat_error(pfatfs));
		}

		parsefat_read_file(pfatfile);
		fat_fclose(pfatfile);

	/* error */
	} else {
		fprintf(stderr, "%s: parsefat_read: %ls: error=%d\n",
			PROGRAM_NAME, path, fat_error(pfatfs));
	}

	fat_umount(pfatfs);
}

int main(int argc, char *argv[])
{
	off_t offset = 0;
	int ch = 0, cmdread = 0;
	wchar_t *path = NULL;

	enum {
		OPTION_HELP = CHAR_MAX+1,
		OPTION_VERSION,
		OPTION_OFFSET,
		OPTION_CMD_READ
	};

	struct option longopts[] = {
		{ "help"   , no_argument,       NULL, 'h'             },
		{ "version", no_argument,       NULL, OPTION_VERSION  },
		{ "offset" , required_argument, NULL, OPTION_OFFSET   },
		{ "read"   , required_argument, NULL, OPTION_CMD_READ },
		{ NULL     , 0                , NULL, 0               }
	};

	while ((ch = getopt_long(argc, argv, "h", longopts, NULL)) != -1) {
		switch (ch) {
			case 'h':
				usage();
				return EXIT_FAILURE;

			case OPTION_VERSION:
				fprintf(stderr, "%s: %s\n", PROGRAM_NAME, PROGRAM_VERSION);
				return EXIT_FAILURE;

			case OPTION_OFFSET:
				offset = (off_t) strtoul(optarg, NULL, 0);
				break;

			case OPTION_CMD_READ:
				/* allocate memory for path */
				path = calloc(1, (strlen(optarg) + 1) * sizeof(wchar_t));
				if (!path) {
					fprintf(stderr, "%s: calloc error", PROGRAM_NAME);
					return EXIT_FAILURE;
				}

				/* convert path */
				mbstowcs(path, optarg, strlen(optarg));
				cmdread = 1;
				break;

			default:
				fprintf(stderr, "Try '%s -h' for more information.\n", PROGRAM_NAME);
				return EXIT_FAILURE;
		}
	}

	argc -= optind;
	argv += optind;

	if (!argc) {
		fprintf(stderr, "disk not specified.\n");
		return EXIT_FAILURE;
	}

	if (cmdread) {
		parsefat_read(path, *argv, offset);
		free(path);
	}

	return EXIT_SUCCESS;
}
