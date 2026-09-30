#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "dmblkid.h"
#include "libautomount_core.h"
#include "test_images.h"
#include <errno.h>
#include <string.h>

/*
 * The libautomount mount logic (libs/libautomount/src/libautomount_core.c)
 * on image files probed by the real dmblkid, with fakes of the
 * libautomount_os_*() functions standing for a file system tree: every
 * directory created and every mount point is recorded (a mount point exists
 * while mounted, like in dmvfs), so the steps can check where a volume ended
 * up and that nothing is left behind.
 */

#define IMAGE_PATH      "libautomount_test.img"
#define BASE_DIR        "/mnt"
#define NODE_NAME       "dmsdio0_0p1"
#define MAX_PATHS       8
#define MAX_PATH_LENGTH 64

typedef struct
{
    char    dirs[MAX_PATHS][MAX_PATH_LENGTH];     /* existing directories, "" = free slot */
    char    mounted_dir[MAX_PATH_LENGTH];
    char    mounted_module[MAX_PATH_LENGTH];
    char    mounted_node[MAX_PATH_LENGTH];
    int     mount_calls;
    int     unmount_calls;
    int     mount_result;
    int     locked;
} fake_tree_t;

static fake_tree_t g_tree;

static int find_dir(const char* path)
{
    for (int i = 0; i < MAX_PATHS; i++)
    {
        if (strcmp(g_tree.dirs[i], path) == 0)
        {
            return i;
        }
    }
    return -1;
}

static void add_dir(const char* path)
{
    int slot = find_dir("");
    if (slot >= 0)
    {
        strncpy(g_tree.dirs[slot], path, MAX_PATH_LENGTH - 1);
    }
}

static void copy_text(char* target, const char* text)
{
    strncpy(target, text, MAX_PATH_LENGTH - 1);
    target[MAX_PATH_LENGTH - 1] = '\0';
}

int libautomount_os_mount(const char* module, const char* dir, const char* node)
{
    g_tree.mount_calls++;
    if (g_tree.mount_result == 0)
    {
        add_dir(dir);
        copy_text(g_tree.mounted_dir, dir);
        copy_text(g_tree.mounted_module, module);
        copy_text(g_tree.mounted_node, node);
    }
    return g_tree.mount_result;
}

int libautomount_os_unmount(const char* dir)
{
    g_tree.unmount_calls++;
    if (strcmp(g_tree.mounted_dir, dir) != 0)
    {
        return -EINVAL;
    }
    g_tree.dirs[find_dir(dir)][0] = '\0';
    g_tree.mounted_dir[0] = '\0';
    return 0;
}

bool libautomount_os_exists(const char* path)   { return find_dir(path) >= 0; }
int  libautomount_os_make_dir(const char* path) { add_dir(path); return 0; }
void libautomount_os_lock(void)                 { g_tree.locked++; }
void libautomount_os_unlock(void)               { g_tree.locked--; }

static libautomount_t* create(int* error)
{
    return libautomount_core_mount(IMAGE_PATH, NODE_NAME, BASE_DIR, error);
}

void dmod_test_setup(void)
{
    memset(&g_tree, 0, sizeof(g_tree));
}

void dmod_test_teardown(void)
{
    Dmod_FileRemove(IMAGE_PATH);
}

static bool name_is(const char* label, const char* expected)
{
    char* name = libautomount_core_label_to_name(label);
    bool  ok   = (expected == NULL) ? name == NULL : (name != NULL && strcmp(name, expected) == 0);
    if (name != NULL)
    {
        Dmod_Free(name);
    }
    return ok;
}

