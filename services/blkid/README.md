# blkid

Command line front end of [dmblkid](../../README.md): prints what is on each
node given as an argument.

```
blkid <node>...
```

One line per node:

```
> blkid /dev/dmsdio0/0 /dev/dmsdio0/0p1 /dev/dmsdio0/0p2
/dev/dmsdio0/0: PTTYPE="mbr" PTUUID="1a2b3c4d" PARTITIONS="2" BLOCK_SIZE="512" SIZE="15931539456"
/dev/dmsdio0/0p1: TYPE="vfat" VERSION="FAT32" LABEL="BOOT" UUID="CAFE-BABE" SIZE="268435456" MODULE="dmfatfs" MOUNTABLE="yes"
/dev/dmsdio0/0p2: SIZE="15662055424"
```

| Field | Meaning |
|-------|---------|
| `PTTYPE`, `PTUUID`, `PARTITIONS`, `BLOCK_SIZE` | Partition table: `mbr`/`gpt`, disk signature or GUID, number of partitions, block size used |
| `TYPE`, `VERSION`, `LABEL`, `UUID` | Filesystem: `vfat`/`exfat`/`dmffs`, format version, volume label, serial |
| `MODULE`, `MOUNTABLE` | DMOD filesystem module for the type (omitted if there is none), and whether it can mount the node |
| `SIZE` | Node size in bytes |

A node with nothing recognized (blank, damaged, unsupported) only shows its
`SIZE`. A node that cannot be read shows `cannot probe (<errno>)`; the exit
status is then that negative errno.

Nodes are never listed automatically: reading a character device (a UART,
say) as if it was a disk would block or consume its data.
