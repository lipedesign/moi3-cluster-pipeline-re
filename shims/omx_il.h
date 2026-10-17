/* omx_il.h — the minimal OpenMAX IL 1.1.2 ABI our fake libOmxCore needs to stand in for the
 * Qualcomm encoder core. Only what the mss actually touches, declared to match the standard
 * OpenMAX IL 1.1.2 layout (so the component struct and buffer header are ABI-compatible with the
 * real binary). Hand-written to keep this repo self-contained; validated against the mss's
 * observed behaviour. */
#ifndef OMX_IL_MIN_H
#define OMX_IL_MIN_H
#include <stdint.h>

typedef unsigned char  OMX_U8;
typedef unsigned short OMX_U16;
typedef unsigned int   OMX_U32;
typedef signed int     OMX_S32;
typedef int64_t        OMX_TICKS;
typedef void          *OMX_PTR;
typedef char          *OMX_STRING;
typedef unsigned char *OMX_BYTE;
typedef void          *OMX_HANDLETYPE;
typedef enum { OMX_FALSE = 0, OMX_TRUE = 1 } OMX_BOOL;
#define OMX_IN
#define OMX_OUT
#define OMX_INOUT

typedef union OMX_VERSIONTYPE {
    struct { OMX_U8 nVersionMajor, nVersionMinor, nRevision, nStep; } s;
    OMX_U32 nVersion;
} OMX_VERSIONTYPE;

typedef enum OMX_ERRORTYPE {
    OMX_ErrorNone = 0,
    OMX_ErrorInsufficientResources = 0x80001000,
    OMX_ErrorUndefined = 0x80001001,
    OMX_ErrorUnsupportedIndex = 0x8000101A,
    OMX_ErrorNoMore = 0x8000100E,
    OMX_ErrorInvalidState = 0x80001019
} OMX_ERRORTYPE;

typedef enum OMX_STATETYPE {
    OMX_StateInvalid, OMX_StateLoaded, OMX_StateIdle, OMX_StateExecuting,
    OMX_StatePause, OMX_StateWaitForResources
} OMX_STATETYPE;

typedef enum OMX_COMMANDTYPE {
    OMX_CommandStateSet, OMX_CommandFlush, OMX_CommandPortDisable,
    OMX_CommandPortEnable, OMX_CommandMarkBuffer
} OMX_COMMANDTYPE;

typedef enum OMX_EVENTTYPE {
    OMX_EventCmdComplete, OMX_EventError, OMX_EventMark, OMX_EventPortSettingsChanged,
    OMX_EventBufferFlag, OMX_EventResourcesAcquired, OMX_EventComponentResumed,
    OMX_EventDynamicResourcesAvailable, OMX_EventPortFormatDetected
} OMX_EVENTTYPE;

typedef int OMX_INDEXTYPE;   /* standard OMX_IndexParam* values compared numerically where needed */
#define OMX_IndexParamPortDefinition      0x02000001
#define OMX_IndexParamVideoInit           0x02000006
#define OMX_IndexParamVideoPortFormat     0x02000020
#define OMX_IndexParamVideoBitrate        0x02000021
#define OMX_IndexParamVideoAvc            0x02000029

typedef enum OMX_DIRTYPE { OMX_DirInput, OMX_DirOutput, OMX_DirMax = 0x7FFFFFFF } OMX_DIRTYPE;

typedef enum OMX_COLOR_FORMATTYPE {
    OMX_COLOR_FormatUnused = 0,
    OMX_COLOR_FormatYUV420Planar = 0x13,
    OMX_COLOR_FormatYUV420SemiPlanar = 0x15
} OMX_COLOR_FORMATTYPE;

