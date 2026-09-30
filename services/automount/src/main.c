#include "dmod.h"
#include "dmosi.h"
#include "libsystemd.h"
#include "automount.h"
#include <errno.h>

/*
 * automount <node> <name> [<base dir>] - mount service for one block node.
 *
 * dmdevfs reports every block node (whole media and partitions) to
 * libsystemd as class "block"; the [class=block] rule in
 * configs/automount.rules starts automount@<name> from
 * configs/automount@.ini with the node path and the node name. The service
 * mounts the node if it holds a mountable file system, waits until it is
 * stopped (the node went away, or "service stop"), then unmounts it. The
 * mount logic is in automount.c.
 */

#define DEFAULT_BASE_DIR    "/mnt"
#define DIR_MODE            0755

/*
 * dmvfs is built into the firmware (it is not a module), its API is
 * resolved from the system like the rest of the built-in API.
 */
DMOD_BUILTIN_API( dmvfs, 1.0, bool, _mount_fs, (const char* fs_name, const char* mount_point, const char* config) );
DMOD_BUILTIN_API( dmvfs, 1.0, bool, _unmount_fs, (const char* mount_point) );

/* Shared by every running instance - they all run this one loaded module. */
static void* g_lock;

int dmod_init(const Dmod_Config_t* Config)
{
    (void)Config;
    g_lock = Dmod_Mutex_New(false);
    return (g_lock != NULL) ? 0 : -ENOMEM;
}

int dmod_deinit(void)
{
    if (g_lock != NULL)
    {
        Dmod_Mutex_Delete(g_lock);
        g_lock = NULL;
    }
    return 0;
}

static int  op_mount(const char* module, const char* dir, const char* node) { return dmvfs_mount_fs(module, dir, node) ? 0 : -EIO; }
static int  op_unmount(const char* dir)     { return dmvfs_unmount_fs(dir) ? 0 : -EIO; }
static bool op_exists(const char* path)     { return Dmod_Access(path, 0) == 0; }
static int  op_make_dir(const char* path)   { return (Dmod_MakeDir(path, DIR_MODE) == 0) ? 0 : -EIO; }
static void op_lock(void)                   { Dmod_Mutex_Lock(g_lock); }
static void op_unlock(void)                 { Dmod_Mutex_Unlock(g_lock); }

/* Only reached when the unit had to be killed: main() never returned. */
static void on_killed(dmosi_process_t process, int exit_status, void* arg)
{
    (void)process;
    (void)exit_status;
    automount_destroy((automount_t*)arg);
}

static void print_usage(const char* prog)
{
    Dmod_Printf("Usage: %s <node> <name> [<base dir>]\n", prog);
    Dmod_Printf("\n");
    Dmod_Printf("Mount service for one block node (e.g. /dev/dmsdio0/0p1): mounts the file\n");
    Dmod_Printf("system on it at <base dir>/<label> (<base dir>/<name> if the volume has no\n");
    Dmod_Printf("label or it is taken; <base dir> defaults to " DEFAULT_BASE_DIR ") until stopped.\n");
    Dmod_Printf("Normally started by libsystemd from automount@.ini for every node dmdevfs\n");
    Dmod_Printf("reports as \"block\".\n");
}

/* Sleep until libsystemd asks the service to stop. */
static int wait_for_stop(void)
{
    dmosi_semaphore_t wakeup = dmosi_semaphore_create(0, 1);
    if (wakeup == NULL)
    {
        return -ENOMEM;
    }
    int ret = libsystemd_set_stop_semaphore(wakeup);
    if (ret == 0)
    {
        while (!libsystemd_stop_requested())
        {
            dmosi_semaphore_wait(wakeup, 1, -1);
        }
        libsystemd_set_stop_semaphore(NULL);
    }
    else
    {
        DMOD_LOG_ERROR("automount: cannot register for stop requests (%d)\n", ret);
    }
    dmosi_semaphore_destroy(wakeup);
    return ret;
}

static int run(automount_t* mount)
{
    dmosi_process_t self = dmosi_process_current();
    dmosi_process_exit_callback_handle_t killed =
        (self != NULL) ? dmosi_process_register_exit_callback(self, on_killed, mount) : NULL;
    if (killed == NULL)
    {
        DMOD_LOG_ERROR("automount: cannot register exit callback\n");
        return -ENOMEM;
    }
    int ret = wait_for_stop();
    dmosi_process_unregister_exit_callback(self, killed);
    return ret;
}

int main(int argc, char* argv[])
{
    if (argc < 3 || argv[1][0] == '\0' || argv[2][0] == '\0')
    {
        print_usage(argv[0]);
        return -EINVAL;
    }
    /* Filled at run time: a constant initializer could become a pointer table in data, which the loader does not relocate. */
    automount_ops_t ops;
    ops.mount      = op_mount;
    ops.unmount    = op_unmount;
    ops.exists     = op_exists;
    ops.make_dir   = op_make_dir;
    ops.lock       = op_lock;
    ops.unlock     = op_unlock;
    int error = 0;
    automount_t* mount = automount_create(&ops, argv[1], argv[2], (argc > 3) ? argv[3] : DEFAULT_BASE_DIR, &error);
    if (mount == NULL)
    {
        /* Nothing to mount, or it cannot be mounted: a restart would not change that. */
        return (error == -ENOMEM) ? error : 0;
    }
    int ret = run(mount);
    automount_destroy(mount);
    return ret;
}
