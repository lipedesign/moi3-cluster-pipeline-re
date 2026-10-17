/* fakeomxcore.c — a stand-in libOmxCore.so for the MOI3 cluster-map PoC (ASYNC version).
 *
 * The real mss loads libOmxCore + libOmxVenc; OMX_GetHandle("OMX.qcom.video.encoder.avc")
 * instantiates the Venus encoder, which opens /dev/video33 (V4L2) and dies off the SoC. We
 * replace libOmxCore entirely: our OMX_GetHandle returns our own component, so the real libOmxVenc
 * and /dev/video33 are never touched.
 *
 * OpenMAX IL is asynchronous: SendCommand/EmptyThisBuffer/FillThisBuffer must return immediately
 * and the component reports completion later via callbacks from its own thread. A synchronous stub
 * deadlocks the mss's Loaded->Idle (the mss waits for CmdComplete(Idle) on one thread while another
 * allocates buffers). So each component runs a worker thread; commands and buffers are queued and
 * their callbacks fire off-thread. Loaded->Idle completes once every port's buffers are allocated.
 *
 * Linked against the firmware glibc (loads in the chroot). Container-only; client is firmware.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "omx_il.h"

#define LOGF(...) do { fprintf(stderr, "[fakeomx] " __VA_ARGS__); fflush(stderr); } while (0)

#define IN_PORT  0
#define OUT_PORT 1
#define ENC_W 1280
#define ENC_H 480
#define QN 256

enum { J_STATE, J_ETB, J_FTB };
struct job { int kind; OMX_U32 state; OMX_BUFFERHEADERTYPE *buf; };

struct enc {
    OMX_COMPONENTTYPE comp;         /* first: handle == &comp */
    OMX_CALLBACKTYPE cb;
    OMX_PTR appdata;
    OMX_STATETYPE state;
    OMX_U32 w, h, bitrate, in_cnt, out_cnt, in_sz, out_sz;
    int in_alloc, out_alloc, want_idle;
    pthread_t th; pthread_mutex_t mx; pthread_cond_t cv; int running;
    struct job q[QN]; int qh, qt;
    OMX_BUFFERHEADERTYPE *outq[QN]; int outh, outt;   /* saídas primadas (FillThisBuffer), livres p/ encode */
    int enc_fd; unsigned long long frame_no;           /* socket do daemon encoder + timestamp */
};
static struct enc *E(OMX_HANDLETYPE h) { return (struct enc *)h; }
static int buffers_ok(struct enc *e) { return e->in_alloc >= (int)e->in_cnt && e->out_alloc >= (int)e->out_cnt; }

/* === host x264 encoder forwarding ===
 * x264/openh264 não linkam neste shim (-nostdlib contra a glibc 2.23 do FW), então o encoder real
 * roda no host (enc_daemon.py, PyAV/libx264). Encaminhamos o NV12 por um unix socket e recebemos
 * H.264 Annex-B. Protocolo: req "ENC0"(4) w(4) h(4) size(4) + NV12; resp nal(4) flags(4) + bytes. */
