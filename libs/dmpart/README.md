# dmpart

Partition table parser - MBR (with extended/logical partitions) and GPT - as
a small DMOD library module. It is a pure parser: it reads the medium through
a function the caller supplies and reports every partition it finds, with no
knowledge of nodes, drivers or files.

It is the one implementation both users of partition tables share, so they
always agree on what a partition table is:

- **dmdevfs** creates the partition nodes (`<node>p<N>`) of a block device
  from it,
- **dmblkid** reports a medium with a table as `partition_table`, so the whole
  disk is never mounted as a filesystem.

It is built and released together with dmblkid.

## Usage

```c
#include "dmpart.h"

static int read_medium(void* ctx, uint64_t offset, void* buffer, size_t size)
{
    /* read exactly `size` bytes at byte `offset`, 0 on success */
}

static bool on_partition(void* ctx, uint32_t number, uint64_t first_lba, uint64_t lba_count)
{
    /* number: MBR 1-4 by slot, logical from 5; GPT entry index + 1 */
    return true;    /* false stops the scan */
}

dmpart_medium_t medium = { read_medium, my_ctx, block_size, block_count };
dmpart_info_t info;
if (dmpart_scan(&medium, on_partition, my_ctx, &info) == 0 && info.table != dmpart_table_none)
{
    /* info.table: dmpart_table_mbr / dmpart_table_gpt, info.count partitions,
       info.disk_id: MBR disk signature (bytes 0-3) or GPT disk GUID */
}
```

Pass callbacks that are `static` functions of the calling module: the
address of an external function is loaded through `.got`, which x86_64 `ld`
relaxes into an absolute constant the module loader does not relocate.

## What is accepted

- **MBR**: `0x55AA` signature, status bytes `0x00`/`0x80`, at least one used
  entry, every entry inside the medium. A FAT or exFAT boot sector (a
  partitionless "superfloppy") is never taken for an MBR, even if its boot
  code looks like partition entries. Extended partitions (types `0x05`,
  `0x0F`, `0x85`) are followed through their EBR chain - at most 64 logical
  partitions; loops and logical partitions outside the extended partition
  are ignored.
- **GPT** (protective MBR, type `0xEE`): the primary header at LBA 1, or the
  backup header at the last LBA if the primary header or its entry array
  fails its CRC32. Entries outside the usable range are skipped.
- Block sizes 512, 1024, 2048 and 4096; 64-bit offsets throughout.

A medium that cannot be read is reported as having no table. `dmpart_scan()`
returns `-EINVAL` for a `NULL` info, a `NULL` read function or an
unsupported geometry, `-ENOMEM` if it cannot allocate its block buffer, and
0 otherwise.

See [include/dmpart.h](include/dmpart.h) for the full declarations.

## Tests

`tests/dmpart_test.c` scans an in-memory sparse medium: MBR primaries and
logical partitions, EBR loops and escapes, invalid tables, FAT/exFAT boot
sectors, blank media, GPT with 512/4096-byte blocks, backup header above
4 GiB, damaged entry arrays, both headers damaged, stopping the scan, and
bad arguments.
