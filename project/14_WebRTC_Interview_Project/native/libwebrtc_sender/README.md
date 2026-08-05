# Native libwebrtc Sender

This directory is reserved for the Phase 3 native libwebrtc sender demo.

The implementation is intentionally not started yet because the current WSL environment is missing the libwebrtc build toolchain:

- `ninja`
- `gn`
- `fetch`
- `gclient`
- `clang / clang++`
- local WebRTC checkout

See:

```text
docs/phase3/native-libwebrtc-sender-design.md
docs/phase3/native-signaling-protocol.md
docs/phase3/libwebrtc-env-check.md
```

Recommended first runnable target after the toolchain is ready:

```text
synthetic I420 frame -> libwebrtc VideoTrackSource -> PeerConnection -> browser receiver
```

Do not vendor the full WebRTC source tree into this project directory. Keep it in an external checkout such as:

```text
/home/bfm01000/workspace/third_party/webrtc-checkout
```
## I420 file source

Generate a reusable I420 segment from the unified FRXXZ sample:

```bash
cd /home/bfm01000/workspace/learnffmpeg/project/14_WebRTC_Interview_Project
scripts/prepare_frxxz_i420.sh 200 320 180
```

Run native sender with real decoded frames:

```bash
scripts/run_native_i420_sender.sh lab 320 180 25 200
```

Open `http://localhost:3000`, keep room `lab`, then click Join.