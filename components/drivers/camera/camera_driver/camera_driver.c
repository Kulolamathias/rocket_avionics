/**
 * @file components/drivers/camera/camera_driver.c
 * @brief Camera Driver – implementation for ESP32‑S3‑CAM.
 *
 * =============================================================================
 * IMPLEMENTATION NOTES
 * =============================================================================
 * - Uses esp_camera component (espressif/esp32-camera).
 * - Pin mapping is fixed for ESP32‑S3‑CAM board (OV2640 / OV3660).
 * - Supports one‑shot capture and continuous capture (for streaming).
 * - Frame buffers are managed by the driver; the user gets read‑only pointers.
 * =============================================================================
 */

#include "camera_driver.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "CAMERA_DRV";

/* ============================================================
 * Internal handle structure
 * ============================================================ */
struct camera_handle_t {
    camera_config_t config;             /**< Active configuration */
    camera_pixformat_t pixformat;
    camera_framesize_t framesize;
    uint8_t jpeg_quality;
    uint8_t fb_count;
    bool grab_mode;
    bool streaming;                     /**< true if continuous capture is active */
    camera_fb_t *latest_fb;             /**< Most recent frame buffer (for streaming) */
    esp_timer_handle_t capture_timer;   /**< Timer for periodic capture (if needed) */
};

/* ============================================================
 * Helper: convert our enums to esp_camera equivalents
 * ============================================================ */
static pixformat_t to_esp_pixformat(camera_pixformat_t fmt)
{
    switch (fmt) {
        case CAMERA_PIXFORMAT_JPEG:     return PIXFORMAT_JPEG;
        case CAMERA_PIXFORMAT_RGB565:   return PIXFORMAT_RGB565;
        case CAMERA_PIXFORMAT_GRAYSCALE:return PIXFORMAT_GRAYSCALE;
        default:                        return PIXFORMAT_JPEG;
    }
}

static framesize_t to_esp_framesize(camera_framesize_t size)
{
    switch (size) {
        case CAMERA_FRAMESIZE_QQVGA:    return FRAMESIZE_QQVGA;
        case CAMERA_FRAMESIZE_QVGA:     return FRAMESIZE_QVGA;
        case CAMERA_FRAMESIZE_VGA:      return FRAMESIZE_VGA;
        case CAMERA_FRAMESIZE_SVGA:     return FRAMESIZE_SVGA;
        case CAMERA_FRAMESIZE_XGA:      return FRAMESIZE_XGA;
        case CAMERA_FRAMESIZE_HD:       return FRAMESIZE_HD;
        case CAMERA_FRAMESIZE_FHD:      return FRAMESIZE_FHD;
        case CAMERA_FRAMESIZE_UXGA:     return FRAMESIZE_UXGA;
        case CAMERA_FRAMESIZE_QXGA:     return FRAMESIZE_QXGA;
        default:                        return FRAMESIZE_VGA;
    }
}

/* ============================================================
 * Public API
 * ============================================================ */

esp_err_t camera_driver_create(const camera_config_t *cfg, camera_handle_t *out_handle)
{
    if (!cfg || !out_handle) return ESP_ERR_INVALID_ARG;

    /* Allocate handle */
    camera_handle_t handle = calloc(1, sizeof(struct camera_handle_t));
    if (!handle) return ESP_ERR_NO_MEM;

    /* Store configuration */
    handle->pixformat = cfg->pixel_format;
    handle->framesize = cfg->frame_size;
    handle->jpeg_quality = cfg->jpeg_quality;
    handle->fb_count = cfg->fb_count;
    handle->grab_mode = cfg->grab_mode;
    handle->streaming = false;
    handle->latest_fb = NULL;

    /* Configure the camera driver (ESP32‑S3‑CAM specific pins) */
    camera_config_t esp_cfg = {
        .pin_pwdn  = -1,            /* No power down pin */
        .pin_reset = -1,            /* No reset pin */
        .pin_xclk  = 15,            /* XCLK pin */
        .pin_sscb_sda = 4,          /* SCCB SDA */
        .pin_sscb_scl = 5,          /* SCCB SCL */
        .pin_d7 = 16,
        .pin_d6 = 17,
        .pin_d5 = 18,
        .pin_d4 = 12,
        .pin_d3 = 11,
        .pin_d2 = 9,
        .pin_d1 = 8,
        .pin_d0 = 7,
        .pin_vsync = 6,
        .pin_href = 42,
        .pin_pclk = 13,
        .xclk_freq_hz = 20000000,   /* 20 MHz clock */
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = to_esp_pixformat(handle->pixformat),
        .frame_size = to_esp_framesize(handle->framesize),
        .jpeg_quality = handle->jpeg_quality,
        .fb_count = handle->fb_count,
        .grab_mode = handle->grab_mode ? CAMERA_GRAB_WHEN_EMPTY : CAMERA_GRAB_LATEST,
        .fb_location = CAMERA_FB_IN_DRAM,
        // .fb_location = handle->grab_mode ? CAMERA_FB_IN_DRAM : CAMERA_FB_IN_PSRAM,
        .frame_size = to_esp_framesize(handle->framesize),
    };

    esp_err_t ret = esp_camera_init(&esp_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: %d", ret);
        free(handle);
        return ret;
    }

    *out_handle = handle;
    ESP_LOGI(TAG, "Camera driver initialised (res=%d, quality=%d, pixfmt=%d)",
             handle->framesize, handle->jpeg_quality, handle->pixformat);
    return ESP_OK;
}

