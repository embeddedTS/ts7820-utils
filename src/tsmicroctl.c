/* SPDX-License-Identifier: BSD-2-Clause */

#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <assert.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#define SILABS_CHIP_ADDRESS 0x54

void usage(char **argv) {
	fprintf(stderr,
		"Usage: %s [OPTIONS] ...\n"
		"Technologic Systems Silabs Control Utility\n"
		"\n"
		"  -s, --sleep <seconds>  Remove power to the main ARM and keep microcontroller alive\n"
		"  -i, --info             Print silabs revision\n"
		"  -a, --adc              Print out all analog values\n"
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

	if (strcasestr(mdl, "TS-7800-v2")) {
		return 0x7800;
	} else if (strcasestr(mdl, "TS-7840")){
		return 0x7840;
	} else if (strcasestr(mdl, "TS-7820")){
		return 0x7820;
	} else {
		perror("model");
		return 0;
	}
}

static int silabs_init(void)
{
	static int fd = -1;
	fd = open("/dev/i2c-0", O_RDWR);
	if(fd != -1) {
		if (ioctl(fd, I2C_SLAVE_FORCE, SILABS_CHIP_ADDRESS) < 0) {
			perror("Silabs did not ACK\n");
			return -1;
		}
	}

	return fd;
}

static int silabs_read(int twifd, uint8_t *data, uint16_t addr, int bytes)
{
	struct i2c_rdwr_ioctl_data packets;
	struct i2c_msg msgs[2];
	uint8_t busaddr[2];

	busaddr[0] = ((addr >> 8) & 0xff);
	busaddr[1] = (addr & 0xff);

	msgs[0].addr = SILABS_CHIP_ADDRESS;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf = busaddr;

	msgs[1].addr = SILABS_CHIP_ADDRESS;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = bytes;
	msgs[1].buf =  (void *)data;

	packets.msgs  = msgs;
	packets.nmsgs = 2;

	if(ioctl(twifd, I2C_RDWR, &packets) < 0) {
		perror("Unable to send data");
		return 1;
	}
	return 0;
}

static int silabs_write(int twifd, uint8_t *data, uint16_t addr, int bytes)
{
	struct i2c_rdwr_ioctl_data packets;
	struct i2c_msg msg;
	uint8_t outdata[4096];

	/* Linux only supports 4k transactions at a time, and we need
	* two bytes for the address */
	assert(bytes <= 4094);

	outdata[0] = ((addr >> 8) & 0xff);
	outdata[1] = (addr & 0xff);
	memcpy(&outdata[2], data, bytes);

	msg.addr = SILABS_CHIP_ADDRESS;
	msg.flags = 0;
	msg.len  = 2 + bytes;
	msg.buf  = outdata;

	packets.msgs  = &msg;
	packets.nmsgs = 1;

	if(ioctl(twifd, I2C_RDWR, &packets) < 0) {
		return 1;
	}
	return 0;
}

void do_info(int twifd)
{
	uint8_t buf[26];
	int i;

	memset(buf, 0, sizeof(buf));
	if(silabs_read(twifd, buf, 1280, sizeof(buf))) {
		perror("Failed to talk to silabs!");
		return;
	}

	for(i = 0; i < 13; i++){
		uint16_t p;
		uint32_t d, c, f;
		uint16_t val;
		p = 0x3FF & *(uint16_t *)&buf[i*2];

		switch(i) {
		case 4:
			val = p * 50;
			val += ((uint32_t)p * 3350) / 21483;
			printf("V8_36_MV=%d\n", val);
			break;
		case 5:
			val = p * 5;
			val += (p * 5035) / 24893;
			printf("V5_A_MV=%d\n", val);
			break;
		case 7:
			val = p * 5;
			val += ((uint32_t)p * 115595) / 150381;
			printf("AN_SUP_CHRG_MV=%d\n", val);
			break;
		case 8:
			val = p * 4;
			val += (p * 908) / 1023;
			printf("AN_SUP_CAP_1_MV=%d\n", val);
			break;
		case 9:
			val = p * 4;
			val += ((uint32_t)p * 908) / 1023;
			printf("AN_SUP_CAP_2_MV=%d\n", val);
			break;
		case 10:
			val = p * 2;
			val += (p * 454) / 1023;
			val = (val / 720) * 100; // 100mA per 720mV
			printf("FAN_CURRENT_MA=%d\n", val);
			break;
		case 12:
			d = ((uint64_t)p * 160156) - (764 << 16);
			c = d / 188088;
			f = ((d % 188088) * 1000) / 188088;
			val = (c * 1000) + f;
			printf("SILAB_TEMP_MC=%d\n", val);
			break;
		}
	}
}

void do_sleep(int twifd, int seconds)
{
	unsigned char dat[4] = {0};
	int opt_sleepmode = 1; // Legacy mode on new boards
	int opt_resetswitchwkup = 1;
	static int touchfd = -1;
	touchfd = open("/dev/i2c-0", O_RDWR);

	if (ioctl(touchfd, I2C_SLAVE_FORCE, 0x5c) == 0) {
		dat[0] = 51;
		dat[1] = 0x1;
		write(touchfd, &dat, 2);
		dat[0] = 52;
		dat[1] = 0xa;
		write(touchfd, &dat, 2);
	}

	dat[0] = (0x1 | (opt_resetswitchwkup << 1) |
	  ((opt_sleepmode-1) << 4) | 1 << 6);
	dat[3] = (seconds & 0xff);
	dat[2] = ((seconds >> 8) & 0xff);
	dat[1] = ((seconds >> 16) & 0xff);
	write(twifd, &dat, 4);
}

static void do_silabs_sleep(int twifd, uint32_t deciseconds)
{
	uint8_t data[6];

	printf("Sleeping for %d deciseconds...\n", deciseconds);

	data[0] = deciseconds & 0xFF;
	data[1] = (deciseconds >> 8) & 0xFF;
	data[2] = (deciseconds >> 16) & 0xFF;
	data[3] = (deciseconds >> 24) & 0xFF;

	if(silabs_write(twifd, data, 1024, 4)) {
		perror("Failed to write to the silabs!");
	}

	data[0] = 2;

	if(silabs_write(twifd, data, 1028, 1)) {
		perror("Failed to write to the silabs!");
	}
}

int main(int argc, char **argv)
{
	int c, twifd, model;

	static struct option long_options[] = {
		{ "info", 0, 0, 'i' },
		{ "sleep", required_argument, 0, 's' },
		{ "help", 0, 0, 'h' },
		{ 0, 0, 0, 0 }
	};

	if(argc == 1) {
		usage(argv);
		return 1;
	}

	model = get_model();
	if(model != 0x7820){
		fprintf(stderr, "Unsupported model\n");
		return 1;
	}
	
	twifd = silabs_init();
	if(twifd == -1) {
		fprintf(stderr, "ERROR: Cannot initialize connection to Silabs via /dev/i2c-0\n");
		return 1;
	}

	while((c = getopt_long(argc, argv, "is:h", long_options, NULL)) != -1) {
		switch(c) {
		case 'i':
			do_info(twifd);
			break;

		case 's':
			do_silabs_sleep(twifd, atoi(optarg) * 100);
			break;

		case ':':
			fprintf(stderr, "%s: option `-%c' requires an argument\n",
				argv[0], optopt);
			break;

		default:
			fprintf(stderr, "%s: option `-%c' is invalid\n",
                		argv[0], optopt);

		case 'h':
			usage(argv);
			return 1;
		}
	}

	return 0;
}

