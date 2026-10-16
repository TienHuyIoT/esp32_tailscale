/**
 * @file pc_relay.c
 * @brief Relay Manager implementation (NVS-backed config, RAM-only state).
 *
 * All relay GPIO operations run here, serialized by an internal mutex. The
 * pins are disjoint from the PC-control function pins (validated on both
 * save paths), so the pulse engine in pc_hw.c and the relay driver can
 * never fight over one pin.
 *
 * Idle vs assert: idle = the de-energized electrical level; assert = the
 * configured active level. Every pin is pre-latched into its target level
 * BEFORE the output driver is enabled (same glitch-free technique as
 * pc_hw.c pulse()) - enabling an output can never emit a spurious pulse.
 *
 * Honesty: `state` is only ever the COMMANDED logical state. There is no
 * relay contact feedback, so nothing here claims the physical contact
 * closed or that a load is powered.
 */

#include "pc_relay.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "pc_relay";

#define NVS_NS             "pc_ctl"   /* shared with pc_config / pc_auth */
#define NVS_KEY_RELAYS     "relays"
#define RELAY_BLOB_VERSION 1

typedef struct __attribute__((packed)) {
    char    name[PC_RELAY_NAME_MAX + 1];
    uint8_t enabled;
    int8_t  gpio;            /* -1 = NC */
    uint8_t active_level;    /* PC_CFG_LEVEL_HIGH / _LOW */
    uint8_t startup_policy;  /* PC_RELAY_STARTUP_OFF */
} pc_relay_blob_channel_t;

typedef struct __attribute__((packed)) {
    uint8_t version;
    uint8_t channel_count;
    pc_relay_blob_channel_t channels[CONFIG_PCCTL_RELAY_MAX_CHANNELS];
} pc_relay_blob_t;

static pc_relay_snapshot_t s_snap;   /* configuration + commanded states */
static SemaphoreHandle_t s_lock;
static nvs_handle_t s_nvs;
static bool s_nvs_ok;

static void lock(void) { if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { if (s_lock) xSemaphoreGive(s_lock); }

static void load_defaults(void) {
    memset(&s_snap, 0, sizeof(s_snap));
    s_snap.max_channels = CONFIG_PCCTL_RELAY_MAX_CHANNELS;
    for (int i = 0; i < CONFIG_PCCTL_RELAY_MAX_CHANNELS; i++) {
        s_snap.channels[i].gpio = -1;
        s_snap.channels[i].active_level = PC_CFG_LEVEL_HIGH; /* pc_config default */
        s_snap.channels[i].startup_policy = PC_RELAY_STARTUP_OFF;
    }
}

/* Caller buffer must be NUL-terminated. Control characters are rejected;
 * UTF-8 multi-byte sequences (>= 0x80) pass through. */
static bool name_ok(const char *name) {
    size_t n = strlen(name);
    if (n > PC_RELAY_NAME_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c < 0x20 || c == 0x7F) return false;
    }
    return true;
}

/* Blob copy: must also be NUL-terminated WITHIN the fixed array. */
static bool blob_name_ok(const char *name) {
    if (!memchr(name, '\0', PC_RELAY_NAME_MAX + 1)) return false;
    return name_ok(name);
}

static int configured_pin_count(const pc_relay_snapshot_t *snap) {
    int n = 0;
    for (int i = 0; i < CONFIG_PCCTL_RELAY_MAX_CHANNELS; i++) {
        if (snap->channels[i].gpio >= 0) n++;
    }
    return n;
}

/* Drive one pin to its idle (de-energized) level. The idle level is
 * pre-latched BEFORE the output driver is enabled, so enabling a pin can
 * never emit an assert pulse (the pc_hw.c pulse() technique). */
static void drive_idle(int pin, uint8_t level) {
    if (pin < 0) return;
    const int idle = (level == PC_CFG_LEVEL_HIGH) ? 0 : 1;
    gpio_set_level(pin, idle);
    esp_err_t err = gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO%d idle drive failed: %s", pin, esp_err_to_name(err));
        return;
    }
    gpio_set_level(pin, idle);
}

static void drive_all_idle(const pc_relay_snapshot_t *snap) {
    for (int i = 0; i < CONFIG_PCCTL_RELAY_MAX_CHANNELS; i++) {
        if (snap->channels[i].gpio >= 0) {
            drive_idle(snap->channels[i].gpio, snap->channels[i].active_level);
        }
    }
}

