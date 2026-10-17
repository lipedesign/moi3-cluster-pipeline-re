/* fakegbm.c — a stand-in libgbm.so for the MOI3 cluster-map PoC.
 *
 * The real multi-screen-server links the Qualcomm msm_gbm (libgbm.so), which needs
 * /dev/dri/card0 (KGSL) and /dev/ion — neither exists off the SoC. The mss only ever touches
 * GBM through the public API (9 functions), never the internals, so we provide our own libgbm:
 * a buffer object is a plain memfd with width/height/stride/format, handed out as an fd. Our EGL
 * import side (glhost) reads that fd back; nothing here needs a GPU or a DRM device.
 *
 * Linked against the firmware glibc (like the glforward shim) so it loads inside the chroot
 * without mixing C libraries. Every call logs once so the next missing piece is always named.
 * Runs only in the throwaway container; the client is firmware. Nothing goes to a vehicle.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#ifndef SYS_memfd_create
#define SYS_memfd_create 279   /* arm64 */
#endif

#define LOGF(...) do { fprintf(stderr, "[fakegbm] " __VA_ARGS__); fflush(stderr); } while (0)

struct gbm_device { int fd; };
struct gbm_bo { int memfd; uint32_t w, h, stride, format; uint64_t size; };

/* GBM_BO_IMPORT_FD = 0x5503; its buffer arg is struct gbm_import_fd_data. */
struct gbm_import_fd_data { int fd; uint32_t width, height, stride, format; };

/* The mss's ShareServer::cb_damage (RE: FUN_00115860 @ 0x115860) imports each frame with a
 * Qualcomm-private type 0x4fff and this struct layout: { int fd; u32 fence; u32 w; u32 h; u32 drm_fourcc }.
 * The buffer fd is the dma-buf delivered in the ivi_share_surface.damage event (hh). See
 * ../docs/shareserver-pipeline.md. */
struct msm_import_data { int fd; uint32_t fence; uint32_t width, height, fourcc; };

static int mk_memfd(uint64_t size)
{
    int fd = (int)syscall(SYS_memfd_create, "fakegbm_bo", 1u /* MFD_CLOEXEC */);
    if (fd >= 0 && ftruncate(fd, (off_t)size) != 0) { close(fd); return -1; }
    return fd;
}
static struct gbm_bo *mk_bo(uint32_t w, uint32_t h, uint32_t fmt, uint32_t stride, int importfd)
{
    struct gbm_bo *bo = calloc(1, sizeof *bo);
    if (!bo) return 0;
    bo->w = w; bo->h = h; bo->format = fmt;
    bo->stride = stride ? stride : w * 4;
    bo->size = (uint64_t)bo->stride * h;
    bo->memfd = importfd >= 0 ? dup(importfd) : mk_memfd(bo->size);
    LOGF("bo %p: %ux%u stride %u fmt %#x fd %d%s\n", (void *)bo, w, h, bo->stride, fmt,
         bo->memfd, importfd >= 0 ? " (imported)" : "");
    return bo;
}

struct gbm_device *gbm_create_device(int fd)
{
    struct gbm_device *d = calloc(1, sizeof *d);
    if (d) d->fd = fd;
    LOGF("gbm_create_device(fd=%d) -> %p\n", fd, (void *)d);
    return d;
}
void gbm_device_destroy(struct gbm_device *g) { LOGF("gbm_device_destroy(%p)\n", (void *)g); free(g); }

