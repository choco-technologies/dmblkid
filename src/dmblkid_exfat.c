#include "dmblkid_internal.h"

/*
 * exFAT (Microsoft exFAT specification). Recognized only if:
 *  - the boot sector has the "EXFAT   " name, the zeroed legacy BPB area
 *    and 0x55AA,
 *  - sector/cluster shifts, FAT count and every region (FAT, cluster heap,
 *    root directory) are consistent with the volume length, and the volume
 *    fits in the node,
 *  - the boot region checksum (sector 11) matches sectors 0-10, so a
 *    damaged boot region is not reported as a filesystem.
 * The label is the volume label entry in the first root directory cluster.
 */

#define EXFAT_MIN_VOLUME_BYTES      (1024u * 1024u)
#define EXFAT_CHECKSUM_SECTOR       11u
#define EXFAT_DIR_ENTRY_SIZE        32u
#define EXFAT_MAX_ROOT_ENTRIES      1024u       /* bounds the label search */
#define EXFAT_ENTRY_END             0x00u
#define EXFAT_ENTRY_LABEL           0x83u
#define EXFAT_LABEL_MAX_CHARS       11u

typedef struct
{
    uint64_t    volume_length;      /* sectors */
    uint32_t    fat_offset;
    uint32_t    fat_length;
    uint32_t    heap_offset;
    uint32_t    cluster_count;
    uint32_t    root_cluster;
    uint32_t    serial;
    uint8_t     revision_major;
    uint8_t     revision_minor;
    uint8_t     sector_shift;
    uint8_t     cluster_shift;      /* sectors per cluster, log2 */
    uint8_t     fat_count;
} exfat_boot_t;

static bool exfat_header_ok(const uint8_t* s)
{
    for (uint32_t i = 11; i < 64; i++)
    {
        if (s[i] != 0)
        {
            return false;       /* MustBeZero: where FAT keeps its BPB */
        }
    }
    return s[0] == 0xEBu && s[1] == 0x76u && s[2] == 0x90u && dmblkid_bytes_equal(s + 3, "EXFAT   ", 8) &&
           dmblkid_has_boot_signature(s);
}

static void exfat_read_boot(const uint8_t* s, exfat_boot_t* b)
{
    b->volume_length  = dmblkid_le64(s + 72);
    b->fat_offset     = dmblkid_le32(s + 80);
    b->fat_length     = dmblkid_le32(s + 84);
    b->heap_offset    = dmblkid_le32(s + 88);
    b->cluster_count  = dmblkid_le32(s + 92);
    b->root_cluster   = dmblkid_le32(s + 96);
    b->serial         = dmblkid_le32(s + 100);
    b->revision_minor = s[104];
    b->revision_major = s[105];
    b->sector_shift   = s[108];
    b->cluster_shift  = s[109];
    b->fat_count      = s[110];
}

static bool exfat_layout_ok(const exfat_boot_t* b, uint64_t node_size)
{
    if (b->sector_shift < 9u || b->sector_shift > 12u || b->cluster_shift > 25u - b->sector_shift ||
        b->fat_count < 1u || b->fat_count > 2u)
    {
        return false;
    }
    uint64_t fat_end    = (uint64_t)b->fat_offset + (uint64_t)b->fat_length * b->fat_count;
    uint64_t min_length = EXFAT_MIN_VOLUME_BYTES >> b->sector_shift;
    bool regions_ok = b->volume_length >= min_length && b->fat_offset >= 24u && b->fat_length != 0 &&
                      fat_end <= b->heap_offset && b->heap_offset < b->volume_length &&
                      b->cluster_count != 0 &&
                      b->cluster_count <= ((b->volume_length - b->heap_offset) >> b->cluster_shift) &&
                      b->root_cluster >= 2u && b->root_cluster <= (uint64_t)b->cluster_count + 1u;
    bool fits_ok = node_size == 0 || b->volume_length <= (node_size >> b->sector_shift);
    return regions_ok && fits_ok;
}

static uint32_t exfat_checksum(uint32_t checksum, const uint8_t* data, uint32_t size, bool boot_sector)
{
    for (uint32_t i = 0; i < size; i++)
    {
        if (boot_sector && (i == 106u || i == 107u || i == 112u))
        {
            continue;       /* VolumeFlags and PercentInUse change at run time */
        }
        checksum = ((checksum & 1u) ? 0x80000000u : 0u) + (checksum >> 1) + data[i];
    }
    return checksum;
}

