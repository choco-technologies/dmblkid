/**
 * @file automount.c
 * @brief Mount logic of the automount service
 */
#include "dmod.h"
#include "dmblkid.h"
#include "automount.h"
#include <errno.h>
#include <string.h>

#define AUTOMOUNT_MAGIC     0x41554D54u     /* 'AUMT' */

struct automount
{
    uint32_t        magic;
    automount_ops_t ops;
    char*           dir;        /**< Mount directory, owned */
};

/* What was found on the node: the module to mount it with and its label. */
typedef struct
{
    char*   module;
    char*   label;      /**< NULL if the volume has none */
} volume_t;

static bool is_valid(const automount_t* mount)
{
    return mount != NULL && mount->magic == AUTOMOUNT_MAGIC;
}

static bool is_unsafe(unsigned char c)
{
    static const char unsafe[] = " /\\;|&<>\"'`$*?";
    return c < 0x20u || c == 0x7Fu || strchr(unsafe, c) != NULL;
}

char* automount_label_to_name(const char* label)
{
    if (label == NULL || strcmp(label, ".") == 0 || strcmp(label, "..") == 0)
    {
        return NULL;
    }
    char* name = Dmod_StrDup(label);
    bool  kept = false;
    for (char* c = name; c != NULL && *c != '\0'; c++)
    {
        bool unsafe = is_unsafe((unsigned char)*c);
        *c   = unsafe ? '_' : *c;
        kept = kept || !unsafe;
    }
    if (name != NULL && !kept)
    {
        Dmod_Free(name);
        name = NULL;
    }
    return name;
}

const char* automount_get_dir(const automount_t* mount)
{
    return is_valid(mount) ? mount->dir : NULL;
}

static void free_volume(volume_t* volume)
{
    if (volume->module != NULL)
    {
        Dmod_Free(volume->module);
    }
    if (volume->label != NULL)
    {
        Dmod_Free(volume->label);
    }
}

/* 1: mountable volume found, 0: nothing to mount, negative errno: failure. */
static int probe_volume(const char* node, volume_t* volume)
{
    dmblkid_t* result = NULL;
    int ret = dmblkid_probe(node, &result);
    if (ret != 0)
    {
        return ret;
    }
    ret = 0;
    if (dmblkid_is_mountable(result))
    {
        const char* label = dmblkid_get_label(result);
        volume->module = Dmod_StrDup(dmblkid_get_module(result));
        volume->label  = (label != NULL) ? automount_label_to_name(label) : NULL;
        ret = (volume->module != NULL && (label == NULL || volume->label != NULL)) ? 1 : -ENOMEM;
    }
    else
    {
        DMOD_LOG_INFO("automount: nothing to mount on %s (%s)\n", node,
                      (dmblkid_get_type(result) != NULL) ? dmblkid_get_type(result) : "unknown contents");
    }
    dmblkid_destroy(result);
    return ret;
}

/* "<base_dir>/<name>" on the heap */
static char* join_path(const char* base_dir, const char* name)
{
    size_t base_length = strlen(base_dir);
    char*  path        = Dmod_Malloc(base_length + strlen(name) + 2);
    if (path != NULL)
    {
        memcpy(path, base_dir, base_length);
        path[base_length] = '/';
        strcpy(&path[base_length + 1], name);
    }
    return path;
}

/* Mount at <base_dir>/<name> - or -EBUSY if it is taken. Under the lock. */
static int mount_at(const automount_ops_t* ops, const volume_t* volume, const char* node,
                    const char* base_dir, const char* name, char** dir)
{
    *dir = join_path(base_dir, name);
    if (*dir == NULL)
    {
        return -ENOMEM;
    }
    int ret = ops->exists(*dir) ? -EBUSY : ops->mount(volume->module, *dir, node);
    if (ret != 0)
    {
        Dmod_Free(*dir);
        *dir = NULL;
    }
    return ret;
}

/* The label directory first, then the node name if the label is missing or taken. */
static int mount_volume(const automount_ops_t* ops, const volume_t* volume, const char* node,
                        const char* name, const char* base_dir, char** dir)
{
    ops->lock();
    int ret = ops->exists(base_dir) ? 0 : ops->make_dir(base_dir);
    if (ret == 0)
    {
        ret = (volume->label != NULL) ? mount_at(ops, volume, node, base_dir, volume->label, dir) : -EBUSY;
        if (ret == -EBUSY)
        {
            ret = mount_at(ops, volume, node, base_dir, name, dir);
        }
    }
    ops->unlock();
    return ret;
}

static bool valid_arguments(const automount_ops_t* ops, const char* node, const char* name, const char* base_dir)
{
    return ops != NULL && ops->mount != NULL && ops->unmount != NULL && ops->exists != NULL &&
           ops->make_dir != NULL && ops->lock != NULL && ops->unlock != NULL &&
           node != NULL && name != NULL && base_dir != NULL && name[0] != '\0' && strchr(name, '/') == NULL;
}

static automount_t* create_mount(const automount_ops_t* ops, char* dir)
{
    automount_t* mount = Dmod_Malloc(sizeof(automount_t));
    if (mount == NULL)
    {
        return NULL;
    }
    mount->magic = AUTOMOUNT_MAGIC;
    mount->ops   = *ops;
    mount->dir   = dir;
    return mount;
}

static void release_dir(const automount_ops_t* ops, char* dir)
{
    ops->lock();
    int ret = ops->unmount(dir);
    ops->unlock();
    if (ret != 0)
    {
        DMOD_LOG_ERROR("automount: cannot unmount %s (%d)\n", dir, ret);
    }
    Dmod_Free(dir);
}

automount_t* automount_create(const automount_ops_t* ops, const char* node, const char* name,
                              const char* base_dir, int* error)
{
    int      unused;
    int*     status = (error != NULL) ? error : &unused;
    volume_t volume = { 0 };
    char*    dir    = NULL;
    if (!valid_arguments(ops, node, name, base_dir))
    {
        *status = -EINVAL;
        return NULL;
    }
    *status = probe_volume(node, &volume);
    if (*status == 1)
    {
        *status = mount_volume(ops, &volume, node, name, base_dir, &dir);
        if (*status == 0)
        {
            DMOD_LOG_INFO("automount: %s mounted at %s (%s)\n", node, dir, volume.module);
        }
    }
    free_volume(&volume);
    automount_t* mount = (dir != NULL) ? create_mount(ops, dir) : NULL;
    if (dir != NULL && mount == NULL)
    {
        release_dir(ops, dir);
        *status = -ENOMEM;
    }
    return mount;
}

void automount_destroy(automount_t* mount)
{
    if (!is_valid(mount))
    {
        return;
    }
    mount->magic = 0;
    DMOD_LOG_INFO("automount: unmounting %s\n", mount->dir);
    release_dir(&mount->ops, mount->dir);
    Dmod_Free(mount);
}