struct gbm_bo *gbm_bo_create(struct gbm_device *g, uint32_t w, uint32_t h, uint32_t fmt, uint32_t flags)
{
    (void)g; LOGF("gbm_bo_create(%ux%u fmt %#x flags %#x)\n", w, h, fmt, flags);
    return mk_bo(w, h, fmt, 0, -1);
}
struct gbm_bo *gbm_bo_import(struct gbm_device *g, uint32_t type, void *buffer, uint32_t flags)
{
    (void)g; LOGF("gbm_bo_import(type=%#x buffer=%p flags=%#x)\n", type, buffer, flags);
    if (type == 0x5503u && buffer) {   /* GBM_BO_IMPORT_FD */
        struct gbm_import_fd_data *d = buffer;
        LOGF("  import_fd: fd=%d %ux%u stride=%u fmt=%#x\n", d->fd, d->width, d->height, d->stride, d->format);
        return mk_bo(d->width, d->height, d->format, d->stride, d->fd);
    }
    if (type == 0x4fffu && buffer) {   /* Qualcomm-private type the mss cb_damage uses */
        struct msm_import_data *d = buffer;
        LOGF("  import 0x4fff: fd=%d fence=%u %ux%u fourcc=%#x\n", d->fd, d->fence, d->width, d->height, d->fourcc);
        return mk_bo(d->width, d->height, d->fourcc, 0 /* stride = w*4 */, d->fd);
    }
    LOGF("  import type %#x not handled yet; placeholder bo\n", type);
    return mk_bo(1280, 480, 0x34325258u, 0, -1);
}
void gbm_bo_destroy(struct gbm_bo *bo) { if (bo) { if (bo->memfd >= 0) close(bo->memfd); free(bo); } }
int gbm_bo_get_fd(struct gbm_bo *bo) { int fd = bo ? dup(bo->memfd) : -1; LOGF("gbm_bo_get_fd(%p) -> %d\n", (void *)bo, fd); return fd; }
uint32_t gbm_bo_get_height(struct gbm_bo *bo) { return bo ? bo->h : 0; }
uint32_t gbm_bo_get_stride(struct gbm_bo *bo) { return bo ? bo->stride : 0; }
uint32_t gbm_bo_get_width(struct gbm_bo *bo) { return bo ? bo->w : 0; }
uint32_t gbm_bo_get_format(struct gbm_bo *bo) { return bo ? bo->format : 0; }

/* Qualcomm extension: int gbm_perform(int operation, ...). The mss's cb_damage (memcpy mode) uses
 * three ops per frame: op 8 = "is this bo CPU-mappable/linear?" (return non-zero so it proceeds and
 * computes size = stride*height), op 6 = CPU map (hand back a readable pointer to bo->size bytes),
 * op 7 = CPU unmap. Without a real map the pass buffer stays empty and the encoder is never fed
 * (RE 17/10/2026: feeder gate requires passbuf!=0). We map the imported dma-buf when its fd is at
 * least bo->size (real pixels); otherwise an anonymous zeroed page of bo->size (a blank frame — safe,
 * no SIGBUS). The copy in the mss is clamped to the input buffer's nAllocLen, so no overflow. */
int gbm_perform(int op, ...)
{
    va_list ap; va_start(ap, op);
    int ret = 0;
    if (op == 8) {                     /* query: report mappable/linear so cb_damage CPU-maps */
        ret = 1;
    } else if (op == 6 || op == 7) {   /* 6 = CPU map, 7 = CPU unmap; args: (bo, void **out) */
        struct gbm_bo *bo = va_arg(ap, struct gbm_bo *);
        void **out = va_arg(ap, void **);
        uint64_t sz = bo ? bo->size : 0;
        if (op == 6) {
            void *p = MAP_FAILED;
            /* glibc 2.23 (firmware) does not export fstat (it inlines to __fxstat); use lseek,
             * a real exported symbol, to size the fd before mapping it (avoids SIGBUS). */
            if (bo && bo->memfd >= 0 && sz) {
                off_t end = lseek(bo->memfd, 0, SEEK_END);
                if (end >= 0) lseek(bo->memfd, 0, SEEK_SET);
                if (end >= 0 && (uint64_t)end >= sz)
                    p = mmap(NULL, sz, PROT_READ, MAP_SHARED, bo->memfd, 0);    /* real dma-buf */
            }
            if (p == MAP_FAILED && sz)
                p = mmap(NULL, sz, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);                   /* blank fallback */
            if (out) *out = (p == MAP_FAILED) ? (void *)-1 : p;
            LOGF("gbm_perform(6 map) bo=%p size=%llu -> %p\n", (void *)bo,
                 (unsigned long long)sz, out ? *out : (void *)0);
        } else {
            if (bo && out && *out && *out != (void *)-1) munmap(*out, sz);
            LOGF("gbm_perform(7 unmap) bo=%p\n", (void *)bo);
        }
    } else {
        LOGF("gbm_perform(op=%d) [stub -> 0]\n", op);
    }
    va_end(ap);
    return ret;
}
