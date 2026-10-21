# ApricotGS

![ApricotGS icon](sce_sys/icon0.png)

A dedicated native PS5 player and PC static-translation project for HeartGold and SoulSilver. The displayed application name is **ApricotGS**. Development status belongs in release notes, not in the title.

**Bring your own legally obtained game data.** No ROM/ISO, game-derived executable, reconstructed game source, extracted graphics/music, BIOS/firmware or savegame is distributed here. The PC preparation step must generate each user's private output locally; the PS5 player supplies platform services.

## Current status

Private build0.5.6 was installed, fully readback-verified and started on PS5. Both German editions have independently prepared modules, title imagery/music and separate save identities. A two-cartridge selector, native controller/audio/display services and early gameplay have private test evidence. No interpreter or JIT fallback is used.

The current public repository is a development record, **not a downloadable playable release**. A first public experimental package is intended as v0.0.1. Known gameplay bugs may be addressed through later patches; a complete campaign has not been verified. See [tested scope](TESTED.md) and [known issues](KNOWN-ISSUES.md).

The next0.5.7 candidate removes the old display-name suffix and adds a small manually shareable support report. Host ASan/UBSan13/13 passes; console verification of that candidate is pending. Private version numbers do not imply a public release.

## Controls

The current private build displays actual DualSense button artwork next to game inputs. Cross maps to A, Circle to B, Triangle to X, Square to Select, touchpad click to Y, Options to Start, L1/R1 to L/R. Left stick or D-pad moves; right stick moves the touchscreen pointer; R2 taps. L3+R3 exits. L3 alone no longer blurs the image.

## Help with debugging

Please report reproducible errors in [GitHub Issues](https://github.com/BaZZtian/PS5-ApricotGS/issues). Include app version, edition/game code, language, steps, expected result and actual result. Clearly distinguish a repeated failure from an occasional observation.

In the0.5.7 candidate, the limited report is `/data/orqenva/debug-report.json`. Sharing is voluntary and manual. **Never attach ROM/ISO/NDS files, game assets/music, generated modules, saves, raw logs, RAM/core dumps or the complete game folder.** Existing `.txt` and `.ram` files in the diagnostics directory are private developer evidence, not public support reports. [Report contents and privacy](SUPPORT-REPORT.md).

## Preparation and releases

The existing private workflow is not yet a finished public builder. An empty-user-directory test still needs to prove static initialization-variant generation without developer fixtures. Dependency source/license closure, distributable cartridge geometry and final package review remain required. [Release checklist](RELEASING.md).

The Prospero Store title reservation is PPSA99429, named ApricotGS. A Coming soon record is not a download. Only link a published, audited app ZIP with its exact checksum after the preparation path works. Store acceptance is reviewed by its maintainers.

## Licensing and credits

Original project documentation and independently authored helpers retain their own notices. The private player includes attributed open-source work by Mihawk-99, Mihawk and BlackBearReloaded; GPL/LGPL terms must accompany any corresponding release. Controller artwork derives from AHarmlessPotato's CC BY4.0 DualSense model renders; original notices must be retained. No umbrella license grants rights to third-party game content.

No affiliation with Nintendo, Game Freak or Sony is claimed. This is not a legal guarantee, and a user's ownership of a copy does not by itself settle every jurisdiction's reproduction/circumvention rules. AI assistance is used under project-owner oversight.
