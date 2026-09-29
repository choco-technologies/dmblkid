#include "dmod.h"
#include "test_images.h"
#include <string.h>

#define SECTOR  512u

/* ---- helpers ---- */

static void put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t* p, uint32_t v) { put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }
static void put64(uint8_t* p, uint64_t v) { put32(p, (uint32_t)v); put32(p + 4, (uint32_t)(v >> 32)); }

static uint8_t* zeroed(size_t size)
{
    uint8_t* buffer = Dmod_Malloc(size);
    if (buffer != NULL)
    {
        memset(buffer, 0, size);
    }
    return buffer;
}

static bool write_and_free(const char* path, uint64_t offset, uint8_t* data, size_t size)
{
    bool ok = data != NULL && test_image_write(path, offset, data, size);
    Dmod_Free(data);
    return ok;
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

/* ---- raw file access ---- */

static bool seek_and(void* file, uint64_t offset)
{
    return file != NULL && Dmod_FileSeek(file, (Dmod_FileOffset_t)offset, DMOD_SEEK_SET) == 0;
}

bool test_image_create(const char* path, uint64_t size)
{
    uint8_t zero = 0;
    void* file = Dmod_FileOpen(path, "wb");
    bool ok = size == 0 || (seek_and(file, size - 1u) && Dmod_FileWrite(&zero, 1, 1, file) == 1);
    if (file != NULL)
    {
        Dmod_FileClose(file);
    }
    return file != NULL && ok;
}

bool test_image_write(const char* path, uint64_t offset, const void* data, size_t size)
{
    void* file = Dmod_FileOpen(path, "r+b");
    bool ok = seek_and(file, offset) && Dmod_FileWrite(data, 1, size, file) == size;
    if (file != NULL)
    {
        Dmod_FileClose(file);
    }
    return ok;
}

bool test_image_read(const char* path, uint64_t offset, void* data, size_t size)
{
    void* file = Dmod_FileOpen(path, "rb");
    bool ok = seek_and(file, offset) && Dmod_FileRead(data, 1, size, file) == size;
    if (file != NULL)
    {
        Dmod_FileClose(file);
    }
    return ok;
}

uint32_t test_image_checksum(const char* path, uint64_t size)
{
    uint8_t block[SECTOR];
    uint32_t sum = 0;
    for (uint64_t offset = 0; offset < size; offset += SECTOR)
    {
        if (!test_image_read(path, offset, block, SECTOR))
        {
            return 0;
        }
        sum = crc32(block, SECTOR) ^ ((sum << 1) | (sum >> 31));
    }
    return sum;
}

/* ---- FAT ---- */

typedef struct
{
    uint16_t    reserved;
    uint8_t     sectors_per_cluster;
    uint16_t    root_entries;
    uint32_t    total_sectors;
    uint32_t    fat_size;
    uint8_t     media;
    bool        fat32;
    uint32_t    serial;
    const char* label;          /* 11 characters */
    const char* fs_name;        /* 8 characters */
} fat_params_t;

static uint8_t* fat_boot_sector(const fat_params_t* p)
{
    uint8_t* s = zeroed(SECTOR);
    if (s == NULL)
    {
        return NULL;
    }
    s[0] = 0xEB; s[1] = 0x3C; s[2] = 0x90;
    memcpy(s + 3, "MSWIN4.1", 8);
    put16(s + 11, SECTOR);
    s[13] = p->sectors_per_cluster;
    put16(s + 14, p->reserved);
    s[16] = 2;
    put16(s + 17, p->root_entries);
    put16(s + 19, (p->total_sectors < 0x10000u && !p->fat32) ? (uint16_t)p->total_sectors : 0);
    s[21] = p->media;
    put16(s + 22, p->fat32 ? 0 : (uint16_t)p->fat_size);
    put32(s + 32, (p->total_sectors < 0x10000u && !p->fat32) ? 0 : p->total_sectors);
    uint8_t* ext = s + (p->fat32 ? 64 : 36);
    if (p->fat32)
    {
        put32(s + 36, p->fat_size);
        put32(s + 44, 2);       /* root cluster */
    }
    ext[0] = 0x80;
    ext[2] = 0x29;
    put32(ext + 3, p->serial);
    memcpy(ext + 7, p->label, 11);
    memcpy(ext + 18, p->fs_name, 8);
    s[510] = 0x55; s[511] = 0xAA;
    return s;
}

static bool write_dir_entry(const char* path, uint64_t offset, uint8_t first, const char* name, uint8_t attr)
{
    uint8_t entry[32] = { 0 };
    memcpy(entry, name, 11);
    entry[0]  = first;
    entry[11] = attr;
    return test_image_write(path, offset, entry, sizeof(entry));
}

bool test_build_fat12(const char* path)
{
    fat_params_t p = { 1, 1, 224, 2880, 9, 0xF0, false, 0x1234ABCDu, "NO NAME    ", "FAT12   " };
    uint8_t* s = fat_boot_sector(&p);
    if (s != NULL)
    {
        /* Boot code that looks like a bootable partition entry - still not an MBR. */
        s[446] = 0x80; s[450] = 0x06; put32(s + 454, 63); put32(s + 458, 2000);
    }
    uint64_t root = (1u + 2u * 9u) * SECTOR;
    return test_image_create(path, 2880u * SECTOR) && write_and_free(path, 0, s, SECTOR) &&
           write_dir_entry(path, root, 'F', "FLOPPY     ", 0x08);
}

bool test_build_fat16(const char* path)
{
    fat_params_t p = { 4, 4, 512, 65536, 64, 0xF8, false, 0x0BADF00Du, "DATA16     ", "FAT16   " };
    return test_image_create(path, 32u * TEST_MIB) && write_and_free(path, 0, fat_boot_sector(&p), SECTOR);
}

bool test_build_fat32(const char* path)
{
    fat_params_t p = { 32, 1, 0, 131072, 1024, 0xF8, true, 0xCAFEBABEu, "OLDLABEL   ", "FAT32   " };
    uint64_t root = (32u + 2u * 1024u) * SECTOR;     /* first data sector = cluster 2 */
    return test_image_create(path, 64u * TEST_MIB) && write_and_free(path, 0, fat_boot_sector(&p), SECTOR) &&
           write_dir_entry(path, root, 0xE5, "GONE       ", 0x08) &&
           write_dir_entry(path, root + 32u, 'A', "LONGNAMEPAR", 0x0F) &&
           write_dir_entry(path, root + 64u, 'B', "BOOT       ", 0x28);
}

/* ---- exFAT ---- */

#define EXFAT_VOLUME_SECTORS    (5ull * TEST_GIB / SECTOR)
#define EXFAT_HEAP_OFFSET       9437184u            /* 4.5 GiB */
#define EXFAT_CLUSTER_SHIFT     3u
#define EXFAT_ROOT_CLUSTER      5u

static void exfat_boot_sector(uint8_t* s)
{
    s[0] = 0xEB; s[1] = 0x76; s[2] = 0x90;
    memcpy(s + 3, "EXFAT   ", 8);
    put64(s + 72, EXFAT_VOLUME_SECTORS);
    put32(s + 80, 2048);                            /* FAT offset */
    put32(s + 84, 1025);                            /* FAT length */
    put32(s + 88, EXFAT_HEAP_OFFSET);
    put32(s + 92, 131072);                          /* cluster count */
    put32(s + 96, EXFAT_ROOT_CLUSTER);
    put32(s + 100, 0x5EED0001u);
    s[104] = 0x00; s[105] = 0x01;                   /* revision 1.00 */
    s[106] = 0x02;                                  /* VolumeFlags: excluded from the checksum */
    s[108] = 9;
    s[109] = EXFAT_CLUSTER_SHIFT;
    s[110] = 1;
    s[111] = 0x80;
    s[112] = 37;                                    /* PercentInUse: excluded from the checksum */
    s[510] = 0x55; s[511] = 0xAA;
}

/* Sectors 0-11 of the boot region, sector 11 holding the checksum. */
static uint8_t* exfat_boot_region(bool valid_checksum)
{
    uint8_t* region = zeroed(12u * SECTOR);
    if (region == NULL)
    {
        return NULL;
    }
    exfat_boot_sector(region);
    for (uint32_t i = 1; i <= 8; i++)
    {
        put32(region + i * SECTOR + 508, 0xAA550000u);  /* extended boot sectors */
    }
    uint32_t checksum = 0;
    for (uint32_t i = 0; i < 11u * SECTOR; i++)
    {
        if (i != 106 && i != 107 && i != 112)
        {
            checksum = ((checksum & 1u) ? 0x80000000u : 0u) + (checksum >> 1) + region[i];
        }
    }
    for (uint32_t i = 0; i < SECTOR; i += 4)
    {
        put32(region + 11u * SECTOR + i, valid_checksum ? checksum : checksum + 1u);
    }
    return region;
}

bool test_build_exfat(const char* path, bool valid_checksum)
{
    static const uint16_t label[] = { 'K', 'a', 'r', 't', 'a', ' ', 0x0142 };
    uint8_t entries[64] = { 0 };
    entries[0] = 0x81;                              /* allocation bitmap */
    entries[32] = 0x83;                             /* volume label */
    entries[33] = 7;
    for (int i = 0; i < 7; i++)
    {
        put16(entries + 34 + 2 * i, label[i]);
    }
    uint64_t root = ((uint64_t)EXFAT_HEAP_OFFSET + ((EXFAT_ROOT_CLUSTER - 2u) << EXFAT_CLUSTER_SHIFT)) * SECTOR;
    return test_image_create(path, 5u * TEST_GIB) &&
           write_and_free(path, 0, exfat_boot_region(valid_checksum), 12u * SECTOR) &&
           test_image_write(path, root, entries, sizeof(entries));
}

/* ---- dmffs ---- */

static size_t put_tlv(uint8_t* p, uint32_t type, const char* value, uint32_t length)
{
    put32(p, type);
    put32(p + 4, length);
    if (value != NULL)
    {
        memcpy(p + 8, value, length);
    }
    return 8u + (value != NULL ? length : 0u);
}

bool test_build_dmffs(const char* path)
{
    uint8_t image[96] = { 0 };
    size_t size = put_tlv(image, 3, "1.0", 3);
    size += put_tlv(image + size, 1, NULL, 23);     /* FILE: NAME + DATA below */
    size += put_tlv(image + size, 4, "a.txt", 5);
    size += put_tlv(image + size, 5, "hi", 2);
    size += put_tlv(image + size, 2, NULL, 9);      /* DIR: NAME below */
    size += put_tlv(image + size, 4, "d", 1);
    size += put_tlv(image + size, 0xFFFFFFFFu, NULL, 0);
    return test_image_create(path, size) && test_image_write(path, 0, image, size);
}

/* ---- MBR ---- */

static void mbr_entry(uint8_t* sector, int index, uint8_t status, uint8_t type, uint32_t first, uint32_t count)
{
    uint8_t* e = sector + 446 + 16 * index;
    e[0] = status;
    e[4] = type;
    put32(e + 8, first);
    put32(e + 12, count);
}

bool test_build_mbr(const char* path)
{
    uint8_t* mbr = zeroed(SECTOR);
    uint8_t* ebr = zeroed(SECTOR);
    if (mbr != NULL && ebr != NULL)
    {
        put32(mbr + 440, 0x1A2B3C4Du);
        mbr_entry(mbr, 0, 0x80, 0x0C, 2048, 65536);
        mbr_entry(mbr, 1, 0x00, 0x05, 67584, 32768);
        mbr[510] = 0x55; mbr[511] = 0xAA;
        mbr_entry(ebr, 0, 0x00, 0x0C, 2048, 8192);
        ebr[510] = 0x55; ebr[511] = 0xAA;
    }
    bool ok = test_image_create(path, 64u * TEST_MIB);
    ok = write_and_free(path, 0, mbr, SECTOR) && ok;
    return write_and_free(path, 67584ull * SECTOR, ebr, SECTOR) && ok;
}

/* ---- GPT ---- */

#define GPT_ENTRIES         128u
#define GPT_ENTRY_SIZE      128u
#define GPT_ARRAY_BYTES     (GPT_ENTRIES * GPT_ENTRY_SIZE)

typedef struct
{
    uint32_t    block_size;
    uint64_t    last_lba;
    uint64_t    entry_blocks;
    uint64_t    first_usable;
    uint64_t    last_usable;
} gpt_layout_t;

static void gpt_part(uint8_t* e, uint8_t id, uint64_t first, uint64_t last)
{
    memset(e, id, 16);          /* type GUID */
    memset(e + 16, id + 1, 16); /* unique GUID */
    put64(e + 32, first);
    put64(e + 40, last);
}

static uint8_t* gpt_header(const gpt_layout_t* l, uint64_t my_lba, uint64_t other_lba, uint64_t entries_lba,
                           uint32_t entries_crc)
{
    static const uint8_t guid[16] = { 0x67, 0x45, 0x23, 0x01, 0xAB, 0x89, 0xEF, 0xCD,
                                      0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77 };
    uint8_t* h = zeroed(l->block_size);
    if (h == NULL)
    {
        return NULL;
    }
    memcpy(h, "EFI PART", 8);
    put32(h + 8, 0x00010000u);
    put32(h + 12, 92);
    put64(h + 24, my_lba);
    put64(h + 32, other_lba);
    put64(h + 40, l->first_usable);
    put64(h + 48, l->last_usable);
    memcpy(h + 56, guid, 16);
    put64(h + 72, entries_lba);
    put32(h + 80, GPT_ENTRIES);
    put32(h + 84, GPT_ENTRY_SIZE);
    put32(h + 88, entries_crc);
    put32(h + 16, crc32(h, 92));
    return h;
}

static bool gpt_write_tables(const char* path, const gpt_layout_t* l, bool damage_primary)
{
    uint8_t* entries = zeroed(GPT_ARRAY_BYTES);
    if (entries == NULL)
    {
        return false;
    }
    gpt_part(entries, 0x11, l->first_usable, l->first_usable + 1000u);
    gpt_part(entries + GPT_ENTRY_SIZE, 0x22, l->first_usable + 2000u, l->last_usable);
    uint32_t crc = crc32(entries, GPT_ARRAY_BYTES);
    uint64_t backup_entries = l->last_lba - l->entry_blocks;
    uint8_t* primary = gpt_header(l, 1, l->last_lba, 2, crc);
    uint8_t* backup  = gpt_header(l, l->last_lba, 1, backup_entries, crc);
    if (primary != NULL && damage_primary)
    {
        primary[60] ^= 0xFFu;   /* disk GUID changed after the CRC was computed */
    }
    bool ok = test_image_write(path, 2ull * l->block_size, entries, GPT_ARRAY_BYTES) &&
              test_image_write(path, backup_entries * l->block_size, entries, GPT_ARRAY_BYTES);
    Dmod_Free(entries);
    ok = write_and_free(path, l->block_size, primary, l->block_size) && ok;
    return write_and_free(path, l->last_lba * l->block_size, backup, l->block_size) && ok;
}

bool test_build_gpt(const char* path, uint64_t size, uint32_t block_size, bool damage_primary)
{
    gpt_layout_t l;
    l.block_size   = block_size;
    l.last_lba     = size / block_size - 1u;
    l.entry_blocks = GPT_ARRAY_BYTES / block_size;
    l.first_usable = 2u + l.entry_blocks;
    l.last_usable  = l.last_lba - l.entry_blocks - 1u;

    uint8_t* mbr = zeroed(block_size);
    if (mbr != NULL)
    {
        uint64_t blocks = size / block_size - 1u;
        mbr_entry(mbr, 0, 0x00, 0xEE, 1, (blocks > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)blocks);
        mbr[510] = 0x55; mbr[511] = 0xAA;
    }
    return test_image_create(path, size) && write_and_free(path, 0, mbr, block_size) &&
           gpt_write_tables(path, &l, damage_primary);
}
