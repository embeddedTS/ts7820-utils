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
#include <linux/pci.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

struct modelinfo
{
	int variant;
	int maxrate;
	int maxcores;
	char *name;
};

struct modelinfo ts7820_models[] =
{
	{
		.variant = 1,
		.name = "TS-7820-DMN1I",
		.maxrate = 1333,
		.maxcores = 2
	}
};

struct modelinfo ts7840_models[] =
{
	{
		.variant = 1,
		.name = "TS-7840-DMN1I",
		.maxrate = 1333,
		.maxcores = 2
	}
};

#define ARRAY_SIZE(array) \
    (sizeof(array) / sizeof(*array))

int cpurates[] = {
	1866,
	1600,
	1333,
	1066,
	666
};

int cpusar[] = {
	0x10,
	0xc,
	0x8,
	0x4,
	0x0
};

#define SILABS_CHIP_ADDRESS 0x54

static uint32_t get_fpga_phy(void)
{
	static uint32_t fpga = 0;

	if (fpga == 0) {
		uint32_t config[PCI_STD_HEADER_SIZEOF];
		FILE *f = fopen("/sys/bus/pci/devices/0000:02:00.0/config", "r");

		if (fread(config, 1, sizeof(config), f) > 0) {
			if (config[PCI_BASE_ADDRESS_2 / 4])
				fpga = (uint32_t)config[PCI_BASE_ADDRESS_2 / 4];
		} else {
			fprintf(stderr, "Can't read from the config!\n");
		}
		fclose(f);
	}

	return fpga;
}
void usage(char **argv) {
	fprintf(stderr,
		"Usage: %s [OPTIONS] ...\n"
		"Technologic Systems System Utility\n"
		"\n"
		"  -i, --info             Print board revisions\n"
		"  -m, --macaddr (<addr>) Print, or optionally change the mac address\n"
		"  -A, --nvadd <value>    Writes the value to the specified address\n"
		"  -D, --nvrw (<value>)     Read nvaddr, or optionally write specified value\n"
		"  -l  --rate (<rate>)    List all possible rates, or set clock rate.  Set 0 to use max\n"
		"  -c  --cores (<1/2)>)   Read number of enabled cores, or if specified set number\n"
		"                         of max cores.  Set 0 to use max\n"
     		"  -t  --temp             Display board temperature\n"
		"  -h, --help             This message\n"
		"\n",
		argv[0]
	);
}

static int get_cpu_rate(uint8_t strap)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cpusar); i++)
	{
		if((strap & 0x1c) == cpusar[i]) {
			return cpurates[i];
		}
	}

	return -1;
}

