# Builds the shared portable app/ sources plus the ESP32-S3 HAL as the "main" component.
include(${CMAKE_CURRENT_LIST_DIR}/../../../app/sources.cmake)

idf_component_register(
  SRCS
    "main.c"
    "hal/hal_system.c"
    ${NEUPOD_APP_SOURCES}
  INCLUDE_DIRS
    ${NEUPOD_APP_INCLUDE_DIRS}
  REQUIRES
    esp_timer
    freertos
)
