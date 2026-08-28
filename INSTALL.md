# INSTALL.md — Reproducing this port on your own Jetson Orin Nano

This guide walks through reproducing this OV9281 dual-camera driver port
from scratch on a Jetson Orin Nano running L4T R39.2 (JetPack 7.2). It
assumes you are comfortable with the Linux command line and have physical
console access (HDMI + keyboard) to your board — you will need it for the
boot-entry step near the end.

## 0. Prerequisites

- Jetson Orin Nano (Developer Kit or module + carrier board), already
  flashed and running **L4T R39.2 / JetPack 7.2**. Check with:
  ```bash
  cat /etc/nv_tegra_release
  ```
- Two OV9281-based camera modules (e.g. Waveshare OV9281-120), wired to the
  CAM0 and CAM1 CSI connectors.
- ~10 GB free disk space for BSP source downloads and builds.
- Physical console access (HDMI + keyboard) — required later to select a
  new boot entry; SSH alone is not enough for that step.

## 1. Download the L4T R39.2 BSP sources

From NVIDIA's JetPack 7.2 archive page:

**https://developer.nvidia.com/embedded/jetpack/downloads/archive-7.2**

Under **Jetson Linux 39.2 → Sources**, download **"Driver Package (BSP)
Sources"**. This is a large nested archive; extract it fully:

```bash
mkdir -p ~/ov9281-build/public_sources_39.2
cd ~/ov9281-build/public_sources_39.2
tar -xjf /path/to/public_sources.tbz2
cd Linux_for_Tegra/source
tar -xjf kernel_src.tbz2
tar -xjf kernel_oot_modules_src.tbz2
```

You should now have `nvidia-oot/drivers/media/i2c/` populated with NVIDIA's
existing sensor drivers (`nv_ov5693.c`, `nv_imx219.c`, etc) — this is the
tree you'll be adding OV9281 support into.

## 2. (Optional but recommended) Get the original NVIDIA OV9281 driver

NVIDIA shipped an official OV9281 driver through L4T R35.6.5 (kernel 5.10)
before dropping it in R39.2 (kernel 6.8) — see this project's main README
for how that was confirmed. If you want the original source as a reference
(this repo already includes the ported result, so this step is optional):

**https://developer.nvidia.com/embedded/jetson-linux-r3565**

Download that release's "Driver Package (BSP) Sources" the same way, and
look under `Linux_for_Tegra/source/public/kernel/nvidia/drivers/media/i2c/`
for `nv_ov9281.c` and `ov9281_mode_tbls.h`.

## 3. Copy this repo's driver files into the BSP tree

```bash
NVOOT=~/ov9281-build/public_sources_39.2/Linux_for_Tegra/source/nvidia-oot

cp driver/nv_ov9281.c            "$NVOOT/drivers/media/i2c/nv_ov9281.c"
cp driver/ov9281_mode_tbls.h     "$NVOOT/drivers/media/i2c/ov9281_mode_tbls.h"
cp driver/ov9281.h               "$NVOOT/include/media/ov9281.h"
cp driver/ov9281_trace.h         "$NVOOT/include/trace/events/ov9281.h"
```

Add the driver to the build by editing
`"$NVOOT/drivers/media/i2c/Makefile"` — add this line next to the existing
`obj-m += nv_ov5693.o` entry:

```makefile
obj-m += nv_ov9281.o
```

## 4. Build the kernel module

**Do not** build `nvidia-oot/Makefile` in isolation — it will fail with a
missing `nvidia/conftest.h` header, because that header is only generated
when building through the top-level entry point. Build from the top level
instead:

```bash
cd ~/ov9281-build/public_sources_39.2/Linux_for_Tegra/source
KDIR=/usr/src/linux-headers-$(uname -r)-ubuntu24.04_aarch64/3rdparty/canonical/linux-noble

make -C "$KDIR" M=$(pwd)/nvidia-oot kernel_name=noble system_type=l4t modules
```

This builds NVIDIA's entire `nvidia-oot` module tree (not just OV9281),
which takes several minutes on the board itself. It's normal for this to
fail at unrelated components (e.g. `nvdisplay`, `unifiedgpudisp`) if you
didn't download their proprietary sources separately — as long as
`nvidia-oot/drivers/media/i2c/nv_ov9281.ko` is produced successfully, you're
fine.

Reference: NVIDIA's Camera Software Development Solution documentation
confirms the expected build/driver location convention this project
follows:
**https://docs.nvidia.com/jetson/archives/r39.2/DeveloperGuide/SD/CameraDevelopment/ArgusFramework/CameraSoftwareDevelopmentSolution.html**

## 5. Install the module

```bash
sudo cp "$NVOOT/drivers/media/i2c/nv_ov9281.ko" \
    /lib/modules/$(uname -r)/updates/drivers/media/i2c/nv_ov9281.ko
sudo depmod -a
```

