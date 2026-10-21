# Known issues

- SoulSilver: a reported house-entry native memory failure remains unresolved. A local6000frame replay did not prove that the same route was traversed and does not establish a fix.
- HeartGold: a post-title missing-native-target fault was reproduced locally; five proven switch targets were added. The corrected1800frame sanitizer replay passes, but physical-console regression acceptance remains separate.
- Full campaign, battles, broader progression and per-edition save/load coverage are incomplete. Trading and network rooms are not implemented.
- Some building transitions and save operations can stall. Improved ordinary movement does not prove every transition is fixed.
- New physical-button legend and input mapping need final owner hardware acceptance. Sharp presentation scales original game pixels; output/UHD display is not higher internal game resolution.
- Public ROM-to-module preparation still depends on closing the initialization-variant bootstrap gate. There is currently no public playable package.

Patches and additional acceptance work are planned without promised dates. Report reproducible errors using SUPPORT-REPORT.md; never attach game data or saves.
