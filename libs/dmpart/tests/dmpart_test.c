#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "dmpart.h"
#include <errno.h>
#include <string.h>

/*
 * dmpart_scan() on an in-memory medium: a sparse map of written blocks, all
 * others read as zeros - so multi-GiB media cost a few blocks of RAM.
 */

#define MAX_BLOCKS      80u
#define MAX_FOUND       8u

typedef struct
{
    uint32_t    block_size;
    uint64_t    block_count;
    uint64_t    lba[MAX_BLOCKS];
    uint8_t*    data[MAX_BLOCKS];
    uint32_t    used;
    bool        fail_reads;
} disk_t;

typedef struct
{
    uint32_t    count;
    uint32_t    stop_after;         /* 0: never stop */
    uint32_t    number[MAX_FOUND];
    uint64_t    first[MAX_FOUND];
    uint64_t    blocks[MAX_FOUND];
} found_t;

static disk_t g_disk;
static found_t g_found;
static dmpart_info_t g_info;

void dmod_test_setup(void)
{
    memset(&g_disk, 0, sizeof(g_disk));
    memset(&g_found, 0, sizeof(g_found));
    memset(&g_info, 0xA5, sizeof(g_info));
}

void dmod_test_teardown(void)
{
    for (uint32_t i = 0; i < g_disk.used; i++)
    {
        Dmod_Free(g_disk.data[i]);
    }
    g_disk.used = 0;
}

/* ---- the medium ---- */

static void disk_init(uint32_t block_size, uint64_t block_count)
{
    g_disk.block_size  = block_size;
    g_disk.block_count = block_count;
}

/* The block at @p lba, created (zeroed) on first use. */
static uint8_t* block(uint64_t lba)
{
    for (uint32_t i = 0; i < g_disk.used; i++)
    {
        if (g_disk.lba[i] == lba)
        {
            return g_disk.data[i];
        }
    }
    uint8_t* data = (g_disk.used < MAX_BLOCKS) ? Dmod_Malloc(g_disk.block_size) : NULL;
    if (data != NULL)
    {
        memset(data, 0, g_disk.block_size);
        g_disk.lba[g_disk.used]  = lba;
        g_disk.data[g_disk.used] = data;
        g_disk.used++;
    }
    return data;
}

static const uint8_t* find_block(uint64_t lba)
{
    for (uint32_t i = 0; i < g_disk.used; i++)
    {
        if (g_disk.lba[i] == lba)
        {
            return g_disk.data[i];
        }
    }
    return NULL;
}

/* Static: see dmblkid src/dmblkid_ptable.c - no external function addresses in modules. */
static int disk_read(void* ctx, uint64_t offset, void* buffer, size_t size)
{
    disk_t* disk = ctx;
    uint8_t* out = buffer;
    if (disk->fail_reads || offset + size > disk->block_count * disk->block_size)
    {
        return -EIO;
    }
    while (size > 0)
    {
        uint64_t lba = offset / disk->block_size;
        size_t in_block = (size_t)(offset % disk->block_size);
        size_t chunk = disk->block_size - in_block;
        chunk = (chunk < size) ? chunk : size;
        const uint8_t* data = find_block(lba);
        if (data != NULL)
        {
            memcpy(out, data + in_block, chunk);
        }
        else
        {
            memset(out, 0, chunk);
        }
        out += chunk;
        offset += chunk;
        size -= chunk;
    }
    return 0;
}

static bool on_found(void* ctx, uint32_t number, uint64_t first_lba, uint64_t lba_count)
{
    found_t* found = ctx;
    if (found->count < MAX_FOUND)
    {
        found->number[found->count] = number;
        found->first[found->count]  = first_lba;
        found->blocks[found->count] = lba_count;
    }
    found->count++;
    return found->stop_after == 0 || found->count < found->stop_after;
}

static int scan(void)
{
    dmpart_medium_t medium = { disk_read, &g_disk, g_disk.block_size, g_disk.block_count };
    return dmpart_scan(&medium, on_found, &g_found, &g_info);
}

