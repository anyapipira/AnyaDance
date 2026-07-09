# Changelog

## Unreleased

- Unregister Driver no longer opens the Restart SteamVR confirmation; unregistering only edits configuration files, and the status line already points to the separate Restart SteamVR button to apply the change.
- The empty body-panel area now manipulates the whole rig: middle mouse drag rotates all six devices (yaw/pitch) about the HMD position, middle+right rolls them, and right mouse drag alone moves the rig vertically (clamped to the 2 m ceiling).
- A right mouse drag on the HMD box now moves the HMD vertically, matching the rig gesture (the previous left+right chord still works).
- Fixed the window width being reset when restoring from minimized: the minimum-size hint no longer measures the window frame while the window is iconic.

## Initial public release

- Windows SteamVR/OpenVR virtual-device driver with six devices: HMD, two controllers, hip tracker, and two foot trackers.
- Bundled Dear ImGui UI for 60 Hz pose streaming, mouse manipulation, controller input capture, driver registration, SteamVR restart, UDP log inspection, and MMD dance playback.
- UDP JSON protocol with safety clamps, stale-packet freshness behavior, T-pose defaults, localized UI strings, copy/resend helpers, and focused native tests.
- Build, package, register, unregister, and restart scripts, plus public docs, licensing notices, Simplified Chinese README, and user safety disclaimer.