static int enc_writen(int fd, const void *p, size_t n)
{ const char *b = p; while (n) { ssize_t r = write(fd, b, n); if (r <= 0) return -1; b += r; n -= (size_t)r; } return 0; }
static int enc_readn(int fd, void *p, size_t n)
{ char *b = p; while (n) { ssize_t r = read(fd, b, n); if (r <= 0) return -1; b += r; n -= (size_t)r; } return 0; }
static void put32(unsigned char *p, OMX_U32 v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24); }
static OMX_U32 get32(const unsigned char *p) { return (OMX_U32)p[0] | ((OMX_U32)p[1] << 8) | ((OMX_U32)p[2] << 16) | ((OMX_U32)p[3] << 24); }
static int enc_connect(void)
{
    const char *path = getenv("MOI3_ENC_SOCKET");
    if (!path || !*path) return -1;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un a; memset(&a, 0, sizeof a); a.sun_family = AF_UNIX;
    strncpy(a.sun_path, path, sizeof a.sun_path - 1);
    if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0) { close(fd); return -1; }
    LOGF("encoder daemon conectado em %s (fd %d)\n", path, fd);
    return fd;
}
static OMX_U32 enc_frame(struct enc *e, const OMX_U8 *nv12, OMX_U32 size, OMX_U8 *out, OMX_U32 outcap, OMX_U32 *flags)
{
    unsigned char hdr[16], rh[8]; OMX_U32 nal, fl, take, rest;
    *flags = 0;
    if (e->enc_fd < 0) e->enc_fd = enc_connect();
    if (e->enc_fd < 0) return 0;
    memcpy(hdr, "ENC0", 4); put32(hdr + 4, e->w); put32(hdr + 8, e->h); put32(hdr + 12, size);
    if (enc_writen(e->enc_fd, hdr, 16) || enc_writen(e->enc_fd, nv12, size)) goto fail;
    if (enc_readn(e->enc_fd, rh, 8)) goto fail;
    nal = get32(rh); fl = get32(rh + 4);
    take = nal < outcap ? nal : outcap;
    if (take && enc_readn(e->enc_fd, out, take)) goto fail;
    rest = nal - take;                       /* drena o excedente p/ não dessincronizar o stream */
    while (rest) { char tmp[4096]; OMX_U32 c = rest < sizeof tmp ? rest : (OMX_U32)sizeof tmp; if (enc_readn(e->enc_fd, tmp, c)) goto fail; rest -= c; }
    *flags = fl;
    return take;
fail:
    LOGF("encoder forward falhou; fechando fd %d\n", e->enc_fd);
    if (e->enc_fd >= 0) close(e->enc_fd);
    e->enc_fd = -1;
    return 0;
}

static void evt_cmdcomplete(struct enc *e, OMX_U32 data2)
{ if (e->cb.EventHandler) e->cb.EventHandler(&e->comp, e->appdata, OMX_EventCmdComplete, OMX_CommandStateSet, data2, NULL); }

static void *worker(void *arg)
{
    struct enc *e = arg;
    pthread_mutex_lock(&e->mx);
    while (e->running) {
        while (e->running && e->qh == e->qt && !(e->want_idle && buffers_ok(e)))
            pthread_cond_wait(&e->cv, &e->mx);
        if (!e->running) break;
        if (e->want_idle && buffers_ok(e)) {
            e->want_idle = 0; e->state = OMX_StateIdle;
            pthread_mutex_unlock(&e->mx);
            LOGF("Loaded->Idle complete (buffers in %u/%u out %u/%u)\n", e->in_alloc, e->in_cnt, e->out_alloc, e->out_cnt);
            evt_cmdcomplete(e, OMX_StateIdle);
            pthread_mutex_lock(&e->mx);
            continue;
        }
        if (e->qh != e->qt) {
            struct job j = e->q[e->qh]; e->qh = (e->qh + 1) % QN;
            pthread_mutex_unlock(&e->mx);
            if (j.kind == J_STATE) { e->state = (OMX_STATETYPE)j.state; LOGF("state -> %u (async)\n", j.state); evt_cmdcomplete(e, j.state); }
            else if (j.kind == J_ETB) {
                OMX_BUFFERHEADERTYPE *in = j.buf, *out = NULL;
                if (e->outh != e->outt) { out = e->outq[e->outh]; e->outh = (e->outh + 1) % QN; }  /* saída livre */
                if (out && in && in->pBuffer && in->nFilledLen) {
                    OMX_U32 fl = 0;
                    OMX_U32 nal = enc_frame(e, in->pBuffer, in->nFilledLen, out->pBuffer, out->nAllocLen, &fl);
                    out->nFilledLen = nal; out->nOffset = 0;
                    out->nTimeStamp = (OMX_TICKS)(e->frame_no * 33333);
                    out->nFlags = 0x10u /*ENDOFFRAME*/ | ((fl & 1u) ? 0x20u /*SYNCFRAME*/ : 0u);
                    e->frame_no++;
                    if (e->frame_no <= 5 || e->frame_no % 60 == 0)
                        LOGF("encoded frame #%llu -> %u bytes (idr=%d)\n", e->frame_no, nal, (int)(fl & 1u));
                    if (e->cb.FillBufferDone) e->cb.FillBufferDone(&e->comp, e->appdata, out);
                } else if (out) {                         /* sem input: devolve saída vazia p/ reciclar */
                    out->nFilledLen = 0; out->nFlags = 0;
                    if (e->cb.FillBufferDone) e->cb.FillBufferDone(&e->comp, e->appdata, out);
                }
                if (e->cb.EmptyBufferDone) e->cb.EmptyBufferDone(&e->comp, e->appdata, in);
            }
            else if (j.kind == J_FTB) {                   /* segura a saída primada p/ o próximo encode */
                if (j.buf) { e->outq[e->outt] = j.buf; e->outt = (e->outt + 1) % QN; }
            }
            pthread_mutex_lock(&e->mx);
        }
    }
    pthread_mutex_unlock(&e->mx);
    return NULL;
}
static void enq(struct enc *e, int kind, OMX_U32 state, OMX_BUFFERHEADERTYPE *buf)
{
    pthread_mutex_lock(&e->mx);
    e->q[e->qt].kind = kind; e->q[e->qt].state = state; e->q[e->qt].buf = buf; e->qt = (e->qt + 1) % QN;
    pthread_cond_signal(&e->cv);
    pthread_mutex_unlock(&e->mx);
}

