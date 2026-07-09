# Changelog

## Unreleased

- Unregister Driver no longer opens the Restart SteamVR confirmation; unregistering only edits configuration files, and the status line already points to the separate Restart SteamVR button to apply the change.

## Initial public release

- Windows SteamVR/OpenVR virtual-device driver with six devices: HMD, two controllers, hip tracker, and two foot trackers.
- Bundled Dear ImGui UI for 60 Hz pose streaming, mouse manipulation, controller input capture, driver registration, SteamVR restart, UDP log inspection, and MMD dance playback.
- UDP JSON protocol with safety clamps, stale-packet freshness behavior, T-pose defaults, localized UI strings, copy/resend helpers, and focused native tests.
- Build, package, register, unregister, and restart scripts, plus public docs, licensing notices, Simplified Chinese README, and user safety disclaimer.
