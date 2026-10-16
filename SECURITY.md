# Security Policy

This is research / experimental code — LD_PRELOAD shims and bench harnesses, not a production
service. Even so, if you find a memory-safety or other security issue in the code here, a report
is welcome.

## Reporting

Open a GitHub issue, or use GitHub's private **"Report a vulnerability"** on this repository.

## Scope

- **In scope** — bugs in the code in this repo: the shims (`shims/`), the GL-forwarding host
  (`glforward-ext/`), the compositor stub (`compositor/`) and the harnesses (`spike/`). Memory
  safety in the IPC / receiver paths is the most relevant class.
- **Out of scope** — the deliberately omitted parts (firmware, protection bypass, on-unit code
  execution). This repo neither contains nor accepts them.

The IPC paths assume a cooperating local peer (the shimmed process, in a throwaway container with
no network), but geometry and lengths from that peer are validated defensively regardless, so a
malformed frame cannot read out of bounds.