static void fill_portdef(struct enc *e, OMX_PARAM_PORTDEFINITIONTYPE *p)
{
    OMX_U32 port = p->nPortIndex;
    p->eDir = port == IN_PORT ? OMX_DirInput : OMX_DirOutput;
    p->nBufferCountActual = port == IN_PORT ? e->in_cnt : e->out_cnt;
    p->nBufferCountMin = 1;
    p->nBufferSize = port == IN_PORT ? e->in_sz : e->out_sz;
    p->bEnabled = OMX_TRUE; p->bPopulated = OMX_FALSE;
    p->eDomain = 1; /* OMX_PortDomainVideo */
    p->format.video.nFrameWidth = e->w;
    p->format.video.nFrameHeight = e->h;
    p->format.video.nStride = (OMX_S32)(e->w);
    p->format.video.nSliceHeight = e->h;
    p->format.video.nBitrate = e->bitrate;
    p->format.video.xFramerate = 30 << 16;
    p->format.video.eCompressionFormat = port == IN_PORT ? OMX_VIDEO_CodingUnused : OMX_VIDEO_CodingAVC;
    p->format.video.eColorFormat = port == IN_PORT ? OMX_COLOR_FormatYUV420SemiPlanar : OMX_COLOR_FormatUnused;
}
static OMX_BUFFERHEADERTYPE *mk_bufhdr(OMX_U32 port, OMX_PTR app, OMX_U32 sz, OMX_U8 *buf)
{
    OMX_BUFFERHEADERTYPE *b = calloc(1, sizeof *b);
    b->nSize = sizeof *b; b->nVersion.s.nVersionMajor = 1; b->nVersion.s.nVersionMinor = 1; b->nVersion.s.nRevision = 2;
    b->pBuffer = buf; b->nAllocLen = sz; b->pAppPrivate = app;
    if (port == IN_PORT) b->nInputPortIndex = port; else b->nOutputPortIndex = port;
    return b;
}

static OMX_ERRORTYPE c_GetComponentVersion(OMX_HANDLETYPE h, OMX_STRING name, OMX_VERSIONTYPE *v, OMX_VERSIONTYPE *sv, OMX_U8 *uuid)
{ (void)h; (void)uuid; if (name) strcpy(name, "OMX.qcom.video.encoder.avc"); if (v) v->nVersion = 0;
  if (sv) { sv->s.nVersionMajor = 1; sv->s.nVersionMinor = 1; sv->s.nRevision = 2; sv->s.nStep = 0; } return OMX_ErrorNone; }

