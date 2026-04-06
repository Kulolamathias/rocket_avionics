/**
 * @file components/services/payload/camera_service/camera_service.h
 * @brief Camera Service – manages camera capture, streaming, and MQTT integration.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This service owns the camera driver and provides a command interface to:
 * - Start / stop MJPEG streaming (HTTP server).
 * - Capture still images (JPEG) and publish the URL via MQTT.
 * - Change resolution and JPEG quality on the fly.
 * - Optionally record short video clips (future).
 *
 * It does NOT contain any rocket flight logic; it is a payload service.
 *
 * =============================================================================
 * COMMANDS HANDLED
 * =============================================================================
 * - CMD_CAMERA_START_STREAM     – start HTTP MJPEG stream
 * - CMD_CAMERA_STOP_STREAM      – stop streaming
 * - CMD_CAMERA_CAPTURE          – take a still image and publish its URL
 * - CMD_CAMERA_SET_RESOLUTION   – change resolution (params: camera_resolution_params_t)
 * - CMD_CAMERA_SET_QUALITY      – change JPEG quality (params: camera_quality_params_t)
 *
 * =============================================================================
 * EVENTS POSTED
 * =============================================================================
 * - EVENT_CAMERA_IMAGE_CAPTURED – when a still image is captured (contains URL)
 *
 * =============================================================================
 * HTTP ENDPOINTS (provided by the service)
 * =============================================================================
 * - /stream                     – MJPEG stream (multipart/x-mixed-replace)
 * - /capture                    – single JPEG image (download)
 * - /status                     – camera status (JSON)
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026-04-03
 */

#ifndef CAMERA_SERVICE_H
#define CAMERA_SERVICE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the camera service.
 *
 * Creates the camera driver, initialises the HTTP server, and registers endpoints.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t camera_service_init(void);

/**
 * @brief Register camera service command handlers with the command router.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t camera_service_register_handlers(void);

/**
 * @brief Start the camera service (idle; streaming is started by command).
 *
 * @return ESP_OK always.
 */
esp_err_t camera_service_start(void);

/**
 * @brief Stop the camera service (stops streaming, deletes HTTP server).
 *
 * @return ESP_OK always.
 */
esp_err_t camera_service_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* CAMERA_SERVICE_H */