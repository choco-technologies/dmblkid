#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "dmblkid.h"

static dmblkid_t g_handle = NULL;

void dmod_test_setup(void)
{
    g_handle = dmblkid_create();
}

void dmod_test_teardown(void)
{
    dmblkid_destroy(g_handle);
    g_handle = NULL;
}

DMOD_TEST_STEP(dmblkid_create)
{
    DMOD_TEST_EXPECT_NOT_NULL(g_handle);
}

DMOD_TEST_STEP(dmblkid_is_valid)
{
    DMOD_TEST_EXPECT_TRUE(dmblkid_is_valid(g_handle));
}

DMOD_TEST_STEP(dmblkid_destroy_null)
{
    /* Destroying NULL must not crash. */
    dmblkid_destroy(NULL);
}