static OMX_ERRORTYPE c_SendCommand(OMX_HANDLETYPE h, OMX_COMMANDTYPE cmd, OMX_U32 param, OMX_PTR data)
{
    struct enc *e = E(h); (void)data;
    LOGF("SendCommand cmd=%d param=%u (state=%d)\n", cmd, param, e->state);
    if (cmd == OMX_CommandStateSet) {
        enq(e, J_STATE, param, NULL);   /* complete every state transition async, incl. Loaded->Idle */
        return OMX_ErrorNone;
    }
    if (e->cb.EventHandler) e->cb.EventHandler(h, e->appdata, OMX_EventCmdComplete, cmd, param, NULL);
    return OMX_ErrorNone;
}

static OMX_ERRORTYPE c_GetParameter(OMX_HANDLETYPE h, OMX_INDEXTYPE idx, OMX_PTR p)
{
    struct enc *e = E(h);
    if (idx == OMX_IndexParamPortDefinition && p) { fill_portdef(e, (OMX_PARAM_PORTDEFINITIONTYPE *)p); LOGF("GetParameter PortDefinition port=%u\n", ((OMX_PARAM_PORTDEFINITIONTYPE *)p)->nPortIndex); return OMX_ErrorNone; }
    LOGF("GetParameter idx=%#x\n", idx); return OMX_ErrorNone;
}
static OMX_ERRORTYPE c_SetParameter(OMX_HANDLETYPE h, OMX_INDEXTYPE idx, OMX_PTR p)
{
    struct enc *e = E(h);
    if (idx == OMX_IndexParamPortDefinition && p) {
        OMX_PARAM_PORTDEFINITIONTYPE *pd = p;
        if (pd->nPortIndex == IN_PORT) {
            if (pd->format.video.nFrameWidth) e->w = pd->format.video.nFrameWidth;
            if (pd->format.video.nFrameHeight) e->h = pd->format.video.nFrameHeight;
            if (pd->nBufferCountActual) e->in_cnt = pd->nBufferCountActual;
            if (pd->nBufferSize) e->in_sz = pd->nBufferSize;
        } else {
            if (pd->nBufferCountActual) e->out_cnt = pd->nBufferCountActual;
            if (pd->nBufferSize) e->out_sz = pd->nBufferSize;
        }
        LOGF("SetParameter PortDefinition port=%u %ux%u bufs=%u sz=%u\n", pd->nPortIndex, e->w, e->h,
             pd->nPortIndex == IN_PORT ? e->in_cnt : e->out_cnt, pd->nBufferSize);
        return OMX_ErrorNone;
    }
    LOGF("SetParameter idx=%#x\n", idx); return OMX_ErrorNone;
}
static OMX_ERRORTYPE c_GetConfig(OMX_HANDLETYPE h, OMX_INDEXTYPE i, OMX_PTR p) { (void)h; (void)p; LOGF("GetConfig idx=%#x\n", i); return OMX_ErrorNone; }
static OMX_ERRORTYPE c_SetConfig(OMX_HANDLETYPE h, OMX_INDEXTYPE i, OMX_PTR p) { (void)h; (void)p; LOGF("SetConfig idx=%#x\n", i); return OMX_ErrorNone; }
static OMX_ERRORTYPE c_GetExtensionIndex(OMX_HANDLETYPE h, OMX_STRING n, OMX_INDEXTYPE *i) { (void)h; LOGF("GetExtensionIndex %s\n", n ? n : "?"); if (i) *i = 0x7F000001; return OMX_ErrorNone; }
static OMX_ERRORTYPE c_GetState(OMX_HANDLETYPE h, OMX_STATETYPE *s) { if (s) *s = E(h)->state; return OMX_ErrorNone; }
static OMX_ERRORTYPE c_TunnelRequest(OMX_HANDLETYPE h, OMX_U32 a, OMX_HANDLETYPE b, OMX_U32 c, OMX_PTR d) { (void)h;(void)a;(void)b;(void)c;(void)d; return OMX_ErrorNone; }

