/**
 * @file pc_api.h
 * @brief PC-control HTTP API + embedded UI on the shared port-80 server.
 */

#pragma once

#include "esp_err.h"
#include "microlink.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Register /pc and the api-v1 routes on the existing ml_config_httpd
 *  server instance (port 80).
 *  Must be called after microlink_start() (server already running) and
 *  after pc_controller_start(). */
esp_err_t pc_api_start(microlink_t *ml);

#ifdef __cplusplus
}
#endif
