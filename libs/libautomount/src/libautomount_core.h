#ifndef LIBAUTOMOUNT_CORE_H
#define LIBAUTOMOUNT_CORE_H

#include <stdbool.h>

/**
 * @file libautomount_core.h
 * @brief Mount logic of libautomount (see libautomount_core.c)
 *
 * Private to the library. Everything that touches the file system tree goes
 * through the libautomount_os_*() functions: libautomount_os.c implements
 * them with dmvfs on target, the host tests (no dmvfs there) link their own
 * fakes instead. Only the probe (dmblkid) is called directly.
 */

typedef struct libautomount libautomount_t;

/* ---- tree access (libautomount_os.c, or the tests) ---- */

/** Create / destroy the library-wide lock (target only, from dmod_init()/dmod_deinit()). */
bool libautomount_os_init(void);
void libautomount_os_deinit(void);

/** Mount @p node with file system module @p module at @p dir: 0 or a negative errno. */
int  libautomount_os_mount(const char* module, const char* dir, const char* node);

/** Unmount @p dir: 0 or a negative errno. */
int  libautomount_os_unmount(const char* dir);

/** Whether anything (a file, a directory, a mount) exists at @p path. */
bool libautomount_os_exists(const char* path);

/** Create directory @p path: 0 or a negative errno. */
int  libautomount_os_make_dir(const char* path);

/**
 * Library-wide lock: picking a free directory and mounting there happen
 * under it, so two services never take the same one.
 */
void libautomount_os_lock(void);
void libautomount_os_unlock(void);

/* ---- mount logic (libautomount_core.c) ---- */

/**
 * @brief Mount @p node if it holds a file system a module can mount
 *
 * Probes the node with dmblkid. For a mountable file system the mount
 * directory is <base_dir>/<volume label>, or <base_dir>/<name> when the
 * volume has no label or <base_dir>/<label> is already taken (by another
 * volume with the same label, or anything else). Characters that do not
 * belong in a path or a shell word are replaced with '_' in the label.
 * Only <base_dir> is created if missing: dmvfs needs no directory under a
 * mount point, so nothing is left behind once it is unmounted.
 *
 * @param node      Device node, e.g. "/dev/dmsdio0/0p1"
 * @param name      Fallback directory name, e.g. "dmsdio0_0p1"
 * @param base_dir  Directory the volumes are mounted in, e.g. "/mnt"
 * @param error     Receives 0 when there is nothing to mount (partition
 *                  table, unknown contents, file system without a module
 *                  that mounts nodes), otherwise a negative errno value:
 *                  the probe error (e.g. -ENOENT), -EBUSY (no free
 *                  directory), the mount error, -EINVAL or -ENOMEM.
 * @return The mount, or NULL when nothing was mounted.
 */
libautomount_t* libautomount_core_mount(const char* node, const char* name, const char* base_dir, int* error);

/**
 * @brief Unmount and release the mount
 *
 * Safe with NULL. Also safe from a process exit callback after the thread
 * of the service was killed.
 */
void libautomount_core_unmount(libautomount_t* mount);

/** @return The mount directory, e.g. "/mnt/BOOT"; NULL for an invalid mount. */
const char* libautomount_core_get_dir(const libautomount_t* mount);

/**
 * @brief Directory name for a volume label
 *
 * @return The label with every character that does not belong in a path or
 *         a shell word (control characters, space, '/', '\\', ';', '|', '&',
 *         '<', '>', quotes, '`', '$', '*', '?') replaced with '_', on the
 *         heap; NULL for no label, a label that is only such characters,
 *         "." or "..", or on allocation failure.
 */
char* libautomount_core_label_to_name(const char* label);

#endif // LIBAUTOMOUNT_CORE_H