static int get_cpu_cores(uint8_t strap)
{
	if(strap & 0x40)
		return 2;
	return 1;
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
			perror("SiLabs did not ACK\n");
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

static uint8_t nvram_read(int twifd, uint8_t addr)
{
	uint8_t val;
	silabs_read(twifd, &val, addr + 1536, 1);
	return val;
}

static void nvram_write(int twifd, uint8_t addr, uint8_t value)
{
	silabs_write(twifd, &value, addr + 1536, 1);
}

/** must be in the form XX:XX:XX:XX:XX:XX, where XX is a hex number */
static int parse_mac_address(const char *str, unsigned char *buf)
{
	int n;
	unsigned int addr[6];

	if (sscanf(str, "%x:%x:%x:%x:%x:%x",
		&addr[5],&addr[4],&addr[3],
		&addr[2],&addr[1],&addr[0]) != 6)
	return 0;

	for(n=0; n < 6; n++) {
		if (addr[n] > 255) return 0;
		buf[n] = addr[n];
	}

   return 1;
}

int get_cputemp()
{
	char buf[32];
	FILE *f = fopen("/sys/devices/virtual/thermal/thermal_zone0/temp", "r");
	fread(buf, 1, 32, f);
	return atoi(buf);
}

struct modelinfo *get_build_variant(int model)
{
	/* Until we have straps implemented, just assume the only build */
	if(model == 0x7820) {
		return &ts7820_models[0];
	} else if (model == 0x7840) {
		return &ts7840_models[0];
	}
	return NULL;
}

int main(int argc, char **argv)
{
	int c, twifd, model;
	unsigned char new_mac[6];
	int opt_info = 0, display_mac = 0, set_mac = 0;
	int display_cores = 0, display_rate = 0;
	int cpu_cores = -1, cpu_rate = -1;
	int mem;
	volatile uint32_t *fpga_bar0;
	uint32_t fpga_bar0_addr;

	static struct option long_options[] = {
		{ "info", 0, 0, 'i' },
		{ "macaddr", optional_argument, 0, 'm' },
		{ "nvadd", required_argument, 0, 'A' },
		{ "nvrw", optional_argument, 0, 'D' },
		{ "rate", optional_argument, 0, 'l' },
		{ "cores", optional_argument, 0, 'c' },
		{ "temp", 0, 0, 't' },
		{ "help", 0, 0, 'h' },
		{ 0, 0, 0, 0 }
	};

	if(argc == 1) {
		usage(argv);
		return 1;
	}

	if ((fpga_bar0_addr = get_fpga_phy()) == 0) {
		fprintf(stderr, "Warning:  Did not discover FPGA base from PCI probe\n");
		fpga_bar0_addr = (uint32_t)0xe4080000;
	}
	mem = open("/dev/mem", O_RDWR|O_SYNC);
	fpga_bar0 = mmap(0, 
			 getpagesize(),
			 PROT_READ|PROT_WRITE,
			 MAP_SHARED,
			 mem,
			 fpga_bar0_addr); 

	model = get_model();
	if(!model) {
		fprintf(stderr, "Unsupported model 0x%X\n", model);
		return 1;
	}
	
	twifd = silabs_init();
	if(twifd == -1) {
		fprintf(stderr, "ERROR: Cannot initialize connection to Silabs via /dev/i2c-0\n");
		return 1;
	}

	while((c = getopt_long(argc, argv, "im::A:d::l::c::th", long_options, NULL)) != -1) {
		switch(c) {
		case 'i':
			opt_info = 1;
			display_mac = 1;
			break;
		case 'm':
			if(optarg) {
				if (!parse_mac_address(optarg, new_mac)) {
					fprintf(stderr, "Invalid MAC: %s\n", optarg);
					return 1;
				}
				set_mac = 1;
			}
			display_mac = 1;
			break;

		case 'c':
			if(optarg) {
				cpu_cores = atoi(optarg);
			}
			display_cores = 1;
			break;

		case 'l':
			if(optarg) {
				cpu_rate = atoi(optarg);
			}
			display_rate = 1;
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

	if (set_mac) {
		silabs_write(twifd, new_mac, 1536, sizeof(new_mac));
		fprintf(stderr, "Microcontroller eeprom written; A power-cycle is needed before the new settings take effect\n");
	}

	if (opt_info){
		struct modelinfo *variant = get_build_variant(model);
		uint8_t strap = nvram_read(twifd, 6);
		uint32_t reg = fpga_bar0[0x100/4];

		printf("model=%s\n", variant->name);
		printf("fpga_rev=%d\n", reg & 0xff);
		printf("max_cores=%d\n", variant->maxcores);
		printf("max_rate=%d\n", variant->maxrate);
		printf("current_cores=%d\n", get_cpu_cores(strap));
		printf("current_rate_mhz=%d\n", get_cpu_rate(strap));
		printf("cpu_millicelcius=%d\n", get_cputemp());
	}

	if (display_mac) {
		unsigned char mac[6];
		memset(mac, 0, sizeof(mac));

		if(silabs_read(twifd, mac, 1536, sizeof(mac))){
			perror("Failed to talk to silabs!");
			return 1;
		}

		printf("mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
			mac[5],mac[4],mac[3],mac[2],mac[1],mac[0]);
	}

	if(cpu_cores != -1 || cpu_rate != -1)
	{
		uint8_t strap, new_strap;
		int i;
		struct modelinfo *variant = get_build_variant(model);
		strap = new_strap = nvram_read(twifd, 6);

		if(variant == NULL) {
			fprintf(stderr, "Invalid variant, can't set rate!\n");
			return 1;
		}

		if(cpu_cores > variant->maxcores) {
			fprintf(stderr, "Requested %d cores, max supported by %s is %d\n",
			cpu_cores,
			variant->name,
			variant->maxcores);
			exit(1);
		}

		if(cpu_rate > variant->maxrate) {
			fprintf(stderr, "Requested %dMhz cpu clock, max supported by %s is %dMhz\n",
			cpu_rate,
			variant->name,
			variant->maxrate);
			exit(1);
		}

		/* If the user specifies 0, correct to max supported by this variant */
		if(cpu_cores == 0)
			cpu_cores = variant->maxcores;
		if(cpu_rate == 0)
			cpu_rate = variant->maxrate;

		if(cpu_cores == 1)
			new_strap &= ~(0x40);
		else if(cpu_cores == 2)
			new_strap |= 0x40;

		if(cpu_rate != -1) {
			new_strap &= 0xe0;
			for (i = 0; i < ARRAY_SIZE(cpurates); i++)
			{
				if(cpu_rate == cpurates[i]) {
					new_strap |= cpusar[i];
					break;
				}
			}
		}

		if(new_strap == strap){
			fprintf(stderr, "Requested strap value 0x%X is already set\n", new_strap);
		} else {
			nvram_write(twifd, 6, new_strap);
			fprintf(stderr, "Strap value 0x%X is set, disconnect power and usb console to use new values\n", new_strap);
		}
	}

	if(display_cores)
	{
		uint8_t strap = nvram_read(twifd, 6);
		printf("current_rate=%dMHz\n", get_cpu_rate(strap));
	}

	if(display_rate)
	{
		uint8_t strap = nvram_read(twifd, 6);
		printf("current_cores=%d\n", get_cpu_cores(strap));
	}

	return 0;
}

