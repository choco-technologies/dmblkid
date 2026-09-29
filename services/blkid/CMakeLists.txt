# =====================================================================
#               blkid - command line front end of dmblkid
# =====================================================================
# Prints what dmblkid_probe() finds on the nodes given as arguments.
set(DMOD_MODULE_NAME        blkid)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         2048)
set(DMOD_PRIORITY           0)
set(DMOD_DMR_PATH           ${CMAKE_CURRENT_SOURCE_DIR}/blkid.dmr)

dmod_add_executable(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    src/main.c
)

target_link_libraries(${DMOD_MODULE_NAME} dmblkid_if)