DMOD_TEST_STEP(label_to_name)
{
    DMOD_TEST_EXPECT_TRUE(name_is("BOOT", "BOOT"));
    DMOD_TEST_EXPECT_TRUE(name_is("My Card", "My_Card"));
    DMOD_TEST_EXPECT_TRUE(name_is("a/b\\c;d", "a_b_c_d"));
    DMOD_TEST_EXPECT_TRUE(name_is("Karta \xC5\x82", "Karta_\xC5\x82"));
    DMOD_TEST_EXPECT_TRUE(name_is("NO.NAME", "NO.NAME"));
    DMOD_TEST_EXPECT_TRUE(name_is(NULL, NULL));
    DMOD_TEST_EXPECT_TRUE(name_is("   ", NULL));
    DMOD_TEST_EXPECT_TRUE(name_is(".", NULL));
    DMOD_TEST_EXPECT_TRUE(name_is("..", NULL));
}

DMOD_TEST_STEP(mounts_at_label)
{
    int error = 1;
    DMOD_TEST_EXPECT_TRUE(test_build_fat32(IMAGE_PATH));
    libautomount_t* mount = create(&error);
    DMOD_TEST_EXPECT_NOT_NULL(mount);
    DMOD_TEST_EXPECT_EQ(error, 0);
    DMOD_TEST_EXPECT_EQ(strcmp(libautomount_core_get_dir(mount), BASE_DIR "/BOOT"), 0);
    DMOD_TEST_EXPECT_EQ(strcmp(g_tree.mounted_dir, BASE_DIR "/BOOT"), 0);
    DMOD_TEST_EXPECT_EQ(strcmp(g_tree.mounted_module, "dmfatfs"), 0);
    DMOD_TEST_EXPECT_EQ(strcmp(g_tree.mounted_node, IMAGE_PATH), 0);
    DMOD_TEST_EXPECT_TRUE(libautomount_os_exists(BASE_DIR));
    DMOD_TEST_EXPECT_TRUE(libautomount_os_exists(BASE_DIR "/BOOT"));
    libautomount_core_unmount(mount);
    DMOD_TEST_EXPECT_EQ(g_tree.unmount_calls, 1);
    DMOD_TEST_EXPECT_EQ(g_tree.mounted_dir[0], '\0');
    DMOD_TEST_EXPECT_FALSE(libautomount_os_exists(BASE_DIR "/BOOT"));
    DMOD_TEST_EXPECT_EQ(g_tree.locked, 0);
}

DMOD_TEST_STEP(utf8_label)
{
    DMOD_TEST_EXPECT_TRUE(test_build_exfat(IMAGE_PATH, true));
    libautomount_t* mount = create(NULL);
    DMOD_TEST_EXPECT_NOT_NULL(mount);
    DMOD_TEST_EXPECT_EQ(strcmp(g_tree.mounted_dir, BASE_DIR "/Karta_\xC5\x82"), 0);
    libautomount_core_unmount(mount);
}

DMOD_TEST_STEP(taken_label_falls_back_to_node_name)
{
    DMOD_TEST_EXPECT_TRUE(test_build_fat32(IMAGE_PATH));
    add_dir(BASE_DIR);
    add_dir(BASE_DIR "/BOOT");
    libautomount_t* mount = create(NULL);
    DMOD_TEST_EXPECT_NOT_NULL(mount);
    DMOD_TEST_EXPECT_EQ(strcmp(libautomount_core_get_dir(mount), BASE_DIR "/" NODE_NAME), 0);
    DMOD_TEST_EXPECT_EQ(strcmp(g_tree.mounted_dir, BASE_DIR "/" NODE_NAME), 0);
    libautomount_core_unmount(mount);
    /* Only its own directory is removed */
    DMOD_TEST_EXPECT_TRUE(libautomount_os_exists(BASE_DIR "/BOOT"));
    DMOD_TEST_EXPECT_FALSE(libautomount_os_exists(BASE_DIR "/" NODE_NAME));
}

