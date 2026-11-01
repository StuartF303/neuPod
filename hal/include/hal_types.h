// SPDX-License-Identifier: MPL-2.0
/**
 * @file hal_types.h
 * @brief Common types shared by every HAL interface.
 */
#ifndef NEUPOD_HAL_TYPES_H
#define NEUPOD_HAL_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Status code returned by HAL calls. Zero is success; negative values are errors. */
typedef enum {
    HAL_OK = 0,
    HAL_ERR = -1,      /**< Unspecified failure. */
    HAL_EINVAL = -2,   /**< Invalid argument. */
    HAL_ETIMEOUT = -3, /**< Operation timed out. */
    HAL_ENODEV = -4,   /**< Device absent or not responding. */
    HAL_ENOTSUP = -5,  /**< Not implemented on this platform. */
    HAL_EBUSY = -6,    /**< Resource busy (e.g. DMA in flight). */
} hal_status_t;

#endif // NEUPOD_HAL_TYPES_H
