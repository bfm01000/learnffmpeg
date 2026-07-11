#!/usr/bin/env python3
"""
Download specific libwebrtc m120 source files for M4 + M6 study.
Source: webrtc.googlesource.com, branch-heads/6099 (m120).
Skips files that already exist (unless --force).
"""
import subprocess, sys, os, pathlib

BASE_URL = "https://webrtc.googlesource.com/src/+/refs/branch-heads/6099"
ROOT = pathlib.Path("/home/bfm01000/workspace/learnffmpeg/project/libwebrtc-src/m120")

# ── Files needed for M4 RTP module (§5 13 functions + deps) ──
M4_FILES = [
    # Already have most rtp_rtcp/source/, adding remaining:
    "modules/rtp_rtcp/source/rtp_sender.cc",
    "modules/rtp_rtcp/source/rtp_sender.h",
    "modules/rtp_rtcp/source/rtp_packet_history.cc",
    "modules/rtp_rtcp/source/rtp_packet_history.h",
    # NALU scanning + byte order utilities
    "common_video/h264/h264_common.cc",
    "common_video/h264/h264_common.h",
    "rtc_base/byte_order.h",
    # Key deps for rtp_packet / rtp_format_h264 to resolve includes
    "api/array_view.h",
    "api/rtp_headers.h",
    "api/rtp_parameters.h",
    "api/transport/rtp/dependency_descriptor.h",
    "rtc_base/copy_on_write_buffer.h",
    "rtc_base/bitstream_reader.h",
    "common_video/h264/nalu_rewriter.h",
    "common_video/h264/sps_parser.h",
    "common_video/h264/sps_vui_rewriter.h",
    "modules/rtp_rtcp/source/rtp_packet_to_send.h",  # already exists
    "modules/rtp_rtcp/source/rtp_packet_received.h",
    "modules/rtp_rtcp/source/rtp_rtcp_config.h",
    "modules/rtp_rtcp/source/rtp_rtcp_interface.h",
    "modules/rtp_rtcp/source/byte_io.h",
]

# ── Files needed for M6 JitterBuffer module (§5 12 functions + deps) ──
M6_FILES = [
    # PacketBuffer (ring buffer indexed by seq)
    "modules/video_coding/packet_buffer.cc",
    "modules/video_coding/packet_buffer.h",
    "modules/video_coding/packet.h",
    # FrameBuffer (frame-level buffering + scheduling)
    "modules/video_coding/frame_buffer.cc",
    "modules/video_coding/frame_buffer.h",
    # JitterEstimator (Kalman filter)
    "modules/video_coding/jitter_estimator.cc",
    "modules/video_coding/jitter_estimator.h",
    # Timing (render scheduling)
    "modules/video_coding/timing/timing.cc",
    "modules/video_coding/timing/timing.h",
    "modules/video_coding/timing/inter_frame_delay.h",
    "modules/video_coding/timing/timestamp_extrapolator.h",
    "modules/video_coding/timing/decode_time_percentile_filter.h",
    # NackTracker
    "modules/rtp_rtcp/source/nack_tracker.cc",
    "modules/rtp_rtcp/source/nack_tracker.h",
    # RtpVideoStreamReceiver (entry point)
    "modules/video_coding/rtp_video_stream_receiver2.cc",
    "modules/video_coding/rtp_video_stream_receiver2.h",
    # Key deps for M6
    "modules/video_coding/encoded_frame.h",
    "modules/video_coding/frame_helpers.h",
    "modules/video_coding/loss_notification_controller.h",
    "modules/video_coding/unique_timestamp_counter.h",
    "modules/video_coding/rtp_frame_reference_finder.h",
    "modules/video_coding/rtp_generic_ref_finder.h",
    "modules/video_coding/rtp_seq_num_only_ref_finder.h",
    "modules/video_coding/rtp_vp8_ref_finder.h",
    "modules/video_coding/rtp_vp9_ref_finder.h",
    "modules/video_coding/histogram.h",
    "modules/video_coding/h264_sps_pps_tracker.h",
    "modules/video_coding/timing/timing_module.h",
    "rtc_base/numerics/histogram_percentile_counter.h",
    "rtc_base/numerics/moving_percentile_filter.h",
    "system_wrappers/include/clock.h",
    "system_wrappers/include/ntp_time.h",
]

# ── Bonus: media/engine for the kMaxPayloadSize reference ──
EXTRA_FILES = [
    "media/engine/webrtc_video_engine.cc",
]

ALL_FILES = M4_FILES + M6_FILES + EXTRA_FILES


def download_file(rel_path: str) -> bool:
    """Download one file from googlesource, return True on success."""
    dest = ROOT / rel_path
    if dest.exists():
        print(f"  SKIP (exists): {rel_path}")
        return True

    dest.parent.mkdir(parents=True, exist_ok=True)
    url = f"{BASE_URL}/{rel_path}?format=TEXT"

    try:
        result = subprocess.run(
            ["curl", "-sL", "--max-time", "30", url],
            capture_output=True, timeout=35
        )
        if result.returncode != 0 or not result.stdout.strip():
            print(f"  FAIL (curl error): {rel_path}")
            return False

        decoded = subprocess.run(
            ["base64", "-d"],
            input=result.stdout, capture_output=True, timeout=5
        )
        if decoded.returncode != 0:
            print(f"  FAIL (base64 decode): {rel_path}")
            return False

        dest.write_bytes(decoded.stdout)
        lines = decoded.stdout.count(b'\n')
        print(f"  OK  ({lines} lines): {rel_path}")
        return True
    except Exception as e:
        print(f"  FAIL ({e}): {rel_path}")
        return False


def main():
    force = "--force" in sys.argv
    if force:
        print("⚠️  --force: will re-download all files\n")

    total, ok, fail = len(ALL_FILES), 0, 0
    for f in ALL_FILES:
        if force and (ROOT / f).exists():
            (ROOT / f).unlink()
        if download_file(f):
            ok += 1
        else:
            fail += 1

    print(f"\n{'='*50}")
    print(f"Done: {ok} OK, {fail} FAIL, {ok+fail}/{total} total")
    if fail:
        print("Failed files (may not exist at m120 or path changed):")
        sys.exit(1)


if __name__ == "__main__":
    main()
