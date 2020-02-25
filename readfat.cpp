/*
 * readfat.cpp - show info about a fat volume
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
#include <vector>
#include <string>
#include <iostream>
#include <getopt.h>
#include <limits.h>
#include <string.h>

#define PROGRAM_NAME "readfat"
#define PROGRAM_VERSION "0.1"

static void
usage(void)
{
	std::cerr << "Usage: " << PROGRAM_NAME <<  "[OPTIONS...] [DEVICE]\n";
	std::cerr << " -h, --help       print this help list\n";
	std::cerr << "     --version    show program version\n";
	std::cerr << "     --offset     start offset (default=0)\n";
	std::cerr << "     --list       list files recursively\n";
	std::cerr << "     --cat <file> print the contents of file to the stdout\n";
}

static void
do_list_files(fatdir_t *fatdir, std::string &strdir, std::wstring &wstrdir)
{
	struct fat_dir_entry *dptr = NULL;
	while ((dptr = fat_readdir(fatdir))) {
		/* skip */
		if (!strcmp(dptr->short_name, ".") || !strcmp(dptr->short_name, ".."))
			continue;

		/* print short name */
		std::cout << strdir << "/" << dptr->short_name;

		/* print long name */
		if (dptr->long_name[0] != 0x00) {
			std::cout << std::endl;

			if (wstrdir.size())
				std::wcout << wstrdir << L"/";
			else
				std::cout << strdir << "/";
			std::wcout << dptr->long_name << " (long filename)" << std::endl;
		}

		std::cout << std::endl;

		/* print dir */
		if (dptr->attr & FAT_ATTR_DIRECTORY) {
			fatdir_t *fatdir2 = fat_opendir(fatdir, dptr->short_name);
			if (fatdir2) {
				std::string strdir2 = strdir + "/" + dptr->short_name;
				std::wstring wstrdir2;

				if (dptr->long_name[0] != 0x00)
					wstrdir2 = wstrdir + L"/" + dptr->long_name;

				do_list_files(fatdir2, strdir2, wstrdir2);
				fat_closedir(fatdir2);
			}
		}
	}
}

static void
list_files(fatdir_t *fatdir)
{
	std::string str_initial_dir("");
	std::wstring wstr_initial_dir(L"");
	do_list_files(fatdir, str_initial_dir, wstr_initial_dir);
}

static void
cat_file(fatdir_t *rootdir, char *filename)
{
	fatfile_t *fatfile = NULL;
	fatdir_t *curr_dir = rootdir;
	fatdir_t *prev_dir = rootdir;
	char *curr_name = (filename[0] == '/') ? &filename[1] : filename;
	char *next = NULL;

	/* find target dir */
	while ((next = strchr(curr_name, '/'))) {
		next[0] = '\0';
		curr_dir = fat_opendir(prev_dir, curr_name);

		/* avoid to free the caller object */
		if (prev_dir != rootdir)
			fat_closedir(prev_dir);

		if (!curr_dir)
			break;

		prev_dir = curr_dir;
		curr_name = next + 1;
	}

	/* open file */
	fatfile = fat_open(curr_dir, curr_name);
	if (!fatfile)
		std::cerr << PROGRAM_NAME << ": cat_file: file not found\n";

	/* read file content */
	unsigned char chunk[256];
	ssize_t bytes_read = fat_read(fatfile, chunk, sizeof(chunk));
	while (bytes_read > 0) {
		for (ssize_t i = 0; i < bytes_read; i++)
			std::cout << chunk[i];

		bytes_read = fat_read(fatfile, chunk, sizeof(chunk));
	}

	/* close file */
	fat_close(fatfile);

	/* avoid to free the caller object */
	if (curr_dir != rootdir)
		fat_closedir(curr_dir);
}

static int
parse_volume(const char *input, off_t offset, int list, char *cat_target_file)
{
	/* init parser */
	fatfs_t *fatfs = fat_mount(input, offset);
	if (!fatfs) {
		std::cerr << PROGRAM_NAME << ": fatfs_mount: not a fat\n";
		return -1;
	}

	/* print label */
	//std::cout << fat_getlabel(fatfs) << std::endl;

	/* get root directory */
	fatdir_t *fatdir = fat_getroot(fatfs);
	if (!fatdir)
		return -1;

	/* --list */
	if (list)
		list_files(fatdir);

	/* --cat <file> */
	if (cat_target_file)
		cat_file(fatdir, cat_target_file);

	fat_closedir(fatdir);
	fat_umount(fatfs);
	return 0;
}

int
main(int argc, char *argv[])
{
	int ch = 0, list = 0;
	char *input_path = NULL;
	char *cat_target_file = NULL;
	unsigned long offset = 0;

	enum {
		OPTION_HELP = CHAR_MAX+1,
		OPTION_VERSION,
		OPTION_OFFSET,
		OPTION_LIST,
		OPTION_CAT
	};

	struct option longopts[] = {
		{ "help"   , no_argument,       NULL, 'h'            },
		{ "version", no_argument,       NULL, OPTION_VERSION },
		{ "offset" , required_argument, NULL, OPTION_OFFSET  },
		{ "list"   , no_argument,       NULL, OPTION_LIST    },
		{ "cat"    , required_argument, NULL, OPTION_CAT     },
		{ NULL     , 0                , NULL, 0              }
	};

	/* parse args */
	while ((ch = getopt_long(argc, argv, "h", longopts, NULL)) != -1) {
		switch (ch) {
			case 'h':
				usage();
				return EXIT_FAILURE;

			case OPTION_VERSION:
				std::cerr << PROGRAM_NAME << " " << PROGRAM_VERSION << std::endl;
				return EXIT_FAILURE;

			case OPTION_OFFSET:
				offset = strtoul(optarg, NULL, 0);
				if (offset == ULONG_MAX) {
					std::cerr << PROGRAM_NAME << ": error: strtoul: " << errno
						<< std::endl;
					return EXIT_FAILURE;
				}
				break;

			case OPTION_LIST:
				list = 1;
				break;

			case OPTION_CAT:
				cat_target_file = optarg;
				break;

			default:
				std::cerr << "Try '" << PROGRAM_NAME
					<< " --help' for more information." << std::endl;
				return EXIT_FAILURE;
		}
	}

	argc -= optind;
	argv += optind;

	if (!argc) {
		std::cerr << "device not specified.\n";
		return EXIT_FAILURE;
	}

	input_path = *argv;
	return parse_volume(input_path, offset, list, cat_target_file);
}
