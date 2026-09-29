#include "dmblkid_internal.h"
#include "dmdrvi_ioctl.h"
#include <errno.h>

/*
 * The node is read through the regular file API (Dmod_File*, 64-bit
 * offsets) - on target that is dmvfs, so dmdevfs block and partition nodes
 * work the same as image files on the host. It is only ever opened
 * read-only.
 */

/* Block nodes know their geometry; image files (and other nodes) fall back to the file size. */
static void read_geometry(dmblkid_source_t* source)
{
    dmdrvi_block_info_t info = { 0 };
    if (Dmod_Ioctl(source->file, DMDRVI_IOCTL_BLOCK_GET_INFO, &info) == 0 &&
        dmblkid_is_power_of_2(info.logical_block_size) && info.block_count != 0 &&
        info.block_count <= UINT64_MAX / info.logical_block_size)
    {
        source->block_size = info.logical_block_size;
        source->size       = (uint64_t)info.block_count * info.logical_block_size;
        return;
    }
    source->block_size = 0;
    source->size       = Dmod_FileSize(source->file);
}

int dmblkid_source_open(dmblkid_source_t* source, const char* path)
{
    source->file = Dmod_FileOpen(path, "rb");
    if (source->file == NULL)
    {
        return -ENOENT;
    }
    read_geometry(source);
    return 0;
}

void dmblkid_source_close(dmblkid_source_t* source)
{
    if (source->file != NULL)
    {
        Dmod_FileClose(source->file);
        source->file = NULL;
    }
}

int dmblkid_source_read(const dmblkid_source_t* source, uint64_t offset, void* buffer, size_t size)
{
    if (source->size != 0 && (offset > source->size || size > source->size - offset))
    {
        return -ERANGE;
    }
    if (offset > (uint64_t)INT64_MAX || Dmod_FileSeek(source->file, (Dmod_FileOffset_t)offset, DMOD_SEEK_SET) != 0)
    {
        return -EIO;
    }
    return (Dmod_FileRead(buffer, 1, size, source->file) == size) ? 0 : -EIO;
}
