/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <errno.h>
#include <getopt.h>
#include <string.h>

#define MAGIC_STRING "TSPROD"

void usage(char **argv) {
	fprintf(stderr,
		"Usage: %s [OPTIONS] ...\n"
		"embeddedTS Production Info\n"
		"\n"
		"  -d, --device           Specify device to read/write\n"
		"  -r, --read             Read info from last 512b block of typically mmcblk*boot1\n"
		"  -w, --write            Takes string values from stdin and saves them to device\n"
		"  -h, --help             This message\n"
		"  This stores string values in the last 512b of a given block device.\n"
		"  returns 0/1 on --read to indicate status of valid block\n"
		"\n",
		argv[0]
	);
}

int main(int argc, char **argv)
{
	int i, opt_read = 0, opt_write = 0;
	char *device = 0;
	int devfd;

	static struct option long_options[] = {
		{ "device", required_argument, 0, 'd' },
		{ "read", 0, 0, 'r' },
		{ "write", 0, 0, 'w' },
		{ "help", 0, 0, 'h' },
		{ 0, 0, 0, 0 }
	};

	while((i = getopt_long(argc, argv, "d:wrh", long_options, NULL)) != -1) {
		switch(i) {
		case 'd':
			device = strdup(optarg);
			break;
		case 'r':
			opt_read = 1;
			break;
		case 'w':
			opt_write = 1;
			break;
		case ':':
			fprintf(stderr, "%s: option `-%c' requires an argument\n",
				argv[0], optopt);
			break;
		default:
			fprintf(stderr, "%s: option `-%c' is invalid\n",
                		argv[0], optopt);
			break;
		case 'h':
			usage(argv);
			return 1;
		}
	}

	if(!device) {
		fprintf(stderr, "Must specify a device\n");
		return 1;
	}

	devfd = open(device, O_RDWR);
	if(devfd < 0)
	{
		fprintf(stderr, "%s", strerror(errno));
		return 1;
	}

	/* Skip to last 512b sector */
	if(lseek(devfd, -512, SEEK_END) == -1)
	{
		fprintf(stderr, "%s", strerror(errno));
		return 1;
	}

	if(opt_write) {
		int wr;
		char buf[512];
		bzero(buf, 512);
		strcpy(buf, MAGIC_STRING);

		for (i = strlen(MAGIC_STRING) + 1; i < 512; i++)
		{
			int dat = getchar();
			if(dat == EOF || dat == 0)
				break;

			buf[i] = (char)dat;
			if(i + 1 == 512)
			{
				fprintf(stderr, 
					    "Max size for tsprodinfo is %zd.  Data not saved.\n",
					    512 - strlen(MAGIC_STRING));
				return 1;
			}
		}

		wr = write(devfd, buf, 512);
		if(wr != 512)
		{
			fprintf(stderr, "Write failed with: %s", strerror(errno));
			return 1;
		}
	}

	if(opt_read) {
		int rd;
		char buf[512];

		rd = read(devfd, buf, 512);
		if(rd != 512) {
			fprintf(stderr, "%s", strerror(errno));
			return 1;
		}

		/* Check for magic string */
		if(strcmp(MAGIC_STRING, buf) != 0){
			fprintf(stderr, "No tsprodinfo saved on this device\n");
			return 1;
		}
		for (i = strlen(MAGIC_STRING)+1; i < 512; i++)
		{
			if(buf[i] != 0)
				putchar(buf[i]);
			else
				break;
		}
	}

	return 0;
}

