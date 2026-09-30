#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "dmblkid.h"
#include "test_images.h"
#include <errno.h>
#include <string.h>

/*
 * Every step builds a (sparse) image file, probes it through dmblkid_probe()
 * - the same file API path a /dev node takes on target - and checks the
 * result. The image is removed in the teardown.
 */

#define IMAGE_PATH  "dmblkid_test.img"

static dmblkid_t* g_result = NULL;

void dmod_test_setup(void)
{
    g_result = NULL;
}

void dmod_test_teardown(void)
{
    dmblkid_destroy(g_result);
    g_result = NULL;
    Dmod_FileRemove(IMAGE_PATH);
}

static bool str_is(const char* value, const char* expected)
{
    return value != NULL && strcmp(value, expected) == 0;
}

/* Probe IMAGE_PATH into g_result; true on success. */
static bool probe(void)
{
    return dmblkid_probe(IMAGE_PATH, &g_result) == 0 && g_result != NULL;
}

static void expect_filesystem(const char* type, const char* version, const char* label, const char* uuid)
{
    DMOD_TEST_EXPECT_EQ(dmblkid_get_usage(g_result), dmblkid_usage_filesystem);
    DMOD_TEST_EXPECT_TRUE(str_is(dmblkid_get_type(g_result), type));
    DMOD_TEST_EXPECT_TRUE(str_is(dmblkid_get_version(g_result), version));
    DMOD_TEST_EXPECT_TRUE(label == NULL ? dmblkid_get_label(g_result) == NULL
                                        : str_is(dmblkid_get_label(g_result), label));
    DMOD_TEST_EXPECT_TRUE(str_is(dmblkid_get_uuid(g_result), uuid));
    DMOD_TEST_EXPECT_EQ(dmblkid_get_partition_count(g_result), 0u);
}

static void expect_unknown(void)
{
    DMOD_TEST_EXPECT_EQ(dmblkid_get_usage(g_result), dmblkid_usage_unknown);
    DMOD_TEST_EXPECT_NULL(dmblkid_get_type(g_result));
    DMOD_TEST_EXPECT_NULL(dmblkid_get_module(g_result));
    DMOD_TEST_EXPECT_FALSE(dmblkid_is_mountable(g_result));
}

/* FAT and exFAT are mounted by dmfatfs, straight from the node. */
static void expect_dmfatfs(void)
{
    DMOD_TEST_EXPECT_TRUE(str_is(dmblkid_get_module(g_result), "dmfatfs"));
    DMOD_TEST_EXPECT_TRUE(dmblkid_is_mountable(g_result));
}

/* ---- filesystems ---- */

DMOD_TEST_STEP(fat12_floppy_with_mbr_like_boot_code)
{
    DMOD_TEST_EXPECT_TRUE(test_build_fat12(IMAGE_PATH));
    DMOD_TEST_EXPECT_TRUE(probe());
    expect_filesystem("vfat", "FAT12", "FLOPPY", "1234-ABCD");
    DMOD_TEST_EXPECT_EQ(dmblkid_get_size(g_result), 2880ull * 512u);
    expect_dmfatfs();
}

DMOD_TEST_STEP(fat16_label_from_bpb)
{
    DMOD_TEST_EXPECT_TRUE(test_build_fat16(IMAGE_PATH));
    DMOD_TEST_EXPECT_TRUE(probe());
    expect_filesystem("vfat", "FAT16", "DATA16", "0BAD-F00D");
    expect_dmfatfs();
}

DMOD_TEST_STEP(fat32_label_from_root_directory)
{
    DMOD_TEST_EXPECT_TRUE(test_build_fat32(IMAGE_PATH));
    DMOD_TEST_EXPECT_TRUE(probe());
    expect_filesystem("vfat", "FAT32", "BOOT", "CAFE-BABE");
    expect_dmfatfs();
}

DMOD_TEST_STEP(exfat_above_4gib)
{
    DMOD_TEST_EXPECT_TRUE(test_build_exfat(IMAGE_PATH, true));
    DMOD_TEST_EXPECT_TRUE(probe());
    /* The label lives in the cluster heap at 4.5 GiB. */
    expect_filesystem("exfat", "1.0", "Karta \xC5\x82", "5EED-0001");
    DMOD_TEST_EXPECT_EQ(dmblkid_get_size(g_result), 5ull * TEST_GIB);
    expect_dmfatfs();
}

