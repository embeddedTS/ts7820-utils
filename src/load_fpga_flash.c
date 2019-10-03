#include <stdio.h>
#include <stdint.h>
#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <assert.h>
#include <string.h>
#include <sys/time.h>

#include "fpga.c"

/* Offset is 0 for factory load, 0xf0000 for app load */
#ifndef OFFSET
#define OFFSET 0xf0000
#endif

/*
 * 32-bit FPGA read register
 *   bit 19: toggles when data_valid ASMI output is 1
 *   bit 18: illegal erase
 *   bit 17: illegal write
 *   bit 16: ASMI busy
 *   bit 15-8: ASMI status out
 *   bit 7-0: ASMI data out
 *
 * 32-bit FPGA write register
 *   bit 31: reserved, write 0
 *   bit 30-29: operation
 *     0- read
 *     1- write
 *     2- sector erase
 *     3- read status
 *   bit 28-8: ASMI address (21-bits for 2MB)
 *   bit 7-0: ASMI data in
 */

#define ASMI_READ 		0x0
#define ASMI_WRITE 		0x1
#define ASMI_SECTOR_ERASE	0x2
#define ASMI_READ_STATUS	0x3
#define ASMI_PAGE_PROGRAM	0x4

#define ASMI_BUSY 		(1 << 16)
#define ASMI_DATA_VALID		(1 << 19)
#define ASMI_ILLEGAL_ERASE	(1 << 18)
#define ASMI_ILLEGAL_WRITE	(1 << 17)

static const unsigned char reverse[] =
{
	0x00, 0x80, 0x40, 0xC0, 0x20, 0xA0, 0x60, 0xE0, 0x10, 0x90, 0x50, 0xD0,
	0x30, 0xB0, 0x70, 0xF0,	0x08, 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8,
	0x18, 0x98, 0x58, 0xD8, 0x38, 0xB8, 0x78, 0xF8,	0x04, 0x84, 0x44, 0xC4,
	0x24, 0xA4, 0x64, 0xE4, 0x14, 0x94, 0x54, 0xD4, 0x34, 0xB4, 0x74, 0xF4,
	0x0C, 0x8C, 0x4C, 0xCC, 0x2C, 0xAC, 0x6C, 0xEC, 0x1C, 0x9C, 0x5C, 0xDC,
	0x3C, 0xBC, 0x7C, 0xFC,	0x02, 0x82, 0x42, 0xC2, 0x22, 0xA2, 0x62, 0xE2,
	0x12, 0x92, 0x52, 0xD2, 0x32, 0xB2, 0x72, 0xF2,	0x0A, 0x8A, 0x4A, 0xCA,
	0x2A, 0xAA, 0x6A, 0xEA, 0x1A, 0x9A, 0x5A, 0xDA, 0x3A, 0xBA, 0x7A, 0xFA,
	0x06, 0x86, 0x46, 0xC6, 0x26, 0xA6, 0x66, 0xE6, 0x16, 0x96, 0x56, 0xD6,
	0x36, 0xB6, 0x76, 0xF6, 0x0E, 0x8E, 0x4E, 0xCE, 0x2E, 0xAE, 0x6E, 0xEE,
	0x1E, 0x9E, 0x5E, 0xDE, 0x3E, 0xBE, 0x7E, 0xFE,	0x01, 0x81, 0x41, 0xC1,
	0x21, 0xA1, 0x61, 0xE1, 0x11, 0x91, 0x51, 0xD1, 0x31, 0xB1, 0x71, 0xF1,
	0x09, 0x89, 0x49, 0xC9, 0x29, 0xA9, 0x69, 0xE9, 0x19, 0x99, 0x59, 0xD9,
	0x39, 0xB9, 0x79, 0xF9,	0x05, 0x85, 0x45, 0xC5, 0x25, 0xA5, 0x65, 0xE5,
	0x15, 0x95, 0x55, 0xD5, 0x35, 0xB5, 0x75, 0xF5,	0x0D, 0x8D, 0x4D, 0xCD,
	0x2D, 0xAD, 0x6D, 0xED, 0x1D, 0x9D, 0x5D, 0xDD, 0x3D, 0xBD, 0x7D, 0xFD,
	0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3, 0x13, 0x93, 0x53, 0xD3,
	0x33, 0xB3, 0x73, 0xF3,	0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB,
	0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB,	0x07, 0x87, 0x47, 0xC7,
	0x27, 0xA7, 0x67, 0xE7, 0x17, 0x97, 0x57, 0xD7, 0x37, 0xB7, 0x77, 0xF7,
	0x0F, 0x8F, 0x4F, 0xCF, 0x2F, 0xAF, 0x6F, 0xEF, 0x1F, 0x9F, 0x5F, 0xDF,
	0x3F, 0xBF, 0x7F, 0xFF
};

