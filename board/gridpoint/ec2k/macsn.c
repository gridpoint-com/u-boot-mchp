#include <command.h>
#include <config.h>
#include <dm.h>
#include <env.h>
#include <i2c_eeprom.h>
#include <linux/ctype.h>
#include <u-boot/crc.h>
#include <net.h>
#include <nand.h>
#include "macsn.h"

#define DEBUG 0

#define MACSN_OFFSET 0

static struct MAC_SN m;

static int has_been_read = 0;

static void update_crc(void);

static bool is_sn_set(void)
{
	switch (m.version) {
	case 3:
		return !((m.v.v3.sn[0] == 0xff) || (m.v.v3.sn[0] == 0x00));
	case 4:
		return m.v.v4.sn != ULONG_MAX;
	default:
		return false;
	}
}

static bool set_sn_v3(const char* sn)
{
	memset(m.v.v3.sn, 0, sizeof(m.v.v3.sn));
	strncpy((char *)m.v.v3.sn, sn, sizeof(m.v.v3.sn) - 1);
	return true;
}

static bool set_sn_v4(const char* sn)
{
	if (strncmp(sn, "EC2KWRG", 7)) {
		printf("Invalid Serial Number prefix\n");
		return false;
	}

	u32 sn_value = strtoul(&sn[7], NULL, 10);
	if (sn_value == ULONG_MAX) {
		printf("Invalid Serial Number");
		return false;
	}

	m.v.v4.sn = cpu_to_be32(sn_value);

	return true;
}

static bool set_sn(const char* sn)
{
	bool is_set = false;
	switch(m.version) {
	case 3:
		is_set = set_sn_v3(sn);
		break;
	case 4:
		is_set = set_sn_v4(sn);
		break;
	default:
		break;
	}

	if (is_set)
		update_crc();

	return is_set;
}

/**
 * show_macsn - display the contents of the EEPROM
 */
static void show_macsn(void)
{
	unsigned int crc;

	if (!has_been_read)
		read_macsn();

	printf("  VER: %d\n", m.version);
	if (!is_valid_ethaddr(m.mac))
		printf("  MAC: Not Set!\n");
	else
		printf("  MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
			m.mac[0], m.mac[1], m.mac[2], m.mac[3], m.mac[4], m.mac[5]);

	if (!is_sn_set())
		printf("   SN: Not Set!\n");
	else
		printf("   SN: %s\n", get_macsn_sn());

	printf("HWrev: %02x.%02x\n", m.hwrev[0], m.hwrev[1]);

	/* Build date, BCD date values, as YYMMDDhhmmss */
	if (m.date[0] == 0xff)
		printf(" Date: Not Set!\n");
	else
		printf(" Date: 20%02d/%02d/%02d %02d:%02d:%02d\n",
			m.date[0], m.date[1], m.date[2],
			m.date[3] & 0x7F, m.date[4], m.date[5]);

	crc = crc32(0, (void *)&m, 128 - sizeof(m.crc));

	if (crc == m.crc)
		printf("  CRC: %08x (valid)\n", m.crc);
	else
		printf("  CRC: %08x (should be %08x)\n", m.crc, crc);

#if DEBUG
	{
		int i;
		printf("MACSN dump: (0x%x bytes)\n", sizeof(m));
		for (i = 0; i < sizeof(m); i++) {
			if ((i % 16) == 0)
				printf("%02X: ", i);
			printf("%02X ", ((u8 *)&m)[i]);
			if (((i % 16) == 15) || (i == sizeof(m) - 1))
				printf("\n");
		}
	}
#endif
}

struct MAC_SN* read_macsn(void)
{
	struct udevice *dev;
	int ret = 0;
	size_t len = sizeof(m);

	if (has_been_read)
		return &m;

	ret = uclass_first_device_err(UCLASS_I2C_EEPROM, &dev);
	if (ret) {
		printf("Error - unable to init I2C EEPROM read macsn\n");
		goto err_read;
	}

	printf("%s: %s\n", __func__, dev->name);
	ret = i2c_eeprom_read(dev, MACSN_OFFSET, (u_char*)&m, len);
	if (ret) {
		printf("Error - unable to read MACSN\n");
		goto err_read;
	}

err_read:
	has_been_read = (ret == 0) ? 1 : 0;

	return (has_been_read ? &m : NULL);
}

int save_macsn(void)
{
	struct udevice *dev;
	int ret = 0;
	size_t len = sizeof(m);

	if (is_valid_ethaddr(m.mac) && (env_get("ethaddr") == NULL)) {

		uint8_t tmp[20];

		sprintf((char *)tmp, "%02x:%02x:%02x:%02x:%02x:%02x",
			m.mac[0], m.mac[1], m.mac[2],
			m.mac[3], m.mac[4], m.mac[5]);

		env_set("ethaddr", (char *)tmp);
	}

	if (is_sn_set() && (env_get("serial#") == NULL)) {
		env_set("serial#", get_macsn_sn());
	}

	ret = uclass_first_device_err(UCLASS_I2C_EEPROM, &dev);
	if (ret) {
		printf("Error - unable to init I2C EEPROM save macsn\n");
		goto err_write;
	}

	printf("%s: %s\n", __func__, dev->name);
	ret = i2c_eeprom_write(dev, MACSN_OFFSET, (u_char*)&m, len);
	if (ret) {
		printf("Error - unable to write MACSN to I2C EEPROM: %d\n", ret);
		goto err_write;
	}

err_write:
	if (ret) {
		printf("Programming MACSN failed.\n");
		return -1;
	}

	printf("Programming MACSN passed.\n");
	return 0;
}

