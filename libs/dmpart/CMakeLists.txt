# =====================================================================
#               dmpart - partition table parser
# =====================================================================
# MBR/GPT parser shared by dmblkid (partition table detection) and dmdevfs
# (partition nodes) - see README.md. Built and released with dmblkid.
set(DMOD_MODULE_NAME        dmpart)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         1024)
set(DMOD_DMR_PATH           ${CMAKE_CURRENT_SOURCE_DIR}/dmpart.dmr)

dmod_add_library(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    src/dmpart.c
)

target_include_directories(${DMOD_MODULE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(${DMOD_MODULE_NAME}_if INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_subdirectory(tests)
