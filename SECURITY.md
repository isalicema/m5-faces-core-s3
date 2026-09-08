# Security notes

Do not commit `.local/`, exported firmware images, flash backups, serial logs,
or copied device preferences. These can contain pairing tokens, Wi-Fi
credentials, device identifiers, and personal playback metadata.

The music bridge listens on localhost by default. Its LAN mode requires a
pairing token and should only be used on a trusted network. If a token may have
been exposed, delete `.local/music.json` while the bridge is stopped and start
it again to create a replacement.

To report a security issue privately, use the repository's GitHub security
advisory flow rather than publishing credentials or reproduction data in an
issue.
