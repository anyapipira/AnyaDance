# Changelog

## Unreleased

- The driver emits versioned command-processing reports over a configurable, loopback-only UDP multicast group, allowing the UI and multiple external processes to monitor the same events without increasing driver send work. The UI can hot-toggle its listener and avoids duplicate successful-send rows.
- The two virtual controllers now expose an `/output/haptic` component, and the driver reports every haptic request SteamVR routes to them on the existing driver log multicast group as a `haptic_vibration` event carrying the controller, duration, frequency, and amplitude. The driver has no motor and plays nothing back; the report lets external tools drive indicators or forward to physical hardware. Gated by `haptic_log_enabled` (default true); the component is always created, so the switch never changes what SteamVR or a game sees. Readers must dispatch on the `event` field, since the group now carries two event types. Resolves #9.
- Driver command reports are limited to commands that change what is asked of the devices, so holding a pose no longer floods the log at the stream rate. Distinct commands are still reported individually, and the number of identical commands absorbed while a pose was held is carried in the report's `suppressed` field.
- Unregister Driver updates the configuration immediately; the separate Restart SteamVR button applies the change.
- Only the HMD is held above the ground plane. Its Y range stays `0–25 m`, while the controllers, hip, and feet may go below zero and are bounded only by the shared `±30 m` position range, so poses and dances that put a foot under the floor or a hip in a floor move are no longer flattened. The rig's vertical move now stops on the way down when the HMD reaches `0 m`.
- The empty body-panel area manipulates the whole rig: middle mouse drag rotates all six devices (yaw/pitch) about the HMD position, middle+right rolls them, and right mouse drag moves the rig vertically.
- A right mouse drag on the HMD box moves the HMD vertically; the left+right chord provides the same gesture.
- All device manipulation and loaded poses use the same per-device Y range as the protocol.
- Window restoration measures a realized client area before updating its minimum-size hint, preserving the restored width.

## Initial public release

- Windows SteamVR/OpenVR virtual-device driver with six devices: HMD, two controllers, hip tracker, and two foot trackers.
- Bundled Dear ImGui UI for 60 Hz pose streaming, mouse manipulation, controller input capture, driver registration, SteamVR restart, UDP log inspection, and MMD dance playback.
- UDP JSON protocol with safety clamps, stale-packet freshness behavior, T-pose defaults, localized UI strings, copy/resend helpers, and focused native tests.
- Build, package, register, unregister, and restart scripts, plus public docs, licensing notices, Simplified Chinese README, and user safety disclaimer.
