#ifndef DMBLKID_H
#define DMBLKID_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "dmod_types.h"
#include "dmblkid_defs.h"

/**
 * dmblkid - identifies what a block device or partition contains.
 *
 * dmblkid_probe() reads a node (e.g. /dev/dmsdio0/0 or /dev/dmsdio0/0p1)
 * through the regular file API with 64-bit offsets and reports:
 *  - a partition table (MBR/GPT), so a whole disk with partitions is never
 *    mounted, or
 *  - a filesystem (FAT12/16/32, exFAT, dmffs) with its label, uuid and the
 *    DMOD filesystem module that mounts it, or
 *  - nothing recognized (blank or damaged medium, unsupported format).
 *
 * Probing only ever reads: the node is opened read-only.
 */

/** Opaque probe result - see dmblkid_probe(). */
typedef struct dmblkid dmblkid_t;

/** What was found on the node. */
typedef enum
{
    dmblkid_usage_unknown = 0,          /**< Nothing recognized (blank, damaged or unsupported) */
    dmblkid_usage_filesystem,           /**< A filesystem - see dmblkid_get_type() */
    dmblkid_usage_partition_table,      /**< An MBR or GPT - the partitions are separate nodes */
} dmblkid_usage_t;

/**
 * @brief Identify the contents of a node
 *
 * The geometry comes from DMDRVI_IOCTL_BLOCK_GET_INFO when the node answers
 * it (dmdevfs block and partition nodes), otherwise from the file size with
 * 512-byte blocks (4096/2048/1024 if a GPT header is found there) - so
 * image files can be probed too. A node without a size (not a block device,
 * an empty file) is reported as unknown without being read.
 *
 * @param node_path Path of the node or image file.
 * @param result    Set to the result on success; release it with dmblkid_destroy().
 * @return 0 on success - also when nothing was recognized (see
 *         dmblkid_get_usage()); -EINVAL on bad arguments, -ENOENT if the
 *         node cannot be opened, -EIO if reading it failed, -ENOMEM.
 */
dmod_dmblkid_api(1.0, int, _probe, ( const char* node_path, dmblkid_t** result ));

/**
 * @brief Release a result of dmblkid_probe(). Safe to call with NULL.
 */
dmod_dmblkid_api(1.0, void, _destroy, ( dmblkid_t* result ));

/** @return What was found, dmblkid_usage_unknown for an invalid result. */
dmod_dmblkid_api(1.0, dmblkid_usage_t, _get_usage, ( const dmblkid_t* result ));

/**
 * @return "vfat", "exfat" or "dmffs" for a filesystem, "mbr" or "gpt" for a
 *         partition table, NULL if nothing was recognized.
 */
dmod_dmblkid_api(1.0, const char*, _get_type, ( const dmblkid_t* result ));

/** @return Format version ("FAT12"/"FAT16"/"FAT32", exFAT "1.0", dmffs VERSION), or NULL. */
dmod_dmblkid_api(1.0, const char*, _get_version, ( const dmblkid_t* result ));

/** @return Volume label, or NULL if the filesystem has none. */
dmod_dmblkid_api(1.0, const char*, _get_label, ( const dmblkid_t* result ));

/**
 * @return Filesystem serial ("ABCD-1234" for FAT/exFAT), MBR disk signature
 *         ("1a2b3c4d") or GPT disk GUID, or NULL if there is none.
 */
dmod_dmblkid_api(1.0, const char*, _get_uuid, ( const dmblkid_t* result ));

/**
 * @return Name of the DMOD filesystem module that handles this format
 *         ("dmfatfs" for FAT/exFAT, "dmffs"), or NULL if there is none.
 */
dmod_dmblkid_api(1.0, const char*, _get_module, ( const dmblkid_t* result ));

/**
 * @return true if dmblkid_get_module() can mount the node itself. False for
 *         partition tables, unknown contents, formats without a module and
 *         dmffs (it mounts memory-mapped flash, not a node).
 */
dmod_dmblkid_api(1.0, bool, _is_mountable, ( const dmblkid_t* result ));

/** @return Size of the node in bytes, 0 if unknown. */
dmod_dmblkid_api(1.0, uint64_t, _get_size, ( const dmblkid_t* result ));

/** @return Logical block size the partition table was read with, 0 if unknown. */
dmod_dmblkid_api(1.0, uint32_t, _get_block_size, ( const dmblkid_t* result ));

/** @return Number of partitions in the table (partition tables only). */
dmod_dmblkid_api(1.0, uint32_t, _get_partition_count, ( const dmblkid_t* result ));

#endif // DMBLKID_H
