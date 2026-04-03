#ifndef TELEMETRY_SERVICE_H
#define TELEMETRY_SERVICE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t telemetry_service_init(void);
esp_err_t telemetry_service_register_handlers(void);
esp_err_t telemetry_service_start(void);

#ifdef __cplusplus
}
#endif

#endif /* TELEMETRY_SERVICE_H */