Quick sanity check before touching the device tree — this should load
cleanly (it just won't bind to anything yet, since no matching DT node is
active):

```bash
sudo insmod "$NVOOT/drivers/media/i2c/nv_ov9281.ko"
sudo dmesg | tail -20
sudo rmmod nv_ov9281
```

## 6. Build the device tree overlay

```bash
cd ~/ov9281-build

INC1=~/ov9281-build/public_sources_39.2/Linux_for_Tegra/source/kernel/kernel-noble/include
INC2=~/ov9281-build/public_sources_39.2/Linux_for_Tegra/source/hardware/nvidia/t23x/nv-public/include/platforms

cpp -nostdinc -undef -x assembler-with-cpp -I "$INC1" -I "$INC2" \
    devicetree/tegra234-p3767-camera-p3768-ov9281-dual.dts \
    -o ov9281-dual.pp.dts

dtc -@ -I dts -O dtb \
    -o tegra234-p3767-camera-p3768-ov9281-dual.dtbo \
    ov9281-dual.pp.dts
```

**Important — check your GPIO pins before proceeding.** The device tree in
this repo hardcodes `TEGRA234_MAIN_GPIO(H,6)` (CAM0 reset) and
`TEGRA234_MAIN_GPIO(AC,0)` (CAM1 reset), verified against this project's
specific Orin Nano Super Dev Kit carrier board (P3768). These are
properties of the CSI connector slot on the carrier board, not of the
sensor — if you're on a different carrier board, confirm your own reset
GPIO numbers by inspecting the **live, running** device tree for whatever
reference sensor (e.g. IMX219) already works on your board:

```bash
sudo find /proc/device-tree -iname "*imx219*"
# then inspect the reset-gpios property under that node
```

## 7. Install the overlay as a new (non-default) boot entry

**Do not overwrite your existing working camera configuration.** Back up
first:

```bash
sudo cp /boot/extlinux/extlinux.conf /boot/extlinux/extlinux.conf.bak
sudo cp ov9281-dual.dtbo /boot/tegra234-p3767-camera-p3768-ov9281-dual.dtbo
```

Edit `/boot/extlinux/extlinux.conf` and add a **new** `LABEL` entry (do
**not** change the existing `DEFAULT` line), modeled on whatever entry your
board already uses for its working camera (copy its `LINUX`/`FDT`/`INITRD`/
`APPEND` lines exactly, only changing the `OVERLAYS` line):

```
LABEL OV9281Dual
        MENU LABEL Custom Header Config: <CSI Camera OV9281 Dual>
        LINUX /boot/Image
        FDT /boot/dtb/<your-base-dtb-filename>.dtb
        INITRD /boot/initrd
        APPEND ${cbootargs} root=PARTUUID=<your-root-partuuid> rw rootwait ...
        OVERLAYS /boot/tegra234-p3767-camera-p3768-ov9281-dual.dtbo
```

Verify you now have 3 `LABEL` entries and that `DEFAULT` still points to
your original working entry:

```bash
grep "^LABEL\|^DEFAULT" /boot/extlinux/extlinux.conf
```

This is your safety net: if the new entry fails to boot for any reason, not
selecting it at the boot menu (or letting the timeout expire) always falls
back to your known-good configuration.

## 8. Reboot and select the new entry

```bash
sudo reboot
```

Watch the HDMI console as it boots — a text boot menu will appear listing
your `LABEL` entries. Select `OV9281Dual` (arrow keys + Enter; if number-key
shortcuts don't register, check that NumLock isn't interfering). If you
have physical console access only via keyboard number keys and they don't
seem to work, try toggling NumLock — this was the actual cause of an
apparent "stuck menu" during this project's own bring-up.

## 9. Verify

```bash
sudo dmesg | grep -i ov9281
v4l2-ctl --list-devices
v4l2-ctl -d /dev/video0 --list-formats-ext
v4l2-ctl --set-fmt-video=width=1280,height=800,pixelformat=BG10 \
  --stream-mmap --stream-count=1 -d /dev/video0 --stream-to=test.raw
ls -la test.raw   # expect exactly 2,048,000 bytes for a good capture
```

If you hit `-121` (I2C EREMOTEIO), a CSI `request timed out`, or a probe
failure with a different error code, check this project's main
[README's debugging journey table](README.md#the-debugging-journey) first
— several of the failures documented there are specific to how DT
properties get silently dropped when authoring a new sensor node from an
existing reference sensor's overlay, and may well be exactly what you're
hitting.

## 10. (Do not expect) Argus / GStreamer ISP support

`nvarguscamerasrc` will not work with this sensor — this is a confirmed
architectural limitation of NVIDIA's closed-source Argus camera service,
not something fixable at the driver or device-tree level. See the main
README's "Known limitations" section for the root cause. Use V4L2 direct
capture (`v4l2-ctl`, OpenCV's `cv2.VideoCapture` with the V4L2 backend, or
any other V4L2-based tool) instead.