/* No memcmp in modules. */
static bool bytes_equal(const uint8_t* a, const uint8_t* b, size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        if (a[i] != b[i])
        {
            return false;
        }
    }
    return true;
}

/* ---- builders ---- */

static void put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t* p, uint32_t v) { put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }
static void put64(uint8_t* p, uint64_t v) { put32(p, (uint32_t)v); put32(p + 4, (uint32_t)(v >> 32)); }

static void mbr_entry(uint64_t lba, int index, uint8_t status, uint8_t type, uint32_t first, uint32_t count)
{
    uint8_t* s = block(lba);
    uint8_t* e = s + 446 + 16 * index;
    e[0] = status;
    e[4] = type;
    put32(e + 8, first);
    put32(e + 12, count);
    s[510] = 0x55;
    s[511] = 0xAA;
}

static uint32_t crc32(const uint8_t* data, size_t size)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; i++)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++)
        {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

#define GPT_ENTRIES     128u
#define GPT_ENTRY_SIZE  128u

/* Entry array (16 KiB) starting at @p lba, two partitions; returns its CRC. */
static uint32_t gpt_entries(uint64_t lba, uint64_t first_usable, uint64_t last_usable)
{
    uint32_t per_block = g_disk.block_size / GPT_ENTRY_SIZE;
    uint32_t blocks = GPT_ENTRIES / per_block;
    uint8_t* array = Dmod_Malloc(GPT_ENTRIES * GPT_ENTRY_SIZE);
    memset(array, 0, GPT_ENTRIES * GPT_ENTRY_SIZE);
    memset(array, 0x11, 32);
    put64(array + 32, first_usable);
    put64(array + 40, first_usable + 99u);
    memset(array + 5u * GPT_ENTRY_SIZE, 0x22, 32);          /* entry 6: numbers follow the slot */
    put64(array + 5u * GPT_ENTRY_SIZE + 32, first_usable + 100u);
    put64(array + 5u * GPT_ENTRY_SIZE + 40, last_usable);
    for (uint32_t b = 0; b < blocks; b++)
    {
        memcpy(block(lba + b), array + b * g_disk.block_size, g_disk.block_size);
    }
    uint32_t crc = crc32(array, GPT_ENTRIES * GPT_ENTRY_SIZE);
    Dmod_Free(array);
    return crc;
}

static void gpt_header(uint64_t lba, uint64_t other, uint64_t entries_lba, uint32_t entries_crc,
                       uint64_t first_usable, uint64_t last_usable)
{
    uint8_t* h = block(lba);
    memcpy(h, "EFI PART", 8);
    put32(h + 8, 0x00010000u);
    put32(h + 12, 92);
    put64(h + 24, lba);
    put64(h + 32, other);
    put64(h + 40, first_usable);
    put64(h + 48, last_usable);
    for (int i = 0; i < 16; i++)
    {
        h[56 + i] = (uint8_t)(0xD0 + i);                    /* disk GUID */
    }
    put64(h + 72, entries_lba);
    put32(h + 80, GPT_ENTRIES);
    put32(h + 84, GPT_ENTRY_SIZE);
    put32(h + 88, entries_crc);
    put32(h + 16, crc32(h, 92));
}

static void build_gpt(void)
{
    uint64_t last = g_disk.block_count - 1u;
    uint64_t array_blocks = GPT_ENTRIES * GPT_ENTRY_SIZE / g_disk.block_size;
    uint64_t first_usable = 2u + array_blocks;
    uint64_t last_usable = last - array_blocks - 1u;
    uint64_t blocks = g_disk.block_count - 1u;
    mbr_entry(0, 0, 0x00, 0xEE, 1, (blocks > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)blocks);
    uint32_t crc = gpt_entries(2, first_usable, last_usable);
    gpt_entries(last - array_blocks, first_usable, last_usable);
    gpt_header(1, last, 2, crc, first_usable, last_usable);
    gpt_header(last, 1, last - array_blocks, crc, first_usable, last_usable);
}

/* ---- MBR ---- */

DMOD_TEST_STEP(mbr_primaries_numbered_by_slot)
{
    disk_init(512, 100000);
    put32(block(0) + 440, 0x1A2B3C4Du);
    mbr_entry(0, 0, 0x80, 0x0C, 2048, 1000);
    mbr_entry(0, 2, 0x00, 0x83, 4096, 5000);
    DMOD_TEST_EXPECT_EQ(scan(), 0);
    DMOD_TEST_EXPECT_EQ(g_info.table, dmpart_table_mbr);
    DMOD_TEST_EXPECT_EQ(g_info.count, 2u);
    DMOD_TEST_EXPECT_EQ(g_found.count, 2u);
    DMOD_TEST_EXPECT_EQ(g_found.number[0], 1u);
    DMOD_TEST_EXPECT_EQ(g_found.first[0], 2048u);
    DMOD_TEST_EXPECT_EQ(g_found.blocks[0], 1000u);
    DMOD_TEST_EXPECT_EQ(g_found.number[1], 3u);
    DMOD_TEST_EXPECT_EQ(g_found.first[1], 4096u);
    uint8_t id[DMPART_DISK_ID_SIZE] = { 0x4D, 0x3C, 0x2B, 0x1A };
    DMOD_TEST_EXPECT_TRUE(bytes_equal(g_info.disk_id, id, sizeof(id)));
}

DMOD_TEST_STEP(mbr_logical_partitions_from_5)
{
    disk_init(512, 100000);
    mbr_entry(0, 0, 0x00, 0x0C, 2048, 1000);
    mbr_entry(0, 1, 0x00, 0x0F, 10000, 50000);              /* extended */
    mbr_entry(10000, 0, 0x00, 0x83, 100, 900);              /* logical 5 */
    mbr_entry(10000, 1, 0x00, 0x05, 20000, 10000);          /* next EBR at 30000 */
    mbr_entry(30000, 0, 0x00, 0x83, 100, 5000);             /* logical 6 */
    DMOD_TEST_EXPECT_EQ(scan(), 0);
    DMOD_TEST_EXPECT_EQ(g_info.table, dmpart_table_mbr);
    DMOD_TEST_EXPECT_EQ(g_found.count, 3u);
    DMOD_TEST_EXPECT_EQ(g_found.number[1], 5u);
    DMOD_TEST_EXPECT_EQ(g_found.first[1], 10100u);
    DMOD_TEST_EXPECT_EQ(g_found.number[2], 6u);
    DMOD_TEST_EXPECT_EQ(g_found.first[2], 30100u);
    DMOD_TEST_EXPECT_EQ(g_found.blocks[2], 5000u);
}

DMOD_TEST_STEP(mbr_ebr_loop_and_escapes_are_ignored)
{
    disk_init(512, 100000);
    mbr_entry(0, 0, 0x00, 0x05, 10000, 20000);              /* extended 10000-29999 */
    mbr_entry(10000, 0, 0x00, 0x83, 100, 900);              /* logical 5 */
    mbr_entry(10000, 1, 0x00, 0x05, 5000, 1000);            /* next EBR at 15000 */
    mbr_entry(15000, 0, 0x00, 0x83, 100, 90000);            /* runs past the extended partition */
    mbr_entry(15000, 1, 0x00, 0x05, 0, 1000);               /* points back to the first EBR */
    DMOD_TEST_EXPECT_EQ(scan(), 0);
    DMOD_TEST_EXPECT_EQ(g_info.table, dmpart_table_mbr);
    DMOD_TEST_EXPECT_EQ(g_found.count, 1u);
    DMOD_TEST_EXPECT_EQ(g_found.number[0], 5u);
}

DMOD_TEST_STEP(mbr_invalid_tables_are_none)
{
    disk_init(512, 100000);
    mbr_entry(0, 0, 0x7F, 0x0C, 2048, 1000);                /* bad status byte */
    DMOD_TEST_EXPECT_EQ(scan(), 0);
    DMOD_TEST_EXPECT_EQ(g_info.table, dmpart_table_none);

    mbr_entry(0, 0, 0x80, 0x0C, 2048, 200000);              /* beyond the medium */
    DMOD_TEST_EXPECT_EQ(scan(), 0);
    DMOD_TEST_EXPECT_EQ(g_info.table, dmpart_table_none);

    block(0)[511] = 0;                                      /* no signature */
    DMOD_TEST_EXPECT_EQ(scan(), 0);
    DMOD_TEST_EXPECT_EQ(g_info.table, dmpart_table_none);
    DMOD_TEST_EXPECT_EQ(g_found.count, 0u);
}

DMOD_TEST_STEP(boot_sectors_are_not_mbrs)
{
    disk_init(512, 100000);
    uint8_t* s = block(0);
    mbr_entry(0, 0, 0x80, 0x06, 63, 2000);                  /* boot code that looks like an entry */
    s[0] = 0xEB; s[1] = 0x3C; s[2] = 0x90;
    memcpy(s + 54, "FAT16   ", 8);
    DMOD_TEST_EXPECT_EQ(scan(), 0);
    DMOD_TEST_EXPECT_EQ(g_info.table, dmpart_table_none);

    memset(s + 54, 0, 8);
    memcpy(s + 82, "FAT32   ", 8);
    DMOD_TEST_EXPECT_EQ(scan(), 0);
    DMOD_TEST_EXPECT_EQ(g_info.table, dmpart_table_none);

    memset(s + 82, 0, 8);
    s[1] = 0x76;
    memcpy(s + 3, "EXFAT   ", 8);
    DMOD_TEST_EXPECT_EQ(scan(), 0);
    DMOD_TEST_EXPECT_EQ(g_info.table, dmpart_table_none);
    DMOD_TEST_EXPECT_EQ(g_found.count, 0u);
}

DMOD_TEST_STEP(blank_medium_is_none)
{
    disk_init(512, 100000);
    DMOD_TEST_EXPECT_EQ(scan(), 0);
    DMOD_TEST_EXPECT_EQ(g_info.table, dmpart_table_none);
    DMOD_TEST_EXPECT_EQ(g_info.count, 0u);
    uint8_t zero[DMPART_DISK_ID_SIZE] = { 0 };
    DMOD_TEST_EXPECT_TRUE(bytes_equal(g_info.disk_id, zero, sizeof(zero)));
}

/* ---- GPT ---- */

static void expect_gpt(void)
{
    DMOD_TEST_EXPECT_EQ(g_info.table, dmpart_table_gpt);
    DMOD_TEST_EXPECT_EQ(g_found.count, 2u);
    DMOD_TEST_EXPECT_EQ(g_found.number[0], 1u);
    DMOD_TEST_EXPECT_EQ(g_found.blocks[0], 100u);
    DMOD_TEST_EXPECT_EQ(g_found.number[1], 6u);
    DMOD_TEST_EXPECT_EQ(g_info.disk_id[0], 0xD0);
    DMOD_TEST_EXPECT_EQ(g_info.disk_id[15], 0xDF);
}

DMOD_TEST_STEP(gpt_entries_numbered_by_index)
{
    disk_init(512, 200000);
    build_gpt();
    DMOD_TEST_EXPECT_EQ(scan(), 0);
    expect_gpt();
    DMOD_TEST_EXPECT_EQ(g_found.first[0], 34u);
}

DMOD_TEST_STEP(gpt_4k_blocks)
{
    disk_init(4096, 50000);
    build_gpt();
    DMOD_TEST_EXPECT_EQ(scan(), 0);
    expect_gpt();
    DMOD_TEST_EXPECT_EQ(g_found.first[0], 6u);
}

DMOD_TEST_STEP(gpt_backup_above_4gib_replaces_damaged_primary)
{
    disk_init(512, 6ull * 1024u * 1024u * 2u);             /* 6 GiB */
    build_gpt();
    block(1)[60] ^= 0xFFu;                                  /* primary header CRC mismatch */
    DMOD_TEST_EXPECT_EQ(scan(), 0);
    expect_gpt();
}

DMOD_TEST_STEP(gpt_backup_replaces_damaged_primary_entries)
{
    disk_init(512, 200000);
    build_gpt();
    block(2)[0] ^= 0xFFu;                                   /* primary entry array CRC mismatch */
    DMOD_TEST_EXPECT_EQ(scan(), 0);
    expect_gpt();
}

DMOD_TEST_STEP(gpt_with_both_headers_damaged_is_none)
{
    disk_init(512, 200000);
    build_gpt();
    block(1)[60] ^= 0xFFu;
    block(g_disk.block_count - 1u)[60] ^= 0xFFu;
    DMOD_TEST_EXPECT_EQ(scan(), 0);
    DMOD_TEST_EXPECT_EQ(g_info.table, dmpart_table_none);
    DMOD_TEST_EXPECT_EQ(g_found.count, 0u);
}

/* ---- API contract ---- */

DMOD_TEST_STEP(found_can_stop_the_scan)
{
    disk_init(512, 200000);
    build_gpt();
    g_found.stop_after = 1;
    DMOD_TEST_EXPECT_EQ(scan(), 0);
    DMOD_TEST_EXPECT_EQ(g_info.table, dmpart_table_gpt);
    DMOD_TEST_EXPECT_EQ(g_found.count, 1u);
    DMOD_TEST_EXPECT_EQ(g_info.count, 1u);
}

DMOD_TEST_STEP(found_may_be_null)
{
    disk_init(512, 100000);
    mbr_entry(0, 0, 0x00, 0x0C, 2048, 1000);
    dmpart_medium_t medium = { disk_read, &g_disk, 512, 100000 };
    DMOD_TEST_EXPECT_EQ(dmpart_scan(&medium, NULL, NULL, &g_info), 0);
    DMOD_TEST_EXPECT_EQ(g_info.table, dmpart_table_mbr);
    DMOD_TEST_EXPECT_EQ(g_info.count, 1u);
}

DMOD_TEST_STEP(unreadable_medium_is_none)
{
    disk_init(512, 100000);
    mbr_entry(0, 0, 0x00, 0x0C, 2048, 1000);
    g_disk.fail_reads = true;
    DMOD_TEST_EXPECT_EQ(scan(), 0);
    DMOD_TEST_EXPECT_EQ(g_info.table, dmpart_table_none);
}

DMOD_TEST_STEP(bad_arguments)
{
    dmpart_medium_t medium = { disk_read, &g_disk, 512, 100000 };
    DMOD_TEST_EXPECT_EQ(dmpart_scan(&medium, NULL, NULL, NULL), -EINVAL);
    DMOD_TEST_EXPECT_EQ(dmpart_scan(NULL, NULL, NULL, &g_info), -EINVAL);
    DMOD_TEST_EXPECT_EQ(g_info.table, dmpart_table_none);
    medium.block_size = 768;
    DMOD_TEST_EXPECT_EQ(dmpart_scan(&medium, NULL, NULL, &g_info), -EINVAL);
    medium.block_size = 8192;
    DMOD_TEST_EXPECT_EQ(dmpart_scan(&medium, NULL, NULL, &g_info), -EINVAL);
    medium.block_size = 512;
    medium.block_count = 1;
    DMOD_TEST_EXPECT_EQ(dmpart_scan(&medium, NULL, NULL, &g_info), -EINVAL);
    medium.block_count = 100000;
    medium.read = NULL;
    DMOD_TEST_EXPECT_EQ(dmpart_scan(&medium, NULL, NULL, &g_info), -EINVAL);
}
