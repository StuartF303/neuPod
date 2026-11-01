# Portable application sources, shared by every platform build (incl. ESP-IDF components).
set(NEUPOD_APP_DIR ${CMAKE_CURRENT_LIST_DIR})
set(NEUPOD_APP_SOURCES
  ${NEUPOD_APP_DIR}/app.c
)
set(NEUPOD_APP_INCLUDE_DIRS
  ${NEUPOD_APP_DIR}/include
  ${NEUPOD_APP_DIR}/../hal/include
)
