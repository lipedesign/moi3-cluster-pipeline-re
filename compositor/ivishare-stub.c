/* ivishare-stub.c — a fake Wayland compositor that ALSO offers the LG `ivi_share` extension, so
 * the real MOI3 `multi-screen-server` gets past "ivi_share not supported" and can be driven on a
 * PC workbench.
 *
 * Derived from the MOI3 sandbox compositor `vc_wlstub.c`: it occupies
 * $XDG_RUNTIME_DIR/wayland-0, answers the minimum of wl_display / registry / wl_compositor /
 * ivi_application / wl_output so the firmware client proceeds, and does NOT composite — drawing
 * goes through the OpenGL forwarder (glhost), not Wayland. On top of that it announces
 * `ivi_share` and implements get_ivi_share_surface + the configure / input_capabilities /
 * share_surface_state events (protocol in ../docs/ivi-share-protocol.md).
 *
 * The `damage` event (opcode 0, `hh`: the frame as a dma-buf fd + fence) is intentionally NOT
 * sent yet — delivering a real buffer is the next, load-bearing step of the spike. For now the
 * goal is to get the consumer past the missing global and watch how it asks for the frame.
 *
 *   ivishare-stub <socket> [width height [mm_w mm_h]]
 *   e.g.: ivishare-stub /run/platform/weston/wayland-0 800 480 174 104
 *
 * Each incoming request prints one line on stdout. Static; meant to run only inside the
 * throwaway container, no network. The client is firmware.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#ifndef SYS_memfd_create
#define SYS_memfd_create 279   /* arm64 */
#endif

enum { T_NADA, T_DISPLAY, T_REGISTRO, T_RETORNO, T_COMPOSITOR, T_IVIAPP, T_SAIDA, T_SUPERFICIE,
       T_REGIAO, T_IVISUP, T_IVISHARE, T_IVISHARESURF };
static const char *NOME[] = { "?", "wl_display", "wl_registry", "wl_callback", "wl_compositor",
                              "ivi_application", "wl_output", "wl_surface", "wl_region",
                              "ivi_surface", "ivi_share", "ivi_share_surface" };
#define MAXC 16
#define MAXO 4096
#define MAXP 64

/* ivi_share_surface configure values — provisional, enough to drive the consumer up to where it
 * waits for `damage`. 1280x480 is the cluster map stream size; format is a DRM fourcc guess. */
#define SHARE_W 1280
#define SHARE_H 480
#define SHARE_STRIDE (SHARE_W * 4)
#define DRM_FORMAT_XRGB8888 0x34325258u   /* 'XR24' */

struct cli {
    int fd, n;                          /* socket and client number */
    uint8_t tipo[MAXO];
    uint32_t pend[MAXP]; int npend;     /* frame callbacks awaiting commit */
    uint8_t rx[65536]; size_t len;
    unsigned long pedidos;
};
static struct cli C[MAXC];
static int g_w = 800, g_h = 480, g_mmw = 174, g_mmh = 104;
static uint32_t g_serie = 1;
static double t0;

static double agora(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec + (double)t.tv_nsec / 1e9; }
#define LOG(...) do { printf("%8.3f ", agora() - t0); printf(__VA_ARGS__); printf("\n"); } while (0)

/* ---- event marshalling ---- */
static uint8_t tx[1024]; static size_t txl;
static void e_ini(void) { txl = 8; }
static void e_u32(uint32_t v) { memcpy(tx + txl, &v, 4); txl += 4; }
static void e_str(const char *s) { uint32_t n = (uint32_t)strlen(s) + 1, p = (n + 3) & ~3u; e_u32(n); memset(tx + txl, 0, p); memcpy(tx + txl, s, n); txl += p; }
static void e_fim(struct cli *c, uint32_t obj, uint32_t op)
{
    uint32_t w = ((uint32_t)txl << 16) | op;
    memcpy(tx, &obj, 4); memcpy(tx + 4, &w, 4);
    if (send(c->fd, tx, txl, MSG_NOSIGNAL) < 0) LOG("c%d send failed: %s", c->n, strerror(errno));
}
static void apaga_id(struct cli *c, uint32_t id) { e_ini(); e_u32(id); e_fim(c, 1, 1); if (id < MAXO) c->tipo[id] = T_NADA; }
static void retorno_feito(struct cli *c, uint32_t id) { e_ini(); e_u32(g_serie++); e_fim(c, id, 0); apaga_id(c, id); }

