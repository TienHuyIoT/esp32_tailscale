# Implementation Plan: Relay Control for ESP32-S3 PC Controller

**Platform:** ESP32-S3, ESP-IDF 6.1  
**Existing connectivity:** MicroLink + Tailscale  
**Web UI:** Existing `/pc` page  
**Persistence:** Existing NVS configuration mechanism  
**Hardware:** Existing relay module, supporting AC/DC loads

> **Status (implementation):** implemented in the firmware -
> `examples/pc_control/main/pc_relay.[ch]` (Relay Manager, NVS namespace
> `pc_ctl` / key `relays`, safe-start GPIO handling), API
> `GET/POST /api/v1/relays` + `POST /api/v1/relays/config`, UI sections on
> `/pc` (control) and `/pc/settings` (configuration). Runtime contract:
> `docs/PC_CONTROLLER.md` ("Relay control"), usage:
> `examples/pc_control/README.md`. The Windows Python service is gone and
> no relay path depends on it.

## 1. Objective

Add a configurable, extensible Relay Control feature to the existing ESP32-S3 PC Controller.

Users must be able to manage relay channels from the existing `/pc` web UI, including channel names, Enable/Disable, GPIO assignment, Active-HIGH/Active-LOW logic, and ON/OFF commands.

This is an incremental feature addition. Inspect the repository first and reuse existing infrastructure.

## 2. Mandatory Architecture Constraints

### Preserve all existing functionality

Keep the existing implementation of:

- `/pc` web UI and its current request/response flow.
- MicroLink and Tailscale connectivity.
- USB HID Keyboard and USB HID Login.
- All six existing PC control functions: POWER_ON, WAKE_UP, PWR_RESET, and FORCE_POWER_OFF.
- Existing GPIO configuration, including Active-HIGH/Active-LOW options.
- Existing GPIO allocation rules, NVS configuration, authentication, and other features added during development with Cline.

### Windows Python Service is removed

The Windows Python Service has already been removed by Cline at the user's request.

- Do not recreate it.
- Do not introduce dependencies on it.
- Do not route relay commands through Windows.
- Handle relay commands in the existing ESP32-S3 firmware request/API path.

### No unnecessary rewrites

- Do not replace or redesign `/pc`.
- Do not rewrite MicroLink or Tailscale.
- Do not rewrite USB HID.
- Do not simplify or remove existing GPIO features.
- Do not replace the existing NVS configuration mechanism.
- Do not reintroduce obsolete mock-only restrictions for existing PC control functions.

## 3. Phase 1 — Inspect the Existing Repository

Before modifying source code, identify:

1. The component serving `/pc`.
2. The actual handler for existing PC control requests.
3. The current GPIO registry and pin validation mechanism.
4. How Active-HIGH/Active-LOW logic is represented and applied.
5. How GPIO ownership conflicts are prevented, if already implemented.
6. The NVS configuration schema, namespace, versioning, and migration strategy.
7. Existing synchronization mechanisms for GPIO access across tasks.
8. Available GPIO pins for the actual ESP32-S3 board.
9. Existing authentication and authorization checks for remote commands.

Report the relevant files, current architecture, and minimal proposed changes before implementation.

Treat the current source code as authoritative. Do not assume the repository matches an older plan.

## 4. Phase 2 — Relay Channel Model

Implement a Relay Manager with a configurable number of channels, subject to explicit firmware limits, available GPIO resources, and memory/NVS capacity.

Each relay channel should have:

- `id`: stable channel identifier.
- `name`: user-configurable display name.
- `enabled`: Enable/Disable.
- `gpio`: valid GPIO or NC, using the project's existing representation.
- `active_level`: Active-HIGH or Active-LOW, reusing the existing GPIO logic convention.
- `startup_policy`: safe startup behavior, defaulting to OFF.

Add further fields only when required by the existing implementation.

### Channel validation rules

- `GPIO = NC`: reject ON/OFF commands.
- `enabled = false`: reject ON/OFF commands.
- Invalid GPIO: reject configuration.
- GPIO already assigned to another function: reject assignment.
- GPIO reserved by USB, board hardware, or another peripheral: reject assignment.
- Do not change an existing function's GPIO mapping as a side effect of relay configuration.
- A configuration change must not generate an unintended relay pulse.

Do not assume GPIO numbers from a sample UI are valid on the actual board. Use the current board-specific GPIO validation mechanism.

## 5. Phase 3 — Relay Driver and State Management

Implement a hardware abstraction for relay outputs and keep it separate from the web UI.

Required operations:

- Initialize relay outputs safely.
- Set a channel ON.
- Set a channel OFF.
- Read the commanded logical state.
- Validate channel availability and permissions.
- Apply Active-HIGH/Active-LOW mapping correctly.
- Report configuration or GPIO errors.

All relay operations must go through the Relay Manager or the project's equivalent centralized GPIO-control mechanism.

Use existing synchronization facilities or introduce appropriate locking if necessary to prevent concurrent tasks from racing over GPIO state.

### Logical and electrical states

The API and UI should use logical ON/OFF states, independent of the electrical output polarity.

For example:

- Active-HIGH: logical ON maps to the configured active electrical level.
- Active-LOW: logical ON maps to the opposite electrical level.

Confirm the actual relay module's trigger polarity before applying this mapping. Do not assume all relay boards use the same polarity.

### Safe startup

