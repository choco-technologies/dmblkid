#ifndef LIBAUTOMOUNT_H
#define LIBAUTOMOUNT_H

#include <stdbool.h>
#include "dmod_types.h"
#include "libautomount_defs.h"

/**
 * libautomount - mounting block nodes as they appear.
 *
 * Everything the automount service does, as a library: a library module is
 * loaded once, while the service runs once per block node - so the service
 * itself is only a main() calling libautomount_run().
 *
 * A node holding a file system that a module can mount from a node (FAT and
 * exFAT through dmfatfs, as dmblkid reports it) is mounted at
 * <base dir>/<volume label>, or <base dir>/<name> when the volume has no
 * label or that path is taken. See README.md.
 */

typedef struct libautomount libautomount_t;

/**
 * Mount @p node, wait until the calling process is asked to stop through
 * libsystemd (libsystemd_stop_service(): node removed, "service stop"),
 * unmount. If the process is killed instead, an exit callback unmounts.
 *
 * Meant to be the whole main() of a service started per node.
 *
 * @param node      Device node, e.g. "/dev/dmsdio0/0p1"
 * @param name      Directory name used when the label cannot be, e.g. "dmsdio0_0p1"
 * @param base_dir  Directory the volumes are mounted in, NULL for "/mnt"
 * @return 0 - also when there is nothing to mount, or it cannot be mounted
 *         (logged; a restart would not change that) - or -ENOMEM.
 */
dmod_libautomount_api(1.0, int, _run, ( const char* node, const char* name, const char* base_dir ));

/**
 * Mount @p node if it holds a mountable file system (see above), without
 * waiting for anything.
 *
 * @param base_dir  NULL for "/mnt"
 * @param error     Receives 0 when there is nothing to mount, otherwise a
 *                  negative errno value (probe error, -EBUSY: no free
 *                  directory, mount error, -EINVAL, -ENOMEM). May be NULL.
 * @return The mount, or NULL when nothing was mounted.
 */
dmod_libautomount_api(1.0, libautomount_t*, _mount, ( const char* node, const char* name, const char* base_dir, int* error ));

/** Unmount a mount made by libautomount_mount() and release it. Safe with NULL. */
dmod_libautomount_api(1.0, void, _unmount, ( libautomount_t* mount ));

/** @return The directory the volume is mounted at, e.g. "/mnt/BOOT"; NULL for an invalid mount. */
dmod_libautomount_api(1.0, const char*, _get_dir, ( const libautomount_t* mount ));

#endif // LIBAUTOMOUNT_H
