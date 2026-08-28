#!/usr/bin/env python3
"""
rectified_depth.py — Calibrated stereo depth demo for the dual OV9281 rig.

Loads stereo_params.npz (produced by calibrate_stereo.py), captures a
synchronized frame pair, rectifies both images onto a common epipolar
geometry, computes disparity with StereoSGBM, and converts disparity to
real-world distance in millimeters using the calibrated baseline and focal
length:

    depth_mm = (baseline_mm * focal_length_px) / disparity_px

Outputs:
    left_rectified.png, right_rectified.png  -- undistorted + rectified views
    disparity.png                             -- colorized disparity map
    depth_annotated.png                       -- disparity map with the
                                                  estimated distance (mm) at
                                                  the image center printed on
                                                  it, as a sanity-check number

Usage:
    python3 rectified_depth.py [--params stereo_params.npz] [--out-dir .]
"""

import argparse
import subprocess
import sys
from pathlib import Path

import numpy as np
import cv2


def capture_pair(width: int, height: int, out_dir: Path) -> tuple[Path, Path]:
    left_raw = out_dir / "left.raw"
    right_raw = out_dir / "right.raw"
    cmd_tpl = (
        "v4l2-ctl --set-fmt-video=width={w},height={h},pixelformat=BG10 "
        "--stream-mmap --stream-count=1 -d {dev} --stream-to={out}"
    )
    procs = [
        subprocess.Popen(cmd_tpl.format(w=width, h=height, dev="/dev/video0", out=left_raw), shell=True),
        subprocess.Popen(cmd_tpl.format(w=width, h=height, dev="/dev/video1", out=right_raw), shell=True),
    ]
    for p in procs:
        if p.wait(timeout=20) != 0:
            raise RuntimeError("v4l2-ctl capture failed")
    return left_raw, right_raw


def load_mono8(path: Path, width: int, height: int) -> np.ndarray:
    data = np.fromfile(path, dtype=np.uint16).reshape(height, width)
    return (data >> 8).astype(np.uint8)


def compute_disparity(left: np.ndarray, right: np.ndarray) -> np.ndarray:
    block_size = 5
    num_disp = 16 * 8
    stereo = cv2.StereoSGBM_create(
        minDisparity=0,
        numDisparities=num_disp,
        blockSize=block_size,
        P1=8 * 1 * block_size ** 2,
        P2=32 * 1 * block_size ** 2,
        disp12MaxDiff=1,
        uniquenessRatio=10,
        speckleWindowSize=100,
        speckleRange=32,
        mode=cv2.STEREO_SGBM_MODE_SGBM_3WAY,
    )
    return stereo.compute(left, right).astype(np.float32) / 16.0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--params", default="stereo_params.npz")
    ap.add_argument("--out-dir", type=Path, default=Path("."))
    args = ap.parse_args()

    if not Path(args.params).exists():
        print(f"ERROR: {args.params} not found. Run calibrate_stereo.py first.")
        return 1

    cal = np.load(args.params)
    width, height = int(cal["img_size"][0]), int(cal["img_size"][1])
    baseline_mm = float(cal["baseline_mm"])
    Q = cal["Q"]

    args.out_dir.mkdir(parents=True, exist_ok=True)

    print(f"Loaded calibration: baseline={baseline_mm:.2f}mm, image size={width}x{height}")
    print("Capturing synchronized frame pair...")
    left_raw, right_raw = capture_pair(width, height, args.out_dir)
    left = load_mono8(left_raw, width, height)
    right = load_mono8(right_raw, width, height)

    print("Rectifying...")
    left_rect = cv2.remap(left, cal["map1x"], cal["map1y"], cv2.INTER_LINEAR)
    right_rect = cv2.remap(right, cal["map2x"], cal["map2y"], cv2.INTER_LINEAR)
    cv2.imwrite(str(args.out_dir / "left_rectified.png"), left_rect)
    cv2.imwrite(str(args.out_dir / "right_rectified.png"), right_rect)

    print("Computing disparity...")
    disparity = compute_disparity(left_rect, right_rect)

    valid = disparity > 0
    disp_norm = np.zeros_like(disparity)
    disp_norm[valid] = disparity[valid]
    disp_norm = cv2.normalize(disp_norm, None, 0, 255, cv2.NORM_MINMAX).astype(np.uint8)
    disp_color = cv2.applyColorMap(disp_norm, cv2.COLORMAP_JET)
    disp_color[~valid] = (0, 0, 0)
    cv2.imwrite(str(args.out_dir / "disparity.png"), disp_color)

    # Convert disparity -> 3D points using the calibration's Q matrix, then
    # read off the depth (Z, mm) at the image center as a sanity-check value.
    points_3d = cv2.reprojectImageTo3D(disparity, Q)
    cy, cx = height // 2, width // 2
    center_depth_mm = points_3d[cy, cx, 2]

    annotated = disp_color.copy()
    cv2.drawMarker(annotated, (cx, cy), (255, 255, 255), cv2.MARKER_CROSS, 20, 2)
    label = (
        f"{center_depth_mm:.0f} mm"
        if valid[cy, cx] and np.isfinite(center_depth_mm) and center_depth_mm > 0
        else "no valid depth here"
    )
    cv2.putText(annotated, label, (cx + 15, cy - 15),
                cv2.FONT_HERSHEY_SIMPLEX, 0.8, (255, 255, 255), 2)
    cv2.imwrite(str(args.out_dir / "depth_annotated.png"), annotated)

    valid_pct = 100.0 * valid.sum() / valid.size
    print(f"Valid disparity coverage: {valid_pct:.1f}%")
    print(f"Estimated distance at image center: {label}")
    print(f"Wrote left_rectified.png, right_rectified.png, disparity.png, "
          f"depth_annotated.png to {args.out_dir.resolve()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
