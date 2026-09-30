/**
 * @file libautomount_os.c
 * @brief File system tree access of libautomount on target (dmvfs)
 *
 * The host tests replace this file with fakes (there is no dmvfs there).
 */
#include "dmod.h"
#include "dmvfs.h"
#include "libautomount_core.h"
#include <errno.h>

#define DIR_MODE    0755

/* One per library - shared by every automount service using it. */
static void* g_lock;

bool libautomount_os_init(void)
{
    g_lock = Dmod_Mutex_New(false);
    return g_lock != NULL;
}

void libautomount_os_deinit(void)
{
    if (g_lock != NULL)
    {
        Dmod_Mutex_Delete(g_lock);
        g_lock = NULL;
    }
}

int libautomount_os_mount(const char* module, const char* dir, const char* node)
{
    return dmvfs_mount_fs(module, dir, node) ? 0 : -EIO;
}

int libautomount_os_unmount(const char* dir)
{
    return dmvfs_unmount_fs(dir) ? 0 : -EIO;
}

bool libautomount_os_exists(const char* path)
{
    return Dmod_Access(path, 0) == 0;
}

int libautomount_os_make_dir(const char* path)
{
    return (Dmod_MakeDir(path, DIR_MODE) == 0) ? 0 : -EIO;
}

void libautomount_os_lock(void)
{
    Dmod_Mutex_Lock(g_lock);
}

void libautomount_os_unlock(void)
{
    Dmod_Mutex_Unlock(g_lock);
}