- Initialize outputs to the configured safe electrical state before enabling control.
- Default to relay OFF after reboot.
- Do not restore ON states automatically unless a future requirement explicitly requests it and the behavior has been validated for the connected loads.
- Do not activate relays during GPIO reassignment, configuration loading, or error recovery.

Software cannot guarantee a safe output during every power transition unless the hardware design also supports it. Document any hardware limitations.

## 6. Phase 4 — NVS Persistence

Reuse the existing NVS infrastructure while keeping relay configuration separate from existing PC function settings.

Requirements:

- Persist relay channel configuration.
- Do not persist passwords or unrelated transient data.
- Do not write to NVS on every ON/OFF transition.
- Validate stored configuration at startup.
- Reject invalid or out-of-range GPIO mappings.
- Include schema versioning if needed.
- Preserve existing configuration keys and backward compatibility.
- Never erase or overwrite unrelated NVS namespaces as part of relay initialization.

If migration is necessary, implement it explicitly and report the migration behavior.

## 7. Phase 5 — API Integration

Extend the existing ESP32-S3 request/API handler used by `/pc`.

Do not introduce a new server or Windows dependency.

Use the existing API naming, response format, error handling, and authentication conventions. Inspect the code before selecting exact endpoint names.

Required capabilities:

- List configured relay channels and their logical states.
- Request ON for a channel.
- Request OFF for a channel.
- Read current relay configuration.
- Update permitted channel configuration if the existing UI architecture supports configuration changes.

Prefer explicit ON and OFF commands over a toggle-only API. Retried requests must not accidentally reverse the intended state.

Return distinct errors for:

- Unknown channel.
- Disabled channel.
- GPIO = NC.
- Invalid GPIO or GPIO conflict.
- Unauthorized request.
- Hardware or initialization failure.
- Invalid configuration.

Do not report that the physical load is confirmed ON/OFF unless hardware feedback is available. Without feedback, report the commanded output state.

## 8. Phase 6 — `/pc` UI Integration

Add a Relay Control section to the existing `/pc` page.

Each channel should display:

- Channel name.
- Logical ON/OFF state.
- Enable/Disable state.
- Configured GPIO.
- Active-HIGH/Active-LOW logic.
- ON and OFF controls.
- Configuration errors when applicable.

Provide configuration controls for channel count and individual channel settings only within the supported firmware limits.

Requirements:

- Reuse the existing page layout and API request mechanism.
- Follow the existing authentication model.
- Show pending, success, and error states.
- Prevent repeated clicks from generating overlapping commands.
- Disable controls when a channel is disabled or has GPIO = NC.
- Do not add a new web server or a separate management application.
- Do not expose unprotected configuration endpoints.

A channel-count setting must not silently remove or reassign existing GPIO mappings. Reject unsafe reductions or require explicit confirmation and validation.

## 9. Phase 7 — AC/DC Load Safety

The relay module is already available and may control AC or DC loads.

Software implementation requirements:

- Never infer load voltage or current from the channel configuration.
- Keep relay logic independent of whether the connected load is AC or DC.
- Validate GPIO startup behavior and active polarity.
- Avoid automatic restoration of energized loads after reboot.
- Do not treat GPIO state as proof of physical contact state.

Hardware validation must separately confirm that the relay contacts are rated for the intended AC/DC voltage, current, and load type. Inductive loads may require suitable suppression. AC mains wiring must use appropriate isolation, enclosure, and electrical safety practices.

Do not make hardware wiring changes automatically as part of this software task.

## 10. Phase 8 — Verification and Regression

Test each relay channel using the actual relay module.

### Relay tests

- [ ] Initialize every configured channel safely.
- [ ] Turn each channel ON and OFF.
- [ ] Verify Active-HIGH and Active-LOW behavior.
- [ ] Verify Enable/Disable enforcement.
- [ ] Verify GPIO = NC rejection.
- [ ] Verify invalid GPIO and GPIO conflict rejection.
- [ ] Verify channel configuration survives reboot.
- [ ] Verify safe startup after power loss or reset.
- [ ] Verify invalid NVS configuration recovery.
- [ ] Verify repeated and concurrent requests.
- [ ] Verify timeout, disconnected network, and UI error reporting.

### Existing-feature regression

- [ ] `/pc` remains accessible through the existing Tailscale path.
- [ ] MicroLink and Tailscale remain functional.
- [ ] USB HID Login remains functional.
- [ ] All six PC control functions remain operational.
- [ ] Existing GPIO H/L configuration remains unchanged.
- [ ] Existing NVS settings remain intact.
- [ ] USB COM/debug and existing development workflow remain functional.

Only mark a test complete after actual execution and evidence. Do not claim physical relay verification based only on a successful build.

## 11. Implementation and Reporting Rules

Implement incrementally:

1. Inspect and report existing architecture.
2. Propose the minimal file-level change set.
3. Implement the Relay Manager and GPIO validation.
4. Add NVS persistence.
5. Add API handlers.
6. Integrate the `/pc` UI.
7. Build, test, and run regression checks.

After each stage, report:

- Files changed.
- Key design decisions.
- Build result.
- Tests executed and their actual outcomes.
- Remaining issues.

Do not remove or refactor unrelated features.

## Completion Criteria

Relay Control is complete when configurable relay channels can be managed through the existing `/pc` page, ON/OFF commands operate the actual relay hardware with correct polarity, settings persist safely, invalid configurations are rejected, and all existing PC Controller functions remain operational.

The final implementation must not depend on a Windows Python Service.