/* Load the NVS blob: every field is validated; an invalid channel entry is
 * parked as NC + disabled instead of rejecting the whole configuration
 * (recovery without erasing NVS). Cross-checks keep relay pins unique and
 * disjoint from the PC-control function pins. */
static void load_blob(const pc_relay_blob_t *blob, int slots) {
    pc_config_snapshot_t cfg;
    pc_config_get(&cfg);

    /* channel_count can only refer to slots actually present in the blob */
    s_snap.channel_count = (blob->channel_count > (uint8_t)slots)
                               ? (uint8_t)slots : blob->channel_count;
    uint8_t recovered = 0;
    for (int i = 0; i < CONFIG_PCCTL_RELAY_MAX_CHANNELS; i++) {
        pc_relay_channel_t *ch = &s_snap.channels[i];
        if (i >= slots) continue;   /* not stored - keep load_defaults() values */
        const pc_relay_blob_channel_t *bc = &blob->channels[i];
        bool ok = blob_name_ok(bc->name) &&
                  (bc->enabled == 0 || bc->enabled == 1) &&
                  (bc->gpio == -1 || pc_config_gpio_allowed(bc->gpio)) &&
                  (bc->active_level == PC_CFG_LEVEL_HIGH ||
                   bc->active_level == PC_CFG_LEVEL_LOW) &&
                  bc->startup_policy == PC_RELAY_STARTUP_OFF;
        if (ok && bc->gpio >= 0) {
            for (int j = 0; j < i && ok; j++) {
                if (s_snap.channels[j].gpio == bc->gpio) ok = false;
            }
            for (int s = 0; s < PC_CFG_GPIO_SLOTS && ok; s++) {
                if (cfg.gpio[s] == bc->gpio) ok = false;
            }
        }
        if (!ok) {
            recovered++;
            memset(ch, 0, sizeof(*ch));
            ch->gpio = -1;
            ch->active_level = PC_CFG_LEVEL_HIGH;
            ch->startup_policy = PC_RELAY_STARTUP_OFF;
            continue;
        }
        snprintf(ch->name, sizeof(ch->name), "%s", bc->name);
        ch->enabled = (bc->enabled != 0);
        ch->gpio = bc->gpio;
        ch->active_level = bc->active_level;
        ch->startup_policy = bc->startup_policy;
    }
    if (recovered) {
        ESP_LOGW(TAG, "%u invalid relay channel entry(ies) parked as NC + disabled",
                 recovered);
    }
    ESP_LOGI(TAG, "relay config from NVS: %u channels, %d pins configured",
             s_snap.channel_count, configured_pin_count(&s_snap));
}

void pc_relay_init(void) {
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    load_defaults();

    if (nvs_open(NVS_NS, NVS_READWRITE, &s_nvs) == ESP_OK) {
        s_nvs_ok = true;
    } else {
        ESP_LOGW(TAG, "NVS namespace '%s' open failed - defaults apply", NVS_NS);
    }

    pc_relay_blob_t blob;
    memset(&blob, 0, sizeof(blob));
    size_t len = sizeof(blob);
    esp_err_t err = s_nvs_ok ? nvs_get_blob(s_nvs, NVS_KEY_RELAYS, &blob, &len)
                             : ESP_ERR_NVS_NOT_FOUND;
    /* The v1 blob is a header (version + channel_count) followed by N packed
     * channel slots. Blobs written when CONFIG_PCCTL_RELAY_MAX_CHANNELS was
     * smaller are accepted as well (their missing slots keep the defaults),
     * so raising the channel limit never discards an existing configuration;
     * nothing is written back until the next explicit save. */
    size_t hdr = sizeof(blob.version) + sizeof(blob.channel_count);
    size_t slot = sizeof(blob.channels[0]);
    int slots = 0;
    if (err == ESP_OK && len >= hdr && blob.version == RELAY_BLOB_VERSION &&
        (len - hdr) % slot == 0 &&
        (len - hdr) / slot <= CONFIG_PCCTL_RELAY_MAX_CHANNELS) {
        slots = (int)((len - hdr) / slot);
    }
    if (slots > 0) {
        load_blob(&blob, slots);
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "no relay configuration in NVS - 0 channels until configured");
    } else {
        ESP_LOGW(TAG, "relay configuration in NVS invalid - defaults apply");
    }

    /* Safe electrical state BEFORE any control is possible: every
     * configured pin is driven to its idle (OFF) level. */
    drive_all_idle(&s_snap);

    ESP_LOGI(TAG, "relay manager ready: %u/%u channels, %d pins held OFF",
             s_snap.channel_count, s_snap.max_channels,
             configured_pin_count(&s_snap));
}

