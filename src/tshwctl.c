/* SPDX-License-Identifier: BSD-2-Clause */

#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <assert.h>

#include "fpga.c"

void usage(char **argv) {
	fprintf(stderr,
		"Usage: %s [OPTIONS] ...\n"
		"embeddedTS System Utility\n"
		"\n"
		"  -i, --info             Print board revisions\n"
		"  -h, --help             This message\n"
		"\n",
		argv[0]
	);
}

static int get_model(void)
{
	FILE *proc;
	char mdl[256];

	proc = fopen("/proc/device-tree/model", "r");
	if (!proc) {
		perror("model");
		return 0;
	}
	assert(fread(mdl, 256, 1, proc) == 0);

	if (strcasestr(mdl, "TS-7840")){
		return 0x7840;
	} else if (strcasestr(mdl, "TS-7820")){
		return 0x7820;
	} else if (strcasestr(mdl, "TS-7825")){
		return 0x7825;
	} else {
		perror("model");
		return 0;
	}
}

int main(int argc, char **argv)
{
	int c;
	int opt_info = 0;

	static struct option long_options[] = {
		{ "info", 0, 0, 'i' },
		{ "help", 0, 0, 'h' },
		{ 0, 0, 0, 0 }
	};

	if(argc == 1) {
		usage(argv);
		return 1;
	}

	while((c = getopt_long(argc, argv, "im::w:r::l::qc:th", long_options, NULL)) != -1) {
		switch(c) {
		case 'i':
			opt_info = 1;
			break;

		case ':':
			fprintf(stderr, "%s: option `-%c' requires an argument\n",
				argv[0], optopt);
			break;

		default:
			fprintf(stderr, "%s: option `-%c' is invalid\n",
                		argv[0], c);

		case 'h':
			usage(argv);
			return 1;
		}
	}
	fpga_init();

	if (opt_info){
		uint32_t fpga_rev = fpga_peek32(0x0);
		uint32_t fpga_hash = fpga_peek32(0x4);

		printf("model=0x%X\n", get_model());
		printf("fpga_rev=%d\n", fpga_rev & 0x7fffffff);
		if(fpga_rev & (1 << 31))
			printf("fpga_hash=\"%x-dirty\"\n", fpga_hash);
		else
			printf("fpga_hash=\"%x\"\n", fpga_hash);
	}

	return 0;
}

