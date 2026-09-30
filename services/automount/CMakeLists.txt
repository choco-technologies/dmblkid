# =====================================================================
#               automount - mount service for block nodes
# =====================================================================
# Mounts every block node dmdevfs reports that holds a mountable file
# system, started per node by libsystemd - see README.md.
set(DMOD_MODULE_NAME        automount)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         2048)
set(DMOD_PRIORITY           0)
set(DMOD_DMR_PATH           ${CMAKE_CURRENT_SOURCE_DIR}/automount.dmr)

dmod_add_executable(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    src/main.c
    src/automount.c
)

target_include_directories(${DMOD_MODULE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${DMOD_MODULE_NAME} dmblkid_if)

dmod_link_modules(${DMOD_MODULE_NAME}
    dmosi
    libsystemd
)
