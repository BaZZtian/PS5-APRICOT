# Binding repository policy: ownership, licensing, publication

Applies to all maintainers, AI agents, automation, commits, PRs and releases. Read
this file before changing code, documentation, metadata or Store submissions.

1. **Separate authorship from maintenance.** The repository maintainer controls
   their fork, releases and their own original contributions only. Do not claim
   ownership of upstream engines, decompilations, ports, assets, SDKs or code
   authored by other contributors. Credit the actual authors and rights holders.
2. **Never relicense third-party work.** Preserve original copyright/SPDX
   notices, licenses, attribution and source-offer obligations unchanged.
   Ownership of a fork or a Store title ID grants no upstream rights. No single
   MIT/GPL/MPL/CC0 label applies automatically to a mixed program; confirm each
   actual component, revision and license compatibility before release.
3. **Do not license newly authored code by assumption.** Its copyright stays with
   its actual authors. No new public license, SPDX identifier or blanket
   proprietary claim without an explicit authorized decision. Previously
   granted licenses on earlier files/releases remain applicable and must not be
   removed or represented as revoked.
4. **Fail closed on rights doubts.** Missing or contested permissions, unclear
   provenance, conflicting licenses or uncertain bundled assets block source
   export, packaging, releases and Store submissions until independently
   checked. An upstream project's public visibility is not a license.
5. **Publish only what users and contributors genuinely need.** Public content:
   functional source, build/install documentation, tests and fixtures safe to
   distribute, required notices, concise changelog and verified status.
   Internal strategy, working notes, drafts, incident logs, private test reports,
   exploratory analyses, local paths, account details and AI transcripts stay
   outside the repository, including outside git history.
6. **Secrets are never committed.** No access tokens, API keys, passwords,
   credentials, private signing keys, service-account files, private certificates,
   account IDs, private URLs, live saves, game dumps or derived proprietary
   assets. Inspect tracked files, diffs and release artifacts for sensitive
   data before publishing. Treat suspicious content as private until reviewed;
   never echo discovered secrets in reports. If a secret was committed, stop
   publication, revoke/rotate it through its provider and handle git history
   separately. .gitignore does not unpublish historical commits.
7. **Public communications must state roles accurately.** Say "maintainer of
   this port/repository" or "submitter of this listing", not "owner of the
   original application", unless independently proved. Preserve credit.
8. **Do not use a release deadline as an exception.** An AGENTS.md rule does
   not grant rights, change any existing license or constitute legal clearance.
