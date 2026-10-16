/* fakedrm.c — LD_PRELOAD shim that gives the MOI3 mss an openable DRM render node.
 *
 * On the SoC the mss opens /dev/dri/renderD128 and runs the DRM auth handshake
 * (DRM_IOCTL_GET_MAGIC + DRM_IOCTL_AUTH_MAGIC) before it will import frames. In the throwaway
 * container there is no GPU and no /dev/dri, so that open returns -1 and the mss's
 * ShareServer::cb_damage (RE: FUN_00115860 @ 0x115860) rejects every frame at its render-fd guard
 * (`*(int*)(param_2+0x58) < 0`). We don't want a real GPU — our fakegbm already stands in for the
 * buffer side — we only need open() to hand back a valid fd and the DRM auth ioctls to "succeed".
 *
 * So we interpose openat/open (faking any /dev/dri node with a memfd) and ioctl (answer the DRM 'd' ioctls
 * on those fds). Everything else passes straight through to the kernel via syscall(), so we never
 * recurse into libc and never touch unrelated files. Linked against the firmware glibc like the
 * other shims, and LD_PRELOADed only inside the container. Nothing goes to a vehicle.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>

#ifndef SYS_memfd_create
#define SYS_memfd_create 279   /* arm64 */
#endif

#define LOGF(...) do { fprintf(stderr, "[fakedrm] " __VA_ARGS__); fflush(stderr); } while (0)

/* DRM ioctls (base 'd' = 0x64, struct drm_auth = { __u32 magic }). */
#define DRM_IOCTL_GET_MAGIC   0xC0046402u
#define DRM_IOCTL_AUTH_MAGIC  0x40046411u
#define IOC_TYPE(req)         (((req) >> 8) & 0xff)

#define MAXFAKE 8
static int fake_fds[MAXFAKE];
static int n_fake;

static int is_fake(int fd) {
    for (int i = 0; i < n_fake; i++) if (fake_fds[i] == fd) return 1;
    return 0;
}
static int is_dri(const char *path) { return path && strstr(path, "/dev/dri/") != 0; }

static int fake_open(const char *path) {
    int fd = (int)syscall(SYS_memfd_create, "fakedrm_node", 1u /* MFD_CLOEXEC */);
    if (fd >= 0 && n_fake < MAXFAKE) fake_fds[n_fake++] = fd;
    LOGF("open(%s) -> fake fd %d\n", path, fd);
    return fd;
}

int openat(int dirfd, const char *path, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = (mode_t)va_arg(ap, int); va_end(ap); }
    if (is_dri(path)) return fake_open(path);
    return (int)syscall(SYS_openat, dirfd, path, flags, mode);
}

int open(const char *path, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = (mode_t)va_arg(ap, int); va_end(ap); }
    if (is_dri(path)) return fake_open(path);
    return (int)syscall(SYS_openat, AT_FDCWD, path, flags, mode);
}

int ioctl(int fd, unsigned long req, ...) {
    void *arg; va_list ap; va_start(ap, req); arg = va_arg(ap, void *); va_end(ap);
    if (is_fake(fd)) {
        if (req == DRM_IOCTL_GET_MAGIC && arg) { *(uint32_t *)arg = 0xA5A5A5A5u; LOGF("GET_MAGIC fd=%d -> ok\n", fd); return 0; }
        if (req == DRM_IOCTL_AUTH_MAGIC) { LOGF("AUTH_MAGIC fd=%d -> ok\n", fd); return 0; }
        if (IOC_TYPE(req) == 0x64) { LOGF("DRM ioctl %#lx fd=%d -> ok (stub)\n", req, fd); return 0; }
        return 0;
    }
    return (int)syscall(SYS_ioctl, fd, req, arg);
}
