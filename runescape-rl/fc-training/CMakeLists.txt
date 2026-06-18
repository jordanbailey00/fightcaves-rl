# fc-training: Headless Fight Caves backend for RL training.
# Builds libfc_capi.so — loaded by Python via ctypes for PufferLib integration.
# Links against the shared fc-core backend and compiles the shared C API shim.

add_library(fc_capi SHARED ../fc-core/src/fc_capi.c)
target_include_directories(fc_capi PRIVATE
    ${PROJECT_SOURCE_DIR}/fc-core/include
)
target_compile_definitions(fc_capi PRIVATE FC_NO_HASH)
target_link_libraries(fc_capi PRIVATE fc_core m)

set_target_properties(fc_capi PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "fc_capi"
)
