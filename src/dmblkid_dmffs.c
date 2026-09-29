#include "dmblkid_internal.h"

/*
 * dmffs (choco-technologies/dmffs): a chain of little-endian TLVs
 * { uint32 type; uint32 length; value[length] }. make_dmffs writes
 *   VERSION("1.0") { FILE | DIR }* END(0xFFFFFFFF)
 * at the top level (DIR/FILE nest their contents in their value).
 *
 * There is no magic number, so the chain itself is the signature: an
 * optional VERSION with a short printable value, then top-level FILE/DIR
 * entries that all fit in the node, up to END. Without a VERSION the chain
 * must reach END within DMFFS_MAX_TOP_LEVEL entries.
 *
 * The dmffs module mounts memory-mapped flash (flash_addr/flash_size), not a
 * node, so an image found on a node is reported but not mountable.
 */

#define DMFFS_TLV_FILE          1u
#define DMFFS_TLV_DIR           2u
#define DMFFS_TLV_VERSION       3u
#define DMFFS_TLV_END           0xFFFFFFFFu
#define DMFFS_TLV_HEADER_SIZE   8u
#define DMFFS_VERSION_MAX       16u
#define DMFFS_MAX_TOP_LEVEL     256u

static int read_tlv(const dmblkid_source_t* source, uint64_t offset, uint32_t* type, uint32_t* length)
{
    uint8_t header[DMFFS_TLV_HEADER_SIZE];
    int ret = dmblkid_source_read(source, offset, header, sizeof(header));
    *type   = dmblkid_le32(header);
    *length = dmblkid_le32(header + 4);
    return ret;
}

/* The VERSION value if it is short printable ASCII; 1 if it is, 0 if not, < 0 on error. */
static int read_version(const dmblkid_source_t* source, uint32_t length, char* version)
{
    if (length == 0 || length > DMFFS_VERSION_MAX)
    {
        return 0;
    }
    int ret = dmblkid_source_read(source, DMFFS_TLV_HEADER_SIZE, version, length);
    if (ret != 0)
    {
        return dmblkid_not_found_if_outside(ret);
    }
    for (uint32_t i = 0; i < length; i++)
    {
        if (version[i] < 0x20 || version[i] > 0x7E)
        {
            return 0;
        }
    }
    version[length] = '\0';
    return 1;
}

/* 1 if the top-level chain from @p offset is well formed, 0 if not, < 0 on error. */
static int walk_top_level(const dmblkid_source_t* source, uint64_t offset, bool has_version)
{
    uint32_t entries = 0;
    for (; entries < DMFFS_MAX_TOP_LEVEL; entries++)
    {
        uint32_t type, length;
        int ret = read_tlv(source, offset, &type, &length);
        if (ret != 0)
        {
            return dmblkid_not_found_if_outside(ret);
        }
        if (type == DMFFS_TLV_END)
        {
            return (has_version || entries != 0) ? 1 : 0;
        }
        uint64_t next = offset + DMFFS_TLV_HEADER_SIZE + length;
        if ((type != DMFFS_TLV_FILE && type != DMFFS_TLV_DIR) || (source->size != 0 && next > source->size))
        {
            return 0;
        }
        offset = next;
    }
    return has_version ? 1 : 0;     /* a long root without END: trust it only with a VERSION */
}

static int describe(dmblkid_t* result, const char* version)
{
    dmblkid_set_filesystem(result, "dmffs", "dmffs", false);
    int ret = (version[0] != '\0') ? dmblkid_set_string(&result->version, version) : 0;
    return (ret < 0) ? ret : 1;
}

int dmblkid_probe_dmffs(const dmblkid_source_t* source, dmblkid_t* result)
{
    char version[DMFFS_VERSION_MAX + 1u] = { 0 };
    uint32_t type, length;
    int ret = read_tlv(source, 0, &type, &length);
    if (ret != 0)
    {
        return dmblkid_not_found_if_outside(ret);
    }
    uint64_t offset = 0;
    if (type == DMFFS_TLV_VERSION)
    {
        ret = read_version(source, length, version);
        offset = DMFFS_TLV_HEADER_SIZE + (uint64_t)length;
    }
    else if (type == DMFFS_TLV_FILE || type == DMFFS_TLV_DIR)
    {
        ret = 1;
    }
    ret = (ret == 1) ? walk_top_level(source, offset, version[0] != '\0') : ret;
    return (ret == 1) ? describe(result, version) : ret;
}