/* ivi_share_surface events sent right after get_ivi_share_surface. Opcodes from the protocol:
 * configure=1 (uuuuu), input_capabilities=2 (u), share_surface_state=3 (u). damage=0 (hh) later.
 *
 * configure arg mapping is EMPIRICAL (RE of FUN_00112fb0 + spike-5): of the 5 uints, the mss uses
 * arg1 -> width (ctx[0xc]), arg2 -> height (ctx[0x10]), arg4 -> a fourcc selector read by cb_damage
 * (1->AR24, 2->XR24, else->AB24). arg0 and arg3 are unused. So width/height go in the 2nd/3rd
 * slots and the selector 2 (XR24, our XRGB8888 buffer) in the 5th. See ../docs/shareserver-pipeline.md. */
#define FOURCC_SEL_XR24 2u
static void share_eventos(struct cli *c, uint32_t id)
{
    e_ini(); e_u32(SHARE_W); e_u32(SHARE_W); e_u32(SHARE_H); e_u32(SHARE_STRIDE); e_u32(FOURCC_SEL_XR24); e_fim(c, id, 1);
    e_ini(); e_u32(0); e_fim(c, id, 2);   /* input_capabilities = none */
    e_ini(); e_u32(1); e_fim(c, id, 3);   /* share_surface_state = 1 (provisional) */
    LOG("c%d ivi_share_surface(%u): sent configure w=%d h=%d (arg1/arg2), fourcc_sel=%u -> XR24, caps, state", c->n, id, SHARE_W, SHARE_H, FOURCC_SEL_XR24);
}

/* A test-pattern buffer (8 colour bars, XRGB8888) shared with the mss via the damage event. */
static int g_buffd = -1;
static struct { int ci; uint32_t id; } g_shares[MAXC]; static int g_nsh;
static double g_last_dmg;
static void criar_buffer(void)
{
    size_t sz = (size_t)SHARE_STRIDE * SHARE_H;
    int fd = (int)syscall(SYS_memfd_create, "share_buf", 1u);
    if (fd < 0) { LOG("memfd_create failed: %s", strerror(errno)); return; }
    if (ftruncate(fd, (off_t)sz) != 0) { LOG("ftruncate failed: %s", strerror(errno)); close(fd); return; }
    uint32_t *px = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (px == MAP_FAILED) { LOG("mmap failed: %s", strerror(errno)); close(fd); return; }
    static const uint32_t bar[8] = { 0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0x00FFFF00u,
                                      0x0000FFFFu, 0x00FF00FFu, 0x00FFFFFFu, 0x00202020u };
    for (uint32_t y = 0; y < (uint32_t)SHARE_H; y++)
        for (uint32_t x = 0; x < (uint32_t)SHARE_W; x++)
            px[y * (SHARE_STRIDE / 4) + x] = bar[(x * 8) / SHARE_W];
    munmap(px, sz);
    g_buffd = fd;
    LOG("test-pattern buffer ready: fd %d, %ux%u stride %u", fd, SHARE_W, SHARE_H, SHARE_STRIDE);
}

/* send an event whose arguments are file descriptors (Wayland 'h'): empty body, the fds travel
 * as SCM_RIGHTS ancillary data on the same sendmsg. */
static void e_fim_fds(struct cli *c, uint32_t obj, uint32_t op, int *fds, int nfds)
{
    uint32_t w = ((uint32_t)txl << 16) | op;
    char cbuf[CMSG_SPACE(sizeof(int) * 8)];
    struct msghdr msg;
    struct iovec io;
    struct cmsghdr *cm;
    memcpy(tx, &obj, 4); memcpy(tx + 4, &w, 4);
    memset(&msg, 0, sizeof msg);
    memset(cbuf, 0, sizeof cbuf);
    io.iov_base = tx; io.iov_len = txl;
    msg.msg_iov = &io; msg.msg_iovlen = 1;
    msg.msg_control = cbuf; msg.msg_controllen = CMSG_SPACE(sizeof(int) * nfds);
    cm = CMSG_FIRSTHDR(&msg);
    cm->cmsg_level = SOL_SOCKET; cm->cmsg_type = SCM_RIGHTS;
    cm->cmsg_len = CMSG_LEN(sizeof(int) * nfds);
    memcpy(CMSG_DATA(cm), fds, sizeof(int) * nfds);
    if (sendmsg(c->fd, &msg, MSG_NOSIGNAL) < 0) LOG("c%d sendmsg(fds) failed: %s", c->n, strerror(errno));
}

/* re-send the shared frame to each active share surface — the consumer expects a continuous
 * stream of frames (it runs at a fixed fps), not a single damage. */
static void envia_damages(void)
{
    int i, fds[2];
    for (i = 0; i < g_nsh; i++) {
        struct cli *c = &C[g_shares[i].ci];
        if (c->fd < 0) continue;
        fds[0] = g_buffd; fds[1] = g_buffd;
        e_ini();
        e_fim_fds(c, g_shares[i].id, 0, fds, 2);
    }
}

