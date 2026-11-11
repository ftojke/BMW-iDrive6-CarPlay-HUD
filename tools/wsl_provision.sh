#!/usr/bin/env bash
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Provisions the QNX 6.5 armle-v7 cross-toolchain inside WSL Ubuntu (run as root, re-runnable).
#   1. Docker engine (docker.io + buildx) inside WSL, no Docker Desktop.
#   2. Builds luka-dev/qnx65-armv7-toolchain (GCC built from source, ~30 min, no QNX license key).
# usage (from Windows):
#   wsl -d Ubuntu-24.04 -u root -- bash -c "tr -d '\r' < tools/wsl_provision.sh | bash -s -- 8.5"
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
VARIANT="${1:-8.5}"
REPO_DIR=/root/qnx65-armv7-toolchain
log() { echo "[provision $(date +%H:%M:%S)] $*"; }

log "apt: docker.io git buildx"
apt-get update -y -q
apt-get install -y -q docker.io git ca-certificates
apt-get install -y -q docker-buildx || log "docker-buildx package not available, using the default builder"

log "starting docker"
if [ -d /run/systemd/system ]; then
    systemctl enable --now docker
else
    pgrep -x dockerd >/dev/null || (nohup dockerd >/var/log/dockerd.log 2>&1 &)
fi
for _ in $(seq 1 30); do docker info >/dev/null 2>&1 && break; sleep 2; done
docker info >/dev/null
log "docker ok: $(docker --version)"

if [ ! -d "$REPO_DIR/.git" ]; then
    log "cloning qnx65-armv7-toolchain"
    git clone --depth 1 https://github.com/luka-dev/qnx65-armv7-toolchain "$REPO_DIR"
else
    log "repo present, pulling"
    git -C "$REPO_DIR" pull --ff-only || true
fi

cd "$REPO_DIR"
# snapshot.debian.org intermittently answers 504/503; make apt retry inside every image stage.
# (Package signatures and checksums stay enforced; only the retry/timeout policy changes.)
if ! grep -q '80-retries' Dockerfile; then
    log "patching Dockerfile: apt retries for snapshot.debian.org"
    sed -i '/> \/etc\/apt\/sources.list/a RUN printf '"'"'Acquire::Retries "30";\\nAcquire::http::Timeout "180";\\n'"'"' > /etc/apt/apt.conf.d/80-retries' Dockerfile
fi
grep -n '80-retries' Dockerfile

log "building image variant $VARIANT (GCC from source; this is the long step)"
ok=0
for attempt in 1 2 3; do
    if ./host-scripts/qnx-run.sh build "$VARIANT"; then ok=1; break; fi
    log "build attempt $attempt failed; retrying in 30 s (cached layers are kept)"
    sleep 30
done
[ $ok -eq 1 ] || { log "build failed after 3 attempts"; exit 1; }
docker images | grep -E 'qnx65|REPOSITORY'
log "done"
