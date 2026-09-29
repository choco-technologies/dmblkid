#include "dmblkid_internal.h"
#include <errno.h>
#include <string.h>

/*
 * MBR:  sector 0, signature 0x55 0xAA at 510, four 16-byte entries at 446.
 *       Types 0x05/0x0F/0x85 are extended partitions: a chain of EBRs, each
 *       with the logical partition in entry 0 (relative to the EBR) and the
 *       next EBR in entry 1 (relative to the extended partition start).
 *       Type 0xEE (protective MBR) means the medium is GPT.
 * GPT:  header at LBA 1 (backup at the last LBA), entry array CRC32-checked.
 *
 * Every value read from the medium is validated against the medium size;
 * nothing found outside it, overlapping the table or looping is reported.
 *
 * This is the parser dmdevfs uses to create partition nodes (dmdevfs
 * src/partitions.c), so a medium dmdevfs splits into partitions is reported
 * here as a partition table - and never as a filesystem to mount whole.
 */

#define MBR_SIGNATURE_OFFSET        510u
#define MBR_DISK_SIGNATURE_OFFSET   440u
#define MBR_ENTRIES_OFFSET          446u
#define MBR_ENTRY_SIZE              16u
#define MBR_ENTRY_COUNT             4u
#define MBR_TYPE_GPT_PROTECTIVE     0xEEu
#define MBR_FIRST_LOGICAL_NUMBER    5u
#define MBR_MAX_LOGICAL             64u     /* bounds a looping EBR chain */

#define GPT_HEADER_MIN_SIZE         92u
#define GPT_DISK_GUID_OFFSET        56u
#define GPT_ENTRY_MIN_SIZE          128u
#define GPT_MAX_ENTRIES             1024u
#define GUID_SIZE                   16u

typedef struct
{
    dmblkid_part_read_t     read;
    void*                   read_ctx;
    uint32_t                block_size;
    uint64_t                block_count;
    dmblkid_part_found_t    found;
    void*                   found_ctx;
    uint8_t*                block;      /**< One block buffer */
    uint32_t                count;      /**< Partitions reported */
    uint8_t                 disk_id[GUID_SIZE]; /**< MBR disk signature (4 bytes) or GPT disk GUID */
} scan_t;

typedef struct
{
    uint8_t     status;
    uint8_t     type;
    uint32_t    first_lba;
    uint32_t    lba_count;
} mbr_entry_t;

/* ---- little helpers ---- */

static bool all_zero(const uint8_t* p, size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        if (p[i] != 0)
        {
            return false;
        }
    }
    return true;
}

