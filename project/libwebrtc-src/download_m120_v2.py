#!/usr/bin/env python3
"""Download remaining libwebrtc m120 files with corrected paths."""
import subprocess, sys, pathlib

BASE_URL = "https://webrtc.googlesource.com/src/+/refs/branch-heads/6099"
ROOT = pathlib.Path("/home/bfm01000/workspace/learnffmpeg/project/libwebrtc-src/m120")

# Files with VERIFIED correct m120 paths
FILES = [
    # ── M4: rtp_rtcp + common_video (corrected) ──
    "rtc_base/byte_order.h",
    "api/array_view.h",
    "modules/rtp_rtcp/source/rtp_sender.cc",
    "modules/rtp_rtcp/source/rtp_sender.h",
    "modules/rtp_rtcp/source/rtp_packet_history.cc",
    "modules/rtp_rtcp/source/rtp_packet_history.h",
    "common_video/h264/h264_common.cc",
    "common_video/h264/h264_common.h",
    "api/rtp_headers.h",
    "api/rtp_parameters.h",
    "api/transport/rtp/dependency_descriptor.h",
    "rtc_base/copy_on_write_buffer.h",
    "rtc_base/bitstream_reader.h",
    "common_video/h264/sps_parser.h",
    "common_video/h264/sps_vui_rewriter.h",
    "modules/rtp_rtcp/source/rtp_packet_received.h",
    "modules/rtp_rtcp/source/rtp_rtcp_config.h",
    "modules/rtp_rtcp/source/rtp_rtcp_interface.h",
    "modules/rtp_rtcp/source/byte_io.h",

    # ── M4 extras ──
    "media/engine/webrtc_video_engine.cc",

    # ── M6: packet_buffer + timing (corrected paths) ──
    "modules/video_coding/packet_buffer.cc",
    "modules/video_coding/packet_buffer.h",
    "modules/video_coding/timing/jitter_estimator.cc",
    "modules/video_coding/timing/jitter_estimator.h",
    "modules/video_coding/timing/timing.cc",
    "modules/video_coding/timing/timing.h",
    "modules/video_coding/timing/timestamp_extrapolator.cc",
    "modules/video_coding/timing/timestamp_extrapolator.h",
    "modules/video_coding/timing/decode_time_percentile_filter.h",
    "modules/video_coding/timing/rtt_filter.cc",
    "modules/video_coding/timing/rtt_filter.h",
    "modules/video_coding/timing/inter_frame_delay_variation_calculator.h",

    # ── M6: nack (renamed: nack_tracker → nack_requester in m120) ──
    "modules/video_coding/nack_requester.cc",
    "modules/video_coding/nack_requester.h",

    # ── M6: receiver (moved: modules/video_coding → video/ in m120) ──
    "video/rtp_video_stream_receiver2.cc",
    "video/rtp_video_stream_receiver2.h",

    # ── M6: frame assembly (frame_buffer split into multiple files in m120) ──
    "modules/video_coding/video_receiver2.cc",
    "modules/video_coding/video_receiver2.h",
    "modules/video_coding/generic_decoder.cc",
    "modules/video_coding/generic_decoder.h",
    "modules/video_coding/frame_helpers.cc",
    "modules/video_coding/frame_helpers.h",
    "modules/video_coding/loss_notification_controller.cc",
    "modules/video_coding/loss_notification_controller.h",
    "modules/video_coding/histogram.cc",
    "modules/video_coding/histogram.h",
    "modules/video_coding/h264_sps_pps_tracker.cc",
    "modules/video_coding/h264_sps_pps_tracker.h",
    "modules/video_coding/rtp_frame_reference_finder.cc",
    "modules/video_coding/rtp_frame_reference_finder.h",
    "modules/video_coding/rtp_vp8_ref_finder.cc",
    "modules/video_coding/rtp_vp8_ref_finder.h",
    "modules/video_coding/rtp_vp9_ref_finder.cc",
    "modules/video_coding/rtp_vp9_ref_finder.h",
    "modules/video_coding/rtp_seq_num_only_ref_finder.cc",
    "modules/video_coding/rtp_seq_num_only_ref_finder.h",
    "modules/video_coding/rtp_generic_ref_finder.cc",
    "modules/video_coding/rtp_generic_ref_finder.h",
    "modules/video_coding/rtp_frame_id_only_ref_finder.cc",
    "modules/video_coding/fec_controller_default.cc",
    "modules/video_coding/fec_controller_default.h",
    "modules/video_coding/encoded_frame.h",

    # ── M6: api/video types ──
    "api/video/encoded_frame.h",
    "api/video/encoded_image.h",
    "api/video/video_timing.h",

    # ── Utilities ──
    "rtc_base/numerics/histogram_percentile_counter.h",
    "rtc_base/numerics/moving_percentile_filter.h",
    "system_wrappers/include/clock.h",
    "system_wrappers/include/ntp_time.h",
]


def download(rel_path):
    dest = ROOT / rel_path
    if dest.exists():
        return "SKIP"
    dest.parent.mkdir(parents=True, exist_ok=True)
    url = f"{BASE_URL}/{rel_path}?format=TEXT"
    try:
        raw = subprocess.run(["curl", "-sL", "--max-time", "30", url],
                             capture_output=True, timeout=35)
        if raw.returncode != 0 or not raw.stdout.strip():
            return "FAIL"
        decoded = subprocess.run(["base64", "-d"], input=raw.stdout,
                                 capture_output=True, timeout=5)
        if decoded.returncode != 0:
            return "FAIL"
        dest.write_bytes(decoded.stdout)
        lines = decoded.stdout.count(b'\n')
        return f"OK ({lines}L)"
    except Exception as e:
        return f"FAIL ({e})"

ok, skip, fail = 0, 0, 0
for f in FILES:
    r = download(f)
    if r == "SKIP":
        skip += 1
        print(f"  SKIP  {f}")
    elif r.startswith("OK"):
        ok += 1
        print(f"  {r:6s}  {f}")
    else:
        fail += 1
        print(f"  FAIL  {f}  ({r})")

print(f"\n{'='*50}")
print(f"New: {ok}  Skipped: {skip}  Failed: {fail}  Total: {len(FILES)}")
