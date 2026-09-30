# libautomount

Mounting block nodes as they appear - everything the
[automount](../../services/automount/README.md) service does, as a library.

A library module is loaded once, while the service runs once per block node
(one instance per partition of every card). Keeping the logic here means it
is in memory once, however many volumes are mounted: the service itself is
only a `main()` calling `libautomount_run()`.

## API

```c
#include "libautomount.h"

/* The whole service: mount, wait for a libsystemd stop, unmount. */
int libautomount_run(const char* node, const char* name, const char* base_dir);

/* The same without waiting. */
libautomount_t* libautomount_mount(const char* node, const char* name, const char* base_dir, int* error);
void            libautomount_unmount(libautomount_t* mount);
const char*     libautomount_get_dir(const libautomount_t* mount);
```

- `node` - device node, e.g. `/dev/dmsdio0/0p1`
- `name` - directory used when the volume label cannot be, e.g. `dmsdio0_0p1`
- `base_dir` - directory volumes are mounted in, `NULL` for `/mnt`

The node is probed with dmblkid. A file system that a module can mount from
a node (`dmblkid_is_mountable()`; FAT and exFAT through dmfatfs) is mounted
with that module at `<base_dir>/<label>`, or `<base_dir>/<name>` when the
volume has no label or that path is taken. In the label, characters that do
not belong in a path or a shell word become `_`. Anything else - a partition
table, unknown contents, dmffs - is left alone (`error` 0, nothing mounted).

Picking the path and mounting happen under one library-wide lock, so two
volumes never end up at the same path. Only `base_dir` is created: dmvfs
needs no directory under a mount point.

`libautomount_run()` returns 0 also when there is nothing to mount or the
volume cannot be mounted (logged), so a service restart policy never spins
on it; only `-ENOMEM` is an error. While mounted it sleeps on a semaphore
registered with `libsystemd_set_stop_semaphore()`; if the process is killed
instead of stopped, a process exit callback unmounts.

## Structure

| File | Contents |
|------|----------|
| `src/libautomount.c` | API, `libautomount_run()` (libsystemd stop, exit callback) |
| `src/libautomount_core.c` | Probe, label, path choice, mount/unmount |
| `src/libautomount_os.c` | dmvfs and file API access, the lock |

The host tests (`tests/libautomount_test.c` in the repository root) compile
`libautomount_core.c` with fakes of the `libautomount_os_*()` functions -
there is no dmvfs on the host loader.
