# ApricotGS - voluntary support reports

Report reproducible problems at https://github.com/BaZZtian/PS5-ApricotGS/issues.
Include the app version, edition/game code, language, reproducible steps, expected result and actual result. Review KNOWN-ISSUES.md first.

## Limited report in the0.5.7 candidate

The runtime writes `/data/orqenva/debug-report.json` at startup, selection, gameplay entry, handled failure and normal exit. It does not upload anything or create an issue. Console verification is pending;0.5.6 does not implement this limited report.

The report has a fixed allowlist: schema/app/version, supported game code, lifecycle phase, frame counter, two numeric native stop categories and automatic_upload=false. It contains no ROM hashes, save content, inventory, player name, RAM/registers, filenames, account identifiers or network addresses. It is under1024bytes, atomically replaced with0644 permissions. An abrupt OS termination may leave the previous lifecycle checkpoint; this is not a complete crash dump or a definitive root cause.

Inspect it before deliberately attaching it to an issue. Keep personal details out of your issue text.

## Never share

ROM/ISO/NDS files; derived native code/modules; title labels; music; extracted assets; savegames; RAM/core dumps; raw/private logs; complete game/app folders. In particular, `/data/orqenva/diagnostics/*.txt` and `*.ram` are **private** and are not the public report. Do not upload them.

Runtime-generated reports and user data are never release contents. Packagers must build from an explicit allowlist rather than compressing the installed console folder.
