#define DMOD_ENABLE_REGISTRATION ON
#include "dmblkid_internal.h"
#include <string.h>

/*
 * Probe order:
 *  1. partition table - the same parser dmdevfs uses for partition nodes,
 *     so a disk it splits is never reported as one filesystem; it does not
 *     take FAT/exFAT boot sectors for an MBR,
 *  2. FAT and exFAT - strong, fully validated boot sectors,
 *  3. dmffs - no magic number, recognized by its TLV chain, so last.
 * The first prober that recognizes the contents wins.
 */

static bool is_valid(const dmblkid_t* result)
{
    return result != NULL && result->magic == DMBLKID_MAGIC;
}

static dmblkid_t* create(void)
{
    dmblkid_t* result = Dmod_Malloc(sizeof(*result));
    if (result != NULL)
    {
        memset(result, 0, sizeof(*result));
        result->magic = DMBLKID_MAGIC;
        result->usage = dmblkid_usage_unknown;
    }
    return result;
}

static int run_probers(const dmblkid_source_t* source, dmblkid_t* result)
{
    int ret = dmblkid_probe_ptable(source, result);
    if (ret == 0)
    {
        ret = dmblkid_probe_fat(source, result);
    }
    if (ret == 0)
    {
        ret = dmblkid_probe_exfat(source, result);
    }
    if (ret == 0)
    {
        ret = dmblkid_probe_dmffs(source, result);
    }
    return ret;
}

dmod_dmblkid_api_declaration(1.0, int, _probe, ( const char* node_path, dmblkid_t** result ))
{
    if (node_path == NULL || result == NULL)
    {
        return -EINVAL;
    }
    *result = NULL;
    dmblkid_source_t source = { 0 };
    int ret = dmblkid_source_open(&source, node_path);
    if (ret != 0)
    {
        return ret;
    }
    /*
     * No size: not a block device (a UART would block the first read) or an
     * empty file - nothing to identify, nothing is read.
     */
    dmblkid_t* probe = create();
    ret = (probe != NULL) ? 0 : -ENOMEM;
    if (ret == 0 && source.size != 0)
    {
        ret = run_probers(&source, probe);
    }
    dmblkid_source_close(&source);
    if (ret < 0)
    {
        dmblkid_destroy(probe);
        return ret;
    }
    probe->size = source.size;
    *result = probe;
    return 0;
}

dmod_dmblkid_api_declaration(1.0, void, _destroy, ( dmblkid_t* result ))
{
    if (!is_valid(result))
    {
        return;
    }
    Dmod_Free(result->version);
    Dmod_Free(result->label);
    Dmod_Free(result->uuid);
    result->magic = 0;
    Dmod_Free(result);
}

dmod_dmblkid_api_declaration(1.0, dmblkid_usage_t, _get_usage, ( const dmblkid_t* result ))
{
    return is_valid(result) ? result->usage : dmblkid_usage_unknown;
}

dmod_dmblkid_api_declaration(1.0, const char*, _get_type, ( const dmblkid_t* result ))
{
    return is_valid(result) ? result->type : NULL;
}

dmod_dmblkid_api_declaration(1.0, const char*, _get_version, ( const dmblkid_t* result ))
{
    return is_valid(result) ? result->version : NULL;
}

dmod_dmblkid_api_declaration(1.0, const char*, _get_label, ( const dmblkid_t* result ))
{
    return is_valid(result) ? result->label : NULL;
}

dmod_dmblkid_api_declaration(1.0, const char*, _get_uuid, ( const dmblkid_t* result ))
{
    return is_valid(result) ? result->uuid : NULL;
}

dmod_dmblkid_api_declaration(1.0, const char*, _get_module, ( const dmblkid_t* result ))
{
    return is_valid(result) ? result->module : NULL;
}

dmod_dmblkid_api_declaration(1.0, bool, _is_mountable, ( const dmblkid_t* result ))
{
    return is_valid(result) && result->mountable;
}

dmod_dmblkid_api_declaration(1.0, uint64_t, _get_size, ( const dmblkid_t* result ))
{
    return is_valid(result) ? result->size : 0;
}

dmod_dmblkid_api_declaration(1.0, uint32_t, _get_block_size, ( const dmblkid_t* result ))
{
    return is_valid(result) ? result->block_size : 0;
}

dmod_dmblkid_api_declaration(1.0, uint32_t, _get_partition_count, ( const dmblkid_t* result ))
{
    return is_valid(result) ? result->partition_count : 0;
}

dmod_dmblkid_api_declaration(1.0, dmblkid_ptable_t, _partitions_scan, ( dmblkid_part_read_t read, void* read_ctx,
                                                                         uint32_t block_size, uint64_t block_count,
                                                                         dmblkid_part_found_t found, void* found_ctx ))
{
    return dmblkid_ptable_scan(read, read_ctx, block_size, block_count, found, found_ctx);
}

int dmod_init(const Dmod_Config_t *Config)
{
    (void)Config;
    return 0;
}

int dmod_deinit(void)
{
    return 0;
}
