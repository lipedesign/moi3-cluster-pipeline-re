# ShareServer — máquina de estados do pipeline de vídeo (RE)

Resultado da engenharia reversa do `multi-screen-server` (mss) da MOI3/LG, feita com Ghidra 12.3
(decompilador) sobre o binário `/lge/app_ro/graphics/multi-display/multi-screen-server` extraído do
`system.img`. Base de imagem no Ghidra = `0x100000` (um VMA de arquivo `0x1b48c` vira `0x11b48c`).

Este documento descreve **só o caminho de exibição** (como o surface renderizado vira stream de vídeo):
o que o mss espera receber do compositor Wayland para montar o encoder e começar a transmitir. Nada
aqui depende de derrotar Secure Boot, dm-verity, assinatura VW ou SMACK, e nada aqui é método de
execução de código na unidade (fora de escopo). É o contrato do protocolo `ivi_share` do lado do
consumidor, que a nossa bancada precisa satisfazer para o binário real transmitir de ponta a ponta.

## Função de entrada

`ShareServer::startShareInternal(std::string dest, int, int surface_id, int fps)` — `FUN_001175d0 @ 0x1175d0`.
Chamada a partir de `START STREAMING` (`FUN_0011b350 @ 0x11b350`). Monta todo o estado e **arma as
callbacks**; não roda encode diretamente. Sequência:

1. Aloca o *user_data* do listener (`piVar31`, uma struct pequena) e **inicializa o estado
   `piVar31[0] = 2`** (decomp. linha 808). Também: `piVar31[2]` = ponteiro para o `ContextHolder`
   (`plVar8`), `piVar31[4]` = Mutex, `piVar31[6]` = ShareServer, `piVar31[8]` = id do surface.
2. Preenche o `ContextHolder` (`plVar8`) com a config vinda do pedido de share: largura/altura,
   contagem de buffers (`[0x65]`), estado (`[0x79]`, começa 2), **bitrate `[0x3dc] = 4000000`
   (4 Mbps H.264)** (linha 1157), ponteiros de EGL display/context, `gbm_device [0x3d0]`.
3. Pega os ponteiros `eglCreateImageKHR [0x17]` e `eglDestroyImageKHR [0x18]` via `eglGetProcAddress`.
4. Emite o request **`ivi_share.get_ivi_share_surface(new_id, surface_id)`** (opcode 0,
   `wl_proxy_marshal_constructor` com `ivi_share_surface_interface`, linha 1188).
5. Cria o **`TransportStream(dest, surface_id, 0xa7ed)`** (linha 1342) — o transporte UDP. `0xa7ed`
   (42989) é a porta base embutida; o destino (`ff14::2:4`) e a porta efetiva (42800) vêm por
   argumento/config.
6. Registra o listener: **`wl_proxy_add_listener(share_surface, &surface_listener_callbacks, piVar31)`**
   (linha 1370). A struct `surface_listener_callbacks` está em `0x157b68`.

## As callbacks do `ivi_share_surface`

Struct de 4 ponteiros em `0x157b68` (ordem = ordem dos eventos na interface). Primeiro argumento de
cada callback é o `data` (= `piVar31`); `*data` é o estado do share.

| # | evento | função | o que faz |
|---|---|---|---|
| 0 | `damage(fd, u)` | `cb_damage` `FUN_00115860 @ 0x115860` | importa o frame |
| 1 | `configure(w,h,stride,fmt,x)` | `FUN_00112fb0 @ 0x112fb0` | monta o encoder |
| 2 | `input_capabilities(u)` | `FUN_00111da0 @ 0x111da0` | **no-op** (só loga) |
| 3 | `share_surface_state(u)` | `FUN_001188e0 @ 0x1188e0` | transição de estado |

### configure (monta o encoder) — `FUN_00112fb0`

Guardas (se falhar, só loga e volta, sem montar nada):
- **largura e altura ≠ 0** (linha 668);
- **`*data ∈ {2, 3}`** (linha 677: a condição de pular é `1 < *data - 2U`). Como o estado nasce 2
  (passo 1 acima), o primeiro `configure` válido já passa.

