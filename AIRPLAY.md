# System AirPlay output toggle

In Music and Collection, **O** toggles the Mac's system audio between the configured AirPlay speaker and the previous output. Existing keys, buttons and layout are unchanged. This routes system audio, not just the music app. Radio and Companion do not use this shortcut.

The Bridge uses its existing `FacesMusicHelper.app`. Configure the exact speaker name in the runtime's ignored `.local/airplay.json`:

```json
{"target": "Your speaker name"}
```

Build/install the helper with `tools/build_music_helper.sh`. The helper needs macOS Accessibility permission, including when launched by the background Bridge. On macOS 27 this permission appears under Privacy & Security > Device Control & Data Access (设备控制和数据访问). Permission available to a terminal or coding app does not prove the background helper is authorized. Re-check this after rebuilding an ad-hoc signed helper.

Connecting opens System Settings > Sound > Output. The helper matches the exact name through accessibility hit testing within the output table and selects the resulting row. It does not click fixed screen coordinates. After dispatching the selection, it immediately hides Settings and restores the previous foreground app while the connection continues. It also cleans up on discovery errors. If Settings was already your foreground app, it stays available; if you switch to another app during discovery, the helper does not steal focus back. Keep the output list unobstructed until the selection is sent. The implementation was validated on macOS 27.0.1; system UI changes may require adaptation. Switching back uses the saved Core Audio device UID.

The authenticated `POST /api/audio/toggle` endpoint accepts a `request_id` of 8–80 characters. Requests are single-flight, and a bounded persistent replay journal prevents recent repeated requests from toggling twice, including across Bridge restarts. An uncertain request is not automatically retried. A manually changed AirPlay route is not treated as owned by the Bridge; an unavailable original output is not replaced by a guessed device.

The helper can also be diagnosed directly:

```sh
FacesMusicHelper --doctor
FacesMusicHelper --audio status
FacesMusicHelper --audio select "Your speaker name"
FacesMusicHelper --audio restore "previous-device-uid"
```

Save the `output.uid` from `status` before selecting a speaker. Audio route configuration and state stay local; do not publish them. Building firmware and passing tests do not establish that the device has been updated or the physical O key has been accepted by its owner.
