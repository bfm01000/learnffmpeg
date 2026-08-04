# third_party

This directory keeps project-facing entries for large external dependencies.

## WebRTC checkout

```text
third_party/webrtc-checkout -> /home/bfm01000/workspace/third_party/webrtc-checkout
```

The actual checkout is kept outside the project directory to avoid vendoring a 16G+ source tree into this interview project. The symlink makes it convenient to navigate from the project while preserving the safer external checkout layout.

Important binaries:

```text
third_party/webrtc-checkout/src/out/Default/peerconnection_server
third_party/webrtc-checkout/src/out/Default/peerconnection_client
```