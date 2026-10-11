# Partition Tables (dual-OTA, shared across examples)

ESP-IDF partition tables for MicroLink v2, kept at the repository root so every
example in `examples/` can share them:

- **Dual OTA app slots** (`ota_0` / `ota_1`) — every board can update the app
  over the air. There is no `factory` partition; the bootloader boots `ota_0`
  when otadata is empty (first flash works out of the box).
- **NVS = 128 KB** — MicroLink keeps a lot of state in NVS:
  - `ml_config` peer allowlist: up to 1024 entries ≈ 29 KB (DISCO probe filter)
  - `ml_peers` DISCO peer cache: up to 1024 entries ≈ 94 KB
  - WiFi list (16 networks), settings blob, Tailscale machine/WG keys ≈ 2 KB
  - 128 KB covers the worst case with headroom; the previous 16 KB / 24 KB NVS
    partitions do not fit the default configuration.
- **`assets`** — a spiffs partition for network-loadable content, sized to fill
  the remaining flash of each configuration.

## Available configurations

| File | Flash | nvs | `ota_0` / `ota_1` | assets |
|------|-------|-----|-------------------|--------|
| `4m.csv`  | 4 MB  | 128 KB | 1.5 MB each | 832 KB    |
| `8m.csv`  | 8 MB  | 128 KB | 2 MB each   | 3904 KB   |
| `16m.csv` | 16 MB | 128 KB | 4 MB each   | 8000 KB   |
| `32m.csv` | 32 MB | 128 KB | 5 MB each   | 22336 KB  |

All offsets/sizes respect the ESP-IDF partition alignment rules (app partition
offsets are 64 KB aligned, data partitions 4 KB aligned).

## Using a table in an example

Add these three lines to the example's `sdkconfig.defaults` (replace the
`CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y` line if present). The path is
resolved relative to the example's project directory, so `../../partitions/`
points at this folder:

```ini
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="../../partitions/16m.csv"
```

Keep the flash size and the CSV filename **paired** (4 MB with `4m.csv`,
8 MB with `8m.csv`, and so on). The build fails loudly on a mismatch:

```
Partitions tables occupies 16.0MB of flash (16777216 bytes) which does not
fit in configured flash size 8MB.
```

`CONFIG_ESPTOOLPY_FLASHSIZE_*` is a Kconfig choice — keep exactly one line.
After changing these options on an existing build, delete the generated
`sdkconfig` (regenerated from `sdkconfig.defaults`) or update via
`idf.py menuconfig`, then rebuild.

## Migrating an already-flashed device (SINGLE_APP_LARGE -> dual-OTA layout)

The `nvs` partition stays at `0x9000`, so existing NVS data *can* survive the
switch — but **back it up first**, because an empty NVS silently re-creates
itself in two ways:

1. The area the new layout reclaims (`0xF000-0x2FFFF`) still holds leftovers of
   the old layout (old phy_init + old factory app). If those corrupt pages
   reach `nvs_flash_init()`, the standard `ESP_ERR_NVS_NO_FREE_PAGES` /
   `ESP_ERR_NVS_NEW_VERSION_FOUND` fallback (present in most examples,
   including `basic_connect/main.c`) silently erases and re-creates an empty
   NVS — losing WiFi list, settings and Tailscale keys.
2. Any full `idf.py erase-flash` you ran earlier already wiped it.

Safe migration recipe (all steps are fast):

```bash
# 1. Back up the old NVS partition (SINGLE_APP_LARGE: 0x6000 bytes at 0x9000)
python -m esptool -p COMx read-flash 0x9000 0x6000 nvs_backup.bin

# 2. Erase the region the new layout reclaims (old phy_init + old factory app).
#    NVS data (0x9000-0xEFFF) is NOT touched by this.
python -m esptool -p COMx erase-region 0xF000 0x21000

# 3. Flash the new firmware (bootloader + partition table + app in ota_0)
idf.py -p COMx flash

# 4. Restore the NVS backup (re-writes the original pages at 0x9000)
python -m esptool -p COMx write-flash 0x9000 nvs_backup.bin
```

Step 4 is harmless even if step 3's first boot already wiped NVS — it simply
puts the original data back (the reset at the end of the command applies it).
Skip the backup/restore only if you accept re-provisioning WiFi + auth key
afterwards (the device falls back to setup mode when no credentials remain).
