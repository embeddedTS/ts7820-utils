/* SPDX-License-Identifier: BSD-2-Clause */

#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <stdint.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include "fpga.c"
#include "frac-clk-gen.c"

#define FRAC_BITS 11
#define FRAC_MSK ((1<<FRAC_BITS)-1)
#define IDIV_BITS 7
#define IDIV_MSK ((1<<IDIV_BITS)-1)
#define BASE_CLK_FREQ 125000000

/* Returns 32-bit value to write to FPGA reg if 16550 UART
 * is set for 115200 divisor (dl = 1) */
uint32_t set_baudrate(uint8_t channel, uint32_t baudrate) {
	return frac_clk_gen(baudrate * 16)|(channel<<29);
}

void usage(char **argv) {
	fprintf(stderr,	
		"Usage: %s [OPTIONS] ...\n"
		"Technologic Systems UART baud rate control\n"
		"\n"
		"  -p, --port <num>       Set port to modify\n"
		"  -b, --baud <rate>      Specify target baud rate\n"
		"  -h, --help             This message\n"
		"\n",
		argv[0]
	);
}

/* If we had to scale down, actual frequency nowill be off */
float actual_freq(uint32_t ctl) {
	uint32_t idiv = ctl>>(FRAC_BITS*2);
	float frac = (ctl>>FRAC_BITS)&FRAC_MSK;
	frac /= ctl&FRAC_MSK;
	frac += idiv;
	return BASE_CLK_FREQ / frac;
}

/* Parts per million error */
int32_t ppm(float ctl, float b) {
	float err = (b - ctl)/b;
	return err * 1000000;
}

/* Uart specific stuff... */
float bitperiod_min(uint32_t ctl) {
	uint32_t idiv = ctl>>(FRAC_BITS*2);
	uint32_t fracn = (ctl>>FRAC_BITS)&FRAC_MSK;
	uint32_t fracd = ctl&FRAC_MSK;
	uint32_t clks = idiv*16;

	clks += (fracd-1+(fracn*16))/fracd;
	return (float)BASE_CLK_FREQ/clks;
}

float bitperiod_max(uint32_t ctl) {
	uint32_t idiv = ctl>>(FRAC_BITS*2);
	uint32_t fracn = (ctl>>FRAC_BITS)&FRAC_MSK;
	uint32_t fracd = ctl&FRAC_MSK;
	uint32_t clks = idiv*16;

	clks += fracn*16/fracd;
	return (float)BASE_CLK_FREQ/clks;
}

float byteperiod_min(uint32_t ctl) {
	uint32_t idiv = ctl>>(FRAC_BITS*2);
	uint32_t fracn = (ctl>>FRAC_BITS)&FRAC_MSK;
	uint32_t fracd = ctl&FRAC_MSK;
	uint32_t clks = idiv*160;

	clks += (fracd-1+(fracn*160))/fracd;
	return ((float)BASE_CLK_FREQ/clks)*10;
}

float byteperiod_max(uint32_t ctl) {
	uint32_t idiv = ctl>>(FRAC_BITS*2);
	uint32_t fracn = (ctl>>FRAC_BITS)&FRAC_MSK;
	uint32_t fracd = ctl&FRAC_MSK;
	uint32_t clks = idiv*160;

	clks += fracn*160/fracd;
	return ((float)BASE_CLK_FREQ/clks)*10;
}

#ifdef CTL
int main(int argc, char **argv)
{
	int c;
	int opt_port = -1;
	int opt_baud = 0;
	int opt_verbose = 0;
	uint32_t reg;

	static struct option long_options[] = {
		{ "port", required_argument, 0, 'p' },
		{ "baud", required_argument, 0, 'b' },
		{ "verbose", 0, 0, 'v' },
		{ "help", 0, 0, 'h' },
		{ 0, 0, 0, 0 }
	};

	if(argc == 1) {
		usage(argv);
		return 1;
	}

	lisp_init();
	fpga_init();

	while((c = getopt_long(argc, argv, "p:b:vh", long_options, NULL)) != -1) {
		switch(c) {
		case 'p':
			opt_port = atoi(optarg);
			if(opt_port < 0 || opt_port > 7) {
				printf("Port must be between 0-7\n");
			}
			break;
		case 'b':
			opt_baud = atoi(optarg);
			if(opt_baud < 115200) {
				fprintf(stderr, "For baud rates < 115200, use 115200 as the baudrate and set the baud rate with termios\n");
				return 1;
			}
			break;
		case 'v':
			opt_verbose = 1;
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

	printf("port=%d\n", opt_port);
	printf("requested_baud=%d\n", opt_baud);

	reg = frac_clk_gen(opt_baud * 16);
	printf("actual_baud=%f\n", actual_freq(reg)/16);
	if(opt_verbose) {
		printf("baud_ppm_error=%d\n", ppm(reg, opt_baud*16));
		printf("xtal_freq_required_mhz=%f\n", opt_baud*16/1e6);
		printf("xtal_freq_actual_mhz=%f\n", actual_freq(reg)/1e6);
		printf("min1bit_freq=%f\n", bitperiod_min(reg));
		printf("min1bit_freq_ppm=%d\n", ppm(bitperiod_min(reg), opt_baud));
		printf("max1bit_freq=%f\n", bitperiod_max(reg));
		printf("max1bit_freq_ppm=%d\n", ppm(bitperiod_max(reg), opt_baud));
		printf("min10bit_freq=%f\n", byteperiod_min(reg));
		printf("min10bit_freq_ppm=%d\n", ppm(byteperiod_min(reg), opt_baud));
		printf("max10bit_freq=%f\n", byteperiod_max(reg));
		printf("max10bit_freq_ppm=%d\n", ppm(byteperiod_max(reg), opt_baud));
	}

	fpga_poke32(0x20, set_baudrate(opt_port, opt_baud));

	return 0;
}
#endif