Passando as guardas:
- grava `w` em `ctx[0xc]`, `h` em `ctx[0x10]`; arredonda para par (`w & ~1`, `h & ~1`);
- cria o shader **`Rgba2yuv(eglDisplay[0x60], eglContext[0x68], w, h, 0, flag)`** (RGBA→YUV);
- chama **`omx_encoder_create(w, h, bitrate=ctx[0x3dc], ctx[0x3d8], fps, FUN_001135c0, ctx, Rgba2yuv, gbm_device=ctx[0x3d0])`**
  (o 4º arg de assinatura varia com o modo `ctx[0x3c8]` ∈ {0,1,2,3}; `FUN_001135c0` é a callback de
  frame pronta do encoder);
- seta `ctx[0x3e0] = 1` e `ctx[0x240] = 1` (flags de "ativo") e **inicia as duas threads**:
  `AsyncTask::execute(ctx+0x230)` e `AsyncTask::execute(ctx+0x400)`;
- por fim **`*data = 1`** (linha 815) — que é exatamente o gate que o `cb_damage` exige.

### damage (importa cada frame) — `cb_damage` `FUN_00115860`

Guardas: `fd` válido (≥ 0); `ContextHolder ≠ NULL`; **`*data == 1`** (linha 137 — só processa depois
que o `configure` rodou). Depois:
- monta uma struct de import `{ width=ctx[0xc], height=ctx[0x10], drm_fourcc, fd, ... }`. O fourcc vem
  do modo `ctx[0x1c]`: `1`→`AR24` (ARGB8888), `2`→`AB24` (ABGR8888), senão `XR24` (XRGB8888);
- **`gbm_bo_import(gbm_device, 0x4fff, &import_data, 4)`** (linha 163) — embrulha o dma-buf que chegou
  no evento. O **buffer de entrada é o próprio dma-buf do `damage`** — não há `gbm_bo_create` de
  entrada nas callbacks;
- `gbm_bo_get_stride(bo)` (linha 269) e segue para EGLImage → shader `Rgba2yuv` → OMX → TransportStream.

### share_surface_state (transição) — `FUN_001188e0`, valor em `param_4`

- `param_4 == 1`: **se `*data == 2`, faz `*data = 3`** (linhas 1392-1393) e cria um worker
  `AsyncTask` (vtable `0x158288`, `setAutoRemove(true)`, `execute`).
- `param_4 == 2`: registro no mapa do servidor (`Rb_tree<int, ContextHolder*>`); se já havia uma
  entrada, **derruba a anterior** (para as threads, destrói, estado 3 na teardown).

## As duas threads (AsyncTask)

Ambas são de **temporização/controle de taxa**, não o grab de frame:
- `ctx+0x230` (vtable `0x158228`), run = `FUN_00112640`: laço `usleep` + ajuste do intervalo de
  quadro (`1000/fps`), sinaliza condição. Controle de FPS adaptativo.
- `ctx+0x400` (vtable `0x158258`), run = `FUN_00113910`: `Condition::wait` + estatística de tempos
  entre quadros. O laço roda enquanto `ctx[0x3e0] != 0` (o flag que o `configure` liga).

O trabalho por frame (import, shader, encode) mora na callback `damage`, disparada pelo compositor.

## Sequência mínima que a bancada precisa emitir

Do lado do compositor (nós), depois que o mss fizer `get_ivi_share_surface`:

1. (opcional) `share_surface_state`: desnecessário para o caminho básico — o estado já nasce 2.
2. **`configure(w, h, stride, fmt, x)`** com `w,h ≠ 0` (1280×480, XRGB8888). → mss monta
   `Rgba2yuv` + `omx_encoder_create`, inicia as threads, e passa o estado para 1.
3. **`damage(fd, u)`** repetido (~30 fps), cada um com um fd de dma-buf do frame. → mss importa,
   converte e encoda, e o `TransportStream` manda RTP/UDP para `ff14::2:4`.

Cuidado de ordem: **não** mandar `share_surface_state(1)` **depois** do `configure`. Após o
`configure`, `*data == 1`; aí `cb_share_surface_state(1)` cai no `else` de `*data==2` (não a
transição esperada). Se for mandar o state, mandar antes; o mais simples é não mandar.

## Correções empíricas ao protocolo (confirmadas rodando o binário)

Rodando o mss real contra o compositor da bancada, dois detalhes do protocolo ficaram claros, além
da RE estática:

