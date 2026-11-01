# Host unit tests. Run with: ctest --test-dir build/host --output-on-failure
add_executable(test_clickwheel test_clickwheel.c)
target_link_libraries(test_clickwheel neupod_clickwheel)
add_test(NAME clickwheel COMMAND test_clickwheel)

add_executable(test_fatfs test_fatfs.c)
# hal_host last: neupod_fatfs's diskio references hal_storage.
target_link_libraries(test_fatfs neupod_fatfs hal_host)
add_test(NAME fatfs COMMAND test_fatfs)
set_tests_properties(fatfs PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

set_source_files_properties(test_clickwheel.c test_fatfs.c
  PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")