typedef enum OMX_VIDEO_CODINGTYPE {
    OMX_VIDEO_CodingUnused, OMX_VIDEO_CodingAutoDetect, OMX_VIDEO_CodingMPEG2,
    OMX_VIDEO_CodingH263, OMX_VIDEO_CodingMPEG4, OMX_VIDEO_CodingWMV,
    OMX_VIDEO_CodingRV, OMX_VIDEO_CodingAVC
} OMX_VIDEO_CODINGTYPE;

typedef struct OMX_VIDEO_PORTDEFINITIONTYPE {
    OMX_STRING cMIMEType;
    OMX_PTR pNativeRender;
    OMX_U32 nFrameWidth;
    OMX_U32 nFrameHeight;
    OMX_S32 nStride;
    OMX_U32 nSliceHeight;
    OMX_U32 nBitrate;
    OMX_U32 xFramerate;
    OMX_BOOL bFlagErrorConcealment;
    OMX_VIDEO_CODINGTYPE eCompressionFormat;
    OMX_COLOR_FORMATTYPE eColorFormat;
    OMX_PTR pNativeWindow;
} OMX_VIDEO_PORTDEFINITIONTYPE;

typedef struct OMX_AUDIO_PORTDEFINITIONTYPE { OMX_STRING cMIMEType; OMX_PTR pNativeRender; OMX_BOOL bFlagErrorConcealment; OMX_U32 eEncoding; } OMX_AUDIO_PORTDEFINITIONTYPE;
typedef struct OMX_IMAGE_PORTDEFINITIONTYPE { OMX_STRING cMIMEType; OMX_PTR pNativeRender; OMX_U32 nFrameWidth, nFrameHeight; OMX_S32 nStride; OMX_U32 nSliceHeight; OMX_BOOL bFlagErrorConcealment; OMX_U32 eCompressionFormat, eColorFormat; OMX_PTR pNativeWindow; } OMX_IMAGE_PORTDEFINITIONTYPE;
typedef struct OMX_OTHER_PORTDEFINITIONTYPE { OMX_U32 eFormat; } OMX_OTHER_PORTDEFINITIONTYPE;

typedef union OMX_PORTDEFINITION_FORMATTYPE {
    OMX_AUDIO_PORTDEFINITIONTYPE audio;
    OMX_VIDEO_PORTDEFINITIONTYPE video;
    OMX_IMAGE_PORTDEFINITIONTYPE image;
    OMX_OTHER_PORTDEFINITIONTYPE other;
} OMX_PORTDEFINITION_FORMATTYPE;

typedef struct OMX_PARAM_PORTDEFINITIONTYPE {
    OMX_U32 nSize;
    OMX_VERSIONTYPE nVersion;
    OMX_U32 nPortIndex;
    OMX_DIRTYPE eDir;
    OMX_U32 nBufferCountActual;
    OMX_U32 nBufferCountMin;
    OMX_U32 nBufferSize;
    OMX_BOOL bEnabled;
    OMX_BOOL bPopulated;
    OMX_U32 eDomain;   /* OMX_PORTDOMAINTYPE */
    OMX_PORTDEFINITION_FORMATTYPE format;
    OMX_BOOL bBuffersContiguous;
    OMX_U32 nBufferAlignment;
} OMX_PARAM_PORTDEFINITIONTYPE;

typedef struct OMX_BUFFERHEADERTYPE {
    OMX_U32 nSize;
    OMX_VERSIONTYPE nVersion;
    OMX_U8 *pBuffer;
    OMX_U32 nAllocLen;
    OMX_U32 nFilledLen;
    OMX_U32 nOffset;
    OMX_PTR pAppPrivate;
    OMX_PTR pPlatformPrivate;
    OMX_PTR pInputPortPrivate;
    OMX_PTR pOutputPortPrivate;
    OMX_HANDLETYPE hMarkTargetComponent;
    OMX_PTR pMarkData;
    OMX_U32 nTickCount;
    OMX_TICKS nTimeStamp;
    OMX_U32 nFlags;
    OMX_U32 nOutputPortIndex;
    OMX_U32 nInputPortIndex;
} OMX_BUFFERHEADERTYPE;

