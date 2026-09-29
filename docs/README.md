# dmblkid Documentation

dmblkid identifies what a block device or partition contains: a partition
table (MBR/GPT) or a filesystem (FAT12/16/32, exFAT, dmffs), with its label,
uuid and the DMOD filesystem module that mounts it.

## Contents

- **[api-reference.md](api-reference.md)** - Complete API documentation

## Quick Reference

```c
#include "dmblkid.h"

dmblkid_t* result = NULL;
if (dmblkid_probe("/dev/dmsdio0/0p1", &result) == 0)
{
    const char* type = dmblkid_get_type(result);    /* "vfat", "exfat", "dmffs", "mbr", "gpt" or NULL */
    dmblkid_destroy(result);
}
```

From the shell: `blkid <node>...` (the `blkid` application module).

View documentation using `dmf-man`:

```bash
dmf-man dmblkid          # Main documentation
dmf-man dmblkid api      # API reference
```