static OMX_ERRORTYPE c_UseBuffer(OMX_HANDLETYPE h, OMX_BUFFERHEADERTYPE **pp, OMX_U32 port, OMX_PTR app, OMX_U32 sz, OMX_U8 *buf)
{ struct enc *e = E(h); *pp = mk_bufhdr(port, app, sz, buf); LOGF("UseBuffer port=%u sz=%u -> %p\n", port, sz, (void *)*pp);
  pthread_mutex_lock(&e->mx); if (port == IN_PORT) e->in_alloc++; else e->out_alloc++; pthread_cond_signal(&e->cv); pthread_mutex_unlock(&e->mx); return OMX_ErrorNone; }
static OMX_ERRORTYPE c_AllocateBuffer(OMX_HANDLETYPE h, OMX_BUFFERHEADERTYPE **pp, OMX_U32 port, OMX_PTR app, OMX_U32 sz)
{ struct enc *e = E(h); *pp = mk_bufhdr(port, app, sz, calloc(1, sz ? sz : 1)); LOGF("AllocateBuffer port=%u sz=%u -> %p\n", port, sz, (void *)*pp);
  pthread_mutex_lock(&e->mx); if (port == IN_PORT) e->in_alloc++; else e->out_alloc++; pthread_cond_signal(&e->cv); pthread_mutex_unlock(&e->mx); return OMX_ErrorNone; }
static OMX_ERRORTYPE c_FreeBuffer(OMX_HANDLETYPE h, OMX_U32 port, OMX_BUFFERHEADERTYPE *b) { (void)h; (void)port; if (b) free(b); return OMX_ErrorNone; }

static OMX_ERRORTYPE c_EmptyThisBuffer(OMX_HANDLETYPE h, OMX_BUFFERHEADERTYPE *b)
{ enq(E(h), J_ETB, 0, b); return OMX_ErrorNone; }
static OMX_ERRORTYPE c_FillThisBuffer(OMX_HANDLETYPE h, OMX_BUFFERHEADERTYPE *b)
{ LOGF("FillThisBuffer alloc=%u\n", b ? b->nAllocLen : 0); enq(E(h), J_FTB, 0, b); return OMX_ErrorNone; }
static OMX_ERRORTYPE c_SetCallbacks(OMX_HANDLETYPE h, OMX_CALLBACKTYPE *cb, OMX_PTR app)
{ struct enc *e = E(h); if (cb) e->cb = *cb; e->appdata = app; LOGF("SetCallbacks\n"); return OMX_ErrorNone; }
static OMX_ERRORTYPE c_ComponentDeInit(OMX_HANDLETYPE h)
{ struct enc *e = E(h); LOGF("ComponentDeInit\n"); pthread_mutex_lock(&e->mx); e->running = 0; pthread_cond_signal(&e->cv); pthread_mutex_unlock(&e->mx); pthread_join(e->th, NULL); return OMX_ErrorNone; }
static OMX_ERRORTYPE c_UseEGLImage(OMX_HANDLETYPE h, OMX_BUFFERHEADERTYPE **pp, OMX_U32 port, OMX_PTR app, void *egl)
{ (void)h;(void)pp;(void)port;(void)app;(void)egl; LOGF("UseEGLImage\n"); return OMX_ErrorNone; }
static OMX_ERRORTYPE c_RoleEnum(OMX_HANDLETYPE h, OMX_U8 *role, OMX_U32 idx)
{ (void)h; if (idx == 0 && role) { strcpy((char *)role, "video_encoder.avc"); return OMX_ErrorNone; } return OMX_ErrorNoMore; }

OMX_ERRORTYPE OMX_Init(void) { LOGF("OMX_Init\n"); return OMX_ErrorNone; }
OMX_ERRORTYPE OMX_Deinit(void) { LOGF("OMX_Deinit\n"); return OMX_ErrorNone; }