void pc_relay_get(pc_relay_snapshot_t *out) {
    if (!out) return;
    lock();
    *out = s_snap;
    unlock();
}

bool pc_relay_gpio_in_use(int pin) {
    if (pin < 0) return false;
    bool used = false;
    lock();
    for (int i = 0; i < CONFIG_PCCTL_RELAY_MAX_CHANNELS; i++) {
        if (s_snap.channels[i].gpio == pin) {
            used = true;
            break;
        }
    }
    unlock();
    return used;
}

bool pc_relay_channel_available(const pc_relay_snapshot_t *snap, uint8_t id,
                                const char **reason) {
    static const char *r_unknown  = "unknown relay channel";
    static const char *r_disabled = "disabled in device Settings";
    static const char *r_nc       = "GPIO = NC (not configured) - set one in Settings";
    if (reason) *reason = NULL;
    if (!snap || id >= snap->channel_count) {
        if (reason) *reason = r_unknown;
        return false;
    }
    const pc_relay_channel_t *ch = &snap->channels[id];
    if (!ch->enabled) {
        if (reason) *reason = r_disabled;
        return false;
    }
    if (ch->gpio < 0) {
        if (reason) *reason = r_nc;
        return false;
    }
    return true;
}

esp_err_t pc_relay_set_state(uint8_t id, bool on) {
    lock();
    if (id >= s_snap.channel_count) {
        unlock();
        return ESP_ERR_INVALID_ARG;
    }
    pc_relay_channel_t *ch = &s_snap.channels[id];
    if (!ch->enabled || ch->gpio < 0) {
        unlock();
        return ESP_ERR_INVALID_STATE;
    }
    const int idle_lv   = (ch->active_level == PC_CFG_LEVEL_HIGH) ? 0 : 1;
    const int assert_lv = idle_lv ^ 1;
    /* Pre-latch the target level before (re)asserting the output driver:
     * glitch-free for a pin not yet driven; a plain transition for an
     * already-driven pin (that transition IS the commanded action). */
    gpio_set_level(ch->gpio, on ? assert_lv : idle_lv);
    esp_err_t err = gpio_set_direction(ch->gpio, GPIO_MODE_OUTPUT);
    if (err == ESP_OK) {
        err = gpio_set_level(ch->gpio, on ? assert_lv : idle_lv);
    }
    if (err == ESP_OK) {
        ch->state = on;
        ESP_LOGI(TAG, "relay %u -> %s (GPIO%d, %s)",
                 id, on ? "ON" : "OFF", ch->gpio,
                 ch->active_level == PC_CFG_LEVEL_HIGH ? "active HIGH" : "active LOW");
    } else {
        ESP_LOGE(TAG, "relay %u GPIO%d drive failed: %s", id, ch->gpio,
                 esp_err_to_name(err));
    }
    unlock();
    return err;
}

/* Persist the whole configuration in ONE versioned blob write. The NVS
 * namespace "pc_ctl" and its other keys ("hw", "auth", "api_token") are
 * never touched, and nothing is written outside explicit user saves. */
static esp_err_t persist(const pc_relay_snapshot_t *snap) {
    if (!s_nvs_ok) return ESP_ERR_NVS_NOT_INITIALIZED;
    pc_relay_blob_t blob;
    memset(&blob, 0, sizeof(blob));
    blob.version = RELAY_BLOB_VERSION;
    blob.channel_count = snap->channel_count;
    for (int i = 0; i < CONFIG_PCCTL_RELAY_MAX_CHANNELS; i++) {
        const pc_relay_channel_t *ch = &snap->channels[i];
        snprintf(blob.channels[i].name, sizeof(blob.channels[i].name), "%s", ch->name);
        blob.channels[i].enabled = ch->enabled ? 1 : 0;
        blob.channels[i].gpio = ch->gpio;
        blob.channels[i].active_level = ch->active_level;
        blob.channels[i].startup_policy = ch->startup_policy;
    }
    esp_err_t err = nvs_set_blob(s_nvs, NVS_KEY_RELAYS, &blob, sizeof(blob));
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    return err;
}