esp_err_t camera_driver_capture(camera_handle_t handle, const uint8_t **jpeg_buf, size_t *jpeg_len)
{
    if (!handle || !jpeg_buf || !jpeg_len) return ESP_ERR_INVALID_ARG;

    /* If streaming is active, temporarily stop to allow one‑shot capture */
    bool was_streaming = handle->streaming;
    if (was_streaming) {
        camera_driver_stop_stream(handle);
    }

    /* Capture a frame */
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Frame capture failed");
        return ESP_FAIL;
    }

    *jpeg_buf = fb->buf;
    *jpeg_len = fb->len;
    /* Note: we do NOT free the frame buffer; the caller must call esp_camera_fb_return(fb) later.
       However, our driver returns a pointer to the internal buffer; the caller cannot free it.
       To keep the API simple, we will copy the data? But copying large JPEGs wastes memory.
       Better: the caller should be responsible for returning the frame buffer.
       We'll store the fb in the handle and provide a separate release function. */

    /* For simplicity, we return the pointer and expect the caller to call camera_driver_release_frame().
       We'll store the fb in the handle. */
    handle->latest_fb = fb;
    *jpeg_buf = fb->buf;
    *jpeg_len = fb->len;

    if (was_streaming) {
        camera_driver_start_stream(handle);
    }
    return ESP_OK;
}

/* Helper to release the frame buffer (not yet in header, but we can add) */
static void release_frame(camera_handle_t handle)
{
    if (handle && handle->latest_fb) {
        esp_camera_fb_return(handle->latest_fb);
        handle->latest_fb = NULL;
    }
}

esp_err_t camera_driver_start_stream(camera_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    if (handle->streaming) return ESP_OK;

    /* For continuous capture, we set grab_mode to CAMERA_GRAB_WHEN_EMPTY */
    /* The esp_camera already supports continuous grabbing. We simply mark streaming = true.
       The user will call camera_driver_get_frame periodically. */
    handle->streaming = true;
    ESP_LOGI(TAG, "Continuous capture started");
    return ESP_OK;
}

esp_err_t camera_driver_get_frame(camera_handle_t handle, const uint8_t **jpeg_buf, size_t *jpeg_len)
{
    if (!handle || !jpeg_buf || !jpeg_len) return ESP_ERR_INVALID_ARG;
    if (!handle->streaming) return ESP_ERR_INVALID_STATE;

    /* Get the latest frame (non‑blocking). We use esp_camera_fb_get() which blocks until a frame is ready.
       To make it non‑blocking, we would need a different approach. For simplicity, we use a blocking call
       but with a short timeout. However, esp_camera_fb_get has no timeout. We'll rely on the fact that
       frames arrive regularly. In streaming, we want to get the newest frame as fast as possible.
       We'll use esp_camera_fb_get() and then immediately return it. The caller is responsible for calling
       camera_driver_release_frame() after processing. */
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        return ESP_ERR_TIMEOUT;
    }
    /* Release previous frame if any */
    if (handle->latest_fb) {
        esp_camera_fb_return(handle->latest_fb);
    }
    handle->latest_fb = fb;
    *jpeg_buf = fb->buf;
    *jpeg_len = fb->len;
    return ESP_OK;
}

esp_err_t camera_driver_stop_stream(camera_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    if (!handle->streaming) return ESP_OK;
    handle->streaming = false;
    /* Release any pending frame */
    if (handle->latest_fb) {
        esp_camera_fb_return(handle->latest_fb);
        handle->latest_fb = NULL;
    }
    ESP_LOGI(TAG, "Continuous capture stopped");
    return ESP_OK;
}

esp_err_t camera_driver_set_resolution(camera_handle_t handle, camera_framesize_t framesize)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    sensor_t *s = esp_camera_sensor_get();
    if (!s) return ESP_ERR_INVALID_STATE;
    framesize_t esp_size = to_esp_framesize(framesize);
    int ret = s->set_framesize(s, esp_size);
    if (ret == 0) {
        handle->framesize = framesize;
        ESP_LOGI(TAG, "Resolution changed to %d", framesize);
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t camera_driver_set_quality(camera_handle_t handle, uint8_t quality)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    sensor_t *s = esp_camera_sensor_get();
    if (!s) return ESP_ERR_INVALID_STATE;
    int ret = s->set_quality(s, quality);
    if (ret == 0) {
        handle->jpeg_quality = quality;
        ESP_LOGI(TAG, "JPEG quality set to %d", quality);
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t camera_driver_delete(camera_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    camera_driver_stop_stream(handle);
    /* Deinit camera (optional, but we can leave it for the next init) */
    /* esp_camera_deinit() is not provided; we just free the handle. */
    free(handle);
    ESP_LOGI(TAG, "Camera driver deleted");
    return ESP_OK;
}