# =====================================================================
#               libautomount - mounting block nodes as they appear
# =====================================================================
# Everything the automount service (services/automount) does, as a library:
# loaded once, while the service runs once per block node - see README.md.
# Built and released with dmblkid.
set(DMOD_MODULE_NAME        libautomount)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         1024)
set(DMOD_DMR_PATH           ${CMAKE_CURRENT_SOURCE_DIR}/libautomount.dmr)

dmod_add_library(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    src/libautomount.c
    src/libautomount_core.c
    src/libautomount_os.c
)

target_include_directories(${DMOD_MODULE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_include_directories(${DMOD_MODULE_NAME}_if INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# dmblkid - local sibling (repository root): headers from its _if target
target_link_libraries(${DMOD_MODULE_NAME} dmblkid_if)

dmod_link_modules(${DMOD_MODULE_NAME}
    dmosi
    libsystemd
)
