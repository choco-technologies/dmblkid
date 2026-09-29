# dmblkid API Reference

```c
#include "dmblkid.h"
```

## Types

### `dmblkid_t`

Opaque probe result, created by `dmblkid_probe()` and released with
`dmblkid_destroy()`. Every accessor checks that it gets a valid result and
returns the "nothing" value (`NULL`, `0`, `false`, `dmblkid_usage_unknown`)
otherwise.

### `dmblkid_usage_t`

| Value | Meaning |
|-------|---------|
| `dmblkid_usage_unknown` | Nothing recognized: blank, damaged or unsupported contents. |
| `dmblkid_usage_filesystem` | A filesystem. |
| `dmblkid_usage_partition_table` | An MBR or GPT. Its partitions are separate nodes (dmdevfs `<node>p<N>`); the whole node must not be mounted. |

## Functions

### `dmblkid_probe`

```c
int dmblkid_probe(const char* node_path, dmblkid_t** result);
```

Opens `node_path` read-only and identifies its contents. Probe order:

1. partition table (MBR, GPT),
2. FAT12/16/32, exFAT,
3. dmffs.

The first match wins.

Geometry: `DMDRVI_IOCTL_BLOCK_GET_INFO` if the node answers it, otherwise the
file size with a 512-byte block size (or 1024/2048/4096 if a GPT header is
found at that block size). A node without a size - not a block device (a
UART would block the first read), or an empty file - is reported as unknown
without being read.

Returns:

| Value | Meaning |
|-------|---------|
| `0` | Success. `*result` is set, also when nothing was recognized. |
| `-EINVAL` | `node_path` or `result` is `NULL`. |
| `-ENOENT` | The node cannot be opened. |
| `-EIO` | Reading the node failed. |
| `-ENOMEM` | Out of memory. |

A structure that would lie beyond the end of the node is treated as "not
this format", not as an error.

### `dmblkid_destroy`

```c
void dmblkid_destroy(dmblkid_t* result);
```

Releases a result. `NULL` and invalid pointers are ignored.

### `dmblkid_get_usage`

```c
dmblkid_usage_t dmblkid_get_usage(const dmblkid_t* result);
```

### `dmblkid_get_type`

```c
const char* dmblkid_get_type(const dmblkid_t* result);
```

`"vfat"`, `"exfat"`, `"dmffs"`, `"mbr"`, `"gpt"`, or `NULL` if nothing was
recognized.

### `dmblkid_get_version`

```c
const char* dmblkid_get_version(const dmblkid_t* result);
```

| Type | Version |
|------|---------|
| `vfat` | `"FAT12"`, `"FAT16"` or `"FAT32"` |
| `exfat` | FileSystemRevision, e.g. `"1.0"` |
| `dmffs` | The VERSION TLV value (e.g. `"1.0"`), `NULL` if the image has none |

### `dmblkid_get_label`

```c
const char* dmblkid_get_label(const dmblkid_t* result);
```

The volume label, or `NULL` if there is none.

- **FAT**: the volume label entry of the root directory (the label the OS
  shows). Otherwise the extended BPB label; `"NO NAME"` means no label.
  Trailing spaces are removed; non-ASCII bytes become `?`.
- **exFAT**: the volume label entry of the first root directory cluster,
  converted to UTF-8.

### `dmblkid_get_uuid`

```c
const char* dmblkid_get_uuid(const dmblkid_t* result);
```

| Type | Format |
|------|--------|
| `vfat`, `exfat` | Volume serial, `"ABCD-1234"` |
| `mbr` | Disk signature, `"1a2b3c4d"` (`NULL` if zero) |
| `gpt` | Disk GUID, `"01234567-89ab-cdef-0011-223344556677"` |

### `dmblkid_get_module`

```c
const char* dmblkid_get_module(const dmblkid_t* result);
```

The DMOD filesystem module that handles the type: `"dmffs"` for dmffs.
`NULL` for types the ecosystem has no module for yet (FAT, exFAT), for
partition tables and for unknown contents.

### `dmblkid_is_mountable`

```c
bool dmblkid_is_mountable(const dmblkid_t* result);
```

`true` only if `dmblkid_get_module()` can mount the node itself. `false` for
dmffs: its module mounts memory-mapped flash (`flash_addr`/`flash_size`), not
a node.

### `dmblkid_get_size`

```c
uint64_t dmblkid_get_size(const dmblkid_t* result);
```

Size of the node in bytes, `0` if unknown.

### `dmblkid_get_block_size`

```c
uint32_t dmblkid_get_block_size(const dmblkid_t* result);
```

The logical block size the partition table was read with (partition tables
only, otherwise `0`).

### `dmblkid_get_partition_count`

```c
uint32_t dmblkid_get_partition_count(const dmblkid_t* result);
```

The number of partitions in the table. The extended partition container of
an MBR is not counted; its logical partitions are.

## Partition tables

The MBR/GPT parser is the separate `dmpart` library module
(`libs/dmpart`, `dmpart_scan()`), shared with dmdevfs - see its README.