DMOD_TEST_STEP(exfat_bad_boot_checksum)
{
    DMOD_TEST_EXPECT_TRUE(test_build_exfat(IMAGE_PATH, false));
    DMOD_TEST_EXPECT_TRUE(probe());
    expect_unknown();
}

DMOD_TEST_STEP(dmffs_image)
{
    DMOD_TEST_EXPECT_TRUE(test_build_dmffs(IMAGE_PATH));
    DMOD_TEST_EXPECT_TRUE(probe());
    DMOD_TEST_EXPECT_EQ(dmblkid_get_usage(g_result), dmblkid_usage_filesystem);
    DMOD_TEST_EXPECT_TRUE(str_is(dmblkid_get_type(g_result), "dmffs"));
    DMOD_TEST_EXPECT_TRUE(str_is(dmblkid_get_version(g_result), "1.0"));
    DMOD_TEST_EXPECT_TRUE(str_is(dmblkid_get_module(g_result), "dmffs"));
    DMOD_TEST_EXPECT_FALSE(dmblkid_is_mountable(g_result));     /* dmffs mounts flash, not nodes */
    DMOD_TEST_EXPECT_NULL(dmblkid_get_label(g_result));
}

DMOD_TEST_STEP(dmffs_broken_chain)
{
    uint8_t length[4] = { 0xFF, 0xFF, 0xFF, 0x00 };     /* FILE TLV running past the end */
    DMOD_TEST_EXPECT_TRUE(test_build_dmffs(IMAGE_PATH));
    DMOD_TEST_EXPECT_TRUE(test_image_write(IMAGE_PATH, 15, length, sizeof(length)));
    DMOD_TEST_EXPECT_TRUE(probe());
    expect_unknown();
}

/* ---- partition tables ---- */

DMOD_TEST_STEP(mbr_with_logical_partition)
{
    DMOD_TEST_EXPECT_TRUE(test_build_mbr(IMAGE_PATH));
    DMOD_TEST_EXPECT_TRUE(probe());
    DMOD_TEST_EXPECT_EQ(dmblkid_get_usage(g_result), dmblkid_usage_partition_table);
    DMOD_TEST_EXPECT_TRUE(str_is(dmblkid_get_type(g_result), "mbr"));
    DMOD_TEST_EXPECT_TRUE(str_is(dmblkid_get_uuid(g_result), "1a2b3c4d"));
    DMOD_TEST_EXPECT_EQ(dmblkid_get_partition_count(g_result), 2u);
    DMOD_TEST_EXPECT_EQ(dmblkid_get_block_size(g_result), 512u);
    DMOD_TEST_EXPECT_FALSE(dmblkid_is_mountable(g_result));
}

static void expect_gpt(uint32_t block_size)
{
    DMOD_TEST_EXPECT_EQ(dmblkid_get_usage(g_result), dmblkid_usage_partition_table);
    DMOD_TEST_EXPECT_TRUE(str_is(dmblkid_get_type(g_result), "gpt"));
    DMOD_TEST_EXPECT_TRUE(str_is(dmblkid_get_uuid(g_result), "01234567-89ab-cdef-0011-223344556677"));
    DMOD_TEST_EXPECT_EQ(dmblkid_get_partition_count(g_result), 2u);
    DMOD_TEST_EXPECT_EQ(dmblkid_get_block_size(g_result), block_size);
    DMOD_TEST_EXPECT_FALSE(dmblkid_is_mountable(g_result));
}

DMOD_TEST_STEP(gpt_disk)
{
    DMOD_TEST_EXPECT_TRUE(test_build_gpt(IMAGE_PATH, 64u * TEST_MIB, 512, false));
    DMOD_TEST_EXPECT_TRUE(probe());
    expect_gpt(512);
}

DMOD_TEST_STEP(gpt_backup_header_above_4gib)
{
    DMOD_TEST_EXPECT_TRUE(test_build_gpt(IMAGE_PATH, 6u * TEST_GIB, 512, true));
    DMOD_TEST_EXPECT_TRUE(probe());
    expect_gpt(512);
    DMOD_TEST_EXPECT_EQ(dmblkid_get_size(g_result), 6ull * TEST_GIB);
}

DMOD_TEST_STEP(gpt_4k_blocks)
{
    DMOD_TEST_EXPECT_TRUE(test_build_gpt(IMAGE_PATH, 64u * TEST_MIB, 4096, false));
    DMOD_TEST_EXPECT_TRUE(probe());
    expect_gpt(4096);
}

/* ---- nothing to find ---- */

