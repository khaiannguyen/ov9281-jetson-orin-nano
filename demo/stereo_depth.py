#!/usr/bin/env python3
"""
stereo_depth.py — Stereo disparity demo for the dual OV9281 global-shutter
mono camera rig on Jetson Orin Nano.

Both cameras are global-shutter and monochrome, which is exactly the sensor
type stereo vision rigs are usually built from (no rolling-shutter skew, no
demosaicing artifacts affecting matching). This script:

  1. Captures one synchronized frame pair from /dev/video0 and /dev/video1
     via v4l2-ctl (run as two parallel subprocesses -- this project's
     Argus/nvarguscamerasrc path does not work for this sensor; see the
     project README's "Known limitations" section for why V4L2 direct
     capture is used instead).
  2. Loads both raw frames as 16-bit mono (the V4L2 pixel format is labeled
     BG10 due to a platform-wide framework limitation with no monochrome
     format entry -- see README -- but the underlying byte data is correct
     monochrome data regardless of that label).
  3. Computes a disparity map with OpenCV's StereoBM block matcher.
  4. Saves a colorized disparity visualization plus the two rectified-ish
     source frames.

CALIBRATION NOTE: this script produces a *qualitative* disparity map only.
Turning disparity into real-world distance requires:
    depth_mm = (baseline_mm * focal_length_px) / disparity_px
which needs a proper stereo calibration (cv2.stereoCalibrate) with a
checkerboard pattern to obtain focal_length_px and to rectify both images
onto a common epipolar geometry -- lens/mounting tolerances mean the two
cameras are not perfectly parallel out of the box. Calibration is a
separate step not included here; this demo is meant to prove the dual-
camera pipeline end-to-end, not to be metrology-grade.

Usage:
    python3 stereo_depth.py [--width 1280] [--height 800] [--out-dir .]
"""

import argparse
import subprocess
import sys
from pathlib import Path

import numpy as np
import cv2


def capture_pair(width: int, height: int, out_dir: Path) -> tuple[Path, Path]:
    """Capture one frame from each camera simultaneously via v4l2-ctl."""
    left_raw = out_dir / "left.raw"
    right_raw = out_dir / "right.raw"

    cmd_tpl = (
        "v4l2-ctl --set-fmt-video=width={w},height={h},pixelformat=BG10 "
        "--stream-mmap --stream-count=1 -d {dev} --stream-to={out}"
    )

    procs = [
        subprocess.Popen(
            cmd_tpl.format(w=width, h=height, dev="/dev/video0", out=left_raw),
            shell=True,
        ),
        subprocess.Popen(
            cmd_tpl.format(w=width, h=height, dev="/dev/video1", out=right_raw),
            shell=True,
        ),
    ]
    for p in procs:
        ret = p.wait(timeout=20)
        if ret != 0:
            raise RuntimeError(f"v4l2-ctl capture failed with exit code {ret}")

    expected_bytes = width * height * 2  # 16-bit container per pixel
    for f in (left_raw, right_raw):
        if not f.exists() or f.stat().st_size != expected_bytes:
            raise RuntimeError(
                f"{f} missing or wrong size "
                f"(got {f.stat().st_size if f.exists() else 0}, "
                f"expected {expected_bytes})"
            )
    return left_raw, right_raw


def load_mono16(path: Path, width: int, height: int) -> np.ndarray:
    """Load a raw capture as 16-bit monochrome, downshifted to 8-bit."""
    data = np.fromfile(path, dtype=np.uint16).reshape(height, width)
    return (data >> 8).astype(np.uint8)


def compute_disparity(left: np.ndarray, right: np.ndarray) -> np.ndarray:
    stereo = cv2.StereoBM_create(numDisparities=16 * 6, blockSize=15)
    disparity = stereo.compute(left, right)
    return disparity


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--width", type=int, default=1280)
    ap.add_argument("--height", type=int, default=800)
    ap.add_argument("--out-dir", type=Path, default=Path("."))
    args = ap.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)

    print(f"Capturing synchronized frame pair ({args.width}x{args.height})...")
    left_raw, right_raw = capture_pair(args.width, args.height, args.out_dir)

    left = load_mono16(left_raw, args.width, args.height)
    right = load_mono16(right_raw, args.width, args.height)

    cv2.imwrite(str(args.out_dir / "left.png"), left)
    cv2.imwrite(str(args.out_dir / "right.png"), right)

    print("Computing disparity map...")
    disparity = compute_disparity(left, right)

    disp_norm = cv2.normalize(
        disparity, None, alpha=0, beta=255, norm_type=cv2.NORM_MINMAX
    ).astype(np.uint8)
    disp_color = cv2.applyColorMap(disp_norm, cv2.COLORMAP_JET)
    cv2.imwrite(str(args.out_dir / "disparity.png"), disp_color)

    print(
        "Done. Wrote left.png, right.png, disparity.png to "
        f"{args.out_dir.resolve()}"
    )
    print(
        "Note: disparity is qualitative only -- see script docstring for "
        "what's needed to turn this into calibrated metric depth."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
