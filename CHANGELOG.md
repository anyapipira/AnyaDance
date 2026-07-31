# Changelog

## Unreleased

- Unregister Driver updates the configuration immediately; the separate Restart SteamVR button applies the change.
- The empty body-panel area manipulates the whole rig: middle mouse drag rotates all six devices (yaw/pitch) about the HMD position, middle+right rolls them, and right mouse drag moves the rig vertically within the shared `0–25 m` Y range.
- A right mouse drag on the HMD box moves the HMD vertically; the left+right chord provides the same gesture.
- All device manipulation and loaded poses use the shared `0–25 m` Y range.
- Window restoration measures a realized client area before updating its minimum-size hint, preserving the restored width.

## Initial public release

- Windows SteamVR/OpenVR virtual-device driver with six devices: HMD, two controllers, hip tracker, and two foot trackers.
- Bundled Dear ImGui UI for 60 Hz pose streaming, mouse manipulation, controller input capture, driver registration, SteamVR restart, UDP log inspection, and MMD dance playback.
- UDP JSON protocol with safety clamps, stale-packet freshness behavior, T-pose defaults, localized UI strings, copy/resend helpers, and focused native tests.
- Build, package, register, unregister, and restart scripts, plus public docs, licensing notices, Simplified Chinese README, and user safety disclaimer.
