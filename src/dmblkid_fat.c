#include "dmblkid_internal.h"

/*
 * FAT12/16/32 (Microsoft FAT specification). A boot sector is taken for FAT
 * only if the whole BPB is consistent - not just the 0x55AA marker:
 *  - x86 jump, 0x55AA at 510,
 *  - bytes/sector 512..4096 and sectors/cluster 1..128 (powers of 2),
 *  - >= 1 reserved sector, 1..4 FATs, a valid media byte,
 *  - FATs, root directory and data region inside the volume, >= 1 cluster,
 *  - FATs large enough for every cluster, volume inside the node.
 * FAT32 is the BPB with a zero 16-bit FAT size; otherwise the cluster count
 * decides between FAT12 (< 4085) and FAT16 (< 65525).
 *
 * The label is the volume label entry of the root directory (what the OS
 * shows), else the one in the extended BPB; "NO NAME" means none.
 */

#define FAT12_MAX_CLUSTERS      4084u
#define FAT16_MAX_CLUSTERS      65524u
#define FAT_DIR_ENTRY_SIZE      32u
#define FAT_MAX_ROOT_ENTRIES    1024u       /* bounds the label search */
#define FAT_ATTR_VOLUME_ID      0x08u
#define FAT_ATTR_DIRECTORY      0x10u
#define FAT_ATTR_LONG_NAME      0x0Fu
#define FAT_EXT_SIGNATURE       0x29u
#define FAT_EXT_SIGNATURE_OLD   0x28u       /* serial only, no label */
#define FAT_LABEL_SIZE          11u

typedef struct
{
    uint32_t    bytes_per_sector;
    uint32_t    sectors_per_cluster;
    uint32_t    reserved;
    uint32_t    fat_count;
    uint32_t    root_entries;
    uint32_t    total_sectors;
    uint32_t    fat_size;
    uint32_t    root_cluster;       /* FAT32 only */
    uint32_t    root_dir_sectors;   /* FAT12/16 only */
    uint32_t    first_data_sector;
    uint32_t    clusters;
    unsigned    bits;               /* 12, 16 or 32 */
} fat_geometry_t;

static bool fat_boot_code_ok(const uint8_t* s)
{
    bool jump  = (s[0] == 0xEBu && s[2] == 0x90u) || s[0] == 0xE9u;
    bool media = s[21] == 0xF0u || s[21] >= 0xF8u;
    return jump && media && dmblkid_has_boot_signature(s);
}

/* The raw BPB fields; false if one of them is out of range on its own. */
static bool fat_read_bpb(const uint8_t* s, fat_geometry_t* g)
{
    uint16_t total16 = dmblkid_le16(s + 19);
    uint16_t fat16   = dmblkid_le16(s + 22);
    g->bytes_per_sector    = dmblkid_le16(s + 11);
    g->sectors_per_cluster = s[13];
    g->reserved            = dmblkid_le16(s + 14);
    g->fat_count           = s[16];
    g->root_entries        = dmblkid_le16(s + 17);
    g->total_sectors       = (total16 != 0) ? total16 : dmblkid_le32(s + 32);
    g->fat_size            = (fat16 != 0) ? fat16 : dmblkid_le32(s + 36);
    g->root_cluster        = (fat16 != 0) ? 0 : dmblkid_le32(s + 44);
    g->bits                = (fat16 != 0) ? 16u : 32u;     /* 12 vs 16 decided by the cluster count */

    bool fat32_ok = fat16 != 0 || (g->root_entries == 0 && total16 == 0 && g->root_cluster >= 2u);
    return g->bytes_per_sector >= DMBLKID_SECTOR_SIZE && g->bytes_per_sector <= DMBLKID_MAX_BLOCK_SIZE &&
           dmblkid_is_power_of_2(g->bytes_per_sector) && dmblkid_is_power_of_2(g->sectors_per_cluster) &&
           g->reserved != 0 && g->fat_count != 0 && g->fat_count <= 4u && g->total_sectors != 0 &&
           g->fat_size != 0 && fat32_ok && (fat16 == 0 || g->root_entries != 0);
}

/* Regions and cluster count; false if they do not add up. */
static bool fat_layout_ok(fat_geometry_t* g, uint64_t node_size)
{
    uint64_t root_bytes = (uint64_t)g->root_entries * FAT_DIR_ENTRY_SIZE;
    g->root_dir_sectors = (uint32_t)((root_bytes + g->bytes_per_sector - 1u) / g->bytes_per_sector);
    uint64_t first_data = (uint64_t)g->reserved + (uint64_t)g->fat_count * g->fat_size + g->root_dir_sectors;
    if (first_data >= g->total_sectors)
    {
        return false;
    }
    g->first_data_sector = (uint32_t)first_data;
    g->clusters = (g->total_sectors - g->first_data_sector) / g->sectors_per_cluster;
    if (g->bits == 16u)
    {
        g->bits = (g->clusters <= FAT12_MAX_CLUSTERS) ? 12u : 16u;
    }
    bool clusters_ok = g->clusters != 0 && (g->bits == 32u || g->clusters <= FAT16_MAX_CLUSTERS) &&
                       (g->bits != 32u || g->root_cluster < (uint64_t)g->clusters + 2u);
    bool fat_ok  = (uint64_t)g->fat_size * g->bytes_per_sector * 8u >= ((uint64_t)g->clusters + 2u) * g->bits;
    bool fits_ok = node_size == 0 || (uint64_t)g->total_sectors * g->bytes_per_sector <= node_size;
    return clusters_ok && fat_ok && fits_ok;
}

