#include "pc_cmd.h"
#include <string.h>

static const pc_cmd_def_t s_defs[PC_CMD_COUNT] = {
    { PC_CMD_POWER_ON, "POWER_ON", PC_CMD_ROUTE_HARDWARE, false, false,
      "Pulse the motherboard PWR_SW input to power the PC on" },
    { PC_CMD_WAKE_UP, "WAKE_UP", PC_CMD_ROUTE_HARDWARE, false, false,
      "Wake a sleeping PC (short PWR_SW press via the configured GPIO)" },
    { PC_CMD_SHUTDOWN, "SHUTDOWN", PC_CMD_ROUTE_WINDOWS, false, true,
      "Graceful Windows shutdown via the PC agent (cancellable while pending)" },
    { PC_CMD_RESTART, "RESTART", PC_CMD_ROUTE_WINDOWS, false, true,
      "Graceful Windows restart via the PC agent (cancellable while pending)" },
    { PC_CMD_PWR_RESET, "PWR_RESET", PC_CMD_ROUTE_HARDWARE, true, false,
      "Pulse the motherboard RESET_SW input (hardware reset; confirmation required)" },
    { PC_CMD_FORCE_POWER_OFF, "FORCE_POWER_OFF", PC_CMD_ROUTE_HARDWARE, true, false,
      "Hold PWR_SW to force a frozen PC off (strong confirmation required)" },
};

const pc_cmd_def_t *pc_cmd_def(pc_cmd_id_t id) {
    return (id >= 0 && id < PC_CMD_COUNT) ? &s_defs[id] : NULL;
}

const pc_cmd_def_t *pc_cmd_lookup(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < PC_CMD_COUNT; i++) {
        if (strcmp(s_defs[i].name, name) == 0) return &s_defs[i];
    }
    return NULL;
}

const char *pc_cmd_state_str(pc_cmd_state_t state) {
    switch (state) {
    case PC_CMD_STATE_ACCEPTED:  return "accepted";
    case PC_CMD_STATE_RUNNING:   return "running";
    case PC_CMD_STATE_SUCCEEDED: return "succeeded";
    case PC_CMD_STATE_FAILED:    return "failed";
    case PC_CMD_STATE_TIMEOUT:   return "timeout";
    case PC_CMD_STATE_CANCELLED: return "cancelled";
    default:                     return "none";
    }
}

const char *pc_cmd_route_str(pc_cmd_route_t route) {
    return (route == PC_CMD_ROUTE_WINDOWS) ? "windows" : "hardware";
}
