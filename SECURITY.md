# Security

Report security issues privately by email to `anyapipira@gmail.com`. Do not open
a public issue for a suspected vulnerability. Include reproduction steps, the
affected version, and the potential impact when possible.

The driver intentionally binds UDP only to loopback (`127.0.0.1:39570`). It does not authenticate packets and does not send acknowledgements. Do not expose the UDP socket on a public or LAN interface.

The companion UI does not install global keyboard hooks. It reads key state only while its window has foreground focus and neutralizes inputs on focus loss.
