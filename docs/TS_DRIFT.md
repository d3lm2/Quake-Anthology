# TS drift since the C port began

The C port started from TS revision `53ad56d` (2026-10-25). TS has had 44 commits since,
including the 2026-11-01 audit remediation (`2a2cb7bd`, 1537 files) and the SourceStrategy
refactor of SharedSimulation. Every TS change since the baseline must be reflected in C
before the confirmation gate can pass.

## Tools

- `bun tools/ts_semantic_drift.ts ../quake-anthology-ts 53ad56d > docs/ts_semantic_drift.tsv`
  classifies every TS file: `code` (behaviour changed), `types-only`, `format-only`,
  `added`, `removed`, `same`. Only `code`, `types-only`, `added` and `removed` need work.
  The 2026-11-03 Biome reformat (`6420d047`) is semantics-free except for 5 files
  (application.ts, network/remote-unified.ts, simulation/runtime.ts,
  compat/q2/native-mod-provider.ts, content/q1/foundation/spawns.ts), which the diff tool covers.
- `tools/ts_semantic_diff.sh FILE...` or `tools/ts_semantic_diff.sh --module content/q1`
  prints a diff with the baseline run through TS's own Biome config first, so the reformat
  drops out and only real changes remain. New files print a note to read them in full.
- `python3 tools/ts_drift_files.py` (mtime-based, coarser) writes `docs/ts_drift.tsv`.

## Counts (2026-11-03)

src: 694 code, 393 types-only, 88 added, 68 removed (10 of them moves, listed below).
tests: 415 code, 140 added.