esp_err_t pc_relay_config_apply(const pc_relay_config_patch_t *patch) {
    if (!patch) return ESP_ERR_INVALID_ARG;

    pc_relay_snapshot_t old_snap, next;
    lock();
    old_snap = s_snap;
    unlock();
    next = old_snap;

    if (patch->set_channel_count) {
        if (patch->channel_count > CONFIG_PCCTL_RELAY_MAX_CHANNELS) {
            return ESP_ERR_INVALID_ARG;
        }
        next.channel_count = patch->channel_count;
    }

    for (int i = 0; i < CONFIG_PCCTL_RELAY_MAX_CHANNELS; i++) {
        if (!patch->set_channels[i]) continue;
        const pc_relay_channel_patch_t *p = &patch->channels[i];
        pc_relay_channel_t *ch = &next.channels[i];
        if (p->set_name) {
            if (!name_ok(p->name)) return ESP_ERR_INVALID_ARG;
            snprintf(ch->name, sizeof(ch->name), "%s", p->name);
        }
        if (p->set_enabled) ch->enabled = p->enabled;
        if (p->set_gpio) {
            if (p->gpio != -1 && !pc_config_gpio_allowed(p->gpio)) {
                return ESP_ERR_INVALID_ARG;
            }
            ch->gpio = (int8_t)p->gpio;
        }
        if (p->set_level) {
            if (p->level != PC_CFG_LEVEL_LOW && p->level != PC_CFG_LEVEL_HIGH) {
                return ESP_ERR_INVALID_ARG;
            }
            ch->active_level = (uint8_t)p->level;
        }
        if (p->set_startup) {
            if (p->startup != PC_RELAY_STARTUP_OFF) return ESP_ERR_INVALID_ARG;
            ch->startup_policy = (uint8_t)p->startup;
        }
    }

    /* GPIO ownership: unique across relay channels, disjoint from the
     * PC-control function pins (pc_config.c re-checks the other direction
     * on its own save path). */
    pc_config_snapshot_t cfg;
    pc_config_get(&cfg);
    for (int a = 0; a < CONFIG_PCCTL_RELAY_MAX_CHANNELS; a++) {
        int pin = next.channels[a].gpio;
        if (pin < 0) continue;
        for (int s = 0; s < PC_CFG_GPIO_SLOTS; s++) {
            if (cfg.gpio[s] == pin) return ESP_ERR_INVALID_STATE;
        }
        for (int b = a + 1; b < CONFIG_PCCTL_RELAY_MAX_CHANNELS; b++) {
            if (next.channels[b].gpio == pin) return ESP_ERR_INVALID_STATE;
        }
    }

    /* Unsafe reduction: a channel dropped by a lower channel_count must be
     * idle (NC + disabled) already - GPIO mappings are never silently
     * removed. Raise the count, clear the channel, then reduce. */
    if (next.channel_count < old_snap.channel_count) {
        for (int i = next.channel_count; i < old_snap.channel_count; i++) {
            if (next.channels[i].gpio >= 0 || next.channels[i].enabled) {
                return ESP_ERR_NOT_ALLOWED;
            }
        }
    }

    /* A configuration change always lands in the safe state: the commanded
     * state is RAM-only and resets to OFF (it is never persisted anyway). */
    for (int i = 0; i < CONFIG_PCCTL_RELAY_MAX_CHANNELS; i++) {
        next.channels[i].state = false;
    }

    /* Persist first; apply to RAM and hardware only after the blob is
     * stored, so a failed write changes nothing. */
    esp_err_t err = persist(&next);
    if (err != ESP_OK) return err;

    lock();
    s_snap = next;
    unlock();

    /* Pins that LEFT the configuration are parked at an idle level under
     * their OLD mapping; then every current pin is driven idle under the
     * NEW mapping. Only idle levels are ever driven here - a configuration
     * change can never produce an assert pulse. */
    for (int i = 0; i < CONFIG_PCCTL_RELAY_MAX_CHANNELS; i++) {
        const pc_relay_channel_t *o = &old_snap.channels[i];
        const pc_relay_channel_t *n = &next.channels[i];
        if (o->gpio >= 0 && o->gpio != n->gpio) {
            drive_idle(o->gpio, o->active_level);
        }
    }
    drive_all_idle(&next);

    ESP_LOGI(TAG, "relay config applied: %u channels, %d pins held OFF",
             next.channel_count, configured_pin_count(&next));
    return ESP_OK;
}
