/*
 * seekfile.cpp - test fat_seek function
 */

#include "fat.h"
#include <iostream>
#include <getopt.h>
#include <limits.h>
#include <string.h>

#define PROGRAM_NAME "seekfile"
#define PROGRAM_VERSION "0.1"

static void
usage(void)
{
	std::cerr << "Usage: " << PROGRAM_NAME <<  "[OPTIONS...] [DEVICE]\n";
	std::cerr << " -h, --help        print this help list\n";
	std::cerr << "     --version     show program version\n";
	std::cerr << "     --offset      start offset (default=0)\n";
	std::cerr << "     --file <file> use this file for testing\n";
}

static fatfile_t *
find_file(fatfs_t *fatfs, char *path)
{
	fatdir_t *curr_dir = fat_getroot(fatfs);
	fatdir_t *prev_dir = curr_dir;
	char *curr_name = (path[0] == '/') ? &path[1] : path;
	char *next = NULL;

	/* find target dir */
	while ((next = strchr(curr_name, '/'))) {
		next[0] = '\0';
		curr_dir = fat_opendir(prev_dir, curr_name);
		fat_closedir(prev_dir);

		if (!curr_dir)
			break;

		prev_dir = curr_dir;
		curr_name = next + 1;
	}

	/* open file */
	return fat_open(curr_dir, curr_name);
}

static int
test_seek(const char *input, off_t offset, char *target_file)
{
	fatfs_t *fatfs = NULL;
	fatfile_t *fatfile = NULL;

	if (!target_file) {
		std::cout << "target file not specified." << std::endl;
		return -1;
	}

	std::cout << "[+] mounting " << input << std::endl;
	fatfs = fat_mount(input, offset);
	if (!fatfs) {
		std::cout << "[!] " << PROGRAM_NAME
			<< ": fat_mount: invalid volume" << std::endl;
		return -1;
	}

	std::cout << "[+] opening " << target_file << std::endl;
	fatfile = find_file(fatfs, target_file);
	if (!fatfile) {
		std::cout << "[!] " << PROGRAM_NAME
			<< ": find_file: file not found" << std::endl;
		fat_umount(fatfs);
		return -1;
	}

	/* test seek function */
	std::cout << "[+] seeking to eof:      fat_seek -> "
		<< fat_seek(fatfile, 0, FAT_SEEK_END) << std::endl;

	std::cout << "[+] seeking to eof+20:   fat_seek -> "
		<< fat_seek(fatfile, 20, FAT_SEEK_END) << std::endl;

	std::cout << "[+] seeking to curr+512: fat_seek -> "
		<< fat_seek(fatfile, 512, FAT_SEEK_CUR) << std::endl;

	std::cout << "[+] seeking to eof+200:  fat_seek -> "
		<< fat_seek(fatfile, 200, FAT_SEEK_END) << std::endl;

	std::cout << "[+] seeking to curr-400: fat_seek -> "
		<< fat_seek(fatfile, -400, FAT_SEEK_CUR) << std::endl;

	std::cout << "[+] seeking to 150:      fat_seek -> "
		<< fat_seek(fatfile, 150, FAT_SEEK_SET) << std::endl;

	std::cout << "[+] seeking to curr-50:  fat_seek -> "
		<< fat_seek(fatfile, -50, FAT_SEEK_CUR) << std::endl;

	std::cout << "[+] seeking to eof-105:  fat_seek -> "
		<< fat_seek(fatfile, -105, FAT_SEEK_END) << std::endl;

	std::cout << "[+] seeking to 20:       fat_seek -> "
		<< fat_seek(fatfile, 20, FAT_SEEK_SET) << std::endl;

	std::cout << "[+] seeking to -10:      fat_seek -> "
		<< fat_seek(fatfile, -10, FAT_SEEK_SET) << std::endl;

	std::cout << "[+] seeking to curr:     fat_seek -> "
		<< fat_seek(fatfile, 0, FAT_SEEK_CUR) << std::endl;

	fat_close(fatfile);
	fat_umount(fatfs);
	return 0;
}

int
main(int argc, char *argv[])
{
	int ch = 0;
	char *input_path = NULL;
	char *target_file = NULL;
	unsigned long offset = 0;

	enum {
		OPTION_HELP = CHAR_MAX+1,
		OPTION_VERSION,
		OPTION_OFFSET,
		OPTION_FILE_NAME
	};

	struct option longopts[] = {
		{ "help"   , no_argument,       NULL, 'h'              },
		{ "version", no_argument,       NULL, OPTION_VERSION   },
		{ "offset" , required_argument, NULL, OPTION_OFFSET    },
		{ "file"   , required_argument, NULL, OPTION_FILE_NAME },
		{ NULL     , 0                , NULL, 0                }
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

			case OPTION_FILE_NAME:
				target_file = optarg;
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
	return test_seek(input_path, offset, target_file);
}
