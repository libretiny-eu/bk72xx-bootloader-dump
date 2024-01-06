/* Copyright (c) Kuba Szczodrzyński 2024-01-04. */

#include <libretiny.h>

__attribute__((packed)) typedef struct {
	char magic[4];
	uint32_t ota_alg;
	uint32_t timestamp;
	char name[16];
	char version[24];
	char serial_number[24];
	uint32_t crc;
	uint32_t hash;
	uint32_t size_raw;
	uint32_t size_packaged;
	uint32_t header_crc;
} bk_rbl_hdr_t;

__attribute__((packed)) typedef struct {
	char magic[4];
	char part_name[24];
	char flash_name[24];
	uint32_t offset;
	uint32_t length;
	uint32_t reserved;
} fal_part_t;

static const char RBL_MAGIC[4] = "RBL\0";
static const int RBL_SIZE	   = sizeof(bk_rbl_hdr_t);
static const int RBL_SIZE_CRC  = sizeof(bk_rbl_hdr_t) / 32 * 34;

static const char BK7231_MAGIC[8]		 = "BK7231\0\0";
static const int BK7231_MAGIC_OFFSET	 = 0x100;
static const int BK7231_MAGIC_OFFSET_CRC = 0x100 / 32 * 34;

static const char FAL_MAGIC[4]	  = "01PE";
static const int FAL_SIZE		  = sizeof(fal_part_t);
static const int FAL_SIZE_CRC	  = sizeof(fal_part_t) / 32 * 34;
static const char FAL_BEKEN_CRC[] = "beken_onchip_crc";

extern uint32_t crc32(const void *buf, uint32_t size);

bool read_rbl(bk_rbl_hdr_t *rbl) {
	uint8_t buf[RBL_SIZE_CRC];

	for (uint32_t end = 0xEE00; end <= 0x11000; end += 0x1100) {
		if (lt_flash_read(end - RBL_SIZE_CRC, buf, RBL_SIZE_CRC) != RBL_SIZE_CRC) {
			LT_W("Couldn't read flash @ 0x%x-102", end);
			continue;
		}
		if (memcmp(buf, RBL_MAGIC, 4) != 0)
			continue;

		LT_I("Found RBL @ 0x%x-102", end);
		memcpy((uint8_t *)rbl + 0, buf + 0, 32);
		memcpy((uint8_t *)rbl + 32, buf + 34, 32);
		memcpy((uint8_t *)rbl + 64, buf + 68, 32);
		return true;
	}
	return false;
}

int main() {
	LT_I("BK72xx Bootloader Dump\n");

	bk_rbl_hdr_t rbl;
	if (!read_rbl(&rbl)) {
		LT_E("Couldn't find or read RBL. Dumping unprocessed bootloader...");
		hexdump((const uint8_t *)0x0, 0xFFFF, 0, 16);
		while (1) {}
	}

	LT_I("");
	LT_I("RBL Header:");
	LT_I(" - magic='%s'", rbl.magic);
	LT_I(" - ota_alg=%08x", rbl.ota_alg);
	LT_I(" - timestamp=%d", rbl.timestamp);
	LT_I(" - name='%s'", rbl.name);
	LT_I(" - version='%s'", rbl.version);
	LT_I(" - serial_number='%s'", rbl.serial_number);
	LT_I(" - crc=%08x", rbl.crc);
	LT_I(" - hash=%08x", rbl.hash);
	LT_I(" - size_raw=%d", rbl.size_raw);
	LT_I(" - size_packaged=%d", rbl.size_packaged);
	LT_I(" - header_crc=%08x", rbl.header_crc);

	uint32_t header_crc_calc = crc32(&rbl, RBL_SIZE - sizeof(uint32_t));
	if (header_crc_calc != rbl.header_crc) {
		LT_E("Header CRC check failed! Calculated: %08x, found: %08x", header_crc_calc, rbl.header_crc);
	} else {
		LT_I("Header CRC OK!");
	}
	LT_I("");

	if (rbl.size_raw > 0x10000) {
		LT_F("RBL raw size too large!");
		while (1) {}
	}

	uint8_t *bootloader = malloc(rbl.size_raw);
	if (bootloader == NULL) {
		LT_F("malloc failed!");
		while (1) {}
	}

	LT_I("Copying bootloader from memory...");
	memcpy(bootloader, (const void *)0x0, rbl.size_raw);

	uint8_t bk_marker[8];
	lt_flash_read(BK7231_MAGIC_OFFSET_CRC, bk_marker, 8);
	if (memcmp(bk_marker, BK7231_MAGIC, sizeof(BK7231_MAGIC)) == 0) {
		LT_I(" - BK7231 marker @ 0x%x, copying", BK7231_MAGIC_OFFSET_CRC);
		memcpy(bootloader + BK7231_MAGIC_OFFSET, bk_marker, sizeof(BK7231_MAGIC));
	} else {
		LT_W(" - BK7231 marker not found @ 0x%x", BK7231_MAGIC_OFFSET_CRC);
	}

	uint8_t fal_buf[FAL_SIZE_CRC];
	for (uint32_t fal_addr = rbl.size_raw - FAL_SIZE; fal_addr >= rbl.size_raw - FAL_SIZE * 10; fal_addr -= FAL_SIZE) {
		uint32_t fal_offs = (fal_addr / 32 * 34) + (fal_addr % 32);
		lt_flash_read(fal_offs, fal_buf, FAL_SIZE_CRC);

		uint32_t crc_start = 32 - (fal_addr % 32);
		for (uint32_t crc_offs = crc_start; crc_offs < FAL_SIZE_CRC; crc_offs += 32) {
			uint8_t tmp[FAL_SIZE_CRC];
			memcpy(tmp, fal_buf + crc_offs + 2, FAL_SIZE_CRC - crc_offs - 2);
			memcpy(fal_buf + crc_offs, tmp, FAL_SIZE_CRC - crc_offs - 2);
		}

		if (memcmp(fal_buf, FAL_MAGIC, 4) != 0)
			continue;
		fal_part_t *part = (fal_part_t *)fal_buf;

		LT_I(" - FAL partition '%s' (0x%06X+0x%X)", part->part_name, part->offset, part->length);
		// LT_I("    -> copied from logical:0x%x/physical:0x%x", fal_addr, fal_offs);
		if (strcmp(part->flash_name, FAL_BEKEN_CRC) == 0)
			LT_I("    -> calculated physical bounds: (0x%06X+0x%X)", part->offset / 32 * 34, part->length / 32 * 34);
		memcpy(bootloader + fal_addr, fal_buf, FAL_SIZE);
	}

	LT_I("");

	LT_I("Bootloader data:");
	hexdump(bootloader, rbl.size_raw, 0, 16);
	LT_I("Finished");
	while (1) {}
}
