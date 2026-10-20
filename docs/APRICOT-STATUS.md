# ApricotGS status

Date: 2026-10-19

## Public release state

The public project repository is [BaZZtian/PS5-ApricotGS](https://github.com/BaZZtian/PS5-ApricotGS). The Prospero catalog entry for PPSA99429 is a Coming-soon reservation. It has no public version, artifact URL, checksum, or download.

The repository contains no ROM/ISO, extracted game data, original game assets, BIOS/firmware, game-derived native module, save file, or executable release. The old synthetic module test remains historical evidence only; it is not a game build.

## Private engineering state

The current private work uses a statically translated native module format and a native runtime. A checked IPGD input identity can reach the early laboratory/starter flow, with private save and reload verification. The earlier v0.1.2 PS5 preview verified native intro/title startup on the physical console.

This is an early native foundation, not a complete port. The following are still open:

- full ARM9 and ARM7 coverage across the campaign;
- battles, menus, events, audio and all game-specific services;
- complete HeartGold support and independent regional validation;
- the launcher choice between two prepared editions;
- stable PS5 validation of the current private module;
- clean-room/source/license audit for a future public source release;
- a compatible private-room link/trading service.

No emulator CPU core, interpreter, or JIT fallback is part of the intended architecture.

## Planned edition handling

Each locally prepared game input will be identified from its verified header and content identity. If both editions are present, ApricotGS should show an edition choice at launch. If exactly one is present, it should start that edition automatically. Saves remain isolated by edition and verified input identity.

This launcher behavior is a planned feature and is not promised by the current catalog reservation.

## Planned link/trading direction

A future trading feature would use a private room and an explicit compatible native link service. Room connectivity alone is not sufficient: both sides need matching protocol behavior, confirmation, crash-safe recovery, and tests for the actual in-game exchange. No Nintendo online service or physical DS compatibility is claimed.

## Next release gate

The next public release should be a reviewable source/documentation milestone only after the source, dependency notices, generated-file exclusions, and target-hardware evidence have been audited. A playable release remains out of scope until the complete native game path is implemented and tested.
