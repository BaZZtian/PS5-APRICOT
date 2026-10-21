# Publishing ApricotGS

Application title: **ApricotGS**. Do not append Native Alpha to the visible title. First intended public tag: v0.0.1 experimental pre-release. Internal development0.5.x is not a public download claim. Later bug fixes and features follow the owner's published version policy.

## Required before a downloadable Store release

1. Reproduce user-ROM preparation from an empty directory: extract, statically compile ARM7/ARM9 and initialization variants, build the native module, create reference manifest and local title imagery/music. Do not silently require private developer variants or bundle game-derived files.
2. Pin actual platform dependencies and satisfy GPL/LGPL corresponding-source/notice requirements. Preserve author notices; a repository root license is not permission for every nested component.
3. Supply independently authored, distributable cartridge geometry. The privately supplied mesh lacks a supplied distribution license and must not be redistributed.
4. Build from reviewed sources, run matching Linux sanitizers, then cross-build/inspect PS5. Verify package install behavior separately from gameplay.
5. Package only an explicit list of cleared app/runtime/artwork/license files in `PPSA99429/`. Never archive the live console directory. Exclude ROMs/ISOs/NDS, generated game code/modules, title images/music, saves, diagnostics, credentials, logs, caches and private models. Keep a hash manifest of every member.
6. Publish the exact audited ZIP plus checksum and matching source to a versioned GitHub pre-release. Include known issues, install/preparation instructions, controls and voluntary debugging instructions. Never replace an already listed asset in place.
7. Update only `apps/PPSA99429.json` in the catalog with the actual release URL/version/hash and pinned icon. Run catalog validation and submit from the repository owner's account. Maintainer merge determines Store availability.

Known gameplay bugs may be documented in an experimental release; this checklist does not demand a full campaign before alpha. It does demand a usable own-data preparation route and a cleared distributable artifact.