/* CRC32 (IEEE 802.3, reflected) as used by GPT - bitwise, no table to keep flash small. */
static uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t size)
{
    crc = ~crc;
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

static bool read_block(scan_t* scan, uint64_t lba)
{
    return lba < scan->block_count &&
           scan->read(scan->read_ctx, lba * scan->block_size, scan->block, scan->block_size) == 0;
}

static bool in_medium(const scan_t* scan, uint64_t first_lba, uint64_t lba_count)
{
    return lba_count != 0 && first_lba != 0 && first_lba < scan->block_count &&
           lba_count <= scan->block_count - first_lba;
}

static bool report(scan_t* scan, uint32_t number, uint64_t first_lba, uint64_t lba_count)
{
    scan->count++;
    return scan->found == NULL || scan->found(scan->found_ctx, number, first_lba, lba_count);
}

/* ---- MBR ---- */

static mbr_entry_t mbr_entry(const uint8_t* sector, uint32_t index)
{
    const uint8_t* e = sector + MBR_ENTRIES_OFFSET + index * MBR_ENTRY_SIZE;
    mbr_entry_t entry = { e[0], e[4], dmblkid_le32(e + 8), dmblkid_le32(e + 12) };
    return entry;
}

static bool mbr_entry_used(const mbr_entry_t* entry)
{
    return entry->type != 0 && entry->lba_count != 0;
}

static bool mbr_is_extended(uint8_t type)
{
    return type == 0x05u || type == 0x0Fu || type == 0x85u;
}

/*
 * FAT and exFAT boot sectors carry the same 0x55AA signature - a
 * partitionless "superfloppy", whose boot code may look like entries.
 */
static bool looks_like_boot_sector(const uint8_t* sector)
{
    bool jump = (sector[0] == 0xEBu && sector[2] == 0x90u) || sector[0] == 0xE9u;
    return jump && (dmblkid_bytes_equal(sector + 54, "FAT", 3) || dmblkid_bytes_equal(sector + 82, "FAT32", 5) ||
                    dmblkid_bytes_equal(sector + 3, "EXFAT   ", 8));
}

/* Valid MBR: signature, sane status bytes, >= 1 used entry, all used entries inside the medium. */
static bool mbr_is_valid(const scan_t* scan, const uint8_t* sector, bool* protective)
{
    bool used = false;
    *protective = false;
    if (!dmblkid_has_boot_signature(sector) || looks_like_boot_sector(sector))
    {
        return false;
    }
    for (uint32_t i = 0; i < MBR_ENTRY_COUNT; i++)
    {
        mbr_entry_t entry = mbr_entry(sector, i);
        if (entry.status != 0x00u && entry.status != 0x80u)
        {
            return false;
        }
        if (!mbr_entry_used(&entry))
        {
            continue;
        }
        used = true;
        if (entry.type == MBR_TYPE_GPT_PROTECTIVE)
        {
            *protective = true;     /* may span "the whole disk" as 0xFFFFFFFF - not range checked */
        }
        else if (!in_medium(scan, entry.first_lba, entry.lba_count))
        {
            return false;
        }
    }
    return used;
}

/* Walk the EBR chain of one extended partition. Returns false if the caller stopped the scan. */
static bool mbr_scan_logical(scan_t* scan, const mbr_entry_t* extended, uint32_t* number)
{
    uint64_t ext_start = extended->first_lba;
    uint64_t ext_end   = ext_start + extended->lba_count;
    uint64_t ebr       = ext_start;

    for (uint32_t i = 0; i < MBR_MAX_LOGICAL && read_block(scan, ebr) && dmblkid_has_boot_signature(scan->block); i++)
    {
        mbr_entry_t logical = mbr_entry(scan->block, 0);
        mbr_entry_t next    = mbr_entry(scan->block, 1);
        uint64_t first = ebr + logical.first_lba;
        if (mbr_entry_used(&logical) && first + logical.lba_count <= ext_end &&
            !report(scan, (*number)++, first, logical.lba_count))
        {
            return false;
        }
        uint64_t next_ebr = ext_start + next.first_lba;
        if (!mbr_entry_used(&next) || !mbr_is_extended(next.type) || next_ebr <= ebr || next_ebr >= ext_end)
        {
            break;
        }
        ebr = next_ebr;
    }
    return true;
}

static void mbr_scan(scan_t* scan, const uint8_t* sector_copy)
{
    mbr_entry_t entries[MBR_ENTRY_COUNT];
    for (uint32_t i = 0; i < MBR_ENTRY_COUNT; i++)
    {
        entries[i] = mbr_entry(sector_copy, i);
    }
    uint32_t logical_number = MBR_FIRST_LOGICAL_NUMBER;
    for (uint32_t i = 0; i < MBR_ENTRY_COUNT; i++)
    {
        if (!mbr_entry_used(&entries[i]))
        {
            continue;
        }
        bool go_on = mbr_is_extended(entries[i].type)
                   ? mbr_scan_logical(scan, &entries[i], &logical_number)
                   : report(scan, i + 1u, entries[i].first_lba, entries[i].lba_count);
        if (!go_on)
        {
            return;
        }
    }
}

/* ---- GPT ---- */

typedef struct
{
    uint64_t    first_usable;
    uint64_t    last_usable;
    uint64_t    entries_lba;
    uint32_t    entry_count;
    uint32_t    entry_size;
    uint32_t    entries_crc;
    uint8_t     disk_guid[GUID_SIZE];
} gpt_header_t;

static bool gpt_read_header(scan_t* scan, uint64_t lba, gpt_header_t* header)
{
    if (!read_block(scan, lba) || !dmblkid_bytes_equal(scan->block, "EFI PART", 8))
    {
        return false;
    }
    uint32_t size = dmblkid_le32(scan->block + 12);
    uint32_t crc  = dmblkid_le32(scan->block + 16);
    if (size < GPT_HEADER_MIN_SIZE || size > scan->block_size || dmblkid_le64(scan->block + 24) != lba)
    {
        return false;
    }
    memset(scan->block + 16, 0, 4);     /* the CRC is computed with its own field zeroed */
    if (crc32_update(0, scan->block, size) != crc)
    {
        return false;
    }
    header->first_usable = dmblkid_le64(scan->block + 40);
    header->last_usable  = dmblkid_le64(scan->block + 48);
    header->entries_lba  = dmblkid_le64(scan->block + 72);
    header->entry_count  = dmblkid_le32(scan->block + 80);
    header->entry_size   = dmblkid_le32(scan->block + 84);
    header->entries_crc  = dmblkid_le32(scan->block + 88);
    memcpy(header->disk_guid, scan->block + GPT_DISK_GUID_OFFSET, GUID_SIZE);
    return header->entry_size >= GPT_ENTRY_MIN_SIZE && header->entry_size % 8u == 0 &&
           scan->block_size % header->entry_size == 0 && header->entry_count <= GPT_MAX_ENTRIES &&
           header->first_usable <= header->last_usable && header->last_usable < scan->block_count;
}

static uint64_t gpt_entry_blocks(const scan_t* scan, const gpt_header_t* header)
{
    uint64_t bytes = (uint64_t)header->entry_count * header->entry_size;
    return (bytes + scan->block_size - 1u) / scan->block_size;
}

static bool gpt_entries_valid(scan_t* scan, const gpt_header_t* header)
{
    uint64_t blocks = gpt_entry_blocks(scan, header);
    uint64_t remaining = (uint64_t)header->entry_count * header->entry_size;
    uint32_t crc = 0;
    if (!in_medium(scan, header->entries_lba, blocks == 0 ? 1 : blocks))
    {
        return false;
    }
    for (uint64_t b = 0; b < blocks; b++)
    {
        if (!read_block(scan, header->entries_lba + b))
        {
            return false;
        }
        size_t chunk = (remaining < scan->block_size) ? (size_t)remaining : scan->block_size;
        crc = crc32_update(crc, scan->block, chunk);
        remaining -= chunk;
    }
    return crc == header->entries_crc;
}

static void gpt_report(scan_t* scan, const gpt_header_t* header)
{
    uint32_t per_block = scan->block_size / header->entry_size;
    for (uint32_t i = 0; i < header->entry_count; i++)
    {
        if (i % per_block == 0 && !read_block(scan, header->entries_lba + i / per_block))
        {
            return;
        }
        const uint8_t* entry = scan->block + (i % per_block) * header->entry_size;
        uint64_t first = dmblkid_le64(entry + 32);
        uint64_t last  = dmblkid_le64(entry + 40);
        if (all_zero(entry, 16) || first > last || first < header->first_usable || last > header->last_usable)
        {
            continue;
        }
        if (!report(scan, i + 1u, first, last - first + 1u))
        {
            return;
        }
    }
}

/* Primary header and entries first, the backup at the last LBA if either is damaged. */
static bool gpt_scan(scan_t* scan)
{
    gpt_header_t header;
    bool valid = gpt_read_header(scan, 1, &header) && gpt_entries_valid(scan, &header);
    if (!valid)
    {
        valid = gpt_read_header(scan, scan->block_count - 1u, &header) && gpt_entries_valid(scan, &header);
    }
    if (valid)
    {
        memcpy(scan->disk_id, header.disk_guid, GUID_SIZE);
        gpt_report(scan, &header);
    }
    return valid;
}

/* ---- entry points ---- */

static dmblkid_ptable_t scan_medium(scan_t* scan)
{
    bool protective = false;
    if (!read_block(scan, 0) || !mbr_is_valid(scan, scan->block, &protective))
    {
        return dmblkid_ptable_none;
    }
    if (protective)
    {
        return gpt_scan(scan) ? dmblkid_ptable_gpt : dmblkid_ptable_none;
    }

    /* The EBR walk reuses the block buffer - scan from a copy of the MBR. */
    uint8_t* mbr = Dmod_Malloc(scan->block_size);
    if (mbr == NULL)
    {
        return dmblkid_ptable_none;
    }
    memcpy(mbr, scan->block, scan->block_size);
    memcpy(scan->disk_id, mbr + MBR_DISK_SIGNATURE_OFFSET, 4);
    mbr_scan(scan, mbr);
    Dmod_Free(mbr);
    return dmblkid_ptable_mbr;
}

static bool block_size_valid(uint32_t block_size)
{
    return block_size >= DMBLKID_SECTOR_SIZE && block_size <= DMBLKID_MAX_BLOCK_SIZE &&
           dmblkid_is_power_of_2(block_size);
}

static dmblkid_ptable_t run_scan(scan_t* scan_state)
{
    if (scan_state->read == NULL || !block_size_valid(scan_state->block_size) || scan_state->block_count < 2u)
    {
        return dmblkid_ptable_none;
    }
    scan_state->block = Dmod_Malloc(scan_state->block_size);
    if (scan_state->block == NULL)
    {
        return dmblkid_ptable_none;
    }
    dmblkid_ptable_t table = scan_medium(scan_state);
    Dmod_Free(scan_state->block);
    scan_state->block = NULL;
    return table;
}

dmblkid_ptable_t dmblkid_ptable_scan(dmblkid_part_read_t read, void* read_ctx, uint32_t block_size,
                                     uint64_t block_count, dmblkid_part_found_t found, void* found_ctx)
{
    scan_t state = { 0 };
    state.read        = read;
    state.read_ctx    = read_ctx;
    state.block_size  = block_size;
    state.block_count = block_count;
    state.found       = found;
    state.found_ctx   = found_ctx;
    return run_scan(&state);
}

/* ---- prober ---- */

/*
 * dmblkid_part_read_t over a dmblkid_source_t. Static on purpose: the
 * address of an external function is loaded through .got, which x86_64 ld
 * relaxes into an absolute constant the module loader never relocates.
 */
static int source_part_read(void* ctx, uint64_t offset, void* buffer, size_t size)
{
    return dmblkid_source_read((const dmblkid_source_t*)ctx, offset, buffer, size);
}

static bool gpt_signature_at(const dmblkid_source_t* source, uint64_t offset)
{
    uint8_t signature[8];
    return dmblkid_source_read(source, offset, signature, sizeof(signature)) == 0 &&
           dmblkid_bytes_equal(signature, "EFI PART", sizeof(signature));
}

/*
 * Nodes that answer DMDRVI_IOCTL_BLOCK_GET_INFO give their block size. For
 * anything else: the size whose primary (LBA 1) or backup (last LBA) GPT
 * header is where it should be, 512 otherwise.
 */
static uint32_t pick_block_size(const dmblkid_source_t* source)
{
    if (source->block_size != 0)
    {
        return source->block_size;
    }
    for (uint32_t size = DMBLKID_SECTOR_SIZE; size <= DMBLKID_MAX_BLOCK_SIZE; size *= 2u)
    {
        uint64_t last = (source->size / size > 1u) ? (source->size / size - 1u) * size : 0;
        if (gpt_signature_at(source, size) || (last != 0 && gpt_signature_at(source, last)))
        {
            return size;
        }
    }
    return DMBLKID_SECTOR_SIZE;
}

/* PTUUID: the MBR disk signature as 8 hex digits, the GPT disk GUID in the usual text form. */
static int set_table_id(dmblkid_t* result, dmblkid_ptable_t table, const uint8_t* id)
{
    char text[37];
    if (table == dmblkid_ptable_mbr)
    {
        uint32_t signature = dmblkid_le32(id);
        if (signature == 0)
        {
            return 0;
        }
        dmblkid_hex(text, signature, 8, false);
        text[8] = '\0';
        return dmblkid_set_string(&result->uuid, text);
    }
    dmblkid_hex(text, dmblkid_le32(id), 8, false);
    text[8] = '-';
    dmblkid_hex(text + 9, dmblkid_le16(id + 4), 4, false);
    text[13] = '-';
    dmblkid_hex(text + 14, dmblkid_le16(id + 6), 4, false);
    text[18] = '-';
    char* out = text + 19;
    for (int i = 8; i < 16; i++)
    {
        dmblkid_hex(out, id[i], 2, false);
        out += 2;
        if (i == 9)
        {
            *out++ = '-';
        }
    }
    *out = '\0';
    return dmblkid_set_string(&result->uuid, text);
}

int dmblkid_probe_ptable(const dmblkid_source_t* source, dmblkid_t* result)
{
    scan_t state = { 0 };
    state.read        = source_part_read;
    state.read_ctx    = (void*)source;
    state.block_size  = pick_block_size(source);
    state.block_count = source->size / state.block_size;

    dmblkid_ptable_t table = run_scan(&state);
    if (table == dmblkid_ptable_none)
    {
        return 0;
    }
    result->usage           = dmblkid_usage_partition_table;
    result->type            = (table == dmblkid_ptable_gpt) ? "gpt" : "mbr";
    result->block_size      = state.block_size;
    result->partition_count = state.count;
    int ret = set_table_id(result, table, state.disk_id);
    return (ret < 0) ? ret : 1;
}
