set(DMOD_MODULE_NAME        test_dmblkid)
set(DMOD_MODULE_VERSION     ${PROJECT_VERSION})
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         4096)
set(DMOD_PRIORITY           0)

dmod_add_test(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    dmblkid_test.c
    test_images.c
)

target_include_directories(${DMOD_MODULE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(${DMOD_MODULE_NAME} dmblkid_if)

# Register a test module with ctest - this replays the same two steps CI
# runs (see .github/workflows/ci.yml): install the test module's
# dependencies with dmf-get, then execute it through dmod_loader. The test's
# exit code equals its number of failed steps (see dmod_add_test).
find_program(DMF_GET_EXECUTABLE dmf-get HINTS ${DMOD_TOOLS_BIN_DIR} PATHS ENV PATH)
find_program(DMOD_LOADER_EXECUTABLE dmod_loader HINTS ${DMOD_TOOLS_BIN_DIR} PATHS ENV PATH)

function(dmblkid_register_test name)
    if(DMF_GET_EXECUTABLE AND DMOD_LOADER_EXECUTABLE)
        add_test(
            NAME ${name}
            COMMAND ${CMAKE_COMMAND} -E env DMOD_DMF_DIR=${DMOD_DMF_DIR}
                bash -c "${DMF_GET_EXECUTABLE} install -d ${DMOD_DMF_DIR}/${name}-local.dmd -y && ${DMOD_LOADER_EXECUTABLE} ${DMOD_DMF_DIR}/${name}.dmf"
        )
    else()
        message(WARNING "dmf-get/dmod_loader not found - 'ctest' will not run ${name}. Build the tools first with -DDMOD_BUILD_TOOLS=ON")
    endif()
endfunction()

dmblkid_register_test(test_dmblkid)

# test_automount - the automount service logic. Compiles the logic's source
# (the unit under test) into the test; the service's main.c, which only adds
# dmvfs, the file API and libsystemd around it, cannot run on the host loader
# (no dmvfs there).
set(DMOD_MODULE_NAME        test_automount)
dmod_add_test(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    automount_test.c
    test_images.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/automount/src/automount.c
)
target_include_directories(${DMOD_MODULE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/automount/src
)
target_link_libraries(${DMOD_MODULE_NAME} dmblkid_if)
dmblkid_register_test(${DMOD_MODULE_NAME})
