/**
 * @file components/drivers/camera/camera_driver.h
 * @brief Camera Driver – low‑level wrapper for ESP32‑S3‑CAM (OV2640 / OV3660).
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This driver provides a handle‑based interface to the camera sensor.
 * It handles initialisation, configuration (resolution, quality, format),
 * frame capture, and resource management. It contains NO business logic,
 * NO command handling, and NO event posting.
 *
 * =============================================================================
 * FEATURES
 * =============================================================================
 * - Supports OV2640 (built‑in) and OV3660 (higher resolution).
 * - Configurable resolution, JPEG quality, and pixel format.
 * - Frame capture as JPEG buffer (ready for HTTP or MQTT).
 * - Multiple instances (only one sensor, but the handle can be reused).
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026-04-03
 */

#ifndef CAMERA_DRIVER_H
#define CAMERA_DRIVER_H

#include "esp_err.h"
#include "esp_camera.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque handle for a camera instance. */
typedef struct camera_handle_t *camera_handle_t;

/**
 * @brief Camera pixel format.
 */
typedef enum {
    CAMERA_PIXFORMAT_JPEG,   /**< JPEG compressed (recommended for streaming / images) */
    CAMERA_PIXFORMAT_RGB565, /**< Raw RGB565 (large, not recommended) */
    CAMERA_PIXFORMAT_GRAYSCALE, /**< Grayscale (for special use) */
} camera_pixformat_t;

/**
 * @brief Camera frame size (resolution).
 */
typedef enum {
    CAMERA_FRAMESIZE_QQVGA = 0,   /**< 160x120 */
    CAMERA_FRAMESIZE_QVGA  = 1,   /**< 320x240 */
    CAMERA_FRAMESIZE_VGA   = 2,   /**< 640x480 */
    CAMERA_FRAMESIZE_SVGA  = 3,   /**< 800x600 */
    CAMERA_FRAMESIZE_XGA   = 4,   /**< 1024x768 */
    CAMERA_FRAMESIZE_HD    = 5,   /**< 1280x720 */
    CAMERA_FRAMESIZE_FHD   = 6,   /**< 1920x1080 */
    CAMERA_FRAMESIZE_UXGA  = 7,   /**< 1600x1200 (OV2640 max) */
    CAMERA_FRAMESIZE_QXGA  = 8,   /**< 2048x1536 (OV3660) */
} camera_framesize_t;

// /**
//  * @brief Camera configuration structure.
//  */
// typedef struct {
//     camera_pixformat_t pixformat;   /**< Pixel format (JPEG recommended) */
//     camera_framesize_t framesize;   /**< Resolution */
//     uint8_t jpeg_quality;           /**< JPEG quality (0‑63, lower = higher quality) */
//     uint8_t fb_count;               /**< Number of frame buffers (1 is enough for most) */
//     bool grab_mode;                 /**< true = continuous grab, false = one‑shot */
// } camera_config_t;

/**
 * @brief Create a camera instance (initialises the sensor).
 *
 * @param cfg   Configuration (resolution, quality, etc.).
 * @param out_handle Pointer to store the created handle.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t camera_driver_create(const camera_config_t *cfg, camera_handle_t *out_handle);

/**
 * @brief Capture a single frame (blocking).
 *
 * @param handle Camera instance handle.
 * @param[out] jpeg_buf Pointer to buffer that will receive the JPEG data.
 * @param[out] jpeg_len Length of the JPEG data in bytes.
 * @return ESP_OK on success, error code otherwise.
 * @note The returned buffer is managed by the driver; do not free it.
 *       It remains valid until the next capture or driver deletion.
 */
esp_err_t camera_driver_capture(camera_handle_t handle, const uint8_t **jpeg_buf, size_t *jpeg_len);

/**
 * @brief Start continuous frame capture (for streaming).
 *
 * @param handle Camera instance handle.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t camera_driver_start_stream(camera_handle_t handle);

/**
 * @brief Get the latest captured frame (non‑blocking, for streaming).
 *
 * @param handle Camera instance handle.
 * @param[out] jpeg_buf Pointer to buffer with the latest JPEG frame.
 * @param[out] jpeg_len Length of the JPEG data.
 * @return ESP_OK if a new frame is available, ESP_ERR_TIMEOUT otherwise.
 */
esp_err_t camera_driver_get_frame(camera_handle_t handle, const uint8_t **jpeg_buf, size_t *jpeg_len);

/**
 * @brief Stop continuous frame capture.
 *
 * @param handle Camera instance handle.
 * @return ESP_OK on success.
 */
esp_err_t camera_driver_stop_stream(camera_handle_t handle);

/**
 * @brief Change camera resolution (reconfigures the sensor).
 *
 * @param handle Camera instance handle.
 * @param framesize New resolution.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t camera_driver_set_resolution(camera_handle_t handle, camera_framesize_t framesize);

/**
 * @brief Change JPEG quality.
 *
 * @param handle Camera instance handle.
 * @param quality JPEG quality (0‑63, lower = higher quality).
 * @return ESP_OK on success.
 */
esp_err_t camera_driver_set_quality(camera_handle_t handle, uint8_t quality);

/**
 * @brief Delete a camera instance and free resources.
 *
 * @param handle Camera instance handle.
 * @return ESP_OK on success.
 */
esp_err_t camera_driver_delete(camera_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* CAMERA_DRIVER_H */