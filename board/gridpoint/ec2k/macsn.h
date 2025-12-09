#ifndef MACSN_H
#define MACSN_H

struct __attribute__ ((__packed__)) MAC_SN_V3
{
	uint8_t sn[17];
	uint8_t key_size;
	uint8_t key[97];
};

struct __attribute__ ((__packed__)) MAC_SN_V4
{
	uint32_t sn; // big-endian
	uint8_t compressed_data[77];
	uint8_t key_size;
	uint8_t key[33];
};

struct __attribute__ ((__packed__)) MAC_SN
{
	uint8_t date[6];
	uint8_t version; // macsn format version
	uint8_t hwrev[2];
	union
	{
		struct MAC_SN_V3 v3;
		struct MAC_SN_V4 v4;
	} v;
	uint32_t crc;
	uint8_t padding[122];
	uint8_t mac[6];
};

struct MAC_SN* read_macsn(void);
int save_macsn(void);

char* get_macsn_sn(void);

#endif // MACSN_H
