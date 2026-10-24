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

ApricotGS brings HeartGold and SoulSilver to a PS5 homebrew environment through a dedicated native player. Version 0.3.0 includes the same console-local static preparation and gameplay fixes as the accepted v0.3.0 tester: supply your own supported NDS dump and the PS5 prepares it on first use. A PC Builder is not required to play this release.

The project uses a native static execution path. It does not use an ARM interpreter or JIT fallback. Unsupported code paths can stop with an explicit error instead of silently switching execution methods.

**Bring your own legally obtained `.nds` dump.** The download contains no game ROM, reconstructed game source, generated game module, BIOS, firmware, extracted game graphics, music or save files. All game-derived preparation stays on your own console or optional local PC tools.

## Status and availability

Get application packages and the matching PC preparation tool from [GitHub Releases](https://github.com/BaZZtian/PS5-ApricotGS/releases). Published releases will include checksums and their known issues.

**[v0.3.0](https://github.com/BaZZtian/PS5-ApricotGS/releases/tag/v0.3.0) is the current experimental console release.** The complete version-by-version fix history is in the GitHub Release description. ProsperoStore links to the same release and exact application download.

The application title is **ApricotGS**. Prerelease status is recorded in release metadata, not added to the PS5 title name.

## Features

- **Two editions, one app.** Both supported editions are prepared locally on PS5 and have separate prepared folders. With both present, choose an edition in the animated 3D cartridge launcher; with one present, it is selected automatically.
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
| First setup | Launch once to create the persistent ROM inbox, copy your dump there, then restart |
| Transfer | A local file-transfer service on the PS5 |

PC preparation is optional for this release. Developers building the corresponding source need the pinned PS5 homebrew toolchain and Vulkan dependencies referenced in the source and notices.

An `.nds` file is a cartridge dump, not an ISO. Renaming another region or modified ROM does not make it compatible.

## Installation

### 1. Install ApricotGS

Install **ApricotGS v0.3.0** from ProsperoStore or extract the release's PS5 ZIP into `/data/homebrew/`. The normal application title is ApricotGS, title ID `PPSA99429`; it has no Update Tester suffix. No game is included.

### 2. Add your own game

Launch once, then exit with Options if no game is present. Copy your supported, unmodified `.nds` dump into the persistent input folder:

```text
/data/apricotgs-roms/
```

The filename may be chosen freely; compatibility is checked using game code, maker code, revision and the complete ROM fingerprint. The folder survives application replacement. Keep the original dump and do not share it.

### 3. Let the PS5 prepare it

Restart ApricotGS. Static native preparation, startup validation, cartridge-label creation and menu-audio creation run on the console. The progress screen shows the current phase and elapsed time. First preparation can take several minutes per edition; keep the application running until it completes. Unsupported input or execution paths produce an explicit error, with no interpreter/JIT fallback.

Complete game folders from older PC-prepared versions remain read-only migration inputs. This release prepares new versioned edition folders automatically when their original game data is available, leaving previous folders intact. Both editions then appear in the cartridge launcher; choose with the directional controls and start with Cross.

### 4. Saves and updates

Save using the game's own save menu. Production saves remain identity-based under `/data/orqenva/saves`; updating the app must not replace this directory. Back up your saves before updating. The tester's separate save namespace is not imported automatically.

To exit during play, press **L3 + R3**. Store updates replace the application folder, while the persistent ROM inbox, prepared production data and saves remain separate.

The optional PC tools in the source package remain available for existing workflows; they are not a requirement for console preparation.

### Preparation recovery and Home music

The app verifies prepared edition files and replaces damaged or older media
transactionally when the exact supported original dump is available. Previous
folders are retained, and saves stay separate. A completed, validated native
module checkpoint can resume the remaining media/commit phases after restart;
an interruption before that checkpoint requires compilation again.

Home-tile music is generated from your own game data on the console. After
preparation, close the app and allow the Home screen to refresh its title media.
No game music is shipped. Cartridge switching crossfades continuously advancing
title tracks rather than pausing the unselected track.

## Compatibility

The current translation profiles target these **German, revision 0** editions:

| Edition | Game code | Maker code | Status |
| --- | --- | --- | --- |
| HeartGold | `IPKD` | `01` | Experimental |
| SoulSilver | `IPGD` | `01` | Experimental |

Do not assume US, other languages, ROM hacks or modified dumps work because their headers look similar. An identity recognised by a parser is not a promise of gameplay compatibility.

This is an experimental release. A complete campaign has not been verified. See the [GitHub Release description](https://github.com/BaZZtian/PS5-ApricotGS/releases/tag/v0.3.0) for the complete fix history, tested scope and remaining limitations.

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
