<p align="center">
  <img src="sce_sys/icon0.png" width="160" height="160" alt="ApricotGS project icon">
</p>

<h1 align="center">ApricotGS</h1>

<p align="center">
  <strong>HeartGold &amp; SoulSilver on PlayStation 5</strong><br>
  An independent PS5 homebrew player and preparation tool maintained by <a href="https://github.com/BaZZtian">BaZZtian</a>.
</p>

<p align="center">
  <a href="#status-and-availability"><img src="https://img.shields.io/badge/status-experimental%20release-orange" alt="Status: experimental release"></a>
  <img src="https://img.shields.io/badge/platform-PS5%20homebrew-003791" alt="Platform: PS5 homebrew">
  <a href="#requirements"><img src="https://img.shields.io/badge/game%20data-Own%20NDS%20dump-555555" alt="Game data: Own NDS dump"></a>
</p>

<p align="center">
  <a href="#status-and-availability">Status</a> &middot;
  <a href="#installation">Installation</a> &middot;
  <a href="#compatibility">Compatibility</a> &middot;
  <a href="#support">Support</a> &middot;
  <a href="#credits-and-licenses">Credits &amp; licenses</a>
</p>

## About

ApricotGS brings HeartGold and SoulSilver to a PS5 homebrew environment through a dedicated native player and a PC preparation tool. The PC tool statically translates the supported game's ARM9/ARM7 code into an x86-64 module. The PS5 player loads that locally generated module and supplies graphics, audio, controller, storage and save services.

The project uses a native static execution path. It does not use an ARM interpreter or JIT fallback. Unsupported code paths can stop with an explicit error instead of silently switching execution methods.

**Bring your own legally obtained `.nds` dump.** The download contains no game ROM, reconstructed game source, generated game module, BIOS, firmware, extracted game graphics, music or save files. All game-derived preparation stays on your own PC and console.

## Status and availability

