# Installation Guide

> **Use at your own risk.** Tested only on a BMW G30 with `NBTevo_Y21432J`.

## What You Need

1. **OBD2-to-ENET cable** connected to your laptop.
2. **USB flash drive** formatted as **FAT32** or **exFAT** (not NTFS).
3. **SSH access** to your NBT Evo head unit.
   - If your head unit does not already have SSH open, tools like [Bavarian SSH Client](https://ssh.bavarian-aa.uk/) and [TurboCode Proxy](https://turbocode.shop/bmw-remote-enet-tool/) are commonly used to open temporary SSH access. **We have not tested those third-party tools and do not guarantee or support them - use them at your own risk.**

---

## Step 1: Prepare the USB Drive

Copy these 3 files from this repository to the **root** of your FAT32/exFAT USB flash drive:

- `build/libnbt_carplay_hud_real.so`
- `hook/mm-ipod-wrapper.sh`
- `hook/iap2_ident.cfgfrag`

Plug the USB drive into the car's USB port and **disconnect your iPhone from CarPlay** (or turn CarPlay off on the phone).

---

## Step 2: Install via SSH

Open Terminal / PowerShell and connect to the head unit via SSH:

```bash
ssh root@169.254.199.119 -o HostKeyAlgorithms=+ssh-dss -m hmac-sha1
# Or if using a local port forward (e.g. port 6969):
# ssh root@127.0.0.1 -o HostKeyAlgorithms=+ssh-dss -m hmac-sha1 -p 6969
# (If you land on hu-jacinto, switch to hu-omap first: on -f /net/hu-omap/ /bin/sh)
```

Find your USB drive name in `/mnt/` and run the install commands:

```sh
ls -l /mnt/
# Replace umassXXXXX below with your actual USB folder name from ls -l /mnt/
USB=/mnt/umassXXXXX

mount -uw /fs/sda0
mkdir -p /fs/sda0/opt/aa-cp
cp "$USB/libnbt_carplay_hud_real.so" /fs/sda0/opt/aa-cp/libnbt_carplay_hud.so
cp "$USB/mm-ipod-wrapper.sh"         /fs/sda0/opt/aa-cp/mm-ipod-wrapper.sh
chmod 755 /fs/sda0/opt/aa-cp/libnbt_carplay_hud.so /fs/sda0/opt/aa-cp/mm-ipod-wrapper.sh

# Self-test (must print "QNX" and "rc=0"):
LD_PRELOAD=/opt/aa-cp/libnbt_carplay_hud.so NBT_CARPLAY_SELFTEST=1 /bin/uname && echo "rc=$?"

# Back up stock mm-ipod and install wrapper:
test -f /fs/sda0/opt/conn/bin/carplay/mm-ipod.real || mv /fs/sda0/opt/conn/bin/carplay/mm-ipod /fs/sda0/opt/conn/bin/carplay/mm-ipod.real
cp /fs/sda0/opt/aa-cp/mm-ipod-wrapper.sh /fs/sda0/opt/conn/bin/carplay/mm-ipod
chmod 755 /fs/sda0/opt/conn/bin/carplay/mm-ipod

# Back up iap2_Wifi.cfg and enable Route Guidance message IDs:
CFG=/var/opt/conn/carplay/iap2_Wifi.cfg
test -f "$CFG.orig" || cp "$CFG" "$CFG.orig"
grep -q '^\[ident\]' "$CFG" || cat "$USB/iap2_ident.cfgfrag" >> "$CFG"

mount -ur /fs/sda0
```

**Reboot the head unit** (hold the volume knob down for ~30 seconds), connect your iPhone, and start a route in Apple Maps or Google Maps.

---

## Uninstall / Restore Stock

To revert everything back to 100% factory stock over SSH:

```sh
mount -uw /fs/sda0
test -f /fs/sda0/opt/conn/bin/carplay/mm-ipod.real && mv -f /fs/sda0/opt/conn/bin/carplay/mm-ipod.real /fs/sda0/opt/conn/bin/carplay/mm-ipod
rm -rf /fs/sda0/opt/aa-cp
test -f /var/opt/conn/carplay/iap2_Wifi.cfg.orig && cp /var/opt/conn/carplay/iap2_Wifi.cfg.orig /var/opt/conn/carplay/iap2_Wifi.cfg && rm /var/opt/conn/carplay/iap2_Wifi.cfg.orig
mount -ur /fs/sda0
```

Then reboot the head unit (hold the volume knob for ~30 seconds).