#define OMX_BUFFERFLAG_EOS      0x00000001
#define OMX_BUFFERFLAG_SYNCFRAME 0x00000020
#define OMX_BUFFERFLAG_CODECCONFIG 0x00000080

typedef struct OMX_CALLBACKTYPE {
    OMX_ERRORTYPE (*EventHandler)(OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2, OMX_PTR pEventData);
    OMX_ERRORTYPE (*EmptyBufferDone)(OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_BUFFERHEADERTYPE *pBuffer);
    OMX_ERRORTYPE (*FillBufferDone)(OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_BUFFERHEADERTYPE *pBuffer);
} OMX_CALLBACKTYPE;

typedef struct OMX_COMPONENTTYPE {
    OMX_U32 nSize;
    OMX_VERSIONTYPE nVersion;
    OMX_PTR pComponentPrivate;
    OMX_PTR pApplicationPrivate;
    OMX_ERRORTYPE (*GetComponentVersion)(OMX_HANDLETYPE, OMX_STRING, OMX_VERSIONTYPE *, OMX_VERSIONTYPE *, OMX_U8 *);
    OMX_ERRORTYPE (*SendCommand)(OMX_HANDLETYPE, OMX_COMMANDTYPE, OMX_U32, OMX_PTR);
    OMX_ERRORTYPE (*GetParameter)(OMX_HANDLETYPE, OMX_INDEXTYPE, OMX_PTR);
    OMX_ERRORTYPE (*SetParameter)(OMX_HANDLETYPE, OMX_INDEXTYPE, OMX_PTR);
    OMX_ERRORTYPE (*GetConfig)(OMX_HANDLETYPE, OMX_INDEXTYPE, OMX_PTR);
    OMX_ERRORTYPE (*SetConfig)(OMX_HANDLETYPE, OMX_INDEXTYPE, OMX_PTR);
    OMX_ERRORTYPE (*GetExtensionIndex)(OMX_HANDLETYPE, OMX_STRING, OMX_INDEXTYPE *);
    OMX_ERRORTYPE (*GetState)(OMX_HANDLETYPE, OMX_STATETYPE *);
    OMX_ERRORTYPE (*ComponentTunnelRequest)(OMX_HANDLETYPE, OMX_U32, OMX_HANDLETYPE, OMX_U32, OMX_PTR);
    OMX_ERRORTYPE (*UseBuffer)(OMX_HANDLETYPE, OMX_BUFFERHEADERTYPE **, OMX_U32, OMX_PTR, OMX_U32, OMX_U8 *);
    OMX_ERRORTYPE (*AllocateBuffer)(OMX_HANDLETYPE, OMX_BUFFERHEADERTYPE **, OMX_U32, OMX_PTR, OMX_U32);
    OMX_ERRORTYPE (*FreeBuffer)(OMX_HANDLETYPE, OMX_U32, OMX_BUFFERHEADERTYPE *);
    OMX_ERRORTYPE (*EmptyThisBuffer)(OMX_HANDLETYPE, OMX_BUFFERHEADERTYPE *);
    OMX_ERRORTYPE (*FillThisBuffer)(OMX_HANDLETYPE, OMX_BUFFERHEADERTYPE *);
    OMX_ERRORTYPE (*SetCallbacks)(OMX_HANDLETYPE, OMX_CALLBACKTYPE *, OMX_PTR);
    OMX_ERRORTYPE (*ComponentDeInit)(OMX_HANDLETYPE);
    OMX_ERRORTYPE (*UseEGLImage)(OMX_HANDLETYPE, OMX_BUFFERHEADERTYPE **, OMX_U32, OMX_PTR, void *);
    OMX_ERRORTYPE (*ComponentRoleEnum)(OMX_HANDLETYPE, OMX_U8 *, OMX_U32);
} OMX_COMPONENTTYPE;

#endif