OMX_ERRORTYPE OMX_GetHandle(OMX_HANDLETYPE *pHandle, OMX_STRING name, OMX_PTR appData, OMX_CALLBACKTYPE *cb)
{
    struct enc *e = calloc(1, sizeof *e);
    OMX_COMPONENTTYPE *c = &e->comp;
    LOGF("OMX_GetHandle(%s)\n", name ? name : "?");
    e->state = OMX_StateLoaded; e->w = ENC_W; e->h = ENC_H; e->bitrate = 4000000;
    e->in_cnt = 2; e->out_cnt = 2; e->in_sz = ENC_W * ENC_H * 3 / 2; e->out_sz = ENC_W * ENC_H;
    e->enc_fd = -1;   /* lazy-connect ao daemon encoder no 1o frame */
    if (cb) e->cb = *cb;
    e->appdata = appData;
    pthread_mutex_init(&e->mx, NULL); pthread_cond_init(&e->cv, NULL);
    e->running = 1; pthread_create(&e->th, NULL, worker, e);
    c->nSize = sizeof *c; c->nVersion.s.nVersionMajor = 1; c->nVersion.s.nVersionMinor = 1; c->nVersion.s.nRevision = 2;
    c->pComponentPrivate = e;
    c->GetComponentVersion = c_GetComponentVersion; c->SendCommand = c_SendCommand;
    c->GetParameter = c_GetParameter; c->SetParameter = c_SetParameter;
    c->GetConfig = c_GetConfig; c->SetConfig = c_SetConfig;
    c->GetExtensionIndex = c_GetExtensionIndex; c->GetState = c_GetState;
    c->ComponentTunnelRequest = c_TunnelRequest;
    c->UseBuffer = c_UseBuffer; c->AllocateBuffer = c_AllocateBuffer; c->FreeBuffer = c_FreeBuffer;
    c->EmptyThisBuffer = c_EmptyThisBuffer; c->FillThisBuffer = c_FillThisBuffer;
    c->SetCallbacks = c_SetCallbacks; c->ComponentDeInit = c_ComponentDeInit;
    c->UseEGLImage = c_UseEGLImage; c->ComponentRoleEnum = c_RoleEnum;
    *pHandle = (OMX_HANDLETYPE)c;
    return OMX_ErrorNone;
}
OMX_ERRORTYPE OMX_FreeHandle(OMX_HANDLETYPE h) { LOGF("OMX_FreeHandle\n"); if (h) { struct enc *e = E(h); if (e->running) { pthread_mutex_lock(&e->mx); e->running = 0; pthread_cond_signal(&e->cv); pthread_mutex_unlock(&e->mx); pthread_join(e->th, NULL); } free(e); } return OMX_ErrorNone; }
OMX_ERRORTYPE OMX_GetComponentsOfRole(OMX_STRING role, OMX_U32 *num, OMX_U8 **names)
{ (void)role; LOGF("OMX_GetComponentsOfRole %s\n", role ? role : "?"); if (names && num && *num > 0 && names[0]) strcpy((char *)names[0], "OMX.qcom.video.encoder.avc"); if (num) *num = 1; return OMX_ErrorNone; }
OMX_ERRORTYPE OMX_GetRolesOfComponent(OMX_STRING comp, OMX_U32 *num, OMX_U8 **roles)
{ (void)comp; if (roles && num && *num > 0 && roles[0]) strcpy((char *)roles[0], "video_encoder.avc"); if (num) *num = 1; return OMX_ErrorNone; }
OMX_ERRORTYPE OMX_ComponentNameEnum(OMX_STRING name, OMX_U32 len, OMX_U32 idx)
{ (void)len; if (idx == 0 && name) { strcpy(name, "OMX.qcom.video.encoder.avc"); return OMX_ErrorNone; } return OMX_ErrorNoMore; }
OMX_ERRORTYPE OMX_SetupTunnel(OMX_HANDLETYPE a, OMX_U32 b, OMX_HANDLETYPE c, OMX_U32 d) { (void)a;(void)b;(void)c;(void)d; return OMX_ErrorNone; }