/* Where the label search starts and how many entries it may look at. */
static void fat_root_dir(const fat_geometry_t* g, uint64_t* offset, uint32_t* entries)
{
    uint64_t sector;
    if (g->bits == 32u)
    {
        sector   = g->first_data_sector + (uint64_t)(g->root_cluster - 2u) * g->sectors_per_cluster;
        *entries = g->sectors_per_cluster * g->bytes_per_sector / FAT_DIR_ENTRY_SIZE;   /* first cluster */
    }
    else
    {
        sector   = (uint64_t)g->reserved + (uint64_t)g->fat_count * g->fat_size;
        *entries = g->root_entries;
    }
    *offset = sector * g->bytes_per_sector;
    if (*entries > FAT_MAX_ROOT_ENTRIES)
    {
        *entries = FAT_MAX_ROOT_ENTRIES;
    }
}

/* 1 and the label in *label if a volume label entry exists, 0 if not, < 0 on error. */
static int fat_find_root_label(const dmblkid_source_t* source, const fat_geometry_t* g, char** label)
{
    uint8_t entry[FAT_DIR_ENTRY_SIZE];
    uint64_t offset;
    uint32_t entries;
    fat_root_dir(g, &offset, &entries);
    for (uint32_t i = 0; i < entries; i++, offset += FAT_DIR_ENTRY_SIZE)
    {
        int ret = dmblkid_source_read(source, offset, entry, sizeof(entry));
        if (ret != 0 || entry[0] == 0x00u)
        {
            return dmblkid_not_found_if_outside(ret);
        }
        uint8_t attr = entry[11];
        bool volume  = (attr & (FAT_ATTR_VOLUME_ID | FAT_ATTR_DIRECTORY)) == FAT_ATTR_VOLUME_ID;
        if (entry[0] == 0xE5u || attr == FAT_ATTR_LONG_NAME || !volume)
        {
            continue;
        }
        if (entry[0] == 0x05u)
        {
            entry[0] = 0xE5u;       /* KANJI lead byte escape */
        }
        ret = dmblkid_set_label8(label, entry, FAT_LABEL_SIZE);
        return (ret < 0) ? ret : 1;
    }
    return 0;
}

/* Serial and (fallback) label from the extended BPB. */
static int fat_read_ext_bpb(const uint8_t* s, const fat_geometry_t* g, dmblkid_t* result, bool want_label)
{
    const uint8_t* ext = s + ((g->bits == 32u) ? 64u : 36u);   /* drive number */
    if (ext[2] != FAT_EXT_SIGNATURE && ext[2] != FAT_EXT_SIGNATURE_OLD)
    {
        return 0;
    }
    int ret = dmblkid_set_serial(&result->uuid, dmblkid_le32(ext + 3));
    if (ret == 0 && want_label && ext[2] == FAT_EXT_SIGNATURE &&
        !dmblkid_bytes_equal(ext + 7, "NO NAME    ", FAT_LABEL_SIZE))
    {
        ret = dmblkid_set_label8(&result->label, ext + 7, FAT_LABEL_SIZE);
    }
    return ret;
}

static int fat_describe(const dmblkid_source_t* source, const uint8_t* s, const fat_geometry_t* g,
                        dmblkid_t* result)
{
    /* No DMOD filesystem module mounts FAT yet. */
    dmblkid_set_filesystem(result, "vfat", NULL, false);
    const char* version = (g->bits == 12u) ? "FAT12" : (g->bits == 16u) ? "FAT16" : "FAT32";
    int ret = dmblkid_set_string(&result->version, version);
    int found = (ret < 0) ? ret : fat_find_root_label(source, g, &result->label);
    if (found < 0)
    {
        return found;
    }
    ret = fat_read_ext_bpb(s, g, result, found == 0);
    return (ret < 0) ? ret : 1;
}

int dmblkid_probe_fat(const dmblkid_source_t* source, dmblkid_t* result)
{
    uint8_t* sector = Dmod_Malloc(DMBLKID_SECTOR_SIZE);
    if (sector == NULL)
    {
        return -ENOMEM;
    }
    fat_geometry_t geometry;
    int ret = dmblkid_source_read(source, 0, sector, DMBLKID_SECTOR_SIZE);
    if (ret == 0 && fat_boot_code_ok(sector) && fat_read_bpb(sector, &geometry) &&
        fat_layout_ok(&geometry, source->size))
    {
        ret = fat_describe(source, sector, &geometry, result);
    }
    Dmod_Free(sector);
    return dmblkid_not_found_if_outside(ret);
}
