#!/usr/bin/env python3
"""
calibrate_stereo.py — Capture checkerboard image pairs from both OV9281
cameras and compute stereo calibration + rectification parameters.

Must match the printed pattern exactly (see generate_checkerboard.py):
    9 x 6 inner corners, 25mm per square

WORKFLOW
  1. Print checkerboard_A3_9x6_25mm.pdf at 100% scale on A3 paper, verify
     the 100mm reference ruler with a real ruler, mount it flat on a wall.
  2. Run this script in "capture" mode. It grabs a synchronized frame pair
     every few seconds -- move the board (or the rig) between captures to
     get varied positions/angles/distances covering the frame, including
     some tilt in each axis and some captures near the frame edges. Aim
     for 15-20 good pairs where the board is detected in BOTH images.
  3. Run this script in "calibrate" mode to compute intrinsics, extrinsics,
     and rectification maps from the captured pairs, saved to
     stereo_params.npz.
  4. Use rectified_depth.py to capture live frames, rectify them with the
     saved parameters, and get a calibrated disparity/depth map.

Usage:
    python3 calibrate_stereo.py capture --count 20 --interval 3 --out-dir calib_images
    python3 calibrate_stereo.py calibrate --images-dir calib_images --out stereo_params.npz
"""

import argparse
import subprocess
import sys
import time
from pathlib import Path

import numpy as np
import cv2

CHESSBOARD_INNER_CORNERS = (9, 6)  # (columns, rows) of INNER corners
SQUARE_SIZE_MM = 25.0


def capture_pair(width: int, height: int, out_dir: Path, tag: str) -> tuple[Path, Path]:
    left_raw = out_dir / f"{tag}_left.raw"
    right_raw = out_dir / f"{tag}_right.raw"

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


def cmd_capture(args):
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    print(f"Capturing up to {args.count} pairs, {args.interval}s apart.")
    print("Move the checkerboard (or the camera rig) between shots: vary "
          "position, distance (~30cm-1.5m), and tilt angle. Include some "
          "shots near the frame edges/corners, not just centered.")
    print("Press Ctrl+C to stop early once you have enough good pairs.\n")

    good = 0
    i = 0
    try:
        while good < args.count:
            i += 1
            tag = f"pair_{i:03d}"
            print(f"[{i}] Capturing {tag} in 2s...", end=" ", flush=True)
            time.sleep(2)
            left_raw, right_raw = capture_pair(args.width, args.height, out_dir, tag)
            left = load_mono8(left_raw, args.width, args.height)
            right = load_mono8(right_raw, args.width, args.height)

            flags = cv2.CALIB_CB_ADAPTIVE_THRESH + cv2.CALIB_CB_NORMALIZE_IMAGE
            found_l, _ = cv2.findChessboardCorners(left, CHESSBOARD_INNER_CORNERS, flags=flags)
            found_r, _ = cv2.findChessboardCorners(right, CHESSBOARD_INNER_CORNERS, flags=flags)

            if found_l and found_r:
                cv2.imwrite(str(out_dir / f"{tag}_left.png"), left)
                cv2.imwrite(str(out_dir / f"{tag}_right.png"), right)
                good += 1
                print(f"OK (board found in both) -- {good}/{args.count} good pairs so far.")
            else:
                print(f"SKIPPED (board not found: left={found_l}, right={found_r}). "
                      "Try a clearer angle/distance.")
                left_raw.unlink(missing_ok=True)
                right_raw.unlink(missing_ok=True)

            time.sleep(max(args.interval - 2, 0))
    except KeyboardInterrupt:
        print(f"\nStopped early with {good} good pairs.")

    print(f"\nDone. {good} good pairs saved to {out_dir}/ as pair_NNN_{{left,right}}.png")
    if good < 10:
        print("WARNING: fewer than 10 good pairs -- calibration quality will "
              "likely be poor. Aim for 15-20+ if possible.")