/* 1 if sector 11 repeats the checksum of sectors 0-10, 0 if not, < 0 on error. */
static int exfat_boot_checksum_ok(const dmblkid_source_t* source, const exfat_boot_t* b)
{
    uint32_t size = 1u << b->sector_shift;
    uint8_t* sector = Dmod_Malloc(size);
    if (sector == NULL)
    {
        return -ENOMEM;
    }
    uint32_t checksum = 0;
    int ret = 0;
    for (uint32_t i = 0; i < EXFAT_CHECKSUM_SECTOR && ret == 0; i++)
    {
        ret = dmblkid_source_read(source, (uint64_t)i * size, sector, size);
        checksum = (ret == 0) ? exfat_checksum(checksum, sector, size, i == 0) : checksum;
    }
    ret = (ret == 0) ? dmblkid_source_read(source, (uint64_t)EXFAT_CHECKSUM_SECTOR * size, sector, size) : ret;
    bool match = ret == 0;
    for (uint32_t i = 0; match && i < size; i += 4u)
    {
        match = dmblkid_le32(sector + i) == checksum;
    }
    Dmod_Free(sector);
    return (ret < 0) ? dmblkid_not_found_if_outside(ret) : (match ? 1 : 0);
}

/* The volume label entry of the first root directory cluster, if any. */
static int exfat_find_label(const dmblkid_source_t* source, const exfat_boot_t* b, char** label)
{
    uint64_t sector  = b->heap_offset + ((uint64_t)(b->root_cluster - 2u) << b->cluster_shift);
    uint64_t offset  = sector << b->sector_shift;
    uint64_t entries = ((uint64_t)1u << (b->cluster_shift + b->sector_shift)) / EXFAT_DIR_ENTRY_SIZE;
    uint8_t entry[EXFAT_DIR_ENTRY_SIZE];
    for (uint64_t i = 0; i < entries && i < EXFAT_MAX_ROOT_ENTRIES; i++, offset += EXFAT_DIR_ENTRY_SIZE)
    {
        int ret = dmblkid_source_read(source, offset, entry, sizeof(entry));
        if (ret != 0 || entry[0] == EXFAT_ENTRY_END)
        {
            return dmblkid_not_found_if_outside(ret);
        }
        if (entry[0] == EXFAT_ENTRY_LABEL)
        {
            uint8_t chars = (entry[1] <= EXFAT_LABEL_MAX_CHARS) ? entry[1] : EXFAT_LABEL_MAX_CHARS;
            return dmblkid_set_label16(label, entry + 2, chars);
        }
    }
    return 0;
}

/* "<major>.<minor>" of the FileSystemRevision field. */
static int exfat_set_version(dmblkid_t* result, const exfat_boot_t* b)
{
    char text[8];
    size_t length = 0;
    uint8_t parts[2] = { b->revision_major, b->revision_minor };
    for (int p = 0; p < 2; p++)
    {
        uint8_t value = parts[p];
        if (value >= 100u)
        {
            text[length++] = (char)('0' + value / 100u);
        }
        if (value >= 10u)
        {
            text[length++] = (char)('0' + (value / 10u) % 10u);
        }
        text[length++] = (char)('0' + value % 10u);
        text[length++] = (p == 0) ? '.' : '\0';
    }
    return dmblkid_set_string(&result->version, text);
}

static int exfat_describe(const dmblkid_source_t* source, const exfat_boot_t* b, dmblkid_t* result)
{
    /* No DMOD filesystem module mounts exFAT yet. */
    dmblkid_set_filesystem(result, "exfat", NULL, false);
    int ret = exfat_set_version(result, b);
    ret = (ret < 0) ? ret : dmblkid_set_serial(&result->uuid, b->serial);
    ret = (ret < 0) ? ret : exfat_find_label(source, b, &result->label);
    return (ret < 0) ? ret : 1;
}

int dmblkid_probe_exfat(const dmblkid_source_t* source, dmblkid_t* result)
{
    uint8_t* sector = Dmod_Malloc(DMBLKID_SECTOR_SIZE);
    if (sector == NULL)
    {
        return -ENOMEM;
    }
    exfat_boot_t boot = { 0 };
    bool candidate = false;
    int ret = dmblkid_source_read(source, 0, sector, DMBLKID_SECTOR_SIZE);
    if (ret == 0 && exfat_header_ok(sector))
    {
        exfat_read_boot(sector, &boot);
        candidate = exfat_layout_ok(&boot, source->size);
    }
    Dmod_Free(sector);
    ret = candidate ? exfat_boot_checksum_ok(source, &boot) : ret;
    ret = (ret == 1) ? exfat_describe(source, &boot, result) : ret;
    return dmblkid_not_found_if_outside(ret);
}
