/**
 * @file components/services/src/service_manager.c
 * @brief Service Manager – implementation.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * Implements the service manager by iterating over a static table of services
 * for each lifecycle phase (init, register, start). The order of services is
 * determined by the order of entries in the g_services array.
 *
 * =============================================================================
 * DESIGN NOTES
 * =============================================================================
 * - The service table is declared with extern forward declarations for
 *   service functions. The actual services are linked in later.
 * - Fail‑fast: any error in a phase stops execution and returns the error.
 * - Logging is provided for each step.
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026
 */

#include "service_manager.h"
#include "service_interfaces.h"
#include "esp_log.h"
#include "esp_err.h"

#include "loadcell_service.h"
#include "mpu_service.h"
// #include "camera_service.h"

#include "mqtt_service.h"
#include "wifi_service.h"
#include "rocket_discovery_service.h"
#include "rocket_command_service.h"
#include "logging_service.h"

static const char *TAG = "SERVICE_MGR";

/* ============================================================
 * SERVICE ENTRY STRUCTURE
 * ============================================================ */

typedef struct {
    const char *name;
    esp_err_t (*init)(void);
    esp_err_t (*register_handlers)(void);
    esp_err_t (*start)(void);
} service_entry_t;

/* ============================================================
 * STATIC SERVICE REGISTRY
 * ============================================================ */

/**
 * @brief Ordered list of all services.
 *
 * The order defines the initialization, registration, and start sequence.
 * This order is fixed at compile time.
 */
static const service_entry_t g_services[] = {
    {
        .name = "wifi",
        .init = wifi_service_init,
        .register_handlers = wifi_service_register_handlers,
        .start = wifi_service_start
    },
    {
        .name = "mqtt",
        .init = mqtt_service_init,
        .register_handlers = mqtt_service_register_handlers,
        .start = mqtt_service_start
    },
    {
        .name = "rocket_discovery",
        .init = rocket_discovery_service_init,
        .register_handlers = rocket_discovery_service_register_handlers,
        .start = rocket_discovery_service_start
    },
    {
        .name = "rocket_command",
        .init = rocket_command_service_init,
        .register_handlers = rocket_command_service_register_handlers,
        .start = rocket_command_service_start
    },
    {
        .name = "logging",
        .init = logging_service_init,
        .register_handlers = logging_service_register_handlers,
        .start = logging_service_start
    },
    // {
    //     .name = "telemetry",
    //     .init = telemetry_service_init,
    //     .register_handlers = telemetry_service_register_handlers,
    //     .start = telemetry_service_start
    // },
    // {
    //     .name = "camera",
    //     .init = camera_service_init,
    //     .register_handlers = camera_service_register_handlers,
    //     .start = camera_service_start
    // },
    {
        .name = "mpu",
        .init = mpu_service_init,
        .register_handlers = mpu_service_register_handlers,
        .start = mpu_service_start
    },
    {
        .name = "loadcell",
        .init = loadcell_service_init,
        .register_handlers = loadcell_service_register_handlers,
        .start = loadcell_service_start
    }
    // {
    //     .name = "control",
    //     .init = control_service_init,
    //     .register_handlers = control_service_register_handlers,
    //     .start = control_service_start
    // },
    // {
    //     .name = "recovery",
    //     .init = recovery_service_init,
    //     .register_handlers = recovery_service_register_handlers,
    //     .start = recovery_service_start
    // }
    /* New services should be appended here in dependency order */
};

#define SERVICE_COUNT (sizeof(g_services) / sizeof(g_services[0]))

/* ============================================================
 * PHASE EXECUTOR
 * ============================================================ */

typedef esp_err_t (*service_phase_fn_t)(const service_entry_t *service);

static esp_err_t execute_phase(const char *phase_name, service_phase_fn_t phase_fn)
{
    for (size_t i = 0; i < SERVICE_COUNT; i++) {
        const service_entry_t *svc = &g_services[i];
        esp_err_t ret = phase_fn(svc);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[%s] failed: %s (err=0x%x)",
                     phase_name, svc->name, (unsigned int)ret);
            return ret;
        }
        ESP_LOGI(TAG, "[%s] ok: %s", phase_name, svc->name);
    }
    return ESP_OK;
}

/* ============================================================
 * PHASE WRAPPERS
 * ============================================================ */

static esp_err_t phase_init(const service_entry_t *svc)
{
    if (svc->init == NULL) {
        return ESP_OK; /* optional init */
    }
    return svc->init();
}

static esp_err_t phase_register(const service_entry_t *svc)
{
    if (svc->register_handlers == NULL) {
        return ESP_OK; /* optional registration */
    }
    return svc->register_handlers();
}

static esp_err_t phase_start(const service_entry_t *svc)
{
    if (svc->start == NULL) {
        return ESP_OK; /* optional start */
    }
    return svc->start();
}

/* ============================================================
 * PUBLIC API
 * ============================================================ */

esp_err_t service_manager_init_all(void)
{
    ESP_LOGI(TAG, "Initializing all services");
    return execute_phase("init", phase_init);
}

esp_err_t service_manager_register_all(void)
{
    ESP_LOGI(TAG, "Registering all service command handlers");
    return execute_phase("register", phase_register);
}

esp_err_t service_manager_start_all(void)
{
    ESP_LOGI(TAG, "Starting all services");
    return execute_phase("start", phase_start);
}