def cmd_calibrate(args):
    images_dir = Path(args.images_dir)
    left_imgs = sorted(images_dir.glob("*_left.png"))
    right_imgs = sorted(images_dir.glob("*_right.png"))
    if len(left_imgs) != len(right_imgs) or len(left_imgs) == 0:
        print(f"ERROR: found {len(left_imgs)} left / {len(right_imgs)} right "
              f"images in {images_dir} -- expected a matching non-zero pair count.")
        return 1

    print(f"Found {len(left_imgs)} calibration pairs. Detecting corners...")

    cols, rows = CHESSBOARD_INNER_CORNERS
    objp = np.zeros((cols * rows, 3), np.float32)
    objp[:, :2] = np.mgrid[0:cols, 0:rows].T.reshape(-1, 2) * SQUARE_SIZE_MM

    objpoints, imgpoints_l, imgpoints_r = [], [], []
    img_size = None

    criteria = (cv2.TERM_CRITERIA_EPS + cv2.TERM_CRITERIA_MAX_ITER, 100, 1e-5)

    used = 0
    for lp, rp in zip(left_imgs, right_imgs):
        left = cv2.imread(str(lp), cv2.IMREAD_GRAYSCALE)
        right = cv2.imread(str(rp), cv2.IMREAD_GRAYSCALE)
        if img_size is None:
            img_size = (left.shape[1], left.shape[0])

        flags = cv2.CALIB_CB_ADAPTIVE_THRESH + cv2.CALIB_CB_NORMALIZE_IMAGE
        found_l, corners_l = cv2.findChessboardCorners(left, CHESSBOARD_INNER_CORNERS, flags=flags)
        found_r, corners_r = cv2.findChessboardCorners(right, CHESSBOARD_INNER_CORNERS, flags=flags)

        if not (found_l and found_r):
            print(f"  {lp.stem}: board not found in one or both images, skipping.")
            continue

        corners_l = cv2.cornerSubPix(left, corners_l, (11, 11), (-1, -1), criteria)
        corners_r = cv2.cornerSubPix(right, corners_r, (11, 11), (-1, -1), criteria)

        objpoints.append(objp)
        imgpoints_l.append(corners_l)
        imgpoints_r.append(corners_r)
        used += 1

    if used < 8:
        print(f"ERROR: only {used} usable pairs after corner detection -- "
              "need at least ~8-10 for a meaningful calibration. Capture more.")
        return 1

    print(f"Using {used} pairs. Running per-camera calibration...")
    ret_l, mtx_l, dist_l, _, _ = cv2.calibrateCamera(objpoints, imgpoints_l, img_size, None, None)
    ret_r, mtx_r, dist_r, _, _ = cv2.calibrateCamera(objpoints, imgpoints_r, img_size, None, None)
    print(f"  Left reprojection error:  {ret_l:.4f} px")
    print(f"  Right reprojection error: {ret_r:.4f} px")
    if ret_l > 1.0 or ret_r > 1.0:
        print("  WARNING: reprojection error above 1.0px -- consider capturing "
              "more/better pairs (sharper focus, less blur, more varied poses).")

    print("Running stereo calibration...")
    flags = cv2.CALIB_FIX_INTRINSIC
    ret_s, mtx_l, dist_l, mtx_r, dist_r, R, T, E, F = cv2.stereoCalibrate(
        objpoints, imgpoints_l, imgpoints_r,
        mtx_l, dist_l, mtx_r, dist_r, img_size,
        criteria=criteria, flags=flags,
    )
    baseline_mm = np.linalg.norm(T)
    print(f"  Stereo reprojection error: {ret_s:.4f} px")
    print(f"  Computed baseline: {baseline_mm:.2f} mm")

    print("Computing rectification maps...")
    R1, R2, P1, P2, Q, roi1, roi2 = cv2.stereoRectify(
        mtx_l, dist_l, mtx_r, dist_r, img_size, R, T,
        flags=cv2.CALIB_ZERO_DISPARITY, alpha=0,
    )

    map1x, map1y = cv2.initUndistortRectifyMap(mtx_l, dist_l, R1, P1, img_size, cv2.CV_32FC1)
    map2x, map2y = cv2.initUndistortRectifyMap(mtx_r, dist_r, R2, P2, img_size, cv2.CV_32FC1)

    np.savez(
        args.out,
        img_size=img_size,
        mtx_l=mtx_l, dist_l=dist_l,
        mtx_r=mtx_r, dist_r=dist_r,
        R=R, T=T, E=E, F=F,
        R1=R1, R2=R2, P1=P1, P2=P2, Q=Q,
        map1x=map1x, map1y=map1y,
        map2x=map2x, map2y=map2y,
        baseline_mm=baseline_mm,
    )
    print(f"\nSaved calibration to {args.out}")
    print(f"Baseline: {baseline_mm:.2f} mm  |  Stereo RMS error: {ret_s:.4f} px")
    if ret_s > 1.0:
        print("WARNING: stereo RMS error above 1.0px -- depth accuracy will be "
              "limited. Consider recapturing with more/sharper/varied pairs.")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    cap = sub.add_parser("capture", help="Capture calibration image pairs")
    cap.add_argument("--width", type=int, default=1280)
    cap.add_argument("--height", type=int, default=800)
    cap.add_argument("--count", type=int, default=20, help="target number of good pairs")
    cap.add_argument("--interval", type=float, default=3.0, help="seconds between capture attempts")
    cap.add_argument("--out-dir", default="calib_images")
    cap.set_defaults(func=cmd_capture)

    calib = sub.add_parser("calibrate", help="Compute calibration from captured pairs")
    calib.add_argument("--images-dir", default="calib_images")
    calib.add_argument("--out", default="stereo_params.npz")
    calib.set_defaults(func=cmd_calibrate)

    args = ap.parse_args()
    sys.exit(args.func(args) or 0)


if __name__ == "__main__":
    main()