1. **Argumentos do `configure`.** Dos 5 uints do evento, o mss usa o **2º como width** (`ctx[0xc]`),
   o **3º como height** (`ctx[0x10]`) e o **5º como seletor de fourcc** lido depois pelo `cb_damage`
   (`1`→AR24, `2`→XR24, qualquer outro→AB24). O 1º e o 4º uint são ignorados. Para o stream 1280×480
   XRGB8888, o compositor manda `configure = [_, 1280, 480, _, 2]`.
2. **Nó de render DRM obrigatório.** Antes de importar frames, o mss abre `/dev/dri/renderD128` e roda
   o handshake de autenticação DRM (`DRM_IOCTL_GET_MAGIC` + `AUTH_MAGIC`). Sem um fd válido aí, o
   `cb_damage` rejeita todo frame (gate `*(int*)(param_2+0x58) < 0`, linha 142). Na bancada isso é
   coberto por um shim `LD_PRELOAD` (`../shims/fakedrm.c`) que intercepta o `openat` do `/dev/dri`
   devolvendo um fd e finge o auth — não é preciso GPU nem DRM real.

## Resultado na bancada (spikes desta sessão, 15/10/2026)

Com o `fakegbm` (import type `0x4fff`), o `fakeomxcore`, o `fakedrm` e o compositor emitindo o
`configure` com o mapeamento acima, o binário real percorre o pipeline de exibição até o shader:

1. cria o dispositivo GBM (`fakegbm`, fd do `fakedrm`) ✓
2. recebe `get_ivi_share_surface` e o `configure 1280×480` ✓
3. monta o encoder real `OMX.qcom.video.encoder.avc` e o leva ao estado **Executing** ✓
4. no `damage`, importa o dma-buf (`gbm_bo_import` type `0x4fff`, 1280×480 XR24) ✓
5. monta o **EGLImage** do dma-buf (`eglCreateImageKHR` com `EGL_LINUX_DMA_BUF_EXT`) ✓
6. **roda o shader `Rgba2yuv` (RGBA→YUV)** sobre o frame importado — glhost registra o draw completo
   (`glUseProgram`, `glBindTexture`, FBO, `glDrawArrays`, `glFinish`) ✓

Captura (demo): o glhost grava os pixels que o `cb_damage` importou (handler `OPX_IMG_CREATE`) —
`frames/import_000.ppm`, as 8 barras 1280×480 com as cores certas: prova que o binário real leu o
frame pelo caminho dma-buf→EGLImage. É a **entrada** do shader.

Para capturar a **saída** do shader esbarramos numa série de lacunas de completude do `glforward`
(ele foi feito para a HMI, não para todo o pipeline do `Rgba2yuv`). Nenhuma é limitação do binário:
1. **Viewport de saída 0×0** — o `mss` standalone (sem a config de stream do RSI) chama `glViewport(0,0,0,0)`.
   Contornado no glhost (clampa para 1280×480). ✓
2. **Arrays de vértice do lado cliente** — o `convert` desenha o quad com `glVertexAttribPointer` em
   memória de CPU (sem VBO), que o `glforward` descartava. **Implementado** (`glforward-ext`: o shim
   captura os arrays e envia no `glDrawArrays`; o glhost monta um VBO transitório — `OPX_CLIENT_ARRAY`).
   Confirmado: `client array idx=0/1 size=2 GL_FLOAT` chega e vira VBO. ✓
3. **Programa do shader nunca criado no glhost (bloqueio atual).** O diagnóstico pós-`glDrawArrays`
   mostra `prog=0` (nenhum program amarrado) → o desenho não produz nada → saída preta. Nenhuma op de
   criação de programa (`glCreateProgram`/`glCreateShader`/`glCreateShaderProgramv`/binary) chega ao
   glhost, apesar de `glUseProgram` ser chamado. O `Rgba2yuv` monta seu programa por um caminho que o
   forwarder não captura (provável contexto/thread separado, ou antes do `eglMakeCurrent` que o glhost
   serve). Replicar isso é completar o forwarder para rodar todo o pipeline de shader do `mss` — esforço
   aberto, com mais lacunas prováveis.
