/**
 * @file dmblkid_internal.h
 * @brief Shared state and helpers of the dmblkid probers
 */
#ifndef DMBLKID_INTERNAL_H
#define DMBLKID_INTERNAL_H

#include "dmod.h"
#include "dmblkid.h"
#include <errno.h>

#define DMBLKID_MAGIC               0x424C4B49u     /* 'BLKI' */
#define DMBLKID_SECTOR_SIZE         512u
#define DMBLKID_MAX_BLOCK_SIZE      4096u

/*
 * Probe result. type/module point to string literals chosen in code (never
 * freed); version/label/uuid are read from the medium and heap-owned.
 *
 * No prober keeps a table of pointers in initialized data: the module loader
 * relocates only .got, so such a table would keep link-time addresses on
 * target. Probers are called one after another in dmblkid.c instead.
 */
struct dmblkid
{
    uint32_t            magic;
    dmblkid_usage_t     usage;
    const char*         type;
    const char*         module;
    bool                mountable;
    char*               version;
    char*               label;
    char*               uuid;
    uint64_t            size;
    uint32_t            block_size;
    uint32_t            partition_count;
};

/* The node being probed. All reads are bounds-checked against size when it is known. */
typedef struct
{
    void*       file;
    uint64_t    size;           /**< Bytes, 0 if unknown */
    uint32_t    block_size;     /**< From DMDRVI_IOCTL_BLOCK_GET_INFO, 0 if the node does not answer it */
} dmblkid_source_t;

/*
 * Every prober returns 1 if it recognized the contents (and filled the
 * result), 0 if not, or a negative errno (I/O error, no memory) that aborts
 * the probe.
 */
int dmblkid_probe_ptable(const dmblkid_source_t* source, dmblkid_t* result);
int dmblkid_probe_fat(const dmblkid_source_t* source, dmblkid_t* result);
int dmblkid_probe_exfat(const dmblkid_source_t* source, dmblkid_t* result);
int dmblkid_probe_dmffs(const dmblkid_source_t* source, dmblkid_t* result);

/** dmblkid_partitions_scan() - see dmblkid.h. */
dmblkid_ptable_t dmblkid_ptable_scan(dmblkid_part_read_t read, void* read_ctx, uint32_t block_size,
                                     uint64_t block_count, dmblkid_part_found_t found, void* found_ctx);

/* ---- source access (dmblkid_source.c) ---- */

int  dmblkid_source_open(dmblkid_source_t* source, const char* path);
void dmblkid_source_close(dmblkid_source_t* source);

/** Read exactly @p size bytes at @p offset: 0, -EIO (failed/short) or -ERANGE (past the end). */
int  dmblkid_source_read(const dmblkid_source_t* source, uint64_t offset, void* buffer, size_t size);

/** A structure that does not fit in the node is simply not there: -ERANGE becomes 0 ("not recognized"). */
static inline int dmblkid_not_found_if_outside(int ret)
{
    return (ret == -ERANGE) ? 0 : ret;
}

/* ---- result helpers (dmblkid_util.c) ---- */

/** Fill the result for a recognized filesystem. module may be NULL. */
void dmblkid_set_filesystem(dmblkid_t* result, const char* type, const char* module, bool mountable);

/** Take a heap copy of @p value into @p field (freeing the previous one). -ENOMEM on failure. */
int  dmblkid_set_string(char** field, const char* value);

/** "ABCD-1234" from a 32-bit FAT/exFAT volume serial. */
int  dmblkid_set_serial(char** field, uint32_t serial);

/**
 * Label from a fixed-size, space-padded 8-bit field: trailing spaces and NULs
 * are dropped, non-ASCII bytes become '?'. An empty label leaves the field NULL.
 */
int  dmblkid_set_label8(char** field, const uint8_t* label, size_t size);

/** Label from UTF-16LE code units (UTF-8 result, surrogates become '?'). */
int  dmblkid_set_label16(char** field, const uint8_t* label, size_t units);

/** Lowercase or uppercase hex of @p digits digits of @p value into @p out (not terminated). */
void dmblkid_hex(char* out, uint64_t value, unsigned digits, bool upper);

/* ---- little-endian decoding ---- */

static inline uint16_t dmblkid_le16(const uint8_t* p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static inline uint32_t dmblkid_le32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint64_t dmblkid_le64(const uint8_t* p)
{
    return (uint64_t)dmblkid_le32(p) | ((uint64_t)dmblkid_le32(p + 4) << 32);
}

static inline bool dmblkid_bytes_equal(const uint8_t* a, const char* b, size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        if (a[i] != (uint8_t)b[i])
        {
            return false;
        }
    }
    return true;
}

static inline bool dmblkid_is_power_of_2(uint32_t value)
{
    return value != 0 && (value & (value - 1u)) == 0;
}

static inline bool dmblkid_has_boot_signature(const uint8_t* sector)
{
    return sector[510] == 0x55u && sector[511] == 0xAAu;
}

#endif // DMBLKID_INTERNAL_H
