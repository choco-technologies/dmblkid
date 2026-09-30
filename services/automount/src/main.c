#include "dmod.h"
#include "libautomount.h"
#include <errno.h>

/*
 * automount <node> <name> [<base dir>] - mount service for one block node,
 * started per node by libsystemd (see ../README.md). All the work is done by
 * libautomount: a library is loaded once, this service runs once per node.
 */
int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        Dmod_Printf("Usage: %s <node> <name> [<base dir>]\n", argv[0]);
        return -EINVAL;
    }
    return libautomount_run(argv[1], argv[2], (argc > 3) ? argv[3] : NULL);
}
