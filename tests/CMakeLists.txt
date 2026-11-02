# Host unit tests. Run with: ctest --test-dir build/host --output-on-failure
add_executable(test_clickwheel test_clickwheel.c)
target_link_libraries(test_clickwheel neupod_clickwheel)
add_test(NAME clickwheel COMMAND test_clickwheel)

add_executable(test_fatfs test_fatfs.c)
# hal_host last: neupod_fatfs's diskio references hal_storage.
target_link_libraries(test_fatfs neupod_fatfs hal_host)
add_test(NAME fatfs COMMAND test_fatfs)
set_tests_properties(fatfs PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

add_executable(test_gfx test_gfx.c)
target_link_libraries(test_gfx neupod_app hal_host) # hal_host last: gfx_strip references hal_display
add_test(NAME gfx COMMAND test_gfx)

add_executable(test_storage_decode test_storage_decode.c)
target_link_libraries(test_storage_decode hal_host)
target_include_directories(test_storage_decode PRIVATE ${CMAKE_SOURCE_DIR}/hal/common)
add_test(NAME storage_decode COMMAND test_storage_decode)

add_executable(test_nfs test_nfs.c)
target_link_libraries(test_nfs neupod_storage_tests hal_host)
add_test(NAME nfs COMMAND test_nfs)
set_tests_properties(nfs PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

set_source_files_properties(test_nfs.c test_clickwheel.c test_fatfs.c test_gfx.c test_storage_decode.c
  PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")
