#!/bin/sh
# in_container.sh — spike harness, runs INSIDE moi3-bancada:1.
# Brings up the real multi-screen-server (mss) against our ivi_share-enabled compositor,
# isolated (no HMI, no other services), and captures how far it gets and how it asks for the
# frame buffer. Throwaway container, no network. Nothing goes to a vehicle.
#
# Mounts expected:  /fw = <firmware sysroot> (system.img + vw), ro ;  /glf = glforward-moi3 ;
#                   /poc = moi3-cluster-pipeline-re (our code) ;  /out = output dir
set -e
W=/tmp/w; LOW=/mnt/low; O=/out
DLLP=/lge/app_ro/lib64:/usr/lib64:/usr/lib:/lib64:/lib:/lge/hmi/libs:/lge/app_ro/hmi:/lge/app_ro/carif
SECS=${SECS:-15}
MSS=/lge/app_ro/graphics/multi-display/multi-screen-server
# Per the binary's Usage, the mss takes the stream dest and the surface id to share on the
# command line — passing them directly avoids bringing up the whole RSI/displaymanagement stack
# just to tell it what to share. 1000 is the ivi id our test surface will use.
MSS_ARGS="${MSS_ARGS:-ff14::2:4 42800 -m 1000 30}"

mkdir -p $W/fwlibs $W/bin $W/glfwd $LOW $O/frames
mount -o loop,ro,noload /fw/system.img $LOW
mount --bind /fw/vw $LOW/vw
# system.img is read-only; give the chroot writable /tmp and /run (tmpfs), as m1_hmi.sh does
mount -t tmpfs tmpfs $LOW/tmp
mount -t tmpfs tmpfs $LOW/run
cp -L $LOW/lib64/libc.so.6 $LOW/lib64/libpthread.so.0 $LOW/lib64/ld-linux-aarch64.so.1 $W/fwlibs/

echo "[spike] building glforward + compositor" >&2
FWLIBS=$W/fwlibs sh /out/glf-ext/build.sh $W/glfwd > $O/build.log 2>&1
gcc -static -O2 -Wall -o $W/bin/ivishare-stub /poc/compositor/ivishare-stub.c >> $O/build.log 2>&1
gcc -static -O2 -Wall -o $W/bin/tracecatch /poc/spike/tracecatch.c >> $O/build.log 2>&1

# place the fake Adreno GL libs where the chrooted client looks (/tmp/fakegl inside $LOW)
mkdir -p $LOW/tmp/fakegl $LOW/run/platform/weston
cp $W/glfwd/libGLESv2_adreno.so $W/glfwd/libEGL_adreno.so $LOW/tmp/fakegl/
# M1: fakegbm — our libgbm.so, linked against the firmware glibc so it loads in the chroot
gcc -shared -fPIC -O2 -Wall -fno-stack-protector -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
    -mno-outline-atomics -nostdlib -Wl,-soname,libgbm.so -Wl,-rpath-link,$W/fwlibs \
    -o $LOW/tmp/fakegl/libgbm.so /poc/shims/fakegbm.c $W/fwlibs/libc.so.6 >> $O/build.log 2>&1
# M4: fakeomxcore — our libOmxCore.so (OMX IL encoder stand-in), so OMX_GetHandle returns our
# component and the real libOmxVenc / /dev/video33 are never used.
gcc -shared -fPIC -O2 -Wall -fno-stack-protector -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
    -mno-outline-atomics -nostdlib -I/poc/shims -Wl,-soname,libOmxCore.so -Wl,-rpath-link,$W/fwlibs \
    -o $LOW/tmp/fakegl/libOmxCore.so /poc/shims/fakeomxcore.c $W/fwlibs/libc.so.6 $W/fwlibs/libpthread.so.0 >> $O/build.log 2>&1
# fakedrm — LD_PRELOADed; gives the mss an openable /dev/dri render node and fakes the DRM auth
# handshake, so ShareServer::cb_damage passes its render-fd guard. See ../shims/fakedrm.c.
gcc -shared -fPIC -O2 -Wall -fno-stack-protector -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
    -mno-outline-atomics -nostdlib -Wl,-soname,fakedrm.so -Wl,-rpath-link,$W/fwlibs \
    -o $LOW/tmp/fakegl/fakedrm.so /poc/shims/fakedrm.c $W/fwlibs/libc.so.6 >> $O/build.log 2>&1

echo "[spike] starting trace collector + compositor (ivi_share) + glhost" >&2
$W/bin/tracecatch $LOW/tmp/trace_unix_socket > $O/trace.log 2>&1 &
TR=$!
$W/bin/ivishare-stub $LOW/run/platform/weston/wayland-0 800 480 174 104 > $O/wlstub.log 2>&1 &
WL=$!
GLHOST_STREAM= $W/glfwd/glhost $LOW/tmp/moi3-gl-mss.sock 1280 480 $O/frames > $O/glhost.log 2>&1 &
GH=$!
sleep 1

echo "[spike] running mss for ${SECS}s under strace" >&2
cat > $LOW/tmp/run_mss.sh <<EOS
export XDG_RUNTIME_DIR=/run/platform/weston
export LD_LIBRARY_PATH=/tmp/fakegl:$DLLP
export LD_PRELOAD=/tmp/fakegl/fakedrm.so
export MOI3_GL_SOCKET=/tmp/moi3-gl-mss.sock
exec $MSS $MSS_ARGS
EOS
timeout $SECS strace -f -s 200 \
  -e trace=openat,connect,socket,recvmsg,sendmsg,mmap,ioctl,memfd_create,execve,exit_group \
  -o $O/mss.strace chroot $LOW /bin/sh /tmp/run_mss.sh > $O/mss.out 2>&1 || true

sleep 0.5
kill $WL $GH $TR 2>/dev/null || true

{
  echo "=== M1 criterion: did the compositor reach get_ivi_share_surface? ==="
  grep 'get_ivi_share_surface' $O/wlstub.log 2>/dev/null || echo "(not reached)"
  echo; echo "=== shim calls (fakegbm / fakeomx / img) ==="; grep -E '\[fake|\[glfwd/img\]' $O/mss.out 2>/dev/null | head -120
  echo; echo "=== mss internal trace (libtraceif) ==="; cat $O/trace.log 2>/dev/null | head -80
  echo; echo "=== mss stdout/stderr ==="; cat $O/mss.out 2>/dev/null
  echo; echo "=== compositor (ivi_share) log ==="; cat $O/wlstub.log 2>/dev/null
  echo; echo "=== devices the mss opened ==="
  grep -E 'openat\([^)]*(/dev/|dri|ion|udmabuf|render|gbm)' $O/mss.strace 2>/dev/null | head -40
  echo; echo "=== fd passing / memfd / ioctl heads ==="
  grep -E 'recvmsg|SCM_RIGHTS|memfd_create|DRM_IOCTL|ION' $O/mss.strace 2>/dev/null | head -30
} > $O/resumo.txt 2>&1
echo "[spike] done; see /out/resumo.txt" >&2
ls -la $O
