# Builds the shared portable app/ sources plus the ESP32-S3 HAL as the "main" component.
include(${CMAKE_CURRENT_LIST_DIR}/../../../app/sources.cmake)

# NOTE: this ESP-IDF build is UNVERIFIED since the display/input/FatFs work was added.
# The ESP HAL currently only implements hal_system, so compile just the core + click-wheel
# sources. FatFs and diskio are deliberately excluded: ESP-IDF ships its own fatfs component
# (ff.h, diskio.h, disk_* symbols) and pulling in ours as well would clash. Add them back (or
# adapt to IDF's fatfs) once the ESP HAL has storage.
set(NEUPOD_ESP_SOURCES ${NEUPOD_CORE_SOURCES} ${NEUPOD_CLICKWHEEL_SOURCES})

idf_component_register(
  SRCS
    "main.c"
    "hal/hal_system.c"
    ${NEUPOD_ESP_SOURCES}
  INCLUDE_DIRS
    ${NEUPOD_APP_INCLUDE_DIRS}
  REQUIRES
    esp_timer
    freertos
)
