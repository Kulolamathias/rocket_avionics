/**
 * @file components/drivers/sensors/imu/mpu_driver/mpu_driver.c
 * @brief MPU-9250/6500/9255 Driver – I2C communication and sensor fusion.
 *
 * =============================================================================
 * IMPLEMENTATION NOTES
 * =============================================================================
 * - Uses I2C master driver with 100 kHz clock (safe for all boards).
 * - Registers defined for MPU-9250/6500 (compatible subset).
 * - Raw data is converted to physical units using datasheet scales.
 * - Calibration subtracts offsets from raw readings.
 * - Orientation computed via complementary filter (fast, stable).
 * - Quaternion derived from Euler angles using standard conversion.
 * =============================================================================
 * 
 * @author matthithyahu
 * @date 2026
 * 
 */

#include "mpu_driver.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include <inttypes.h>
#include <string.h>
#include <math.h>

static const char *TAG = "MPU_DRV";

/* MPU-9250/6500 Register Map (common subset) */
#define MPU_ADDR            0x68
#define REG_SMPLRT_DIV      0x19
#define REG_CONFIG          0x1A
#define REG_GYRO_CONFIG     0x1B
#define REG_ACCEL_CONFIG    0x1C
#define REG_ACCEL_CONFIG2   0x1D
#define REG_USER_CTRL       0x6A
#define REG_PWR_MGMT_1      0x6B
#define REG_PWR_MGMT_2      0x6C
#define REG_INT_PIN_CFG     0x37
#define REG_INT_ENABLE      0x38
#define REG_ACCEL_XOUT_H    0x3B
#define REG_GYRO_XOUT_H     0x43

/* Magnetometer registers (MPU-9250) – accessed via I2C passthrough */
#define MAG_I2C_ADDR        0x0C
#define MAG_WIA             0x00
#define MAG_CNTL1           0x0A
#define MAG_CNTL2           0x0B
#define MAG_XOUT_L          0x03

/* Scale factors (from datasheet) */
#define ACCEL_SCALE_2G      16384.0f   /* LSB/g */
#define GYRO_SCALE_250DPS   131.0f     /* LSB/(°/s) – convert later to rad/s */
#define MAG_SCALE           0.15f      /* µT per LSB (typical) */

/* Constants for orientation filter */
#define FILTER_ALPHA        0.96f      /* Complementary filter weight for accel */
#define GYRO_DRIFT_COMP     0.001f     /* Gyro drift compensation factor */

/* Internal structure for an MPU instance */
struct mpu_handle_t {
    i2c_port_t i2c_port;
    uint8_t i2c_addr;
    bool enable_mag;
    bool initialized;
    float sample_interval_s;           /* seconds between reads */
    /* Raw data storage */
    mpu_raw_data_t raw;
    mpu_orientation_t orientation;
    /* Calibration offsets */
    mpu_calibration_t calib;
    /* Previous orientation for complementary filter */
    float last_roll, last_pitch, last_yaw;
    uint64_t last_update_us;
};