/**
 * update_crc - update the CRC
 *
 * This function should be called after each update to the EEPROM structure,
 * to make sure the CRC is always correct.
 */
static void update_crc(void)
{
	m.crc = crc32(0, (void *)&m, 128 - sizeof(m.crc));
}

/**
 * h2i - converts hex character into a number
 *
 * This function takes a hexadecimal character (e.g. '7' or 'C') and returns
 * the integer equivalent.
 */
static inline u8 h2i(char p)
{
	if ((p >= '0') && (p <= '9'))
		return p - '0';

	if ((p >= 'A') && (p <= 'F'))
		return (p - 'A') + 10;

	if ((p >= 'a') && (p <= 'f'))
		return (p - 'a') + 10;

	return 0;
}

char* get_macsn_sn(void)
{
	if (!has_been_read)
		read_macsn();

	if (m.version == 3) {
		return m.v.v3.sn;
	} else if (m.version == 4) {
		static char sn[17];
		snprintf(sn, sizeof(sn), "EC2KWRG%09d", cpu_to_be32(m.v.v4.sn));
		return sn;
	}

	return NULL;
}

/**
 * set_date - stores the build date into the SPI flash
 *
 * This function takes a pointer to a string in the format "YYMMDDhhmmss"
 * (2-digit year, 2-digit month, etc), converts it to a 6-byte BCD string,
 * and stores it in the date field of the local copy.
 */
static void set_date(const char *string)
{
	unsigned int i;

	if (strlen(string) != 12) {
		printf("Usage: mac date YYMMDDhhmmss\n");
		return;
	}

	for (i = 0; i < 6; i++)
		m.date[i] = (string[2 * i] - '0') * 10 + (string[2 * i + 1] - '0');

	update_crc();
}

/**
 * set_hwrev - stores the hw rev info to SPI flash
 *
 * This function takes a pointer to HWrev string
 * (i.e."XX.YY", where "XX" is the major number and "YY" is the minor number)
 * and stores it in the HWrev field of the local copy.
 */
static void set_hwrev(const char *string)
{
	char *p = (char *) string;
	unsigned int i;

	if (!string) {
		printf("Usage: macsn hwrev XX.YY\n");
		return;
	}

	for (i = 0; *p && (i < 2); i++) {
		m.hwrev[i] = simple_strtoul(p, &p, 16);
		if (*p == '.')
			p++;
	}

	update_crc();
}

#define CMD_SET_SERIAL_NUMBER 's'
#define CMD_SET_DATE 'd'
#define CMD_SET_HW_REV 'h'
#define CMD_READ 'r'
#define CMD_WRITE 'w'

int do_macsn(struct cmd_tbl *cmdtp, int flag, int argc, char * const argv[])
{
	char cmd;

	if (argc == 1) {
		show_macsn();
		return 0;
	}

	cmd = argv[1][0];

	if (cmd == CMD_READ && argc == 2) {
		show_macsn();
		return 0;
	}

	if (argc == 2) {
		switch (cmd) {
		case CMD_WRITE:
			save_macsn();
			break;
		default:
			cmd_usage(cmdtp);
			break;
		}

		return 0;
	}

	/* We know we have at least one parameter */

	switch (cmd) {
	case CMD_SET_SERIAL_NUMBER:
		if (is_sn_set()) {
			printf("Serial Number already set\n");
		} else if (!set_sn(argv[2])) {
			printf("Unable to set Serial Number\n");
		}
		break;
	case CMD_SET_DATE: /* date BCD format YYMMDDhhmmss */
		set_date(argv[2]);
		break;
	case CMD_SET_HW_REV:
		set_hwrev(argv[2]);
		break;
	default:
		cmd_usage(cmdtp);
		break;
	}

	return 0;
}

U_BOOT_CMD(
	macsn, 3, 0, do_macsn,
	"display the MAC address and program serial#, hw rev, and ref id in I2C EEPROM flash",
	"[read|write|sn|hwrev|date|refid|passwd]\n"
	"macsn read\n"
	"    - show content of MACSN\n"
	"macsn write\n"
	"    - write MACSN to the SPI Flash\n"
	"macsn sn\n"
	"    - program serial number\n"
	"macsn hwrev\n"
	"    - program hardware rev\n"
	"macsn date\n"
	"    - program date YYMMDDhhmmss\n"
);
