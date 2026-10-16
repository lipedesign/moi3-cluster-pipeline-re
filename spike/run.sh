#!/bin/sh
# run.sh — launch the ivi_share spike in a throwaway container (run on the host).
#   sh run.sh [output-dir]
#
# You bring the firmware yourself; set FW and GLF to point at your own extracted sysroot and the
# glforward sources. Defaults assume the layout of this research workspace (firmware NOT shipped
# in this repo). The container runs --privileged (loop-mounts system.img) and with no network.
set -e
POC=$(cd "$(dirname "$0")/.." && pwd)                      # moi3-cluster-pipeline-re
VAG=$(cd "$POC/.." && pwd)
SB="$VAG/referencia/ferramentas/mib3-sandbox"
FW=${FW:-$SB/sysroot/mu0883}                               # system.img + vw/
GLF=${GLF:-$SB/glforward-moi3}                             # OpenGL forwarder sources
OUT=${1:-/tmp/ivishare-spike}
IMG=${IMG:-moi3-bancada:1}

[ -f "$FW/system.img" ] || { echo "no firmware at $FW (set FW=...)"; exit 1; }
[ -f "$GLF/build.sh" ] || { echo "no glforward at $GLF (set GLF=...)"; exit 1; }
mkdir -p "$OUT"

# patch the glforward sources on the host (the container has no python3); mount the result as /out
echo "patching glforward -> $OUT/glf-ext"
python3 -I "$POC/glforward-ext/apply.py" "$GLF" "$OUT/glf-ext" || { echo "glforward patch failed"; exit 1; }

docker run --rm --privileged --network none --name moi3-ivishare-spike \
  -e SECS="${SECS:-15}" \
  -v "$FW":/fw:ro -v "$GLF":/glf:ro -v "$POC":/poc:ro -v "$OUT":/out \
  "$IMG" sh /poc/spike/in_container.sh
echo "output in $OUT"
