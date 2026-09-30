# =====================================================================
#               automount - mount service for block nodes
# =====================================================================
# One instance per block node dmdevfs reports, started by libsystemd - see
# README.md. Only a main(): the work is done by libautomount, which is
# loaded once for all instances.
set(DMOD_MODULE_NAME        automount)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
# Measured on STM32F746G-DISCO (ps STACK column): the peak is 1652 B of the
# 2048 B thread stack (1536 + DMOSI_THREAD_STACK_OVERHEAD), reached while
# the card is read (dmblkid/dmfatfs -> dmvfs -> dmdevfs -> dmsdio).
set(DMOD_STACK_SIZE         1536)
set(DMOD_PRIORITY           0)
set(DMOD_DMR_PATH           ${CMAKE_CURRENT_SOURCE_DIR}/automount.dmr)

dmod_add_executable(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    src/main.c
)

target_link_libraries(${DMOD_MODULE_NAME} libautomount_if)
