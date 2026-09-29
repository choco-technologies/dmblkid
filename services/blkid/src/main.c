#include "dmod.h"
#include "dmblkid.h"
#include <errno.h>

/*
 * blkid <node>... - print what dmblkid finds on each node, one line per node:
 *
 *   /dev/dmsdio0/0: PTTYPE="mbr" PTUUID="1a2b3c4d" PARTITIONS="2" SIZE="15931539456"
 *   /dev/dmsdio0/0p1: TYPE="vfat" VERSION="FAT32" LABEL="BOOT" UUID="1234-ABCD" SIZE="268435456" MOUNTABLE="no"
 *   /dev/dmsdio0/0p2: SIZE="15662055424"
 *
 * Nodes are never listed automatically: reading a character device (a UART)
 * as if it was a disk would block or consume its data.
 */

static void print_usage(const char* prog)
{
    Dmod_Printf("Usage: %s <node>...\n", prog);
    Dmod_Printf("\n");
    Dmod_Printf("Identify the partition table or filesystem on block device nodes\n");
    Dmod_Printf("(e.g. /dev/dmsdio0/0, /dev/dmsdio0/0p1) or image files.\n");
}

/* Dmod_Printf does not necessarily support 64-bit conversions. */
static void print_u64(const char* name, uint64_t value)
{
    char text[21];
    size_t length = sizeof(text) - 1u;
    text[length] = '\0';
    do
    {
        text[--length] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0);
    Dmod_Printf(" %s=\"%s\"", name, text + length);
}

static void print_string(const char* name, const char* value)
{
    if (value != NULL)
    {
        Dmod_Printf(" %s=\"%s\"", name, value);
    }
}

static void print_filesystem(const dmblkid_t* result)
{
    print_string("TYPE", dmblkid_get_type(result));
    print_string("VERSION", dmblkid_get_version(result));
    print_string("LABEL", dmblkid_get_label(result));
    print_string("UUID", dmblkid_get_uuid(result));
    print_u64("SIZE", dmblkid_get_size(result));
    print_string("MODULE", dmblkid_get_module(result));
    print_string("MOUNTABLE", dmblkid_is_mountable(result) ? "yes" : "no");
}

static void print_partition_table(const dmblkid_t* result)
{
    print_string("PTTYPE", dmblkid_get_type(result));
    print_string("PTUUID", dmblkid_get_uuid(result));
    print_u64("PARTITIONS", dmblkid_get_partition_count(result));
    print_u64("BLOCK_SIZE", dmblkid_get_block_size(result));
    print_u64("SIZE", dmblkid_get_size(result));
}

static int probe_node(const char* path)
{
    dmblkid_t* result = NULL;
    int ret = dmblkid_probe(path, &result);
    if (ret != 0)
    {
        Dmod_Printf("%s: cannot probe (%d)\n", path, ret);
        return ret;
    }
    Dmod_Printf("%s:", path);
    switch (dmblkid_get_usage(result))
    {
        case dmblkid_usage_filesystem:      print_filesystem(result);                       break;
        case dmblkid_usage_partition_table: print_partition_table(result);                  break;
        default:                            print_u64("SIZE", dmblkid_get_size(result));    break;
    }
    Dmod_Printf("\n");
    dmblkid_destroy(result);
    return 0;
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        print_usage(argv[0]);
        return -EINVAL;
    }
    int status = 0;
    for (int i = 1; i < argc; i++)
    {
        int ret = probe_node(argv[i]);
        status = (status == 0) ? ret : status;
    }
    return status;
}