DMOD_TEST_STEP(everything_taken)
{
    int error = 0;
    DMOD_TEST_EXPECT_TRUE(test_build_fat32(IMAGE_PATH));
    add_dir(BASE_DIR "/BOOT");
    add_dir(BASE_DIR "/" NODE_NAME);
    DMOD_TEST_EXPECT_NULL(create(&error));
    DMOD_TEST_EXPECT_EQ(error, -EBUSY);
    DMOD_TEST_EXPECT_EQ(g_tree.mount_calls, 0);
    DMOD_TEST_EXPECT_EQ(g_tree.locked, 0);
}

DMOD_TEST_STEP(partition_table_is_left_to_the_partitions)
{
    int error = 1;
    DMOD_TEST_EXPECT_TRUE(test_build_mbr(IMAGE_PATH));
    DMOD_TEST_EXPECT_NULL(create(&error));
    DMOD_TEST_EXPECT_EQ(error, 0);
    DMOD_TEST_EXPECT_EQ(g_tree.mount_calls, 0);
    DMOD_TEST_EXPECT_FALSE(libautomount_os_exists(BASE_DIR));
}

DMOD_TEST_STEP(unknown_contents_are_not_mounted)
{
    int error = 1;
    DMOD_TEST_EXPECT_TRUE(test_image_create(IMAGE_PATH, TEST_MIB));
    DMOD_TEST_EXPECT_NULL(create(&error));
    DMOD_TEST_EXPECT_EQ(error, 0);
    DMOD_TEST_EXPECT_EQ(g_tree.mount_calls, 0);
}

DMOD_TEST_STEP(dmffs_is_not_mounted)
{
    int error = 1;
    DMOD_TEST_EXPECT_TRUE(test_build_dmffs(IMAGE_PATH));
    DMOD_TEST_EXPECT_NULL(create(&error));
    DMOD_TEST_EXPECT_EQ(error, 0);
    DMOD_TEST_EXPECT_EQ(g_tree.mount_calls, 0);
}

DMOD_TEST_STEP(missing_node)
{
    int error = 0;
    DMOD_TEST_EXPECT_NULL(create(&error));
    DMOD_TEST_EXPECT_TRUE(error < 0);
    DMOD_TEST_EXPECT_EQ(g_tree.mount_calls, 0);
}

DMOD_TEST_STEP(mount_failure_cleans_up)
{
    int error = 0;
    DMOD_TEST_EXPECT_TRUE(test_build_fat16(IMAGE_PATH));
    g_tree.mount_result = -EIO;
    DMOD_TEST_EXPECT_NULL(create(&error));
    DMOD_TEST_EXPECT_EQ(error, -EIO);
    /* Not a naming problem - the node name is not tried as well */
    DMOD_TEST_EXPECT_EQ(g_tree.mount_calls, 1);
    DMOD_TEST_EXPECT_FALSE(libautomount_os_exists(BASE_DIR "/DATA16"));
    DMOD_TEST_EXPECT_EQ(g_tree.locked, 0);
}

DMOD_TEST_STEP(invalid_arguments)
{
    int error = 0;
    DMOD_TEST_EXPECT_TRUE(test_build_fat32(IMAGE_PATH));
    DMOD_TEST_EXPECT_NULL(libautomount_core_mount(IMAGE_PATH, "a/b", BASE_DIR, &error));
    DMOD_TEST_EXPECT_EQ(error, -EINVAL);
    DMOD_TEST_EXPECT_NULL(libautomount_core_mount(IMAGE_PATH, "", BASE_DIR, &error));
    DMOD_TEST_EXPECT_NULL(libautomount_core_mount(NULL, NODE_NAME, BASE_DIR, &error));
    DMOD_TEST_EXPECT_NULL(libautomount_core_mount(IMAGE_PATH, NODE_NAME, NULL, &error));
    DMOD_TEST_EXPECT_EQ(g_tree.mount_calls, 0);
    libautomount_core_unmount(NULL);
    DMOD_TEST_EXPECT_NULL(libautomount_core_get_dir(NULL));
}
