# APRICOT

APRICOT is a native PS5 development project for two related handheld RPG editions.

The public repository contains original source and documentation only. It does not contain a ROM/ISO, game assets, BIOS or firmware, a game-derived module, save data, or a playable release. Users must provide and process their own legally obtained game data locally.

## Current status

APRICOT is in private pre-release development. A statically translated native startup path and early save/load flow have been verified privately. A previous v0.1.2 preview also reached the native intro/title path on the physical PS5.

The complete game is not released. Battles, the full campaign, both-edition selection, complete HeartGold coverage, and a compatible link/trading service are still development work. See [docs/APRICOT-STATUS.md](docs/APRICOT-STATUS.md) for the dated status record.

## Intended architecture

- A PC preparation tool reads a user's own NDS image, verifies its game identity, and produces a matched native module and data manifest.
- A PS5 player loads that matched private output and provides native display, audio, input, file, save, and graphics services.
- The project targets statically translated native execution. It does not use a CPU interpreter, JIT, or emulator core as a fallback.
- The two editions remain separate inputs and save identities even though they share an engine family.

The planned launcher can select either prepared edition when both are installed and auto-select the only prepared edition when only one is present. This behavior is planned, not yet part of a public build.

## Clean-room and licensing scope

APRICOT publishes original implementation and documentation plus correctly attributed open-source dependencies where their licenses permit use. License notices are retained. No Nintendo or game-specific proprietary source, graphics, music, text, ROM data, or derived executable is published.

This repository is a development record and does not provide a legal determination for a particular jurisdiction. Users are responsible for their own game data and for complying with applicable law.

## Versioning

- `v0.0.1`: pre-release / alpha start
- `v0.0.2`: bug-fix release
- `v0.1.1`: feature release
- `v1.0.0`: full release

There is currently no public APRICOT release artifact. The Prospero catalog entry is a Coming-soon reservation only.

## Repository policy

Do not commit ROMs, extracted game data, generated game modules, private save files, screenshots containing game content, BIOS/firmware, or build outputs. Private test inputs stay outside this repository.
