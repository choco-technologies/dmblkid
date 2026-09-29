# dmblkid

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![CI](https://github.com/choco-technologies/dmblkid/actions/workflows/ci.yml/badge.svg)](https://github.com/choco-technologies/dmblkid/actions/workflows/ci.yml)

Identifies what a block device or partition contains - the DMOD counterpart
of `libblkid`. Automount uses it to pick the filesystem module for a node
(and to leave whole disks with partitions alone); the `blkid` application
prints it.

## Description

`dmblkid_probe()` reads a node such as `/dev/dmsdio0/0` or
`/dev/dmsdio0/0p1` (or an image file) through the regular file API with
64-bit offsets and reports one of:

| Found | `usage` | `type` | Also reported |
|-------|---------|--------|---------------|
| MBR (incl. extended/logical partitions) | `partition_table` | `mbr` | disk signature, partition count, block size |
| GPT (primary or backup header) | `partition_table` | `gpt` | disk GUID, partition count, block size |
| FAT12/16/32 | `filesystem` | `vfat` | `FAT12`/`FAT16`/`FAT32`, label, serial |
| exFAT | `filesystem` | `exfat` | revision, label, serial |
| dmffs image | `filesystem` | `dmffs` | VERSION value |
| blank, damaged or unsupported | `unknown` | - | size |

Every type is mapped to the DMOD filesystem module that handles it and
whether that module can mount the node. Today none can: FAT and exFAT have
no module yet, and dmffs mounts memory-mapped flash rather than a node - so
they are reported, but marked not mountable.

Detection is strict, so that nothing is mounted by mistake:

- **FAT** - the whole BPB must be consistent, not only the `0x55AA` marker:
  jump instruction, sector and cluster sizes, reserved sectors, FAT count,
  media byte, regions inside the volume, FATs large enough for every cluster,
  volume inside the node. The FAT type comes from the cluster count, as in
  the Microsoft specification.
- **exFAT** - boot sector fields, region layout and the boot region checksum.
- **dmffs** - no magic number: the top-level TLV chain must be well formed.
- **MBR/GPT** - the same parser dmdevfs uses to create partition nodes, so a
  disk dmdevfs splits into partitions is always reported as a partition
  table. A FAT/exFAT boot sector (a partitionless "superfloppy") is never
  taken for an MBR, even if its boot code looks like partition entries. GPT
  headers and entry arrays are CRC-checked; the backup header is used if the
  primary one is damaged.

Probing never writes: the node is opened read-only. The block size comes from
`DMDRVI_IOCTL_BLOCK_GET_INFO` when the node answers it (dmdevfs block and
partition nodes); otherwise it is 512, or the size at which a GPT header is
found (1024/2048/4096). A node without a size (not a block device) is
reported as unknown without being read.

## Usage

```c
#include "dmblkid.h"

dmblkid_t* result = NULL;
if (dmblkid_probe("/dev/dmsdio0/0p1", &result) == 0)
{
    if (dmblkid_get_usage(result) == dmblkid_usage_filesystem && dmblkid_is_mountable(result))
    {
        mount(dmblkid_get_module(result), ...);
    }
    dmblkid_destroy(result);
}
```

The command line front end:

```
> blkid /dev/dmsdio0/0 /dev/dmsdio0/0p1
/dev/dmsdio0/0: PTTYPE="mbr" PTUUID="1a2b3c4d" PARTITIONS="1" BLOCK_SIZE="512" SIZE="15931539456"
/dev/dmsdio0/0p1: TYPE="vfat" VERSION="FAT32" LABEL="SDCARD" UUID="CAFE-BABE" SIZE="15930490880" MOUNTABLE="no"
```

See [apps/blkid/README.md](apps/blkid/README.md).

## API

| Function | Description |
|----------|-------------|
| `dmblkid_probe()` | Identify the contents of a node; returns a result to query. |
| `dmblkid_destroy()` | Release a result. |
| `dmblkid_get_usage()` | Filesystem, partition table or unknown. |
| `dmblkid_get_type()` | `vfat`, `exfat`, `dmffs`, `mbr`, `gpt`. |
| `dmblkid_get_version()` | `FAT32`, exFAT revision, dmffs VERSION. |
| `dmblkid_get_label()` | Volume label. |
| `dmblkid_get_uuid()` | Serial, MBR disk signature or GPT disk GUID. |
| `dmblkid_get_module()` | DMOD filesystem module for the type. |
| `dmblkid_is_mountable()` | Whether that module can mount the node. |
| `dmblkid_get_size()` | Node size in bytes. |
| `dmblkid_get_block_size()` | Block size the partition table was read with. |
| `dmblkid_get_partition_count()` | Number of partitions in the table. |
| `dmblkid_partitions_scan()` | The MBR/GPT parser on its own, over a caller-supplied read function. |

See [include/dmblkid.h](include/dmblkid.h) and
[docs/api-reference.md](docs/api-reference.md).

## Requirements

dmblkid uses the 64-bit file API (`Dmod_FileSeek`/`Dmod_FileTell`/
`Dmod_FileSize` 2.0, dmod#312), so the system it runs in - the firmware or
`dmod_loader` - must be built from a dmod with those APIs. The
`dmod_loader` in the `chocotechnologies/dmod:1.0.4` image predates them; CI
builds one from dmod `develop` (see below).

## Building

### Using CMake

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

Pass `-DDMOD_DIR=/path/to/local/dmod` to build against a local dmod checkout
instead of fetching `develop` from GitHub, or
`-DDMOD_TOOLS_NAME=arch/armv7/cortex-m7` to build for a target.

### Using Make

```bash
make DMOD_MODE=DMOD_MODULE DMOD_DIR=/path/to/dmod
```

## Testing

The tests build disk images (sparse files, up to 6 GiB) and probe them:
FAT12/16/32, exFAT with its label above 4 GiB, a damaged exFAT boot region,
dmffs, an MBR with a logical partition, GPT with 512 and 4096-byte blocks,
GPT with only the backup header valid at the end of a 6 GiB disk, a blank
device, a file too small for any format, damaged FAT boot sectors, and that
probing leaves the image unchanged.

They need a `dmod_loader` with the 64-bit file API:

```bash
git clone --depth 1 -b develop https://github.com/choco-technologies/dmod.git /tmp/dmod
cmake -S /tmp/dmod -B /tmp/dmod/build -DDMOD_MODE=DMOD_SYSTEM -DDMOD_BUILD_EXAMPLES=ON -DDMOD_BUILD_TESTS=OFF
cmake --build /tmp/dmod/build --target dmod_loader

export DMOD_DMF_DIR=$(pwd)/build/dmf
dmf-get install -d ${DMOD_DMF_DIR}/test_dmblkid-local.dmd -y
/tmp/dmod/build/examples/system/dmod_loader/dmod_loader build/dmf/test_dmblkid.dmf
```

`ctest` runs the same; point it to that loader with
`-DDMOD_LOADER_EXECUTABLE=/tmp/dmod/build/examples/system/dmod_loader/dmod_loader`.

## Documentation

See the `docs/` directory:

- **[api-reference.md](docs/api-reference.md)** - Complete API documentation

View documentation using `dmf-man dmblkid`.

## Project Structure

```
dmblkid/
├── apps/
│   └── blkid/             # blkid command line application
├── docs/                  # Documentation (markdown format)
├── include/
│   └── dmblkid.h          # Public API
├── src/
│   ├── dmblkid.c          # dmblkid_probe(), result accessors
│   ├── dmblkid_internal.h
│   ├── dmblkid_source.c   # Node access (file API, geometry)
│   ├── dmblkid_util.c     # Result/string helpers
│   ├── dmblkid_ptable.c   # MBR/GPT
│   ├── dmblkid_fat.c      # FAT12/16/32
│   ├── dmblkid_exfat.c    # exFAT
│   └── dmblkid_dmffs.c    # dmffs
├── tests/
│   ├── CMakeLists.txt
│   ├── dmblkid_test.c
│   └── test_images.c      # Image builders
├── CMakeLists.txt
├── Makefile
├── dmblkid.dmr
└── manifest.dmm
```

## Author

Patryk Kubiak

## License

MIT