DMOD_TEST_STEP(blank_device)
{
    DMOD_TEST_EXPECT_TRUE(test_image_create(IMAGE_PATH, TEST_MIB));
    DMOD_TEST_EXPECT_TRUE(probe());
    expect_unknown();
    DMOD_TEST_EXPECT_NULL(dmblkid_get_uuid(g_result));
    DMOD_TEST_EXPECT_EQ(dmblkid_get_size(g_result), TEST_MIB);
}

DMOD_TEST_STEP(tiny_file)
{
    DMOD_TEST_EXPECT_TRUE(test_image_create(IMAGE_PATH, 100));
    DMOD_TEST_EXPECT_TRUE(probe());
    expect_unknown();
}

DMOD_TEST_STEP(empty_file)
{
    DMOD_TEST_EXPECT_TRUE(test_image_create(IMAGE_PATH, 0));
    DMOD_TEST_EXPECT_TRUE(probe());
    expect_unknown();
    DMOD_TEST_EXPECT_EQ(dmblkid_get_size(g_result), 0u);
}

/* Build the FAT32 image, overwrite @p size bytes at @p offset, expect nothing recognized. */
static void expect_damaged_fat32(uint32_t offset, const void* data, size_t size)
{
    DMOD_TEST_EXPECT_TRUE(test_build_fat32(IMAGE_PATH));
    DMOD_TEST_EXPECT_TRUE(test_image_write(IMAGE_PATH, offset, data, size));
    DMOD_TEST_EXPECT_TRUE(probe());
    expect_unknown();
    dmblkid_destroy(g_result);
    g_result = NULL;
}

DMOD_TEST_STEP(damaged_boot_sector)
{
    uint8_t zero[2] = { 0, 0 };
    uint8_t bps[2] = { 0x00, 0x03 };                    /* 768: not a power of 2 */
    uint8_t fat_size[4] = { 0x10, 0, 0, 0 };            /* 16 sectors cannot map 129k clusters */
    uint8_t total[4] = { 0x00, 0x00, 0x04, 0x00 };      /* 256k sectors: more than the 64 MiB node */
    expect_damaged_fat32(510, zero, sizeof(zero));      /* no 0x55AA */
    expect_damaged_fat32(11, bps, sizeof(bps));
    expect_damaged_fat32(36, fat_size, sizeof(fat_size));
    expect_damaged_fat32(32, total, sizeof(total));
    expect_damaged_fat32(0, zero, sizeof(zero));        /* no jump instruction */
}

/* ---- API contract ---- */

DMOD_TEST_STEP(probe_never_writes)
{
    DMOD_TEST_EXPECT_TRUE(test_build_fat32(IMAGE_PATH));
    uint64_t checked = 2200u * 512u;                    /* boot sector, FATs, root directory */
    uint32_t before = test_image_checksum(IMAGE_PATH, checked);
    DMOD_TEST_EXPECT_TRUE(probe());
    DMOD_TEST_EXPECT_EQ(test_image_checksum(IMAGE_PATH, checked), before);
    DMOD_TEST_EXPECT_NE(before, 0u);
}

DMOD_TEST_STEP(bad_arguments)
{
    dmblkid_t* result = (dmblkid_t*)&result;
    DMOD_TEST_EXPECT_EQ(dmblkid_probe(NULL, &result), -EINVAL);
    DMOD_TEST_EXPECT_EQ(dmblkid_probe(IMAGE_PATH, NULL), -EINVAL);
    DMOD_TEST_EXPECT_EQ(dmblkid_probe("dmblkid_no_such_node.img", &result), -ENOENT);
    DMOD_TEST_EXPECT_NULL(result);
}

DMOD_TEST_STEP(accessors_reject_invalid_results)
{
    uint32_t garbage[16] = { 0 };
    const dmblkid_t* bogus = (const dmblkid_t*)garbage;
    DMOD_TEST_EXPECT_EQ(dmblkid_get_usage(NULL), dmblkid_usage_unknown);
    DMOD_TEST_EXPECT_NULL(dmblkid_get_type(bogus));
    DMOD_TEST_EXPECT_NULL(dmblkid_get_label(bogus));
    DMOD_TEST_EXPECT_EQ(dmblkid_get_size(bogus), 0u);
    DMOD_TEST_EXPECT_FALSE(dmblkid_is_mountable(bogus));
    dmblkid_destroy(NULL);
    dmblkid_destroy((dmblkid_t*)garbage);               /* ignored: no magic */
}
