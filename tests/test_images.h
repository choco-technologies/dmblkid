/**
 * @file test_images.h
 * @brief Builders of the disk images the dmblkid tests probe
 *
 * Images are sparse files: only the structures a prober reads are written,
 * so multi-GiB images cost a few sectors of disk space.
 */
#ifndef TEST_IMAGES_H
#define TEST_IMAGES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TEST_MIB    (1024ull * 1024ull)
#define TEST_GIB    (1024ull * TEST_MIB)

/* ---- raw file access ---- */

/** Create (truncate) @p path with @p size bytes of zeros. */
bool test_image_create(const char* path, uint64_t size);

/** Write @p size bytes at @p offset of an existing image. */
bool test_image_write(const char* path, uint64_t offset, const void* data, size_t size);

/** Read @p size bytes at @p offset. */
bool test_image_read(const char* path, uint64_t offset, void* data, size_t size);

/** A 32-bit checksum of the first @p size bytes - to prove probing did not write. */
uint32_t test_image_checksum(const char* path, uint64_t size);

/* ---- filesystems ---- */

/** 1.44 MB FAT12 floppy: label "FLOPPY" in the root directory, "NO NAME" in the BPB, serial 1234-ABCD. */
bool test_build_fat12(const char* path);

/** 32 MiB FAT16: label "DATA16" only in the BPB, serial 0BAD-F00D. */
bool test_build_fat16(const char* path);

/** 64 MiB FAT32: root directory label "BOOT" (after a deleted and an LFN entry), BPB "OLDLABEL", serial CAFE-BABE. */
bool test_build_fat32(const char* path);

/**
 * 5 GiB exFAT with its cluster heap (and label "Karta ł") above 4 GiB,
 * serial 5EED-0001, revision 1.0. @p valid_checksum false breaks the boot region checksum.
 */
bool test_build_exfat(const char* path, bool valid_checksum);

/** dmffs image: VERSION "1.0", a FILE and a DIR, END. */
bool test_build_dmffs(const char* path);

/* ---- partition tables ---- */

/** 64 MiB MBR disk, signature 1a2b3c4d: primary 1 (FAT32 LBA) and one logical partition (5). */
bool test_build_mbr(const char* path);

/**
 * GPT disk of @p size bytes with @p block_size blocks and two partitions.
 * Disk GUID 01234567-89ab-cdef-0011-223344556677. @p damage_primary breaks
 * the primary header so only the backup at the last LBA is valid.
 */
bool test_build_gpt(const char* path, uint64_t size, uint32_t block_size, bool damage_primary);

#endif // TEST_IMAGES_H
