include(${CMAKE_CURRENT_LIST_DIR}/sources.cmake)

# Portable application code. Must not depend on any platform SDK.
add_library(neupod_app STATIC ${NEUPOD_APP_SOURCES})
target_include_directories(neupod_app PUBLIC ${NEUPOD_APP_INCLUDE_DIRS})
target_compile_features(neupod_app PUBLIC c_std_11)