Get application packages and the matching PC preparation tool from [GitHub Releases](https://github.com/BaZZtian/PS5-ApricotGS/releases). Published releases will include checksums and their known issues.

**[v0.0.2](https://github.com/BaZZtian/PS5-ApricotGS/releases/tag/v0.0.2) is an experimental bug-fix release.** Download the PS5 player and matching PC Builder and Source packages together. Prospero Store availability is separate from GitHub publication: check the live Store listing before attempting a Store installation. Known gameplay errors are listed below.

The application title is **ApricotGS**. Prerelease status is recorded in release metadata, not added to the PS5 title name.

## Features

- **Two editions, one app.** HeartGold and SoulSilver have separate prepared folders. With both present, choose an edition in the animated 3D cartridge launcher; with one present, it is selected automatically.
- **Edition-specific presentation.** Cartridge labels and title music are generated locally from the matching game data, rather than bundled in the download.
- **DualSense controls.** Graphical PlayStation button prompts, analog movement and a touchscreen pointer controlled with the right stick and R2.
- **Two-screen layout.** A large main screen, a secondary touchscreen view and an original ApricotGS background.
- **Sharp presentation.** Nearest-neighbour scaling preserves the original pixel artwork. UHD output does not imply new high-resolution game textures or a native 4K internal game renderer.
- **Separate saves.** Each edition uses its own save identity. Updating the application must not replace the user's prepared games or save directory.
- **Optional support sharing.** A small local report can be attached manually when reporting a problem. There is no automatic report upload.

## Requirements

| Component | Requirement |
| --- | --- |
| Console | A homebrew-enabled PS5 with a compatible native-app environment and Prospero Store |
| Game data | Your own supported, unmodified HeartGold or SoulSilver `.nds` dump |
| Preparation PC | Ubuntu 24.04 x86-64, or Ubuntu 24.04 under WSL2 |
| Build tools | LLVM/Clang 18, LLD, a C++ build environment and glslang-tools |
| Media preparation | Python 3.12, Pillow, NumPy, Vulkan headers/loader and Mesa's Lavapipe driver |
| Transfer | A local file-transfer service on the PS5 |

An `.nds` file is a cartridge dump, not an ISO. Renaming another region or modified ROM does not make it compatible.

## Installation

### 1. Install the PS5 player

Once the downloadable entry is live, install **ApricotGS** from Prospero Store. The player itself contains no game. Its title ID is `PPSA99429`.

Keep the application installation separate from the prepared game directories described below. Do not place ROMs or saves inside an application ZIP or redistribute a populated installation.

### 2. Prepare your own game on PC

Use the PC preparation tool supplied with the same release. It verifies the game code, maker code, revision and full ROM fingerprint before selecting a translation profile.

For Ubuntu / WSL2, install the required tools and create a Python environment:

```bash
sudo apt install clang-18 lld-18 build-essential glslang-tools \
  libvulkan-dev mesa-vulkan-drivers python3-venv
python3 -m venv .venv
.venv/bin/pip install Pillow==11.3.0 numpy==2.3.4
```

From the extracted preparation package:

```bash
.venv/bin/python tools/apricot-prepare.py \
  "/path/to/your-own-game.nds" "/path/to/private-preparation" --jobs 4
```

Choose a **new output directory** for each preparation. Existing output is not overwritten. The process extracts the required data, statically compiles the native module, and generates the matching title label and menu music locally. Initial compilation can take some time.

For updates containing compiler or translation-profile fixes, prepare your original dump again with the new release's PC tool and transfer the complete new edition folder. Updating the PS5 player alone does not change an already prepared native module. Preserve your existing save directory when replacing prepared game files.

The preparation result identifies the transfer folder and destination. Keep the complete preparation private: it contains material derived from your game dump.

### 3. Transfer the prepared edition

Copy the contents of the tool's edition folder to the matching location:

```text
/data/orqenva/games/
├── heartgold/
│   ├── edition.bin
│   ├── game-data.bin
│   ├── game-203.oqgame
│   ├── identity-refs.bin
│   ├── title-label.rgba
│   └── title-menu.pcm
└── soulsilver/
    ├── edition.bin
    ├── game-data.bin
    ├── game-203.oqgame
    ├── identity-refs.bin
    ├── title-label.rgba
    └── title-menu.pcm
```

Use the folder produced for that exact edition. A copied or renamed module from the other edition will not pass identity checks. The `orqenva` data path is retained for compatibility with existing private installations; the displayed application name is ApricotGS.

### 4. Launch and play

Start ApricotGS. If both complete editions are present, select the cartridge you want and confirm with Cross. If only one is prepared, that edition is selected automatically. Cartridge labels and menu audio correspond to the selected edition.

Save through the game's own save menu. To leave the application, press **L3 + R3**. Back up your own save directory before changing installations or trying experimental versions.

## Compatibility

The current translation profiles target these **German, revision 0** editions:

| Edition | Game code | Maker code | Status |
| --- | --- | --- | --- |
| HeartGold | `IPKD` | `01` | Experimental |
| SoulSilver | `IPGD` | `01` | Experimental |

Do not assume US, other languages, ROM hacks or modified dumps work because their headers look similar. An identity recognised by a parser is not a promise of gameplay compatibility.

Version 0.0.2 corrects DMA and card-transfer timing, ignored stores to unconnected memory banks, and native code discovery shared by both supported editions. These changes address reported missing/striped field characters, Options/frame confirmation stops and missing native targets in story, battle and sign interactions.

Targeted native-PC regressions and PS5 testing have been performed; a complete campaign has not been verified. Uncovered code paths can still stop explicitly, transitions may cause loading delays, and cartridge labels may capture an animation frame instead of the title logo. Other regions and modified dumps remain unsupported. **Re-prepare your original dump with the v0.0.2 PC Builder to receive compiler/profile fixes, and preserve your save directory.**

Wireless trading, private rooms and online services are not supported release features.

## Controls

| DualSense input | Game / application action |
| --- | --- |
| Cross | A / confirm |
| Circle | B / back |
| Triangle | X |
| Square | Select |
| Touchpad click | Y |
| Options | Start |
| L1 / R1 | L / R |
| D-pad / left stick | Movement; launcher selection |
| Right stick | Touchscreen pointer |
| R2 | Touchscreen press |
| L3 + R3 | Exit application |

The game view displays graphical button prompts. L3 alone does not toggle a blur filter.

## Support

Please open a [GitHub issue](https://github.com/BaZZtian/PS5-ApricotGS/issues) with:

1. The application version and edition/game code.
2. The steps leading to the problem, including the map or transition where it occurs.
3. What you expected and what happened instead.
4. Whether the issue repeats after restarting.

You may attach the limited `/data/orqenva/debug-report.json` **voluntarily**. It contains the app/version, game code, phase, frame count and numeric stop categories. It contains no save contents, RAM snapshot, account details or automatic upload mechanism.

**Never attach ROM/NDS/ISO files, generated modules or game source, extracted graphics/music, savegames, raw diagnostic logs, RAM/core dumps or the complete game folder.** Private diagnostic files from older test versions are not public support reports. Do not share API keys, tokens or passwords.

If you see **Native target absent**, report the version, edition and steps; do not substitute another game's module. If an edition is missing, check that the complete matching prepared folder was transferred and that preparation finished successfully.

## Credits and licenses

ApricotGS is maintained by **BaZZtian** as an independent PS5 port project. This does not confer ownership of the original game, its engine or third-party contributions. Original authors retain their copyright and license terms. Independently authored contributions remain controlled by their actual rightsholders; no additional license is granted by assumption.

Platform contributions include attributed work by **Mihawk-99 / Mihawk** and **BlackBearReloaded**. The native graphics path uses the PS5 Vulkan/Mesa work and the homebrew payload SDK. Controller prompts derive from **AHarmlessPotato's** CC BY 4.0 DualSense model renders, with the required attribution and modification notices. Component notices, exact dependency source revisions and source build entry points are included in [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt). Component licenses are retained under licenses/.

The existing [MIT license](LICENSE) applies to the original work covered by that notice. It does not relicense GPL/LGPL components, controller artwork, proprietary game content or the combined runtime as a whole. Missing rights or incompatible license obligations block publication; third-party notices are retained.

Game names are used only to identify compatibility. No game content is supplied, no affiliation with Nintendo, Game Freak or Sony is claimed, and users must have the rights required to prepare their own data under applicable law. Providing your own dump is not a blanket legal exemption.
