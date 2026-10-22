# Streamlight

Streamlight is a self-hosted desktop stream host for [Artemis (Moonlight Noir)](https://github.com/ClassicOldSong/moonlight-android). It offers low latency, native client resolution and hardware encoding on AMD, Intel and NVIDIA GPUs, with software encoding as a fallback. A web UI lets you configure Streamlight and pair clients from any browser, including from a phone.

It is a personal fork of [Apollo](https://github.com/ClassicOldSong/Apollo), which is itself based on Sunshine.

## Features

- Built-in virtual display with HDR support that matches your client's resolution and framerate
- Permission management for paired clients
- Clipboard sync
- Commands that run on client connect and disconnect
- Input-only mode
- Web UI for configuration, app list management and client pairing

## Notes

### Permissions

The first client paired with Streamlight gets full permissions. Other clients get only `View Streams` and `List Apps`. If a client gets `Permission Denied` when launching an app, grant it `Launch Apps` in the web UI. If it can't move the mouse or type, grant `Mouse Input` and `Keyboard Input`.

### Virtual display

The virtual display uses SudoVDA and is Windows only. It is created when a stream starts and removed when the app quits. Each client gets a fixed identity, so Windows remembers its display configuration.

Remove any other virtual display software from your system and from the Streamlight config to avoid conflicts. If no virtual display appears when a stream starts, check the driver or look for a persistent virtual display that is still active.

### Dual GPU laptops

To render on the dGPU, set `Adapter Name` to the dGPU and enable `Headless mode` in the `Audio/Video` tab, then save and restart. A dummy plug is not needed.

### HDR

HDR needs Windows 11 23H2 or later, and works best on 24H2. Whether HDR looks right depends on the client display. Apple devices with HDR tend to work well. Other clients vary.

## Integrations

- SudoVDA: virtual display adapter driver
- [Artemis](https://github.com/ClassicOldSong/moonlight-android): controls virtual display options from the client (Android only for now)

## License

GPLv3. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
