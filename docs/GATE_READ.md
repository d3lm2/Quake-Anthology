# Gate read: instructions for a reader

Note (2026-11-12): the owner removed all test infrastructure from this project. Ignore every instruction below
about review tests, oracles and running test binaries; do not create files under `tests/`. Verify by building
(`tools/agent_build.sh NAME all`) and, where needed, scratch programs kept outside the repository.

The confirmation gate requires every C function to be read against its current
TypeScript counterpart in `../quake-anthology-ts` (TS is the source of truth),
and every real divergence fixed. A reader is given one scope. This file is the
standing brief; the task message names the scope, the build directory and any
scope-specific notes.

## Read first

- `docs/PORT_COMPLETION.md`: Rules, Confirmation gate, the "Gate read packets"
  list and the "Gate full read still to do" item.
- `docs/PORTING.md`: conventions.
- `docs/port_review.tsv`: rows for your scope. Rows tagged `drift-done` had
  TS changes applied with targeted reads; a row marked "read in full" is done.
  `docs/manifest.tsv` maps TS files to C files.

## What to check in each function

- Control flow, every branch, every error path.
- Error handling: `qa_raise`/`qa_try` for TS throws. Abort only where TS would
  leave a throw uncaught; remote, file and guest input must never abort. The
  error class must match TS (Error, RangeError, TypeError, AggregateError and
  the project's own classes). TS `try/finally` sites must restore state in C.
- Numeric semantics (`include/qa/foundation/js.h`): NaN propagation through
  Math.min/max, ToInt32/ToUint32/ToUint8 where TS uses `|0`, `>>>0` or typed
  arrays, Math.fround staging where TS uses fround or Float32Array,
  `qa_js_hypot*` instead of C `hypot`, string lengths in UTF-16 units, JS
  `trim` whitespace.
- Evaluation order: operands evaluated into locals in TS order (C argument and
  initializer order is unspecified). Random-number call order.
- Iteration order: Map/Set insertion order versus C arrays; `find` takes the
  first match; Map.set overwrites; loops that remove, free or damage elements
  while iterating.
- Ownership: leaks and use-after-free on raise paths; pointers kept into
  caller-owned or reallocating storage where TS copies or holds a reference;
  objects on the C stack that a callback can keep.
- Limits: fixed C caps and fixed-size buffers where TS has none.
- Data tables: compare value for value against a bun dump unless an existing
  test already does.

Do not rewrite working code for style.

## For each real bug

Fix it, and add a check to a review test for your scope
(`tests/<module>/test_<scope>_gate_review.c`), with expected values produced by
bun (a small oracle under `tests/<module>/oracles/`). Never invent expected
values. Where a fix cannot be reached from a test, say so in the report.

## Verification

- Every test binary for your module, plus the tests the task message names.
- `tests/app/test_sim_verify` from the repository root: 10 cases, 0 mismatches.
  Do not regenerate its oracle files.
- ASan/UBSan/LSan on your new test (copy an `asan.sh` from
  `build/agents/gate-q1b/tmp/` or `build/agents/gate-render5-asan/`).
- Tests that need game data: `QTS_TEST_CORPUS=~/qfiles`; the rerelease game
  DLL corpus is `/home/buzzkill/Projects/qfiles`.

## Hard constraints

- Build only with `tools/agent_build.sh NAME` (private build directory, `-j1`).
- Run every test binary, oracle and tool through `tools/run_capped.sh`,
  including scratch binaries built to prove a check. It applies a 6 GB resident
  cap and runs the command hermetically: throwaway HOME, XDG directories and
  TMPDIR, `QTS_TEST_GAME_WRITE_ROOT` pointing at a scratch folder (a stock game
  writes config files, logs, saves and downloads beside its game data), and
  `SDL_AUDIODRIVER=dummy`. `QTS_TEST_CORPUS` defaults to the real `~/qfiles`.
  Never open an application over the corpus outside the wrapper.
- Close stdin (`</dev/null`) for tests whose bun oracle opens a console
  (`test_application_open_real`); otherwise the oracle never exits.
- Never run more than one build or one heavy test at a time.
- If an unrelated file fails to compile because another agent is mid-edit, use
  `make -k` and a private `ar qc` + `ranlib` archive.
- No helper or sub agents.
- No git commands that modify the repository or index.
- Never use broad `pkill -f` patterns; kill only processes you started, by
  PID. Do not write wait loops whose `pgrep` pattern matches their own command
  line.
- Write logs under your build directory's `tmp/`.
- Before writing a new file, check that it does not exist. Never overwrite a
  file you have not read.
- Keep the tree compilable at every step; other agents share it. When a new
  file takes over definitions, add it before removing the old ones. If a fix
  changes a public signature, update every caller in the same pass and list
  callers outside your scope.
- Stay inside your scope. If you find a bug in another agent's area, list it
  in your report with file and line; fix it only if the task message allows.
- Zero new warnings in touched files.

## Records

- `docs/port_review.tsv`: one row per TS file read (functions read, note).
- `docs/PORT_COMPLETION.md`: mark your packet in the "Gate read packets" list
  and update the "Gate full read still to do" item.
- If context runs low, stop at a clean point and record exactly which files
  remain.

## Final report

Functions read per file group; bugs fixed, one line each; tests and results as
actually run; the sim_verify result; what remains. Be exact, and do not claim
anything you did not run.
