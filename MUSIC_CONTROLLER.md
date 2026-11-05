# Faces Music Remote and Collection Shelf

Faces controls music playing on your Mac; it neither stores an account nor
decodes music on the CoreS3. The optional local Bridge provides a token-protected
browser preview and the device API. It generates its pairing token in ignored
`.local/music.json` on first run.

## Collection setup

Collection is available only for NetEase Cloud Music. Install and authenticate
the official `ncm-cli` on the Mac running the Bridge. The Bridge resolves it
from its process `PATH`, or from `FACES_NCM_CLI` when that local environment
variable names an absolute executable. This setting belongs in your local
service configuration and must not be committed.

The Bridge reads the account's collected playlists asynchronously. It removes
playlists created by the same account, including the special Liked Songs row.
Playlist data, artwork and cache entries stay in memory. Artwork downloads are
restricted to validated NetEase CDN URLs, limited in size, and held in bounded
caches. A missing login, CLI, or network connection is a visible error state;
it is never silently treated as an empty collection.

## Controls

| View | Keyboard | Touch |
| --- | --- | --- |
| Home | `M` Now Playing, `P` Collection | Left Music-card region opens Now Playing; right record sleeve opens Collection. |
| Now Playing | `P` Collection | Use the Collection pill beside Favourite. |
| Collection | `J` / `K` browse, `R` refresh, `Space` / Enter request playback, `Q` / Esc home | Swipe or tap side covers to browse; only the centred cover or confirm button requests playback. |

Five covers remain visible while moving. Repeated and reverse browsing preserves
the current motion; a drag suppresses the following click. A successful request
means the CLI accepted one dispatch to the Mac player, not that audible playback
has been independently confirmed. Requests are never retried automatically.

## Local preview

Build the helper and start the Bridge locally:

```sh
./tools/build_music_helper.sh
./tools/start_music.command
```

For a paired device on a trusted LAN, use `./tools/start_music.command --lan`
and configure its own Wi-Fi, Bridge address, port and pairing token through
Connection Center. Do not expose the Bridge to the public internet.