4. Separado: um **segfault** numa thread auxiliar (controle de taxa, `FUN_00112640`, lê um buffer de
   amostras NULL → `SEGV` em `0x8`) encerra o processo após ~1 frame — race de init do standalone,
   independente do caminho do frame.

Ou seja: o binário real percorre o pipeline e **executa as chamadas de desenho** do shader sobre o
frame real; o que impede a imagem de saída é a fidelidade do forwarder de software (o programa do
shader não é recriado no glhost), não o binário. A rede é `--network none`, então o RTP não precisa
sair para o critério "pixel de ponta a ponta". A prova limpa que temos é o frame de **entrada**
capturado dentro do pipeline do binário (`frames/import_000.ppm`).

## Reproduzindo a conversão do binário na bancada (15/10/2026) — pixel convertido, de verdade

Como o binário não orquestra o shader sozinho em standalone (o `Rgba2yuv::init` só é chamado quando o
encoder OMX aloca os input buffers — handshake que o modo isolado não dispara), reproduzimos a **mesma
conversão** sobre o frame real, no Mesa (llvmpipe) da bancada. Harness: `../spike/moi3_shader_demo.c`.
Enquadramento honesto: a conversão de cor é o padrão **ITU-R BT.601 studio-swing** (um achado de RE — é o
que o binário usa), e o shader do passe A é **implementação nossa** desse padrão. O passe B carrega o
shader codificador **do próprio binário**, que **você fornece** (não vai neste repo). O passo que falta ao
binário em standalone é a orquestração OMX.

- **Conversão de cor (achado de RE):** o `multi-screen-server` converte RGBA→YCbCr com a matriz
  **ITU-R BT.601 studio-swing padrão** — os coeficientes são os valores da norma (luma
  `(0.299,0.587,0.114)×219/255`, croma `×224/255`, offsets `16/128/128` sobre 256), mais um swizzle BGRA.
  São valores **públicos** da BT.601; **não reproduzimos a fonte do binário** — o shader do passe A é nosso.
  Parâmetros de geometria (achado de RE): quad `pos (-1,1)(-1,-1)(1,-1)(1,1)`, texcoords
  `(1,0)(0,0)(0,1)(1,1)`, `TRIANGLE_FAN`; uniforms width=1280, height=480, width_fbo=320, height_fbo=720,
  lstride=1280, lscanl=480.
- **Textura externa no Mesa:** `glTexImage2D` direto em `GL_TEXTURE_EXTERNAL_OES` é recusado nesse
  tamanho; usar `GL_TEXTURE_2D` + `eglCreateImageKHR(EGL_GL_TEXTURE_2D)` + `glEGLImageTargetTexture2DOES`
  (caminho provado no `mesa_import_test.c`).
- **Passe A** (nossa BT.601 padrão, por pixel): converteu o frame e o **round-trip YUV→RGB reproduz as
  8 barras exatas** (ex.: vermelho → YUV `(81,90,240)` → RGB `(255,0,0)`), confirmando a conversão fiel.
- **Passe B** (shader codificador **do binário**, fornecido pelo usuário — não incluído aqui; com o
  empacotamento NV12): compila e roda (só é preciso tirar a linha `precision highp uint;`, que o Adreno
  aceita e o Mesa ES recusa — não afeta a conversão); produz um NV12 1280×480 (buffer 320×720 RGBA,
  921600/921600 bytes não-zero) que, decodificado como NV12 padrão, **também reproduz as barras**. É o
  buffer que entraria no encoder H.264.

Imagens: luma (canal Y), round-trip (passe A) e o NV12 decodificado (passe B). Resultado: a conversão do
pipeline LG roda na bancada e converte o frame real corretamente — o "pixel de ponta a ponta" demonstrado
(passe A com a nossa BT.601 padrão; passe B com o codificador do próprio binário, fornecido pelo usuário).
O que continua em aberto (caminho "A" purista) é fazer o **binário** orquestrar isso sozinho, o que exige
replicar o handshake de input-buffer do OMX.

## Fonte

Decompilação Ghidra 12.3 (headless, scripts em `../tools/ghidra_decomp_*.java`). Logs crus em
`scratchpad/ghidra-decomp/` (`decomp_clean.c`, `vtable.log`, `vtable2.log`, `listener.log`). Todos os
endereços acima são do binário `multi-screen-server` do `system.img` `mu0883`, base `0x100000`.