static uint32_t u32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static void novo(struct cli *c, uint32_t id, int tipo) { if (id < MAXO) c->tipo[id] = (uint8_t)tipo; }

static void pedido(struct cli *c, uint32_t obj, uint32_t op, const uint8_t *a, size_t n)
{
    int t = obj < MAXO ? c->tipo[obj] : T_NADA;
    c->pedidos++;
    switch (t) {
    case T_DISPLAY:
        if (op == 0 && n >= 4) { LOG("c%d wl_display.sync", c->n); retorno_feito(c, u32(a)); return; }
        if (op == 1 && n >= 4) {
            uint32_t r = u32(a);
            novo(c, r, T_REGISTRO);
            LOG("c%d wl_display.get_registry -> announce wl_compositor, ivi_application, wl_output, ivi_share", c->n);
            e_ini(); e_u32(1); e_str("wl_compositor"); e_u32(3); e_fim(c, r, 0);
            e_ini(); e_u32(2); e_str("ivi_application"); e_u32(1); e_fim(c, r, 0);
            e_ini(); e_u32(3); e_str("wl_output"); e_u32(2); e_fim(c, r, 0);
            e_ini(); e_u32(4); e_str("ivi_share"); e_u32(1); e_fim(c, r, 0);
            return;
        }
        break;
    case T_REGISTRO:
        if (op == 0 && n >= 16) {
            uint32_t nome = u32(a), sl = u32(a + 4), pad = (sl + 3) & ~3u, ver, id;
            char itf[64] = "";
            if (8 + (size_t)pad + 8 > n) break;
            memcpy(itf, a + 8, sl < sizeof itf ? sl : sizeof itf - 1);
            ver = u32(a + 8 + pad); id = u32(a + 12 + pad);
            LOG("c%d wl_registry.bind %s version %u -> object %u", c->n, itf, ver, id);
            if (nome == 1) novo(c, id, T_COMPOSITOR);
            else if (nome == 2) novo(c, id, T_IVIAPP);
            else if (nome == 4) novo(c, id, T_IVISHARE);
            else if (nome == 3) {
                novo(c, id, T_SAIDA);
                e_ini(); e_u32(0); e_u32(0); e_u32((uint32_t)g_mmw); e_u32((uint32_t)g_mmh); e_u32(0);
                e_str("moi3-bancada"); e_str("ivishare-stub"); e_u32(0); e_fim(c, id, 0);          /* geometry */
                e_ini(); e_u32(3); e_u32((uint32_t)g_w); e_u32((uint32_t)g_h); e_u32(60000); e_fim(c, id, 1); /* mode */
                if (ver >= 2) { e_ini(); e_u32(1); e_fim(c, id, 3); e_ini(); e_fim(c, id, 2); }   /* scale, done */
            }
            return;
        }
        break;
    case T_COMPOSITOR:
        if (op == 0 && n >= 4) { novo(c, u32(a), T_SUPERFICIE); LOG("c%d wl_compositor.create_surface -> object %u", c->n, u32(a)); return; }
        if (op == 1 && n >= 4) { novo(c, u32(a), T_REGIAO); LOG("c%d wl_compositor.create_region -> object %u", c->n, u32(a)); return; }
        break;
    case T_IVIAPP:
        if (op == 0 && n >= 12) {
            novo(c, u32(a + 8), T_IVISUP);
            LOG("c%d ivi_application.surface_create ivi id %u, surface %u -> object %u", c->n, u32(a), u32(a + 4), u32(a + 8));
            return;
        }
        break;
    case T_IVISHARE:
        if (op == 0 && n >= 8) {   /* get_ivi_share_surface(new_id, surface_id) 'nu' */
            uint32_t nid = u32(a), sid = u32(a + 4);
            novo(c, nid, T_IVISHARESURF);
            LOG("c%d ivi_share.get_ivi_share_surface id %u for surface_id %u", c->n, nid, sid);
            share_eventos(c, nid);
            if (g_buffd >= 0 && g_nsh < MAXC) {
                g_shares[g_nsh].ci = (int)(c - C); g_shares[g_nsh].id = nid; g_nsh++;
                LOG("c%d ivi_share_surface(%u): registered for periodic damage (buffer fd %d)", c->n, nid, g_buffd);
            }
            return;
        }
        break;
    case T_IVISHARESURF: {
        static const char *o[] = { "destroy", "redirect_touch_down", "redirect_touch_up",
                                   "redirect_touch_motion", "redirect_touch_frame", "redirect_touch_cancel" };
        LOG("c%d ivi_share_surface(%u).%s", c->n, obj, op < 6 ? o[op] : "?");
        if (op == 0) apaga_id(c, obj);
        return;
    }
    case T_SUPERFICIE: {
        static const char *o[] = { "destroy", "attach", "damage", "frame", "set_opaque_region", "set_input_region",
                                   "commit", "set_buffer_transform", "set_buffer_scale", "damage_buffer" };
        if (op == 3 && n >= 4) { novo(c, u32(a), T_RETORNO); if (c->npend < MAXP) c->pend[c->npend++] = u32(a); }
        if (op == 6) { int i; for (i = 0; i < c->npend; i++) retorno_feito(c, c->pend[i]); c->npend = 0; }
        if (c->pedidos < 400 || op == 0) LOG("c%d wl_surface(%u).%s", c->n, obj, op < 10 ? o[op] : "?");
        if (op == 0) apaga_id(c, obj);
        return;
    }
    case T_REGIAO: if (op == 0) apaga_id(c, obj); return;
    case T_IVISUP: LOG("c%d ivi_surface(%u).%s", c->n, obj, op == 0 ? "destroy" : "?"); if (op == 0) apaga_id(c, obj); return;
    case T_SAIDA: return;
    }
    LOG("c%d untreated request: object %u (%s) op %u, %zu arg bytes", c->n, obj, NOME[t], op, n);
}