Moves: platform/files/{contained,writable} -> core/files; persistence/q1-source ->
compat/qc/q1-source-save; persistence/q1-quakec -> compat/qc/q1-save;
ui/common/legacy/script/* -> content/legacy-script/*.

## Procedure for a drift packet

1. For each file in the packet, read the semantic diff. Many C files were written after the
   TS change landed, so first check whether C already has it; do not apply a change twice.
2. Apply each real change to the C counterpart (manifest row, or search by symbol).
   Added TS files get ported (or mapped to an existing C file) and a manifest row.
   Removed TS files: delete the C counterpart only if nothing in C still needs it and TS
   moved or dropped the behaviour; otherwise note it in the open items.
3. Port the changed and added TS tests for the packet, with bun-oracle expected values.
4. Build with tools/agent_build.sh (private BUILD dir, -j1) and run every affected test
   binary plus tests/app/test_sim_verify (must stay at 0 differing lines; run it from the repository root: the user content root, whose path is recorded in checkpoint images, is fixed at build/agents/sim-verify2/tmp regardless of TMPDIR).
5. Record each file as `drift-done` in docs/port_review.tsv with a one-line note.

## Packets

| Packet | Scope (src/…) | Diff lines |
|---|---|---|
| D1 | app/bootstrap/simulation | 14.8k |
| D2 | app/bootstrap (rest) incl. network, effects | 12.4k |
| D3 | content/q1 | 9.2k |
| D4 | content/q2 | 6.3k |
| D5 | content/q3, bots | 5.3k |
| D6 | compat, guest | 8.1k |
| D7 | network, render, world, movement | 9.5k |
| D8 | ui, core, formats, persistence, contracts, audio, materials, text, console, settings, input, llm, platform, media, camera, capture, debug, foundation | 6.5k |

## Second drift round (D9, 2026-11-10)

TS moved from `87e98c5c` (the state the D1-D8 packets and the sim_verify oracles were
checked against) to release 1.2.7 plus uncommitted work: 23 commits, 102 src files with
code changes, 14 added src files, 2 removed, 35 changed and 11 added tests. The list is
`docs/ts_semantic_drift_d9.tsv` (same columns as ts_semantic_drift.tsv). To see the diffs:

    TS_BASELINE=87e98c5c TS_DRIFT_TSV=$PWD/docs/ts_semantic_drift_d9.tsv \
      tools/ts_semantic_diff.sh --module app/bootstrap

| Packet | Scope | Diff lines |
|---|---|---|
| D9a | settings, persistence, core, compat, guest, world, formats, input, text, ui, render, movement, network (everything outside app/bootstrap) | ~1.4k + 9 new files |
| D9b | app/bootstrap except simulation | ~6.6k + 5 new files |
| D9c | app/bootstrap/simulation (after the SourceStrategy work) | ~0.9k + 1 new file |

D9a is done (2026-11-10): see the `drift-done (d9a)` rows of docs/port_review.tsv and the D9a entry in the open
items of docs/PORT_COMPLETION.md. Oracle generators for its tests: tests/settings/oracles/layers_steps.ts and
tests/persistence/oracles/q2_save_folder.ts (run under bun from the TS repository).

Themes: stock Quake II saves and loads in the original formats (persistence/q2-save-*,
q2-selection, app q2-native-saves); stock games read and write native config files and
keep demos, screenshots, saves and logs in their own folders (settings/native-*, app
native-settings, native-migration, console-log, seat-profile); engine/game settings
layering on the cvar alias layer (settings/layers, setting-spellings,
core/cvars/setting-declarations); cap removals (392943c8, and 14c69e49 removes the guest
mapped-memory quota and TagMalloc limit that D6 ported, so those come out of C).

### D9c status (2026-11-10, build/agents/d9c): done

Every code/types-only/added/removed file under src/app/bootstrap/simulation in docs/ts_semantic_drift_d9.tsv has a
`drift-done (d9c)` row in docs/port_review.tsv. Ported: native-q2-rerelease-pak.ts (new), the version 2
rerelease native-original record with an absent level, SourceStrategy importLoading/importOriginal (228 members),
SimulationOptions.nativeImport, SaveCaptureOptions (autosave), the classic guest file cap removal, the classic
linkableClientPrefix, and the removal of vec.ts. Forced edit outside the packet: `logfile` in
src/app/bootstrap/q3_common_cvars.c (TS 9542c329), which the q3 and q3qvm sim_verify oracles record.

sim_verify oracles regenerated from TS 6103bb46 (frames 0 1 10 100). Lines that changed against the 87e98c5c oracles:
q2nc 8 (the four `q2:classic-native-original` provider lines, version 1 -> 2, and the four checkpoint-image lines;
TS e06fcfff), q3 6 (the `q3:native` provider line and the checkpoint-image line at frames 1, 10 and 100: the saved
cvar list gains `logfile`; TS 9542c329), q3qvm 4 (the checkpoint-image line at frames 0, 1, 10 and 100: the guest
host state gains `logfile` and the guest's vmCvar handles shift by one; TS 9542c329). The other seven are unchanged.
C: 10 cases, 0 mismatches. New: `test_sim_verify --diagnostics` (tests/app/oracles/sim_diagnostics_<case>.txt).

Not done here: no C test starts a world from options.nativeImport (the TS test is
tests/first-playable/application/q2-native-original-saves.test.ts, an Application test that needs D9b's q2-native-saves).


## Final TS state (2026-11-10 21:35)

The TypeScript project is final at tag `v1.2.7` (HEAD `6f93620c`, which only bumps the
version to 1.2.8-dev after the tag; the tree is clean). `docs/ts_semantic_drift_final.tsv`
lists what changed between `03a43888` (the D9 baseline's HEAD) and that HEAD: 8 src files
and 13 test files. Checked hunk by hunk on 2026-11-10: C already has every src change
(the `nativeConfigCarries` rule in configuration and prepared-startup, the write redirect
keyed by install root only, the download directory without the user root), because D9a
and D9b read the TS tree after those edits were made. `src/version.c` stays "1.2.7", the
released version. The test changes are timeouts, the hermetic preload and download tests
expecting the game folder; they belong to the remaining test ports. No further TS drift
is expected; rerun `bun tools/ts_semantic_drift.ts ../quake-anthology-ts 6f93620c` to
confirm before the gate closes.
