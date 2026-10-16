# Build/run environment for the moi3-cluster-pipeline-re bench.
#
# Contains ONLY the toolchain: a C compiler, Mesa software GL (llvmpipe) and
# tracing/loop-mount tools. NO firmware is included — you mount your own
# extracted sysroot at /fw for the full bench (see README.md, "Getting started").
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc libc6-dev \
        libegl-dev libgles-dev libgbm-dev libgl1-mesa-dri \
        strace util-linux ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# The bench has no GPU — force Mesa software rendering (llvmpipe), surfaceless.
ENV LIBGL_ALWAYS_SOFTWARE=1 \
    EGL_PLATFORM=surfaceless

WORKDIR /poc

# Quick self-contained check (no firmware needed) — the RGBA->YCbCr (BT.601) demo:
#   docker build -t moi3-bench .
#   docker run --rm -v "$PWD":/poc moi3-bench sh -c '\
#     gcc -O2 -o /tmp/demo spike/moi3_shader_demo.c -lEGL -lGLESv2 && \
#     /tmp/demo your_frame.ppm your_encoder.glsl /tmp/y.bin /tmp/nv12.bin'
#
# The full multi-screen-server bench (spike/run.sh) additionally needs --privileged,
# your firmware sysroot at /fw and the glforward base at /glf (both bring-your-own).
CMD ["/bin/sh"]
