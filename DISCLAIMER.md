# Disclaimer

AnyaDance is provided for legitimate, authorized testing and development
only.

Feeding virtual devices or spoofed tracking into a live online game may violate
that game's Terms of Service and can be detected by its anti-cheat system, which
may result in the suspension or permanent ban of your account.

Registering the driver changes your SteamVR configuration: it puts SteamVR into a
fully virtual mode and writes to `steamvr.vrsettings`, so while the driver is
registered your real headset, controllers, and trackers will not be tracked (a
backup is made, and unregistering restores it). The virtual HMD also continuously
renders both eyes through the SteamVR compositor, which consumes additional GPU
and CPU; raising the render resolution increases that load further.

You use this software entirely at your own risk. It is provided "as is" without
warranty of any kind, and the authors accept no responsibility or liability for
any consequences of use or misuse, including account bans or loss of access.

This project is not affiliated with or endorsed by VRChat, Valve, Steam, or
SteamVR. All trademarks belong to their respective owners.

This notice is a safety acknowledgment only. It does not modify the Apache
License 2.0 or impose additional restrictions on use, modification, or
redistribution. The companion UI (`AnyaDance.exe`) displays the notice and asks
you to acknowledge it on first launch.
