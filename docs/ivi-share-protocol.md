# The `ivi_share` Wayland protocol (MOI3 LG)

Recovered from the stripped `multi-screen-server` binary with
[`tools/extract_wl_protocol.py`](../tools/extract_wl_protocol.py), which reads the embedded
`wl_interface` / `wl_message` tables out of `.data.rel.ro`. These are the LG build's exact
names, opcodes (array order = opcode) and argument signatures — the contract you must implement
on the compositor side to feed the real `multi-screen-server`.

> Signatures use Wayland's notation: `u` uint, `i` int, `f` fixed, `o` object, `n` new_id,
> `h` **file descriptor**, `s` string.

## `ivi_share` (version 1)

| op | request | sig | meaning |
|----|---------|-----|---------|
| 0 | `get_ivi_share_surface` | `nu` | create a share handle (`new_id`) onto the IVI surface whose numeric id is `surface_id` (`uint`) |

The consumer binds `ivi_share` from the registry and asks for a handle onto an **existing** IVI
surface by id — i.e. a surface some other application already created via `ivi_application`.

## `ivi_share_surface` (version 1)

Requests (consumer → compositor):

| op | request | sig | meaning |
|----|---------|-----|---------|
| 0 | `destroy` | `` | |
| 1 | `redirect_touch_down` | `uiff` | forward a touch-down back to the shared surface (id, x, y) |
| 2 | `redirect_touch_up` | `ui` | |
| 3 | `redirect_touch_motion` | `iff` | |
| 4 | `redirect_touch_frame` | `` | |
| 5 | `redirect_touch_cancel` | `` | |

Events (compositor → consumer):

| op | event | sig | meaning |
|----|-------|-----|---------|
| 0 | `damage` | `hh` | a new frame is available — delivered as **two file descriptors**: the frame's **dma-buf** and (most likely) a sync/fence fd |
| 1 | `configure` | `uuuuu` | frame geometry/format: `width, height, stride, format, (+1)` — matches the binary's log line `configure [%dx%d] stride %d, fmt %d` |
| 2 | `input_capabilities` | `u` | touch/input capability bitmask of the shared surface |
| 3 | `share_surface_state` | `u` | lifecycle state of the share |

The consumer's own handlers in the binary are named `handle_share_surface_configure`,
`handle_share_surface_input_caps`, `cb_surface_state`, and it logs
`ivi_share_get_ivi_share_surface = %p` / `startShare [id = %d]`.

## Supporting interfaces (standard GENIVI `ivi-application`)

| interface | op | message | sig |
|-----------|----|---------|-----|
| `ivi_application` | req 0 | `surface_create` | `uon` (ivi_id, wl_surface, new ivi_surface) |
| `ivi_surface` | ev 0 | `configure` | `ii` (width, height) |

## What this means for reproduction

1. **The pixels cross as a dma-buf fd** (`damage: hh`), not `wl_shm`. Reproducing the pipeline
   therefore needs a dma-buf producer and a Mesa that can import it — see the spike notes. This
   is the project's load-bearing unknown.
2. The consumer only needs the **surface id** to attach; the compositor owns the mapping from
   id to the producer's buffer.
3. The `configure`/`state`/`input_capabilities` events are enough to drive the consumer up to
   the point where it waits for `damage` — so a compositor can be brought up incrementally.

_Source: `multi-screen-server` (MU0883 train), read statically. Nothing executed on a vehicle._
