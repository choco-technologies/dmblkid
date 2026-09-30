/**
 * @file libautomount.c
 * @brief libautomount API: the service loop and the mount/unmount wrappers
 *
 * The mount logic is in libautomount_core.c, the dmvfs access in
 * libautomount_os.c. This is the only translation unit that emits the
 * library's API registrations (libautomount.h).
 */
#define DMOD_ENABLE_REGISTRATION    ON
#include "dmod.h"
#include "dmosi.h"
#include "libsystemd.h"
#include "libautomount.h"
#include "libautomount_core.h"
#include <errno.h>

#define DEFAULT_BASE_DIR    "/mnt"

int dmod_init(const Dmod_Config_t* Config)
{
    (void)Config;
    if (!libautomount_os_init())
    {
        DMOD_LOG_ERROR("libautomount: cannot create the lock\n");
        return -ENOMEM;
    }
    return 0;
}

int dmod_deinit(void)
{
    libautomount_os_deinit();
    return 0;
}

dmod_libautomount_api_declaration(1.0, libautomount_t*, _mount, ( const char* node, const char* name, const char* base_dir, int* error ))
{
    return libautomount_core_mount(node, name, (base_dir != NULL) ? base_dir : DEFAULT_BASE_DIR, error);
}

dmod_libautomount_api_declaration(1.0, void, _unmount, ( libautomount_t* mount ))
{
    libautomount_core_unmount(mount);
}

dmod_libautomount_api_declaration(1.0, const char*, _get_dir, ( const libautomount_t* mount ))
{
    return libautomount_core_get_dir(mount);
}

/* Only reached when the service had to be killed: libautomount_run() never returned. */
static void on_killed(dmosi_process_t process, int exit_status, void* arg)
{
    (void)process;
    (void)exit_status;
    libautomount_core_unmount((libautomount_t*)arg);
}

/* Sleep until libsystemd asks the calling process to stop. */
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
        DMOD_LOG_ERROR("libautomount: cannot register for stop requests (%d)\n", ret);
    }
    dmosi_semaphore_destroy(wakeup);
    return ret;
}

static int wait_while_mounted(libautomount_t* mount)
{
    dmosi_process_t self = dmosi_process_current();
    dmosi_process_exit_callback_handle_t killed =
        (self != NULL) ? dmosi_process_register_exit_callback(self, on_killed, mount) : NULL;
    if (killed == NULL)
    {
        DMOD_LOG_ERROR("libautomount: cannot register exit callback\n");
        return -ENOMEM;
    }
    int ret = wait_for_stop();
    dmosi_process_unregister_exit_callback(self, killed);
    return ret;
}

dmod_libautomount_api_declaration(1.0, int, _run, ( const char* node, const char* name, const char* base_dir ))
{
    int error = 0;
    libautomount_t* mount = libautomount_core_mount(node, name, (base_dir != NULL) ? base_dir : DEFAULT_BASE_DIR, &error);
    if (mount == NULL)
    {
        /* Nothing to mount, or it cannot be mounted: a restart would not change that. */
        return (error == -ENOMEM) ? error : 0;
    }
    int ret = wait_while_mounted(mount);
    libautomount_core_unmount(mount);
    return ret;
}
