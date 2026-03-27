/**
 * @file components/services/common/service_manager.h
 * @brief Service Manager – deterministic service orchestration.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * Owns the static list of all services and enforces a strict lifecycle:
 *
 *   INIT → REGISTER → START
 *
 * This guarantees:
 * - All services are initialized before handler registration.
 * - All handlers are registered before any service starts.
 * - The system cannot run in a partially wired state.
 *
 * =============================================================================
 * DESIGN PRINCIPLES
 * =============================================================================
 * - Deterministic startup order (compile‑time defined).
 * - Centralized orchestration (no hidden initialization).
 * - Fail‑fast behavior (abort on first failure).
 *
 * =============================================================================
 * SYSTEM INTEGRATION
 * =============================================================================
 * Must be used in this order:
 *
 *   service_manager_init_all()
 *   service_manager_register_all()
 *   command_router_lock()
 *   service_manager_start_all()
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026
 */

#ifndef SERVICE_MANAGER_H
#define SERVICE_MANAGER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * PUBLIC API – LIFECYCLE PHASES
 * ============================================================ */

/**
 * @brief Initialize all services.
 *
 * Calls each service's init() function in predefined order.
 *
 * @return ESP_OK on success, error code of first failing service.
 */
esp_err_t service_manager_init_all(void);

/**
 * @brief Register command handlers for all services.
 *
 * Calls each service's register_handlers() function.
 *
 * @return ESP_OK on success, error code of first failing service.
 */
esp_err_t service_manager_register_all(void);

/**
 * @brief Start all services.
 *
 * Calls each service's start() function. Should be called only after
 * command_router_lock() has been called.
 *
 * @return ESP_OK on success, error code of first failing service.
 */
esp_err_t service_manager_start_all(void);

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_MANAGER_H */