uint32_t asmi_reg_read()
{
	return fpga_peek32(0x8);
}


void asmi_reg_write(uint32_t data)
{
	fpga_poke32(0x8, data);
	fpga_peek32(0x8);
}

int static asmi_busy(int timeout_ms)
{
	struct timeval start, now;
	uint32_t reg;
	uint64_t us;

	gettimeofday(&start, NULL);

	reg = asmi_reg_read();

	while (reg & ASMI_BUSY) {
		reg = asmi_reg_read();
		gettimeofday(&now, NULL);
		us = ((now.tv_sec * 1000000 + now.tv_usec) -
		     (start.tv_sec * 1000000 + start.tv_usec));
		if(us/1000 > timeout_ms){
			return -1;
		}
	}
	return 0;
}

int asmi_read(uint8_t *buf, uint32_t addr, uint32_t size, int en_reverse)
{
	int i;
	static int data_valid = -1;
	uint32_t reg;
	struct timeval start, now;
	uint64_t us;

	/* Get initial data_valid value */
	if(data_valid == -1)
		data_valid = !!(asmi_reg_read() & ASMI_DATA_VALID);

	for (i = 0; i < size; i++) {
		/* Write addr */
		asmi_reg_write((ASMI_READ << 29) |
			       ((addr + i) & 0x1FFFFF) << 8);

		gettimeofday(&start, NULL);
		do {
			reg = asmi_reg_read();
			gettimeofday(&now, NULL);
			us = ((now.tv_sec * 1000000 + now.tv_usec) -
			     (start.tv_sec * 1000000 + start.tv_usec));
			if(us > 100000){
				fprintf(stderr, "ASMI_DATA_VALID timed out\n");
				return 1;
			}
			/* data_valid must toggle every read */
		} while (data_valid == !!(reg & ASMI_DATA_VALID));
		data_valid = !!(reg & ASMI_DATA_VALID);

		buf[i] = (uint8_t)(reg & 0xff);
		if(en_reverse)
			buf[i] = reverse[buf[i]];

		/* Wait up to 100ms until busy deasserts */
		if(asmi_busy(100) == -1){
			fprintf(stderr, "ASMI_READ timed out\n");
			return 1;
		}
	}

	return 0;
}

/* IS25LQ016B-JBLE documents 64K erase blocks */
int asmi_erase(uint32_t addr, uint32_t size)
{
	int i;

	/* Addr must be 64k aligned */
	if(addr % 65536 != 0)
		return 1;
	for (i = 0; i < size; i += 65536) {
		asmi_reg_write((ASMI_SECTOR_ERASE << 29) |
			       ((addr + i) & 0x1FFFFF) << 8);
		if(asmi_busy(2500) == -1){
			fprintf(stderr, "ASMI_ERASE timed out\n");
			return 1;
		}
	}

	return 0;
}

/* Write 256 bytes at a time only */
int asmi_page_write(uint8_t *buf, uint32_t addr, uint32_t size)
{
	int i, x;

	for (i = 0; i < size; i += 256) {
		for (x = 0; x < 256 && (i+x) < size; x++)
			asmi_reg_write((ASMI_PAGE_PROGRAM << 29) | buf[i+x]);
		asmi_reg_write((ASMI_WRITE << 29) | ((addr + i) & 0x1FFFFF) << 8);
		if(asmi_busy(2500) == -1){
			fprintf(stderr, "ASMI_PAGE_WRITE timed out\n");
			return 1;
		}
	}

	return 0;
}

static int asmi_write(uint8_t *data, uint32_t offset, uint32_t len, int en_reverse)
{
	uint8_t wbuf[0x10000];
	int i, ret = 0;

	/* Writes must be 64k aligned */
	assert(offset % 0x10000 == 0);
	assert(len <= 0x10000);

	for (i = 0; i < len; i++){
		/* bitstreams are reverse bit order */
		if(en_reverse)
			wbuf[i] = reverse[data[i]];
		else
			wbuf[i] = data[i];
	}

	/* Erase up to 64k more */
	ret |= asmi_erase(offset, len);
	ret |= asmi_page_write(wbuf, offset, len);

	return ret;
}