int main(int argc, char **argv)
{
    struct sockaddr_un a;
    struct pollfd p[MAXC + 1];
    int ls, i, prox = 1;
    if (argc < 2) { fprintf(stderr, "usage: ivishare-stub <socket> [width height [mm_w mm_h]]\n"); return 2; }
    if (argc >= 4) { g_w = atoi(argv[2]); g_h = atoi(argv[3]); }
    if (argc >= 6) { g_mmw = atoi(argv[4]); g_mmh = atoi(argv[5]); }
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IOLBF, 0);
    t0 = agora();
    unlink(argv[1]);
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    strncpy(a.sun_path, argv[1], sizeof a.sun_path - 1);
    ls = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ls < 0 || bind(ls, (struct sockaddr *)&a, sizeof a) < 0 || listen(ls, 8) < 0) { perror("socket"); return 2; }
    chmod(argv[1], 0777);
    for (i = 0; i < MAXC; i++) C[i].fd = -1;
    criar_buffer();
    LOG("listening on %s; announced output %dx%d, %dx%d mm; ivi_share enabled", argv[1], g_w, g_h, g_mmw, g_mmh);
    for (;;) {
        int n = 1;
        p[0].fd = ls; p[0].events = POLLIN;
        for (i = 0; i < MAXC; i++) if (C[i].fd >= 0) { p[n].fd = C[i].fd; p[n].events = POLLIN; n++; }
        if (poll(p, (nfds_t)n, 33) < 0) { if (errno == EINTR) continue; break; }
        if (p[0].revents & POLLIN) {
            int fd = accept(ls, NULL, NULL);
            for (i = 0; i < MAXC && fd >= 0; i++) if (C[i].fd < 0) {
                memset(&C[i], 0, sizeof C[i]);
                C[i].fd = fd; C[i].n = prox++; C[i].tipo[1] = T_DISPLAY;
                LOG("c%d connected", C[i].n); fd = -1;
            }
            if (fd >= 0) close(fd);
        }
        for (n = 1; n < MAXC + 1 && n < (int)(sizeof p / sizeof p[0]); n++) {
            struct cli *c = NULL;
            ssize_t r;
            if (!(p[n].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            for (i = 0; i < MAXC; i++) if (C[i].fd == p[n].fd) c = &C[i];
            if (!c) continue;
            r = recv(c->fd, c->rx + c->len, sizeof c->rx - c->len, 0);
            if (r <= 0) { LOG("c%d left after %lu requests", c->n, c->pedidos); close(c->fd); c->fd = -1; continue; }
            c->len += (size_t)r;
            for (;;) {
                uint32_t obj, w, tam;
                if (c->len < 8) break;
                obj = u32(c->rx); w = u32(c->rx + 4); tam = w >> 16;
                if (tam < 8 || tam > sizeof c->rx) { LOG("c%d invalid message (size %u)", c->n, tam); c->len = 0; break; }
                if (c->len < tam) break;
                pedido(c, obj, w & 0xffff, c->rx + 8, tam - 8);
                memmove(c->rx, c->rx + tam, c->len - tam); c->len -= tam;
            }
        }
        if (g_nsh && agora() - g_last_dmg >= 0.033) { envia_damages(); g_last_dmg = agora(); }
    }
    return 0;
}
