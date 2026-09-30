#ifndef AUTOMOUNT_H
#define AUTOMOUNT_H

#include <stdbool.h>

/**
 * @file automount.h
 * @brief Mount logic of the automount service (see automount.c)
 *
 * Private to the service: main.c runs it with dmvfs and the file API, the
 * tests with recording fakes. Everything that touches the file system tree
 * goes through automount_ops_t, only the probe (dmblkid) is called
 * directly.
 */

/** How automount reaches the file system tree. All members are required. */
typedef struct
{
    /** Mount @p node with file system module @p module at @p dir: 0 or a negative errno. */
    int  (*mount)(const char* module, const char* dir, const char* node);
    /** Unmount @p dir: 0 or a negative errno. */
    int  (*unmount)(const char* dir);
    /** Whether anything (a file, a directory, a mount) exists at @p path. */
    bool (*exists)(const char* path);
    /** Create directory @p path: 0 or a negative errno. */
    int  (*make_dir)(const char* path);
    /**
     * Lock shared by every automount instance: picking a free directory
     * and mounting there happen under it, so two instances never take
     * the same one.
     */
    void (*lock)(void);
    void (*unlock)(void);
} automount_ops_t;

typedef struct automount automount_t;

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
 * @param ops       Tree access; copied.
 * @param node      Device node, e.g. "/dev/dmsdio0/0p1"
 * @param name      Fallback directory name, e.g. "dmsdio0_0p1"
 * @param base_dir  Directory the volumes are mounted in, e.g. "/mnt"
 * @param error     Receives 0 when there is nothing to mount (partition
 *                  table, unknown contents, file system without a module
 *                  that mounts nodes), otherwise a negative errno value:
 *                  the probe error (e.g. -ENOENT), -EBUSY (no free
 *                  directory), the mount error, or -ENOMEM.
 * @return The mount, or NULL when nothing was mounted.
 */
automount_t* automount_create(const automount_ops_t* ops, const char* node, const char* name,
                              const char* base_dir, int* error);

/**
 * @brief Unmount and release the mount
 *
 * Safe with NULL. Also safe from a process exit callback after the thread
 * of the service was killed.
 */
void automount_destroy(automount_t* mount);

/** @return The mount directory, e.g. "/mnt/BOOT"; NULL for an invalid mount. */
const char* automount_get_dir(const automount_t* mount);

/**
 * @brief Directory name for a volume label
 *
 * @return The label with every character that does not belong in a path or
 *         a shell word (control characters, space, '/', '\\', ';', '|', '&',
 *         '<', '>', quotes, '`', '$', '*', '?') replaced with '_', on the
 *         heap; NULL for no label, a label that is only such characters,
 *         "." or "..", or on allocation failure.
 */
char* automount_label_to_name(const char* label);

#endif // AUTOMOUNT_H