void usage(char **argv) {
	fprintf(stderr,
		"Usage: %s [OPTIONS] ...\n"
		"Access ASMI core to read/write flash\n"
		"\n"
		"  -w, --write <path>     Write an RPD file\n"
		"  -v, --verify           Verify a bitstream while writing\n"
		"  -r, --read <path>      Read a bitstream back to a file\n"
		"  -h, --help             This message\n"
		"\n",
		argv[0]
	);
}

uint32_t do_write_rpd(char *path, int en_verify)
{
	int rpdfd;
	off_t len, pos;
	uint8_t data[0x10000], verify[0x10000];

	rpdfd = open(path, O_RDONLY);
	if(rpdfd == -1){
		perror("FPGA RPD open");
		exit(1);
	}

	/* Writing past the end of disk does bad things */
	len = lseek(rpdfd, 0, SEEK_END);
	if(len > 950005){
		fprintf(stderr, "RPD file is too large.  Refusing to write\n");
		return 0;
	}
	lseek(rpdfd, 0, SEEK_SET);

	for (pos = 0; pos < len; pos += 0x10000) {
		int alen = 0x10000;
		int x;
		int failed = 0;

		if(pos + 0x10000 > len) alen = len % 0x10000;

		if(read(rpdfd, data, alen) != alen){
			perror("RPD read");
			return 0;
		}

		if(asmi_write(data, OFFSET + pos, alen, 1) != 0)
			return 0;

		if(!en_verify)
			continue;

		if(asmi_read(verify, OFFSET + pos, alen, 1) != 0)
			return 0;

		for (x = 0; x < alen; x++) {
			if(data[x] != verify[x]){
				fprintf(stderr, "verify failed at %lu, Wrote 0x%X, read back 0x%X\n",
					pos + x, data[x], verify[x]);
				failed = 1;
			}
		}
		if(failed) exit(1);
	}
	close(rpdfd);

	return (uint32_t)len;
}

uint32_t do_read_rpd(char *path)
{
	int rpdfd;
	off_t len, pos;
	uint8_t data[0x10000];

	rpdfd = open(path, O_RDWR | O_CREAT);
	if(rpdfd == -1){
		perror("FPGA RPD open");
		exit(1);
	}

	/* Compressed bitstreams are technically smaller, but
	 * our FPGA bitstreams are always this size at max. If we
	 * use larger FPGAs in the future we should parse the RPD
	 * format instead */
	len = 950005;

	for (pos = 0; pos < len; pos += 0x10000) {
		int alen = 0x10000;

		if(pos + 0x10000 > len) alen = len % 0x10000;

		if(asmi_read(data, OFFSET + pos, alen, 1) != 0)
			return 0;

		if(write(rpdfd, data, alen) != alen){
			perror("RPD read");
			return 0;
		}
		fsync(rpdfd);
	}

	close(rpdfd);

	return len;
}

int main(int argc, char **argv)
{
	char *opt_write = 0, *opt_read = 0;
	int opt_verify = 0;
	int ret = 0;
	int c;

	static struct option long_options[] = {
		{ "write", required_argument, 0, 'w' },
		{ "verify", 0, 0, 'v' },
		{ "read", required_argument, 0, 'r' },
		{ "help", 0, 0, 'h' },
		{ 0, 0, 0, 0 }
	};


	while((c = getopt_long(argc, argv, "r:w:vh", long_options, NULL)) != -1) {
		switch(c) {
		case 'r':
			opt_read = strdup(optarg);
			break;
		case 'w':
			opt_write = strdup(optarg);
			break;
		case 'v':
			opt_verify = 1;
			break;
		case 'h':
		default:
			usage(argv);
			return 1;
		}
	}

	fpga_init();

	/* If the app is interrupted & restarted in the middle of an
	 * erase/write, it may still be busy.  Assume worst case of up
	 * to 2.5s for a page write */
	asmi_busy(2500);

	if(opt_write) {
		uint32_t len = do_write_rpd(opt_write, opt_verify);
		if(!len) return 1;

		printf("rpd_bytes_written=%u\n", len);
		if(opt_verify)
			printf("verify_ok=1\n");
	}

	if(opt_read) {
		uint32_t len = do_read_rpd(opt_read);
		if(!len) return 1;

		printf("rpd_bytes_read=%u\n", len);
	}

	return ret;
}
