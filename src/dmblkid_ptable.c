#include "dmblkid_internal.h"
#include "dmpart.h"
#include <errno.h>

/*
 * Partition tables are found by dmpart - the parser dmdevfs uses to create
 * partition nodes - so a medium dmdevfs splits into partitions is reported
 * here as a partition table, never as a filesystem to mount whole.
 */

/*
 * dmpart_read_t over a dmblkid_source_t. Static on purpose: the
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
static int set_table_id(dmblkid_t* result, dmpart_table_t table, const uint8_t* id)
{
    char text[37];
    if (table == dmpart_table_mbr)
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
    dmpart_medium_t medium = { source_part_read, (void*)source, pick_block_size(source), 0 };
    medium.block_count = source->size / medium.block_size;
    dmpart_info_t info;
    int ret = dmpart_scan(&medium, NULL, NULL, &info);
    if (ret == -EINVAL || info.table == dmpart_table_none)
    {
        return (ret == -EINVAL) ? 0 : ret;      /* -EINVAL: too small to hold a table */
    }
    result->usage           = dmblkid_usage_partition_table;
    result->type            = (info.table == dmpart_table_gpt) ? "gpt" : "mbr";
    result->block_size      = medium.block_size;
    result->partition_count = info.count;
    ret = set_table_id(result, info.table, info.disk_id);
    return (ret < 0) ? ret : 1;
}
