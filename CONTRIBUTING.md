# Contributing

Thanks for your interest. This is an active **reverse-engineering experiment**, so the most
valuable contributions are **confirmations, corrections and reproductions**.

## Ground rules

- **No proprietary material.** Never commit firmware binaries, extracted shaders, sysroots, or
  any file from the VW/LG firmware. Document RE *findings* (addresses, behaviour, standard
  formats) — don't paste the vendor's code. Everything here is **bring-your-own-firmware**.
- **No protection bypass.** This repo reproduces the *display* pipeline. It does not accept, and
  does not need, anything that defeats Secure Boot, dm-verity, VW signing or SMACK, or that
  obtains code execution on a unit.
- **No vehicle-identifying data.** No VINs, serial numbers or personal data — in code, docs, logs
  or sample frames.
- **Read-only on cars.** Nothing here is for writing to a vehicle.

## How to help

- **Reproduce** the bench on your own extracted firmware and report what matches or differs —
  model, market and firmware train / MU (**never a VIN**).
- **Improve** the shims, the GL-forwarding host, the harnesses, the docs or the tooling.
- **Fix bugs** — with a short note on how you verified the fix.

Open an issue or a pull request, and keep the scope above. New here? The open threads live in the
companion research repo's [open-questions](https://github.com/lipedesign/mib3-moi3-lg-research/blob/main/docs/open-questions.md).

## Acknowledgements

Reverse engineering, bench harness and docs by **lipedesign**, built with **Claude (Claude Code,
Anthropic)** as a pair-programming and RE assistant.
