#ifndef DMPART_H
#define DMPART_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "dmod_types.h"
#include "dmpart_defs.h"

/**
 * dmpart - partition table parser (MBR incl. extended/logical partitions, GPT).
 *
 * A pure parser: it reads the medium through a caller-supplied function and
 * reports every partition it finds, with no knowledge of nodes, drivers or
 * files. dmdevfs uses it to create partition nodes, dmblkid to tell a
 * partitioned disk from a filesystem - one implementation, so both always
 * agree on what is a partition table.
 */

/** Kind of partition table found on a medium. */
typedef enum
{
    dmpart_table_none = 0,          /**< No (valid) table - e.g. a superfloppy or a blank medium */
    dmpart_table_mbr,               /**< MBR, possibly with an extended partition */
    dmpart_table_gpt,               /**< GPT (primary or backup header) */
} dmpart_table_t;

/** Size of dmpart_info_t::disk_id. */
#define DMPART_DISK_ID_SIZE     16u

/**
 * @brief Read exactly @p size bytes at byte @p offset of the medium
 * @return 0 on success, a negative value if the read failed or was short
 */
typedef int (*dmpart_read_t)(void* ctx, uint64_t offset, void* buffer, size_t size);

/**
 * @brief Called for every partition found
 *
 * @param number    Partition number: MBR primaries 1-4 by slot, logical
 *                  partitions from 5 in chain order; GPT entry index + 1.
 * @param first_lba First block of the partition.
 * @param lba_count Number of blocks.
 * @return false to stop the scan.
 */
typedef bool (*dmpart_found_t)(void* ctx, uint32_t number, uint64_t first_lba, uint64_t lba_count);

/** The medium to scan. */
typedef struct
{
    dmpart_read_t   read;           /**< Medium access */
    void*           ctx;            /**< Passed to read */
    uint32_t        block_size;     /**< Logical block size: 512, 1024, 2048 or 4096 */
    uint64_t        block_count;    /**< Number of logical blocks (>= 2) */
} dmpart_medium_t;

/** What dmpart_scan() found. */
typedef struct
{
    dmpart_table_t  table;
    uint32_t        count;          /**< Partitions reported to the found callback */
    /**
     * MBR: the disk signature in bytes 0-3 (little-endian, as on disk), the
     * rest zero. GPT: the disk GUID as stored on disk (mixed-endian).
     */
    uint8_t         disk_id[DMPART_DISK_ID_SIZE];
} dmpart_info_t;

/**
 * @brief Find the partitions of a medium
 *
 * - MBR: signature, sane status bytes, >= 1 used entry, every entry inside
 *   the medium. A FAT or exFAT boot sector (a partitionless "superfloppy")
 *   is never taken for an MBR, even if its boot code looks like entries.
 *   Extended partitions are followed through their EBR chain.
 * - GPT (protective MBR): primary header at LBA 1, or the backup at the
 *   last LBA if the primary header or its entry array fails its CRC32.
 *
 * A medium that cannot be read is reported as having no table.
 *
 * @param medium    The medium.
 * @param found     Called for every partition, in table order; may be NULL.
 * @param found_ctx Passed to @p found.
 * @param info      Filled with the result (zeroed first).
 * @return 0 (info->table tells what was found), -EINVAL for a NULL info, a
 *         NULL read function or an unsupported geometry, -ENOMEM.
 */
dmod_dmpart_api(1.0, int, _scan, ( const dmpart_medium_t* medium, dmpart_found_t found,
                                   void* found_ctx, dmpart_info_t* info ));

#endif // DMPART_H