/* Helper: write a byte to an MPU register */
static esp_err_t mpu_write_byte(mpu_handle_t handle, uint8_t reg, uint8_t data)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (handle->i2c_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, data, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(handle->i2c_port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

/* Helper: read multiple bytes from an MPU register */
static esp_err_t mpu_read_bytes(mpu_handle_t handle, uint8_t reg, uint8_t *buf, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (handle->i2c_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (handle->i2c_addr << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, buf, len, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(handle->i2c_port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

/* Helper: read a signed 16‑bit value from a register */
static int16_t mpu_read_int16(mpu_handle_t handle, uint8_t reg)
{
    uint8_t buf[2];
    if (mpu_read_bytes(handle, reg, buf, 2) != ESP_OK) return 0;
    return (int16_t)(buf[0] << 8 | buf[1]);
}

/* Helper: write to magnetometer (if enabled) */
static esp_err_t mag_write_byte(mpu_handle_t handle, uint8_t reg, uint8_t data)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MAG_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, data, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(handle->i2c_port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

/* Helper: read from magnetometer */
static esp_err_t mag_read_bytes(mpu_handle_t handle, uint8_t reg, uint8_t *buf, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MAG_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MAG_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, buf, len, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(handle->i2c_port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

/* Helper: read magnetometer data */
static void read_magnetometer(mpu_handle_t handle)
{
    if (!handle->enable_mag) return;
    uint8_t buf[6];
    if (mag_read_bytes(handle, MAG_XOUT_L, buf, 6) != ESP_OK) {
        handle->raw.mag_valid = false;
        return;
    }
    int16_t mx = (int16_t)(buf[1] << 8 | buf[0]);
    int16_t my = (int16_t)(buf[3] << 8 | buf[2]);
    int16_t mz = (int16_t)(buf[5] << 8 | buf[4]);
    handle->raw.mag_x = (float)mx * MAG_SCALE;
    handle->raw.mag_y = (float)my * MAG_SCALE;
    handle->raw.mag_z = (float)mz * MAG_SCALE;
    handle->raw.mag_valid = true;
}

/* Helper: compute orientation using complementary filter */
static void update_orientation(mpu_handle_t handle)
{
    uint64_t now = esp_timer_get_time();
    float dt = (now - handle->last_update_us) / 1000000.0f;
    if (dt <= 0) dt = 0.01f;  /* fallback */
    handle->last_update_us = now;

    /* Convert gyro from deg/s to rad/s */
    float gx_rad = handle->raw.gyro_x * (M_PI / 180.0f);
    float gy_rad = handle->raw.gyro_y * (M_PI / 180.0f);
    float gz_rad = handle->raw.gyro_z * (M_PI / 180.0f);

    /* Estimate roll and pitch from accelerometer */
    float accel_roll = atan2f(handle->raw.accel_y, handle->raw.accel_z);
    float accel_pitch = atan2f(-handle->raw.accel_x, sqrtf(handle->raw.accel_y * handle->raw.accel_y +
                                                             handle->raw.accel_z * handle->raw.accel_z));

    /* Complementary filter for roll and pitch */
    float roll = FILTER_ALPHA * (handle->last_roll + gx_rad * dt) + (1 - FILTER_ALPHA) * accel_roll;
    float pitch = FILTER_ALPHA * (handle->last_pitch + gy_rad * dt) + (1 - FILTER_ALPHA) * accel_pitch;

    /* Yaw from magnetometer (if available) or gyro integration */
    float yaw;
    if (handle->enable_mag && handle->raw.mag_valid) {
        float mag_yaw = atan2f(handle->raw.mag_y, handle->raw.mag_x);
        yaw = FILTER_ALPHA * (handle->last_yaw + gz_rad * dt) + (1 - FILTER_ALPHA) * mag_yaw;
    } else {
        yaw = handle->last_yaw + gz_rad * dt;
    }

    /* Store for next iteration */
    handle->last_roll = roll;
    handle->last_pitch = pitch;
    handle->last_yaw = yaw;

    /* Convert to quaternion (from Euler angles ZYX order) */
    float cy = cosf(yaw * 0.5f);
    float sy = sinf(yaw * 0.5f);
    float cp = cosf(pitch * 0.5f);
    float sp = sinf(pitch * 0.5f);
    float cr = cosf(roll * 0.5f);
    float sr = sinf(roll * 0.5f);

    handle->orientation.q_w = cr * cp * cy + sr * sp * sy;
    handle->orientation.q_x = sr * cp * cy - cr * sp * sy;
    handle->orientation.q_y = cr * sp * cy + sr * cp * sy;
    handle->orientation.q_z = cr * cp * sy - sr * sp * cy;

    handle->orientation.roll_rad = roll;
    handle->orientation.pitch_rad = pitch;
    handle->orientation.yaw_rad = yaw;
    handle->orientation.orientation_valid = true;
}

/* ============================================================
 * Public API
 * ============================================================ */

esp_err_t mpu_driver_create(const mpu_config_t *cfg, mpu_handle_t *out_handle)
{
    if (!cfg || !out_handle) return ESP_ERR_INVALID_ARG;

    mpu_handle_t handle = calloc(1, sizeof(struct mpu_handle_t));
    if (!handle) return ESP_ERR_NO_MEM;

    handle->i2c_port = cfg->i2c_port;
    handle->i2c_addr = (cfg->i2c_addr != 0) ? cfg->i2c_addr : MPU_ADDR;
    handle->enable_mag = cfg->enable_magnetometer;
    handle->sample_interval_s = 1.0f / cfg->sample_rate_hz;
    handle->initialized = false;
    memcpy(&handle->calib, &cfg->calibration, sizeof(mpu_calibration_t));
    handle->last_update_us = esp_timer_get_time();

    /* Configure I2C (if not already configured) – assume caller has set up I2C master */
    /* We'll just trust the I2C port is already initialised; if not, caller must init. */

    /* Wake up MPU */
    esp_err_t ret = mpu_write_byte(handle, REG_PWR_MGMT_1, 0x00);
    if (ret != ESP_OK) goto fail;
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Configure sample rate divider */
    uint8_t divider = (1000 / cfg->sample_rate_hz) - 1;
    if (divider > 255) divider = 255;
    mpu_write_byte(handle, REG_SMPLRT_DIV, divider);

    /* Configure gyroscope (250 dps = ±250°/s) */
    mpu_write_byte(handle, REG_GYRO_CONFIG, 0x00);   /* FS_SEL = 0 -> 250 dps */
    /* Configure accelerometer (2g) */
    mpu_write_byte(handle, REG_ACCEL_CONFIG, 0x00);  /* AFS_SEL = 0 -> 2g */
    mpu_write_byte(handle, REG_ACCEL_CONFIG2, 0x00); /* set average filter */

    /* Enable I2C passthrough for magnetometer */
    if (handle->enable_mag) {
        mpu_write_byte(handle, REG_INT_PIN_CFG, 0x02); /* BYPASS_EN */
        vTaskDelay(pdMS_TO_TICKS(10));
        /* Initialise magnetometer */
        mag_write_byte(handle, MAG_CNTL2, 0x01);      /* soft reset */
        vTaskDelay(pdMS_TO_TICKS(10));
        mag_write_byte(handle, MAG_CNTL1, 0x16);      /* 100 Hz continuous mode */
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    handle->initialized = true;
    *out_handle = handle;
    ESP_LOGI(TAG, "MPU driver initialised (I2C port %d, addr 0x%02X, sample rate %" PRId32" Hz)",
             handle->i2c_port, handle->i2c_addr, cfg->sample_rate_hz);
    return ESP_OK;

fail:
    free(handle);
    return ret;
}

esp_err_t mpu_driver_update(mpu_handle_t handle)
{
    if (!handle || !handle->initialized) return ESP_ERR_INVALID_STATE;

    /* Read accelerometer and gyroscope */
    int16_t ax = mpu_read_int16(handle, REG_ACCEL_XOUT_H);
    int16_t ay = mpu_read_int16(handle, REG_ACCEL_XOUT_H + 2);
    int16_t az = mpu_read_int16(handle, REG_ACCEL_XOUT_H + 4);
    int16_t gx = mpu_read_int16(handle, REG_GYRO_XOUT_H);
    int16_t gy = mpu_read_int16(handle, REG_GYRO_XOUT_H + 2);
    int16_t gz = mpu_read_int16(handle, REG_GYRO_XOUT_H + 4);

    /* Convert to physical units (2g scale, 250 dps scale) */
    handle->raw.accel_x = (float)ax / ACCEL_SCALE_2G * 9.80665f;  /* m/s² */
    handle->raw.accel_y = (float)ay / ACCEL_SCALE_2G * 9.80665f;
    handle->raw.accel_z = (float)az / ACCEL_SCALE_2G * 9.80665f;
    handle->raw.gyro_x  = (float)gx / GYRO_SCALE_250DPS;         /* °/s */
    handle->raw.gyro_y  = (float)gy / GYRO_SCALE_250DPS;
    handle->raw.gyro_z  = (float)gz / GYRO_SCALE_250DPS;

    /* Apply calibration offsets */
    handle->raw.accel_x -= handle->calib.accel_offset_x;
    handle->raw.accel_y -= handle->calib.accel_offset_y;
    handle->raw.accel_z -= handle->calib.accel_offset_z;
    handle->raw.gyro_x  -= handle->calib.gyro_offset_x;
    handle->raw.gyro_y  -= handle->calib.gyro_offset_y;
    handle->raw.gyro_z  -= handle->calib.gyro_offset_z;

    handle->raw.accel_valid = true;
    handle->raw.gyro_valid = true;

    /* Read magnetometer if enabled */
    if (handle->enable_mag) {
        read_magnetometer(handle);
        if (handle->raw.mag_valid) {
            handle->raw.mag_x -= handle->calib.mag_offset_x;
            handle->raw.mag_y -= handle->calib.mag_offset_y;
            handle->raw.mag_z -= handle->calib.mag_offset_z;
        }
    }

    /* Update orientation */
    update_orientation(handle);

    return ESP_OK;
}

esp_err_t mpu_driver_get_raw(mpu_handle_t handle, mpu_raw_data_t *data)
{
    if (!handle || !data) return ESP_ERR_INVALID_ARG;
    memcpy(data, &handle->raw, sizeof(mpu_raw_data_t));
    return ESP_OK;
}

esp_err_t mpu_driver_get_orientation(mpu_handle_t handle, mpu_orientation_t *orientation)
{
    if (!handle || !orientation) return ESP_ERR_INVALID_ARG;
    memcpy(orientation, &handle->orientation, sizeof(mpu_orientation_t));
    return ESP_OK;
}

esp_err_t mpu_driver_set_calibration(mpu_handle_t handle, const mpu_calibration_t *calib)
{
    if (!handle || !calib) return ESP_ERR_INVALID_ARG;
    memcpy(&handle->calib, calib, sizeof(mpu_calibration_t));
    return ESP_OK;
}

esp_err_t mpu_driver_calibrate(mpu_handle_t handle, uint32_t duration_ms)
{
    if (!handle) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "Starting automatic calibration for %lu ms...", duration_ms);
    uint32_t samples = 0;
    float sum_ax = 0, sum_ay = 0, sum_az = 0;
    float sum_gx = 0, sum_gy = 0, sum_gz = 0;

    uint32_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(duration_ms)) {
        /* Read raw values (without calibration) */
        int16_t ax = mpu_read_int16(handle, REG_ACCEL_XOUT_H);
        int16_t ay = mpu_read_int16(handle, REG_ACCEL_XOUT_H + 2);
        int16_t az = mpu_read_int16(handle, REG_ACCEL_XOUT_H + 4);
        int16_t gx = mpu_read_int16(handle, REG_GYRO_XOUT_H);
        int16_t gy = mpu_read_int16(handle, REG_GYRO_XOUT_H + 2);
        int16_t gz = mpu_read_int16(handle, REG_GYRO_XOUT_H + 4);

        sum_ax += (float)ax / ACCEL_SCALE_2G * 9.80665f;
        sum_ay += (float)ay / ACCEL_SCALE_2G * 9.80665f;
        sum_az += (float)az / ACCEL_SCALE_2G * 9.80665f;
        sum_gx += (float)gx / GYRO_SCALE_250DPS;
        sum_gy += (float)gy / GYRO_SCALE_250DPS;
        sum_gz += (float)gz / GYRO_SCALE_250DPS;
        samples++;
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    if (samples == 0) return ESP_ERR_TIMEOUT;

    /* Compute average offsets */
    handle->calib.accel_offset_x = sum_ax / samples;
    handle->calib.accel_offset_y = sum_ay / samples;
    handle->calib.accel_offset_z = sum_az / samples - 9.80665f; /* expect 1g on Z */
    handle->calib.gyro_offset_x  = sum_gx / samples;
    handle->calib.gyro_offset_y  = sum_gy / samples;
    handle->calib.gyro_offset_z  = sum_gz / samples;

    ESP_LOGI(TAG, "Calibration complete. Accel offsets: (%.3f, %.3f, %.3f) m/s²",
             handle->calib.accel_offset_x, handle->calib.accel_offset_y, handle->calib.accel_offset_z);
    ESP_LOGI(TAG, "Gyro offsets: (%.3f, %.3f, %.3f) °/s",
             handle->calib.gyro_offset_x, handle->calib.gyro_offset_y, handle->calib.gyro_offset_z);
    return ESP_OK;
}

esp_err_t mpu_driver_delete(mpu_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    free(handle);
    return ESP_OK;
}