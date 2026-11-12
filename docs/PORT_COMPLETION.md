# Completing the port

`docs/manifest.tsv` says every file is "ported". That is not true. It records
that a C file exists. Measured on 2026-11-02, most game code could not execute
in the binary and many files were empty shells. Use these two tools instead:

- `tools/reachability.py [--list] [--module PREFIX]`: objects reachable from `main`.
- `tools/depth_audit.py [--write]`: per-file token comparison with the TypeScript;
  writes the backlog to `docs/port_gaps.tsv`.
- `tools/admission_audit.sh [path...]`: comments admitting behavior was left out.
  A file can pass the token check with branches falling into a no-op; these find it.
- `tools/symbol_audit.py [--write] [--module PREFIX]`: TS functions, classes, and
  methods with no C name; writes `docs/port_missing_symbols.tsv`.
- `tools/eval_order_audit.py [--list] [path...]`: expressions whose operands'
  effects can run in a different order than TypeScript's left-to-right.

## Paused 2026-11-03 (~14:00), resumed 2026-11-10

Rule added 2026-11-10: run every test binary, oracle and tool through `tools/run_capped.sh`
(default 6 GB resident cap, no swap; since 21:55 also hermetic: throwaway HOME/XDG/TMPDIR,
QTS_TEST_GAME_WRITE_ROOT scratch folder, SDL_AUDIODRIVER=dummy, like the TS test preload). A render test grew to 12 GB in 28 seconds and nearly
exhausted the machine; a capped run is killed (exit 137) instead.

Baseline of the committed tree on 2026-11-10: test_sim_verify 10 cases 0 mismatches;
test_native_mod_host 70/70; test_startup_port 457/0; test_remote_loopback qw, q2, q3 and
unified-mixed at 0 differing, the five Q1-profile-client families differ because current TS
(1.2.7) changed settings/config behaviour (packets D9a-D9c in docs/TS_DRIFT.md).

Gate read packets (planned 2026-11-10; mark each done here as it finishes):
- content/q1: foundation, base, quakec, equipment, addons, missionpacks DONE (missionpacks: gate-q1mp).
- render: DONE, read in full (gate-render5 finished gl/* and the worker; 864 functions, 135 fixes).
- content/q2: foundation DONE (gate-q2a, finished by gate-q2c); base and equipment DONE (gate-q2b:
  65 files, 601 functions, the 19 monster tables against a bun dump); missionpacks DONE (gate-q2mp: the
  71 files not already read by content-q2mp, the 24 monster tables against a bun dump);
  rerelease DONE (gate-q2r: 50 of 72 files read in full, 226 functions, 29 fixes, the 25 rerelease monster
  tables against a bun dump; gate-q2r2: the 22 monster behaviour files gate-q2r left, 397 functions by a
  declaration count, 15 fixes);
  multiplayer DONE (gate-q2c: 21 files, 252 functions, 29 fixes).
- content other: catalog, mounts, archive, roots, user-data and the small top-level readers DONE (gate-content:
  30 files read in full, 89 fixes in all); composition, mods and monsters DONE (gate-content2, 2026-11-11: the
  files gate-content left, 52 fixes; mods/native-callbacks read in TS and replayed against bun, its C not read line
  by line). content/legacy-script DONE (gate-content2: the lexer's public calls and nextInto, and the
  preprocessor's directives, macros, expression parser and reader calls, replayed against bun over 3491 scripts
  and call sequences; gate-legacy, 2026-11-11: lexer.c 1-1075 and its save state, memory.c, precomp_memory.c,
  source_memory.c, token_memory.c, and in preprocessor.c the macro table, global defines, builtins, save state,
  the async read and include_directive, 32 fixes; replayed against bun with a memory owner, save capture and
  restore and global defines in tests/ui/test_ui_gate_review_legacy.c).
- content/q3: base + foundation + equipment DONE (gate-q3a: 71 of 74 files, 741 functions, 98 fixes; gate-q3b:
  base/game rankings.ts, save-state.ts, save-values.ts, 50 functions, 12 fixes, and gate-q3a's open items); presentation DONE (gate-q3p: 28 of 43 files, 525 of
  660 functions, 44 fixes; gate-q3c: 14 more files, 119 functions, 22 fixes; gate-content2: client.ts, 16 functions, 4 fixes);
  team-arena DONE (gate-q3c: 15 files, 255 functions by a declaration count, 19 fixes).
- compat: q2 DONE, all 75 files read in full (gate-cq2: 19 files, 207 functions, 62 fixes; the classic and
  rerelease hosts, imports, module, records, printf, pmove and the layout tables; gate-cq2b: 28 more
  files, 234 functions, 107 fixes: classic combat binding, compatibility, native combat call, damage, input and
  pickups, the 13 native primary files, and native-mod actors, armor, client-stages, clients, deferred,
  invocations, pickups, region and weapon-stage; gate-cq2c: the last 28 files and C-only native_mod_host_api.c,
  326 functions, 144 fixes: native-mod-combat, -items, -protection, -provider and the 24 rerelease files that
  were left, foreign actors rewritten; open items under gate-cq2c below); qvm PARTLY
  (gate-cqvm: 29 of 93 files, 212 functions, 29 fixes; image, interpreter, operations, memory, memory writes,
  guest and game memory, game data, syscall dispatch and the common, cvar, file, memory, math, vector, server
  game, client game, server info, entity token, client state/cinematic/mark/audio/collision/browser and UI key
  traps, legacy presentation; entity-record fields against a bun dump; the mod provider, hooks, module,
  registry, symbols, regions, the other records, the bot traps, render and script traps are not: list under
  gate-cqvm below; gate-cqvm2: 36 more files, 370 functions by a declaration count, 112 fixes: the hooks
  (game, game-combat and its scope and binding, game-input, -equipment-movement, -inventory, -pickups,
  -weapons), the five primary profiles, grapple profile and provider, weapon-behavior-profile, item catalog
  and storage, body-scope, cgame-body, and mod-objectives, -actor-frame, -pickups, -player-events, -input,
  -clients, -protection, -weapon-stage, -items, -actors, -presentation-checkpoint, -spatial, -fields,
  -projections, -checkpoint; LEFT of its scope: mod-presentation, mod-provider, mod-validation;
  gate-cqvm3: 25 more files, 186 functions, 20 fixes: module, registry, symbols, regions, abi, artifacts,
  compatibility, cgame, ui, client-state, legacy-client-abi, the render, script, legacy bot, bot library and
  bot navigation traps, and the player, client-state, shared-entity, trace, render, script and bot-navigation
  records with the copy/write forms of entity-record; gate-cqc: mod-presentation, mod-validation and
  mod-provider, 41 fixes: compat/qvm DONE); qc DONE (gate-cqc: 26 of 38 files, about 340 functions by a
  declaration count: program, memory, machine, executor, builtins, the entity, world, client, spatial, movement
  and pusher hosts, actor-state, borrowed-actors, source-call, save, q1-save, q1-source-save, compatibility,
  profile, message-routing, message-effects, quakeworld-presentation, weapon-behavior-profile, mod-objectives,
  mod-commands; gate-cqc2, 2026-11-11: the other 12, about 160 functions: mod-environment, mod-input,
  mod-pickups, mod-actors, mod-clients, mod-validation, mod-messages, mod-combat, mod-protection, mod-items,
  presentation-host and the rest of mod-provider, 60 fixes, and the three open items settled); q3 DONE
  (gate-cqc: 7 files, about 80 functions, 16 fixes).
  guest: DONE (abi/core/elf/pe and runtime parts by gate-guest, 2026-11-11: core (memory, contracts, callbacks, cpuid,
  registers), elf (parse, index, relocate, unwind), pe (format, image, directories, exports, loader), abi (values,
  classify, adapter, runner), runtime/common (memory, format, format/arguments, float, scan, services) and
  runtime/windows/crt read in full, kernel TLS/FLS read; about 70 fixes, tests/guest/test_guest_gate_review.c with
  tests/guest/oracles/guest_gate_review.ts. The worst: guest memory never freed a retired mapping or an unobserved
  observer (a guest malloc/free loop grew the process without bound); every ELF/PE/guest-memory error was a
  RangeError, so nothing could catch it by class, and a failed ELF/PE load left its pages mapped; observers that raised
  stopped the rest; an empty CRT onexit table cleared and called the word below it; printf refused a 64-bit SIZE_MAX
  capacity. gate-guest2 (2026-11-11) took the rest: runtime/system-v (libc, stdio, iostream, locale, cxx, cxx-data,
  runtime) and runtime/windows (kernel, runtime, msvc/locale, msvc/streams) read in full; x86 arithmetic/decoder, x64
  decoder, trigonometric and raw-sse read in full; x86/x64 cpu, integer-kernel, plan, binary, x87, x87-extended and sse
  read at the run loops, unary/divide and block executor and otherwise checked by a differential run against bun
  (tests/guest/test_guest_gate_review_cpu.c, 14043 instruction cases over both CPUs, managed and plain x64, edge
  operands, faults, REP boundaries, self-modifying code; oracle tests/guest/oracles/guest_gate_review_cpu.ts). About
  50 fixes; the worst: x64 64-bit IMUL gave the unsigned high half; 32-bit IDIV of INT64_MIN by -1 killed the host
  with SIGFPE; the qa_bigint division was bit-serial, so an out-of-domain F2XM1 hung for hours (now Knuth D, plus
  bun's 2^20-bit BigInt RangeError); an ordinary error inside a CPU run left the memory's fault target pointing at a
  dead frame and skipped the TS register/flag/IP restore; the MSVC stream flush/inserters skipped their finally;
  System V load/initialize had no finally/catch; VirtualAlloc over mapped memory aborted; System V service states were
  never freed. Known divergence left: TS x87 fastBinaryInstruction (unported perf) omits DE for denormal D8/DC memory
  operands; when it is ported the C will follow. Left: x87.c's numeric faults (stack fault, unmasked exception)
  longjmp past bigint locals in execute_inner, a few bytes leaked per faulting instruction (LSan, 320 bytes over
  the differential); Windows canonical() lowercases ASCII only. guest DONE.
- bots: behavior (two packets); navigation. behavior/q3 PARTLY (gate-bots2, 2026-11-11: 23 of 30 files read,
  about 640 functions by a declaration count, 52 fixes, most of them plain Error throws that raised RangeError
  or printed and aborted; review test tests/bots/test_bots_gate_review_q3.c). LEFT in behavior/q3: catalog.ts;
  the save/restore halves of library.ts and movement-state.ts (fromHandle, allocate, checkpoint, restore); the
  type and enum files game-host.ts, navigation-types.ts, travel/types.ts, travel/index.ts (enum values not
  compared against a bun dump); source-game.ts and ai-definitions.ts were read in full by bots-finish.
  gate-bots3 (2026-11-11): those behavior/q3 leftovers DONE (catalog.ts in full, the library/movement-state
  save halves, the enums against a bun dump) and gate-bots2's open items fixed; behavior/rerelease PARTLY (16
  of 17 files read; brain.ts read except its constants/types head and checkpoint layout; data/botdata.ts
  parsers NOT read); navigation PARTLY (21 of 26 files read in full; NOT read: the AAS compiler files
  aas-cluster.ts, aas-optimize.ts, aas-reachability.ts, -geometry.ts, -spatial.ts, -special.ts, -types.ts and
  aas-write.ts, which nothing in the app reaches). estimate-store.ts sharing ported (estimate_store.c). About
  60 fixes; review test tests/bots/test_bots_gate_review_nav.c (bun oracle bots_gate_review_nav.ts), clean
  under ASan/UBSan/LSan.
  gate-symbols (2026-11-12): navigation DONE: the eight AAS compiler files read in full (149 declarations),
  and rerelease/data/botdata.ts read in full; 17 fixes (closest-edge projection staging, the float 0.1 step
  test, plain Errors raised as RangeError, undefined signed shifts in func_bobbing, the 64-pair game-rule cap,
  the skill header JSON text, and every allocation a raise out of a malformed asset leaked); review test
  tests/bots/test_bots_navigation_gate_review.c (bun oracle bots_navigation_gate_review.ts, 64 checks), clean
  under ASan/UBSan/LSan. Left in bots: brain.ts constants/types head and checkpoint layout (gate-bots3 note).
  behavior/library and the top-level behavior files DONE (gate-bots1, 2026-11-11: 21 files read in full, 1081
  declarations by a declaration count, 64 fixes: plain Errors that raised RangeError or aborted; restores that
  validated before reading their whole image; the goal and chat setup retirements, which longjmp'd across the
  script reader's catch frames, now unwind through qa_raise/qa_try; assets key/list/candidate/sort details;
  stale level-item heap; include diagnostics name the included file; leaks on raise paths. Review test
  tests/bots/test_bots_gate_review_library.c, 90 checks against oracles/bots_gate_review_library.ts). Open items
  from it are under "Open items found by porters" (gate-bots1). navigation not yet read.
- network: q1 + common + services + unified DONE (gate-net1, 2026-11-11: all 46 TS files and
  app/bootstrap/network/unified-finite.ts read in full, 575 functions by a declaration count, 26 fixes; review test
  tests/network/test_network_gate_review_q1.c with oracles/network_gate_review_q1.ts, 49 checks, clean under
  ASan/UBSan/LSan. The worst: HTTP Location resolved by string pasting (scheme-relative and query references
  requested the wrong path) and the first request went out with URL credentials; header values were untrimmed
  first fields (a trailing-space ETag disabled ranged downloads); the download sink kept the first close failure
  and leaked the retained descriptor when inspection cleanup failed; local account and ranking saves wrote raw
  control characters (unreadable JSON). Open items: ipx_dosbox error fan-out while a listener closes sockets;
  online ranking save key order; outside the scope: unified_content.c canonical() writes -0 as "0" (TS "-0") and
  raises RangeError for a non-serializable value (TS TypeError); unified_client.c/unified_server.c raise the
  channel failure as RangeError (TS Error); these three are fixed by gate-net3); q2 DONE (gate-net2, 2026-11-11: all 46 TS files read in full, 657
  functions by a declaration count, 24 fixes; review test tests/network/test_network_gate_review_q2.c with
  oracles/network_gate_review_q2.ts, 100 checks, clean under ASan/UBSan/LSan. The worst: the UTF-8 connectionless
  check read one byte past an unterminated KEX packet; handleQ2Rcon sliced the command past the text after an
  unterminated quote; UTF-8 tokens and the KEX userinfo/lobby-name limits counted bytes, not UTF-16 units; the
  private dlserver rule refused an IPX game server; readMvdHeader left a freed configstring list in its output after
  the player-limit raise; the MVD broadcast and GTV client had no idle timeout; KEX reliable-ring and broadcast
  buffers leaked when a send raised. Q2ConnectionlessServer (removed in TS) is deleted: test_server_browser answers
  through qa_app_q2_server_network. Open: prediction.c's sequence ring can alias two TS Map keys a capacity apart
  and caps capacity at 4096 (tests only); download-server URLs from Latin-1 OOB text percent-encode bytes, not the
  UTF-8 of the characters; the zlib deflate bytes of zpacket/KEX compression differ from Bun's zlib-ng); q3 DONE
  (gate-net3, 2026-11-11: all 35 TS files read in full, about 444 functions by a declaration count, 31 fixes; review
  test tests/network/test_network_gate_review_q3.c with oracles/network_gate_review_q3.ts, 27 checks including the
  entity and player netField tables against a bun dump, clean under ASan/UBSan/LSan. The worst: training the shared
  message Huffman tree kept about 28 MB of dead blocks for the process lifetime; gamestate baselines resolved a
  repeated number to the first entry (TS Map: the last), in encode and decode; lastResend truncated time() to int32
  (undefined at 2^31 and above, a gamestate resend was lost); LAN_AddServer/RemoveServer raised on an unresolvable
  address (TS refuses with -1) and getPing raised outside the 32 ping slots; getIpAuthorize text was cut at 1399
  bytes; SV_RateMsec and the download window lost a NaN rate through Math.min/max; validateEntities accepted
  fractional and NaN numbers; shownet printed an uninitialized record number; raise-path leaks in admission,
  server and client transmit, recording, client/server message encode. Also (allowed edits in app/bootstrap/network):
  unified_content.c canonical() -0 and TypeError; unified_client.c (2 sites) and unified_server.c (2 channel-failure
  sites and the transport rejection) raise Error. Open: other plain-Error throws in unified_client.c (80, 85, 95,
  179, 287, 367, 375, 441, 463) and unified_server.c (223, 228, 276, 278, 281, 286, 675, 838, 839; 877 is TS
  AggregateError) still raise RangeError (app/bootstrap/network reader; fixed by gate-appnet); configstrings set() snapshots each
  client's phase where TS reads it per iteration; Q3 wire text is Latin-1 source bytes in C, so UTF-8 cvar text
  (a name typed with non-ASCII) goes out as its UTF-8 bytes where TS would send Latin-1 or refuse units above 0xff).
- ui DONE but menu.ts data classes (gate-ui, 2026-11-11: 43 of 45 TS files; gate-legacy, 2026-11-11: legacy/runtime.ts
  in full, 172 declarations, and legacy/menu.ts UiMenuSourceParser, 56 of its 265 declarations; the 1-1452
  data-layout classes are covered by tests, not read line by line; 27 fixes in runtime, menu and team-arena memory,
  tests/ui/test_ui_gate_review_legacy.c with tests/ui/oracles/ui_gate_review_legacy.ts, 53 checks, clean under
  ASan/UBSan/LSan. The worst: runScript had no finally, so a menu command that threw (exec) left the script depth
  raised and after 64 failures every menu script stopped at the nesting limit; a menu parse that raised left its
  source handle allocated in the owner (no cursor dispose) and a MenuParseFailure longjmp skipped cleanup and
  qa_try frames; team-arena String_Alloc kept every stringDef handle until destroy (grew per String_Init).
  Open from gate-legacy: precomp tokens/defines and preprocessor frames are id-indexed arrays (8 bytes per id
  ever issued; a save naming a huge id fails with OOM where TS succeeds; capture walks id order, TS Map order,
  different only for a hand-made save); a menu load that raises leaks the parser and its partial definitions
  (the runtime may hold asset references published during the parse); engine_destroy without dispose frees owner
  allocations TS would leave (reached only by a create that raises after its frames).
  Earlier gate-ui record: 43 of 45 TS files read in full (about 560 declarations by a declaration count,
  59 fixes; rows in port_review.tsv; review test tests/ui/test_ui_gate_review.c with oracles/ui_gate_review.ts,
  33 checks, clean under ASan/UBSan/LSan). The worst: the settings bindings that share one state (audio device and format choices, input
  routing's three choices, the music track) shared one read buffer and choice/string pool, so building a settings
  page overwrote earlier controls' selected ids and choices and left them pointing at freed strings; the mods menu
  moved its ctx array mid-build (a long status left earlier controls on freed memory); the weapon wheel's drawState
  sorted the carousel into the wheel's own storage; the map-rotation and server-profile actions aborted on an invalid
  map name or a profile naming settings the source lacks (TS shows the message); the rerelease Q2 HUD leaked every
  string a layout command held whenever a server layout raised (each frame); a non-numeric maxclients configstring
  rejected every scoreboard client; plain TS Errors raised RangeError throughout. LEFT then (closed by gate-legacy, above): legacy/runtime.ts and
  legacy/menu.ts (C runtime.c 4707 lines, menu.c 2777) are not read (only pattern-scanned for casts); the
  content/legacy-script leftovers (lexer.c 1-1075 and its save state, memory.c, precomp_memory.c,
  source_memory.c, token_memory.c, the preprocessor macro table, global defines, builtin expansion, save state and
  async read) are not read. Open items: src/input/support.c qa_input_js_space(160) treats a lone 0xA0 byte as
  white space, so qa_input_js_trim strips the last byte of "à", "Р" and every UTF-8 character ending in 0xA0 (used
  by src/input, src/app/bootstrap image/render settings, shared_setting_cvars, startup_saves; the ui files now use
  qa_ui_js_trim); team-arena String_Alloc kept every stringDef handle until destroy (fixed by gate-legacy); the
  Q2 classic layout tokenizes lazily, so a malformed token after a bad stat reference reports the stat error where TS
  (tokenize first) reports the parse error.
- audio + materials + formats (gate-amf, 2026-11-11): formats DONE, read in full function by function and replayed
  against bun (tests/formats/test_formats_gate_review.c: 28 fixtures, each decoded unchanged and under 120 xorshift
  mutations, 3396 checks, clean under ASan/UBSan/LSan); 31 fixes, nearly all of them state leaked by a caught
  BinaryError (md2 and md4 had no guard at all and md4 freed uninitialized slots; a raise inside the q1 lump directory
  left uninitialized pointers for the map's destroy; q1/q2 BSP reads now release the map themselves), plus the
  PNG 32-bit zlib counters, Latin-1 text in error messages, q3-bmp's huge-size digits, GIF's bit>>>3 wrap, and the
  MD5 text grammar (Unicode \s, UTF-16 offsets, recursive scale JSON that overflowed the C stack, uncapped messages,
  quadratic normal accumulation). audio PARTLY: wav.ts and source-paint.ts in full and the mixer's mix path;
  WAV decoding replayed against bun (tests/audio/test_audio_gate_review.c, 2576 checks, clean under sanitizers);
  5 fixes (unsigned RIFF-bounds wrap in two decoders, FourCC JSON text, byte-rate wrap, a use after free in
  decodeQuakeWav's loop-end error, leaks on raise). materials PARTLY: color.ts in full, material.ts evaluation and
  name helpers, an error-class pass over all 20 files (44 TS `new Error` sites raised RangeError; shader script
  rejections raised RangeError not ShaderParseError); tests/materials/test_materials_gate_review.c. Open findings in
  the "Gate full read still to do" item below.
- audio + materials second packet (gate-amf2, 2026-11-11): audio DONE, the rest of mixer.ts, engine, bank, music,
  streams, output, reverb, environments and types read in full, reverb-presets against bun; tests/audio/
  test_audio_gate_review2.c replays 2550 generated mixer and UnifiedAudio script lines against bun (painted int16
  buffers hashed sample for sample with the clocks, voice events, reverb, underwater filter, environment selection,
  preset table), clean under ASan/UBSan/LSan; 18 fixes (NaN through Math.max in both spatializers and the environment
  probes, resetRound kept the free entity list, Vorbis failures aborted instead of raising so MusicPlayer could not
  stop a damaged track, voice-observer iteration, close try/finally, leaks on raise across mixer, engine, stream
  restore and parseEnvironments, Unicode material match). materials PARTLY: fog, legacy-fog, q2-lightgrid,
  q2-lighting, dlight, sky, turbulence, geometry, cinematic read in full, evaluate partly; the ShaderParser body,
  registration, material-finish (default profile), iterator and source state replayed against bun over all 74
  baseq3/missionpack scripts, 400 mutations and non-ASCII scripts (tests/materials/test_materials_gate_review2.c,
  780 checks, clean under sanitizers); 16 fixes, including the gate-amf open findings: shader and MD5 text decoded as
  TextDecoder does (src/formats/text_decoder.c, app/bootstrap/assets.c load_scripts and text_of), positions, token
  limit, tcMod length and shaderNameHash in UTF-16 units, Unicode image names, byteText entry for the turntable,
  nativeAtof's byte-character RangeError; plus prepareFogVolume (side order, file-sized buffer, leak, NaN), NaN
  through Math.max in Q1 fog, Q2 light grid, Q2 dynamic lights and sky bounds, 0xA0 in worldspawn fog, dlight leaks.
  LEFT and open items in the "Gate full read still to do" item below.
- leftovers packet (gate-misc3, 2026-11-11): materials DONE (deform, legacy, lighting, q3-lighting, evaluate, compile,
  material-finish with every profile and implicit input, material-iterator (both C copies), source-state and the
  ShaderParser body read in full); common_parse fixed at the source (text-mode Tokenizer counting UTF-16 units, the
  materials workaround removed, callers classified: q1 wheel text, network/configstring/entity callers bytes); decoded
  U+0000 kept as C0 80 (shader and MD5 text); script shader names meet BSP/model byte names as code units in the render
  registry; binary reader names interned (no leak on any BinaryError); stopChannel past int64; ov_clear failure raised;
  console re-read in full against v1.2.7 (dedicated by gate-misc2); camera/application re-read (spline spot-checked);
  llm partly (errors, request, responses, session, codex account id, models string/efforts). About 40 fixes, rows in
  port_review.tsv. Review tests tests/materials/test_materials_gate_review2.c (908 checks: NUL mutations, Latin-1 source
  programs, four extra finish profiles, implicit kinds, surface flags, deforms, Q1/Q2 lightmaps, light grid and entity
  lighting, legacy animation and sky) and tests/core/test_core_gate_review2.c (36 checks), both against bun oracles
  and clean under ASan/UBSan/LSan (LSan found three more raise leaks, fixed: an incomplete animation sequence, the
  Q1/Q2 lightmap accumulator on a missing style, implicitShaderInput's storage on a bad lightmap index).
  LEFT: llm api, auth (outside its JSON parser), auth-worker, settings, models parse/discover, request fetch path;
  camera/spline line by line (both DONE by gate-misc4, next entry). Open items under gate-misc3 below.
- leftovers packet 2 (gate-misc4, 2026-11-11, build/agents/gate-misc4): llm DONE (api, auth, auth-worker, settings,
  models parse/discover and the request fetch path read in full, 72 functions, 14 fixes; the worst: auth_worker.c
  aborted the process on invalid worker data, the libcurl fetch resolved on an interim 100 Continue as HTTP 100,
  refreshModels kept the new list after a failed preference write, otherService's URL parse was a small subset of new
  URL, settings files were read as raw bytes and JSON "\u0000" cut the C string); camera/spline DONE (read line by
  line: Unicode white space in the tokenizer and number trim). Cleanup items DONE: text/atlas qa_text_codepoints reads
  UTF-8 as TextDecoder; input/mouse_settings documentation in static storage; rerelease argv/args write UTF-8 of
  console text and bot_rerelease's chat goes through qa_app_chat_console_text; mod_presentations' failed script read
  keeps its class and whole message; campaign-cinematic saves each audio block in its producer's key order
  (qa_cinematic_audio.key_order); loading services the window at least once; foundation localeCompare uses weights
  generated under bun (tools/js_collation_tables.js) for U+0000-U+052F, U+1E00-U+1EFF, U+2000-U+20CF, U+2100-U+218F,
  U+3000-U+30FF, U+FF00-U+FFEF with expansions and three levels (0 differences from bun over the replayed pairs in
  those ranges; outside them code points sort after the table by code point, combining marks per code point). Rows
  "(gate-misc4)" in port_review.tsv. Review test tests/misc/test_misc_gate_review4.c with
  tests/misc/oracles/misc_gate_review4.ts. text/truetype: not re-read by gate-misc4 (gate-misc read it in full, 45
  functions, gate-misc2 its scratch; the d8c/d9a drift points were checked present). Open items under gate-misc4
  below.
- rest of core, input, media, platform, text, persistence, contracts. core + foundation + contracts DONE, persistence and
  settings PARTLY (gate-core, 2026-11-11, build/agents/gate-core): foundation read in full and js.c/node_path.c replayed
  against bun over 60121 cases (equal); core cvars/index (52 functions) and setting-declarations read in full;
  persistence and settings files as listed in the "Gate full read still to do" item; about 60 fixes (rows in
  port_review.tsv). Review test tests/core/test_core_gate_review.c (60167 checks) with
  tests/foundation/oracles/foundation_gate_review.ts and tests/core/oracles/core_gate_review.ts. The worst: a saved bot
  nextPointer near 2^32 sized three tables by it (tens of GB, abort; libvars now keeps sparse maps, as TS's Maps); a
  crafted save with a bad provider owner/version, and a source-JSON number given to BigInt, aborted the process (TS
  throws); readers leaked their partial state on every SaveFormatError (cvar restore, save image, recipe mods, guest
  checkpoints, mod sessions, random/ordering lists), and guest layouts, callback parameters and mod sessions leaked on
  every successful load; a settings document starting with a byte-order mark was quarantined to .bad (Bun's
  file.text() drops the mark); JSON "\u0000" and lone surrogates did not round-trip; a long caught message was lost
  when a catch's cleanup raised before the rethrow. Settled: qa_caught_message keeps eight owner-keyed whole messages,
  so every qa_rethrow keeps the long text (sites that wrap a caught message in a new class still read caught.message,
  2047 bytes: camera/application.c:188, ui/hud/q2_native.c:495, app/bootstrap/network/q3_types.c:16,
  startup_application_frontend.c:768, render/worker_runtime.c:39,109, render/worker.c:86, application_saves.c:49,
  simulation/rerelease_weapon_behavior.c:117, simulation/q3/native_guest_source.c:1035,1050,
  application_commands.c:1152, q3_client.c:1333, network/transport.c:86,87, simulation/bot_rerelease.c:316,
  q3_client/qvm.c:1287, and the prints at core/commands/index.c:2464,2536); NUL in save strings: qa_save_value strings
  and keys carry their length (re-encoded exactly, a key with a NUL never matches a field, qa_save_reader_string_length),
  but C-string consumers still see the text before the NUL (world profile classname, weapon declaration text, item icon
  lump check, recipe resourcePath's NUL refusal: for those readers' owners); cvar transfer:
  startup_application_frontend.c:434 now frees the qa_cvar_transfer (remote seat/profile/config free theirs through the
  arena); the cvar alias declaration raises are leak-free. Results as run (2026-11-11, gate-core tree): every binary
  under tests/foundation, core, persistence, contracts, settings (42) and tests/bots test_library_core 1767/0,
  test_bots_gate_review_library 90/0 pass; test_shared_simulation_save 101/0, test_app_concrete_original_save 9/0,
  test_q2_original_saves 109/0, test_configuration 133/0, test_d9b_drift 47/0, test_sim_verify 10 cases 0 mismatches,
  test_remote_loopback all nine families 0 differing (54/0); test_core_gate_review, test_save_mutation (the 34 KB
  recipe.c leak left by review-c is gone), test_persistence_recipe and test_cvar_transfer clean under ASan/UBSan/LSan.
  gate-core2 (2026-11-11, build/agents/gate-core2): persistence and settings PARTLY → most of what gate-core left. Read in
  full: q1-foundation, q2-foundation, q2-movers, q2-items, q2-hand-grenades, q2-missionpacks, q2-classic, q2-classic-guest,
  q2-rerelease, q2-typescript, q2-save-folder, q2-save-store, q2-selection, q1-selection, qvm-grapple, protection,
  execution's module/api/abi readers; source-items (definition, icon, held weapon, readSourceItems); settings restart,
  server/profile, library, q1, types; every server setting collection dumped against bun (equal). Checked by a
  differential replay rather than line by line: q2-base-entities, q2-monsters, q2-players, q2-weapons, q2-rerelease-state
  (tests/persistence/test_persistence_gate_review.c: 25 records from TS runs on e1m1, base1, rerelease base1 and rmine1
  plus synthetic ones, each decoded unchanged and under ~5000 splices, including every object's members broken one by one
  to fix TS's read order field by field; same error text or same decoded value, captured records re-encode to their own
  bytes). About 70 fixes (port_review.tsv rows "gate-core2"). Settled gate-core's open items: reader paths of any length
  (node chain owned by the value tree); JSON parser, unpack, pack and free iterative, JSON.parse unlimited, unpack's
  RangeError at QA_SAVE_VALUE_MAX_DEPTH (17500, the lowest depth bun was measured to fail at; bun's own limit moves
  with the JS stack); multi-member gzip (Node's rule: members until zero padding); sourceItemNamed lowers Unicode;
  byte strings vs settings text decided per site (classic configstrings are byte strings: converted at the JSON
  boundary; q1 selection lowers Latin-1; profile/slot-name lengths count UTF-16 units of UTF-8); the ~20 wrapped
  caught.message sites read qa_caught_message (qa_caught_copy keeps a copied first failure whole). Not settled: saved
  cvar flags/changedFlags (TS keeps the restored safe integer >= 0 and writes it back unchanged until a flag operation
  makes it int32; C wraps to int32 at restore, so a crafted save with flags >= 2^31 re-saves different bytes: core
  cvars/index.c stores flags as int32_t at 41 sites); a native recipe imageOffset above 2^63 (recipe.c, int64 field).
  Results as run (gate-core2 tree): every binary under tests/persistence (14), tests/settings (7), tests/foundation (9),
  tests/core (11) and tests/contracts (3) passes; test_persistence_gate_review 5293/0 and test_settings_gate_review 93/0,
  both clean under ASan/UBSan/LSan; test_shared_simulation_save 101/0, test_app_concrete_original_save 9/0,
  test_q2_checkpoint_bytes 10/0, test_q2_original_saves import/classic/level-change/sidecar/engine-logic ok and rerelease
  31/0 (7 min: renderer resource retirement removes textures and journal entries one memmove at a time, the same in
  gate-core's binary, 6 min; render's), test_q2_rerelease_pak 150/0, test_configuration 133/0, test_d9b_drift 47/0,
  test_native_mod_host 70/0, test_sim_verify 10 cases 0 mismatches, test_remote_loopback all nine families 0 differing
  (54/0). Other areas, for their owners: world/collision/q3/topology.c:78,110,112 pass reader.path (the first 255 bytes
  of a longer path) to qa_save_format_error (qa_save_reader_path gives the whole text); the classic configstring
  byte-string boundary also applies to compat/qvm/mod_provider.c:793, compat/q2/native_mod_provider.c:466,
  app/bootstrap/simulation/native_q2_rerelease_save.c:86 and rerelease_weapon_checkpoint.c:52, which read configstring
  values from checkpoint JSON as raw text; network/transport.c:86-87 (platform ipx error, 320 bytes) and the render
  worker transport (qa_rwt_failure.message, 1024 bytes) still cut long messages (render worker: whole since tests-b6).
  gate-persist3 (2026-11-11, build/agents/gate-persist3): persistence PARTLY → all but save_image.c's builders, settings
  DONE. Read in full against v1.2.7: q2-base-entities, q2-monsters, q2-players, q2-weapons, the rest of q2-rerelease-state,
  world-state, source-items' prepareSourceItemRestore, the rest of execution.ts (readStorage, readValueLayout,
  readCallbackReference, readCallback, readPrivate, readRegionPermissions, readNativeHeap); settings layers, native-config
  (with the KEX key/alias/command tables), server/lmctf-cvars, the rest of server/q2-owner. Rows "gate-persist3" in
  port_review.tsv. Fixes: world-state finish never reached "failed" (TS try/catch) and leaked on raises, restore leaked
  its scratch, its protection/source-item reads and the deferred completion; q2-weapons' legacy kickTime read its two
  fields in unspecified order; source-items duplicate checks and lookups are hash tables (were quadratic); the cleanup
  items: saved cvar flags/changedFlags kept as the restored numbers (bit operations through ToInt32; qa_cvar_read
  gains flags_value), imageOffset unsigned 64-bit with the whole-bigint range check (qa_save_reader_bigint_u64,
  qa_save_value_bigint_u64), save_policy.c's commitContained raises each failing step's Node error (Bun's recursive
  mkdir rule), controller-settings reports saveGyro's own error; settings: layer and native-config writes raise
  writeAtomic's Node error (were "Cannot write <path>"), native-config reads raise Node's open/read errors (were
  "<strerror>: <path>"), q2-owner's mapList splits on U+00A0, and leaks on those raises (layers resolve, absorbSession,
  readScript, merge, recordWritten, q2-owner restore). Review test tests/persistence/test_persistence_gate_review3.c with
  tests/persistence/oracles/persistence_gate_review3.ts (41 checks: the kickTime order,
  the unsigned bigint range and round trip, restored/fullSet cvar flags through capture, mark, addFlags and take, the
  NBSP map list, a refused q2-owner restore, save-policy's mkdir/rename errors and a committed write, a layer write
  into a file; 41/0, clean under ASan/UBSan/LSan). Results as run (gate-persist3 tree; linked from a private archive
  because llm/auth_worker.c was mid-edit by another agent): every binary under tests/core (10), tests/persistence (15)
  and tests/settings (7) 0 failures (test_persistence_gate_review 5293/0, test_settings_layers 288/0,
  test_native_config 99/0), test_shared_simulation_save 101/0, test_app_concrete_original_save 9/0, test_d2_drift 96/0,
  test_q2_rerelease_pak 150/0, test_sim_verify 10 cases 0 mismatches, test_remote_loopback 54/0 (0 differing),
  test_application_open_real 12/0, test_q2_original_saves 109/0.
  Outside the scope (edited, small): include/qa/contracts/weapon_behavior.h (image_offset uint64_t),
  src/world/gameplay/weapon_behaviors.c:317 (whole-bigint comparison), src/app/bootstrap/controller_settings.c (save).
  Found outside the scope (not edited): content/q3/base/game/save_module_values.c:32 wraps a restored module cvar's flags
  with ToInt32 on read and :16 captures the int32 snapshot flags (TS keeps the number; use flags_value);
  src/platform/files/contained.c:113 builds error paths in a 4096-byte buffer.
  Kept / open: the Q2 player name is char[96] in the checkpoint and live state (a crafted save's longer name is cut);
  execution.ts bigint fields are int64_t (saturate outside int64 where TS keeps any bigint); native-config reads a
  file up to its first NUL; qa_layer_map lookups are linear (TS Map). Not reached by the test: world-state's failed
  state and leaks, source-items (covered by the existing replay), native-config's error texts, the gyro save.
  LEFT: save_image.c's builders (image_value and its helpers) line by line [DONE by gate-clean1].
  gate-clean1 (2026-11-11, build/agents/gate-clean1): cleanup packet DONE but two items (below). save_image.c's
  builders read line by line (image_value and every *_value helper, about 70) against each key order TS holds: the
  readers (recipe, execution, shared, mods, qvm-grapple, save-image) and the producers (catalog launch/equipment,
  startup-selection, mounts #open, weapon-behavior-selection, applyApplicationMods, the runtime checkpoint,
  Q3NativeGuestSource, QvmModule). Fixes: a resolved prefix-order resolution wrote rank before prefix; a read
  recipe's quakec/qvm/native execution modules used content.ts order (readExecution: kind, owner, artifact, profile?,
  role, api; keyed on the artifact's parsed_order); a launched recipe's grapple and hand grenades used readGrapple's
  order; a q3-qvm grapple raised "not yet ported" (now written in readQvmGrappleProfile or readQvmGrappleDefinition
  order); a selected weapon behavior wrote component before definition; mods/weaponBehaviors were dropped when empty
  and always first (qa_executable_recipe.mods_key/weapon_behaviors_key: readRecipe keeps a read empty list first,
  applyApplicationMods and selectApplicationWeaponBehavior append; ss_recipe_equal ignores the position); live
  mod-session guests lacked QvmModule/QuakeC order; a read native guest wrote kind/version first and the spread
  processor layout; recipe free leaked a read q3-qvm grapple profile's four arrays. Cleanup items: module cvar flags
  (flags_value both ways); Q2 player name owned (qa_q2_base_player_state_set_name; checkpoint name a string, the
  rerelease userinfo path too); execution.ts bigints outside int64 (TS reads any bigint: a read guest lists them in
  wide_bigints and a re-encode writes the saved decimal; the runtime sees the saturated int64, where no mapping can
  exist); native-config past a NUL (U+0000 held as C0 80 in native_config.c and layers.c, \u0000 in layers JSON;
  console text ends at it); qa_layer_map hash index past 8 entries; QVM mod configstrings converted at the JSON
  boundary both ways (native_mod_provider.c:466 is rerelease UTF-8, left raw); topology errors via
  qa_save_reader_fail; IPX error text whole (src/platform/ipx_native.h, ipx.c); qa_hypot3 = qa_js_hypot3;
  condump/writeconfig rethrow writeAtomic's Node error (writeconfig's notify sees it first); q3_client/assets.c:499
  was already fixed by gate-app11. Review test tests/persistence/test_cleanup_gate_review1.c with
  tests/persistence/oracles/cleanup_gate_review1.ts (28 checks: a crafted image round trip byte-exact against TS
  covering every fixed reader order and the wide bigints, launched equipment, appended mod keys, cvar flags, hypot
  NaN/Infinity, a 100-entry layer map, a 200-character Q2 name through capture/codec/restore; 28/0, clean under
  ASan/UBSan/LSan). Not reached by the test: launched weapon behaviors and mod-session guests (no live mod in a unit
  test), native-config NUL and the layers \u0000 text, QVM configstrings, IPX and condump/writeconfig failures.
  Tests updated for the owned name: test_persistence_b, test_persistence_q2_players, test_q2_base, test_q2_rerelease,
  test_q2_multiplayer, test_sim_gate_review_a5; test_platform_gate_review's text buffer widened. Outside scope
  (edited, small): app/bootstrap/mod_selection.c, weapon_behavior_selection.c,
  simulation/shared_simulation_core.c (key positions), content/q2/rerelease/players.c (name). OPEN: the render
  worker transport's 1024-byte qa_rwt_failure/qa_rwt_error messages (about 100 by-value uses; needs an ownership
  pass) and the screenshot command's fixed message (capture/index.c returns false without TS's error).
  Results as run (gate-clean1 tree): every binary under tests/persistence (16), tests/settings (7), tests/core (10),
  tests/world (10), tests/platform (4), tests/console (3), the compat QVM/mod tests (test_compat_qvm_gate_review*,
  test_qvm_provider, test_qvm_provider_depth, test_qvm_mods_depth, test_q2_native_mod), test_network_common, the
  content Q2/Q3 tests touched (test_q2_base, test_q2_gate_review_base, test_q2_rerelease, test_q2_gate_review_rerelease
  1-3, test_q2_multiplayer, test_q3_base_ta) and test_sim_gate_review_a5: 0 failures (test_persistence_gate_review
  5293/0, test_settings_layers 288/0, test_native_config 99/0, test_world_collision 26271/0, test_compat_qvm_gate_review
  13216/0); test_sim_verify 10 cases 0 mismatches (and --diagnostics 10/0); test_q2_original_saves 109/0;
  test_shared_simulation_save 101/0; test_app_concrete_original_save 9/0; test_remote_loopback nine families 0
  differing (54/0); test_application_open_real q1/q2/q3 0 differing (12/0). tests/render not run (src/render untouched).
- input, text, media, platform, console, llm, camera, capture, debug PARTLY (gate-misc, 2026-11-11). input DONE: the
  14 files review-d left read in full (about 200 functions; seat, router, bindings, user-command, client-commands,
  midi, gamepad, haptics, source-joystick, source-input, weapon-bindings, mouse-settings, source-midi, sdl-keys), the
  XLateKey, key-name and source-key tables against a bun dump. text DONE: truetype.ts read in full (every font read is
  bounds-checked; refusal texts and classes equal TS); the other 14 files were review-a's plus the d8c/d9a drift.
  media PARTLY: roq.ts in full, cin.ts Huffman. platform PARTLY: the SDL2 audio open path. console: history confirmed
  against current TS (review-a read the rest). About 20 fixes, rows in port_review.tsv; review test
  tests/input/test_input_gate_review.c with tests/input/oracles/input_gate_review.ts (76 checks). The worst: client
  command owners were freed by publishSeats while the caller kept the handle (a later remove() read freed memory);
  weaponBindingItem read past the NUL of a short text; bind/bindlist cut lines at 512-2048 bytes; a hostile BNVIB file
  was cached as "no pattern" after its first error; RoQ motion blocks used memcpy over a possibly overlapping range.
  qa_input_js_trim (shared with ui and app/bootstrap callers) cut the trailing 0xA0 byte of UTF-8 characters such as
  U+00E0 (found by gate-ui; now by code point; qa_ui_js_trim in ui/common/controls.c is the same algorithm and can
  forward to it). llm/sse.ts: the stream's leading byte order mark was not stripped (Bun strips it only when the first
  chunk holds all of it; test_llm_review test_sse_bom).
  media and platform DONE (gate-misc2, 2026-11-11): every file gate-misc left read in full (about 500 functions by a
  declaration count; rows in port_review.tsv), about 50 fixes. Review tests tests/media/test_media_gate_review.c
  (62 checks; oracle tests/media/oracles/media_gate_review.ts: CIN frames/palettes/audio over eou4_/eou1_.cin and 21
  cut/mutated copies, RoQ frames/audio over Team Arena ammo_regen/crusaders and baseq3 idlogo plus 15 cut/mutated
  copies, the first six 1920x1080 Theora frames of the rerelease end.ogv and cut/CRC-damaged copies, node:fs errors,
  checkpoint refusals, dimension probes, the image reentry flag) and tests/platform/test_platform_gate_review.c (64
  checks; oracle tests/platform/oracles/platform_gate_review.ts: SDL text decoding, native library candidates and
  error classes, render-context and IPX refusals, 42 damaged-glyf fonts, deep JSON), both clean under
  ASan/UBSan/LSan. The worst: every malformed movie leaked (and the CIN/ogv input stayed open) because no
  constructor released what it built when a decode raised; CinematicImage.resolve stayed "executing" after a
  throwing apply, so every later resolve failed; SdlWindow.open leaked the native window, context and subsystem on a
  failure the renderer restart recovers from; sdl, sdl-render-context and native-libraries raised RangeError where
  TS throws Error/AggregateError/TypeError; RoqPlayback.reset left a destroyed decoder when the new one threw;
  FreeType metrics skipped TS's int32 RangeError. text/truetype scratch and the llm JSON depth (gate-misc open
  items) fixed. capture, debug and console/dedicated re-read in full against v1.2.7 (2 fixes). LEFT: console (all
  but dedicated and history), llm (all but sse and the JSON parser), camera: only their v1.2.7 drift is confirmed in
  C; the function-by-function re-read was not done. Open items under gate-misc and gate-misc2 below.
- app/bootstrap: simulation (several packets), application, network, remote application,
  startup, the rest.
- app/bootstrap/simulation packet A PARTLY (gate-simA, 2026-11-11, build/agents/gate-simA). Read in full: save.ts,
  dropped-pickups.ts, equipment-checkpoint.ts, weapon-slot-checkpoint.ts, grapple-checkpoint.ts, foreign-match-rules.ts,
  foreign-horde-rules.ts, diagnostics.ts. runtime.ts read on the step/checkpoint/restore/close/load path (31 members:
  stepAsync, planStep and the four boundary phases, step, finishFrameState, concludeSourceFrame, snapshot, the
  selected-QVM frame and actors, the native Q2 step and nativeQ2Frames, the checkpoint guards, checkpoint,
  checkpointModsForTravel, checkpointLoading, checkpointFromSource, restore, close, load, diagnostics, orderedActorTurns);
  players.ts on the NetQuake input path and every throw site. About 25 fixes (rows in port_review.tsv), review test
  tests/app/test_sim_gate_review_a.c with tests/app/oracles/sim_gate_review_a.ts. The worst: checkpointLoading had no
  finally, so a raise in the mod, weapon-behaviour or guest capture left checkpointInProgress set and every later step,
  save and close refused; the step's fadeIntermission and three gravityMultiplier try/finally sites left their state
  changed after a raise; save.ts raised RangeError for every plain Error and skipped TS checks (rerelease native decode,
  native-execution checks done by schema presence, savedSimulationSettings never validated a Q3 guest checkpoint and
  read its client slots from the empty players list, saved bots not decoded and matched any player of the slot);
  prepareNetQuake never opened the applied-movement slice when input applications are active and had no catch;
  finishNetQuakeInput did not aggregate its errors. LEFT in packet A: runtime.ts outside that path (constructor,
  createSource, worlds/monsters, arsenal, equipment, grapple, presentation, travel, original save; C
  shared_simulation_{core,source,worlds,arsenal,players,access,mods}.c), source-strategy.ts (228 members, C
  source_strategy.c), player-checkpoint.ts, players.ts outside the NetQuake path, foreign-match-host.ts and the tag,
  lmctf and deathball rules, checkpoint_values.c, types.ts, source-hosts.ts, random.ts, provider-frames.ts and the
  other small files beside runtime.ts. Open: C player services fix inputApplication at creation (owner exists) where
  TS reads `active` per call; the native-Q2 hand-grenade player copy and several restore readers leak on a raise;
  orderedActorTurns' visited set is a linear scan (quadratic per frame in actor count); last_attack stores the damage
  request's borrowed attacker pointer. Outside the scope, seen: render/image_journal.c:84 and render/gl/textures.c:85
  release by linear search plus memmove (a world retirement in test_q2_original_saves spends minutes there, RSS near
  5 GB). Results as run (gate-simA tree): test_sim_verify 10 cases 0 mismatches and --diagnostics 10 cases 0 failed;
  test_sim_a 38/0, test_sim_b 38/0, test_sim_c 333 passed, test_sim_concrete 108/0, test_shared_simulation_save 101/0,
  test_shared_simulation_core/b1/b2/b3 ok, test_shared_simulation_rest 67 passed, test_app_concrete_original_save 9/0,
  test_q2_original_saves 109/0, test_d2_drift 96/0, test_application_open_real q1/q2/q3 0 differing (12/0),
  test_remote_loopback nine families 0 differing (54/0), test_sim_gate_review_a 13/0 and clean under ASan/UBSan/LSan.
- app/bootstrap/simulation packet A, second reader PARTLY (gate-simA2, 2026-11-11, build/agents/gate-simA2). Read in full:
  player-checkpoint.ts, foreign-match-host.ts, foreign-tag-rules.ts, foreign-lmctf-rules.ts, foreign-deathball-rules.ts,
  random.ts, provider-frames.ts, source-hosts.ts, quakec-map.ts, native-q2-map.ts, q1-punch.ts; players.ts outside the
  NetQuake path (MovementPlayer: constructor, applied movement, readState, commit, acceptArsenalIntent, receiveNetQuake,
  clientMovementOutputs, fixedEnvironment, move, withInputCommand, moveCommand, applyQ3Contacts); runtime.ts: the
  constructor's validation, foreignMatchHost, lastAttack and the damage reaction hooks; source-strategy.ts: LoadingStrategy
  (every default), every teamGame, Q1 restoreFoundation and QuakeC concludeFrame/afterPlayerCommand, Q1/Q2 admitCarry and
  enterWorld. Every literal throw in runtime.ts, source-strategy.ts, players.ts, player-checkpoint.ts and the 43 small files
  beside them matched to its C raise by message and class (script). gate-simA's open items settled: inputApplication is read
  per call (players.c host getter; slices pair their push/pop as TS sliceApplications; the parent is the innermost
  client-command application and a null one is no parent); lastAttack and the Q2 entity.lastAttack keep boxed copies of the
  attacker/inflictor; the native Q2 hand-grenade player copy and the restore readers (foreign worlds, grapple, weapon slots,
  hand grenades, Q1 foundations, legacy Q3 source, movement player ammo and player) free on a raise; the actor-turn visited
  set is still a linear scan (runtime.c in_visited, quadratic per frame): left for optimization. About 45 fixes (rows in
  port_review.tsv, gate-simA2). The worst: ck_fail raised RangeError for 27 checkpoint/restore Errors and used its text as a
  format; restored Q3 jumpPads all pointed at one static id; acceptArsenalIntent left the impulse undefined where TS defines
  it (an impulse carried from a pending intent was ignored by the arsenal); clientMovementOutputs and fixedEnvironment kept
  their outputs in static storage shared by every player and compared bounds with memcmp; netQuakeInput and moveCommand
  built the environment before fixedEnvironment; fixed caps where TS has none: Q2 nearby() 256, Q1 punch 64, QuakeC map 64
  starts and 256 fields, the source inventory binding 128 rows, entity origin parts 128 bytes; the rerelease random stream
  took ToUint32 of a bad seed; native-q2-map wrote a field-less entity without its blank line. Review test
  tests/app/test_sim_gate_review_a2.c (oracle tests/app/oracles/sim_gate_review_a2.ts). LEFT: runtime.ts outside the paths
  above (~200 private members; C shared_simulation_{core,source,worlds,arsenal,players,access,mods}.c); source-strategy.ts
  per-class bodies of Q1, QuakeC, Q2, Q2Native, Q3 and Q3Qvm beyond the members listed; checkpoint_values.c (key orders
  checked only through test_sim_verify); types.c (C-only event deep copy); the small files beside runtime.ts other than the
  ones above (actor-execution, mod-callbacks, native-mod*, qvm-mod, quakec-mod bodies, q3-commands, weapon-slot*,
  powerup-timers, new-toss, q1-fog, player-jump, source-transition, quakeworld-cvars, native-commands, native-q1-pusher,
  native-pmove-contacts, q2-rerelease-slide, shared-quakeworld-movement, q2-native-world). Open: netQuakeInput ignores
  netQuakeApplication.command when no slice opened; ss_player_host and the player slice record cap nesting at 16 (C-only);
  the player UI storage ring has fixed label (64 bytes), powerup (32) and inventory (128) rows, and arsenal intents a ring
  of 16; Q1 say truncation counts bytes (TS UTF-16 units; equal only for one-byte text); random draws are int64 (TS any
  number). Outside the scope, seen: movement/q1/quakeworld.c step and movement/q3/provider.c have no catch ending an open
  input slice (TS end(state, true)) and quakeworld.c raises RangeError for "Input output changed command dialect" (TS
  Error); plain TS Errors raised as RangeError in weapon_slot.c (14 sites), player_movement.c (3), quakec_local_messages.c
  (6), q2_rerelease_slide.c (2), shared_quakeworld_movement.c (1), q2_native_world.c (1); runtime.c (the older concrete
  host application.c and main.c's dedicated path use) aborts at 51 qa_error sites where TS throws.
  Results as run (gate-simA2 tree, final sources): test_sim_verify 10 cases 0 mismatches and --diagnostics 10 cases 0
  failed; test_sim_a 38/0, test_sim_b 38/0, test_sim_c 333 passed, test_sim_concrete 108/0, test_sim_equipment 376/0,
  test_sim_monsters 248/0, test_shared_simulation_save 101/0, test_shared_simulation_core/b1/b2/b3 ok,
  test_shared_simulation_rest 67 passed, test_app_concrete_original_save 9/0, test_q2_original_saves 109/0, test_d2_drift
  96/0, test_application_open_real q1/q2/q3 0 differing (12/0), test_remote_loopback 0 differing (54/0),
  test_sim_gate_review_a 13/0, test_sim_gate_review_a2 21/0 and clean under ASan/UBSan/LSan.
- app/bootstrap/simulation packet A, third reader PARTLY (gate-simA3, 2026-11-11, build/agents/gate-simA3). Read in full:
  weapon-slot.ts, weapon-slot-projection.ts, q1-fog.ts, actor-execution.ts, mod-callbacks.ts, native-mod-camera.ts,
  q3-commands.ts, native-commands.ts, powerup-timers.ts, player-jump.ts, source-transition.ts, quakeworld-cvars.ts,
  shared-quakeworld-movement.ts, q2-native-world.ts; quakec-mod.ts and qvm-mod.ts bodies; native-mod-host.ts setup,
  command scope, clock and import boundary; source-strategy.ts Q1Strategy and QuakeCStrategy member by member, the
  other four classes by a scan of every find/Math/??/slice site plus fov, userinfo and powerup members; runtime.ts:
  sourcePlayerName, toggleQ1Pause, queueQuakeWorldCommands/Action, runQuakeWorldNewMissile, clientIdentities,
  prepareArsenalCommand, primaryUi/guestUi/arsenalUi, slotProjection and every toLowerCase site; checkpoint_values.c
  key orders compared with bun for q3SpawnArsenalRuntime, q3SpawnLoadout, newQ1Travel and the WeaponSlotState literals
  (equal). About 35 fixes (rows "gate-simA3" in port_review.tsv); review test tests/app/test_sim_gate_review_a3.c with
  tests/app/oracles/sim_gate_review_a3.ts (19 checks). The worst: the grapple-slot weapon status returned by the slot
  projection pointed into the projection call's dead stack frame; weapon-slot compared states by value where TS
  compares identity, reused freed binding slots out of Map order, and a throwing request cancel left the activation
  set (no finally); a saved per-player Q1 fog wrote its player key last (save bytes differ from TS) and a 65th fog
  actor raised; Q3 uiPowerups called a NULL callback for a missing followed client; the shared QW movement copied
  viewHeight once (TS getter); QW and Q3 movement never ended an open input slice on a raise. gate-simA2's open items:
  netQuakeInput now reads the open application's command; the player slice record and ss_player_host's application
  stack grow (no 16 cap); arsenal intents are caller storage (no ring); Q1 say counts UTF-16 units; the Q3 guest UI
  inventory grows to the source inventory (lead's item, the 128-row cap); random draws stay int64 (settled: every
  reader admits safe integers only and counts start at 0); the remaining UI rows (weapon labels 64 bytes, 32
  powerups, 96 items, the 4-record ring) are bounded by the fixed game tables, not by input. movement/q1/quakeworld.c
  (catch ending the slice; five Error classes), movement/q3/provider.c (catch; three Error, two TypeError; effects
  realloc), movement/q1/common.c and types.h (view_height_ref) edited as allowed. The RangeError sites listed for
  weapon_slot.c, player_movement.c, quakec_local_messages.c, q2_rerelease_slide.c, shared_quakeworld_movement.c and
  q2_native_world.c were already fixed by gate-simC (confirmed; only "Out of memory" remains). LEFT: runtime.ts ~185
  private members not read line by line; source-strategy.ts Q2, Q2Native, Q3, Q3Qvm bodies beyond the scan;
  native-mod-host.ts classic/rerelease closures; native-mod-presentation.ts (outside the HUD stats); the other
  checkpoint_values.c builders (only through test_sim_verify); types.c. Open: frame-loop visited sets and ordered-turn
  iterators in shared_simulation_{step,worlds,arsenal}.c leak on a raise; ASCII-only lowercasing elsewhere differs
  from toLowerCase only for U+212A (fixed for the weapon command text); q2-native-world's module revision is formatted
  into a 1024-byte buffer by its callers (classic/rerelease guest sources, shared_simulation_source.c, save.c);
  qa_app_sim_prepare_mounted_quakec_mod and the host-form qa_app_sim_prepare_q2_guest have no callers. Outside the
  scope, seen: content/q2/rerelease/world_text.c qa_q2_world_text takes UTF-8 bytes as units, so a world text above
  U+007F becomes Latin-1 bytes (TS keeps the characters &255 as a string). The older simulation/runtime.c is still
  used by application.c (create/load/as_simulation/step), main.c (qa_app_sim_shared_simulation_concrete_host),
  tests/app/test_sim_c.c and test_sim_concrete.c, and the SharedSimulation itself links its ordered actor turns
  and frame plan from it (qa_app_sim_ordered_actor_turns_*, qa_app_sim_frame_plan_compute): move those before deleting it.
  Results as run (gate-simA3 tree, final sources): test_sim_verify 10 cases 0 mismatches and --diagnostics 10 cases 0
  failed; test_sim_a 38/0, test_sim_b 38/0, test_sim_c 333 passed, test_sim_concrete 108/0, test_sim_equipment 376/0,
  test_sim_monsters 248/0, test_shared_simulation_save 101/0, test_shared_simulation_core/b1/b2/b3 ok,
  test_shared_simulation_rest 67 passed, test_app_concrete_original_save 9/0, test_q2_original_saves 109/0, test_d2_drift
  96/0, every tests/movement binary (7) pass, test_application_open_real q1/q2/q3 0 differing (12/0), test_remote_loopback
  nine families 0 differing (54/0), test_sim_gate_review_a 13/0, test_sim_gate_review_a2 21/0, test_sim_gate_review_a3
  19/0 and clean under ASan/UBSan/LSan (detect_stack_use_after_return on).
- app/bootstrap/simulation packet A, fourth reader PARTLY (gate-simA4, 2026-11-11, build/agents/gate-simA4). Read in full:
  source-strategy.ts Q2Strategy, Q2NativeStrategy, Q3Strategy and Q3QvmStrategy member by member (with the qa_ss_*
  members they install); native-mod-host.ts classic and rerelease closures; native-mod-presentation.ts; types.c (every
  pointer member of each SourcePresentationEvent payload is in the copy shapes, script over the headers); the remaining
  checkpoint_values.c builders against their TS literals (hand grenades, QVM grapple source, grapple runtime, foreign Q2
  world, Q3 selected arsenal, Q3 character, q2BuildView, the Q2 model event: equal). runtime.ts members read:
  orderedActorTurns, selectedWeaponCommand, sourceItemCommand, giveSelectedItem, playerCommand, captureTravel,
  sourceRestartPlan/restartSourceRound/settlement, presentationFraction, interpolatedPose, q1VisualFields, killBox,
  setWorldGravity, disconnectPlayer, sourcePlayerView, and every TS finally site. About 30 fixes (rows "gate-simA4" in
  port_review.tsv); review test tests/app/test_sim_gate_review_a4.c with tests/app/oracles/sim_gate_review_a4.ts. The
  worst: Q2 monstersSearching(null) was inverted (rerelease combat state); Q2 finishFrame's withQ2Rules and Q3
  playerThink had no finally; world text above U+007F left q2WorldText as raw Latin-1 bytes (now UTF-8, as the renderer
  and network codecs read it); QVM grapple/weapon-behaviour targets cut each userinfo at 511 bytes; command text,
  revision strings and every ss_fail/wl_fail/ck_fail message had 1024-byte buffers (now whole: qa_ss_raise_error);
  the native mod presentation named an unbound record by enum number; the selected ammo timer owner never observed
  actor release (now actors.onRelease as TS, unobserved by its close, coordinator item from gate-simC3). Coordinator items from gate-simC4: the q3-source execution records live as long as the actor's
  registered execution (freed at the next frame end after release); ss_q3_arsenal_runtime reads over the source
  product's spawn runtime, not the caller's output. Coordinator item from gate-simB3: a raising QuakeC source constructor or checkpoint
  restore no longer leaks the source and its release subscription (quakec_source.c create, edited and listed).
  Results as run (gate-simA4 tree, final sources): test_sim_verify 10 cases 0 mismatches and --diagnostics 10 cases 0
  failed; test_sim_a 38/0, test_sim_b 38/0, test_sim_c 333 passed, test_sim_concrete 108/0, test_sim_equipment 376/0,
  test_sim_monsters 248/0, test_shared_simulation_save 101/0, test_shared_simulation_core/b1/b2/b3 ok,
  test_shared_simulation_rest 67 passed, test_app_concrete_original_save 9/0, test_q2_original_saves 109/0, test_d2_drift
  96/0, every tests/movement binary (7) pass, the q2 rerelease content tests (test_q2_rerelease 72, rerelease_monsters
  134, gate_review_rerelease 24733/0, rerelease2 187, rerelease3 44/0) pass, test_application_open_real q1/q2/q3 0
  differing (12/0), test_remote_loopback nine families 0 differing (54/0), test_sim_gate_review_a 13/0, a2 21/0, a3 19/0,
  a4 9/0 and a4 clean under ASan/UBSan/LSan (detect_stack_use_after_return on). Task items
  settled: the frame-loop visited sets and turn cursors free on a raise (worlds runGuestMonsters, arsenal
  beginNativeEquipmentFrame; step.c already did); orderedActorTurns moved to shared_simulation_turns.c (TS module
  function beside the class; runtime.c links it, frame_plan_compute stays in runtime.c, the shared simulation plans in
  shared_simulation_plan.c); module revisions whole (q2_native_world.c, classic/rerelease guest sources,
  shared_simulation_source.c, save.c; callers in gate-simB2's guest sources edited minimally:
  classic_guest_source.c/.h and rerelease_guest_source.c revision fields); removed unused
  qa_app_sim_prepare_mounted_quakec_mod (+ its mounts struct) and the host-form qa_app_sim_prepare_q2_guest with
  qa_app_sim_prepared_native_primary (TS counterparts are the _content form and qa_app_shared_sim_native_primary);
  content/q2/rerelease/world_text.c fixed (allowed outside the packet; entities.c buffers 160 -> 256). Not reachable
  from a unit test: the Q2/Q3 strategy fixes, the turn-loop frees, the userinfo pool and the ammo release
  subscription (each needs a live source); test_sim_verify covers the sources it runs. LEFT: runtime.ts ~160 private
  members not read line by line (createSource and the per-source hosts, monster/foreign worlds, selected arsenals,
  native weapons/inventory, grapple/hand grenades, presentations, original save, restore). Outside the scope, seen:
  qa_q2_base_player_state.userinfo is a fixed array (Q2 storeUserinfo snprintf's into it; TS any length;
  content/q2/base); qa_source_item_named and ss_requested/ss_normalize-style ASCII lowering remain where only ASCII ids
  are compared.
- app/bootstrap/simulation packet A, fifth reader PARTLY (gate-simA5, 2026-11-11, build/agents/gate-simA5). runtime.ts
  read member by member (about 150 members, rows "gate-simA5" in port_review.tsv): createSourceHost's lambdas, the mod
  client services, the Q1 punch and view roll, monsterSourceFor, the foreign Q1/Q2 worlds (identities, creation,
  q1CompositionServices, spawn, capture/restore, frames), selected and guest monster admission (prepareSelectedMonsters
  with validatePlacement/placementReady, prepareGuestMonsters, admitGuestMonsters, capture/restoreSelectedMonsters,
  beginMonsterFrames, selectedCreaturePickups, createMonsterMovement), the Q3 supply/pickup members, the native and QVM
  weapon input/spawn/step members, createSelectedQ1/Q2/Q3Arsenal, prepareSelectedOriginalPickups, selectedQ3ClientChanged,
  the ammo timers and delays, q1ActorHost, q2ActorHost, the equipment, grapple and hand-grenade members (createGrapple,
  initializeQvmGrapple, createHandGrenades, stepHandGrenade, beginNativeEquipmentFrame, requireEquipmentPlayer,
  grappleAnchor, equipmentWeaponInput, the character animations, grappleSlotHost), the weapon behaviors, createSource (all
  six branches closure by closure), victimArmorContext, registerCombat, schedule and the actor-infrastructure members,
  createPlayer, the Q3 client admission/spawn members, the selected travel members, the presentation members
  (registerResource through primaryPresentations), the shared travel carries, restoreCampaignTravel, capture/restore
  OriginalSave, the native drop source/projection, the userinfo members and takeTransitions. 14 fixes: claimOutputs
  leaked its wrapper when the lease claim raised; advanceQ1Punch, beginNativeEquipmentFrame and initializeQvmGrapple
  leaked their player copies on a raise; the selected-creature checkpoint restore leaked the decoded value on a raise;
  both "Unimplemented authored ... spawns" reports cut their message at 2047 bytes and leaked the name list;
  restartSession (primary and foreign Q1) and QuakeC changeLevel cut `q1:${map}` at 507 bytes; the QVM pickup fallback
  `q3:${className}` was cut at 255 bytes; the ambiguous-item message at 2047 bytes; validatePlacement parsed authored Q2
  entities only on a Q2 world (TS for any Q2 map program); every Q2 entity registration kept a read-monster record until
  the simulation ended (actor_execution.h read_monster now takes the entry's actor; actor_execution.c and
  monster_admission.c callers updated); createNativeWeaponBehavior and the Q2 dmflags writer leaked cvar snapshots.
  Task item: Q2PlayerState userinfo and skin are owned strings (set_userinfo/set_skin; the Q2 strategy stores a
  component's userinfo whole), name grows to 96 bytes; userinfoChanged (base 511/15, rerelease 2047/31) and the Q2 say
  message (150) cut by UTF-16 units instead of bytes (qa_q2_text_slice_units); checkpoint captures copies, a decoded
  checkpoint borrows the save's strings; saves byte-identical with TS (a 3000-byte userinfo and skin). Sites:
  content/q2/base/player/{types,checkpoint,index,commands}.c and their headers, content/q2/rerelease/players.c,
  persistence/q2_players.c, simulation/source_strategy.c, tests/persistence/test_persistence_q2_players.c,
  tests/content/test_q2_multiplayer.c. Review test tests/app/test_sim_gate_review_a5.c with
  tests/app/oracles/sim_gate_review_a5.ts (14 checks). Not reachable from a unit test: the raise-path frees, the whole
  messages (each needs a live source), the placement parse and the userinfoChanged/say slicing (players need a game
  fixture; the slice helper itself is checked). LEFT (about 95 runtime.ts members, listed in the port_review.tsv row):
  admitPlayer and the per-family spawn/character attach members, weaponStep, setPlayerMovement, touch, jump, the
  animation steps, nativeInventoryRows, the mod client command members, the match/team members, the accessors,
  captureNativeQ2Travel, the native Q2 client/character/input members (bindNativeInput, tickNativeCharacters,
  syncNativeMovementCombat), characterViews, grantSelectedArsenal, giveNativeSelectedAmmo, selectionConsumption (C),
  bindSelectedOriginalPickups, nativeEquipmentDelay, the rerelease navigation members. Open: a selected arsenal's read
  is malloc'd, but about 15 sites assign it straight to player->arsenal (players.c 742, 755, 1053, 1544, 1756, 2212,
  2217, 2472, 3047; checkpoint.c 957, 2381, 2477; source.c 1112, 1350; worlds.c 1066; selectQ3SpawnWeapon) and leak it
  (only store_player_arsenal adopts it; routing them through it needs a check that no caller holds the previous array);
  per-call qa_app_shared_sim_keep growth in ss_handoff_begin (each primaryHandoff), ss_slot_presentation,
  ss_qvm_resolve_item (resource and interned fallback per QVM pickup), ss_q3_qvm_loadout (per QVM admission) and
  qa_app_shared_sim_shared_grapple. Outside the scope, seen: persistence/q2_players.c encodes a decoded player state in
  class order (helpChanged before landmarkFreeFall) where TS decode-then-encode keeps the reader order (only for a
  decoded checkpoint re-encoded without restore); bot_world.c:536, bot_rerelease.c:951/953 and network_q3.c:949 leak
  cvar register snapshots.
  Results as run (gate-simA5 tree, final sources): test_sim_verify 10 cases 0 mismatches and --diagnostics 10 cases 0
  failed; test_sim_a 38/0, test_sim_b 38/0, test_sim_c 333 passed, test_sim_concrete 108/0, test_sim_equipment 376/0,
  test_sim_monsters 248/0, test_sim_navigation 94/0, test_sim_bots 459/0, test_guest_bots_verify 92 lines 0 mismatches,
  test_shared_simulation_save 101/0, test_shared_simulation_core/b1/b2/b3 ok, test_shared_simulation_rest 67 passed,
  test_app_concrete_original_save 9/0, test_q2_original_saves 109/0, test_d2_drift 96/0, test_persistence_q2_players
  22/0, every tests/movement binary (7) and every tests/content test_q2_* binary (27) pass, test_sim_gate_review_a 13/0,
  a2 21/0, a3 19/0, a4 9/0, a5 14/0 and a5 clean under ASan/UBSan/LSan (detect_stack_use_after_return on),
  test_application_open_real q1/q2/q3 0 differing (12/0), test_remote_loopback nine families 0 differing (54/0).
- app/bootstrap/simulation packet A, sixth reader PARTLY (gate-simA6, 2026-11-11, build/agents/gate-simA6). runtime.ts
  members read (row "gate-simA6" in port_review.tsv): admitPlayer, admitTravel, prepareBotClient, spawnQ1Player, weaponStep,
  setPlayerMovement, touch, jump, nativeInventoryRows, useSelectedInventoryItem, characterViews, grantSelectedArsenal,
  giveNativeSelectedAmmo, bindNativeInput (to the drop binding), slotProjection and playerUi. Task items settled:
  (1) every selected arsenal read assigned straight to player->arsenal now goes through store_player_arsenal or the new
  assign_player_arsenal (players.c restoreSelectedTravel, quakeCClientSpawned, placeQ1Player, the QuakeC refresh and
  post-movement hook, admitPlayer, the Q2 travel admission, respawnPlayer, selectQ3SpawnWeapon; source.c weaponChanged and
  the Q2 respawn; worlds.c the foreign Q1 weaponChanged; checkpoint.c capture and both selected restores; restoreSelectedTravel
  also discarded two admit reads). The "no caller holds the previous array" check failed: a movement input carries
  player->arsenal and accept_result writes it back after a touch's weaponChanged refreshed the player, so freeing on
  replacement would be a use after free; adopted arrays now stay until the next step's start and are freed there once no
  player's arsenal references them (qa_app_shared_sim_release_call_storage at the three step entries) and with the
  simulation. moveQ3Client's weapon step leaked its input read every Q2-profile command; the step's weapon call,
  grantSelectedArsenal and the Q2 handoff resume leaked reads. (2) Keep-arena per-call growth: new scratch records (freed
  at the next step's start unless claimed) for the strategy primaryHandoff records (a weapon slot claims its handoff and
  frees it with the slot, with the slot's owner record: release_slot_bindings at slot replacement, actor release,
  checkpoint restore and destroy), the QVM pickup resource, the QVM death-drop ammo words and the QVM loadout ammo (admit
  keeps none of it); the grapple slot presentation context is one per actor; qa_app_shared_sim_intern keeps one copy per
  distinct text (every intern grew the arena). Other keep users are per construction or grow geometrically (source
  bindings, character contexts, match players, Q3 contact buffer, mod client application stacks), or per restore
  (checkpoint.c restore copies; restartSession's map name): left. sharedGrapple is per source services, as TS.
  (3) bot_world.c:536, bot_rerelease.c:951/953 and network_q3.c:949 already free their snapshots in this tree. (4)
  persistence/q2_players.c class-order encode of a decoded state is unreachable: the only decode
  (content/composition/q2/save.c) restores into the live players, and capture re-encodes from them in class order on both
  sides. Coordinator item: slotProjection's caps (8 sources, 32 weapons per source, 8 attachments) are gone (ring entries
  grow their buffers); the ring stays (SS_UI_RING nested reads; callers read and copy before the next projection), but
  primaryPresentations kept the gear model's attachment pointer into the ring, stale after four slot players: the
  attachments are now converted once per source array and live with the simulation. nativeInventoryRows cut a retained
  ammo icon path at 159 bytes (now whole). Review test tests/app/test_sim_gate_review_a6.c (15 checks of the C ownership
  contracts; no TS-observable value, so no bun oracle). Not reachable from a unit test: the per-site arsenal routing,
  the scratch users and the slot caps (each needs a live source); test_sim_verify covers the sources it runs. Outside the
  scope, seen: the player UI storage still caps rows at SS_UI_ITEMS (96) and the QuakeC UI cut silently at that count
  (shared_simulation_arsenal.c primaryUi); native inventory equipment rows at SS_Q3_INVENTORY (64, raises).
  LEFT (about 80 runtime.ts members, listed in the port_review.tsv row).
  Results as run (gate-simA6 tree): test_sim_verify 10 cases 0 mismatches and --diagnostics 10 cases 0 failed (final
  sources); test_sim_a 38/0, test_sim_b 38/0, test_sim_c 333 passed, test_sim_concrete 108/0, test_sim_equipment 376/0,
  test_sim_monsters 248/0, test_sim_navigation 94/0, test_sim_bots 459/0, test_guest_bots_verify 92 lines 0 mismatches,
  test_bot_fidelity 752/0, test_sim_network 176/0, test_sim_network_q3 81/0, test_shared_simulation_save 101/0,
  test_shared_simulation_core/b1/b2/b3 ok, test_shared_simulation_rest 67 passed, test_app_concrete_original_save 9/0,
  test_q2_original_saves 109/0, test_d2_drift 96/0, test_persistence_q2_players 22/0, every tests/movement binary (7) pass,
  test_sim_gate_review_a 13/0, a2 21/0, a3 19/0, a4 9/0, a5 14/0, a6 15/0 and a6 clean under ASan/UBSan/LSan
  (detect_stack_use_after_return on), test_remote_loopback every family 0 differing (54/0), test_application_open_real
  q1/q2/q3 0 differing (12/0). The module list ran before the last edit (grantSelectedArsenal's raise cleanup);
  test_sim_verify, --diagnostics, test_sim_c/concrete/equipment, the loopback and open_real ran on the final sources.
- app/bootstrap/simulation packet A, seventh reader (gate-simA7, 2026-11-11, build/agents/gate-simA7): runtime.ts's
  remaining ~80 members (gate-simA6's LEFT list) read member by member against v1.2.7 (row "gate-simA7" in
  port_review.tsv). With gate-simA to gate-simA7, every runtime.ts member has now been read once. Task items: the
  player UI caps are gone (SS_UI_ITEMS 96: the ring slots' rows, the selected arsenal UI items and labels, the guest UI
  rows, the per-class arsenalUi rows and the QuakeC primaryUi, which cut silently, now grow to their source; the
  selected catalog's 64 cap went with them, sized by the selected weapons through the new
  qa_app_q2_selected_arsenal_weapon_count; selectedWeaponCommand/sourceItemCommand's fixed arrays and 511-byte
  "cannot be used/dropped" message too); native inventory equipment rows and the native drop projection no longer
  raise past SS_Q3_INVENTORY 64. gate-simA6's ownership rules kept (adopted arsenals freed at the next step start; the
  slot projection ring; UI rows live in their ring slot until it is reused). Other fixes: observeClientCommand's
  (angle + delta) & 65535 was ToInt32(angle) + ToInt32(delta) (fractions, int32 overflow); attachQ2Character cut the
  player model path at 255 bytes; per-gib and per-bind keep-arena records (spawnCharacterGib, bindSelectedOriginalPickups,
  which every native/QVM respawn rebinds; the binding is now malloc'd and freed by its dispose or at destroy);
  raise-path leaks in giveQ2StartItems, nativeInventoryRows, tickNativeCharacters and bindNativeInput's pickup rebinding.
  Review test tests/app/test_sim_gate_review_a7.c with tests/app/oracles/sim_gate_review_a7.ts (14 checks). Outside the
  scope, seen: world/gameplay/inventory.c keeps every pickup binding until the table is destroyed (unbind only drops the
  pointer) and leaks its captured rules on a raise; qa_ss_q1_set_identity leaks on a raising update; arsenal/q2.c cuts
  mission weapon labels at 63 bytes.
  Results as run (gate-simA7 tree, final sources): test_sim_verify 10 cases 0 mismatches and --diagnostics 10 cases 0
  failed; test_sim_a 38/0, test_sim_b 38/0, test_sim_c 333 passed, test_sim_concrete 108/0, test_sim_equipment 376/0,
  test_sim_monsters 248/0, test_sim_navigation 94/0, test_sim_bots 459/0, test_guest_bots_verify 92 lines 0 mismatches,
  test_bot_fidelity 752/0, test_sim_network 176/0, test_sim_network_q3 81/0, test_shared_simulation_save 101/0,
  test_shared_simulation_core/b1/b2/b3 ok, test_shared_simulation_rest 67 passed, test_app_concrete_original_save 9/0,
  test_d2_drift 96/0, test_persistence_q2_players 22/0, every tests/movement binary (7) pass, test_sim_gate_review_a
  13/0, a2 21/0, a3 19/0, a4 9/0, a5 14/0, a6 15/0, a7 14/0 and a7 clean under ASan/UBSan/LSan
  (detect_stack_use_after_return on). test_q2_original_saves failed 4 cases (classic, level-change, sidecar,
  engine-logic: "SDL audio driver cannot report exact queued input frames") on a first build that caught
  app/bootstrap/audio.c mid-edit by gate-app11; after a rebuild on its current source: 109/0, and test_sim_verify 10/0,
  test_remote_loopback every family 0 differing (54/0) and test_application_open_real q1/q2/q3 0 differing (12/0).
- app/bootstrap/simulation packet B PARTLY (gate-simB, 2026-11-11, build/agents/gate-simB). Read in full:
  native-q2-rerelease-save.ts, rerelease-guest-services.ts, rerelease-guest-source.ts, classic-guest-services.ts,
  classic-guest-source.ts, classic-guest-files.ts, q3/guest-artifact.ts, native-mod.ts, native-pmove-contacts.ts,
  native-guest-travel.ts, q3/host.ts (about 175 functions). Read for the task's open items or by throw site: native-q2-rerelease-pak.ts
  (configstring byte boundary), native-mod-presentation.ts (HUD stats), q3/runtime.ts (restore part, throws),
  q3/guest-runtime.ts, rerelease-guest-world.ts, classic-guest-world.ts, native-mod-host.ts, qvm-mod.ts, quakec-mod.ts,
  quakec-source.ts, quakec-local-messages.ts, qvm-grapple-source.ts, qvm-component-state.ts, foreign-q2-world.ts,
  network-q2-guest-core.ts and the q3 host/server-state/qvm-engine/server-traps/native-guest-source (every literal throw
  matched to its C raise by message and class, script), and every TextDecoder/JSON.parse site in the packet. About 100
  fixes (rows "gate-simB" in port_review.tsv); review test tests/app/test_sim_gate_review_b.c with
  tests/app/oracles/sim_gate_review_b.ts (31 checks), clean under ASan/UBSan/LSan. The worst: rerelease sourceInventory
  kept one source client per services, so a second client's binding destroyed the first's (use-after-free in coop);
  a refused rerelease native save leaked its decoded tree (the 22.9 KB LSan leak); the classic and rerelease guest
  sources ran no cleanup when construction or Init threw and stopped close/discard at the first failing step (TS runs
  every step and aggregates); a decoded rerelease save written back reordered its visited levels' keys (bytes differ
  from TS); the native HUD frame kept 32 of the 64 rerelease stats (found by the app network reader); 42 plain Errors
  in q3/guest_runtime.c and q3/runtime.c raised RangeError. Settled task items: native_q2_rerelease_save leak;
  rerelease inventory roster now owned per services (qa_compat_q2_rerelease_inventory_items_owned); playerState has a
  caller-owned _into form (services and world; the plain form stays for callers that copy at once, which every current
  caller does); qa_compat_qc_damage_inputs takes a caller-owned request (compat/qc/mod_combat.c, callers quakec_source.c
  and tests/compat/test_qc_mods_depth.c); the Q3 primary reader and compat/qvm/primary_presentation_profile.c's equipment
  reader are rooted at the declaration's own paths; q3/runtime.c's dead "graph" restore part removed. Rerelease
  configstrings (lead's item): TS holds a live rerelease configstring as TextDecoder output, not a byte string (only
  the pak import's byteText gives byte characters), so the C services keep the UTF-8 of the TS string and the checkpoint
  JSON carries it unchanged (native_q2_rerelease_save.c and rerelease_weapon_checkpoint.c read it as is; converting there
  would double-encode live text); the byte boundary is the pak: byteText on import, textBytes on export (.sv2 and the
  server.ssv comment, SaveFormatError "text: source save requires byte characters" above U+00FF), checked both ways
  against bun (cs.* checks, characters U+0080..U+00FF). LEFT (not read line by line): quakec-source.ts (2508 lines),
  quakec-local-messages.ts, q3/runtime.ts, q3/guest-runtime.ts, q3/native-guest-source.ts, q3/engine.ts,
  q3/server-state.ts (fixed 64-client/1024-configstring tables, TS-bounded), q3/qvm-engine.ts, q3/server-traps.ts, classic-guest-world.ts, rerelease-guest-world.ts,
  native-mod-host.ts, qvm-mod.ts, quakec-mod.ts, qvm-component-state.ts, qvm-grapple-source.ts, foreign-q2-world.ts,
  network-q2-guest-core.ts (drift-read by d9c), native-mod-presentation.ts beyond the HUD stats. Open: classic
  playerState's stats are one buffer per services; quakec_local_messages.c caps retained keys at 512 and clients at 64
  (TS Maps; a guest sending more than ~500 prompt choices raises "Out of memory"); quakec_source.c's fail() cuts a
  message at 1023 bytes; the rerelease pak's latched cvars and map command are not converted at the byte boundary
  (cvar registry convention is core's); a non-recoverable guest file open raises strerror text where Node says
  "CODE: text, open '...'"; q3/guest_bots.c bspValue keeps raw bytes where TS TextDecoder-decodes (packet C).
  Results as run (gate-simB tree, 2026-11-11): test_sim_verify 10 cases 0 mismatches and --diagnostics 10 cases 0
  failed; test_native_mod_host 70/0; test_q3_native_engine 57 lines 0 mismatches; test_q2_original_saves 109/0;
  test_q2_rerelease_pak 150/0 (and clean under ASan/UBSan/LSan: the 22.9 KB leak is gone); test_quakec_source 70/0;
  test_q3_source ok; test_guest_bots_verify 92 lines 0 mismatches; test_sim_native_mod 19/0; test_sim_guests 143/0;
  test_shared_simulation_save 101/0; test_app_concrete_original_save 9/0; test_q2_native_mod 224/0;
  test_q3_native_depth 160/0; test_qvm_mods_depth 195/0; test_qc_mods_depth 65/0; test_remote_loopback nine families
  0 differing (54/0); test_sim_gate_review_b 31/0, clean under ASan/UBSan/LSan.
- app/bootstrap/simulation packet B, second reader PARTLY (gate-simB2, 2026-11-11, build/agents/gate-simB2). Read in full:
  quakec-local-messages.ts, classic-guest-world.ts, rerelease-guest-world.ts, native-mod-host.ts, qvm-mod.ts, quakec-mod.ts,
  qvm-component-state.ts, foreign-q2-world.ts, network-q2-guest-core.ts, q3/guest-runtime.ts, q3/native-guest-source.ts,
  q3/server-state.ts, q3/qvm-engine.ts, q3/server-traps.ts, q3/engine.ts (about 360 functions by a declaration count);
  qvm-grapple-source.ts lines 1-420. About 45 fixes (rows "gate-simB2" in port_review.tsv); review test
  tests/app/test_sim_gate_review_b2.c with tests/app/oracles/sim_gate_review_b2.ts (24 checks), clean under
  ASan/UBSan/LSan. The worst: no Q3 guest external operation ended on a raise (TS finally), so one guest error left
  externalOperations stuck and every later checkpoint refused; admit, disconnect, initialize, shutdown, map shutdown and
  discard skipped their catch/finally cleanup and aggregation; QuakeC mods published no presentations and had no client
  presentation source; a failed classic or rerelease travel never released the candidate world (the shared guest source
  leaked); rerelease writeSave's game projections borrowed host scratch the level write refills. Open items settled:
  classic playerState stats buffer confirmed (every caller copies at once; audit in classic_guest_services.h);
  quakec_local_messages.c caps removed (checked with 600 prompt choices and 100 clients against bun); quakec_source.c
  fail() keeps the whole message; the rerelease pak's latched cvars and map command need no conversion: TS keeps them as
  byte characters end to end (the cvar registry and command text refuse anything above U+00FF), which C holds as raw
  bytes (checked both ways against bun, cv.*); a non-recoverable guest file open raises Node's form via
  qa_content_fs_error (checked: EISDIR). LEFT (not read line by line): quakec-source.ts (2508 lines), q3/runtime.ts
  (1303 lines; gate-simB read its restore part and throws), qvm-grapple-source.ts lines 420-624 (damage/sameTeam bindings,
  host checkpoint/restore, create, beginFrame, fire/admit/release, weaponView, presentations, pull, capture/restore),
  native-mod-presentation.ts beyond the HUD stats. Found outside scope: a C cvar registry restored from its checkpoint
  holds U+0080..U+00FF as UTF-8 where TS holds one byte character (core/cvars index.c capture/restore; server.ssv then
  writes C3 A9 for TS E9); quakec_source.c keeps every local message in s->retained for the source's lifetime (events
  borrow the text until drained); compat/q2/classic/host.c qa_compat_q2_fit_classic_userinfo cuts bytes and does not
  replace units above U+00FF with "?"; AggregateError messages with appended details in bot_rerelease.c:317 and
  qvm_weapon_behavior.c:527/672.
  Results as run (gate-simB2 tree, 2026-11-11, final sources): test_sim_verify 10 cases 0 mismatches and --diagnostics 10
  cases 0 failed; test_native_mod_host 70/0; test_q3_native_engine 57 lines 0 mismatches; test_q2_original_saves 109/0;
  test_q2_rerelease_pak 150/0; test_quakec_source 70/0; test_q3_source ok; test_guest_bots_verify 92 lines 0 mismatches;
  test_sim_native_mod 19/0; test_sim_guests 143/0; test_shared_simulation_save 101/0; test_app_concrete_original_save 9/0;
  test_sim_gate_review_b 31/0; test_q2_native_mod 224/0; test_q3_native_depth 160/0; test_qvm_mods_depth 195/0;
  test_qc_mods_depth 65/0; test_remote_loopback nine families 0 differing (54/0); test_sim_gate_review_b2 24/0, clean under
  ASan/UBSan/LSan.
- app/bootstrap/simulation packet B DONE (third reader gate-simB3, 2026-11-11, build/agents/gate-simB3). Read in full:
  quakec-source.ts (116 declarations), q3/runtime.ts (58 declarations and its closures in runtime.c), qvm-grapple-source.ts
  lines 420-624 and the constructor bindings gate-simB2 left (sameTeam, damage, host checkpoint/restore, create); with
  gate-simB and gate-simB2 every file of packet B is now read except native-mod-presentation.ts beyond its HUD stats,
  which this round assigned to gate-simA4. 26 fixes (rows "gate-simB3" in port_review.tsv); review test
  tests/app/test_sim_gate_review_b3.c with tests/app/oracles/sim_gate_review_b3.ts (30 checks; the Q3 checks need
  baseq3 pak0.pk3). The worst: a restore-mode Q3 source never ran spawnHandlers(), so trigger/misc/target save callbacks
  were unbound and any native save naming them failed to restore; the Q3 pool's mapStartTime was a construction snapshot
  (0) instead of level.startTime; Q3 native-state text (vmCvar settings, server configstrings/userinfo) was written as raw
  bytes, so the native checkpoint was invalid UTF-8 with any non-ASCII cvar. Text lifetime (task item): every local
  NetQuake message was kept in s->retained for the source's lifetime; the local-message tables now own copies of what
  they keep (quakec_local_messages.c; a restore copies its input first, so restoring the state's own capture is safe) and
  SimulationEvents/presentation state already copy emitted text, so the pool and the 1024-slot print ring are gone.
  fail() keeps the whole message (raise formats before cleanups); no test reaches it. Open items settled: core/cvars
  index.c capture writes the UTF-8 of each registry text and restore reads it back to bytes (refusing text above U+00FF,
  which a byte registry cannot hold; TS would keep it), settings/server/q2_owner.c's merged tree spells injected texts the
  same way, checked against bun (bytes, restored bytes, recapture); compat/q2/classic/host.c fitClassicUserinfo cuts UTF-16
  units, writes "?" per unit above U+00FF and answers guest byte characters, its readback held as UTF-8, checked against
  bun (8 cases); bot_rerelease.c's AggregateError already matched TS in the working tree. Found outside scope (not
  edited): application_transition.c guest_variables (lines ~466-484, 1243-1250, 1429-1431) passes cvar-tree text as
  registry text and now needs qa_command_text_from_utf8 for non-ASCII values (gate-app2); a QuakeCSource whose
  construction or checkpoint restore raises is leaked with its on_release subscription (callers are gate-simA4's source
  strategies); the Q3 runtime's hosts hold snapshots of TS live getters refreshed at entry points (refresh added at
  beginFrame and playerThink), a pattern that stays a risk wherever an entry point skips refresh; hostGive lowercases
  ASCII only where JS toLowerCase also folds Latin-1 letters (encoding of console args unsettled).
  Results as run (gate-simB3 tree, 2026-11-11, final sources): test_sim_verify 10 cases 0 mismatches and --diagnostics 10
  cases 0 failed; test_sim_gate_review_b3 30/0; test_sim_gate_review_b 31/0; test_sim_gate_review_b2 24/0;
  test_native_mod_host 70/0; test_q3_native_engine 57 lines 0 mismatches; test_q2_rerelease_pak 150/0; test_quakec_source
  70/0; test_q3_source ok; test_guest_bots_verify 92 lines 0 mismatches; test_sim_native_mod 19/0; test_sim_guests 143/0;
  test_shared_simulation_save 101/0; test_app_concrete_original_save 9/0; test_q2_native_mod 224/0; test_q3_native_depth
  160/0; test_qvm_mods_depth 195/0; test_qc_mods_depth 65/0; test_q2_original_saves 109/0; test_cvar_aliases 187/0;
  test_cvars_fixes ok; test_cvar_transfer 22/0; test_remote_loopback nine families 0 differing (54/0);
  test_application_open_real q1/q2/q3 0 differing (12/0); test_sim_gate_review_b3 clean under ASan/UBSan/LSan. Also run for
  the changed text boundaries: test_core_gate_review 60167/0, test_settings 147/0, test_settings_drift9 37/0,
  test_q2_save_folder 317/0, test_q3_game_logic 202/0, test_compat_drift 70/0, test_compat_q2_gate_review 2804/0,
  test_qvm_b 335 ok, test_sim_a 38/0, test_q3_director_source 532/532, test_q3_foundation 182/0, test_weapon_behaviors ok,
  test_ui_gate_review_legacy 53/0.
- app/bootstrap/simulation packet C PARTLY (gate-simC, 2026-11-11, build/agents/gate-simC). Read in full: physics.ts,
  q2-rerelease-slide.ts, new-toss.ts, q1-punch.ts, native-q1-pusher.ts, events.ts, arsenal-intent.ts,
  arsenal/inventory-labels.ts, network.ts, network-unified.ts, bot-world.ts, bots.ts, navigation.ts, monster-navigation.ts,
  equipment-runtime.ts, grapple-runtime.ts, q2-monster-sources.ts and prediction/runtime, sequence, step, presentation,
  source-state, qw-source-state (about 300 functions). By throw site over the whole packet: every qa_range_error message
  matched to its TS class by script (124 sites where TS throws Error raised RangeError: prediction, weapon-slot,
  weapon-behavior-runtime, player-movement, arsenal q2/q3/q3-source/q3-source-legacy/q3-ammo-regen,
  shared-quakeworld-movement, quakec-local-messages, q2-native-world). About 50 more fixes (rows "gate-simC" in
  port_review.tsv); review test tests/app/test_sim_gate_review_c.c with tests/app/oracles/sim_gate_review_c.ts.
  The worst: physics kept solids, motions, flags, collisions and sources in fixed 1024-entry tables (a larger level
  raised "Out of memory"), pushTeam had no finally (one raising touch left every later pusher refused), and impact
  handed touch callbacks physics-wide plane/surface scratch a nested impact overwrote; private prediction dropped the
  restored Q3 jump pad (lead's item: jumppad_ent cleared on every predicted command), ran QW stance commands on the
  plain QW provider, and never saw a new world gravity (cgame profile getter) or new QW movevars (inner profile);
  saved bot reliable slots were checked by bytes and a saved NUL accepted. Settled task items: Q2 nearby (gate-simA2
  removed the cap; confirmed); network.c keeps its own userinfo copy, network_unified.c parses it into its values at
  once and bot_world.c copies it at once (confirmed); q3/guest_bots.c bspValue decodes its text as TextDecoder does (lead's item); qa_ss_q2_reaction_end's kept attack (shared_simulation_core.c,
  packet A's file) has its attacker/inflictor boxed and now interns its strings (id1 environment death types live in
  the damage call's buffer). LEFT (not read line by line): bot-assets, bot-q1/q2/q3-knowledge, bot-selected-knowledge,
  bot-arsenal, bot-commands, bot-objectives, bot-prediction, bot-mover-connections, bot-rerelease, bot-rerelease-world,
  network-q1, network-q1-quakec, network-qw, network-q2-guest, network-q2-guest-core, network-q2-rerelease-native,
  network-q3, arsenal/q1, q2, q3, q3-source, q3-source-legacy, q3-ammo-regen, weapon-status, monster-runtime,
  monster-placement, monster-sources, guest-monsters, monster-admission, weapon-slot, weapon-slot-projection,
  weapon-behavior-runtime, quakec-/qvm-/rerelease-weapon-behavior, native-primary-weapons, actor-execution,
  player-movement, player-jump, player-input-application, powerup-timers, q1-fog, shared-quakeworld-movement,
  native-commands, q3-commands, native-q2-map, native-q2-travel, source-transition, q2-native-world, quakeworld-cvars,
  quakec-character-animation, quakec-client-adapter, quakec-map, quakec-player-ui, prediction.ts and the q3/ directory
  (taken as packet B's guest hosts); their throw classes are fixed as above. Open: monster navigation keeps per-actor
  runtimes until destroy; grapple handoff blocks and retired Q2 bindings accumulate until destroy; a Q1 threewave grapple
  checkpoint read from a save raises "no content conversion" (TS restores it); qa_q2_server_data_result levelname and
  gamedir are fixed arrays (network/q2 contract). Results as run (gate-simC tree): test_sim_verify
  10 cases 0 mismatches and --diagnostics 10 cases 0 failed; test_sim_c 333 passed, test_sim_equipment 376/0,
  test_sim_monsters 248/0, test_sim_navigation 94/0, test_sim_network 176/0, test_sim_network_q3 81/0,
  test_weapon_behaviors ok, test_shared_simulation_save 101/0, test_sim_bots 459/0, test_bot_fidelity 752/0,
  test_q3_source ok, test_guest_bots_verify (frames 1-300) 0 mismatches, every tests/bots binary 0 failures,
  test_remote_loopback nine families 0 differing (54/0), test_application_open_real q1/q2/q3 0 differing (12/0),
  test_sim_gate_review_c 48/0 and clean under ASan/UBSan/LSan.
- app/bootstrap/simulation packet C, second reader PARTLY (gate-simC2, 2026-11-11, build/agents/gate-simC2). Read in full:
  bot-arsenal, bot-assets, bot-q1/q2/q3-knowledge, bot-selected-knowledge, bot-commands, bot-objectives, bot-prediction,
  bot-mover-connections, bot-rerelease, bot-rerelease-world, network-q2-guest-core, network-q2-guest,
  network-q2-rerelease-native, network-q3, arsenal/weapon-status, arsenal/selected, arsenal/q1, q2, q3, q3-ammo-regen,
  monster-admission, monster-sources, native-primary-weapons, player-input-application, quakec-character-animation,
  quakec-client-adapter, quakec-player-ui, native-q2-travel, prediction.ts (about 330 functions). Targeted reads:
  network-q1 (entities, baseline, clientData, observe, operator ring, prints), network-q1-quakec (command, input),
  network-qw (command, frame caps, downloads), rerelease-weapon-behavior (storage, operation, flushReleases, attach),
  guest-monsters (planGuestMonsters). Skipped as gate-simA3's: actor-execution, weapon-slot*, q3-commands and the rest of
  its list. 37 fixes (rows "gate-simC2" in port_review.tsv); review test tests/app/test_sim_gate_review_c2.c with
  tests/app/oracles/sim_gate_review_c2.ts. The worst: the Q2 and Q3 selected arsenals kept primary-weapon handoff
  contexts in a global 16-entry ring, so the 17th weapon slot bound overwrote a live slot's actor (holster/select acted on
  another player); the Q3 arsenal kept pending holdable ids in a global 64-entry ring and capped players at 64, and leaked
  the spawn loadout and every step's ammo list; the Q1 rerelease bot chat freed each looked-up text before choosing a
  variant (use-after-free); the Q2 arsenal capped turns at 64 and its pickup order at 32; mapped ammo regeneration capped
  owners at 16 and actors at 64 and kept a refused entry; the rerelease weapon component capped bindings, slots, retired
  trajectories and owners at 256, dropped pending releases past 256 and left releases unflushed after a raising operation.
  Settled gate-simC open items: a Q1 threewave grapple checkpoint read from a save restores (converted to the content
  checkpoint); grapple handoff blocks are one per actor and retired Q2 bindings are freed once uncaptured; monster
  navigation frees a runtime when replaced or when its actor is released. LEFT (not read line by line): arsenal/q3-source,
  q3-source-legacy, monster-runtime, monster-placement, weapon-behavior-runtime, quakec-/qvm-weapon-behavior, the rest of
  rerelease-weapon-behavior, player-movement (throw sites matched), the rest of network-q1, network-q1-quakec and
  network-qw. Open: qa_q2_server_data_result levelname/gamedir stay 2048-byte arrays (every network/q2 reader uses 2048
  buffers; a guest CS_NAME longer than 2047 bytes is cut; not changed); the native Q3 network host keeps every input()
  command until destroy (network/q3.c queues the pointer until poll and the host has no per-frame hook; fix needs the
  queue to copy the command, network/q3.c); the Q1/Q2/Q3 arsenal handoff records (one small record per actor seen) and
  removed Q3 arsenal players live until destroy; the Q3 mapped ammo timers are freed at release while a
  caller may hold the views (as before). gate-simC's unchecked fixes (physics tables, push-team finally, nested impact
  locals, network caps, QW stance, live-profile reads) still have no review check; the gate-simC2 fixes in bot-rerelease,
  grapple-runtime, monster-navigation, network prints and rerelease-weapon-behavior are not reached by a review check
  (they need running populations, saves or guests).
  Results as run (gate-simC2 tree, final sources): test_sim_verify 10 cases 0 mismatches and --diagnostics 10 cases 0
  failed; test_sim_bots 459/0, test_bot_fidelity 752/0, test_guest_bots_verify (frames 1-300) 92 lines 0 mismatches,
  test_sim_navigation 94/0, test_sim_monsters 248/0, test_sim_equipment 376/0, test_sim_network 176/0, test_sim_network_q3
  81/0, test_sim_c 333 passed, test_weapon_behaviors ok, test_q3_source ok, test_shared_simulation_save 101/0,
  test_sim_gate_review_c 48/0, every tests/bots binary 0 failures (13), test_remote_loopback nine families 0 differing
  (54/0), test_application_open_real q1/q2/q3 0 differing (12/0), test_sim_gate_review_c2 11/0 and clean under
  ASan/UBSan/LSan (the leak it found, the Q3 spawn loadout's ammo, is fixed); against the old weapon-status, q3 and
  q3-ammo-regen sources it aborts (RangeError: Out of memory at the 65th Q3 player).
- app/bootstrap/simulation packet C, third reader PARTLY (gate-simC3, 2026-11-11, build/agents/gate-simC3). Read in full:
  weapon-behavior-runtime, quakec-weapon-behavior, qvm-weapon-behavior, arsenal/q3-source-legacy, monster-placement;
  player-movement partly (helpers, selected profile, environment, prediction input, observation). About 25 fixes (rows
  "gate-simC3" in port_review.tsv); review test tests/app/test_sim_gate_review_c3.c with
  tests/app/oracles/sim_gate_review_c3.ts. Settled gate-simC2 open items: the native Q3 host no longer keeps every input
  command (network/q3.c copies each accepted command into its queue, poll returns copies valid until the next poll; the
  host's input() returns a borrowed scratch command); arsenal handoff records are reused for a newer generation in the
  same actor slot (one per live slot) and removed Q3 players are freed at once or when the outermost step ends; Q3
  mapped ammo timer views stay valid after release (retired, current() false) until the next timers/capture/restore,
  which also drops entries of actors no longer live (nothing routes onRelease to the owner); qa_q2_server_data_result
  gamedir/levelname are owned strings (every network/q2 reader and app caller updated; list in port_review.tsv). The
  worst of the rest: the weapon-behavior runtime kept sources/definitions/deferred entries in 16-entry arrays (17th
  overflowed), closed only up to the first raising part, and restored qvm/native actors by reference where TS fails the
  save for a retired actor; QuakeC and QVM weapon sources kept one instance per projectile fired until destroy; QVM
  AggregateError messages had the inner messages appended (coordinator item); Q2UnboundServerMessage was raised as
  Error (network/q2/server_messages.c). gate-simC's unchecked fixes: no review check added (physics tables, push-team,
  nested impact locals, network caps, QW stance and live-profile reads all need a populated shared physics or
  prediction world; not built in this pass). LEFT (not read line by line): arsenal/q3-source, monster-runtime, the rest
  of rerelease-weapon-behavior (its instance lifetime too), player-movement's provider/prediction setup, the rest of
  network-q1, network-q1-quakec and network-qw. Open: the Q1, Q1-QuakeC, Q2 and QW network hosts (network_q1.c,
  network_q1_quakec.c, simulation/network.c host_input) still allocate every input command and keep it until destroy:
  the fix is the Q3 one, which needs app/bootstrap/network/q1.c, qw.c and q2.c to copy accepted commands (not in this
  pass's allowed edits); the shared simulation does not route actor release to the Q3 mapped ammo owner
  (shared_simulation_arsenal.c qa_app_ammo_actor_registry has no onRelease; packet A's file).
  Results as run (gate-simC3 tree, final sources): test_sim_verify 10 cases 0 mismatches and --diagnostics 10 cases 0
  failed; test_guest_bots_verify 92 lines 0 mismatches, test_bot_fidelity 752/0, test_sim_bots 459/0,
  test_sim_navigation 94/0, test_sim_monsters 248/0, test_sim_equipment 376/0, test_sim_network 176/0,
  test_sim_network_q3 81/0, test_sim_c 333 passed, test_weapon_behaviors ok, test_q3_source ok,
  test_shared_simulation_save 101/0, test_sim_gate_review_c 48/0, test_sim_gate_review_c2 11/0, test_q2_app_units
  1500/0, test_q3_server_network_catch 31/0, every tests/network q2 binary 0 failures (gate_review_q2 100, network_q2
  105, codec_readers 405, codecs_depth 3266, q2_depth 110, gtv_mvd 186, mvd_demo 8600, network_cases 89, remote_input
  300), test_q3_round 1104/0, test_q3_app 704/0, test_remote_loopback nine families 0 differing (54/0),
  test_application_open_real q1/q2/q3 0 differing (12/0), test_sim_gate_review_c3 25/0 and clean under ASan/UBSan/LSan.
- app/bootstrap/simulation packet C, fourth reader DONE (gate-simC4, 2026-11-11, build/agents/gate-simC4). Read in full
  what gate-simC3 left: arsenal/q3-source, monster-runtime, the rest of rerelease-weapon-behavior, player-movement's
  createPlayerMovementProvider/createPlayerMovementPrediction, the rest of network-q1, network-q1-quakec and network-qw
  (about 190 functions; rows "gate-simC4" in port_review.tsv). Review test tests/app/test_sim_gate_review_c4.c with
  tests/app/oracles/sim_gate_review_c4.ts. Fixes: the Q3-arsenal movement prediction never freed its source movement
  hooks (one per bot/navigation query) and leaked itself when its setup raised, and read the Q3 arsenal runtime over an
  uninitialized previous state; the rerelease weapon component kept every trajectory instance until destroy (close frees
  it now), handed the provisioning ClientCommand a garbage record (the source passed as the call context), swallowed a
  flushReleases failure in operation's finally, never flushed releases after a raising checkpoint/restore, and had no
  TS try/catch around initialization (now close plus AggregateError "Native component initialization and cleanup
  failed"); the selected Q3 source skipped publish() after a raising call (TS publishes always; AggregateError "Selected
  Q3 call and publication failed"), had no finally in giveHoldable, stopped close at the first failure (TS collects
  them: AggregateError "Selected Q3 source close failed"), and kept one step closure per tracked actor until destroy
  (freed at release now); the NetQuake QuakeC frame consumed damage before clientData; QW prepareDownload lowercased
  ASCII only, and a refused QW setup leaked its tables; leaks on refused saves (Q3 graph and published rows, rerelease
  cvars/configstrings/saves). monster-runtime: no divergence. Settled gate-simC3's open item: network/q1.c and
  network/q2.c copy each admitted command into their queue and poll returns copies valid until the next poll (as Q3);
  the Q1, QuakeC-Q1 and classic Q2 hosts return one scratch command from input() (they allocated one per command and
  freed them at observe, so a frame without publish kept them; not until destroy). QW needs nothing: commandGroup hands
  the commands to the simulation queue, which copies. gate-simC's unchecked fixes (physics fixed tables, push-team
  finally, nested impact locals, network caps, QW stance provider, live-profile prediction reads): still no dedicated
  check; none is reachable without a populated shared physics or prediction world, which no existing unit test builds
  (test_sim_verify and the loopbacks run real maps through them but do not force a table past 1024 or a raising touch).
  No review check for the rerelease weapon (needs the game DLL guest), selected Q3 source (needs the shared simulation
  host), QuakeC NetQuake frame and QW host fixes (need a QuakeC source world). Not ours, left: shared_simulation_arsenal.c
  ss_q3_execute keeps one execution record per tracked actor until the simulation ends (qa_app_shared_sim_keep);
  shared_simulation_access.c ss_q3_arsenal_runtime uses *out as the previous state (player_movement.c now initializes it);
  QW authentication is refreshed at admit/observe (TS getters read live; network/qw contract holds plain strings).
  Results as run (gate-simC4 tree, final sources): test_sim_verify 10 cases 0 mismatches and --diagnostics 10 cases 0
  failed; test_guest_bots_verify 92 lines 0 mismatches, test_bot_fidelity 752/0, test_sim_bots 459/0,
  test_sim_navigation 94/0, test_sim_monsters 248/0, test_sim_equipment 376/0, test_sim_network 176/0,
  test_sim_network_q3 81/0, test_sim_c 333 passed, test_weapon_behaviors ok, test_q3_source ok,
  test_shared_simulation_save 101/0, test_q2_app_units 1500/0, every tests/network binary 0 failures (37, among them
  test_q3_app 704/0, test_q3_round 1104/0, test_q1_app_units 156/0, test_network_q2 105), every tests/movement binary 0
  failures (7), test_sim_gate_review_c 48/0, c2 11/0, c3 25/0, test_remote_loopback nine families 0 differing (54/0),
  test_application_open_real q1/q2/q3 0 differing (12/0), test_sim_gate_review_c4 34/0 and clean under ASan/UBSan/LSan
  (against the old player_movement.c it reports the two leaks: the hooks holder and the raising setup's prediction).
- app/bootstrap top-level packet 1 PARTLY (gate-app1, 2026-11-11, build/agents/gate-app1). Read against v1.2.7: application.ts
  open/openBorrowed/openSource (both catch levels), step, run, recoverDedicatedFrame, close, closeOwned, closeFailedWorld,
  saveGame, loadGame, restoreSavedGame, autosaveLevel, saveUnavailable; configuration.ts, config-scripts.ts, options.ts,
  frame-time.ts, frame-loop.ts, console-log.ts, native-settings.ts, native-migration.ts, q2-native-saves.ts, seat-profile.ts,
  demo-library.ts, capture.ts, shared-setting-cvars.ts, q3-common-cvars.ts, server-map.ts, world-replacement.ts,
  application-tools.ts in full;
  prepared-startup.ts (run/applyArchive), content.ts and diagnostic-tools.ts for error classes only. About 45 fixes plus
  ~75 error-class corrections (TS `new Error` sites that raised RangeError in options, configuration, prepared startup,
  content, diagnostic tools, config scripts). Rows "gate-app1" in port_review.tsv. Review test
  tests/app/test_app_gate_review_1.c with tests/app/oracles/app_gate_review_1.ts.
  Settled: the native config encoding (open item from gate-misc and D9b (4)): the native script reaches the console as
  one byte per code unit like every other script; write/writeBindings/absorbSession take console text and write UTF-8;
  cvar archives convert at their JSON boundary; mixed-layer values and +command arguments convert to console text (a
  character above U+00FF is refused with TS's RangeError "Command text requires source bytes"; for the native script C
  raises it at the read, TS when it executes). Written files byte-identical to TS (bun oracle). The d9b nonexistent-corpus
  leak: openSource's raise paths and openInitialConfigurationContent now free what they built (the catalog discovered
  before the missing product raised was most of it). Under LSan, test_d9b_drift over a missing corpus went from about
  71 KB to 1179 bytes, all of it outside this packet: app/bootstrap/q3_product.c:55 (policy cvar registry, identity
  and the product-id text when the Q3 product preparation raises) and content/catalog/index.c:761
  (qa_content_catalog_require's formatted message); test_app_gate_review_1 under ASan/UBSan/LSan: 28/0, only the
  41-byte catalog message leaks.
  The worst: prepareInitialConfiguration and prepareProfileConfiguration kept the caller's stack argument block (the
  prepared startup printed through it and a borrowed game's configuration continuation read it after open returned);
  every failed Application.open leaked its whole partial state; closeOwned of a borrowed game skipped stopRecording,
  the client capture's beforeWorldChange and clearing client.source.current; input deferred across a world load kept
  the platform's transient text pointer.
  Results as run (gate-app1 tree): every tests/app binary named test_app_*, test_application*, test_top_*, plus
  test_configuration, test_d2_drift (its outputFormat check moved to TS v1.2.7's default), test_d9b_drift,
  test_d9d_drift, test_main_composition, test_startup_port 457/0, test_application_open_real (dedicated q1/q2/q3 0
  differing from bun, graphical q1), test_remote_loopback (54/0, every family 0 differing), test_q2_original_saves
  (109/0, all six scenarios), every tests/settings binary; test_sim_verify 10 cases 0 mismatches.
  LEFT in application.ts: 2039-8735 (prepareGuestBots, createBots, services, recordings, network, openGraphical, seats,
  arenas, keys, world transition stages, travel, cinematics, local player changes, command dispatch, presentOutput) not
  read in this pass; content.ts, prepared-startup.ts (beyond run/applyArchive), original-save.ts, demo-local-recording.ts,
  image-settings.ts, diagnostic-tools.ts, match-results.ts not read line by line.
  Open: console text written to files is still bytes where TS encodes UTF-8 (qconsole.log/games.log AppendLog,
  writeconfig in console/commands.c), and cvar values typed in the C console field are UTF-8 where TS holds code units
  (src/console/field.c), so such a value is double-encoded when it reaches a native config or archive; a borrowed
  application's prepared profile is never destroyed (its continuation may still be adopted by the client's prepared
  startup); the other callers of qa_app_prepare_initial/profile_configuration (startup_application_graphics.c:989,
  remote_application_config.c:719, remote_application_profile.c:539) do not pass args.pending, so a raise there still
  leaks the partial configuration.
- app/bootstrap top-level packet 2 PARTLY (gate-app2, 2026-11-11, build/agents/gate-app2). Read against v1.2.7: application.ts
  2039-6436 except the members listed as NOT read in its port_review.tsv row (96 members: bots, getters, input, the
  console/source command preparation, kick/restart/game log, sourceActions, recordings, administration, networkHost,
  openNetwork, openGraphical, publishBorrowed, seats/client cvars/arenas/keys, saved clients, replaceWorld and the world
  transition through stageGraphical's first half, stageCommit and finishPublication); diagnostic-tools.ts in full (C).
  The five open items from gate-app1 are settled: qconsole.log/games.log (AppendLog), condump, writeconfig and the
  dedicated writeconfig now write the console text as UTF-8; the console field holds console text (one byte per
  character) with SDL text and clipboard converted at the input boundary, and seat settings convert bound commands and
  console history at their JSON boundary, so a typed cvar value is no longer encoded twice; a borrowed game's prepared
  profile is destroyed with its open record after its continuation is retracted from the client's prepared startup;
  queued client inputs own their text; args.pending is passed at startup_application_graphics.c (initial),
  remote_application_config.c and remote_application_profile.c (an arena slot); q3_product.c frees its policy registry,
  identity and phases on a raise and catalog/index.c frees qa_content_catalog_require's message. test_d9b_drift with a
  nonexistent corpus is leak-free under LSan (see results). Other fixes: mounted source scripts stayed UTF-8 (now console
  text; a character above U+00FF raises TS's RangeError at the read); kick targets and command names lowered ASCII only
  (TS folds Latin-1) and kick names were cut at 2047 bytes; the base arena announcer name lowered ASCII only; mod client
  drop records leaked every frame with drops; Team Arena postgame leaked its store, map name and demo library on a raise;
  native Q2 seat userinfo leaked, and a transition's graphical aliased the staged native rows, so a later local player
  change reallocated them and the transaction release freed the stale pointer; a replaced local guest seat leaked its
  userinfo; saved-client close failures never formed TS's AggregateError; bots created by a world transition inserted
  console commands into the candidate buffer after publication (TS guestCommands() resolves per insert); the planned Q3
  cvar rows and queued guest network commands leaked; diagnostic timerstamp labels were cut at 1023 bytes, timer stamp
  reports at 128 bytes a stamp, and frameinfo neither escaped its strings nor took more than 1023 bytes; the Q3 product id
  read leaked its product name text when the mount open raised (content.c, the last d9b missing-corpus leak); the guest
  cvar rows a QVM map change carries (application_transition.c guest_variables) took the checkpoint tree's text, which
  is now UTF-8 (gate-simB3's cvar checkpoint change), so they are converted to console text before they reach a registry
  or q3Cvars (needs a QVM map change with a non-ASCII cvar: not reached by the review test). Review test
  tests/app/test_app_gate_review_2.c with tests/app/oracles/app_gate_review_2.ts (18 checks), clean under ASan/UBSan/LSan.
  Results as run (gate-app2 tree): every tests/app test_app_*, test_application*, test_top_* binary, test_configuration,
  test_d2_drift, test_d9b_drift, test_d9d_drift, test_main_composition, test_startup_port 457/0,
  test_application_open_real 12/0 (stdin closed), test_remote_loopback 54/0, test_q2_original_saves 109/0, every
  tests/console and tests/settings binary, test_sim_verify 10 cases 0 mismatches; test_d9b_drift with
  QTS_TEST_CORPUS=/nonexistent under ASan/LSan: no leak (its three open cases fail as they must without the games). After
  the last two fixes (guest_variables, content.c) the quick app tests, open_real, remote_loopback, sim_verify and
  startup_port were rerun green; test_top_c then failed to compile from another agent's in-progress interface change.
  Divergence kept: a console-field character above U+00FF is dropped at the input boundary (TS keeps it and refuses the
  submitted command with "Command text requires source bytes").
  Found outside the scope (not edited): src/app/bootstrap/simulation network bindings take a master-server snapshot
  (qa_app_sim_qw/q2 server binding options masters/master_count) where TS passes administration.masters as a function, so
  a later setmaster or `public` change is not seen and the snapshot points into admin_masters' buffer, which the next
  masters() call frees; content/q3/product_restriction.c:26 "Invalid product identification" raises RangeError (TS
  Error); remote_application_profile.c:808 publishes a profile continuation the remote releases without retracting it
  from the client's prepared startup.
  LEFT: application.ts 6436-8735 (publishWorld, retirePreviousWorld, saves helpers, restartSourceRound, applyTransition,
  travel, cinematics, local player changes, command dispatch, presentOutput) and the members listed in the row;
  content.ts, prepared-startup.ts (beyond run/applyArchive and its writeconfig), original-save.ts,
  demo-local-recording.ts, image-settings.ts, match-results.ts not read line by line.
- app/bootstrap top-level packet 2b PARTLY (gate-app5, 2026-11-11, build/agents/gate-app5). Read against v1.2.7: application.ts
  6436-8735 in full (publishWorld through presentOutput: retirement, saves helpers, restartSourceRound, applyTransition,
  travel, cinematics and system cinematics, local player changes, command dispatch, presentOutput) and gate-app2's NOT-read
  members (uiSourceOptions, createQ3SeatClient, createSeatUi, createWorldSeatPresentation, openGuestBrowser,
  createCandidateSinks, stageSourceCommands, stageGuestContinuation and stageBots C side, stageGraphical after the volume
  binding, createWorldAudio, createPresentationServices, stagedClock, createApplicationCapture, createVideoRestart,
  hostPrinter, stageCapture, createCandidateCapture, stageReplacementSet, stageHeadlessAdoption): application.ts is read in
  full. original-save.ts, demo-local-recording.ts and match-results.ts read in full; image-settings.ts, content.ts and
  prepared-startup.ts partly (see their rows). Rows "gate-app5" in port_review.tsv.
  Open items settled: (1) the console field divergence gate-app2 kept: the field, chat field and console history hold the
  TS string as UTF-8, so a character above U+00FF stays in the field; submit prints the echo and then raises TS's
  RangeError "Command text requires source bytes" before the append, history and clear; trimStart trims JS white space
  (U+3000, U+00A0); settings/config.c keeps history lines as UTF-8 (no console-text conversion); the console draw decodes
  the field; qa_console_text_from_input is gone. (2) content/q3/product_restriction.c raises plain Error: test_q3_foundation
  145/0 (its CHECK_ABORTS holds; the test does not inspect the class).
  Fixes: presentOutput delivered a native Q2 seat's source/raw messages again when the host's localMessages answered []
  for a recipient it had already served (TS `??` only skips an absent function); sourceCommand's arsenal branch and step's
  supplemental-weapon check freed the weapon-slot projection's ui.items, which the simulation's projection ring owns and
  frees on reuse (double free); LoadedApplicationContent.forContent opened a provider's mounts with the whole pure list,
  so openMountPlan raised "Required pure archive is missing" for every approved archive the provider does not hold (TS
  filters the list); openRemoteContent, openRemoteApplicationContent and openApplicationConfigurationContent leaked their partial
  mounts and discovered catalog when a later step raised;
  images.cfg was read as raw bytes and written as console bytes (TS decodes UTF-8 with the BOM dropped,
  drops a file holding a wide character with "Command text requires source bytes", and writes UTF-8); the Q3 guest exec
  inserted raw script bytes (TS TextDecoder); load, the classic qts.sav/server.ssv check and the rerelease sidecar check
  took a folder for an existing file (Bun.file().exists()); image settings' modelPolicy trimmed and parsed console text as
  UTF-8 (a U+00A0 byte was not white space); rereleasePortalCount raised its own message for an empty map
  file; preparedStartup.resetBindings' weapon/impulse/use test read console text as UTF-8 (a U+00A0 separator missed);
  saveSourceArchive/saveClientArchives leaked their store and entries when the archive write raised (gate-app7's item;
  TS propagates the error); selectedApplicationPreset's QuakeWorld timing kept the NetQuake clock fields (TS builds a new clock) and
  applicationOptionsForRecipe took the last movement timing profile (TS find takes the first);
  fixed-size buffers in the recipe id's revision text (512 bytes), exec/botlist/"Unknown game command"/status/ping, the saved secondary-seat cvar message and the
  prepared startup's 64-registry output set. New core helpers qa_command_buffer_append_decoded/insert_decoded (TS
  sourceText over a decoded string).
  Review test tests/app/test_app_gate_review5.c with tests/app/oracles/app_gate_review5.ts (43 checks: the console field
  and submit against bun, images.cfg load/save bytes and modelPolicy; clean under ASan/UBSan/LSan). tests/console/test_console_review.c and
  tests/app/test_app_gate_review_2.c moved to the UTF-8 field (same TS values).
  Not reached by a test: the Q2 localMessages fall-through, the projection ring double free, forContent's pure filter,
  the exec decode, the folder existence checks, rereleasePortalCount, resetBindings' U+00A0, the buffer caps.
  Kept: the chat field's text reaches say with a character above U+00FF left out (input.c console_chat; TS passes the
  string on to each source's say, not traced per family).
  Found outside the scope (not edited): include/qa/app/bootstrap/simulation/weapon_slot_projection.h:15 still says the
  caller frees ui.items (qa_app_shared_sim_weapon_slot's ring owns them, shared_simulation_arsenal.c:2701).
  Results as run (gate-app5 tree): every tests/app test_app_*, test_application*, test_top_* binary 0 failures (review5
  43/0, review_1 28/0, review_2 18/0, review3 20/0, review4 31/0, test_top_q3_client 23276/0), test_configuration 133/0,
  test_d2_drift 96/0, test_d9b_drift 47/0, test_d9d_drift 6/0, test_main_composition 54/0, test_startup_port 457/0, every
  tests/console (111, 230/0, 48/0) and tests/settings binary 0 failures, test_q3_foundation 145/0, test_sim_verify 10 cases
  0 mismatches, test_remote_loopback 54/0 (nine families 0 differing), test_application_open_real 12/0 (q1/q2/q3 0
  differing), test_q2_original_saves 109/0 (all six scenarios; run twice, before and after the last content.c fixes). test_app_gate_review_2, test_startup_port and test_settings were relinked
  from freshly compiled test objects against a private archive (another agent's in-progress qa_seat_settings and
  startup_config.c changes left the tree's test objects stale and startup_config.c not compiling); the shared build's
  binaries of the earlier gate-app5 build passed them as well (18/0, 457/0, 147/0). After the saveSourceArchive/saveClientArchives fix:
  test_app_gate_review5 43/0, test_application_open 85/0, test_d9b_drift 47/0, test_application_core ok, test_sim_verify
  10 cases 0 mismatches.
  LEFT (finished by gate-app10, next entry): content.ts:
  prepareMods/QvmGrapple/WeaponBehaviors, retainMainMounts, resolveApplicationTravel and
  loadApplicationContent after the world decode (QuakeC/Q3/Q2 preparation); prepared-startup.ts
  beyond run/applyArchive/writeconfig and the members in its row (adopt, bindings, adoptBindingDefaults, previewBindings,
  execute, advanceFrame, adoptSeat, publishSeats, prepareClientCommands); image-settings.ts registration table (no bun dump), applyDisplay, refresh,
  refreshDisplay, policy, bindViewSettings.
- app/bootstrap top-level packet 2c DONE (gate-app10, 2026-11-11, build/agents/gate-app10). Read against v1.2.7 what the
  Application readers left: content.ts (prepareMods, prepareQvmGrapple, prepareWeaponBehaviors, retainMainMounts,
  openedMounts, forContent, resolveApplicationTravel, loadApplicationContent after the world decode), prepared-startup.ts
  (adopt, bindings, adoptBindingDefaults, previewBindings, execute, advanceFrame, adoptSeat, publishSeats,
  prepareClientCommands and its function, run's steps), image-settings.ts (the registration table value for value against
  bun for three dialects, applyDisplay, refresh, refreshDisplay, policy, bindViewSettings, prepareClientSettings): all
  three files are now read in full. And the demo-*.ts files no packet had read: demo-commands, demo-playback,
  demo-recording, demo-recording-commands in full (demo-library: gate-app1; demo-local-recording: gate-app5). The
  startup-*.ts files are gate-app8's. Rows "gate-app10" in port_review.tsv; about 45 fixes. The worst:
  - demo commands used the request's freed-and-reallocated name buffer as its identity: the next request usually got the
    same address, so its completion read as already completed and the attract loop stopped after its first demo; the
    kept command contexts were not copied (a script origin's name: use after free at completion);
  - every command documentation the demo and recording commands and q1_client_settings.c's chase cvars registered
    pointed into the stack or a freed buffer (the buffer and registry borrow documentation): help printed another
    command's example, and the review test crashed reading it;
  - a recording start whose seed, attach or open raised left the recorder busy for ever ("Recording operation is already
    in progress") and leaked the open recording; "Completed demo" printed after a failed stop; recording files were
    named in console bytes (TS: UTF-8), open errors were "could not open recording file" (TS: Node's EEXIST/ENOTDIR
    messages), writes were stdio-buffered and unsynced;
  - loadApplicationContent leaked the whole loaded content (catalog, mounts, world, preparations) on any raise between
    discovery and the return; forContent leaked its scratch on a raise and kept the first of a repeated mount id (TS Map:
    the last); the Q3 preparation did not prefer a QVM module; travel resources were not keyed by id;
  - PreparedStartup.adopt replaced the constructor's dialects (TS keeps them); run yielded between seat configurations
    where TS continues in the same step; prepareClientCommands capped registries at 64 and released inputs in the wrong
    dialect.
  Open items settled: (1) the chat field's text (gate-app5's kept item): TS hands the string to each family's say; every
  byte writer it reaches keeps one byte per UTF-16 unit (Q1/QW/Q2 wire and classic game argv: & 255; Q3 wire: '.' above
  127; QVM argv: & 255), so the chat text is now each unit's low byte (input.c qa_app_chat_console_text); the Q2 KEX
  client's command and userinfo are now UTF-8 as TS's TextEncoder writes them (network/q2.c). Kept: a character above
  U+00FF reaches the UTF-8 writers (rerelease game argv, KEX, unified control) as its low byte's character. (2) The
  coordinator's item from gate-app8: a startup script read that raises now settles the exec as failed: the core/commands
  read result and completion carry the error class (error_name), the buffer prints "couldn't exec <name>: <message>", and
  StartupConfig rethrows the read's class (prepared_startup.c, configuration.c adapters through
  qa_app_script_read_settle; startup_config.c; config_scripts.c releases its read count on a raise). (3) gate-app9's
  items: application_tools.c camera_read and image_settings.c open free what they built when loadText raises; the
  source buffer's script read (application_commands.c) raised RangeError at the read for a decoded character above
  U+00FF, before "execing"; TS's insertScript cuts the decoded string at U+0000 and throws after "execing": a READY
  read result can now carry a TextDecoder string (text_utf8) that the core inserts through sourceCommandText.
  Review test tests/app/test_app_gate_review10.c with tests/app/oracles/app_gate_review10.ts (350 checks: chat bytes,
  the failed read, the attract loop, Q2 completion, command examples, the local-world stage class, demo playback errors,
  recording paths, file names and Node errors, the busy flag, the generated name, the image-settings registration table
  a failed save, and scripts read as TextDecoder strings with a NUL or a wide character; clean under ASan/UBSan/LSan).
  Not reached from a test: the content.ts fixes (installed content), the prepared-startup fixes (a running startup), the
  recording write/fsync errors, the KEX UTF-8 writes.
  Found outside the scope (not edited): src/input/mouse_settings.c:76-86 documents each mouse cvar with a stack usage
  buffer and a stack examples array (the registry borrows them); compat/q2/rerelease/imports.c argv/args write console
  bytes where TS writes TextEncoder UTF-8 (bot_rerelease.c's chat text, gate-simA6's file, must be checked first);
  mod_presentations.c:376 builds a failed script read without error_name (reads as Error; gate-app9's file).
  Results as run (gate-app10 tree): every tests/app test_app_*, test_application*, test_top_* binary
  0 failures (review10 350/0, review_1 28/0, _2 18/0, 3 20/0, 4 31/0, 5 43/0, 6 72/0, 7 18/0, 8 71/0, 9 21/0,
  test_top_q3_client 23276/0, test_application_open 85/0), test_configuration 133/0, test_d2_drift 96/0, test_d9b_drift
  47/0, test_d9d_drift 6/0, test_main_composition 54/0, test_startup_port 457/0, tests/console (111, 230/0, 48/0),
  test_component_command_owner 8/0, test_app_concrete_original_save 9/0, test_q2_original_saves 109/0,
  test_sim_verify 10 cases 0 mismatches, test_remote_loopback 54/0 (nine families 0 differing; stdin closed),
  test_application_open_real 12/0 (q1/q2/q3 0 differing; stdin closed). A first run of test_q2_original_saves failed
  4 scenarios with "SDL audio driver cannot report exact queued input frames at this sample rate" while another agent's
  audio open-error change (src/audio/engine.c, src/app/bootstrap/audio.c) was mid-edit; relinked with HEAD's four audio
  sources it passed 109/0, and the full run above passed on the working tree. The last change (the drain frees the
  dispatched line and source when a handler raises) came after that run: test_app_gate_review10 350/0 under
  ASan/UBSan/LSan, and relinked from a private archive (q2_console.c did not compile, another agent mid-edit):
  test_app_gate_review5 43/0, _8 71/0, test_component_command_owner 8/0, test_configuration 133/0, tests/console (111,
  230/0, 48/0), test_startup_port 457/0, test_sim_verify 10 cases 0 mismatches; the non-ASan review10 relink crashed
  in its cvar snapshot read because include/qa/core/cvars/index.h changed (another agent, 18:26) after the archive's
  cvars object was built (header skew, not a test result).
- app/bootstrap top-level plan packet 2 (remote-application + remote seats, server-browser, server-administration) PARTLY
  (gate-app3, 2026-11-11, build/agents/gate-app3). Read in full against v1.2.7: server-administration.ts,
  server-browser.ts, server-browser-cache.ts, server-browser-addresses.ts, remote-seat-source.ts, remote-seat-view.ts,
  remote-seats.ts, remote-seat-pump.ts, remote-seat-identities.ts, remote-components.ts (about 136 functions);
  remote-application.ts partly (step, sourceSessionActions, the remote-seat frame/poll/retire members, run, close,
  closeOwned: 13 members). 23 fixes, rows "gate-app3" in port_review.tsv. The worst: the server operator state kept a
  pointer to application_open.c's stack ConfigStore, so every addip/removeip wrote the filter profile through freed
  memory (the state now owns its store); every prepared remote seat view leaked its seat UI, and a unified remote with
  character "q3" never loaded the Q3 character; the QW baseskin getter shared the userinfo slot (a userinfo read freed
  the baseskin the presentation held); rateUnsigned kept the sign of a capped magnitude; remote seat identities, seat
  channels and the pump lost TS's AggregateErrors and error classes (RangeError for TS Error); poll bounds measured
  Latin-1 status names as UTF-8. Open items settled: the QW host's authentication is getters read before each
  connectionless packet (network/qw_server.h/.c; simulation/network_qw.c reads the cvars per call); the QW/Q2 server
  binding options take TS's masters function, called each heartbeat (simulation/network.h, network_qw.h, network.c,
  network_qw.c, network_q2_guest.c, network_q2_rerelease_native.c, caller application_services.c); a released remote
  prepared profile retracts its configuration continuation first (remote_application_config.c
  qa_remote_release_profile[_slot], gate-app2's pattern). Review test tests/app/test_app_gate_review3.c with
  tests/app/oracles/app_gate_review3.ts (20 checks: filter profile writes, profile/rcon/setmaster error classes, the
  rcon rate parser, unbounded lrcon messages; 9 of them fail against the old server_administration.c), clean under
  ASan/UBSan/LSan. Not reached from a test: the QW password getters, masters call-through, identities/channel/pump
  error paths, seat view UI/character, baseskin slot, closeOwned initial components, successor map equality, poll byte
  bounds (need a UDP status fixture), profile retraction.
  Open (not fixed): server-browser rows() lowercases/localeCompares Latin-1 byte-string status names as UTF-8 while the
  menu filter is UTF-8 and cache/KEX statuses are UTF-8 (mixed representation in qa_server_status); a saved direct
  remote holding "\u0000" is truncated by the C string and accepted; ConfigStore.dump failures in the operator state
  (saveFilters/writeip) are ignored where TS rejects; the Q3 master list resolves synchronously (TS defers).
  Seen outside the scope (not edited): src/app/bootstrap/application_open.c:953 hands the operator state a stack
  ConfigStore it destroys at once (now harmless: the state copies it).
  LEFT in remote-application.ts: every member not listed above (see its port_review.tsv row). FINISHED by gate-app6
  (next entry); the four open items above are settled there.
  Results as run (gate-app3 tree): test_app_gate_review3 20/0 (clean under ASan/UBSan/LSan), test_app_gate_review_1 28/0, _2 18/0, test_appnet_gate_review 22/0,
  2 28/0, 3 7/0, test_remote_application 346/0, test_remote_application_open 4/0, test_remote_seat_source 110/0,
  test_server_browser 9396/0, test_world_seat_presentation 215/0, test_app_concrete_seat_ui 6/0, test_top_c 66/0,
  test_sim_network 176/0, test_application_deps 227/0, test_q2_app_units 1500/0, every tests/network binary (37) 0 failures,
  test_remote_loopback nine families 0 differing (54/0), test_application_open_real q1/q2/q3 0 differing (12/0),
  test_sim_verify 10 cases 0 mismatches.
- app/bootstrap top-level plan packet 2 (remote) DONE (gate-app6, 2026-11-11, build/agents/gate-app6): the rest of
  remote-application.ts read in full against v1.2.7 (the 127 members gate-app3 left; with its 13 every member is read;
  C in src/app/bootstrap/remote_application*.c). 17 fixes in remote-application, rows "gate-app6" in port_review.tsv.
  The worst: prepareRemoteSeats closed and destroyed a seat's transport twice when a raise came after the seat source
  took it; bindSeat queued its candidate's actions through queueCommand, which attaches the default execution context to a
  sourceless action and derives its seat (TS pushes them as recorded); dropRemoteSeat refused a successor when both maps
  were absent; bindRemoteSeat left the view open on a failure inside TS's inner try and let a failed close bypass the
  quiet-retirement check; saveSourceSettings ignored a failed settings/client.cfg write; dispatchCommands leaked the
  rest of the batch on a raise; the Q2 userinfo closure read the current configuration.source (TS the constructor's);
  prepareRemoteConfiguration's AggregateError messages carried appended texts.
  gate-app3's open items, settled: server-browser rows() widens Q1/QW/Q2 byte-string statuses to UTF-8 before
  toLowerCase and the filter, and sorts name/map with bun's base-strength primary weights for U+0000-U+00FF (table made
  under bun; above U+00FF the foundation approximation stays); a saved direct remote holding "\u0000" is rejected;
  addip/removeip raise Error when the profile write fails (the list change stays, removeip prints nothing) and writeip
  rejects with its host; Q3 master resolution: observable (a host name's masters reach the heartbeat a step later in TS),
  so the literal prefix of the names resolves in the refresh and the rest from the first name needing a lookup at the next
  refresh, masters applied when all are done. Also: setMasters and refreshQ3Masters now catch a raising resolver per name
  and print "Bad master address" (qa_net_resolve_address raises; the C command aborted), masterFailure keeps its class.
  Kept collapsed (documented): advanceSource's frame-boundary suspension of a peer directory admission (C publishes it
  at once); recorded-source adapters (remote_application_recorded.c) belong to recorded-source.ts.
  Review test tests/app/test_app_gate_review6.c with tests/app/oracles/app_gate_review6.ts (72 checks: rows sort/filter
  over UDP NetQuake statuses with Latin-1 names, direct NUL remote, failing filter writes, Q3 master timing and prints),
  clean under ASan/UBSan/LSan. Not reached from a test: the remote-application fixes (seat transport ownership, bindSeat
  actions, drop/bind seat paths, saveSourceSettings write, constructor Q2 userinfo, configuration messages).
  Seen outside the scope (not edited): src/app/bootstrap/cvar_archives.c:183 (qa_app_save_cvar_archive),
  view_settings.c:158 (qa_app_view_settings_save) and keys.c:47 (store_dump) ignore a failed qa_config_store_dump where
  TS saveCvarArchive / ApplicationViewSettings.save / the key profile store reject; application_services.c
  operator_resolve's "Unable to resolve address" is only reached on OOM (the resolver raises); src/foundation/js_case.c
  collation stays approximate outside ASCII (server-browser details() rule sort and every other localeCompare).
  Results as run (gate-app6 tree): test_app_gate_review6 72/0 (clean under ASan/UBSan/LSan), test_app_gate_review3 20/0,
  _1 28/0, _2 18/0, test_appnet_gate_review 22/0, 2 28/0, 3 7/0, test_remote_application 346/0,
  test_remote_application_open 4/0, test_remote_seat_source 110/0, test_server_browser 9396/0,
  test_world_seat_presentation 215/0, test_app_concrete_seat_ui 6/0, test_sim_network 176/0, test_application_deps
  227/0, test_top_c 66/0, every tests/network binary (37) 0 failures, test_remote_loopback nine families 0 differing
  (54/0), test_application_open_real q1/q2/q3 0 differing (12/0), test_sim_verify 10 cases 0 mismatches.
- app/bootstrap top-level packet 3 DONE (gate-app4, 2026-11-11, build/agents/gate-app4). Read in full against v1.2.7:
  startup.ts (58 members, C in startup_application*.c), startup-selection.ts, startup-saves.ts, frontend-preferences.ts.
  Already read and skipped: console-log, native-settings, native-migration, seat-profile, q2-native-saves (gate-app1, read
  in full), startup-menu.ts and addon-library.ts (startup2 rows "read in full"; their d9b drift is ported). 27 fixes (rows
  "gate-app4" in port_review.tsv). The worst: StartupSaves.resolveExternalSavePath scanned the ".." entry, so a load could
  pick <parent>/<name>.sav outside the external root, and took a directory named <name>.sav for a save; path(id) took
  the last row with a duplicated id; namedPath counted bytes and let any non-ASCII byte through (now 1-48 code points of
  \p{L}\p{N} from a bun table); refreshBaseArenas dropped the previous arena selection when a refresh failed; a raising
  StartupSelectionModel constructor leaked the model and its catalog; close and five other sites appended causes to TS's
  AggregateError messages; the remote print status kept 512 bytes after an ASCII trim (TS: JS trim, 512 UTF-16 units).
  Open item settled: QuakeC give lowercases its argument as TS does over console text (Latin-1 fold; quakec_source.c
  host_give). Review test tests/app/test_app_gate_review4.c with tests/app/oracles/app_gate_review_4.ts; the give check
  is in tests/app/test_quakec_source.c.
  Found outside the scope (not edited): saveFrontendInput's new-seat document gets "rumbleStrength": 1 where TS omits the
  key (settings/config.c qa_seat_settings has no presence flag); src/app/bootstrap/input.c:388 and ui.c:264 read the SDL
  clipboard as raw bytes where TS decodes it with TextDecoder (settled by gate-app7, next entry).
- app/bootstrap top-level packet 3b DONE (gate-app8, 2026-11-11, build/agents/gate-app8). Read in full against v1.2.7 the
  startup-*.ts files no packet had read: startup-audio (18 members), startup-commands (8; its four binding/variable
  helpers live in prepared_startup.c and configuration.c, read and equal), startup-config (10), startup-input-profile (5),
  startup-source (2), startup-timing (1). With gate-app4 (startup.ts, startup-selection, startup-saves,
  frontend-preferences), startup2 (startup-menu, addon-library) and gate-app1, every startup-*.ts file is read. And fixed
  the divergences gate-app4 kept. Rows "gate-app8" in port_review.tsv; about 50 fixes. The worst:
  - map entity text was parsed as NUL-terminated raw bytes: a NUL inside a Q2 quoted value or a Q3 block ended the
    lump (TS reads past it), invalid UTF-8 was not U+FFFD, and the Q2/Q3 token limits and error positions counted bytes.
    Now TextDecoder text parsed over UTF-16 units by a port of the three grammars (src/app/bootstrap/startup_entity_text.c);
  - seat documents: a new one carried "rumbleStrength": 1 and every one was written in the parser's key order at every
    level. qa_seat_settings now has the absence state and the TS object's key order, and the gamepad curves, gyro and
    mouse tuning carry theirs (default literal, parser, MouseSettings.read()), so new and rewritten files match TS byte
    for byte (src/ui/settings/index.c pad_curve_write marks the kind change's new literal; outside the packet, one line);
  - settings writes: every failure said "Could not write X"; writeAtomic now keeps the Node fs error (Bun's recursive
    mkdir path, the "<path>.<uuid>.tmp" temporary, rename's "'a' -> 'b'") for qa_settings_raise_write_failure, used by the
    startup, frontend-preference, seat-profile and renderer writes. loadSeat/loadText read a directory as a document and
    quarantined it to .bad, and took an unreadable file for an absent one (Bun.file().exists() is false for a
    directory; text() rejects with EACCES);
  - StartupConfig.executeFrame kept only its own stage failure, as RangeError; TS keeps any thrown error and throws it
    again; readStartupCommand (C callers: Team Arena demo play, library commands) passed UTF-8 bytes where TS refuses a
    character above U+00FF; startup-source/-commands Errors raised RangeError;
  - monster and Team Arena labels capitalize as JS toUpperCase; model, skin, movie and music track lists sort by UTF-16
    units (qa_js_compare_code_units in foundation/js_case); the Nightdive save directories are looked up on every add-on
    change; the startup error texts returned through 2047-byte buffers (stageLobbyLaunch, lobby join, demo source seeds,
    player progress, startup-menu and addon-library caught messages, the controls print) are whole.
  Decision: failActiveSource prints "Session ended by an error: <name>: <message>" ("<name>" for an empty message), the
  first line of Bun's error.stack; the "    at <file>:<line>" frames that follow in TS name TypeScript source and have no C
  counterpart.
  Review test tests/app/test_app_gate_review8.c with tests/app/oracles/app_gate_review8.ts (entity text, labels, sort,
  startup commands, seat document bytes, write errors, StartupConfig's kept failure).
  Not reached from a test: failActiveSource, the Nightdive lookup, the static-buffer sites (they need a running
  StartupApplication), startup-source and startup-timing.
  Found outside the scope (not edited): startup script reads that raise go straight out of the command buffer, where TS
  catches the rejected read, prints "couldn't exec <name>: <message>" and rethrows from onScriptComplete
  (prepared_startup.c read_script_adapter and configuration.c profile_read_script return READY; core/commands'
  qa_script_read_result carries a message, no class); the audio engine's open/select results lose the SDL error text, so
  startup-audio openOutput prints "Audio output is unavailable" (audio/engine.h, platform/audio.h); input_platform.c:935
  and :956, input_devices.c:319 and audio_settings.c:137 still raise "Could not write X" instead of
  qa_settings_raise_write_failure().
  Results as run (gate-app8 tree): test_app_gate_review8 71/0 (clean under ASan/UBSan/LSan), test_app_gate_review4 31/0, test_startup_port 457/0, test_startup_graphics 17/0, test_startup_menu 38/0, test_addon_library 93/0, test_app_concrete_seat_ui 6/0, test_remote_seat_source 110/0, test_world_seat_presentation 215/0, test_app_concrete_original_save 9/0, test_q2_original_saves 109/0, test_quakec_source 72/0, every tests/settings binary (7) 0 failures, test_input 286/0, test_input_gate_review 80/0, test_js_case 1058/0, test_platform_a/_b/_gate_review 0 failures, test_remote_loopback nine families 0 differing (54/0), test_application_open_real q1/q2/q3 0 differing (12/0), test_sim_verify 10 cases 0 mismatches.
- app/bootstrap top-level packet 4 PARTLY (gate-app7, 2026-11-11, build/agents/gate-app7). Read in full against v1.2.7:
  cvar-archives.ts, view-settings.ts, keys.ts, q3-client/status.ts, userinfo.ts, view.ts, overlay.ts, collision.ts,
  guest-collision.ts, cvars.ts, qvm-display.ts, visibility.ts, equipment.ts, audio/output-settings.ts, playlist.ts,
  playlist-settings.ts, commands.ts, menu.ts, q3.ts; presentation-state.ts partly (owners, retirement, record/emit,
  capture, restore of owners; see its row). Over the whole packet (presentation, presentation-state, presentation-scene,
  effects, effects/*, q3-client, q3-client/*, ui, input, renderer, assets, audio, audio/*, mod-presentation(s),
  character-turntable): every literal TS throw matched to its C raise by class and message (script), and every fixed
  text buffer. About 75 fixes (rows "gate-app7" in port_review.tsv), 45 of them error classes (43 RangeError for TS Error
  in renderer.c, q3_client/source.c, assets.c, cvars.c, overlay.c, collision.c, guest_collision.c, services.c, status.c,
  visibility.c, effects/particles.c). Open items settled: save_cvar_archive, view_settings_save and the key store's dump
  now reject a failed write through qa_app_config_store_dump (cvar_archives.h: Bun's recursive-mkdir error with Node's
  message, Error "<strerror>: could not write <name>" for a later step, whose TS message names a random temporary) and
  free what they built; the SDL clipboard in input.c and ui.c is decoded with TextDecoder (gate-app4's item). The worst:
  view settings' binding label was freed by the next binding() (three callers keep one: use after free) and never
  followed the field of view; keys' markModifiedFlags context was one static for the process (a profile marked the last
  prepared profile's cvars, then a destroyed one), prepare leaked on a raise, and the authorization was rebuilt per
  publish, dropping the authority address TS keeps (network/q3/client_authorization.h gains optional cvars_of/keys_of
  getters; zero-initialized callers unchanged); Q3 cinematic, sound, background-track and model-default paths were cut at
  1-2 KB; presentation.c and effects.c raised a truncated ring error after the ring had raised (copies leaked); the
  local Q3 snapshot took the first of a repeated entity number (TS Map: the last). Review test
  tests/app/test_app_gate_review7.c with tests/app/oracles/app_gate_review7.ts (18 checks: archive bytes and
  ENOTDIR/EEXIST failures, view save failure, bindCvars class, live labels after two binding() calls, keys error classes,
  the per-profile mark, a failed key write; 12 fail against the HEAD keys.c/cvar_archives.c/view_settings.c), clean under
  ASan/UBSan/LSan. Not reached from a test: the clipboard, the authorization address, the error-class fixes outside
  keys/view (renderer, Q3 client), the cinematic messages and caps, the visibility, qvm-display and audio fixes.
  Found outside the scope (not edited): application_core.c qa_application_save_source_archive/save_client_archives leak
  their entries and store when the archive write now raises (gate-app5 files); the seat UI keeps the shared registry
  pointer (ui.c qa_ui_seat_preferences_init) where TS's sharedSettings() follows publishSharedCvars (the registry the
  transition publishes is the persisting image-settings one, so no stale read was found); settings/config.c
  qa_config_store_load_text treats an unreadable existing file as absent (TS rejects).
  LEFT (not read line by line): presentation.ts, presentation-scene.ts, the rest of presentation-state.ts, effects.ts,
  effects/particles.ts, q2-muzzle.ts, q2-view.ts, q3.ts, q3-client.ts, q3-client/assets.ts, cinematics.ts (beyond
  prepare), client-state.ts, qvm-scalars.ts, qvm.ts, scene.ts, services.ts (beyond the track), source.ts, ui.ts,
  input.ts (beyond the clipboard), renderer.ts (beyond FrameCaptures), assets.ts, audio.ts, audio/music.ts, q2-events.ts,
  mod-presentation.ts, mod-presentations.ts, character-turntable.ts; and "the rest" of the top level, not read by any
  packet: authored-start, base-arena-catalog/-menu/-postgame/-progression/-selection/-select-menu, cached-promise,
  campaign-cinematic, campaign-unit, client-bootstrap, component-bodies/-client-save/-commands/-drawings/-media/-scene,
  console, controller-settings, finale, frame-clock, game-prompt, gtv-commands, held-weapon, image-reader,
  input-devices, loading, local-lobby, local-seat-change, match-modes, match-preflight, menu-art, menu-font,
  model-loader, mod-selection, native-held-weapon, native-q2-client, player-death, player-progress(-library),
  player-userinfo, precache, q1-client-commands, q1-client-settings, q1-fog, q1-localization, q1-service-presentation,
  q1-session-actions, q1-source-cvars, q1-wheel, q2-client-commands, q2-console, q2-damage-blend, q2-localization,
  q2-match-reports, q2-match-ui, q2-native-hud, q2-travel, q3-map-command, q3-product, q3-rankings, q3-selected-weapon,
  qvm-grapple-selection, renderer-diagnostics, render-settings, rerelease-presentation (+ rerelease-presentation/fog),
  save-requests, seat-factory, seat-hud-state, selected-q3-presentation, server-master-list, sound-captions, splash,
  team-arena-demo/-overrides/-results/-scores/-skirmish, video-restart, weapon-behavior-selection/-tool-options/-tool,
  weapon-hud, weapon-view, audio-settings; demo-commands, demo-playback, demo-recording(-commands) (read by gate-app10) and startup-audio/-commands/-config/-input-profile/-source/-timing (gate-app4's startup* area?) are unassigned
  as well.
  Results as run (gate-app7 tree; linked from a private archive while startup_config.c/startup_entity_text.c were
  mid-edit by another agent): test_app_gate_review7 18/0 (clean under ASan/UBSan/LSan), test_app_gate_review_1 28/0,
  _2 18/0, 3 20/0, 6 72/0, test_mod_presentation 11/0, test_presentation_state 32970/0, test_top_turntable 230/0,
  test_top_effects 183/0, test_top_presentation 34/0, test_top_assets 194/0, test_top_input 110/0, test_top_ui_audio
  138/0, test_renderer_content 173/0 (gl probe unavailable), test_top_q3_client 23276/0, test_world_seat_presentation
  215/0, test_app_concrete_seat_ui 6/0, test_q2_original_saves 109/0, test_application_open_real q1/q2/q3 0 differing
  (12/0, stdin closed); on the final sources test_sim_verify 10 cases 0 mismatches and test_remote_loopback nine
  families 0 differing (54/0, stdin closed).
- app/bootstrap top-level packet 4, presentation part (gate-app9, 2026-11-11, build/agents/gate-app9). Read line by line
  against v1.2.7 (control flow, ownership, iteration order, numeric semantics, evaluation order): presentation.ts,
  presentation-scene.ts, the rest of presentation-state.ts (local media, resources, replicated owners,
  receive/take/drain, restore's persistent rows, light styles), effects.ts, effects/particles.ts, q2-muzzle.ts (table
  compared value for value with bun), q2-view.ts, q3.ts, mod-presentation.ts, mod-presentations.ts,
  character-turntable.ts. Rows "gate-app9" in port_review.tsv; 20 fixes:
  - SourceParticles' four explosions added base + (rand % 32 - 16) where TS adds (base + rand % 32) - 16 (last-bit
    origins for a non-integer base); q1ColorExplosion accepted a -Infinity length; q2ColorExplosion with a zero run
    trapped on an integer division by zero (JS: NaN color); q2TrackerTrail used C hypot;
  - ApplicationWorldScene.style read NaN for a NaN or infinite prepared time (charCodeAt(NaN) reads index 0);
  - the component (mod) scene's effect frame did not light its models with the scene's lights (TS input.q3Lights);
    component readScript/readCommandScript returned raw bytes where TS decodes with TextDecoder (now console text, cut
    at a NUL, RangeError "Command text requires source bytes" above U+00FF, as application_commands.c does);
  - eleven AggregateError messages in mod_presentation.c/mod_presentations.c carried the inner errors (TS's exact text
    now); restoreOwned failed a changed declaration on the field's path (TS: the record's) and an absent one as changed
    (TS: SaveFormatError "checkpoint contains a non-data value"), staged the HUD color through float and checked
    command duplicates before reading every name; CG_R_REMAP_SHADER and CG_GETGAMESTATE read in another order than TS;
    isDeepStrictEqual took 0 and -0 as equal;
  - raise-path leaks: effects (a full pending ring, the rest of a taken batch), presentation sourceEvents (fog and UI
    arrays), mod presentation frame (taken HUD submissions), remap strings, the cvar archive path when loadText raises.
  Open item settled: ConfigStore.loadText already rejects an existing file it cannot open with Bun's Error
  "EACCES: permission denied, open '<path>'" in the working tree (gate-app8) and treats a stat failure or a directory
  as absent, as Bun does (checked with bun); every caller was checked for TS catch behavior and raise-path ownership.
  Review test tests/app/test_app_gate_review9.c with tests/app/oracles/app_gate_review9.ts (21 checks: loadText
  EACCES/absent cases and loadCvarArchive's rejection, scene style at NaN/Infinity/negative/0.25, five particle
  digests, the infinite colored explosion, the zero Q2 run, the monster muzzle table): against HEAD particles.c and
  presentation_scene.c 8 checks fail and the zero-run check dies on SIGFPE.
  Not reached from a test: the AggregateError texts, restoreOwned, the remap/game-state order, the component scene
  lights, the script decoding, the deep-equal zero, the leaks, q2TrackerTrail's hypot.
  Kept (noted in the rows): the component HUD assertCurrent compares the generation copied at prepare (C prepare is
  synchronous); the turntable's Q3 shader catalog is not cached when a script open raises (TS caches the rejection);
  the turntable key joins with "\x01" (TS "\0").
  Found outside the scope (not edited): application_tools.c camera_read leaks the store root and image_settings.c
  qa_app_image_settings_open leaks the constructed settings when loadText raises (gate-app5 files);
  application_commands.c's source script read converts a decoded U+0000 (C0 80) into RangeError where TS
  sourceCommandText cuts the text at the NUL (gate-app5); AggregateError messages with appended details remain in
  selected_q3_presentation.c:59/66/391, input_platform.c:159 and video_restart.c:291/305; a restored or live Q1 finale
  text is cut at 4095 bytes by qa_q1_intermission_result.text[4096] (content/q1/base/rules.h).
  Still LEFT in packet 4 (unchanged from gate-app7's list apart from the files above): q3-client.ts,
  q3-client/assets.ts, cinematics.ts (beyond prepare), client-state.ts, qvm-scalars.ts, qvm.ts, scene.ts, services.ts
  (beyond the track), source.ts, ui.ts, input.ts (beyond the clipboard), renderer.ts (beyond FrameCaptures), assets.ts,
  audio.ts, audio/music.ts, q2-events.ts, and "the rest" of the top level listed in the gate-app7 entry.
  Results as run (gate-app9 tree, seeded from gate-app8's): test_app_gate_review9 21/0 (clean under ASan/UBSan/LSan),
  test_presentation_state 32970/0, test_top_presentation 34/0, test_top_effects 183/0, test_mod_presentation 11/0,
  test_top_turntable 230/0, test_world_seat_presentation 215/0, test_app_gate_review7 18/0, test_top_ui_audio 138/0,
  test_top_q3_client 23276/0, tests/settings: test_lmctf_cvars_tail 19/0, test_native_config 99/0, test_settings 147/0,
  test_settings_drift9 37/0, test_settings_gate_review 93/0, test_settings_layers 288/0, test_settings_rows 32/0;
  test_sim_verify 10 cases 0 mismatches; test_remote_loopback nine families 0 differing (54/0, stdin closed);
  test_application_open_real q1/q2/q3 0 differing (12/0, stdin closed). test_q2_original_saves 33/4: classic,
  level-change, sidecar and engine-logic raise Error "SDL audio driver cannot report exact queued input frames at this
  sample rate" from the in-flight src/app/bootstrap/audio.c change (check_selection now raises a failed
  selectOutput under SDL_AUDIODRIVER=dummy; another agent's edit, 16:09); a scratch build with that one raise removed
  gives 109/0.
- app/bootstrap top-level packet 4, the rest (gate-app11, 2026-11-11, build/agents/gate-app11). Read line by line against
  v1.2.7: q3-client.ts; q3-client/assets, cinematics (beyond prepare), client-state, qvm-scalars, qvm, scene, services,
  source; audio.ts, audio/music.ts, audio/q2-events.ts (tables dumped against bun); renderer.ts beyond FrameCaptures;
  assets.ts (but createWorldAssets and the model loader bridges). About 40 fixes (rows "gate-app11" in port_review.tsv).
  The worst: Q3 REAL_TIME's DST flag compared with the smaller of the January/July offsets and was 0 in every DST period
  (now qa_app_qvm_real_time_fields); MEMORY_REMAINING read freeram (Bun: MemAvailable); the application audio output
  selections (format, restart, device) ignored a failed open where TS throws; music's trim, cue tokenizer and cd
  lowercasing were ASCII, every music line was cut at 1023 bytes, and a numbered cue already playing with no CD track
  returned early; the Q3 loose-mount walk followed symbolic links and skipped unreadable directories (Bun rejects with
  "<CODE>: ..., open '<dir>'"); generated font exports used fopen without the UserFileStore parent directories; skins,
  sound environments, animation.cfg and the QVM clipboard were raw bytes (TS TextDecoder); ApplicationQ3Client.create
  left assertCurrent/bindFrameTime outside its close-on-failure scope; the QVM client and local input retirement
  AggregateErrors appended details to the TS message; the local source took the last of repeated clients (find: first);
  cinematics' pendingLoads stayed raised after a failed open; the renderer never restored the serial GL context after a
  prepare, discard or retirement, published with a presentation captured late, and left its image registry open.
  Open items settled: input_platform.c seat/routing, input_devices.c and audio_settings.c raise
  qa_settings_raise_write_failure(); the audio engine keeps its last failed open's SDL message
  (qa_unified_audio_open_error), printed and raised by startup_audio.c openOutput/selectOutput and audio.c (the C has
  only the SDL2 backend: "SDL_OpenAudioDevice: <SDL_GetError()>", TS's SDL2 text; TS on SDL3 names "Audio output is
  unavailable: <name>"). A selection failing only SDL2's exact-queue probe ("SDL audio driver cannot report exact queued
  input frames at this sample rate", every resampled rate under the dummy driver; TS's SDL3 stream passes it) keeps the
  restored output instead of raising, since the C cannot tell whether TS would throw (coordinator item: it took
  test_q2_original_saves to 33/4). Review test tests/app/test_app_gate_review11.c with
  tests/app/oracles/app_gate_review11.ts (1869 checks: the q2 event tables with one shared random sequence, REAL_TIME
  fields in six zones, music/cd text, the audio.json write failure, the engine's open message); 5 fail against the HEAD
  music.c/audio_settings.c, and the HEAD DST expression gives 0 where TS gives 1 (Sydney January, Berlin July, Santiago
  January); clean under ASan/UBSan/LSan. Not reached from a test: the q3-client create scope, QVM/input AggregateError
  messages, the loose walk, font export, TextDecoder sites, cinematics, source find, renderer context/presentation/close,
  MEMORY_REMAINING, assets' shader-path lowering and scandir message.
  Found outside the scope (not edited): startup_application_graphics.c:545 and :547 output_select_body ignore
  qa_unified_audio_select_output's result (TS selectOutput throws and the menu reports it); remote_application_cinematic.c:107
  raises "Audio output is unavailable" (qa_unified_audio_open_error now has the text); audio_settings.c
  qa_app_save_audio_settings formats into a 4096-byte buffer whose append_json_string keeps writing past it (cap - len
  underflows) for a long menu track or device name; settings/config.c qa_config_store_dump/save_seat return false on an
  allocation failure without clearing the last write failure (a stale fs error can be raised); renderer.c's restart
  candidate opens resizable at display -1 (TS copies the previous window's resizable flag, display index and position).
  LEFT (not read line by line): ui.ts (input, sample, receive, captionCommands and close read; prepare, weaponOcclusion,
  prepareNativeQ2Hud, refreshImages, prepareImageRefresh, wheel, switchWeapon, cycleWeapon not), input.ts (beyond the
  clipboard and saveSettings' write sites), assets.ts createWorldAssets and the model loader bridges.
  Results as run (gate-app11 tree, final sources): test_app_gate_review11 1869/0 (clean under ASan/UBSan/LSan),
  test_app_gate_review7 18/0, test_top_q3_client 23276/0, test_top_ui_audio 138/0, test_top_input 110/0, test_top_assets
  194/0, test_renderer_content 173/0 (no GL), test_world_seat_presentation 215/0, test_app_concrete_seat_ui 6/0,
  test_q2_original_saves 109/0, test_audio 71/0, test_audio_engine 301/0, test_audio_fixes 8 ok, test_audio_gate_review
  2576/0, test_audio_gate_review2 2550/0, test_audio_rows 7/0, test_input 286/0, test_input_gate_review 80/0,
  test_sim_verify 10 cases 0 mismatches, test_remote_loopback nine families 0 differing (54/0, stdin closed),
  test_application_open_real q1/q2/q3 0 differing (12/0, stdin closed).
- app/bootstrap top-level packet 4, the last of it (gate-app13, 2026-11-11, build/agents/gate-app13). Read line by line
  against v1.2.7: ui.ts refreshImages, prepareImageRefresh, prepare, weaponOcclusion, wheel, switchWeapon, cycleWeapon,
  prepareNativeQ2Hud (with nativeQ2Arsenal, nativeSourceLocalizer, prepareNativeInventory); input.ts beyond the clipboard
  and saveSettings' write sites (input.c, input_commands.c, input_platform.c; console_chat is gate-app10's); assets.ts
  createWorldAssets and the model loader bridges (against model-loader.ts). Rows "gate-app13" in port_review.tsv; 15 fixes:
  - ui: prepareNativeQ2Hud loads the content's localizer (its catalog) once before assertCurrent as TS awaits it (C
    loaded it lazily per text inside the layout, after assertCurrent, and only when a text needed it); the rendering
    environment's ctx lived on the prepare's stack while the HUD keeps the rendering (a relayout after a failed first
    prepare read it: use after return); leaks of the weapon icons and the scr_usekfont read on raise; an uncommitted
    image refresh leaked (new qa_app_seat_ui_image_refresh_discard, called from presentation.c refresh_release).
    ui.h options.native_hud_localizer is now content => qa_app_rerelease_localizer (one caller, application_seat_ui.c).
  - input: the constructor's try { attachWindow; restart; profiles } catch { close owned controllers; throw } had no C
    counterpart (now the input is dropped, closing what it created); create() and prepareLocalSeats leaked what they had
    read when a profile/archive load or the constructor raised; saveNativeConfig leaked on a raising write.
  - assets: model_decode leaked the parsed MD3 when toSceneMd3 raised; q2Replacement's diagnostic was cut at 4095 bytes.
  - gate-app11's open items: audio_settings.c audio.json now grows with its text, byte for byte with bun (a long device
    name wrote past the 4096-byte buffer; the HEAD file segfaults on the new test) and frees the loaded strings when a
    validation raises; startup_application_graphics.c raises a failed device or format selection (the menu reports it;
    the format closure no longer writes the cvars after a failure; the SDL2 exact-queue message keeps the restored output,
    as audio.c); remote_application_cinematic.c raises the engine's open error; renderer.c's restart candidate copies
    the previous window's resizable flag, display index and position (GL only, not reachable here). settings/config.c
    dump/save_seat already clear the last write failure on entry in the working tree (verified, every save_* does).
  Review test tests/app/test_app_gate_review13.c with tests/app/oracles/app_gate_review13.ts (8 checks: audio.json bytes
  and length for a short document, a 250-unit menu track, a 5 KB and a 15 KB device name with quotes, backslashes,
  control characters, U+007F and non-ASCII). Not reached from a test: every ui/input fix, the assets fixes, the menu
  selection, the cinematic message, the restart window.
  Kept (open): input.sharedSettings() returns the registry at the call where TS returns a view following
  publishSharedCvars (gate-app7's item: a seat UI built before a candidate image publication keeps the old registry;
  fixing it needs a SettingCvars view across ui/settings); buildForSeat's arsenal provider/weapon point into the
  selection storage until the next build or selection change (TS holds the selection object).
  Not read (outside the packet's list): ui.ts constructor, draw, drawNativeQ2Hud, centerPrint, clearPrompt, closeMenus.
  Found outside the scope (not edited): none beyond the kept items above (presentation.c's refresh_release now also
  discards the seat UI refresh; renderer.c, startup_application_graphics.c, remote_application_cinematic.c and
  audio_settings.c were edited as gate-app11's open items).
  Results as run (gate-app13 tree, seeded from gate-app11's): test_app_gate_review13 8/0 (clean under ASan/UBSan/LSan;
  linked with the HEAD audio_settings.c it segfaults), test_top_input 110/0, test_top_ui_audio 138/0, test_top_assets
  194/0, test_top_q3_client 23276/0, test_world_seat_presentation 215/0, test_app_concrete_seat_ui 6/0,
  test_app_gate_review7 18/0, test_app_gate_review11 1869/0, test_startup_graphics 17/0, test_startup_port 457/0,
  test_remote_application 346/0, test_q2_original_saves 109/0, test_audio 71/0, test_audio_engine 301/0,
  test_audio_fixes 8 ok, test_audio_gate_review 2576/0, test_audio_gate_review2 2550/0, test_audio_rows 7/0, test_input
  286/0, test_input_gate_review 80/0, tests/settings: test_lmctf_cvars_tail 19/0, test_native_config 99/0, test_settings
  147/0, test_settings_drift9 37/0, test_settings_gate_review 93/0, test_settings_layers 288/0, test_settings_rows 32/0;
  test_sim_verify 10 cases 0 mismatches; test_remote_loopback nine families 0 differing (54/0, stdin closed);
  test_application_open_real q1/q2/q3 0 differing (12/0, stdin closed).
- app/bootstrap ui.ts, the rest, and gate-app13's open items (gate-app15, 2026-11-11, build/agents/gate-app15). Read line
  by line against v1.2.7: ui.ts's constructor (every closure, the settings rows and their order, menu registration
  order, the pause menu), draw, drawNativeQ2Hud, centerPrint, clearPrompt and closeMenus; ui.ts is now read in full
  (gate-app11, gate-app13, gate-app15). Rows "gate-app15" in port_review.tsv; 7 fixes:
  - draw returned early when no menu is open; TS still calls renderUiCommands with an empty list, which emits its
    white set-color (a frame with no menu open lacked it);
  - sharedSettings follow-through (gate-app7/gate-app13's item): TS's input.sharedSettings() is a view that follows
    publishSharedCvars; the C seat UI kept the registry current at construction. The transition
    (application_transition.c: staged input built with the candidate image registry, publish switches to
    app->image_settings) and the remote candidate (remote_application_seat.c: input built with c->image's candidate
    registry, publish switches to app->image_settings) both left the seat preferences, the image/model/console,
    native-video, audio-geometry and music bindings and the r_smp worker reading and writing the unpublished candidate
    registry. Now: ui/settings/index.h qa_ui_setting_cvars (a plain registry or a view read at each call) with *_view
    entry points (bindCvarSetting, image, model, console, native video, audio geometry, music playlist, SeatUiPreferences;
    the registry entry points wrap them, so no other caller changed); input.h
    qa_app_application_input_shared_settings_view (absent when there is no owner, otherwise each read takes the current
    owner and raises Error "Shared settings lost their owner" when it is gone); ui.c builds every one of those through
    the view. No application_*.c edit was needed. Remaining stale reads: the rerelease presentations created by
    application_transition.c:2052, application_open_graphical.c:465 and ui.c native_source_localizer take the registry
    current at creation (TS: input.sharedSettings(), the view), so after a world transition their seat-language read
    and the language binding's write use the candidate registry; rerelease_presentation.h takes no view and the file is
    gate-app14's (fix: a qa_ui_setting_cvars parameter, read through qa_ui_setting_cvars_get at
    rerelease_presentation.c:319/324/561, and the three callers passing qa_app_application_input_shared_settings_view);
  - a built command's arsenal provider/weapon (gate-app13's item) pointed into the seat's selection storage, freed by
    the next setArsenalSelection; remote_application_step.c sets the selection every frame and the unified client keeps
    up to 64 unacknowledged inputs (and the predictions) by value and re-encodes them, so retransmits read freed text.
    input_platform.c now hands out interned process-lifetime copies (TS: the command holds the immutable strings);
  - component HUD re-layout (gate-app13's item): the native HUD environment ctx was one per seat UI, so a renderer whose
    prepare raised before its first result laid out again with whichever prepare ran last (its localizer and font);
    TS's renderer keeps the environment its own last prepare received. Now one ctx per renderer, set only when that
    renderer's prepare is reached;
  - a raising constructor (attachUi's Error "Seat UI is already attached", a settings bind) leaked the half-built UI;
    now dropped through destroy (close/destroy tolerate members the constructor never reached);
  - local-join used capacity < 4 ? capacity : 4 (a NaN capacity enabled it; TS Math.min gives NaN, disabled);
  - draw, drawNativeQ2Hud and captionCommands leaked their command lists, captions and prompts when a sink raised
    (captionCommands' own material sink raises).
  Review test tests/app/test_app_gate_review15.c with tests/app/oracles/app_gate_review15.ts (10 checks: the empty-menu
  draw, preferences before/after publishSharedCvars and the write target, the second constructor's error, the arsenal
  text after a selection change). The oracle runs the real ApplicationSeatUi over a stub input whose sharedSettings,
  publishSharedCvars and attachUi are ApplicationInput's own methods. Against the old draw, preferences and build_one
  6 checks fail (the arsenal provider reads freed memory); with the old constructor LSan reports the leaked UI. Clean
  under ASan/UBSan/LSan. Not reached from a test: the per-renderer environment, the NaN capacity, the draw-path leaks.
  Found outside the scope (not edited): the rerelease presentation's view (above, gate-app14's file);
  ui/settings/images.c and console.c leak their owner when a bind raises "Settings cvar has no owner".
  Results as run (gate-app15 tree, seeded from gate-app13's; linked from a private archive while src/llm/auth_worker.c
  was mid-edit by another agent): test_app_gate_review15 10/0 (clean under ASan/UBSan/LSan), test_top_ui_audio 138/0,
  test_top_input 110/0, test_world_seat_presentation 215/0, test_app_concrete_seat_ui 6/0, test_remote_application
  346/0, test_remote_seat_source 110/0, test_app_gate_review7 18/0, test_app_gate_review11 1869/0,
  test_app_gate_review13 8/0, tests/ui: test_js_semantics_float2int 5/0, test_ui 352/0, test_ui_controller_fixes 32 ok,
  test_ui_controls 31/0, test_ui_fixes 15 ok, test_ui_gate_review 33/0, test_ui_gate_review_legacy 53/0, test_ui_rows ok,
  test_ui_synthetic 20 ok; test_q2_original_saves 109/0 (it ran alongside test_sim_verify, test_remote_loopback and test_application_open_real: a moved-to-background run, a breach of the one-heavy-test rule); test_sim_verify 10 cases 0 mismatches; test_remote_loopback nine families
  0 differing (54/0, stdin closed); test_application_open_real q1/q2/q3 0 differing (12/0, stdin closed).
- app/bootstrap top-level "the rest", first half DONE (gate-app12, 2026-11-11, build/agents/gate-app12). Read line by line
  against v1.2.7 (none had a "read in full" row): authored-start, base-arena-catalog/-menu/-postgame/-progression/
  -selection/-select-menu, cached-promise (no C file), campaign-cinematic, campaign-unit, client-bootstrap,
  component-bodies/-client-save/-commands/-drawings/-media/-scene, console, controller-settings, finale, frame-clock,
  game-prompt, gtv-commands, held-weapon, image-reader, loading, local-lobby, local-seat-change, match-modes,
  match-preflight, menu-art, menu-font, model-loader, mod-selection, native-held-weapon, native-q2-client, player-death,
  player-progress, player-progress-library, player-userinfo (40 files; rows "(gate-app12)" in port_review.tsv; about 60
  fixes). The worst:
  - base_arena_menu.c and base_arena_select_menu.c returned menus with no controls (ui.c and startup_menu.c register
    them): the Q3 single-player arena picker, difficulty, progress, reset and result menus are now built as in TS;
  - player_progress_library.c handed entries() its row array with the wrong stride (every row after the first read
    garbage); the local lobby let a service raise ("Lobby no longer exists", "Cannot join this lobby", ...) propagate
    out of the UI where TS shows the rejection, kept pointers into the service's reallocating lobby array, and skipped
    start()'s publish-failure cleanup (complete, leave, AggregateError);
  - base-arena-progression compared -1 with its -1 null sentinel for training/final; base-arena-catalog kept the first
    of a repeated key, did not split bots on U+00A0, kept a malformed arena file's earlier rows and aborted on a parser
    RangeError instead of skipping the file;
  - component-scene passed the Q3 visibility selector entity records holding only the number (portal generic1/origin2
    lost); component-bodies keyed materials with unquoted printf text ("null" equalled no shader; NaN vs Infinity);
  - player-progress opened a directory as empty progress and ignored mkdir failures (now the Node messages) and parsed
    raw bytes (TS decodes with U+FFFD; a BOM fails JSON.parse); menu-font scanned localization text without
    TextDecoder (a BOM added U+FEFF coverage and fallback pages); finale reused another content's banner size and
    dropped the prepared banner on a new finale; campaign-cinematic tested only the last dot for an extension and
    accepted a save without "pcm"; campaign-unit, local-seat-change, console, gtv, match-preflight, frame-clock,
    component-client-save/-media and base-arena raised RangeError for TS Error (about 40 sites); local-seat-change's
    prepareSeat raise left the client open; game-prompt cached a zeroed catalog after a failed load (TS keeps the
    rejection); fixed buffers removed (authored-start, catalog paths, skin sidecars, mod keys, opponents/limits).
  Public changes: finale.h draw_text takes a double glyph budget (presentation.c, gate-app9's, updated);
  qa_app_source_finale_content returns the prepared content; base_arena_select_menu.h view text is borrowed pointers;
  local_lobby.h last_lobby is an owned snapshot and the struct gains `error`. Review test
  tests/app/test_app_gate_review12.c with tests/app/oracles/app_gate_review12.ts (39 checks: catalog keys/bots/read
  skipping/error classes, progression null slots, the five arena menus read through the controller's draw text, a
  superseded campaign visit, body material identities, GTV and clock error classes, progress store directory/open/mkdir
  errors, BOM, U+FFFD rewrite, library rows, appearance trim, lobby rejections), clean under ASan/UBSan/LSan (it found a
  leaked field name on the catalog's RangeError path, fixed). Not reached from a test: authored-start,
  campaign-cinematic, finale, menu-font, component-scene, model-loader, mod-selection, held/native-held,
  local-seat-change, match-preflight, console, controller-settings, game-prompt. Not measured how many checks fail
  against HEAD.
  Open items (not fixed): campaign-cinematic writes initialAudio blocks in readAudio key order (TS keeps each decoder's
  order); client-bootstrap's account map is keyed by identity pointer (TS WeakMap); loading services the window only
  on nextFrame (TS at least once); controller-settings' failed gyro save reports strerror (settings/config.c; fixed by gate-persist3 in controller_settings.c); outside
  the scope: src/text/atlas.c qa_text_codepoints accepts overlong/surrogate UTF-8 where TextDecoder replaces.
  Results as run (gate-app12 tree): test_app_gate_review12 39/0 (ASan/UBSan/LSan clean), test_app_gate_review_1 28/0,
  _2 18/0, 3 20/0, 4 31/0, 5 43/0, 6 72/0, 7 18/0, 8 71/0, 9 21/0, 10 350/0, 11 1869/0, test_console_a 111 passed,
  test_console_b 230/0, test_console_review 48/0, test_app_concrete_cinematic 5/0, test_app_concrete_seat_ui 6/0,
  test_match_results ok, test_top_a 49/0, _b 50/0, _c 66/0, test_top_assets 194/0, test_top_effects 183/0,
  test_top_input 110/0, test_top_presentation 34/0, test_top_turntable 230/0, test_top_ui_audio 138/0,
  test_top_q3_client 23276/0, test_q2_original_saves 109/0, test_sim_verify 10 cases 0 mismatches,
  test_remote_loopback nine families 0 differing (54/0, stdin closed), test_application_open_real q1/q2/q3 0 differing
  (12/0, stdin closed).
- app/bootstrap top-level "the rest", second half PARTLY (gate-app14, 2026-11-11, build/agents/gate-app14). Read line by
  line against v1.2.7: precache, q1-client-commands, q1-client-settings, q1-fog, q1-localization,
  q1-service-presentation, q1-session-actions, q1-source-cvars, q1-wheel, q2-client-commands, q2-console,
  q2-damage-blend, q2-localization, q2-match-reports, q2-match-ui, q2-native-hud, q2-travel, q3-map-command, q3-product,
  q3-rankings, q3-selected-weapon, qvm-grapple-selection, renderer-diagnostics, render-settings, rerelease-presentation
  (+ /fog), save-requests, seat-factory, seat-hud-state, selected-q3-presentation, server-master-list, sound-captions,
  splash, team-arena-demo/-overrides/-results/-scores/-skirmish, video-restart, weapon-behavior-tool-options,
  weapon-hud, weapon-view, audio-settings (but gate-app13's JSON writer), input-devices. No file had a "read in full"
  row; none skipped. Rows "gate-app14" in port_review.tsv; about 110 fixes, ~45 of them error classes (TS Error raised
  as RangeError). The worst:
  - vid_restart and the six renderer diagnostics read argv[0] (the command name) as their first argument: every
    vid_restart printed its usage and never restarted; every shaderlist was sorted;
  - seat-hud-state: the held-scores help lines were freed before presentation() returned (use after free); a removed
    POI left its strings in the vacated slot and the next appended POI freed them again (double free);
  - q2-match-ui: the menu factory read one process-wide static (a second seat's menu showed the other seat's entries),
    kept the admin-settings event's borrowed strings past the event, leaked every entry/id/label per menu and per
    factory call, and tokenized command.split(/\s+/u);
  - process-wide static closure contexts shared by every instance: q1 sizeup/sizedown, the q2 console's
    quit/map/say forwards, q3 rankings' accountActions (slot % 64);
  - q1-localization's classicQ1Text was a stale 41-row copy of classicQ1Messages (item, weapon, key and fade texts
    fell through to the obituary text); q2-console dumpuser took the last matching player, repeated userinfo keys and
    byte-padded keys; rerelease-presentation view/drawStory took the last seat; team-arena campaign took the first of a
    repeated section and listed repeated team keys twice; Team Arena scores overflowed a signed multiply (Math.imul);
    a master-list line holding U+0000 was cut and accepted;
  - raise-path leaks: q2 travel parse (console input), team-arena campaign parse (untrusted metadata), cloneQ1SourceCvars
    (the captured state was never freed at all), rerelease/seat-hud prepare batches (also: TS drops the rest of a taken
    batch on a throw), grapple selection, native HUD, rankings close (TS try/finally skipped lifecycle.end()).
  Open items settled: (1) selected_q3_presentation.c and video_restart.c AggregateError messages are TS's exact text;
  (2) qa_q1_intermission_result.text is a whole borrowed pointer (rules.h): finale table/key strings, the addon world
  endtext kept by the addon context until its next finale (was a 4096-byte stack copy), the simulation event shape
  copies it (types.c STR), presentation_state.c restore and unified_event_values.c read point into their trees (map
  stays char[64]); (3) q1-wheel decodes wwheel.txt with TextDecoder; (4) world/gameplay/inventory.c pickup bindings
  are counted: the store's reference plus one per resolved match; a match's new release hook
  (contracts/original_pickups.h qa_current_original_pickup.release; authority.c zeroes its matches) is called when
  original_pickups.c drops a scope's matches, so unbind and respawn rebinds free them (the table kept every binding and
  every per-resolve current() state until destroy); bind_pickup frees its captured rules and items when a check raises;
  (5) qa_ss_q1_set_identity releases its userinfo copy when the client update raises; (6) arsenal/q2.c item labels
  point at the table/definition names and the weapon status label is a whole heap string kept until the next ui().
  Also fixed outside the listed files (readers of the above): content/q1 rules.c, addons/campaign.c + context.h/.c,
  missionpacks/world/campaign.c, app/bootstrap/presentation_state.c, network/unified_event_values.c,
  simulation/types.c, world/gameplay/authority.c and original_pickups.c.
  Review test tests/app/test_app_gate_review14.c with tests/app/oracles/app_gate_review14.ts (31 checks: q1 host actor
  errors and slot list, save/load/travel error classes, q1ViewRectangle NaN, TA score path/imul, TA campaign
  sections/teams/errors and demo, tool options, master-list NUL lines over a Bun.serve fetch, vid_restart and
  shaderlist arguments).
  Not reached from a test: the static-context fixes, q2-match-ui, seat-hud-state, q1-localization's table and catalog
  caching, rerelease-presentation, the inventory binding lifetime, the finale text, the leaks.
  Open (not fixed): q2-console dumpuser's surrogate-pair padEnd counts are per UTF-8 sequence. [Settled by gate-clean2:
  the team-arena campaign's Latin-1 bytes are TS's code units, which is what cvars and the addbot command text hold in
  C, so they are right there; the UI choices now convert to UTF-8 and a chosen team back (startup_selection.c); TS's
  COM_Parse cursor ends at the first NUL as the C read does; the input-devices JSON text and the inventory.c
  messages are fixed.]
  Found outside the scope (not edited): application_commands.c:514 formats operator names through a 2048-byte scratch
  (gate-app10's file); weapon_status.c (q1/q3 status labels) still writes into the caller's 64-byte label rows. [Both
  fixed by gate-clean2.]
  LEFT (not read): weapon-behavior-selection.ts and weapon-behavior-tool.ts. [Read in full by gate-clean2.]
  Results as run (gate-app14 tree; linked from a private archive while src/app/bootstrap/ui.c was mid-edit by
  gate-app13): test_app_gate_review14 31/0 (clean under ASan/UBSan/LSan), test_top_a 49/0, _b
  50/0, _c 66/0, test_top_assets 194/0, test_top_effects 183/0, test_top_input 110/0, test_top_presentation 34/0,
  test_top_q3_client 23276/0, test_top_turntable 230/0, test_top_ui_audio 138/0, test_q2_app_units 1500/0,
  test_q3_source ok, test_weapon_behaviors ok, test_weapon_behavior_tool 20/0, test_world_seat_presentation 215/0,
  test_sim_equipment 376/0, test_sim_c 333 passed, test_shared_simulation_save 101/0, test_quakec_source 72/0,
  test_sim_gate_review_a2 21/0, a5 14/0, b3 30/0; tests/world test_gate_world_render 32/0, test_world_actors 72/72,
  test_d7_drift 12/0, test_world_leaks 22/0, test_gate_world2 46/0; tests/content test_q1_d3_drift 85/0,
  test_q1_gate_review_mp 62/0, test_q1_missionpacks 155 passed, test_q1_sync 212/0, test_q1_sync_base 261/0,
  test_q1_quakec 181, test_q1_foundation_tail 102/0; tests/compat test_qc_hosts_depth 46/0, test_qc_core_depth 4927/0,
  test_qc_mods_depth 65/0, test_q2_native_primary 2068 passed; test_header_audit_b 50 passed;
  test_q2_original_saves 109/0, test_sim_verify 10 cases 0 mismatches (and --diagnostics 10 cases 0 failed),
  test_remote_loopback nine families 0 differing (54/0, stdin closed), test_application_open_real q1/q2/q3 0 differing
  (12/0, stdin closed).
- app/bootstrap/network DONE (gate-appnet, gate-appnet2, gate-appnet3; gate-appnet, 2026-11-11, build/agents/gate-appnet): unified-client, unified-server,
  unified-control, unified-content, unified-components, unified-component-publication, q2-rerelease-hud-events,
  socks-settings, client-download-policy, q2-layout and q2-remote-view read in full; unified-frame-codec decode side in full; unified-event-codec settled by regenerating
  src/app/bootstrap/network/unified_event_codec.c from TS v1.2.7 with tools/gen_unified_event_codec (identical byte for
  byte once the templates took the three hand fixes already in the C) and reading the hand-ported helpers; targeted
  reads of unified-frame-values, unified-prediction, unified-component-consumer, unified-native-components,
  remote-unified, q3.ts, q2.ts, qw-server, remote-qw, remote-q1, remote.ts; over all 55 files: every literal TS throw
  matched to its C raise by message and class, last-match loops where TS uses find, fixed text buffers, byte-length
  limits, unsequenced initializer reads. 89 fixed sites, 55 of them error classes (rows in port_review.tsv): the 18 plain-Error throws of
  unified_client.c/unified_server.c plus 35 more in gtv-source, q2-demo, q2-effects, q2-mvd-presentation,
  q2-client-receiver, q3-client-content, q3-client-downloads, q3-demo, q3-downloads, recorded-source raised RangeError;
  unified server shutdown and component-consumer close now raise AggregateError (the consumer stopped at the first
  failing retirement); unified protocol, component-command and native-component strings counted bytes and accepted a
  held NUL (TS: UTF-16 units, NUL refused); a damage or transition simulation event from a server raised in the client
  view and cost the connection (TS passes it on; kept with its kind, outcome/decision not converted); unified server
  drop deleted a peer one poll late after a failed farewell, cut the reason at 1024 bytes, and let a failing
  host.disconnect escape unwrapped (TS UnifiedHostFailure); command-time and acknowledgement Math.max lost NaN; the
  client vtable submit timed every command after the first with 0 ms; content readDigest ran when TS short-circuits,
  and resolve read the first of a repeated resource id (TS Map: the last); actor references, vectors and bounds read
  their fields in unspecified initializer order; angles and axes checked the count before reading the items;
  disconnectClient in q3 and qw and the QW player lookup took the last match; the Q3 disconnect command and packet
  diagnostics and the Q2 client_connect reply had fixed caps; Q1/Q2 lightstyles indexed with a negative or NaN
  server time (undefined in C); leaks on raise in component reading and publication. Forced call site:
  src/app/bootstrap/simulation/network.c holds a copy of qa_q2_connection_result.userinfo across the session,
  identity and admission callbacks (network_unified.c and bot_world.c copy it at once). Settled: rerelease HUD events
  on the stack are deep-copied by both emit hosts; the Q3 userinfo 1023 cut counts bytes, which equal UTF-16 units
  because Q3 wire text decodes one character per byte. Review test tests/app/test_appnet_gate_review.c with
  tests/app/oracles/appnet_gate_review.ts (22 checks, clean under ASan/UBSan/LSan). LEFT (not read line by line):
  the rest of q2.ts, q3.ts, qw-server.ts, remote.ts, remote-q1.ts, remote-qw.ts, unified-prediction.ts,
  unified-frame-values.ts, unified-component-consumer.ts, remote-unified.ts; q2-downloads, q2-demo, q2-effects,
  q2-mvd-presentation, q2-service-presentation, q2-client-receiver, q1.ts, q1-client,
  q1-demo, q1-types, qw-client, qw-camera, qw-downloads, qw-skins, qw-types, qw-server-types, q3-client,
  q3-client-content, q3-client-downloads, q3-demo, q3-downloads, q3-types, remote-q3, remote-world, recorded-source,
  gtv-source, transport, types, unified-native-consumer, unified-types (error
  classes and the scans above only). Open: unified-content reads the offered recipe tree where TS digests readRecipe's
  parsed recipe (an offer with extra recipe fields digests differently); unified frame decode resolves entities in
  order where TS's Promise.all reports the first error to settle; the component publisher keeps borrowed provider
  strings across frames (needs host-stable storage); invalid Q2 filelist/model lines print raw bytes cut at 128 where TS
  prints the TextDecoder text cut at 128 UTF-16 units.
  gate-appnet2 (2026-11-11, build/agents/gate-appnet2) settled those four and read in full q2-downloads, q2-demo,
  q2-effects, q2-mvd-presentation, q2-service-presentation, q2-client-receiver, q1, q1-client, q1-demo, qw-client,
  qw-camera, qw-downloads, qw-skins, q3-client, q3-client-content, q3-client-downloads, q3-demo, q3-downloads,
  recorded-source, gtv-source, transport, remote-world, unified-frame-values (the rest), unified-component-consumer,
  the unified-prediction decode side, unified-component-publication and native projectNativeComponents; targeted
  unified-native-consumer and remote-q3 (rows in port_review.tsv). Settled: unified-content now composes and digests
  readRecipe's parsed recipe (qa_save_recipe_value; a present empty mods/weaponBehaviors list kept); unified frame
  decode replays the Promise.all job schedule over entities and attachments (checked against bun for 16 frames,
  taking each context callback to settle in one job as the remote-unified resource cache and brush models do); the
  component publisher keeps its own copies of what the next projection compares (and projectNativeComponents compares
  configstrings by content as TS sameConfigs does; it compared pointers, so every rebuilt snapshot was resent); the Q2
  HTTP filelist is decoded (BOM, U+FFFD) before it is validated and printed with a 128 UTF-16-unit cut, while model
  and refused-path lines are Latin-1 configstrings whose 128-byte cut is TS's. Other fixes: the publisher's projection
  free freed the borrowed native HUD stats (abort with a native Q2 HUD), a QWD packet with two serverdata messages
  double-freed, qw-skins freed skins still held by remote-qw selections, Q3 package/content open leaked on a raise,
  q3-demo cast a fractional or NaN realTime to int64 (TS RangeError), find-first in q1 disconnect and the native
  consumer, Q1 client handled messages after a disconnect, QW stufftext cmd fall-through, Q2 MVD view-origin addition
  order, frame readKey NUL, character view field order, mkdir/open errors as Bun's, and the cinematic init leaks in
  assets.c, campaign_cinematic.c and q3_client/cinematics.c. Review test tests/app/test_appnet_gate_review2.c with
  tests/app/oracles/appnet_gate_review2.ts (28 checks, clean under ASan/UBSan/LSan). LEFT (not read line by line):
  the rest of q2.ts, q3.ts, qw-server.ts, remote.ts, remote-q1.ts, remote-qw.ts, remote-q3.ts, remote-unified.ts, the
  unified-prediction encode/project side, the rest of unified-native-consumer, and the type-only files (q1-types,
  qw-types, qw-server-types, q3-types, types, unified-types). Open: remote_unified retains every component update of
  an epoch for the consumers' borrowed strings (unbounded in a long epoch); the C Q2 HTTP queue settles assets one at
  a time where TS starts them under Promise.all; MVD records after a close are still decoded (q2-demo, gtv-source)
  where the TS generator stops; a QW recording sink failure raises from submit at once (TS defers it to the next poll).
  gate-appnet3 (2026-11-11, build/agents/gate-appnet3) settled those four and read in full the rest of q2.ts (server
  and client), q3.ts, qw-server.ts, remote.ts, remote-q1.ts, remote-qw.ts, remote-q3.ts, remote-unified.ts, the rest
  of unified-native-consumer, the unified-prediction encode/project side and the six type-only files (no exported
  constants or enums besides the phase strings and the two game callback error classes, equal). Settled:
  remote_unified no longer retains updates (the native and component consumers own copies of what they keep:
  qa_app_unified_component_identity_clone and per-entry blocks); the Q2 HTTP assets are enqueued together and handled
  as they settle (resolved checks before any transfer, two transfers at once, first failure to settle rejects);
  qa_q2_mvd_playback_stop (src/network/q2/mvd_playback.c, allowed) lets the q2-demo and gtv-source sinks leave the
  generator as TS does; a QW recording write failure is kept and thrown by every later poll until a new attach.
  Other fixes (rows in port_review.tsv): use after free in the native consumer (a legacy camera entry kept the freed
  frame's strings; an update without configstrings copied from the block it had just freed), in the Q2 server
  recording (the owner kept the capture's config array the host frees at its next capture; seeds kept the capture),
  the unified predictor was never cleared on an admitted-actor change, remote-q3 passed the command's angle space,
  QW server ping sums in slot order, live-list iteration in say/pings and Q3 receiveEvents, peer.player identity by
  client id, an unhandled QW download dropped instead of reaching the command phase, rate "" as 500, find-first
  misses, Math.max NaN sites in remote/remote-q1, leaks on raise in remote-qw receive and QW changeWorld, an infinite
  maxclients accepted. Review test tests/app/test_appnet_gate_review3.c with tests/app/oracles/appnet_gate_review3.ts
  (7 checks: the MVD stop against bun, clean under ASan/UBSan/LSan). Not reached by a test: the consumer copies, the
  QW recording deferral, the HTTP batch, the QW/Q3/Q2 server fixes (the loopback runs the servers: nine families 0
  differing). Not added: the checks for gate-appnet2's QWD double serverdata, MVD view-origin order, QW stufftext
  fall-through and Q1 client disconnect fixes (still without a fixture). Open, outside the scope: a failed HTTP queue
  result keeps only its reason text (network/services), so an AggregateError reason is rethrown as Error;
  simulation/prediction/source_state.c drops the Q3 jump pad (TS sets jumpPad from actorAt).
- app/bootstrap plan (2026-11-11; 127k TS lines: simulation 54k, network 14.6k, top level 53.5k,
  q3-client 2.9k, effects 1.4k, audio 1k): network DONE (gate-appnet, gate-appnet2, gate-appnet3, see below); simulation in three
  packets (A: runtime/shared_simulation core, source strategy, step, checkpoint/save, players,
  worlds; B: guest hosts — classic/rerelease guest services/world/files, q3 engine/qvm/native,
  native mod host, mods; C: bots glue, physics, prediction, navigation, network sim, events,
  arsenal/equipment, monsters); top level in four packets (application*.ts 9.4k + configuration,
  prepared-startup, options, content, config-scripts; remote-application 5.1k + remote seat
  source/view, server-browser, server-administration; startup 3k + startup-menu, -selection,
  -saves, addon-library, frontend-preferences, native-settings/migration, console-log,
  seat-profile, q2-native-saves; presentation, presentation-state/-scene, effects, q3-client,
  ui, input, renderer, assets, audio, mod-presentation(s), character-turntable, the rest).
- Optimization priority (lead, 2026-11-11): src/render/image_journal.c:84 and src/render/gl/textures.c:85
  find each released image by linear search and shift the array; in test_q2_original_saves one world
  retirement spent minutes there at about 5 GB resident, close to the 6 GB test cap. Index by ordinal
  (TS uses a Map) before anything else in the optimization phase; it can make that test flaky.
- Cleanup packet (lead, 2026-11-11), items found by readers in areas already read; to run after the
  app/bootstrap reads: [DONE by gate-clean1 (see its entry): configstring byte strings at
  compat/qvm/mod_provider.c (now converted at the JSON boundary both ways; compat/q2/native_mod_provider.c:466
  is the rerelease branch, UTF-8 text, correct raw); world/collision/q3/topology.c error paths (qa_save_reader_fail);
  the platform IPX error buffer (src/platform/ipx_native.h, 320 bytes and 300/240-byte cuts; whole now);
  core/math.c qa_hypot3 NaN; q3_client/assets.c:499 (fixed by gate-app11: parseSkin uses qa_text_decoder_decode);
  condump/writeconfig rethrow writeAtomic's Node error; save_module_values.c flags (flags_value); the Q2 player
  name owned; execution.ts bigints outside int64 kept whole; native-config past a NUL; layer maps hashed;
  save_image.c's builders read line by line] [done by tests-b6: the render worker transport's qa_rwt_failure /
  qa_rwt_error messages are whole heap text with move/copy ownership (worker.c last_error and the wire encoder's
  deferred failure too); a failed screenshot/levelshot write raises writeAtomic's Node error (class Error, TS
  message), which the console prints as TS does]; [done by gate-persist3: saved cvar flags kept as the restored number, native
  imageOffset unsigned 64-bit, save_policy.c raising the failing step's Node error, source-items duplicate
  checks linear, controller-settings' gyro save failure text]; app q1_wheel.c:118 raw bytes where TS uses
  TextDecoder; llm
  auth_worker.c aborting on invalid worker data; llm api/auth/auth-worker/settings/models/request
  and camera/spline not yet read line by line;
  foundation/js_case.c localeCompare collation approximate above U+00FF (server browser details()
  rule sort and every other localeCompare; gate-app6 generated an exact table up to U+00FF);
  content/q3/product_restriction.c error class changed to Error by the lead (verify with
  test_q3_foundation, assigned to gate-app5; done, 145/0); src/materials/material.c:1928 -Wclobbered in the
  ASan (-O1) build (gate-simA7); qa_ss_q3_qvm_guest_view reads players[64] up to the returned total (bounded by
  Q3's 64-client maximum, recheck when removing caps); from gate-app12: campaign-cinematic saves pending audio
  blocks in another key order than TS (TS keeps each decoder's order), loading services the window only when the
  operation asks for a frame (TS at least once), client-bootstrap's account map keyed by identity pointer (TS
  WeakMap; check lifetime), src/text/atlas.c qa_text_codepoints accepts malformed UTF-8 that TextDecoder replaces;
  ui.ts constructor, draw, drawNativeQ2Hud, centerPrint, clearPrompt and closeMenus not read line by line;
  text/truetype and llm (api, auth, auth-worker, settings, models, request) and camera/spline still unread
  (assigned to gate-misc4 with the atlas, mouse_settings, imports argv, mod_presentations error_name,
  campaign-cinematic key order, loading and js_case collation items; all DONE by gate-misc4 except a truetype re-read,
  see its entry); from gate-app10: chat text above U+00FF
  on the three UTF-8 paths (rerelease Q2 argv, Q2 KEX client, unified control) arrives as its low byte's
  character because console text cannot hold it, and a unit whose low byte is 0 ends the text (TS writes the 0);
  a raise inside loadApplicationContent's world decode leaks that decode's storage; a main-mount lease that
  outlives qa_app_loaded_content_destroy points at freed content (callers release first today); the source
  script fallback after selected.readScript returns nothing differs slightly from TS's `??` [these three DONE by
  gate-clean2, see its entry]. For a cleanup C pass (lead, 2026-11-11): gate-clean2's item 8 (triage the "Open
  items found by porters" section entry by entry); Q2 console dumpuser padding counts surrogate pairs per UTF-8
  sequence; shared_simulation_arsenal.c:5054-5058 reads userinfo spectator/team through a 256-byte scratch and a
  64-byte team copy, cutting a long value TS compares whole; check every qa_q2_userinfo_get caller's scratch size
  (content/q2/rerelease/players.c userinfo_get callers). gate-clean2's claim that qa_q2_userinfo_get should be
  last-wins is wrong: TS q2Userinfo skips keys already set (first wins), as the C comment says.
- Cleanup packet 2 DONE (gate-clean2, 2026-11-11, build/agents/gate-clean2). Read in full against v1.2.7:
  weapon-behavior-selection.ts and weapon-behavior-tool.ts (rows "gate-clean2" in port_review.tsv). Fixed:
  readWeaponBehaviorRequest's class and /\s/ (U+00A0 in console text); configured options and configured-recipe
  raise leaks; select took the last match; choicesFromMounts freed the provider id the Q3/Q2 selections point at (use
  after free) and leaked its backing on a raise; the tool's 1535-byte error texts, JSON string output (NUL, lone
  surrogates, NUL-holding keys), lenient profile JSON (TS fatal TextDecoder), RangeError write failures (Node fs
  errors, randomUUID temporary, finally removal) and the classic-Q2 refusal order (main.c). Team Arena campaign:
  the Latin-1 bytes are TS's code units, right for cvars and addbot text; the startup UI choices convert to UTF-8 and
  a chosen team back; TS's COM_Parse ends at NUL too. input-devices and cvar archive JSON text share
  qa_app_cvar_json_string (UTF-8, \u0000, escaped lone surrogates). Whole texts: inventory.c and original_pickups.c
  messages, original pickup write keys, application_commands.c operator names (scratch sized from the userinfo),
  q1/q3 weapon labels (qa_app_*_weapon_label_size; the q1/q3 arsenals intern labels per weapon until destroy, the
  shared simulation's UI ring grows its label, the remote Q2 UI keeps its label where the returned status pointed at
  a stack buffer), q1_fail. Q3 arsenal item labels took the last pickup match (TS find); the Q3 status label suffix is
  sliced as TS does. World: see the "Gate full read still to do" item (authority per-hit storage, create() states,
  pickup-match states with reference-counted bound protections, late grant() throws, an item repeated in one rule's
  offered list, capture/resolve raise paths). content.c: the world decode releases its storage on a raise; destroy
  with a main-mount lease out defers the free to the last release; the source script reader no longer falls back
  when a defined selected readScript returns no text (TS `??` on a Promise). From the coordinator: rerelease
  presentations take the shared settings view (qa_ui_setting_cvars; TS input.sharedSettings()), so the seat
  language follows publication; ui/settings images.c and console.c free their owner when a bind raises;
  bot_rerelease.c lowercases console argv as Latin-1. render/assets item: already closed (destroy on release).
  Review test tests/app/test_cleanup_gate_review2.c with tests/app/oracles/cleanup_gate_review2.ts (19 checks).
  Not reached from a test: the tool's write/JSON paths, the content decode/lease/script fallback, the authority and
  pickup lifetimes, the late grant(), the team UI conversion, the label interning, the settings owner leaks.
  tests/app/pres_contracts/events.inc wrote the finale text with strcpy into the borrowed pointer gate-app14 made
  of qa_q1_intermission_result.text (a segfault); it assigns the text now.
  Results as run (gate-clean2 tree): every tests binary of world, app, render, settings, input and ui other than the
  four below exits 0 (155 binaries; test_pres_contracts 1950/0 after its fix), test_cleanup_gate_review2 19/0 (clean
  under ASan/UBSan/LSan), test_sim_verify 10 cases 0 mismatches (--diagnostics 10 cases 0 failed),
  test_q2_original_saves 109/0, test_remote_loopback 54/0 (every family 0 differing, stdin closed),
  test_application_open_real 12/0 (q1/q2/q3 0 differing, stdin closed).
  Open items section (item 8 of the packet): only the items named above were worked; the rest of "Open items found by
  porters" was not re-triaged entry by entry.
Then: unported TS tests, full-tree ASan/UBSan, audits, open items, connect main, optimize.

Owner decision (2026-11-12): all test infrastructure in this project is removed. The `tests/` folder, its bun
oracles, the Makefile test rules and docs/test_coverage.tsv are deleted, and porting the TS test suite is dropped.
Every "test_*" name, review test and result quoted in this document refers to files that no longer exist; the
results stand as records of what was run at the time. The confirmation gate's test conditions (every test binary
passes, sanitizer run over every test binary, C checks cover the TS tests) no longer apply. Remaining work is
source only: connect main to the real Application and SharedSimulation and delete the old path, then optimize.
Verification from here is by building, running the program, and ad hoc comparison against the TS engine with
scratch scripts kept outside the repository.

## Main connected (2026-11-12, agent "main-connect")

Step 4's code work is done: `main` reaches the real Application, StartupApplication, RemoteApplication and
SharedSimulation, and the concrete-host layer is deleted. Part-way through, the owner dropped run verification
from the scope (the program running is not a goal of this step); the runs listed below were made before that and
nothing was run after the last code changes except the build.

What `src/main.c` does now (TS `main.ts`, 934 lines, was 1804):
- `--splash-child W H` runs the splash child. SIGPIPE is ignored (TS drops output after EPIPE).
- `parseApplicationCommand` under `qa_try`: a usage error prints its message to stderr and exits 1 with no crash log
  (it aborted with a core dump before). `--content-root`, `--user-content-root` and `--server-profile` go through
  `qa_node_path_resolve1` (the host used to keep the argument text).
- version, help (the compiled-executable form: "bun run src/main.ts" replaced by the executable's name),
  weapon-behavior, list-content; then the splash (not for dedicated or hidden), the LLM settings directory and the
  one-time legacy migration (from the executable's folder only, `.legacy-migrated` marker, 0600, source unlinked; it
  used to copy from the working directory on every start), `LlmSettingsService.open` unless a dedicated non-menu run.
- host = { llm?, print } where print feeds the 256-chunk recent-output queue and stdout. Dedicated:
  `qa_application_open_application(options, host, NULL, NULL)`. Graphical: `qa_startup_application_open(options,
  host, NULL, command.kind)`. Splash closed, SIGINT/SIGTERM call requestQuit, `run()`, then `close()` in the TS
  finally (a close failure after a run failure is printed as "Shutdown after failure also failed", APP-006), then the
  LLM service is closed. RemoteApplication is opened by StartupApplication, as in TS.
- A raise anywhere after parsing is main's catch: splash closed, crash.log (now with `entry=` and the program
  arguments without argv[0], as `Bun.argv.slice(2)`), "<name>: <message>" on stderr, exit 1.
- main.c no longer builds anything itself except two host callbacks: the weapon-behavior tool's
  `open_product`/`close_product` (the TS body of runWeaponBehaviorTool before the action switch) and the splash
  window/child callbacks.

Deleted:
- `src/app/bootstrap/application.c`, `include/qa/app/bootstrap/application.h` (old `qa_app_application`).
- `src/app/bootstrap/simulation/runtime.c` (old `qa_app_sim_shared_simulation`, the concrete builder and
  `qa_app_sim_frame_plan_compute`, which had no caller left). `simulation/runtime.h` now declares only the ordered
  actor turns and the visited set (defined in shared_simulation_turns.c).
- `src/app/bootstrap/startup.c`, `include/qa/app/bootstrap/startup.h` (the former concrete-host shell).
- `src/app/bootstrap/remote_application.c`, `include/qa/app/bootstrap/remote_application.h` (old
  `qa_app_remote_application` host shell; no caller).
- main.c's `composition` layer (catalog/mount/recipe/launch adapters, the shim simulation host, the dedicated step
  loop, the named "has no concrete C composition" errors) and its private copy of the archive inspector.
- The host-callback layer in content.c and configuration.c that only the old main used: `qa_app_content_host`,
  `qa_app_configuration_host`, `qa_app_configuration_request`, `qa_app_load_application_content`,
  `qa_app_application_source_selection_of`, `qa_app_open_application_configuration_content`,
  `qa_app_open_remote_application_content`, `qa_app_application_preset`, `qa_app_application_configuration_preset`,
  the loaded/configuration content setters, `qa_app_configuration_dialect`, `qa_app_configuration_store`,
  `qa_app_legacy_configuration_options`, `qa_app_open_initial_configuration_content` and its `_for_catalog` form,
  the `host` fields and their fallback branches. `qa_app_load_application_content_native` lost its `guest_host`
  parameter (9 call sites passed a zeroed one).
- The `qa_app_sim_shared_simulation` typedef (types.h): `prepare_rerelease_navigation` takes the
  `qa_app_shared_simulation` and the three casts are gone.
- Stale ledger rows for the deleted files (admission allow-list 1, eval-order ledger 1, js-semantics 9); manifest rows
  for application.ts, remote-application.ts and startup.ts name the real files.
- A scan of every prototype in `include/` against the built objects finds no declaration without a definition.

Numbers (build/agents/main-connect, `BUILD=build/agents/main-connect tools/reachability.py`; the tool takes the
build directory from `BUILD` now):
- Reachability before: 2044/2162 objects (the "487 of 2071" figure is older than the Wave 8 composition work).
  After: 2042/2158. The count barely moves because the old path already pulled most objects in through the same
  libraries; what changed is which top layer runs. 116 objects are unreachable: 78 define no symbol (barrels and
  type-only modules), 38 do. TS's own import graph from main.ts reaches 2069 of 2138 modules; its 69 unreached are
  type-only.
- `tools/admission_audit.sh src include`: 32 before, 0 after.
- Build: `tools/agent_build.sh main-connect all`, 0 warnings, 0 errors, archive rebuilt from scratch.

Fixes made while the real path was being run (all complete, all in the tree):
- world/collision/index.c `qa_shared_query_actors`: a NULL role is the TS default "both" (segfault opening any Q2
  rerelease map through rerelease bot navigation).
- application_close.c: a borrowed client's presentation was freed by Application.close and again by the detached
  world's cleanup (use after free when StartupApplication returned to the frontend).
- prepared_startup.c: an adopted ApplicationConsoleRouting is borrowed, as the header says; the prepared startup
  destroyed it on the next adoption and at close while its creator also freed it (double free at
  StartupApplication destroy). application_transition.c frees the headless routing with its record.
- application_travel.c `qa_application_round_server_settings`: kept and later freed the array the Q3 client already
  frees (double free on the sixth graphical Q3 frame).
- content/q3/base/settings.c: the tracked-cvar print sent a literal backslash-n ("Server: fraglimit changed to
  10\nRanger entered the game" on one line).
- console/dedicated.c `qa_dedicated_console_pump_stdin`, called before the drain in application_step.c: the TS
  DedicatedConsole listens on process.stdin; in C nothing fed the console, so a dedicated server ignored typed
  commands. Written after the runs below; compiled, never run.
- application_transition.c: a NULL content load reports "Out of memory" like its siblings (it named a missing
  loader).

Runs made before the scope change (C binary and `bun ../quake-anthology-ts/src/main.ts` under tools/run_capped.sh,
stdin closed, `--content-root ~/qfiles --seed 1`; compared stdout, stderr without stack lines, exit status):
- Dedicated, 60 and 300 frames: q1-classic-id1 e1m1, q1-quakeworld dm4, q2-classic-baseq2 base1,
  q2-rerelease-baseq2 base1, q3-baseq3 q3dm1; 100 frames: q1-classic-hipnotic hip1m1, q1-classic-rogue r1m1,
  q1-rerelease-id1 e1m1, q2-classic-xatrix xswamp, q2-classic-ctf q2ctf1, q2-rerelease-ctf q2ctf1: identical output,
  exit 0 both, except the timing note below. q2-classic-lmctf lmctf01: both exit 1 with the same stdout; stderr
  differs in 2 lines that were not looked at.
- Graphical with `SDL_VIDEODRIVER=dummy --hidden --renderer cpu --frames 20 +set s_outputRate 44100`: Q1, QW, Q2, Q2
  rerelease, Q3 and `--menu` open the StartupApplication, launch the game, run 20 frames, return and close, exit 0
  both, same output (the autosave line differs only in the throwaway HOME path). `--renderer gl`: both exit 1 at
  window creation ("SDL could not create any source GL visual candidate"); there is no GL context on this machine,
  so nothing past the GL window was run.
- --help, --version, a usage error, weapon-behavior --help, a bad --splash-child: same output and status, apart from
  the two expected lines below.

Runtime differences seen and not fixed:
- Dedicated stdin commands (`status`, `echo`, `quit` piped in): C ignored them and ran until the timeout; TS printed
  the status and quit. The pump above is the intended fix and has not been run.
- `--frames N` counts steps and each step takes wall-clock elapsed time, so runs are not comparable by frame count:
  300 dedicated Q3 frames reach "ClientBegin: 0" in TS and not in C; at 3000 frames (C 13.4 s, TS 25.9 s) TS logs one
  more item pickup. Not shown to be a logic difference.
- q2-classic-lmctf dedicated: 2 differing stderr lines, not read.
- The failure text: C prints "<name>: <message>"; bun prints the stack (for the GL AggregateError only "Error" and
  17 "[aggregated] Error" entries). C has no stack, cause or aggregated list.
- `--help` names the executable in C (the TS compiled-binary branch); `--version` prints 1.2.7 where the TS tree at
  HEAD (v1.2.7 plus the version bump commit) prints 1.2.8-dev.
- Never run: q1-classic-ad, q3-missionpack, q3-classic-lrctf, `--preset q2-q1-q3` and `q1-q2`, a level change, every
  `--listen*` server, every `--connect-*` client (RemoteApplication), demos, saves, split screen, the GL renderer.

Open:
- The 38 unreachable objects that define symbols. Checked: the AAS compiler set (aas_reachability*, aas_cluster,
  aas_optimize, aas_write: 9 objects) and network/common/loopback, render/cpu/commands, formats/q3_model/lod,
  formats/images/q3_pcx and q3_bmp, persistence/q2_classic, q2_rerelease, q2_typescript and source_json,
  network/q2/prediction, content/q3/foundation/events have no caller in TS `src` either. Not checked, and TS does
  call the TS function: app/bootstrap/simulation/bot_arsenal (`createBotArsenalBinding`, used by bots.ts and
  bot-world.ts), world/session/mod_callback_inputs (`modCallbackInputs`, compat mod validation),
  content/monsters/expansions and q2_expansions (definitions.ts), console/source_field (`EditField`, console/field.ts
  and the legacy menu). The rest of the 38 were not looked at. List: `tools/reachability.py --list`.
- Function-level dead code inside reachable objects was only removed where the old main was the sole caller (the
  list above). 4213 of 20005 global symbols are referenced by no other object; many are used inside their own file,
  so that number is an upper bound. A `-ffunction-sections`/`--gc-sections` link would give the exact list.
- The weapon-behavior tool still takes `open_product`/`close_product` from main.c; TS does that inside
  runWeaponBehaviorTool.
- "Done means" items that depend on running the program (side-by-side state hashes, mixed recipes, GL) are not met.

Decisions recorded by the lead (2026-11-10):
- content/roots.c default own directory: the library default is the working directory, as a
  TS run from source; src/main.c passes the executable's directory (`exe_directory()`), as a
  compiled TS binary does (roots.ts `ownDirectory`). No change needed.
- Rerelease `.pak.gz`: Bun's gzip bytes come from zlib-ng (zng_deflateInit2, default level,
  window 31, memLevel 8 reproduces them); C links stock zlib, whose deflate body differs
  (215 vs 228 bytes for the test archive). Header, trailer and the inflated pak are
  identical, so either side reads the other's file. Left as is unless the owner wants
  byte-identical gzip streams, which would add a zlib-ng dependency and tie the bytes to
  its version.

Four agents were stopped mid-task. Their edits may be partial, so build and run sim_verify
and test_remote_loopback before relying on the tree. To resume, restart these jobs:

- D1d (build/agents/d1d-sim), resumed 2026-11-10: done. Its three leftovers were finished by D9c (2026-11-10,
  build/agents/d9c): network-q2-guest-core.ts is network_q2_guest_core.{h,c} (one copy; both guest network hosts
  forward to it), qvm-component-state.ts is qvm_component_state.{h,c} with the TS error texts through `label`, and
  diagnostics.c is checked by `test_sim_verify --diagnostics` against bun. See the D9c item under "Open items".
- startup3: done before the stop, per its "Ported 2026-11-03 (startup3)" notes below, and
  confirmed on the committed tree 2026-11-10: test_startup_port links and passes 457 checks
  with the corpus. Still to do: build and run tests/app/test_startup_graphics once the tree
  links again; the lobby composition producer (buildUnifiedComposition -> qa_composition_identity);
  SDL3-versus-SDL2 audio open at 22050 Hz. The TS drift in startup*.ts is ported (packet D9b, 2026-11-10).
- gate-q1b (build/agents/gate-q1b): resumed 2026-11-10. Done: content/q1 quakec (14 files and
  the C-only compat_types.c), equipment (2), the 15 top-level addons files and addons/ctf
  grapple.ts and travel.ts; the entity string, backpack and spawn-point caps are gone.
- gate-q1c (build/agents/gate-q1c), 2026-11-10: done. The rest of content/q1 addons read in full:
  ctf (11 files), horde (4), items (7), monsters (45, all subdirectories); see port_review.tsv.
- gate-q1mp (build/agents/gate-q1mp), 2026-11-10: done. content/q1/missionpacks read in full (76 files:
  16 top-level, monsters 37 including the 14 frame tables, world 23); 14 divergences fixed, see
  port_review.tsv and the gate item below. Nothing is left to read in content/q1.
- gate-q2a (build/agents/gate-q2a), 2026-11-10: content/q2/foundation, 40 of 46 files read in full (host,
  entity-services, checkpoint, callbacks, fields, runtime, motion, angular-motion, items, movers, targets,
  scenery, shadow-lights, start-items, held-weapons, the two ammo files, effect-resources, weapon-attachments,
  all 13 files of weapons/, and monsters ai, soldier, infantry, gibs, alternate-fly, alternate-fly-state,
  actions, types); 60 divergences fixed there and one in world/actors/registry.c, see port_review.tsv and the
  gate item below. Stopped for context. Still to read in content/q2/foundation: monsters/index.ts (66
  functions; two pattern fixes only), monsters/perception.ts (29 functions), monsters/checkpoint.ts (types
  against the C header), and the tables muzzle.ts, frames.ts, moves.ts. The other content/q2 subtrees (base,
  missionpacks, rerelease, multiplayer, equipment) were not read by gate-q2a.
- gate-q2c (build/agents/gate-q2c), 2026-11-10: done. The six foundation files gate-q2a left are read in full
  (monsters/index.ts 66 functions, perception.ts 29, checkpoint.ts types; muzzle.ts, frames.ts and moves.ts equal a
  bun dump value for value: 502 muzzle offsets, 1588 named ids, 79 moves, 1447 frames), so content/q2/foundation is
  read in full. content/q2/multiplayer is read in full (21 files, 252 functions by a declaration count). 52
  divergences fixed: 16 in monsters/index, 5 in perception, 29 in multiplayer, and the two open foundation items
  (observeSupply's shared ammo buffer, the released train corner's pathtarget); see port_review.tsv and the gate
  item below. Checks: tests/content/test_q2_gate_review_c.c (3754 checks; bun oracles
  tests/content/oracles/q2_gate_review_c.ts and q2_monster_tables.ts/.txt/.inc), clean under ASan/UBSan/LSan.
- gate-render3 (build/agents/gate-render3) and gate-render4 (build/agents/gate-render4), 2026-11-10: model
  shadows, shaders, models prepare/renderer, material registrations, all of world, q3 particles, the patch
  block classes, gl/textures and cpu (rasterizer, triangle-kernel, the row-parallel pool lifecycle) are read (see the render
  row of port_review.tsv).
- gate-render5 (build/agents/gate-render5), 2026-11-10: done. The rest of render is read (gl except textures,
  worker, worker-protocol, worker-transport, worker-runtime, the row-parallel/row-worker wire): render is read in full,
  864 functions, 135 fixes. See the render row of port_review.tsv.
- gate-q2mp (build/agents/gate-q2mp), 2026-11-10: done. content/q2/missionpacks: the 71 TS files not already
  marked read in full are read (top level 7, weapons/definitions, projectiles/index, entities index/types/rogue, modes 3, monsters
  32 behaviour files and the 24 tables); 34 divergences fixed, see port_review.tsv and the gate item below. The
  nine files content-q2mp had already read in full (entities movers and xatrix, the five projectile files,
  spheres, weapons/player) were not re-read; they were checked only for the patterns the foundation reader
  reported (nearby loops, try/finally, cut text), which gave two more fixes there.

- gate-q2r (build/agents/gate-q2r), 2026-11-10: content/q2/rerelease. Read in full: every top-level file
  (players, triggers, entities, index, goals, campaign, lights, spawns, killbox, environment, view, fog,
  obituary, world-text, debug-shapes, types, checkpoint), q64/index, and of monsters: common, index, beam,
  boss, spawn-placement, arachnid, base-variants/actor, and all 25 tables (23 frame/move tables, flashes,
  missionpack) value for value against a bun dump. 29 divergences fixed, see port_review.tsv; checks in
  tests/content/test_q2_gate_review_rerelease.c (oracle tests/content/oracles/q2_gate_review_rerelease.ts).
  Stopped for context; the 22 monster behaviour files it only swept (berserk, gladiator, guardian, guncmdr,
  gunner, shambler, supertank, tank and base-variants boss2, brain, chick, flipper, floater, flyer, hover,
  insane, jorg, makron, medic, mutant, parasite, proboscis) are read by gate-q2r2, below. Changed outside the scope: content/q2/base
  player connect result (text owned by the players object, no 4096/256-byte limit; callers in
  app/bootstrap/simulation network.c, network_unified.c and bot_world.c compile unchanged and read the result
  before the next connect), persistence/q2_rerelease_state.c and tests/persistence/test_persistence_q2_players.c
  (the rerelease player state's socialId/dogtag/helpImage are owned strings).
- gate-q2r2 (build/agents/gate-q2r2), 2026-11-10: done. The 22 rerelease monster behaviour files gate-q2r left
  are read in full (berserk, gladiator, guardian, guncmdr, gunner, shambler, supertank, tank and base-variants
  boss2, brain, chick, flipper, floater, flyer, hover, insane, jorg, makron, medic, mutant, parasite, proboscis;
  397 functions by a declaration count), so content/q2/rerelease is read in full. 15 divergences fixed, see
  port_review.tsv: shambler's lightning cast never made the TS shambler_bolt entity (the d4-q2 drift row said it
  had) and its hand tables were indexed with a fractional frame; tank, tank stand and boss2 `||` fallbacks kept
  NaN; the rerelease Makron borrowed Jorg's target text; the medic cable beam was emitted as kind "beam", a
  reinforcement with no strength parsed as NaN (TS 0; tests/content/test_q2_rerelease_monsters.c asserted the
  NaN and is corrected), the candidate list was capped at 64, initialize built the list with zero slots, the
  revive tested inventory.has by entry count, parsed lists leaked on a caught raise; the proboscis drain's
  Math.min dropped NaN. Checks: tests/content/test_q2_gate_review_rerelease2.c (187 checks, clean under ASan/UBSan/LSan; oracle
  tests/content/oracles/q2_gate_review_rerelease2.ts, which runs the TS callbacks over the same stub world),
  also covering gate-q2r's calculatePitchToFire non-finite distance. Fixes with no dedicated check (they need
  the full monster runtime): the Makron target copy, the revive's inventory.has, boss2's NaN scale. Not done:
  dedicated checks for the other gate-q2r fixes no test reaches (killbox over the reused observations() buffer,
  qa_q2rr_error's class, pendingLandmark interning, killboxUse finally, goals notify with a fractional or NaN
  goal number, afterPickup message restore, module capture/restore with changed maps and repeated entries,
  compassUpdate negative index, q64 turn/eyeThink/cameraThink). Six of these are now checked by
  tests/content/test_q2_gate_review_rerelease3.c (gate-q3b); pendingLandmark, module capture/restore, q64 and
  the three monster fixes are still without a check.

- gate-q3a (build/agents/gate-q3a), 2026-11-10: content/q3 foundation (13 files), equipment (8) and base (50 of
  53) read in full; 102 divergences fixed, see port_review.tsv and the gate item below. Stopped for context.
  Still to read: base/game/rankings.ts (16 functions), save-state.ts (9) and save-values.ts (25), and with
  them the open save-reader finding (qa_q3_read_graph reads fewer fields than readQ3Graph): done by gate-q3b.
- gate-q3b (build/agents/gate-q3b), 2026-11-10: done. base/game rankings.ts (16 functions), save-state.ts (9)
  and save-values.ts (25) are read in full, so content/q3 base, foundation and equipment are read in full.
  12 divergences fixed there, and gate-q3a's open items are settled against TS (see port_review.tsv):
  qa_q3_read_graph reads every readQ3Graph field in one pass in the TS order (a graph encoded by bun decodes,
  reads and re-encodes through the C writer to the same bytes); the client backing keeps its saved length and
  restoreClientBacking rejects it; loadQ3Character's lookups advance one path per round as Promise.all does
  (open order and error precedence equal bun); pickupItem reads the handicap after the persistent-powerup
  writes; areaActors is slice(0, maximum); the save-restore failure paths (graph read/capture/prepare, level,
  module cvars, settings, shader remaps) and inventory_profile.c release what they built. Checks:
  tests/content/test_q3_gate_review_saves.c (61 checks, clean under ASan/UBSan/LSan; oracle
  tests/content/oracles/q3_gate_review_saves.ts and its generated q3_gate_review_saves_graph.inc).
  content/q2 rerelease checks the readers left unwritten: tests/content/test_q2_gate_review_rerelease3.c (44
  checks, clean under ASan/UBSan/LSan; oracle tests/content/oracles/q2_gate_review_rerelease3.ts) covers
  qa_q2rr_error's class, goals notify with a fractional/NaN/negative goal number, compassUpdate with a negative
  or fractional index, the afterPickup message restore over 20 open pickups, killbox over a list its own damage
  grows, and killboxUse's finally; none exposed a remaining divergence. Not done (no check written):
  pendingLandmark interning (needs putInServer over an admitted base player), module capture/restore with
  changed maps and repeated entries, q64 turn/eyeThink/cameraThink, the Makron target copy, the medic revive's
  inventory.has, boss2's NaN scale; and for gate-q3a's fixes: mover deltaAngles rounding, the try/finally
  cleanups (combat dispatch, rail relink, withCurrentOrigin, spawnEntity's catch), the per-pool portal think
  context, target_laser movedir aliasing, the vec3() binary32 sites in mover-spawn, item motion, death and
  missile code (each needs a TS MoverRuntime/MissileRuntime/spawn harness for its expected values), areaActors
  (needs a shared scene query fixture) and the inventory profile raise paths (need a pinned qagame image).
  Outside the scope, not changed: src/app/bootstrap/simulation/q3/runtime.c:2314-2317 (t_restore_part "graph")
  is never called and still uses the C-only records-less qa_q3_restore_graph.
- gate-q3p (build/agents/gate-q3p), 2026-11-10: content/q3 presentation, 28 of 43 files read in full (frame,
  players, scene, ref-entity, weapons, hud, hud-corners, console, server-commands, config, audio, resources,
  mission-hud, events, entities, effects, local-entities, view, snapshots, prediction, state, frame-audio,
  client-info, refdef, retail-snapshot, model-access, character-resources, draw-tools; 525 functions by a
  declaration count). 44 divergences fixed, see port_review.tsv and the gate item below. Stopped for context.
  Still to read in content/q3/presentation: client.ts (16 functions), media.ts (16), mission-owner-draw.ts (38),
  player-state.ts (11), scoreboard.ts (9), mark-projector.ts (10, with the C-only world_mark_projector.c),
  marks.ts (11), draw-status.ts (8), draw-icons.ts (5), info.ts (8), cgame-body-profile.ts (2), ui-adapters.ts
  (1), and the type-only collision-host.ts, movement-host.ts, scene-host.ts. content/q3/team-arena (15 files,
  248 functions) was not read: arenas, client-admission, client-effects, client-events, client-policy,
  client-spawn, client-think, commands, foreign-objectives, match, movement-host, objective-placement,
  server-commands, session, team. The EntityEvent name table in events.c was not compared against a bun dump.
- gate-q3c (build/agents/gate-q3c), 2026-11-10: the presentation files gate-q3p left except client.ts (media,
  mission-owner-draw, player-state, scoreboard, mark-projector with world_mark_projector.c, marks, draw-status,
  draw-icons, info, cgame-body-profile, ui-adapters, and the type-only collision-host, movement-host, scene-host;
  119 functions) and all of content/q3/team-arena (15 files, 255 functions by a declaration count). 42 divergences
  fixed (22 presentation, 19 team-arena, 1 in base/game/format.c), see port_review.tsv and the gate item below.
  The EntityEvent name table equals a bun dump (83 names). Stopped for context.
  Still to read in content/q3: presentation/client.ts (16 functions; client.c is 1,706 lines, the composition
  root). Not done: the dedicated full-client-fixture checks for the gate-q3p fixes listed in the gate item below.

- gate-cq2 (build/agents/gate-cq2), 2026-11-10: compat/q2, stopped for context with 19 of 75 files read in full.
  Read: classic host, records, layout, cvars, printf, pmove, inventory, world-profile, combat-profile, pickup-profile; guest-strings,
  guest-console, native-primary-reader, native-mod-entries; rerelease host, imports, module, player-state. Compared
  with a bun dump value for value: the four classic layouts with their 44 imports and 15 exports, the 22 rerelease
  layouts, the four API table layouts and 209 call signatures, both classic combat profiles and inventory tables.
  62 divergences fixed, see port_review.tsv; checks in tests/compat/test_compat_q2_gate_review.c (2804 checks; bun
  oracle tests/compat/oracles/compat_q2_gate_review.ts and .txt), clean under ASan/UBSan/LSan. The worst: (1) the
  recoverable native throws (SourceItemRetired, cancelled frames, removed input) ran on a second setjmp stack beside
  qa_raise/qa_try, so either kind of unwind crossing the other left a stale frame and skipped cleanups; they are one
  mechanism now and the TS finally blocks behind every native try run for every error class. (2) No guest call in
  the two hosts had its TS try/finally: a guest error left strings, scratch buffers and save buffers mapped (later
  guest addresses move), the classic travel save left every client's inuse word at 0, the rerelease BoxEdicts filter
  depth stayed raised (world rebind then refused forever). (3) classicPrintf printed e0+4 for e+04. (4) The rerelease
  Pmove entry was bound on the runner directly instead of through bindNativeModEntry, without the frame bypass for
  the original call, and the equipment scope was prepared after the input boundary instead of before. (5) reconcile
  released only 512 actors and kept only 256 retained clients. (6) 620 raises had the wrong error class.
  Still to read in compat/q2 (56 files): classic/combat-binding, compatibility,
  native-combat-call, native-damage, native-input, native-pickups, native-mod-actors, -armor, -client-stages,
  -clients, -combat, -deferred, -invocations, -items, -pickups, -protection, -provider, -region, -weapon-stage,
  native-primary and its -commands, -command-profile, -drop, -drop-profile, -inventory, -inventory-profile, -player,
  -player-profile, -profiles, -validation, -weapons, -weapon-profile, and rerelease api, body-shape, client-profile,
  combat-binding, debug-shapes, deferred-damage, equipment-movement, foreign-actors, layouts (functions; the tables
  are checked), messages, native-entries, native-weapon-declaration, navigation, pickup-profile, pickup-protection,
  public-state, q2eaks-weapon-profile, semantics, sounds, source-state, spatial, weapon-behavior-profile,
  world-profile, world-text. Seen there without a read: fixed arrays (classic combat_binding frames[16], armor[64],
  hooks[16] and a static piece[512]; native_combat_call vals[64]; native_primary_validation entries[256];
  foreign_actors and deferred_damage args[16]) and the 13 TS AggregateError sites, none of which has a C raise.
  Open from this packet: rerelease readRereleasePlayerState returns stats in one static array; API 3 text above
  U+00FF (TS writes ? or raises, C carries the bytes); %c of 0. Outside the scope:
  app/bootstrap/simulation/classic_guest_source.c:436 caps a variadic import at 64 arguments ("Too many variadic
  guest arguments", TS has no limit); qa_caught_error keeps 2047 bytes of a message, which cuts a long guest error
  text (API 3 game error, Com_Error).
- gate-cq2b (build/agents/gate-cq2b), 2026-11-11: compat/q2, the classic and top-level files gate-cq2 left; stopped
  for context with 28 of its 32 files read in full (234 functions by a declaration count). Read: classic/combat-binding,
  compatibility, native-combat-call, native-damage, native-input, native-pickups; native-primary and its -commands,
  -command-profile, -drop, -drop-profile, -inventory, -inventory-profile, -player, -player-profile, -profiles,
  -validation, -weapons, -weapon-profile; native-mod-actors, -armor, -client-stages, -clients, -deferred,
  -invocations, -pickups, -region, -weapon-stage. Compared with a bun dump value for value: every leaf of the
  built-in weapon, player, command, inventory and drop profiles for the three digests (two Xatrix builds, the
  rerelease) and of the two classic pickup profiles (1,117 leaves; no divergence). 107 divergences fixed, see
  port_review.tsv; checks in tests/compat/test_compat_q2_gate_review_b.c (1397 checks; bun oracle
  tests/compat/oracles/compat_q2_gate_review_b.ts and .txt), clean under ASan/UBSan/LSan. The worst: (1) classic
  combat: RemovedNativeDamage was a flag read after the guest's T_Damage had run to its end on a removed target; it is
  a native throw now (the frame is the request's identity) and the damage() that owns it restores the processor.
  No part of damage() had its TS finally: a guest error left the write observers and reaction hooks bound to a dead
  stack frame, the scratch vectors mapped and the frame pushed. frames[16] aborted and armor[64] had no bound check.
  One static attacker/inflictor pair was overwritten by a T_Damage running inside another. (2) validateNativePrimary
  checked the decision and pickup regions only: attack skips, the delay and damage regions, give/drop/inventory
  regions, drop callbacks and debits, the world calls and the whole classic and rerelease world tail were not
  validated, and entries[256] had no bound check. (3) readNativeCombatCall did not read an aggregate default, took a
  byte above 255, had no planGuestCall checks and wrote past st[32]. (4) No constructor in the native primary and
  native mod services had its TS catch: a bind that raised left the earlier entries, regions and observers bound to
  an object nobody owned. Writes the TS does inside a try (drop and inventory projections, weapon delay, weapon
  decision masks, pickup quantity, the pickup lifetime observers) were made before it, so the finally did not
  restore them. (5) The three AggregateError sites of the files read are raised (native-mod-clients close,
  native-mod-pickups release and close). (6) nativeModUserCommand refused a
  rerelease command without an impulse and reported a missing input as a RangeError before the interval check.
  Open items of gate-cq2 closed here: classicPrintf has a length-carrying form (%c of 0 is a character; the print
  service still takes a C string); readRereleasePlayerState takes each stats array from a ring of 256 (the contract
  type borrows its stats, so a state kept across 256 later reads still aliases); the variadic import cap of 64 in
  app/bootstrap/simulation/classic_guest_source.c is gone; a caught message longer than 2047 bytes is kept whole
  beside the caught buffer (include/qa/foundation/caught_message.h: qa_caught_message, used by qa_rethrow; a
  consumer that reads caught.message directly still sees 2047 bytes).
  Still to read in gate-cq2b's scope (4 files, 132 functions by the same count): native-mod-combat (34),
  native-mod-items (23), native-mod-protection (20), native-mod-provider (55), with C-only native_mod_host_api.c.
  Their eight AggregateError sites (native-mod-protection 4, native-mod-provider 4) are not ported. Not fixed: API 3 text above
  U+00FF: the port holds API 3 text as bytes (one byte per TS character), so the TS branches for a character above
  U+00FF (fitClassicUserinfo's ?, writeClassicString's RangeError) cannot be told from latin1 bytes inside
  compat/q2; text that entered the engine as UTF-8 (config files, the command line) is carried as its UTF-8 bytes.
  Per-actor contexts (classic combat binding, native-mod-actors body bindings) are kept until their owner is
  destroyed. The fixes that need a running guest (combat binding, native input, native pickups, the native primary
  and native mod services) have no dedicated check: test_sim_verify q2nc/q2nr and the app tests run those paths
  without a raise. Outside the scope, seen: rerelease/foreign_actors.c:502 raises V8's BigInt text (bun: "Not an
  integer"); rerelease callers of qa_compat_q2_native_combat_signature pass fixed parameter storage
  (foreign_actors.c, deferred_damage.c, source_state.c, native_entries.c) where a declared call has no argument
  limit; foreign_actors.c keeps RemovedNativeDamage as a flag (removed_request) as the classic binding did.
- gate-cq2c (build/agents/gate-cq2c), 2026-11-11: compat/q2, what gate-cq2 and gate-cq2b left; no compat/q2 file
  is left unread. Read in full: native-mod-combat (34 functions), -items (23), -protection (20), -provider (55),
  C-only native_mod_host_api.c, and the 24 rerelease files without a row (194 functions by a declaration count):
  api and layouts (the functions; gate-cq2 compared the tables), body-shape, client-profile, combat-binding,
  debug-shapes, deferred-damage, equipment-movement, foreign-actors, messages, native-entries,
  native-weapon-declaration, navigation, pickup-profile, pickup-protection, public-state, q2eaks-weapon-profile,
  semantics, sounds, source-state, spatial, weapon-behavior-profile, world-profile, world-text. Compared with a bun
  dump value for value: every leaf of the retail world profile (1,300), the source client profile (246), the retail
  pickup profile (156) and the built-in trajectory weapon (declaration JSON, definition, both layouts, both
  signatures; 131 leaves); no divergence in the tables. 144 divergences fixed, see port_review.tsv; checks in
  tests/compat/test_compat_q2_gate_review_c.c (2208 checks; bun oracle tests/compat/oracles/compat_q2_gate_review_c.ts
  and .txt), clean under ASan/UBSan/LSan; test_compat_q2b, test_q2_native_mod, test_q2_native_primary,
  test_native_mod_host, test_sim_native_mod and test_sim_guests, which run the rewritten files, are clean under the
  same sanitizer build. The worst: (1) rerelease source-state encoded every damage cause with the classic profile:
  a cause that is not a native one raised 'Damage cause has no rerelease mod_t representation', and the mod_t
  scratch leaked on every call. (2) rerelease foreign-actors (C rewritten): RemovedNativeDamage was a flag, it is a
  native throw whose token is the damage frame; damage() had no TS finally (write observers, reaction entries,
  scratch vectors, frame, reconcile); actors and slots were one array where TS has two Maps; clear() did not end
  when an entry was already releasing; args[16] and id[512] are gone and a frame keeps its address when the frame
  list grows. (3) native-mod-combat
  watched the whole table and walked it by stride (no end for stride 0), its callback entries ran while the service
  was suspended, and the combat flags lost their high 32 bits. (4) validateNativeWeaponImage kept a fixed list of
  72 entries and did not check the rest. (5) Dangling pointers: a weapon definition's id and title pointed into a
  declaration freed by the next call; the think signature and both q2eaks signatures pointed at dead local
  parameter arrays. (6) All nine AggregateError sites are raised after every step ran: native-mod-protection 4,
  native-mod-provider 4 (input field restoration, input staging and rollback, cleanup twice), foreign-actors
  clear(). (7) native-mod-provider: pointerFor, execute, restore, the input scopes and the constructor had no TS
  catch or finally; corrections were not decoded as fatal UTF-8; scratch lists aliased between actors. (8) The debug
  shape lifetime was seconds * 1000 (1000 for a negative value) where TS takes ToUint32 of the truncated float32
  product and refuses a non-finite value. (9) validateRereleasePrimaryWorldProfile had no validateValueLayout.
  Items earlier readers left, closed: the BigInt text, RemovedNativeDamage, and the combat signature storage (sized
  by the declared call in foreign_actors.c, deferred_damage.c and source_state.c; native_entries.c takes caller
  storage for the stock call). NOT closed: readRereleasePlayerState's stats. The contract type borrows them
  (qa_q2_player_view.stats is a qa_double_view in include/qa/contracts/protocol.h, shared with network/q2 and app),
  so a state cannot own its stats without changing every holder; qa_compat_q2_read_rerelease_player_state_into and
  qa_compat_q2_rerelease_public_edict_player_state_into read into caller storage and the compat/q2 callers that keep
  a state use them; the ring of 256 remains for src/app/bootstrap/simulation/rerelease_guest_services.c:685.
  No dedicated check: the AggregateError sites and the catch/finally paths need a gameplay authority or a guest
  that fails part way; test_sim_verify q2nr and the app tests run those paths without a raise. Contexts kept until
  their owner is destroyed: native-mod-items request and capability records, native-mod-combat bound actors,
  rerelease combat-binding bind contexts, foreign-actors stage contexts and retired projections. Outside the
  scope, seen: src/app/bootstrap/simulation/rerelease_guest_services.c:631 holds the process-wide scratch of
  qa_compat_q2_rerelease_inventory_items (two hosts alias; qa_compat_q2_rerelease_inventory_items_owned exists);
  src/app/bootstrap/simulation/native_q2_rerelease_save.c:121 leaks the decoded tree when a refused save raises
  (LeakSanitizer on test_q2_rerelease_pak, 22,930 bytes); include/qa/world/session/mod_client_presentation.h:72
  `generation` is a value where TS has a live getter; an embedded NUL in a JSON string cannot be seen by the C
  string readers (world profile classname, weapon declaration text); the trig in rerelease/weapon_behavior_profile.c uses libm.
- gate-content (build/agents/gate-content), 2026-11-10: the content modules outside q1/q2/q3. Read in full: mounts
  (index, paths), archive (6 files), catalog (15 files), roots, user-data, held-weapon, item-icon, model-attachment;
  composition q1/give and q1/commands; the restore halves of composition q1/clients and q2/save; mods selection, match
  and source-call. Compared with a bun
  dump value for value: the 40 expectedProducts, weaponResources for the 18 products with a weapon adapter, the nine
  supply profiles with expansionSupply and expansionSourceSupply (81 profiles), the 16 monsterSources, the campaign
  roster slots and defaultMonsterRoster for every source. 89 divergences fixed, see port_review.tsv; checks in
  tests/content/test_content_gate_review.c (500 checks; bun oracle tests/content/oracles/content_gate_review.ts and
  .txt), clean under ASan/UBSan/LSan. The four open items handed to this packet:
  (1) the mounts listing cache is ported: #readSource asks the cached listing first and the disk on a miss, so a
  stale spelling wins until invalidateListings, as in TS; (2) qa_game_write_redirect equals bun (the shared root
  joined with the first 16 hex digits of sha256 of the resolved install root); (3) the rerelease `.pak.gz`: Bun's
  node:zlib is zlib-ng, the C links stock zlib 1.3.2. Header (1f 8b 08 00, mtime 0, XFL 0, OS 3) and trailer are
  identical; only the deflate body differs (215 against 228 bytes). No level, memLevel or strategy of stock
  deflateInit2 reproduces Bun's bytes; zlib-ng 2.3.3's zng_deflateInit2(Z_DEFAULT_COMPRESSION, Z_DEFLATED, 31, 8,
  Z_DEFAULT_STRATEGY) reproduces them exactly for the test archive. Not changed: it needs libz-ng as a link
  dependency (Makefile) in persistence/kex_q2_save.c, and equality then depends on the zlib-ng version matching
  Bun's; both streams inflate to the same pak. (4) qa_q2_vector_field splits on JS white space.
- gate-content2 (build/agents/gate-content2), 2026-11-11: what gate-content left. Read in full: composition
  q1/runtime (44 functions), q1/clients (the other 16), q1/q3-pickups, q2/index (17), q2/match (18),
  q2/match-selection, q2/save captureQ2Product, q2-q3-items (5), both expansion arsenals; content/mods callbacks,
  qvm-callbacks, qvm-items, qvm-presentation, catalog, declaration, pickups, client-input, client-outputs,
  item-actions (native-callbacks: TS read, C replayed only); monsters target and authored;
  q3/presentation/client.ts (16 functions). Compared with a bun dump: the six per-file monster tables (474 lines),
  the 51 Q3 foreign item definitions, the q2:composition match value for every match kind. Replayed against bun:
  20,468 single-node mutations of accepted quakec, qvm, native and qvm-presentation declarations (error text and
  class for each), 10 gameplay-mods.json documents, 2316 preprocessor scripts, 102 DEBUG_EVAL traces, 105 reader
  call sequences, 968 lexer scripts. 52 divergences fixed (port_review.tsv); the ones that change behaviour:
  g_select_empty never reached the Q2 composition's player hooks; the q2:composition record lacked the LMCTF travel;
  the three mod readers carried stale copies of the item icon and model grip readers; read_line_token left its
  result uninitialized (a `#define`, `#ifdef` or `#ifndef` with nothing after it printed a stale token or crashed);
  a stringized macro argument took the wrong column; an uncaught ScriptLanguageError aborted the process; the
  script failure channel (its own setjmp frames) did not unwind qa_cleanup nodes or qa_try frames (ASan:
  stack-use-after-return in expandDefine): frames are now entered and left through
  qa_ui_script_catch_enter/leave, built on qa_unwind_mark_take/qa_unwind_to in foundation/support.
  gate-content's open items: discovery releases its working set when a directory read throws (the LSan suppression
  in test_content_gate_review.c is gone); the fatal UTF-8 decode error in network/q2/handshake.c and
  app/bootstrap/network/unified_event_codec.c is Bun's TypeError `Invalid byte sequence`; qa_archive_find_entries
  is left for the optimization phase. Checks: tests/content/test_content_gate_review2.c (24446 checks), clean
  under ASan/UBSan/LSan. Open: content/legacy-script is not read in full (see the packet list above);
  qa_read_qvm_mod_presentation (no callers) leaks its parsed tree; the legacy script heap frees guest allocations
  at destroy that TS leaves allocated when a source is dropped without dispose().
  The worst found: the combined monster source table (monsters/definitions.c) was stale against TS (hipnotic
  monster_spikemine, rogue monster_dragon and monster_morph were missing, so selecting them raised "Selected monster
  implementation is unavailable"; mg3 resources differed): it is now generated by
  tests/content/oracles/monster_sources_gen.ts. A ZIP entry whose DEFLATE stream ran past its declared size was
  accepted when the CRC of the prefix matched. An unreadable directory under a loose root aborted the process.
  Left open, with file and line in the report: discovery leaks its working set when a directory read raises;
  archive findEntries recomputes comparison keys on every lookup (TS builds an index once); ownDirectory() uses the
  working directory where a compiled TS binary uses dirname(process.execPath) (content/roots.c
  qa_content_root_candidates); a NUL inside a JSON string is invisible to the readers (persistence/value keeps C
  strings); Bun 1.3's fatal TextDecoder message is "Invalid byte sequence", which network/q2/handshake.c:23 and
  app/bootstrap/network/unified_event_codec.c:7147 spell "The encoded data was not valid for encoding utf-8".

Not started: full reads of content/q2 multiplayer, catalog/composition/mounts, compat,
guest, bots, network, ui, app, audio, materials, formats, rest of core/input/media/platform/
text/persistence/contracts; unported TS tests; full-tree ASan/UBSan; audits; final drift
re-check; then connect main, then optimize.

## What "ported" means

A TypeScript file is ported when every exported symbol and every private
function and branch exists in C with the same behavior, and the matching tests
under `../quake-anthology-ts/tests` are reproduced as C checks and pass.

The TypeScript code is the only source of truth. Read it; do not infer behavior
from an existing C header, comment, or report.

Not acceptable, in any form:

- A struct that stores its options with no logic (the placeholder pattern).
- Delegating a body to a host callback "until X lands" when TypeScript has the
  logic in that file. Host callbacks are correct only where TypeScript takes a
  host parameter too.
- `(void)param` to drop an argument TypeScript uses; returning a fixed value
  where TypeScript computes one; aborting with "not constructed".
- Comments saying something is unresolved, deferred, or left for later, in place
  of doing it. If a dependency truly is missing, say so in your final report.
- Weakening or deleting a test to make it pass. A failing oracle test means the
  port is wrong until shown otherwise; state the evidence if the oracle is wrong.

A C file can legitimately be small: a table the C port generates differently, or
types that belong in the header. Prove that by reading the TypeScript, and name
the file and reason in your report.

## One engine

This is one shared, unified engine. `SharedSimulation` composes content by
recipe: map entities, movement, weapons, and characters from different games
coexist in one world. Never fork per-game runtime paths where TypeScript has one
path with per-source dispatch, and never add a per-game `main` branch.

## Conventions

`docs/PORTING.md` has the TS-to-C conventions (binary32 staging, error handling,
closures as fn+ctx, ownership). Match the surrounding code.

Every callback taking `void *ctx` must receive the context its author expects.
When you overwrite a host's `ctx`, forward the caller's leaf functions with the
caller's original context. Test this with a sentinel `ctx`: a test whose leaves
ignore `ctx` proves nothing.

## Working alongside other porters

The tree may be edited concurrently, and the base state is uncommitted.

- Do not run any git command that changes the repository or its index: no `git add`,
  `git rm` (including `--cached`), `git commit`, `git stash`, `git checkout`,
  `git restore`, or `git reset`. Delete files with `rm`. Also no `make clean`.
- Build only with `tools/agent_build.sh NAME <targets>`, using your own NAME.
  Binaries land in `build/agents/NAME/`. Example:
  `tools/agent_build.sh bots-lib build/agents/bots-lib/test-bots/test_library`.
- Edit only files in your assignment, plus their tests. Other headers may gain
  additive declarations; do not change or remove what exists. If you must change
  an existing signature, update every caller in the same edit and report it.
- When you must link through a private archive, build it with `ar qc` then `ranlib`,
  not `ar rcs`: `rcs` replaces members that share a basename (several `index.o`,
  `services.o`, `content.o`) and leaves undefined references.
- Do not start helper agents or forks of your own. Several porters each starting
  four or five helpers, with builds and bun runs in each, exhausted memory and crashed
  the machine. Do the work yourself, one build at a time at -j1 (the default).
- A compile error in a file you do not own usually means another porter is mid-
  edit. Retry shortly. Do not fix it.
- New tests go in new files, `tests/<module>/test_<name>.c`; the Makefile finds
  them. Do not rely on `make test` to find your failure: it stops at the first
  failing binary. Run your binaries directly.

## Order of work

The user's instruction: finish porting everything, confirm it is all there, and
only then connect it. Do not wire anything into `main` before the gate below.

1. Port leaf subsystems: bots library, Q3 game logic and presentation, Q2 native
   mods, renderer and network remnants. Memory safety: every test binary clean
   under ASan and UBSan.
2. Port the Q3 bot AI (`bots/behavior/q3`, director) over the finished library,
   then the `app/bootstrap/simulation/*` shells: monsters, guests, grapple, native
   mods, network adapters, bot services. The unified event codec.
3. Port the top layer as code, tested at unit level: `simulation/quakec-source.ts`,
   `simulation/runtime.ts`, `application.ts`, `startup.ts`, `startup-selection.ts`,
   `input.ts`, `effects.ts`, `presentation*.ts`, `ui.ts`, `audio.ts`,
   `remote-application.ts`. `SharedSimulation` stays one type. `createSource` stays
   one dispatch over its six source kinds (native Q2, Q3 guest, QuakeC, TS Q3, TS Q1,
   TS Q2), and `step` stays one pipeline. Split the C across `runtime_*.c` by TS
   region; do not split by game.

**Confirmation gate.** Before step 4, all of these hold and are reported.

Full read: every TS module is compared against its C function by function, every
branch, not only rows the audits flag. Reading 112 rows the name audit counted as
present found 15 behavior bugs, so name and token checks cannot confirm a port. Track
it per module in `docs/port_review.tsv` (module, files, functions read, bugs fixed,
reviewer), and run one review pass per module after its porting is finished.

Also:
`tools/depth_audit.py --ratio 0.75` and `tools/symbol_audit.py` show no unexplained
gap, every exception named with a reason; `tools/admission_audit.sh src include`
has no hit that describes missing behavior; `tools/eval_order_audit.py` reports no
unclassified site; `tools/js_semantics_audit.py` reports no unclassified site;
every TS test file maps to C checks; and all test binaries pass,
also under ASan and UBSan.

Audits status (agent "audits", 2026-11-11; outputs in build/agents/audits/tmp/, each tool run
through tools/run_capped.sh). New allow-lists, each line with a reason: tools/symbol_audit_ledger.tsv,
tools/depth_audit_exceptions.tsv, tools/admission_audit_allow.tsv (symbol_audit.py and depth_audit.py
exit 1 while an unexplained item remains; admission_audit.sh drops the listed hits); manifest c_source may list several files (depth_audit.py,
ts_drift_files.py), and application.ts, remote-application.ts and startup.ts now name their split C files.
- depth_audit: before 2 missing + 10 thin at 0.5, 2 + 35 at 0.75, no exception list. After: 0
  unjustified at both ratios (34 justified: perf-only x86 fast paths, compact data tables checked against
  bun, type-only contracts, barrels, SourceStrategy bodies in shared_simulation_*.c). The alias normal
  table had no value check: tests/formats/test_formats_audit_review.c (326 checks, bun oracle).
- symbol_audit: before 1556 misses, no ledger. After 216 unexplained: 1340 ledgered (C names with the
  same words in another order; names in files port_review.tsv records as read in full; unported perf
  fast paths). The 216 sit in files without a recorded full read (aas-cluster.ts 15, quakec-source.ts 15,
  simulation/q3/runtime.ts 11, render/cpu/*, materials/material.ts, ...): owner, the full read.
- admission_audit: before 156 hits. About 45 stale "not yet ported/landed" notes reworded (the named
  modules exist), the Q3 cd-key/product-policy cvar flag macros replaced by the cvars enum, 60 allow-list
  entries for legitimate hits (TS/id message texts, ABI kind names, "lands" meaning arrives). After 33, all the
  concrete-host layer of step 4: simulation/runtime.c 17, main.c 7, application.c 2,
  application_transition.c 1, simulation/runtime.h 2, simulation/q3/runtime.h 1, simulation/network.h 1,
  content.h 1; plus arsenal/weapon_status.h:16 (gate-clean2's file).
- eval_order_audit: before 182 unclassified of 195 flagged. One real order bug fixed
  (content/composition/q1/clients.c userinfo restore read value before key, so a save with both malformed
  reported the value's error; TS reads key first). 178 sites ledgered by operand kind. After: 0
  unclassified (194 flagged, all ledgered); 6525 "undecidable" (unknown-effect call beside a plain read)
  are a separate count.
- js_semantics_audit: before 836 unclassified (0 parse failures), mostly app/bootstrap and simulation
  sites new since the drift. 357 classification rows added. After 19: compat/qvm/mod_provider.c 14 and
  app/bootstrap/q3_client/assets.c 1 (gate-clean1's files), and 4 divergences on malformed or unusual
  input left open: component_scene.c:120 truncates a fractional player view height to int32 for the Q3
  snapshot selector (TS passes the number to visibility, fround(viewheight)); bot_world.c:167/423 cast
  Number("*<text>") of an inline model name to int64 (TS keeps NaN or a fraction for malformed "*" names);
  content/catalog/addons.c:215 lower-cases a URL host ASCII-only where WHATWG URL applies IDNA to a
  non-ASCII host.
- Real fix found by the admission audit: the LMCTF grapple module's state delete (disconnect) removed only
  from its inactive map while the native equipment held the state (TS grapple.states is the equipment's map),
  so a reconnecting player kept the old hook state and its iteration slot.
Audits status update (agent "gate-symbols", 2026-11-12; outputs in build/agents/gate-symbols/tmp/):
- symbol_audit: unexplained 0 (was 216), stale ledger lines 0; missing=1553, all 1553 ledgered. 213 of the
  216 names ledgered with the C name or the reason checked in the C source (renamed with a joined word such
  as netquake/quakec/typescript/cstring, inlined, static tables, worker-message encodings that shared-memory
  threads do not need, TS emulations of glibc strtod, four dead AAS cluster helpers, perf paths). One ledger
  line went stale (ClusteringWorld now matches) and was removed. Two were genuinely missing and are ported:
  main.ts redactArguments and redactOutput (src/main_redaction.c; main.c's crash log also lacked the 1 MiB
  rotation and the 0600 mode). One wrong port found on the way: q2-save-folder fitsClassicCvar counted bytes.
  docs/port_missing_symbols.tsv rewritten (header only).
- js_semantics_audit: 1 unclassified (was 19), 0 parse failures: content/catalog/addons.c parse_url tolower, the open IDNA item below. mod_provider.c (5 functions) and q3_client/assets.c classified
  (assets.c world_model_bounds also gained an upper bound before its size_t cast). Fixed: component_scene.c
  view height (the Q3 selector takes the number); bot_world.c inline model numbers (a NaN or fractional
  "*<text>" matches no model, as in TS, and is no longer cast). content/catalog/addons.c: the host is now
  percent-decoded and checked after decoding; STILL OPEN there: UTS #46 (IDNA) of a non-ASCII host. WHATWG
  runs the IDNA mapping table, NFC, Punycode and the validity checks; that needs the Unicode IDNA data
  (about 9000 mapping ranges plus normalization tables), which this tree does not carry, and libidn2/ICU
  are not link dependencies (libidn2 also differs from the URL standard's flag set). Effect: a non-ASCII
  spelling that maps to www.quaddicted.com (fullwidth letters, U+3002) is not accepted as the Quaddicted
  host, and a host UTS #46 rejects parses instead of raising the TypeError that skips the row. ASCII
  "xn--" label validation and the IPv4 ends-in-number rule are not reproduced either (same two effects).
- admission_audit: 32 hits, all in the step-4 concrete-host layer (runtime.c, main.c, application.c,
  application_transition.c and four headers). arsenal/weapon_status.h:16 was a stale comment (both callers
  bind the real Q1 services); reworded.
- depth_audit 0.75: 0 unjustified. eval_order_audit: 194 flagged, 0 unclassified.
  tests/content/test_content_audit_review.c (9 checks, bun oracle) covers it.
  The clients.c order fix has no test (a TS-side restore needs a whole Q1 composition host).
  Verified (build/agents/audits): tests/core 21, tests/formats 6 and tests/content 80 binaries pass;
  test_sim_verify 10 cases 0 mismatches; test_remote_loopback nine families 0 differing;
  test_application_open_real q1/q2/q3 0 differing.

4. Connect: `main` and the application reach the real `SharedSimulation`. Delete
   the "concrete host" layer. Run real data side by side with TS and fix what that
   finds, then optimize.

## Done means

- `tools/depth_audit.py` lists no unjustified thin file, including above the 0.5
  threshold on a second pass with `--ratio 0.75`.
- `tools/reachability.py` shows every object reachable, or a named reason it is not.
- Every test binary passes, also under ASan and UBSan; C checks cover the TS tests.
- Side-by-side real-data runs match the TS oracle: console output, entity counts,
  and deterministic state hashes after N frames, for each game and for mixed
  recipes (`--preset q2-q1-q3`), dedicated and graphical.
- Only then optimize, with a profile for every change and these checks as the guard.

## Open items found by porters

- gate-misc (2026-11-11), input/text/media/platform/console/llm, settled or left:
  - Console text encoding (settled, plan recorded, not changed): TS keeps every console script as Latin-1 code
    units (config-scripts.ts, startup-config.ts and settings/native-config.ts readFrom all decode with
    `toString("latin1")`; sourceCommandText refuses units above 0xff) and writes the native config back with
    writeAtomic(text), which encodes UTF-8. So the TS round trip turns a 0xE9 byte into C3 A9. The C convention for
    command text is the code-unit byte (one byte per TS unit), so the native config script should be held as its
    raw Latin-1 bytes like every other script, and the Latin-1 to UTF-8 conversion belongs at the writeAtomic
    boundaries (NativeConfig write, writeBindings, merge, and the snapshot/recordWritten comparison that layers
    use). Plan: (1) qa_native_config_read_script returns the file's raw bytes; (2) a qa_latin1_to_utf8 at the three
    write sites and wherever settings text read from JSON (UTF-8) meets console text (layers' absorbSession, the
    bindings/cvars passed in) converts UTF-8 to code-unit bytes (refusing units above 0xff as sourceCommandText
    does); (3) re-run test_d9b_drift, test_q2_original_saves and the native-files cases. Owner: settings and
    app/bootstrap, not this packet. Until then a config line with a byte above 0x7f executes as UTF-8 bytes in C.
    SETTLED by gate-app1 (see its entry under "Gate read packets").
  - llm JSON.parse depth: FIXED by gate-misc2 (iterative parser and free; test llm.json.*).
  - text/truetype scratch on the tt_err longjmp: FIXED by gate-misc2 (held by the error channel, released before
    the jump; LSan-clean over truetype.glyf.*). Left for optimization: the kern table inserts in O(n^2).
  - platform/audio: C links SDL2 and equals TS's SDL2 backend; TS prefers its SDL3 backend when the library is
    present (stream at the requested rate, buffer frames from the hardware period). Not ported.
  - text/layout sourceOffset is a byte offset in C (UTF-16 units in TS); nothing outside layout reads it.
  - Fixes not reached from a test: router/seat constructor pre-checks (raise paths only, now leak-free), router
    pump cleanups, source-input batch cleanup, truetype directory and color-layer cleanups, RoQ memmove, mouse
    invertPitch for a sign-bit NaN. gate-misc2 reached the truetype glyph scratch only (truetype.glyf.*); the RoQ
    corpus mutations do not prove the overlapping-motion case; the input ones need tests/input infrastructure and
    a negative-NaN m_pitch value the cvar parser does not produce from text.

- gate-misc2 (2026-11-11), media/platform, settled or left:
  - platform/audio SDL3: unchanged from gate-misc (C links SDL2; TS prefers SDL3 when present).
  - SDL2 vs TS: bun FFI renders a NULL `cstring` return as "null"; C SDL_GetAudioDeviceName/SDL_JoystickName NULL
    handling follows that only where sdl.c already maps it (sdl_string); platform/audio raises on NULL device names.
  - qa_binary_reader owns a copy of its source name, so every BinaryError raised through a reader leaks that copy
    unless the caller holds the reader in a cleanup (core, gate-core's area): gate-misc2 added cleanups in media
    only (cin, roq). A borrowed or inline source would close it everywhere.
  - Theora/Vorbis/FreeType library loading is linked, not dlopen'ed: TS's "unavailable" paths (missing library,
    unsupported ABI) have no C counterpart.
  - IPX Windows branch and the ipx "unavailable" reason for win32 are not ported (Linux only).
  - Fixes without a check (need SDL video, a controller, a hostile FreeType face or huge sizes): SdlWindow.open
    unwind, window close AggregateError, gamma finally, controller open/attach cleanup, FreeType metric and native
    address checks, FreeTypeError class, IPX per-listener microtask, capture fs errors, dedicated UTF-16 count,
    CIN playback run/restart state after a throwing read, RoQ reset keeping the old decoder.
  - Outside scope, found: src/app/bootstrap/assets.c:746 allocates entry->playback before
    qa_cinematic_playback_init and only frees it on a false return, so a raising init (malformed movie) leaks it;
    campaign_cinematic.c and q3_client/cinematics.c raise "Out of memory" for a false return without freeing.

- gate-misc4 (2026-11-11), llm/camera/cleanup leftovers, settled or left:
  - localeCompare outside the generated ranges (U+0530-U+1DFF, U+1F00-U+1FFF, U+20D0-U+20FF, U+2190-U+2FFF,
    U+3100-U+FEFF, U+FFF0 and above): after every table weight, by code point; ICU orders Armenian, Hebrew, Arabic,
    Indic, Thai, ... between Cyrillic and kana and Han last. Combining marks carry their own weights (ICU's canonical
    closure gives "e" + U+0301 the weights of "é"). server_browser.c rows() keeps gate-app6's Latin-1 base table
    (equal to the new one there) and could forward to qa_js_locale_compare_base.
  - llm: apiModel looks apiReasoning up by an inherited key in TS ("constructor" gives a function, not reproduced); an
    API key holding U+0000 or a character above U+00FF reaches curl as UTF-8 bytes (Bun's fetch throws TypeError or
    sends Latin-1); IDNA is not reproduced in otherService (a non-ASCII host stays lowercased UTF-8, Bun writes
    punycode); the loginSubscription authorize URL keeps the older partial URL handling (authorizeUrl is a test
    option); the callback worker compares the request pathname without dot-segment normalization.
  - Not reached by a test: rerelease argv/args UTF-8 (needs a guest call harness), the bot chat console conversion,
    mod_presentations' error class, campaign-cinematic key order (needs an application checkpoint with pending
    cinematic audio), the worker's 16 KiB request line, the Expect header suppression (the 100 Continue case is).
  - Outside scope, found: src/app/bootstrap/simulation/bot_rerelease.c:339 lowercases argv[0] with
    qa_js_to_lower_case (UTF-8), but console argv is console text (one byte per UTF-16 unit); a non-ASCII command name
    is lowered wrongly in the "Unknown native bot command" message (qa_js_latin1_to_lower_case fits).
  - text/truetype was not re-read line by line by gate-misc4 (context); gate-misc's full read (45 functions) and
    gate-misc2's scratch fixes stand, and the d8c/d9a drift (CFF budget and stack, composite budget, no cell or atlas
    cap) is present.

- gate-misc3 (2026-11-11), materials/common_parse/console/llm/camera/audio leftovers, settled or left:
  - Representation, settled: decoded text (qa_text_decoder_decode) is UTF-8 with U+0000 as C0 80; byte strings
    (BSP/entity lumps, network strings, configstrings, COM_Parse cursors) are code-unit bytes. qa_tokenizer_init is
    byte mode, qa_tokenizer_init_text text mode. Residuals: a byte-mode token or byte script (qa_inspect_shader_byte_script,
    character turntable) cannot hold a NUL (C strings; their sources end at NUL except the turntable's byteText of a
    file); a script name holding a unit above U+00FF is kept as UTF-8 in the render registry key and could equal a byte
    name spelled with those bytes; a shader/MD5 name holding a decoded U+0000 is not cut at it by stripShaderExtension
    (C0 80 is not NUL); CommonParseState restore refuses a saved token or name with a unit above U+00FF (TS keeps it).
  - Outside scope, found: src/app/bootstrap/q1_wheel.c:118 and src/app/bootstrap/q3_client/assets.c:499 hand raw file
    bytes where TS decodes with TextDecoder (invalid UTF-8 is not replaced; skin names then differ from TS for
    non-UTF-8 bytes); src/core/math.c qa_hypot3 is not Math.hypot for NaN with a zero maximum (callers outside
    materials); console/commands condump/writeconfig/screenshot report fixed texts where TS rethrows the store or capture
    error (the C callbacks return bool/NULL; needs settings and app/bootstrap callback errors); llm/auth_worker.c aborts
    on invalid worker data where the TS worker thread throws (inputs are internal).
  - Not reached from a test: VorbisPcmStream.close VorbisError (libvorbisfile ov_clear returns 0); the evaluate,
    legacy batch, registration and render-material raise guards (LSan-clean under the tests that run them, but no test
    raises from a draw callback); console draw NaN layout; session animate NaN; llm-batch UTF-16 limits.

- gate-bots1 (2026-11-11), bots/behavior/library open items, not fixed:
  - Dense pointer-ID tables (weights heap, character strings, BSP allocations, goal info entities, libvars
    records/strings) are sized by the saved pointer values; a crafted save with a pointer near 2^32 makes C
    allocate tens of GB (OOM abort) where TS uses sparse Maps.
  - call_steps carries a thrown error as text only: the class is lost and it re-raises as RangeError.
  - log.c has no borrow identity: TS throws "Cannot write a closed bot log borrow" for a stale filePointer();
    C writes to the current log.
  - A retired goal or chat setup keeps its undisposed source reader (TS parity: its scripts stay live in the
    bot memory), so its C structs leak; preprocessor.c include_directive also leaks its temporaries when the
    resolve callback raises (ui legacy script, not this packet). Both show under LSan; the review test
    suppresses exactly qa_load_item_config and load_parse_run.
  - Checkpoint builders other than goals (weights, character, weapons, bsp, chat, libvars) leak their partial
    tree when a reference raises midway (invariant violations only).
  - Seen while building, outside this scope: src/compat/qc/mod_provider.c:1962/1970 did not compile (mid-edit,
    gate-cqc); src/compat/qc/presentation_host.c:83 unused format_text and
    src/app/bootstrap/simulation/bot_assets.c:16 unused put_u32 warn.

- D9d (what D9b left open, 2026-11-10/11, build/agents/d9d). Rows `d9d` of docs/port_review.tsv.
  (1) Graphical Quake II presents. Cause: src/render/scene/resources.c kept each created image's content copy "until
  the drain after the one that returned its release", which assumes a drained batch is applied before the next drain.
  A frame builder drains at begin, at world preparation and at every view before its frame executes, so an image
  created and released before the first frame (the `*white` of the menu font's temporary texture loader, ordinal
  15316 on base1) was drained with the first batch and its copy freed by the next drain of the same frame. Fix, in
  the owner: a drained array is one allocation holding everything its operations point at (create content as well
  as update levels); the registry keeps nothing of a drained operation. src/render/cpu/textures.c kept pointers into
  the create's content (palette, translation, fullbright) where TS slices them: it owns copies now, as the GL table
  did. startup_application_graphics.c freed the previous drained array at each drain (the old assumption); it keeps
  every array of a frame until the next frame begins. The other consumers (presentation.c, q3_client.c,
  application_transition.c, remote_application_world.c, renderer.c, render/worker.c, scene/world.c) keep or copy the
  array until its operations are applied. Test: tests/render/test_image_registry_lifetime.c against
  tests/render/oracles/image_registry_lifetime.ts (12 checks; segfaults against the previous library).
  (2) tests/app/test_q2_original_saves: import, classic, level-change, sidecar, engine-logic and rerelease pass (109
  checks; the level-change and sidecar cases are new, classic gained the level metadata checks). Found on the way,
  none reached by a C test before (no C test had run a graphical world replacement or loaded a Quake II save
  through the Application):
  simulation: sourceModels kept each model event's strings by pointer into storage the producer rewrites (the
  classic guest's appearance slots): the map owns copies (qa_app_shared_sim_set_source_model); the restored actor
  registry kept provider strings of a decoded checkpoint freed at once (kept with the simulation now);
  restore_spatial relinked retained actors whose ground/owner pointed into index members clear() had freed;
  sourceItemsRestore.finish was handed the real registry and inventory where persistence takes fn+ctx tables (a
  call through garbage); `ck_oom(effective)` raised "Out of memory" for an empty inventory list; the saved movement
  kind of Quake II classic was spelled "q2" where TS has "q2-classic", so no engine-logic Quake II save loaded; the
  builtin native primary cache was eight entries keyed by the prepared guest's address and never emptied ("Native
  primary cache is full" after eight guests in one process, and a reused address would return another module's
  profile): it is keyed by module digest and edition and has no cap.
  application: the world transition passed the staged scripts as a command owner (`owner.cvars` NULL, a crash in
  ApplicationInput's constructor check) where TS passes them among the input actions; kept records whose release
  frees the record itself (the saved clients record, a seat's cvar registry) were freed twice at destroy; a
  replacement's debug graph and its recording host were both kept records and the application's own; a replaced
  source-command preparation was destroyed while the retiring input's actions still routed through its Q2 console
  (kept until the application is destroyed now); three `qa_cvar_registry_register/set(..., NULL)` calls (the
  registry fills its snapshot unconditionally).
  compat (forced, outside app/bootstrap): src/compat/q2/rerelease/body_shape.c bound the dimensions entry directly
  and called the original through the module, which re-entered the wrapper without end; it uses
  qa_compat_q2_native_entry_bind now, as TS uses bindNativeModEntry, and releases the frame on a raise.
  (3) tests/app/test_shared_simulation_b1, _b3 and _rest set the source strategy after every source.kind and pass.
  (4) TS tests ported: tests/app/test_d9d_drift.c (archive-reload "a dedicated server reads the archives it saved
  and saves them again on exit"; native-files "migration moves a stock game's demos into its own folder, never
  overwrites, and is safe to rerun"). NOT ported, for want of time, each needing application plumbing no C test
  has yet: tests/app/source-owner-archive (planWorldReplacement with a Team Arena skirmish), the front-end case of
  settings/archive-reload (StartupApplication product change), the rest of settings/native-files (demo extension
  table, screenshot numbering, sidecar, rerelease key names: not checked against existing C coverage),
  tests/app/configuration-content (nine cases), the remote-client case of tests/app/console-log, and the changed
  first-playable/input/settings tests (console-input, kex-q2-load, q2-native-foreign-character, q2-native-original,
  renderer-restart, network/q2-downloads, remote-application/download-permission, q2-native-hud-prepare,
  q2-native-save, input/mouse-application, settings/audio-preferences, settings/frontend-mouse).
  (5) Hygiene: every binary under tests/app, tests/render and tests/persistence ran through tools/run_capped.sh;
  afterwards `find ~/qfiles -newer <marker> -type f` listed nothing and `~/.local/share/quake-typescript` was
  identical (624 entries, path/size/mtime). No C path needed a change: the game write redirect is read in one place
  (content/catalog) as in TS.
  (6) persistence/q1.c and q2_containers.c (classic server) release their partial state on a raise (the LSan
  report). NOT done: the native config script stays UTF-8 while other console scripts are raw bytes. TS strings
  have two C encodings here (code-unit bytes in the console/game/wire domain, UTF-8 in settings files and typed
  input, which the console decodes with a Latin-1 fallback); the native config's UTF-8 is what makes the file it
  writes back byte-identical to TS. Converting the script alone at the console boundary would break that; a
  correct change converts at every settings/console boundary (readScript, readBindings, write, writeBindings, the
  layers' absorbSession) and needs the console's own convention for typed text settled first.
  Results (build/agents/d9d, every binary through tools/run_capped.sh with stdin closed, QTS_TEST_CORPUS=~/qfiles):
  all 80 binaries under tests/app exit 0, among them test_remote_loopback 9 families at 0 differing (54 checks),
  test_application_open_real q1/q2/q3 at 0 differing (12 checks), test_sim_verify 10 cases, 0 failed,
  test_q2_original_saves 109/0, test_d9d_drift 6/0, test_d9b_drift 47/0, test_startup_port 457/0,
  test_startup_graphics 17/0; all 18 under tests/render and all 13 under tests/persistence exit 0.
  ASan/UBSan (build/agents/d9d/asan, tmp/asan.sh): test_image_registry_lifetime 12 checks and test_d9d_drift 6
  checks, clean with LeakSanitizer on; test_q2_original_saves sidecar (10), engine-logic (13), level-change (9) and
  classic (44) clean with leak detection off; its import and rerelease scenarios were not run under ASan.
  test_d9b_drift with QTS_TEST_CORPUS=/nonexistent: no leak from persistence/q1.c, q2_containers.c or core/binary
  any more; LeakSanitizer still reports 71310 bytes in 1513 allocations from Application.open raising over the
  missing corpus (the three dedicated qconsole.log cases), a raise path this packet did not touch.
  Left open: the unported TS tests in (4); the native config script encoding in (6); the Application.open raise-path
  leaks above; LSan over test_q2_original_saves; the CPU renderer was not run on a Quake II world after the fix
  (the test uses the default renderer unless Q2_ORIGINAL_SAVES_CPU is set).
- D9b (TS drift 87e98c5c -> 1.2.7 in src/app/bootstrap except simulation, 2026-11-10). Per-file notes are the
  `drift-done (d9b)` rows of docs/port_review.tsv; the new C files are console_log, native_settings, native_migration,
  q2_native_saves and seat_profile. Checked: tests/app/test_d9b_drift
  (unit sections from console-log, settings-persistence and native-files tests, a 98-line bun transcript
  tests/app/oracles/d9b_drift.ts covering options, console log, seat profile, demo library, native settings over a
  discovered catalog, native save folders and StartupSaves, and the three dedicated qconsole.log cases over the corpus)
  and tests/app/test_q2_original_saves `import` (prepareApplicationSave over the corpus's real save folder against
  tests/app/oracles/q2_original_saves.ts: 7 lines, 0 differing). Left open:
  (1) A graphical Quake II application raises on its first presented frame in C: the texture loader's `*white` image
  (1x1, ordinal 15316 on base1) reaches the renderer with `content.levels` pointing at garbage ("CPU image dimensions
  disagree" on the CPU renderer, "OpenGL image descriptor dimensions differ from its base mip level" on GL; no image
  refresh and no texture loader destroy happens before it). Any module (gamex86 or the engine's own logic). Until then
  the `classic`, `engine-logic` and `rerelease` scenarios of test_q2_original_saves (the save/load/autosave checks of
  q2-native-original-saves.test.ts) fail at that frame; they are written and need no change once Q2 presents.
  CLOSED by D9d (the registry's copy lifetime; the scenarios then needed the fixes listed there).
  (2) TS tests of this round not ported: tests/app/source-owner-archive, tests/settings/archive-reload,
  tests/settings/native-files (demo recording into the game folder, migration, script read-back; its demo library
  and layout cases are covered), tests/app/configuration-content (redirected product folder), the remote-client case
  of tests/app/console-log, the level-change and sidecar-agreement cases of q2-native-original-saves, and the
  changed first-playable/input/settings tests (console-input, kex-q2-load, q2-native-foreign-character,
  q2-native-original, renderer-restart, network/q2-downloads, remote-application/download-permission,
  q2-native-hud-prepare, q2-native-save, input/mouse-application, settings/audio-preferences, settings/frontend-mouse).
  (3) Test hygiene, new with this drift: a stock game now writes its config, logs, demos, downloads and saves beside
  the game data, and loading content runs migrateNativeData against the user store. C tests have no counterpart of
  tests/support/hermetic.ts. test_remote_loopback, test_application_open_real and test_startup_graphics now set
  `QTS_TEST_GAME_WRITE_ROOT` beneath their scratch roots; any other test or tool that opens an application over the
  real corpus must do the same or it writes into the install, and one that loads content with the default user root
  (several rows of test_startup_port, test_app_concrete_cinematic, ...) migrates demos and engine-format saves out of
  the developer's real ~/.local/share/quake-typescript. Pointing HOME at a scratch folder made the bun oracle of
  test_application_open_real sit idle (cause not found), so that is not yet a usable sandbox.
  (4) The native config script (qa_native_config_read_script) is UTF-8 text while every other console script is the
  file's raw Latin-1 bytes; they differ for bytes above 0x7f only. Settled by gate-app1.
  (5) Forced edits outside src/app/bootstrap: content/catalog/index.{c,h} (`qa_game_write_redirect` and
  `qa_game_download_directory` lost their user-root argument: TS 93ab0b6f keys the shared redirect by the install
  root alone, after D9a ported 03a43888) with tests/settings/test_settings_drift9.c re-recorded from bun;
  persistence/kex_q2_save.{c,h} (`qa_kex_q2_save_restorable` deleted, its check removed from
  tests/persistence/test_persistence_core.c); simulation/save.{c,h} (`qa_app_sim_native_q2_rerelease_save_decoded`,
  the decoded form of nativeQ2RereleaseSave that q2-native-saves needs).
  (6) Found and fixed on the way (all older than this drift): application_services.c handed the native Q2 server
  hosts the address of a block-local guest world (crash in the first recording observe of a native Q2 game);
  campaign_cinematic.c compared a restored resource with the mounted one as JSON text, which the parsed key order
  made unequal (test_app_concrete_cinematic aborted with "cinematic resource changed"); tests/app/
  test_app_concrete_precache built a simulation with no strategy (segfault since the SourceStrategy port);
  tests/app/test_configuration.c did not compile after `redirected_content` joined the catalog product;
  tests/app/test_renderer_content.c expected remote downloads in the user store (TS now uses the game folder).
  (7) CLOSED by D9d. tests/app/test_shared_simulation_b1, _b3 and _rest segfault (not D9b's files): their hand-built simulations
  have `strategy == NULL` since the SourceStrategy port (first hit `qa_app_shared_sim_players`); each needs
  `sim->strategy = qa_app_create_source_strategy(sim)` after every `source.kind` it sets, as
  test_app_concrete_precache now does.
  (8) The bun oracle of test_application_open_real opens a dedicated console on stdin: run that test with stdin
  closed (`< /dev/null`), or the oracle never exits when stdin is an open pipe.
  Results on 2026-11-10 (build/agents/d9b, every binary through tools/run_capped.sh, QTS_TEST_CORPUS=~/qfiles):
  test_remote_loopback 9 families at 0 differing (54 checks); test_application_open_real q1, q2, q3 at 0 differing
  (12 checks); test_sim_verify 10 cases, 0 failed; test_startup_port 457/0; test_startup_menu 38/0;
  test_startup_graphics 17/0 against its oracle re-recorded from TS 1.2.7 (1494 lines; the only change is that
  default.cfg no longer reports vid_config_x/vid_config_y); test_d9b_drift 47/0; test_q2_original_saves import 2/0;
  test_renderer_content 173/0; test_configuration 133/0; test_remote_application 346/0; test_top_turntable 230/0;
  test_app_concrete_cinematic 5/0; test_app_concrete_precache 77/0; of the other 65 binaries under tests/app 62 exit 0
  and the three in (7) segfault. (The 62 ran before the last one-line fix in startup_application.c, a NULL guard.)
  ASan/UBSan (build/agents/d9b/asan, tmp/asan.sh, 2040 objects): test_d9b_drift without the corpus, 41 checks, no
  memory or UB error; LeakSanitizer reports 64 bytes in 7 allocations, all on raise paths of persistence/q1.c and
  core/binary (the invalid save fixtures of the StartupSaves rows). test_q2_original_saves has not been run under
  ASan.
- D9a (TS drift 87e98c5c -> 1.2.7 outside app/bootstrap and render, 2026-11-10). Per-file notes are the
  `drift-done (d9a)` rows of docs/port_review.tsv. Left open:
  (1) `qa_kex_q2_save_restorable` (persistence/kex_q2_save) no longer exists in TS; it stays in C only because
  src/app/bootstrap/original_save.c and startup_saves.c still call it. D9b removes the calls, then the function.
  (2) A rerelease `NAME.pak.gz` written by `qa_pack_kex_q2_save_archive` holds the same pak as the TypeScript byte for
  byte, but its gzip stream is not Bun's: system zlib and Bun's deflate choose different codes (215 against 228 bytes
  for the test archive; no zlib level, memory level or strategy reproduces it). Each inflates to the identical pak.
  (3) C strings end at the first NUL: the TS refusals of an interior NUL in a configstring and in a save entry have no C
  form, and a rerelease length-prefixed string holding a NUL reads as its first part.
  (4) Not covered by a C check: the redirected product folder in catalog discovery (`redirected_content`; the TS
  tests are app-level: tests/app/configuration-content, source-owner-archive), the bindings menu scope toggle
  (no TS unit test), and tests/text/truetype "a failing glyph table is skipped in the atlas" (ParsedFontT is an
  interface in TS and an opaque parsed font in C).
  (5) TS tests of this round that drive src/app/bootstrap and so belong to D9b/D9c: settings/archive-reload,
  settings/settings-persistence (seat-profile and option cases; its isInstallRoot expectations are ported),
  settings/native-files (demo recording, demo library, migration), settings/audio-preferences, frontend-mouse,
  input/mouse-application, compat/q2/classic/services (guest file-write quota, simulation/classic-guest-files),
  compat/q2/classic/world, app/*, first-playable/*, platform/*.
  (6) Forced call-site edits in src/app/bootstrap: network/q2_downloads.c (total_budget_bytes gone),
  simulation/save.c (provider version through `qa_save_provider_version_supported`),
  simulation/shared_simulation_checkpoint.c and simulation/native_mod_host.c (restore without the identity). The
  simulation still writes `q2:rerelease-native-original` at version 1 (accepted on read); D9c moves it to
  `QA_NATIVE_Q2_ORIGINAL_VERSION` and handles a NULL level path in its restore callbacks.
  (7) Found, not in D9a's files: src/compat/qc/mod_commands.c `qa_compat_qc_console_call` leaks its `args` and
  `globals` arrays (lines 21-22) when `resolve()` raises for a missing argument, because no cleanup node covers
  the two loops (184 bytes in tests/compat/test_qc_mods_depth under LeakSanitizer). tests/movement/
  test_undef_movement_materials has 5 failing CHECK_ABORTS (qa_mat_shader_table_index, qa_mat_evaluate_waveform):
  src/materials/material.c changed on 2026-11-03 and the test still expects the aborts.
- D6 (compat + guest TS drift, 2026-11-03) leftovers. Per-file notes are in docs/port_review.tsv (`d6-compat`;
  `drift-partial` rows name what is missing). (1) Perf-only TS changes not mirrored, each claimed
  behaviour-preserving by TS (its differential tests): guest/x86/fast.ts (I386Fast predecoded path) and the
  i386 decode cache, guest/floating-point/fast-arithmetic.ts with the SSE `fastBinary` and x87
  `fastBinaryInstruction` paths, PERF1 page-indexed write observers in guest/core/memory.ts (C full scan delivers
  the same ranges and counts `writeDeliveries`), QvmMemoryWrites watched pages, QVM lazy resolver words, QVM
  mod-provider observe() reuse/ProjectionSlots index, QVM mod player-event skip, Q2 native-mod per-slot field
  watches, native-mod-provider readOnlyImports/projection index, rerelease host direct reconcile skip, classic
  records cache. (2) Error aggregation on close (`AggregateError`) in qvm/mod-provider, qc/mod-provider and
  q2/native-mod-provider is not represented: the C close functions run every step but cannot collect raised errors.
  (3) Async guest calls (GuestCallRunner.invokeAsync, promise host callbacks, Quake3VmModule.callAsync) have no C
  counterpart; the C runner is synchronous. (4) Not wired by D6 (D1 territory): native Q3/Linux qagame hosting in
  simulation/q3/native-guest-source.ts (it needs qa_native_game_memory, qa_native_q3_game_call, the 16-word
  qa_compat_q3_native_syscalls handler, qa_sparse_allocator_image/restore_allocator and the Windows/System V
  heap_state/restore_heap_state added here), and the NativeGuestHeapImage bytes (persistence/execution, D8) — the
  heap image bytes were therefore not compared against bun. `qa_qvm_semantics_from_cvar` exists but the
  q3-client/qvm.ts and guest-runtime.ts callers (D1/D2) do not pass it yet. Classic `standInActor` is a host
  option nobody sets yet (classic-guest-services.ts, D1). (5) TS tests of the drift not reproduced in C: the
  native Q3 synthetic/host tests, runner-inline chargeBytes/recover tests, x64/x86 differential and fast-path
  tests, guest elf mutation, system-v module-check, the windows installed/msvc/services changes, Q2 classic/native
  primary 1998 profile tests (need the 1998 Xatrix DLL fixture), and most compat/qc, compat/qvm and
  compat/q2/rerelease test edits. New checks: tests/guest/test_guest_drift.c, tests/compat/test_compat_drift.c
  (oracle tests/compat/oracles/d6_drift_oracle.ts). (6) tests/compat/test_qvm_b.c line ~1343 expects
  `capture_part(ctx, "graph") == NULL` from the Q3 runtime table, but the uncommitted
  src/app/bootstrap/simulation/q3/runtime.c (04:21, not D6) raises "Unknown Q3 native save part graph", so the
  binary aborts there; app/bootstrap owner to reconcile the expectation.

Each is assigned or waits for the batch named. Remove an item only once it is done
and tested.

- D8 (misc TS drift, 2026-11-03): done files are the `drift-done (d8-misc)`, `drift-done (d8b)` and
  `drift-done (d8c)` rows of docs/port_review.tsv. D8b finished audio, camera, capture, console, content/archive,
  content/catalog, content/composition, content/mounts, content/user-data, core/{common-parse,game-numeric},
  formats, foundation and media (including the removed foundation/{arena,handles,pool,sys-queue,vec},
  media/{roq-presentation,transitions,index}, core/diagnostics, console/{log,index}, formats/q3-model/{animation,index}
  and the types-only rows of those modules). D8c finished input, llm, materials, persistence, platform, settings, text,
  ui/{common,hud,library,mods,saves,settings} (rows `drift-done (d8c)`), including the added platform/signal-hint,
  settings/setting-spec, ui/common/controls and text/localization-key, the removed persistence/q3, platform/runtime and
  the input/materials/platform/settings/text index barrels, the settings-menu leak (setting controls now use
  caller-owned qa_ui_setting_control_storage; test_ui_controller_fixes is LSan-clean), music onFault in
  app/bootstrap/audio/music.c, and bun's JSON.parse wording (value.c's parser follows JavaScriptCore's LiteralParser).
  D8c tests not ported (harness or fixture changes, corpus-gated, or needing a full Application/SharedSimulation):
  tests/input/input.test.ts (haptics fixture rewrite), llm/settings.test.ts "saving an API key after sign-in keeps the
  subscription", llm/request.test.ts, materials/{animated-images,image-resolution,images,sky-clouds}, persistence/
  {native-weapon,provider-ownership,q1-source,q3-native-graph,q3-native-server,qvm-*,reference-save-fixtures,
  source-saves,save}.test.ts and the q2-classic-guest/save-policy message tightenings, platform/{controller,
  window-*}, settings/* (corpus-gating, preferences and server-ui harness edits), text/{accessibility,presentation,
  sound-captions,sound-captions-table,world}, the truetype hostile-font case (needs a fake ParsedFontT; C fonts are
  concrete structs) and ui/hud/q2-native.test.ts. audio/engine.c (D8b) keeps a private SdlAudioDevice.removed copy
  that calls abort() when SDL enumerates no output (names == NULL); qa_sdl_audio_removed is the platform port.
  Not ported from the D8b modules: tests/audio/{bootstrap,precache-retail,rocket-effects,startup,loop-aggregation}
  (app-level or corpus-gated), the corpus-gated q1-map cycle cases (C has synthetic equivalents), and the
  corpus-gating and message-tightening edits in tests/content/catalog/{anchor-discovery,digest-cache,equipment,
  expansion-weapons,monster-roster,monsters,weapons,start-maps,qvm-weapon-behaviors}; tests/audio/undecodable.test.ts PLATFORM-003 (audio/bank, a d8-misc file) has no C case.
  D8b gaps: content/mounts keeps a fresh directory read instead of TS's cached-listing-first lookup (same result
  except when a cached hit has gone stale), and media/playback still hands tick callers a copy of the retained
  picture (TS now shares it). Known: world/actors/source_slots.c still sets the dropped
  ActorLifetimePolicy clear "source-gentity" for Q3 (D7 file); recipe.c's 34 KB "leak" was the save-image parse
  not freeing what it had read before a later field raised (fixed in save_image.c; test_save_mutation is LSan-clean).
sim_verify: C equals the current TS bun output
  in all 10 cases (0 differing lines). The stored oracles are current: D1 regenerated them from TS HEAD and got
  identical bytes. The checkpoint-image mismatches some agents saw came from the user content root following
  TMPDIR (its path is recorded in the images). Fixed 2026-11-03: test_sim_verify and its oracle now always use
  <repo>/build/agents/sim-verify2/tmp/sim-verify-user-<case>; with the full tree of ~10:30 all 10 cases have
  0 mismatches with TMPDIR unset.
- D4 (content/q2 TS drift, 2026-11-03) leftovers: (1) done by D1: `qa_app_sim_movement_player.up_move` is set in
  moveCommand and passed to `qa_q2_player_movement.up_move`.
  (2) TS tests from the drift not reproduced in C: rerelease monster AI "follows the rerelease source" (3 tests),
  q2-base "medic whose patient is alive again ... alarm does not mark the player hostile", weapons "a version 2
  weapon checkpoint ... still loads" (the C persistence reader accepts v2; "ammo dropped mid-fire" is checked at the
  consume step only),
  and the changed expectations inside TS gameplay/q2-* files that have no C mirror (no C fixture runs full Q2Monsters
  with traces, or the Q2 weapon fixture). New C checks: tests/content/test_q2_drift_d4.c. (3) test_sim_verify q3qvm
  crashes in compat/qvm ("invalid QVM instruction index 103974", then `qa_qvm_files_close_all(NULL)` in
  `qa_app_q3_qvm_server_game_discard`) with the tree of 2026-11-03 ~09:00 (compat/qvm mid-edit by another packet);
  the other nine cases have 0 differing lines. [D6, ~09:40: all ten cases 0 mismatches after the compat/qvm edits
  settled; `qa_qvm_files_close_all` now returns on NULL.] (4) tests/contracts/test_header_audit_a aborts on "Q1 official spawn
  not yet implemented: monster_army" (content/q1, D3).

- Q2 native guests (2026-11-02, sim-guests): `qa_app_sim_native_guest`
  (classic_guest_source.c) implements the `qa_compat_q2_memory`/`qa_compat_q2_runner`
  tables over SparseGuestMemory, GuestCallbackTable and GuestCallRunner, including
  register access, inline-region accepts, unwind (`qa_guest_call_runner_unwind`) and
  free processor. The retail CTF/Xatrix API3 and rerelease API2023 DLLs run through
  ClassicGuestSource/World and RereleaseGuestSource/World, and NativePrimaryWeapons
  runs on the API2023 DLL (tests/app/test_sim_guests.c). The native mod classes
  (NativeModProvider over `simulation/native-mod-host.ts` and `native-mod.ts`) run on
  the retail CTF API3 and rerelease API2023 DLLs (tests/app/test_native_mod_host.c);
  checkpoints match the TS classes byte for byte (oracle:
  tests/app/oracles/native_mod_host_oracle.ts). Still open: the C
  GuestCallRunner aborts on GuestCallStopped, so `runner.recover()` has nothing to
  restore. RereleaseWeaponBehaviorSource is ported but unexecuted: the corpus has no
  DLL matching any native weapon declaration (the built-in one is q2eaks).
- `src/bots/behavior/q3/library.c` must bind the library's `_configure` and chat or
  action hosts. Assigned: Q3 bot foundations.
- `preprocessor.c` root-frame leak per script reader. Assigned: Q3 bot foundations.
- `src/render/gl/renderer.c` was rewritten by two porters in turn and is unrun;
  `qa_gl_renderer_present` needs `swap`, which `src/app/bootstrap/renderer.c` does
  not set. Assigned: render top; the swap is for the connection step.
- UnifiedApplicationServer aborts the process on a malformed peer message, where TS
  drops the peer. Remote crash. Assigned: network follow-up. [gate-appnet: settled; every peer
  message is decoded and handled under qa_try in unified_server.c poll and the peer is dropped.]
- Evaluation order: C leaves argument and initializer order unspecified, so ports of
  `f(read(r), read(r))` can run reversed. Assigned: evaluation-order sweep.
- Full-tree ASan and UBSan run over every test binary: at the gate, once edits stop.
- Native mods follow-ups (2026-11-02, native-mod): the TS guest memory gained pooled
  pages mid-session (`SparseGuestMemory` "a pooled page can only be released whole";
  TS ClassicGuestSource/RereleaseGuestSource close now fails in the oracle). [D6: C guest
  memory has pooled pages now (allocate_pooled_page/release_pooled_page, and the
  allocator image); the test_native_mod_host oracle has not been rerun by D6.] `__stdio_common_vsprintf` reads its size_t count as int64, so the
  MSVC `sprintf` count SIZE_MAX aborts on 64-bit guests ("Guest printf buffer count is
  not size_t") where TS accepts it (the scanf count is fixed). classic records
  `resolves` called the projection's `address`, which aborted where TS catches and
  returns false (fixed by gate-cq2). System V format service states are freed with their
  runtime through `qa_system_v_own` (gate-guest2), as the Windows ones are through `qa_windows_own`. Not yet assigned.
- TS errors that are caught and recovered from: earlier porters aborted instead
  (see docs/PORTING.md). Re-check each module's TS `catch` blocks at the gate.
- Audio: TS prefers an SDL3 backend (`platform/audio.ts` `sdl3Backend`) and falls back
  to SDL2; C has SDL2 only. Port the SDL3 backend and the same selection order.
- Identity-keyed caches not ported: `render/scene/prepare.ts` pose caches,
  `retainedMerge`, GL uniform caches, the guest call-plan cache, the Q1/Q2 alias
  topology cache. They need an invalidation hook (model unload) so a reused
  address cannot return stale data. Port with the connection step.
- Q3 runtime owner (`app/bootstrap/simulation/q3/runtime.c`): with a Q3SourceHost the owner wires weapon fire and
  mover spawns to the owned missile and mover runtimes, the runtime restores the graph through
  `qa_q3_prepare_q3_graph`/`qa_q3_restore_q3_graph`, and close/destroy close the missile runtime. The table's
  `restore_part("graph")` (unit fixtures only) still uses the old reader.
- Gate full read still to do (app/bootstrap top-level "the rest", second half: read by gate-app14 except
  weapon-behavior-selection.ts and weapon-behavior-tool.ts, which gate-clean2 read in full; app/bootstrap top-level packets 1 and 2: application.ts is read in full (gate-app1, gate-app2,
  gate-app5); content.ts, prepared-startup.ts and image-settings.ts are read in full (gate-app5, gate-app10), as are the
  demo-*.ts files (gate-app1, gate-app5, gate-app10); plan packet 2 (remote) is read in full: gate-app3 and gate-app6; top-level packet 3 (startup) is read in full: gate-app4 and gate-app8 (the other startup-*.ts files and gate-app4's kept divergences); top-level packet 4 (presentation and the rest) PARTLY: gate-app7 and gate-app9 (presentation, presentation-scene, presentation-state, effects, effects/*, mod-presentation(s), character-turntable read in full); gate-app11 read q3-client and q3-client/*, audio, audio/music, audio/q2-events, renderer and assets, see its entry; of "the rest" of the top level, authored-start through player-userinfo (40 files) are read in full by gate-app12 (see its entry), precache through audio-settings in the gate-app7 list remain; gate-app13 read ui.ts's listed members, input.ts and the rest of assets.ts, see its entry; ui.ts's constructor, draw, drawNativeQ2Hud, centerPrint, clearPrompt and closeMenus are read by gate-app15, so ui.ts is read in full (open: the rerelease presentation's shared-settings view, gate-app15 entry)); see the gate-app9 entry under "Gate read packets" for the files left; app/bootstrap/simulation packet C is read in full: gate-simC4 finished it, see the gate-simC to gate-simC4 entries under "Gate read
  packets"; app/bootstrap/simulation packet B: read in full by gate-simB, gate-simB2 and gate-simB3 (see their entries under
  "Gate read packets") but native-mod-presentation.ts beyond the HUD stats (gate-simA4's round); app/bootstrap/simulation packet A: every runtime.ts member read once (gate-simA to gate-simA7 entries
  under "Gate read packets"; gate-simA7 read the last ~80); app/bootstrap/network is read in full: gate-appnet3 finished it, see its paragraph)
  (2026-11-03, after review-c and review-d; bots/behavior/q3 leftovers are
  listed under the bots packet above; bots/behavior/library and the top-level behavior files are read in full,
  gate-bots1; bots/navigation is read in full, gate-bots3 and gate-symbols): core: read in full (gate-core finished cvars/index and
  setting-declarations); persistence: gate-core read save-image, recipe, mods, shared, save-policy, q1, q2-containers,
  kex-q2-save, source-json, provider-ownership, native-weapon and execution's guest/layout readers; gate-core2 read the
  q2-*/q1-* codecs, qvm-grapple, protection, most of source-items and the execution identity readers (see the packet);
  gate-persist3 read q2-base-entities, q2-monsters, q2-players, q2-weapons, the rest of q2-rerelease-state, world-state,
  source-items' prepareSourceItemRestore and the rest of execution.ts (see its packet entry); gate-clean1 read
  save_image.c's builders (image_value and every *_value helper): persistence DONE; contracts: every runtime function read (type-only files not);
  settings: config, setting-spellings, native-saves, server/rotation, selection, common, cvars read; gate-core2 read
  restart, server/profile, library, q1, types and dumped every server setting collection against bun; gate-persist3
  read layers, native-config, server/lmctf-cvars and the rest of q2-owner: settings DONE; text/truetype; materials except color.ts and the evaluation
  and name helpers of material.ts (gate-amf2 read fog, legacy-fog, q2-lightgrid, q2-lighting, dlight, sky,
  turbulence, geometry and cinematic in full and replayed the ShaderParser body, registration, material-finish with
  the default profile, material-iterator and source-state against bun; left: deform, legacy, lighting, q3-lighting
  unread; evaluate after its pass loop, and qa_evaluate_material_passes leaks its buffers when a callback or
  consistency check raises; compile.ts shaderRenderMaterial, shaderSurfaceFlags, compileImplicitMaterial;
  material-finish non-default profiles and implicit inputs; the replayed files line by line; all DONE by gate-misc3).
  Audio is read in full (gate-amf, gate-amf2). gate-amf's UTF-8 findings are fixed (gate-amf2); the common_parse byte
  counting, the tcMod sub-parser length, the decoded U+0000 and the BSP/script name equality are fixed by gate-misc3
  (residuals under gate-misc3 above); libm
  sin/cos/exp/acos may differ from JavaScriptCore's in the last ulp (the reverb replay matched); engine actor and
  mixer transmission lookups are linear scans (stopChannel and ov_clear fixed by gate-misc3); src/ui/common/legacy/script/lexer.c:1765
  read_string_field is unused and warns (gate-legacy owns the file). md5 scale JSON dedupes keys with a linear scan
  (quadratic on a crafted object) and ibsp44 model walks use linear sets (quadratic on large test maps); world: read in full (gate-world 225, gate-render 395, gate-world2 183; see
  port_review.tsv), and the gate-render world findings are fixed (gate-world2: session.ts
  closeAll at all 13 sites, retired-bundle double close, mod-files path.resolve, observers
  kept past their call raise "closed" through closure handles, original-pickups releases
  scopes at close). World items closed by gate-clean2: authority per-hit storage (closed
  cursors, decision mutation copies) is freed when the next outermost damage call begins,
  create() states with their binding, pickup-match states by the match release hook; a late
  grant() of a selection used before its scope closed throws; native_mod.c/qvm_mod.c
  already borrow the command port (D1c settled). World still open: retired session
  worlds/seats/clients are freed when their bundle closes, so a pointer kept past that is
  undefined (no such pointer was traced by gate-clean2; not audited caller by caller). Movement is read in full (gate-world, 2026-11-03). Render is read in full: 864
  functions (gate-render, gate-render2, gate-render3, gate-render4, gate-render5; see port_review.tsv). Model shadows
  (r_shadows 2/3) are ported and equal bun on q3dm1, and every Q3 map of the baseq3 pk3s (37) loads and equals bun
  (gate-render4). gate-render5 (2026-11-10) read the GL backend (gl/* except textures), worker, worker-protocol,
  worker-transport, worker-runtime and the row-parallel/row-worker wire and fixed 42 divergences
  (tests/render/test_gate_render5.c, 94 checks, clean under ASan/UBSan/LSan). The GL backend's call stream equals
  the TS GlRenderer's for tests/render/oracles/gate_render5.script (7 scenarios, 7254 GL calls, 66 thrown errors),
  both run over the same recording GL model. The worst three: the GL packed-geometry cache served stale geometry
  for reused storage; a throwing object-opacity child left every later draw bound to the scratch framebuffer; the
  worker wire dropped viewProjection and batch space (TS 2a2cb7bd, wrongly marked not applicable by the drift pass).
  Open from gate-render3, closed: app/bootstrap/assets.c destroys an uncommitted refresh's remaps
  (prepared_images release; commit nulls its own, gate-clean2 checked); formats/images/limits.c still has
  the image dimension cap TS 392943c8 removed (not in src/render; D9; closed: D9a removed it and gate-amf confirmed
  qa_image_size_allowed is width*height*4 <= 0x7fffffff). Open from gate-render4, closed by gate-render5: the app call sites set has_custom_skin; the render worker
  park/resume failure is TS's AggregateError; error names cross the worker boundary unchanged. Still open: the gate-render3 fixes
  listed in port_review.tsv as having no dedicated check still have none. Render leaks under LSan in
  test_remote_loopback are fixed (68.5 MB to 0 in src/render, gate-render2).
  Input, text, media and platform read in full (gate-misc, gate-misc2); capture, debug read against v1.2.7; console
  and camera/application re-read (gate-misc3); llm read in full (gate-misc3, gate-misc4) and camera/spline line by
  line (gate-misc4); then content, compat, bots, network, ui (gate-ui; gate-legacy: legacy runtime in full, menu parser; menu.ts data-layout classes left to their tests), app (not started).
  compat/qvm (gate-cqvm, 2026-11-11): 29 of 93 files read in full (212 functions, 29 fixes;
  tests/compat/test_compat_qvm_gate_review.c with oracles/compat_qvm_gate_review.ts runs hand-assembled programs
  through both interpreters: every opcode with 48 edge words, memory and control-flow failure paths, both
  semantics, 13216 checks, clean under ASan/UBSan/LSan). Fixed: errors crossing a syscall, hook or module call
  kept neither class, code nor full text (a guest Com_Error arrived as RangeError code 0); guest-controlled
  nesting through hooks and reentrant syscalls had no bound on the C stack (now RangeError "Maximum call stack
  size exceeded." at 1024 levels); a malformed QVM header aborted the process; 680 raise sites were RangeError
  where TS throws Error; the file traps' restore, close and publish paths did not roll back as TS does.
  LEFT in compat/qvm, 63 files: abi, artifacts, body-scope, bot-library-syscalls, bot-navigation-records,
  bot-navigation-syscalls, cgame-body, cgame, client-render-syscalls, client-script-syscalls, client-state-record,
  client-state, compatibility, game-combat-binding, game-combat-scope, game-combat, game-equipment-movement,
  game-input, game-inventory, game-pickups, game, game-weapons, grapple-profile, grapple-provider, item-catalog,
  item-storage, legacy-bot-abi, legacy-bot-syscalls, legacy-client-abi, the 17 mod-* files (mod_provider.c holds
  checkpoint, fields, projections, spatial and validation), module, player-record, the five primary-* profiles,
  regions, registry, render-record, script-record, shared-entity-record, symbols, trace-record, ui,
  weapon-behavior-profile, and the copy/write forms of entity-record. Open items from the read: the function
  resolver's argument words are built eagerly (TS on request); QvmMemory.writeString takes C bytes where TS takes
  UTF-16 units masked to a byte; guest-memory copy() reuses one buffer; cgame.c raises a dropped lifetime's error
  as RangeError because the lifetime state carries only its text; the file-trap fixes are not reached from a test.
  compat/qvm (gate-cqvm2, 2026-11-11): 36 more files read in full (370 functions by a declaration count, 112
  fixes; rows in port_review.tsv): game, game-combat, game-combat-scope, game-combat-binding, game-input,
  game-equipment-movement, game-inventory, game-pickups, game-weapons, the five primary-* profiles,
  grapple-profile, grapple-provider, weapon-behavior-profile, item-catalog, item-storage, body-scope, cgame-body,
  mod-objectives, mod-actor-frame, mod-pickups, mod-player-events, mod-input, mod-clients, mod-protection,
  mod-weapon-stage, mod-items, mod-actors, mod-presentation-checkpoint, and of mod_provider.c the mod-spatial,
  mod-fields, mod-projections and mod-checkpoint parts (the host-state reader was rooted at "mod-host" where TS
  uses the default path "save", so every refusal of a saved checkpoint had the wrong text, and a refused
  checkpoint leaked its decoded image). In mod-spatial the guest pointers of the spatial traps were used as raw
  byte offsets where TS masks them to the allocation and refuses a null one (guest.view). tests/compat/test_compat_qvm_gate_review_mods.c with oracles/compat_qvm_gate_review_mods.ts
  (1155 checks, clean under ASan/UBSan/LSan): the item table reader, item storage, the body call scope,
  objective addresses, actor bootstrap stores, the combat call validators, words and damage flags, and the
  grapple profile reader (every field, and which refusal a declaration with two faults names). The main class
  of fix: TS try/finally and catch sites with no C counterpart once a guest call raises (a raise is now
  catchable, so each one left state behind): scope and frame lists not popped, write watches and function
  observers left registered against released state, guest words and scratch bytes not restored (weapon delay,
  attack animation, damage factor, spawn point, teleport, grapple pull and mover hooks, equipment selection,
  damage scratch), hooks left bound after a refused constructor. Others: `find` taking the last match (armor
  tier, team value), a guest word used as a shift count, double-to-int32 casts where TS wraps, a pointer kept
  into the live item catalog across a guest call, a nested-damage cap of 32 frames, readers that named the
  wrong refusal because they read fields in another order, a command written back to guest memory when the
  application left it unchanged, AggregateError on close.
  LEFT of gate-cqvm2's scope, 3 TS files: mod-presentation (C mod_presentation.c, 1801 lines), and in
  mod_provider.c (about 5000 lines) mod-provider and mod-validation; none of their functions were read beyond
  the spatial trap, the field helpers, the projection list and the checkpoint decode. Seen there in passing and
  not fixed: the engine traps read guest strings that leak when the service they are passed to raises
  (mod_provider.c engine_syscall, e.g. the server command text); the provider's host restore applies the saved
  host state while it reads it, where TS decodes and validates the whole state first (decodeQvmModHostState),
  so a malformed state leaves the C provider half restored; src/compat/qc/mod_provider.c:1725 roots its host
  reader at "mod-host" too (TS qc mod-provider.ts:748 uses the default "save"; another reader's file). Outside the scope: src/world/gameplay/inventory.c
  qa_retired prints and aborts where TS throws SourceItemRetired, which QvmModItems catches to cancel the
  retired weapon input (mod_items.c now handles that raise by name once it exists). Open: the hook fixes
  (cgame-body, combat scope and binding, pickups, weapons, grapple provider, input, clients, protection,
  weapon stage) are not reached from a test, which would need a source module that raises inside each hooked
  call; reader path objects leak on a refused primary declaration; the builtin primary profile mapping was read
  but not dumped against bun; combat bindings keep one small context per admission until they close; a raise
  from a finally-side restore that runs while another raise unwinds is dropped (TS lets it replace the first).
  compat/qvm (gate-cqvm3, 2026-11-11): the module layer, the remaining traps and the records, 25 TS files read
  in full (186 functions by this reader's count, 20 fixes; rows in port_review.tsv).
  tests/compat/test_compat_qvm_gate_review_records.c with oracles/compat_qvm_gate_review_records.ts (4630 oracle
  lines, 7633 checks, clean under ASan/UBSan/LSan): every record of both ABI profiles read, written and copied
  against bun bytes (playerState_t, entityState_t, usercmd_t, gameState_t, the source snapshot_t, sharedEntity_t,
  trace_t, refEntity_t, refdef_t, polyVert_t, orientation_t, pc_token_t, the bot goal, init-move, move-result,
  entity-state and entity-info records), the import decoders over 649 words, region qualification over 3000
  generated functions, the VM registry, the symbol table, the compatibility declaration, the client script
  owner. Fixed: TS catch and finally sites with no C counterpart (QvmModule.create and QvmUi.create did not
  retire the module on a refused UI version or owner; QvmCgame.init did not retire on a guest error inside
  CG_Init; QvmClientScripts.load and read left the handle pending and the operation count raised, so a guest
  could exhaust the 63 handles with refused paths and block every checkpoint); a function resolver allocated one
  hook adapter per resolved guest call for the life of the module (mod-actors resolves every actor callback);
  the AddPolys extent overflowed int64 for guest words; the three bot traps that return float bits unmasked
  (TS refuses a sign-bit float from an interpreted module at signedWord); BotInitMoveState read its record
  before the handle check; the legacy player write cast event words to int32; restoreCheckpoint of client
  scripts restored earlier rows before a malformed later row failed; guest strings leaked when a later read or
  the service raised (render, script and bot library traps); QvmModule.invoke raised RangeError where TS throws
  Error; a dropped cgame lifetime's error lost its class; the compatibility declaration lost the SaveReader
  paths of its primary and equipmentPresentation subtrees. Outside compat/qvm: the 11 plain DataView sites that
  raised the writable view's "Offset is outside the bounds of the DataView" now raise bun's "Out of bounds
  access" / "byteOffset cannot be negative" (palette.c, truetype.c, freetype_layout.c, rank_codec.c, kex
  discovery.c, bots library bsp_entities.c, chat_data.c, weapons.c, qvm_grapple_source.c, q3_client/qvm.c).
  For other owners: src/app/bootstrap/simulation/q3/guest_artifact.c:125,142,146 roots the primary reader at
  "qvm.primary" and src/compat/qvm/primary_presentation_profile.c:137 roots the equipment reader at
  "qvm-compatibility.json"; TS uses the declaration's own readers ("qvm-compatibility.json.modules[N].primary",
  "...modules[N].equipmentPresentation"), now carried as declaration.primary_path and
  declaration.equipment_presentation_path. Open: the fixes in module, cgame, ui, the render, bot library and
  bot navigation traps and client-script read/restore are not reached from a test (they need a module over a
  program image, renderer resources, a bot library or mounted content); bot navigation reads points, goals and
  move results eagerly where TS reads through getters (differs for a null or short record the operation never
  reads, and for a goal overlapping the result); FIND_MATCH stores back only changed record fields; a function
  symbol sampled before a symbol reload names its record by index; a module destroyed without retire() would
  leave its interpreter pointer in a registry slot (no application code creates a registry); gate-cqvm's open
  items (eager resolver words, writeString bytes, guest-memory copy()) are unchanged.
  LEFT in compat/qvm after gate-cqvm, gate-cqvm2 and gate-cqvm3: what gate-cqvm2 lists (mod-presentation,
  mod-provider, mod-validation). [gate-cqc read these three: see the next entry.]
  compat/qvm leftovers, compat/qc and compat/q3 (gate-cqc, 2026-11-11; build/agents/gate-cqc; rows in
  port_review.tsv). compat/qvm: mod-presentation (13 fixes), mod-validation (2) and mod-provider (26) read in
  full. The provider's host restore now decodes and validates the whole saved state before applying any of it
  (decodeQvmModHostState, split out of host_image). The main class of fix in the provider is the one
  gate-cqvm2 found in the hooks: TS try/catch/finally with no C counterpart once a source call raises
  (begin, finish, invoke, the console and client commands, initialize, pickupContext, observePickup, refresh,
  the actor scratch, the weapon scope, adoptSource and retireSource: frames left pushed, globals and scratch
  not restored, a write watch left registered on a dead stack frame, pickupContexts stuck). Others: scene
  publications borrowed their command texts from the command ring and from a save tree that was freed (use
  after free once a command was evicted, or when a restored baseline was captured or presented); the
  configstring command text overflowed its buffer on control bytes; close() stopped at its first raise and,
  removing while iterating, released only every other owned actor (TS runs every step and throws
  AggregateError); a refused projected write was queued as a commit before it was validated; the weapon scope
  compared frame stack slots where TS compares frame objects; restore accepted a saved state with no files
  field. In mod-presentation: a 512-entry cap on declared storage addresses (written past at the 513th), the
  scene traps used a guest word as a byte offset, host restore checked rows while reading them, int32
  overflow in hook pointer sums, a read guest checkpoint never released.
  compat/qc: 26 of 38 TS files read in full. The QuakeC machine, loader, string arena and pure builtins are
  replayed against bun: tests/compat/test_compat_qc_gate_review.c with oracles/compat_qc_gate_review.ts (968
  checks: 99 progs.dat images through the loader, 819 hand-assembled programs through the machine under the
  Q1 and Q3 numeric profiles, every opcode over edge operands, operand words outside the globals, entity
  references, pointers, fields, strings, branches, the statement, stack and locals limits, calls, STATE, the
  builtins, and the machine state left by a refused statement; 50 string-arena sequences with refused
  checkpoints; clean under ASan/UBSan/LSan, which found a leak in varString). Fixes: operand words were read
  in C call-argument order, so a statement with two words outside the globals named the wrong one; error
  texts were cut at 2047 bytes; a restored string checkpoint that repeats an engine name resolved to its
  first entry; leaks on refusal in varString, objerror, the executor, aim, setmodel and source values; the
  qc mod provider's host reader was rooted at "mod-host" (TS: "save") and leaked the decoded tree on every
  refusal. Error classes: 225 raises across compat/qc and compat/q3 were RangeError where the TS throw (matched
  by message) is Error or TypeError, and six close/release paths were RangeError where TS throws
  AggregateError; all corrected. NaN: the sign of a NaN that TS stores into a QuakeC word depends on the JIT
  tier (the same program run six times in one bun process stored 0x7fc00000 or 0xffc00000 for 0 / 0), so the
  C machine keeps storing the canonical NaN and the review test compares NaN words by class.
  compat/q3: 7 of 7 files read in full: NativeQ3ServiceUnavailable and NativeQ3GameError raised as RangeError,
  a guest memory fault that is not a permission fault flattened to RangeError, read-only copies kept until the
  memory was destroyed, two traps reading arguments in C call-argument order.
  world/gameplay/inventory.c: qa_retired is a raise named SourceItemRetired (was print and abort).
  [gate-cqc's 12 LEFT compat/qc files were read by gate-cqc2: see the next entry.] Open after gate-cqc: none of
  the compat/qvm provider and presentation fixes, the qc host leak fixes, the q3 fixes or the inventory raise is
  reached from a test (each needs a provider or module fixture whose source call raises); compat/q3 module.c has
  no address-space identity check; tests/compat/test_qvm_provider_depth.c leaks one cvar string of its own
  (LeakSanitizer).
  compat/qc rest (gate-cqc2, 2026-11-11; build/agents/gate-cqc2; rows in port_review.tsv): mod-environment,
  mod-input, mod-pickups, mod-actors, mod-clients, mod-validation, mod-messages, mod-combat, mod-protection,
  mod-items, presentation-host and the rest of mod-provider read in full (about 160 functions; 60 fixes);
  compat/qc is now read in full. The QVM provider's fault classes were each looked for: TS try/finally and
  catch sites ran as throw-time cleanups (invoke skipped objectives.flush() on a raise; the binding release and
  close paths, the release subscription, the input close, reserve/activate in protection, admit/release in
  items and the pickup admission now use qa_try with TS's error precedence and AggregateError texts); close()
  stopped at its first raise (now every step, one AggregateError); inventory lookups took the last match;
  projections and actorsBySlot were one array where TS keeps two Maps; match-player cells lived in a
  reallocating array; guest names were cut in fixed buffers (engine-string keys, deathtype, precache, sound and
  model paths, print texts, the dropped-message report at 2047 bytes); two-fault orders (restore checked the
  duplicate routed key before the bytes, a multicast destination read reliable first, the messages host read
  the clock before its presentation check, combat read the deathtype before the time global); a set-view
  without an owned actor was refused where TS clears the view (presentation-host, quakeworld-presentation).
  Freed-edict reads, foreign-slot quarantine, the QuakeWorld setmodel rule, the ex_* localization keys, NQ
  non-client sprint/centerprint prints and withholds_kills match TS. Settled: the executor host format is
  decoded as TS (whole field, fatal UTF-8, BOM dropped, a NUL kept); q1-source-save also replaces byte 0xa0 (the
  save text is one byte per TS character); the compatibility JSON is decoded fatally; error texts quoting guest bytes above 0x7f stay raw
  bytes by design (C carries a QuakeC string as one byte per TS UTF-16 unit everywhere, and guest refusals are
  caught, never printed by an abort); the stale qvm/mod_items.c comment is fixed. tests/compat/
  test_qc_provider_depth.c now runs on the TS synthetic Copper program (oracles/qc_copper_synthetic.ts, 32
  checks) and on the installed Copper when present. Review test: tests/compat/test_compat_qc_gate_review_mods.c
  with oracles/compat_qc_gate_review_mods.ts (13 checks; clean under ASan/UBSan/LSan, which found a NULL memcpy
  in the routed-buffer restore). Open: most provider fixes (try/finally, AggregateError, two maps, pickup
  commit chain) are not reached by a test (they need a provider fixture whose callbacks raise);
  qa_compat_qc_damage_inputs keeps the request in a static copy (its caller is app/bootstrap quakec_source.c);
  the items invoke cells grow per admission until destroy; the Q1 save comment is a UTF-8 path basename that
  nothing converts to TS byte characters (a non-ASCII name differs; boundary in app/bootstrap and persistence).
  compat/q2 (gate-cq2, 2026-11-10): 19 of 75 files read in full (the classic and rerelease hosts, imports, module,
  records, printf, pmove, inventory, cvars, the layout and combat tables; 62 fixes; tests/compat/test_compat_q2_gate_review.c,
  2804 checks). Still to read: the 56 files listed under gate-cq2 above (native primary, native mod provider and its
  parts, native pickups and input, both combat bindings, foreign actors, deferred damage, the rerelease profiles).
  compat/q2 (gate-cq2b, 2026-11-11): 28 more files read in full (classic combat binding, native combat call, damage,
  input, pickups, the native primary files, 9 of the 13 native mod files; 107 fixes;
  tests/compat/test_compat_q2_gate_review_b.c, 1397 checks).
  compat/q2 (gate-cq2c, 2026-11-11): the last 28 files and native_mod_host_api.c read in full (native-mod-combat,
  -items, -protection, -provider and 24 rerelease files; 326 functions, 144 fixes;
  tests/compat/test_compat_q2_gate_review_c.c, 2208 checks). Nothing is left to read in compat/q2. Open there: the
  rerelease player state borrows its stats (contract type); the catch, finally and AggregateError paths have no
  dedicated check (they need a guest or an authority that fails part way).
  Content outside q1/q2/q3 (gate-content, 2026-11-10): mounts, archive, catalog, roots, user-data and the small
  top-level readers are read in full (30 files; 89 divergences fixed in the packet; tests/content/test_content_gate_review.c,
  500 checks). gate-content2 (2026-11-11) read the rest of composition, content/mods (native-callbacks in TS, its C
  replayed against bun only), the monsters files and q3/presentation/client.ts: 52 divergences fixed,
  tests/content/test_content_gate_review2.c (24446 checks). Still to read there: content/legacy-script in full
  (346 functions: lexer 75, preprocessor 127, memory 31, source-memory 42, token-memory 37, precomp-memory 34). Read
  so far: the lexer's public calls and nextInto, the preprocessor's directives, macros, expression parser and reader
  calls; not read: lexer.c 1-1075 and its save state, the four memory files, and the preprocessor's macro table,
  global defines, builtin expansion, save state and async read. Also native_callbacks.c line by line.
  Content/q2 (gate-q2a, 2026-11-10): foundation, 40 of 46 files read in full (list and per-file notes in
  port_review.tsv), 60 divergences fixed; checks in tests/content/test_q2_gate_review_foundation.c (1207 checks,
  bun oracles tests/content/oracles/q2_gate_review_foundation.ts and q2_weapon_attachments.ts/.txt), clean under
  ASan/UBSan/LSan; the 25 other Q2/content/sim test binaries pass and test_sim_verify is 10 cases at 0 mismatches.
  Found on the way, outside content/q2: world/actors/registry.c kept the per-session generation table keyed by
  the identity token's address for ever, so a new session at a reused address continued a dead session's
  generations (test_q2_composition then failed or passed with the heap layout); qa_identity_owner_destroy now
  tells the registry to drop it (qa_identity_owner_on_destroy, additive in contracts/identity.h).
  The recurring classes: TS WeakMap<Q2Entity, state> ported as tables keyed by entity pointer (a freed entity's
  state matched the next entity at that address; motion, angular-motion, items, movers); a released Q2Entity
  freed while its callers still read it (entity-services keeps it until the source clock moves on); Map order
  lost by swap-remove where a capture lists the map (ballistics, monsters); loops over host.nearby()'s reused
  buffer that damage inside the loop (bfg, and the same pattern fixed in rerelease berserk.c and missionpacks
  nuke.c, bolts.c); host.random() skipped where TS draws it unconditionally (pickTarget, item respawn); fixed
  caps (item ids 512, item team 64, train route 256, admission destinations 16, 128/512-byte text buffers);
  try/finally sites with no cleanup on a raise; `throw new Error` raised as RangeError (about 45 sites).
  The six files gate-q2a left (monsters/index.ts, perception.ts, checkpoint.ts and the tables muzzle.ts, frames.ts,
  moves.ts) are read by gate-q2c, see below: content/q2/foundation is read in full. Open from gate-q2a: source_hosts.c q2_nearby caps its
  result at 256 actors and reuses one buffer (TS returns a fresh unbounded array); shared_simulation_core.c
  qa_ss_q2_reaction_end copies an attack provenance by value with its attacker/inflictor pointers (stable now
  when the Q2 services made it, not for other producers); qa_q2_item_module_observe_supply returns a weapon
  offer's ammo in a thread-local buffer the next call reuses (bot_world.c reads it at once);
  rerelease/monsters/common.c qa_q2rm_bind_armor never frees its binding context (LSan, test_q2_rerelease_monsters;
  fixed by gate-q2r: the records carry the actor identity and are reused, the test is clean under LSan);
  a dead train corner fires no pathtarget where TS would fire the first target of the dead object.
  Fixes with no dedicated check: the item-table and mover-table reuse of a freed entity's address, the
  power-armor cell context move, the bfg nearby copies, lag-compensation rewind on a raise, capture cp_teams.
  Closed by gate-q2c (2026-11-10): observeSupply keeps a weapon offer's ammo in the caller's observation; trainWait
  runs useTargets over a snapshot of the released corner (first target only, as TS); the item-table and mover-table
  reuse and the cp_teams capture now have checks (tests/content/test_q2_gate_review_c.c). Still without a dedicated
  check: the power-armor cell context move (missionpacks/monsters/power_armor.c), the bfg nearby copies and the
  lag-compensation rewind on a raise (both need a Q2 weapons world in the test).
  Content/q2 foundation monsters and multiplayer (gate-q2c, 2026-11-10): monsters/index.ts, perception.ts,
  checkpoint.ts and the three tables, and the 21 files of content/q2/multiplayer, read in full (per-file notes in
  port_review.tsv); 52 divergences fixed; checks in tests/content/test_q2_gate_review_c.c (3754 checks, bun
  oracles tests/content/oracles/q2_gate_review_c.ts and q2_monster_tables.ts), clean under ASan/UBSan/LSan, as
  are test_q2_multiplayer, test_q2_monsters and test_q2_rerelease_monsters; the 30 Q2/content/sim test binaries
  pass and test_sim_verify is 10 cases at 0 mismatches. The worst: a monster context and its state were freed
  the moment its actor was released while die(), moveFrame() and endFrame() still read them (now retired until the
  next beginFrame); entity.lastAttack pointed into the pending-damage record processPain frees (read by every
  later save); respawn, resumeMonster, spawnInfantryDriver and restore did other things than TS (a revived monster
  lost its enemy and kept its old think, turret drivers were not counted, restore hooks ran before the other
  contexts existed); the rerelease combat-point attack and per-player noise refresh in checkAttack were missing;
  the CTF checkpoint encoder wrote its keys in another order than TS (the save text differed); the CTF
  playerlist could write past its buffer; LMCTF never released the shared grapple on death, team change or
  disconnect. Open from gate-q2c: equipment/ctf_grapple.c and lmctf_grapple.c have no delete for one actor's
  state, so `grapple.states.delete(actor)` in ctf/index.c disconnect and lmctf/runtime.c disconnect only reaches
  the inactive map (gate-q2b's files); the CTF and LMCTF checkpoint decoders do not release their per-field
  readers when one raises (the tree and the copied data are released). No dedicated check (needs a full
  CTF or LMCTF match in the test): the ctf/lmctf text-buffer and UTF-16 fixes, selectSpawn's random draw, the
  LMCTF flag map order, the LMCTF grapple abort and restore fixes; unreachable from a test: endFrame's stable sort
  (every entity has its own source slot, so no two keys tie) and the perception restore of duplicate noise or
  trail entries (referenceSaved needs a restored registry).
  Content/q1 (gate-q1, 2026-11-03): foundation (17 files) and base (16 files) read in full,
  19 divergences fixed, checks in tests/content/test_q1_gate_review.c (bun oracle
  tests/content/oracles/q1_gate_review.ts). Gate-q1b (2026-11-03 and 2026-11-10): quakec
  (14 files plus compat_types.c), equipment (2), the 15 top-level addons files and ctf/grapple.ts, ctf/travel.ts read in full
  (see port_review.tsv), 31 divergences fixed there and the entity-string, backpack-message
  and deathmatch-spawn caps removed in foundation/base, checks in
  tests/content/test_q1_gate_review_b.c and test_q1_gate_review_c.c (oracles
  q1_gate_review_b.ts, q1_gate_review_c.ts). Swept over
  all of content/q1: entity loops that remove while iterating (only the Threewave grapple was
  wrong), fixed-size game.find buffers (six 256-entry sites overran), C math look-alikes and
  unsequenced random calls (none). Gate-q1c (2026-11-10): addons/ctf (11 files, 124 functions),
  horde (4, 68), items (7, 70) and monsters (45, 415) read in full; no behaviour divergence in
  ctf, horde or monsters, 7 fixes: a fractional MG3 upgrade impulse (111..115) granted an upgrade
  where TS grants none, and six leaks when a malformed save raises (the saved navigation route
  arrays, the "mg3:world-words" name copy, and the controller created for a saved demodog,
  ordinary monster, boss or base monster with an unknown continuation; the last is in
  base/creatures.c, the same pattern). Checks in
  tests/content/test_q1_gate_review_d.c (oracle q1_gate_review_d.ts), clean under ASan/UBSan/LSan;
  the same binary built without the five addon leak fixes reported 9 leaked allocations. The
  frame tables in scope are equal to TS by test_q1_frame_tables, whose oracle text equals a fresh
  bun dump; the 100 sphere points were compared entry for entry. Not changed: horde door_touch
  forwards the touch surface where TS passes none (base door_touch does not read it); the Error
  throws in these files that reach the top level in TS stay qa_range_error aborts.
  Missionpacks (gate-q1mp, 2026-11-10): all 76 files read in full, 1035 functions, methods and
  closures; 14 fixed. Behaviour: Rogue team messages lacked the empty `args` list every TS team
  message carries (the unified event encoding differed) and cut player names at 96..256 bytes;
  impulse 9 asked the cheatsAllowed host and the rune frame asked the gamecfg host where TS
  short-circuits; impulse 202/203 cut a map classname at 512 bytes. TS `finally` not run when the
  callee raised: the horn charmer (arsenal), targetEvent's target and message (world/common), the
  finale callback's classname (world/campaign). Leaks on a raise: the controller of a saved
  monster with a malformed state (monsters/runtime restore, as base/creatures), and five scratch
  lists held across damage or removal callbacks (commands genocide walk, plasma touch, Vengeance
  spheres, eel zap, ltrail removal). Checks: tests/content/test_q1_gate_review_mp.c (62 checks,
  oracle q1_gate_review_mp.ts), clean under ASan/UBSan/LSan. No `game.find` user reads past a
  fixed buffer (all take the first match or count first). The 14 frame tables equal TS by
  test_q1_frame_tables, whose oracle text equals a fresh bun dump. test_q1_mpm and test_q1_sync_mp
  skipped their retail-map cases (0 and 128 checks) because QTS_TEST_CORPUS was unset; they now
  default to ~/qfiles as test_sim_verify does and run 275 and 282 checks, 0 failures. Not matched: TS compares
  ActorId with `===` (identity) in the charmed-monster target code, lavaman hunt and a few touch
  callbacks; C compares values, which is the same for ids of the live registry (observations,
  references, trace hits). Not changed: scratch lists held across host hooks in rogue_teams.c
  (score, color, disconnect) are not released if a hook raises; obituary text passes through the
  512-byte qa_q1_obituary.text. Nothing is left to read in content/q1. Caps
  left in content/q1: qa_q1_intermission_result text 4096 and map 64 bytes (base/rules.h, used
  by the app), 16 powerup kinds per player. Not matched: qa_q1qc_function_end for a function
  that starts past the program end.
  The app-side Q1 primaryWeaponHandoff
  (simulation/arsenal/q1.c, D1c's area) was not read.
  Unported TS tests from review-c: component-command-owner case 2, local-seat-change,
  q3-native-runtime, q3-native-server, qvm-local-runtime, qvm-map-lifecycle,
  qvm-runtime-save, reference-save-fixtures.
  Content/q2 missionpacks (gate-q2mp, 2026-11-10): 71 files read in full (see port_review.tsv); the 24
  mission-pack move/frame tables, the brain eye offsets, the nine weapon definitions, the inventory and icon
  lists and the random-item tables equal a bun dump value for value (tests/content/test_q2_gate_review_mp.c, 98
  checks, oracle tests/content/oracles/q2_gate_review_mp.ts, clean under ASan/UBSan/LSan). Fixed, behaviour:
  hint-path chain walks read a zero-capacity buffer (touch on any chain of two or more nodes crashed); the
  rogue monster state handed out pointers into a realloc array; mp_internal.h bit helpers cast doubles at or
  above 2^31 to int32; monster power armor's bound set outlived its session and leaked its closures;
  target_blacklight effects were signed and trigger_disguise kept bits above 2^32; target_anger walked the
  shared game.targets scratch and target_killplayers the live entity list; Tag and Deathball changeDamage
  reduced a hit with no attacker when nobody held the token or ball; the powerup list lost Map order on
  reset and was cleared before a throwing restore; widow kick and widow2 crunch knockback with no enemy body,
  widow2 attack on a dead enemy that still has a body, widow2 tongue damage flags; gekk jump and shrink
  did not link the body; the two lag-compensation try/finally sites in weapons/player.c; fixbot scratch
  growth; acid gib and widow gib model-name leaks; cut diagnostic and hint issue texts. Not covered by a
  check: target_killplayers snapshot, widow2 crunch, fixbot scratch, the lag finally sites, items power use
  NaN (the fixes are by reading). Open: decodeQ2RogueHintsCheckpoint and its caller in
  src/persistence/q2_missionpacks.c leak reader paths and the half-built checkpoint when a malformed save
  raises (persistence reader); the nine content-q2mp files were not re-read by this packet.
  Content/q2 base and equipment (gate-q2b, 2026-11-10): all 65 files read in full (player 11, entities 7,
  monsters 23, the 19 monster tables, equipment 5; 601 functions, methods and closures by pattern count; see
  port_review.tsv). The 19 move/frame tables and frame-name maps and the twenty classic definitions equal a bun
  dump value for value (FNV of the dump text; `--dump` on the test and the oracle prints both). No divergence
  in the monster behaviour files' control flow, numerics or random-call order. Fixed, behaviour: connect rebuilt
  a rejected userinfo with repeated keys and compared values through 511-byte copies; chase gathered at most 64
  players; checkRules kept the text after a second `$` in the spawn point; the `target` cheat iterated the
  shared game.targets scratch across use callbacks; `use`/`drop` unknown-item text and the target_actor message
  were cut; autoshield as the last userinfo key read -1; releasing a pushed actor reordered the wind-time
  capture; target_string indexed with a non-integer count; func_clock did not restore target/message when
  useTargets raised (TS finally); restore appended a repeated player. Fixed, C lifetime: the player model text
  lived in the entry table that moves on join/leave (another player's model, or freed text on a corpse); the
  Makron kept a pointer to Jorg's target text, freed with Jorg's head gib; the teleporter trigger, coop spots
  and the intermission landmark name borrowed entity text; the brain power-armor closures read a freed entity
  and its bound set outlived the session (now retired through qa_identity_owner_on_destroy); target_laser's
  passed list was a function-static shared by a re-entrant think; the platform, secret-door, animation, clock,
  breach and driver tables (TS WeakMap<Q2Entity>) were keyed by entity pointer and only grew (entries now carry
  the actor and are dropped on release); medic patient() walked the host's reused nearby buffer; grapple states
  were array elements that moved under callers (now one allocation each). 27 `throw new Error` sites raised
  RangeError; authored-text diagnostics were cut at 255/511 bytes. Checks: tests/content/test_q2_gate_review_base.c
  (100 checks, oracle q2_gate_review_base.ts), clean under ASan/UBSan/LSan; the committed versions of five of the
  files fail 9 of its checks. Not covered by a check: the Makron/teleporter/coop/landmark lifetimes, the laser
  re-entry, the clock finally, autoshield, the brain binding, restore of a repeated player, the release pruning.
  qa_q2_connection_result no longer has the 4096/256-byte limits (gate-q2r: the userinfo is text owned by the
  players object, valid until its next connect; reason is static text). Left: player userinfo/name/skin are fixed buffers at the TS slice sizes; flyer nextMoves is
  keyed by the game pointer (read only by callbacks no move table selects); chat `slice(0, 150)` counts bytes,
  not UTF-16 units; missionpacks/monsters/power_armor.c has the brain's bound-set pattern (other packet).
  content/q3 base, foundation and equipment (gate-q3a, 2026-11-10): 71 of 74 files, 741 functions, 98 fixes;
  tests/content/test_q3_gate_review_base.c (38 checks, bun oracle tests/content/oracles/q3_gate_review_base.ts;
  the direction-byte, item and game-cvar tables equal a bun dump). Patterns fixed: `throw new Error`/TypeError
  raised as RangeError (about 170 sites); TS `vec3()` (binary32) written as an unrounded C compound literal
  (setOrigin, trace probes, launch/drop vectors, mover-spawn withComponent, portal and kamikaze vectors);
  `(a - b) | 0` done as two int32 conversions; TS try/finally without a C counterpart (combat dispatch, rail
  relink, withCurrentOrigin, spawnEntity catch); spawn parser errors without their TextParseError position;
  bound player-state slots cut to int32 on read; deltaAngles words rounded to binary32 by mover pushes; a
  static portal context shared by every pool; shared-actor origin cells allocated per call.
  The items gate-q3a left in this scope (rankings.ts, save-state.ts, save-values.ts; qa_q3_read_graph's missing
  fields; loadQ3Character's lookup order; pickupItem's handicap read; areaActors' maximum; leaks on save-restore
  failure paths) are done (gate-q3b, 2026-11-10: 50 functions, 12 fixes in the three files and 14 in the open
  items; tests/content/test_q3_gate_review_saves.c, 61 checks). Left: dedicated checks for the gate-q3a fixes
  that need a TS mover/missile/spawn harness, listed under gate-q3b above.
  content/q3 presentation (gate-q3p, 2026-11-10): 28 of 43 files, 525 of 660 functions, 44 fixes;
  tests/content/test_q3_gate_review_presentation.c (76 checks, bun oracle
  tests/content/oracles/q3_gate_review_presentation.ts, clean under ASan/UBSan/LSan; cvarTable equals bun for
  both products). The worst: a malformed animation.cfg aborted the process (TS catches the parse error and falls
  back); the grapple cable started 26 units low and the tracer muzzle used animations 9/11 for crouch instead of
  13/23; four view-weapon axes used the float angle rule where TS uses the VM rule; a Team Arena impact for a
  weapon with no case of its own played the lightning sound and drew a random number; TS try/finally with no C
  counterpart left a flag set for good after one raise (frame drawing, snapshot transitioning, configuration
  updating, Mission HUD loading, sound and resource operation counts), so one dropped snapshot or one bad HUD file
  refused every later call; a server command that threw did not dispose the command runtime or keep rejecting
  with the first error; a cvar value of 256 bytes or more threw every frame instead of once. Pattern: 185 raise
  sites across presentation and team_arena raised RangeError where TS throws Error or TypeError (matched to the
  TS throw by message and rewritten by script, unread files included); CommonError drop and SessionDataError
  sites raised under the TS class. The four frame-registration expectations of tests/content/test_q3_owner_draw.c
  that had been derived by reading are now recorded under bun (tests/content/oracles/q3_frame_registration.ts;
  all eight cases of that function match). Fixes with no dedicated check (they need the full client fixture):
  the frame drawing flags, every players.c fix, the VM axes, the railgun flash alpha and flash origin, the item
  second-model types, the server-commands record/expected-{/head-voice/colour-0 texts, audio and resources
  operation counts, Mission HUD loading, the HUD corner formatter, events.c, view.c, prediction.c, model-access.
  Open: restoreCheckpoint in audio.c and resources.c registers each row as it reads it where TS reads every row
  first; same() and the client-info/chat length cuts count bytes where TS counts UTF-16 units; feeder item text
  is borrowed from the client-info name or a reused buffer.
  content/q3 presentation remainder and team-arena (gate-q3c, 2026-11-10): 14 presentation files (119 functions,
  22 fixes) and the 15 team-arena files (255 functions, 19 fixes), plus one line in base/game/format.c;
  tests/content/test_q3_gate_review_ta.c (18 checks, bun oracle tests/content/oracles/q3_gate_review_ta.ts,
  clean under ASan/UBSan/LSan). The worst: a first connect to a team game with g_teamAutoJoin formatted a NULL netname
  and crashed (a NULL %s text now formats as "", the C spelling of the TS empty name); the obelisk startsolid
  warning and a location with no message dereferenced NULL where TS prints "(null)"; adaptForeignQ3Objectives
  overflowed its buffer for an entity with no fields; setLeader printed a freed name after clientUserinfoChanged;
  `vote`/`teamvote` with no argument read past the buffer; touchTriggers and clientThinkReal walked host buffers
  that a touch callback refills (a teleporter's kill box queries the same area list); Math.imul products of
  cvar-sized operands (g_inactivity, g_forcerespawn, timelimit) overflowed a signed int; a bot disconnect with
  no bot service raised RangeError where TS throws ClientCapabilityError (the only wrong class among the 102
  team_arena raise sites, each matched to its TS throw); the mark projector's clip scratch held 64 points where
  one chop can return 122, and its node stack was capped at 4096; mark texture coordinates and quad corners were
  not rounded to binary32. Checkpoint restores (presentation audio and resources, team-arena server commands)
  now decode every row before acting, as TS does. The sound and graphics registration order of media.c equals
  bun for both products, eight game types and buildScript on/off. Settled from the gate-q3p open list: the
  events.c holdable TypeError was unreachable (TS and C both throw CommonError "HoldableItem not found") and is
  removed; the byte-counted name/chat cuts equal UTF-16 counts for Q3 wire text (one char per byte) and differ
  only for non-Latin-1 host text; the Mission HUD feeder text is painted by the list before the next feeder call
  (ui/common/legacy/runtime.c), so the borrow is safe and the 64-byte buffer holds every non-name column.
  Fixes with no dedicated check: info.c, draw_icons.c, ui_adapters.c, scoreboard.c, mission_owner_draw.c,
  cgame_body_profile.c (they need the client fixture or mounted content), and in team-arena the setLeader name,
  the vote text, the owned actor lists, ClientCapabilityError and the callteamvote order (they need the full
  game fixture). Found outside the scope, not changed: qa_net_js_number (network/common/js_number.c) prints
  5.9604644775390625e-8 for 2^-24 where JavaScript prints 5.960464477539063e-8 (the %.15e candidate rounds a
  decimal tie to even and fails the round trip, so 17 digits are used; the 16-digit neighbour round-trips).
  Fixed (lead, 2026-11-10): `qa_js_shortest_digits` in foundation/js.c tries the neighbour on the value's side
  when the nearest k-digit decimal misses, and qa_net_js_number, qa_js_to_fixed and the unified event codec's
  json_number all use it; tests/foundation/test_js_number_text.c compares 29,503 values with bun (0 failures).
  sim_verify: 10 cases, 0 mismatches.
- Review-c/d open findings: app callers not freeing `qa_cvar_transfer` after destroy
  (startup_application_frontend.c, maybe remote_application_seat/profile.c); cvar alias
  declaration errors abort; recipe.c readers leak 34 KB on raise; llm JSON depth cap 512;
  `pmove_ladders`: done by D1 (sourceMovement.ladders reaches the Q3 profile's `ladders_disabled` in player_movement.c,
  and the prediction step options carry `ladders`).
- Rows only located, not read: persistence, llm, contracts, text, media, foundation,
  console, most of formats and core, parts of materials and movement. Covered by the
  full read at the gate.
- The TS working tree is edited concurrently (on 2026-11-02 it had 425 uncommitted
  changes over HEAD f855f940). Ports read whatever was on disk.
  Baseline snapshot: build/ts-baseline/src-20261102-0217.tar. Superseded by
  docs/TS_DRIFT.md: the real port baseline is TS 53ad56d (2026-10-25), and every TS change
  since then (694 src files with code changes, 88 added, 140 added tests) is worked in
  packets D1-D8 with tools/ts_semantic_diff.sh. The gate needs every packet done.
- Startup top layer (2026-11-03, app-startup, app-startup2): `startup-selection.ts` is ported
  (startup_selection.c, StartupSelectionModel over the real catalog, archives, entity
  parsers, launch resolver, mod choices, QVM hook styles, Team Arena and base-arena
  selection; Q2 maps use `qa_q2_parse_entities_checked`; tests/app/test_startup_port.c).
  `startup.ts` StartupApplication is one C type, `qa_startup_application`
  (startup_application*.c; private state in src/app/bootstrap/startup_application_internal.h).
  Ported: the constructor (lobby transitions, StartupSaves, AddonLibrary changed/launch,
  FrontendPreferences dialect/onChange), static open, print, requestQuit, routeCommand,
  frontendCommand (snd_restart, music, cd), connect, lobbyJoinOptions, bindDemoCommands,
  bindFrontendConsole, frontendRoutingState, activateFrontend, failActiveSource,
  returnToFrontend, publishFrontendRouting, closeFrontendMovie (with the renderer clears),
  openFrontendMovie, the five library services, saveFrontendInput, persistPreferences,
  persistPreferencesOrWarn, refreshPreferenceBaseline, stageLobbyLaunch, launch, connectGtv,
  connectOptions, publishPendingSource, publishDemoIntent, dispatchApplicationRequest, step,
  run, close, the public getters, captureNextFrame (qa_startup_application_capture_next_frame
  forwards the renderer's capture request; with no menu it rejects "Startup menu is not
  visible"). Ported 2026-11-03 (startup-gfx), each over its real collaborators with no host:
  FrontendPreferences (bindings() over the ui/settings factories, audioBaseline, the 100 ms
  onChange timer pumped by step(), loadBaseline/saveGlobal/saveAudioBaseline over ConfigStore,
  MouseSettings and audio-settings; the written global-tuning.json and audio.json match TS
  byte for byte), StartupInputProfile (retained/open/save; change detection compares the
  compact JSON texts `qa_input_bindings_json`/`qa_gamepad_tuning_json`/`qa_seat_settings_json`
  in settings/config.c), StartupAudio (UnifiedAudio, SoundBank, ApplicationMusic),
  loadMenuFont/loadMenuTypography (scene image registry, SceneTextureLoader over
  `qa_app_scene_image_reader_open`, the TS mountedImageReader, and the localization coverage
  fallback pages), Application.networkWire (`qa_application_network_wire`; every
  ApplicationNetwork `wire` now returns a `qa_wire_selection`). loadAudioSettings no longer
  aborts on a foreign audio.json: it returns TS {} as the TS catch does. Fixed in
  render/scene/textures.c: ADD_CAND evaluated its xdup argument once per use (leaked candidate
  names) and the decoded PNG/TGA/BMP/JPEG pixel buffers were never freed with the loader.
  Checks: tests/app/test_startup_port.c (preferences bytes, input profile, menu font,
  typography and StartupAudio against oracles in build/agents/startup-gfx/tmp/oracle); wire
  checks in test_q2_app_units, test_q3_app, test_q1_hardening.
  Ported 2026-11-03 (startup2): StartupMenu in the TS options shape (the StartupSelectionModel itself,
  NativeUiArt, body/title TextFontSelection, appearance, characterTurntable, teamArena; the model
  fn-table and `skin_font` are gone), with draw, captionCommands, builderLayout/builderFields/
  builderDropdownSpec/builderFooter/builderViewer and row(); factory scratch is retired per
  generation and freed at the next input()/draw() (it grew without bound before).
  tests/app/test_startup_menu.c compares every RenderCommand draw() and captionCommands() emit with
  the bun oracle tests/app/oracles/startup_menu_draw.ts (13919 lines, 17 menu states, 3 caption
  passes; QTS_TEST_CORPUS). AddonLibrary is concrete (addon_library.c: libcurl catalog fetch with the
  20 s timeout and Cancel, skipped-row count from `qa_parse_addon_catalog_reporting`, cached-catalog
  fallback, HttpDownloadQueue installs, file: packages, importLocal, removeInstalled, purgeHidden,
  hide, ensureDirectory) and is the addons LibraryMenuService (`qa_app_addon_library_service`);
  tests/app/test_addon_library.c compares statuses, entries and the resulting file trees with the
  bun oracle tests/app/oracles/addon_library.ts, no network.
  Ported 2026-11-03 (startup3): openGraphics, graphics.draw and graphics.captionCommands
  (startup_application_graphics.c), in TS order over the real collaborators: renderer preference,
  openInitialConfigurationContent over model.catalog, the configuration store, the source cvar archive,
  ConsoleScriptFiles (`qa_app_open_configuration_scripts`, shared from application_open.c and declared in
  configuration.h), prepareInitialConfiguration with bindDemoCommands, the startup font mounts, InputDevices,
  loadMenuFont/loadMenuTypography/loadNativeUiArt({frames:false}), NativeRenderer.open + refreshDisplay,
  SdlControllers, the rerelease baseq2 theme mounts, StartupAudio open/openOutput/bindOutputCvars,
  SeatUiPreferences, CharacterTurntable, StartupMenu with its full settings list and the lobby view over
  ApplicationLocalLobby, StartupInputProfile.retained, bindInput, setFocus, InputRouter + attachWindow +
  devices.activate, ControllerSettings + bindGyro, SceneFrameBuilder, ApplicationCapture,
  ApplicationVideoRestart, keys.prepare/publish, the client record, bindFrontendConsole, capture.activate,
  videoRestart.register, the saved console history, forwardCommands, the initial requests, flushCommands
  and the pending "initial" source; graphics.draw (audio pump, world guard, menu.draw, the console overlay
  with consoleMetrics/drawConsole, execute). The openGraphics catch closes the opened owners in TS order;
  the closed graphics stay published until destroy (graphics->closed guards close()). main.c's graphical
  run/menu now opens StartupApplication (qa_startup_application_open, run, close; SIGINT/SIGTERM ->
  requestQuit; the host carries the LLM settings as both UI and requester), as TS main; its startup phase
  host, menu construction and nested-game helpers are gone. Fixed: refreshPreferenceBaseline used
  catalog.require where TS uses catalog.product, so a missing product raised "Content <product> requires"
  for the wrong product before openInitialConfigurationContent (C and bun now report
  "Content q1-classic-id1 requires: ..." for an empty --content-root). Defined here because
  startup_application_sources.c calls it: `qa_remote_application_detach_connecting_recording`
  (remote_application_commands.c, the TS detach closure). Checks: tests/app/test_startup_graphics.c, a
  hidden 640x480 CPU StartupApplication over Q1 id1, compares 1496 lines with the bun oracle
  tests/app/oracles/startup_graphics.ts (6 RenderFrames through native.execute, active menu and status per
  step, host prints, captionCommands through the client record); test_main_composition checks the
  StartupApplication failure text against bun. Observation seam: qa_startup_graphics.frame_observer is
  called with each frame before execute (the oracle spies on NativeRenderer.execute). Still open in this
  layer: the menu's lobby `selection` has no qa_composition_identity producer (buildUnifiedComposition
  over loaded content), so creating a lobby from the menu reports "Lobby composition requires
  buildUnifiedComposition, which has no C producer" after the TS hosting-connection check. Found while
  running the binary: platform/audio.c opens SDL2 (sdl2-compat on this machine) where TS prefers SDL3;
  at the default 22050 Hz output the SDL2 probe reports "SDL audio driver cannot report exact queued input
  frames" and StartupAudio.openOutput raises "Audio output is unavailable" (TS opens). The hidden runs here
  pass `+set s_outputRate 44100` (both sides) to avoid it. Not reproducible in C: the TS AddonLibrary "cancel during local
  import" ordering (stop() lands between two awaits of the import; the C import runs inside
  submit()). Fixed (lead, 2026-11-03): `qa_open_archive` leaked the header, ZIP tail and
  directory buffers when a parser raised; each is now released through a cleanup node. Found earlier: ConfigStore.saveSeat writes the parser key order where TS
  stringifies the object as built, so the first StartupInputProfile write of a fresh profile
  differs from TS in key order and in the rumbleStrength key (values equal after load); the C
  ConfigStore has no `decoded` quarantine (`.bad` rename) of an undecodable document. Async becomes synchronous:
  the lobby host/join transitions launch inside the lobby call (TS stages them for
  publishPendingSource); openFrontendMovie reads the startup configuration's mounts (C
  CampaignCinematic.prepare takes a MountedContent, TS passes scripts.openMounted), so a movie
  after a world has replaced those scripts raises "Movie playback requires the startup
  configuration mounts".
  Fixed here: startup_menu.c browser query/favorite/connection/discover failures go to
  browser.status (discover writes String(error)) and StartupMenu.input catches a controller
  failure into its status, as TS; Q3BrowserView maxPing/statusResendTime are JS numbers
  (`qa_q3_browser_number_fn`), so a fractional cl_maxPing is no longer truncated.
  `startup.c` (the former concrete-host shell) is no longer called by main.c; its tests still link it. Found while porting:
  `qa_q3_text_parse_error` (formats/q3_map/entities.c) aborts where TS throws a catchable
  TextParseError, so a malformed Q3 map entity lump aborts prepareMapChoices instead of
  marking that map unavailable; the session composition (network/common/session.h) carries
  the recipe only inside its canonical JSON, which lobbyJoinOptions reads back; nothing builds
  a `qa_composition_identity` from a unified composition yet, so the menu's lobby selection
  has no C producer; the gameplay-mod discovery mounts stay open for the model's life (the mod
  rows borrow their references) where TS closes them when discovery ends;
  demo_recording_commands.c leaves `busy` set if a feed attach raises.
- Q1 sync follow-up (2026-11-02): the quakec source composition in app/bootstrap must
  switch to the id1 `*_create_foreign` constructors. Not yet assigned.
- Q3, from the Q1 reader: `saveEntity` saves `sourceSlot` as null for dynamic entities;
  `armor_stage.c` caches by program pointer.
- Mounts: case-insensitive match is ASCII only where JS lowercases Unicode; the disk
  digest cache saves on every digest instead of the TS 250 ms timer.
- Leaks in `entity_services.c`, `entity.c`, `travel.c`. Q2 rerelease monsters:
  `qa_q2rm_bind_armor` allocates a cell context per monster spawn that nothing frees
  (test_q2_rerelease_monsters, 2 x 80 bytes).
- Parallel game and mirror types: Q3 content has no `*_mirror` types left (GameEntity.r is
  the EntityShared port, GameClient.ps the PlayerState port, items the shared itemList).
  TS `EntityState` is one C type, `qa_q3_entity_state_record` with number storage (the
  wire codecs apply `| 0`/`Math.fround` where TS does). app/bootstrap keeps
  `qa_app_*_mirror` types (remote seats, HUD, q3_client presentation, frame/video time).
  Q2 is one family now (`qa_q2_entity`/`qa_q2_game_services`/
  `qa_q2_foundation_host`; the `qa_q2f_*` family is gone). Q2Players has the TS
  virtual dispatch and Q2RereleasePlayers derives from it; CTF/LMCTF, equipment and
  mission-pack items run on the real weapons, player and item types; Q2 composition
  (content/composition/q2) is ported on the real types with test_q2_composition; the
  Q2 persistence codecs encode the content checkpoint types (one C type per TS type).
  Q2 mirrors still left: persistence/q2_hand_grenades.h `qa_hand_action` (a copy of TS
  HandAction); content/composition/q2_expansion_arsenal now runs on the real types
  (runtime-rest). The Q2 TS drift after the baseline (base player, foundation weapons,
  items, monsters, movers, rerelease monsters common.ts, ctf say_team) is ported (D4,
  2026-11-03; docs/port_review.tsv `drift-done` rows).
- Two recoverable-error mechanisms exist: `qa_try`/`qa_raise` (foundation, about 65
  files) and the Q2 `native_throw.h` channel (16 files). Fold `native_throw` into
  `qa_raise`. Also still aborting where TS recovers: `SourceItemRetired` raised fatally
  in `inventory.c` during `lease.stored`; CommonError "drop" in `core/common_error.c`;
  the Q3 native guest runner is unusable ("wrong architecture") after a host callback
  raises, and leaks.
- `quakec_mod.c` registers its callbacks and binds services.commands now (app-mods). No adapter
  exists from `qa_app_sim_shared_physics` to the typed session mod engine
  (`include/qa/world/session/mod_engine.h`): connection step.
- compat/qc, compat/qvm, compat/q3 depth tests were not run under ASan or UBSan.
- JS semantics: wildcard rows in `tools/js_semantics_audit.tsv` admitted without a full
  read (QVM profile-offset rows, Q3 content range-guard rows) must be re-read. The
  detector cannot see integer overflow where TS uses doubles, integer `%`, `t*t*t*t`
  for `t ** 4`, or wrong hand-written ternaries: the full read covers those.
  `localeCompare` is exact for ASCII only.
- Evaluation order: 4,583 sites are unclassifiable today, mostly calls through host
  interfaces with no implementation in `src` yet. After the connection step, re-run
  `tools/eval_order_audit.py` and classify all of them; "unclassifiable" does not pass
  the gate once implementations exist. 171 rewrites inside conditions use the comma
  operator and read awkwardly; tidy them during the full read.
- Six content binaries failed in several agents' builds during the Q1, Q2 and Q3
  refactors: test_content_composition, test_q1_addon_monsters, test_q1_missionpacks,
  test_q1_rows_4, test_q1_unified, test_q3_base_ta. Assigned to the Q1 and Q3
  unification agents to settle against bun on their final trees.
- Simulation shells batch (2026-11-02, sim-monsters): the Q1 foundation save reader
  returns `qa_persist_q1_foundation_checkpoint`, a second C type for TS
  Q1FoundationCheckpoint with no conversion to the content type, so read monster-source,
  foreign-Q1 and threewave-grapple checkpoints cannot be restored into a game yet.
  `SelectedMonsterSource` (monster_runtime.h) embeds `qa_app_sim_selected_q2_monster_modules`
  from q2_monster_sources.h, still an opaque-pointer shell. Not yet assigned.
- Possible TS defect, reported to the user rather than changed: TS `clientSpawn`
  replaces `ammoTimes` with a fresh `GameClient`'s, which has no `ammoTimerStored`
  hook. C reproduces it faithfully.
- Bots/UI script readers: a reader the chat loader or item-config loader abandons
  (retired scope, failed PC_ExpectTokenType) keeps its C objects, because destroying it
  also frees its script allocations in bot memory, which TS leaves live. It needs a
  C-only teardown (about 4 KB per abandoned reader; test_library_chat and
  test_library_core). Unassigned.
- QuakeCSource (`simulation/quakec_source.c`): matches TS on e1m1 (byte-identical
  entity and globals memory after 100 frames). Left: the local-message text pool grows
  for the source's lifetime (C state borrows text); checkpoint encoding of declaration
  subtrees not compared byte for byte with a TS checkpoint; `.slice(0, 31)` name
  truncation counts bytes, not UTF-16 units. The legacy
  `qa_app_sim_prepare_quakec_source` (no mounts) exists only for runtime.c and
  application.c; delete it when they use `_prepare_quakec_source_mounted`.
- `simulation/runtime.c` leaks cvar snapshots: `c_qc_host_cvar_set` (the `set` result)
  and `c_qc_cvar_register` (the `find` read and the `register` result), 16 per concrete
  host in test_sim_concrete. With the runtime port.
- Network adapters: `slice(0, 15)`/`slice(0, 126)` count bytes, not UTF-16 units. Q2
  foundation `localSound` (rerelease lowammo) is ported (D4): weapons/player.c emits the sound
  event with `local` set, and the network routing reads it. network-q3:
  TS caches the `Q3ApplicationPackages.open` promise, so a failed open fails every later
  prepare; C retries the open. `qa_executable_recipe_free` frees the gameplay mod, grapple
  profile, native declaration and QVM layout records shallowly (their readers have no free
  functions).
- The network adapters and bot shells take access structs
  (`qa_app_sim_network_simulation`, `qa_app_sim_network_content`,
  `qa_app_sim_q2_guest_world`, `qa_app_sim_bot_simulation`, `qa_app_sim_bot_navigation`):
  the runtime port must fill them from the real SharedSimulation.
- `test_q3_game_logic` leaks about 21 MB (5,632 allocations) in Q3 base entity records
  (`src/content/q3/base/records.c` and its users). Unassigned.
- Under ASan, `test_q3_presentation_depth` and three Q3 bot tests failed in one run
  where their sources changed mid-build; confirm on the full-tree run at the gate.
- Top presentation batch (2026-11-02, top-pres): `character-turntable.ts` is ported and matches
  bun bit for bit (tests/app/test_top_turntable.c, QTS_TEST_CORPUS rows). The lower-layer
  blockers of `presentation*.ts`, `mod-presentation(s).ts` and `selected-q3-presentation.ts` are
  resolved (pres-contracts, tests/app/test_pres_contracts.c): SimulationPresentationEvent is one
  owned, copyable tagged union (simulation/types.h, `qa_app_sim_presentation_event_copy`/
  `_release`); the model renderer and light sampler take `qa_world_scene`/`qa_world_view_input`
  and a ModelRenderProvider over the real texture loader and shader registry
  (`qa_app_application_assets_render_provider`); SimulationPresentation's held weapon, model
  anchor and grapple cable are typed; `qa_mod_qvm_presentation_source.context` returns
  `qa_qvm_presentation_context` from both producers; client collision takes the shared clip
  models; the Q3 client assets host is built from ApplicationAssets; Q3SelectedSource and
  Q3SourcePresentationState are ported. Still open on the presentation path (void* or mirror
  where TS has a type; found by the pres-contracts sweep): component_scene.h (`state`, `bounds`,
  `game_state`, `player_state`, `visible_entities`, `select_visible`: Q3PlayerState, Bounds,
  SourceGameStateRecord, EntityStateFields[], Q3VisibleEntities); held_weapon.h `held_pass`
  `resource`/`model` (ResolvedResourceReference, DecodedModel); native_q2_client.h `payload`
  (SimulationPresentationEvent) and its host/world `entity`, `fog`, `fog_from_wire`, `empty_fog`,
  `player_state`, `entity_state`; network/remote.h `qa_app_q2_remote_event.effect`;
  remote_unified.h / unified_component_consumer.h `installed_mounts`/`borrow_mounts`
  (MountedContent); network/unified_types.h frame members (PresentationOwner, ModIdentity,
  NativeModCameraView, UnifiedComponentFrames, UnifiedPredictionProjection, SimulationOutput,
  SimulationPresentation[], Q3CharacterView[], WorldText[], PlayerView, PlayerUi); the unified
  network carries SimulationPresentationEvent as qa_save_value trees
  (simulation/network_unified.c, network/unified_frame_values.c, unified_event_codec.c) and
  PlayerUi.nativeInventory as a save tree, where TS has the typed event and
  NativeInventoryReadout; q3_client.h `movement_timing` and `receive_events`/`receive`
  (`const void *events`: SimulationPresentationEvent[]); q3_client/cinematics.h
  `image_allocate` (RendererImage); remote_q1.h/remote_qw.h/remote_q3.h `character_views`/`presentations`
  (`const void *`, always empty; remote_seat_source.c casts them); selected_q3_presentation.h
  presentation/event arguments; world/session/mod_client_presentation.h `configstrings`
  (ReadonlyMap<number,string>); world/session/mod_presentations.h typedefs
  `qa_session_qvm_entity_state_fields`, `qa_session_source_game_state_record` and mods.h
  `qa_session_qvm_artifact`; compat/qvm/mod_provider.h `scene_state`/`scene_baseline`
  (QvmModScenePublication); content/q3/presentation/ref_entity.h `value` (SceneShader |
  SceneSkin); simulation/types.h `qa_app_sim_player_ui.native_inventory`.
  compat/q2/rerelease/world_text.h keeps q2WorldText's arguments
  (`qa_compat_q2_world_text_input`) where TS emits WorldTextInput; the SharedSimulation world-text
  sink applies `qa_q2_world_text` to them (as native_mod_host.c does). compat/qc: TS ModMessages drops
  "monster-killed" when `publishesKills` is false; the C provider has no publishesKills.
  test_top_effects and test_top_assets now run their retail cases by default (content-life,
  2026-11-03: resource references are owned values, see contracts/content.h). Under
  LeakSanitizer they leak the loaded world's textures and material registrations (about 62 MB
  in test_top_effects, 15 MB in test_top_assets: render/scene/textures.c load_uncached/rgba_wrap,
  material_registrations.c new_registration, world.c lightmaps). Unassigned.
- void* sweep, not on a presentation path (2026-11-02, pres-contracts): struct members and
  callback results typed `void *`/`const void *` where TS has a concrete type, to type during
  the full read. app/bootstrap: application_tools.h `bind_buffer`; capture.h `commands`;
  client_bootstrap.h `attach`, `window`; content.h `application_world.value`;
  demo_recording_commands.h `attach`, `reconnect_recording`; network/q1_types.h (`info`,
  `baselines`, `signon`, frame `messages`/`reliable`/`datagram`/`entities`, host
  `supports_source_wire`/`spawn`/`input`, `transport`); network/q3_client_content.h
  `load_content`; network/q3_types.h (`player`, `entities`, `administration`, `product`, `pure`,
  `open_download`, `rate`, `supports_source_wire`, `game_state`, `input`);
  network/qw_server_types.h (`entities`, `messages`, `reliable`, `administration`,
  `authentication`, `masters`, `supports_source_wire`, `client_info`, `signon`,
  `prepare_download`, `baselines`, `transport`); network/remote*.h `content`/`load_content`,
  remote_unified.h `map_entities_provider`; network/remote_world.h `create_scene_queries`;
  network/types.h (`seed`, `wire`, `poll`, q2 game state `data`/`config_strings`/`baselines`,
  `event`, `downloads`, `message_options`, `administration`, `discovery`, `masters`,
  `supports_source_wire`, `mvd_capture`, `frame`, `input`, client `downloads`/
  `message_options`/`command`, `transport`, `remote`); network/unified_content.h
  `load_content`; options.h (`q3_map_launch`, `team_arena_skirmish`, `server_profile`,
  `weapon_behavior_command`, `parse_weapon_behavior_tool`); original_save.h `data`;
  simulation: foreign_q1_world.h `host`, native_q1_pusher.h
  options, native_q2_travel.h `selected_arsenal`, network.h / network_q3.h / network_qw.h
  `administration`/`masters`, network_q2_guest.h `entity_states`, q2_monster_sources.h (all
  module and factory members), q2_native_world.h `primary_profile`/`source`/`builtin_primary`,
  q3/guest_artifact.h (`artifact`, `items`, `primary` and the prepare-host results),
  q3/guest_bots.h `selected`, q3/guest_runtime.h `checkpoint`, q3/types.h (`services`,
  `mover_actors`, `actors`, `bodies`, `callbacks`, `combat`, `inventory`, `scene`,
  `original_pickups`, `death_animations`, `weapon_behavior`), quakec_source.h
  `prepare_resources`, qvm_mod.h `mounts`, types.h (NativeQ2GuestOptions `capabilities`,
  `prepared`, `localize`, `clipboard`, `semantic_bindings`; SimulationTravel `lmctf`,
  `rerelease`, player `hand_grenades`, `selected_arsenal`, `carry`; SimulationOptions
  `prepared_qvm_grapple`, `prepared_mods`, `mod_commands`, `mod_files`, `enabled_mods`,
  `mod_travel`, `prepare_rerelease_navigation`, `weapon_behavior_real_time`,
  `weapon_behavior_clock`, `weapon_behaviors`, `prepared_quakec`, `native_q2_travel`,
  `q3_guest`, `world`, `mounts`, `q3_session`, `server_profile`, `source_registry`,
  `source_archive`, `monster_navigation`); startup.h `captures`; startup_audio.h
  `engine_selected_output`; startup_selection.h `payload` (x2), `binding_capabilities`;
  video_restart.h `client_command_registration`; weapon_behavior_selection.h `payload`,
  `mounts`, `for_content`; weapon_behavior_tool.h `open_product`. world/session: mods.h
  `clients_visibility`, `clients_at`, `scene`, `physics` (typed views in mod_engine.h),
  `native`. compat: q2/classic/layout.h `cpu_state`, `capture_processor`;
  q2/compatibility.h `classic`, `rerelease`; q2/native_input.h `host`;
  q2/native_mod_items.h `items`, `entry`; q2/native_pickups.h `frames`; q2/native_primary.h
  providers and `world`; q2/rerelease/navigation.h `runtime`; qvm/memory_writes.h `watches`;
  private containers typed `void *` in qvm/mod_actors.h, mod_clients.h, mod_input.h,
  mod_items.h, mod_pickups.h, mod_player_events.h, mod_presentation.h, mod_protection.h,
  mod_provider.h (`frames`, `pickup_scopes`, `weapon_inputs`), mod_weapon_stage.h,
  registry.h `slots`, symbols.h `records`. render: scene/models/renderer.h `materials`,
  particles/q3_system.h `particles`/`animations` (private containers). C-only storage,
  closure contexts and TS identity tokens are not listed.
- Top input (2026-11-02, top-input): `input.ts` is ported (input.c, input_commands.c,
  input_platform.c), and `input-devices.ts` / `controller-settings.ts` were re-ported over
  the real MIDI, joystick, router and config owners (their host-shaped C had no callers).
  For the connection step: application.h's function `qa_app_application_input()` collides
  with the `qa_app_application_input` typedef once application.c includes input.h (rename
  the function); `qa_app_client_bootstrap` (client_bootstrap.h) holds only the
  ClientBootstrap members input.ts reads; seats and consoles published into a prepared
  startup or client hold their ApplicationInput until `qa_app_input_release_holder(owner)`,
  which the application must call before closing that startup or client.
  `sharedSettings()` returns the current shared registry, where TS returns a view that
  follows a later `publishSharedCvars` (the C ui/settings take a concrete registry).
- `test_q3_presentation_depth` leaks about 408 MB in presentation audio and media
  registration. Unassigned.
- SharedSimulation B1 remainder (`bindNativeInput` and `captureNativeQ2Travel` are done, runtime-rest):
  `capture/restoreSelectedMonsters` and
  `capture/restoreForeignQ1World` are blocked on converting
  `qa_persist_q1_foundation_checkpoint` (save reader) to `qa_q1_foundation_checkpoint`
  (content). TS moved tag, LMCTF and DeathBall into `foreign-match-rules.ts` during the
  port: re-check at the drift gate. TS drift without C members: `cloakAlpha` in the
  monster target observation, `foreignItems` in Q2 item hooks. Next free slot.
- Q3 sources (2026-11-02, q3-source): `qa_app_q3_source_runtime_new(options, host, mode)` builds the content/q3
  subsystems from the Q3SourceHost (typed now) and runs on them; `qa_app_shared_sim_create_q3_source` and
  `qa_app_shared_sim_create_q3_qvm_source` are ported; Q3QvmServerGame owns its QvmGame, files, parser and syscall
  host; PreparedQ3Game is typed. Construction over retail q3dm1/mpteam1 (TS source and QVM guest) matches the TS
  dumps line for line (tests/app/test_q3_source.c, oracles in tests/app/oracles/q3_*). Still open: the Q3 native
  checkpoint parts locations/spawns/match/remaps/settings/memory/bridge/missiles/team/arenas/personalPortal/
  serverCommands have struct captures but no checkpoint-tree writer, so `captureNativeState` writes null for them
  (graph, registeredItems, server, report and publishedEvents are written); restore reads every part. The guest
  test runs with bot_enable 0 (no bot library attached), so "32 bots parsed, 35 arenas parsed" are not produced.
- Q1 checkpoint drift: TS Q1 content checkpoints are version 6 (monster animation
  moved, `legacyAnimation`); the C content and reader are version 5, so Q1 checkpoints
  cannot match TS until the Q1 content port catches up. Unassigned.
- `runtime.c` sets save schema_version 3; it is deleted at connection, but anything
  still using it writes old images until then.
- Seat presentation (2026-11-03, app-seat-pres): held-weapon.ts, native-held-weapon.ts and
  q3-selected-weapon.ts run on real ApplicationAssets (HeldAssets/NativeHeldAssets are the TS structural
  interfaces, with ApplicationAssets adapters); presentation-scene.ts (ApplicationWorldScene) and
  presentation.ts (WorldSeatPresentation, plus seat-factory's createSeatPresentation as
  `qa_app_world_seat_presentation_create`) are ported. Scene operations and held passes match bun line for
  line on retail e1m1/q3dm1 (tests/app/test_world_seat_presentation.c, oracle
  tests/app/oracles/world_scene_oracle.ts). mod_client_presentation.h carries `qa_native_q2_hud_frame`;
  the Q3 scene presentation has `server_time`. Still open: ApplicationQ3Client (q3-client.ts) is unported,
  so WorldSeatPresentation reads it through `qa_app_seat_q3_client`, which the application must fill;
  application_step.c (movie branch) must call the new prepare(snapshot, presentations, characters) and
  application_close.c must call `qa_app_world_seat_presentation_destroy` after close (close no longer
  frees); finale.c, q1_fog.h,
  q1_localization.h and the console draw target stay host/mirror-shaped (adapters in presentation.c);
  rendering a seat frame on the CPU backend fails "CPU image dimensions disagree" for the console font
  ("conchars") image registered by `qa_app_application_assets_load_console_font` (its content is gone by
  upload); mod-presentation.test.ts still needs an ApplicationAudio test seam.
- runtime-rest follow-ups (2026-11-03): `qa_app_shared_sim_open_mods` (load's `SessionMods.open`) is still not
  ported: it needs adapters from the PresentationState owner binding (`qa_app_sim_presentation_event` vs
  `qa_app_sim_source_presentation_event`) and typed views for the mod engine services (scene, physics, Q1 client
  visibility, mod operations of the authority, callback table and inventory). `prepareApplicationWeaponBehavior`
  (weapon-behavior-selection.ts) is still a host seam (`qa_app_weapon_behavior_host.prepare`) that nothing implements,
  so the `qa_app_prepared_qvm_weapon_behavior` / `qa_app_prepared_native_weapon_behavior` payloads are never filled.
  TS missionpacks/items.ts `registerQ2MissionPackItems` `rereleaseCapacity` and rerelease quantities are in the C
  registration and the bandolier/pack ceilings (`qa_q2mp_mission_ammo_pack_capacity`) are ported (D4). players.c
  moveQ3Client passes a selected `qa_app_shared_sim_arsenal` read to weaponStep without freeing it.
  Not yet assigned.
- From the runtime-rest porter: `prepareApplicationWeaponBehavior` (application layer)
  is unported, so the qvm and native weapon-behavior payload records are never filled;
  Q2 mission ammo item registration has the TS rerelease capacities (D4);
  `shared_simulation_players.c` moveQ3Client leaks its selected input read.
- Q3 native checkpoint (`captureNativeState`) writes null for locations, spawns, match,
  remaps, settings, memory, bridge, missiles, team, arenas, personalPortal,
  serverCommands and level: those modules have struct captures but no writer to the
  checkpoint value format. Unassigned.
- Q3 client (2026-11-03, app-q3client): `q3-client.ts` (ApplicationQ3Client, q3_client.c) and
  `q3-client/qvm.ts` (ApplicationQvmClient, q3_client/qvm.c) are ported on real types: the QVM
  host runs the TS syscall chain (status, common, files, scripts, render, audio, client state,
  collision, marks, cinematics, browser, UI keys, scalar) over the real services, equipment and
  held-weapon observers bind on the cgame module, retained artifacts are refcounted for video
  reopen. QvmCvarServices is now an interface in compat (`qa_qvm_cvar_table`,
  `qa_qvm_cvar_table_syscall`; `qa_qvm_common_services.cvar_table`), so Q3ClientCvars and the
  CgameStatusView mask run through the compat cvar syscall; `legacyClientCommand` returns null
  as TS does; SharedCvarMirror has a CvarRegistry view (`qa_cvar_registry_mirror_view`);
  `applyServerSystemInfo` is ported (userinfo.c); StartupServerBrowser has a Q3BrowserHost
  adapter (`qa_app_startup_server_browser_q3_host_create`). Follow-up (app-q3c2, 2026-11-03): q3_client/scene.c
  (SceneModelRenderer per provider, world/material batches) and cinematics.c (CinematicImage in the ApplicationAssets
  registry, implicit picture material with a video binding) run on the ApplicationAssets behind ApplicationQ3Assets
  (`qa_app_application_q3_assets_application`); the `scene_renderer`/`cinematics_host` options and the presentation
  hosts' `cinematics`/`scene_renderer` are gone. The services, the scene recorder (`viewport_ref`), the text sink
  (`qa_text_command_sink_share_target`, `qa_text_draw_sink.target_ref`) and the presentation (`common.target`) share the
  client's viewport. WorldViewInput has `model_shadows` (world.c clears stencil and appends shadow-finish as TS does) and
  Q3SceneRenderOptions has `rail`. remote_q3.c fills the whole cgame source. ApplicationVideoRestart and
  prepareVideoGuests run over ApplicationQ3Client with raised errors. tests/app/test_top_q3_client.c runs
  ApplicationQ3Client kind "qvm" over retail baseq3 ui.qvm/cgame.qvm on q3dm1 with a fixed game state and snapshot
  session and matches tests/app/oracles/q3_client_oracle.ts line for line (ui/cgame console output, 21 models and 3
  skins, 4 frames of draw commands: 1,295 lines). Still open: SceneModelRenderer has no modelShadowGroups (the TS
  stencil-volume and projection-shadow groups of cg_shadows 2/3: stencil-shadow.ts, shadowVolumeGroup), so
  `model_shadows` reaches the world but no model emits shadow volumes; `prepareVideoRestart` (application.ts, P2) needs
  an ApplicationQ3Client adapter to `qa_app_seat_q3_client` for replaceQ3Client. Fixed on the way (lower layers, found by
  the retail run): qvm_display/qvm_scalars/status wrote guest records through `qa_qvm_writable_view.bytes` (the
  allocation base, not the view start); render/scene/view.c projected with unstaged dots where TS uses `dot3`;
  world.c registered *fog/*dlight before the surfaces, kept one static clear/clip plane for every prepared view and
  leaked merged sky geometry; material2d.c cast a MaterialHandle picture to a CompiledMaterial (pictures now carry
  `compiled_is_handle`); `qa_content_mounted_list_files` returned NULL for an empty listing. test_top_q3_client under
  LeakSanitizer still reports the known texture/material-registration leaks (about 28 MB), none from the client.
- SharedSimulation link (2026-11-03, sim-link): every shared_simulation_*.o links with no unresolved symbol and the
  tests/app/test_shared_simulation_* binaries link through the normal rule. Post-restore key orders are tracked for
  source movement, grapple controls and weapon state, the Q3 selected arsenal and Q2 sourceModels. Still open: the
  player checkpoint's `readArsenal` builds { provider, activeWeapon, state, ammo } for every family and a restored Q3
  selected arsenal's read() is { provider, activeWeapon, ammo, state }; both reach players[].arsenal, whose writer
  (`qa_app_sim_arsenal_state_value`) picks the order by weapon kind. Carrying that needs an order bit on
  `qa_arsenal_state` (contracts/movement.h, about 1,200 dependent objects). Not yet assigned.
- Application core (2026-11-03, app-core): the real `Application` is `qa_application` (application_class.h,
  src/app/bootstrap/application_*.c; layout and parallel assignments P1-P7 in docs/application_layout.md), built beside
  the concrete-host `qa_app_application`. Struct, constructor, step, run, recoverDedicatedFrame, close and closeOwned
  are ported; openSource is blocked on lower layers with no concrete C form (content.c prepared artifacts,
  openInitialConfigurationContent, resolveApplicationMovement, authoredCampaignStart, sourceConsoleScripts,
  prepareConfiguredApplicationRecipe), openBorrowed on the ClientBootstrap members, and step/close read
  WorldSeatPresentation members presentation.c does not expose. Listed in the layout's "Dependency gaps".
- `qa_q3_text_parse_error` aborts on a malformed Q3 entity lump where TS throws a
  catchable error (startup selection marks the map unavailable): convert to `qa_raise`.
  Nine eval-order sites in `startup_selection.c` are judged allocation-order-only but are
  not ledgered in `tools/eval_order_ledger.tsv`. Unassigned, small.
- `src/content/catalog/launch.c` `launch_service_qvm_compat` reads QVM compatibility
  through the whole mount set, where TS uses the scoped reader. Unassigned, small.
- RemoteApplication (`docs/remote_application_layout.md`): the q1, qw, q2, q3 and unified clients connect over loopback
  UDP to the real C Application server and match the TS RemoteApplication against the same server line for line
  (datagrams through signon, first snapshots, console; tests/app/test_remote_loopback.c), including the unified server
  over the mixed `--preset q2-q1-q3` recipe (unified-network packet, 2026-11-03: typed presentation events <->
  event trees in network/unified_event_values.c, the replica's PresentationState, RemoteComponentView, the concrete
  UnifiedContentHost, the server's typed publish and its prediction-projection ownership fix). Clients whose profile
  differs from the served recipe adopt it as TS does (unified2, 2026-11-03): `qa_app_client_socks_settings_route`
  kept one `base` on the shared ClientSocksSettings, so the candidate's staged routing rewrote what the published
  configuration's routing reported and the prepared startup counted the candidate's fresh source registry as live
  ("Candidate client commands require isolated cvar owners"); TS `route(base)` returns a new closure per call and the C
  route is now its own object. unified-cross (Q2 client, Q1 server), unified-cross-q1 (Q1 client, Q2 base1 coop
  server) and unified-cross-q3 (Q3 client, Q1 server) run by default, 0 differing. App-layer leaks (unified2): every
  default family under ASan+UBSan (build/agents/unified-asan, with the TS runs) reports no ASan or UBSan error and
  68.2 MB leaked with no allocation site in src/app (LeakSanitizer, normal -O2 build, C runs only: 128.7 MB before the
  fixes, 66.6 MB after). Fixed: cleared presentation and bound seat UI (seat HUD, event rings, scene) freed at the
  RemoteApplication's destroy; RemoteSeatView frees its seat UI; per-step unhandled effects; profile default bindings and
  configurationScriptReader ctx (configuration.c); prepared client commands' print state; socks register snapshots;
  demo command handler ctxs, the startup ClientDemoCommands and the published-source demo binding (TS releaseDemos);
  server definitions array. Remaining, owned elsewhere: render (gate-render, ~68.06 MB: world/model/shader textures
  through render/scene/textures.c load_uncached, world lightmaps from world.c qa_world_scene_load, world.c q2 sky
  operations, resources.c image registry updates, material registrations/shader names, md3 tags via
  render/scene/models/transform.c); simulation (D1c, ~106 KB: prediction runtime.c's per-command movement
  effects/contacts, Q2 carry set and Q3 spawn loadout at admission, ss_cvar_set/ss_cvar_register snapshots in
  shared_simulation_source.c). [D1c: fixed the prediction replay (runtime.c, sequence.c, presentation.c free each step's
  movement effects/contacts; step.c frees the movement array it replaces), the Q3 spawn loadout (players.c: the player owns it
  as owned_arsenal_ammo), the cvar snapshots, and qa_capture_q2_product's schema strings (checkpoint.c). The Q2 admission
  carry leak did not reproduce under LSan in sim_verify (q2c). Still leaking under LSan in sim_verify, outside D1c's files or
  not traced: qa_q2_cvar_binding_pull's snapshot (settings/server/q2_owner.c:446), qa_q1_character_actor_capture's replaced
  null value (content/q1/base/player.c:198), and the last QC pusher candidate buffer (physics.c q1_services_candidates). The
  native_mod.c / qvm_mod.c double destroy is fixed: the mods no longer destroy the command port their scope owns; the
  port's memory is not freed by qa_mod_commands_destroy (world/session/mod_commands.c, gate reader's area).] Open: (2) not run against TS output: the unified RemoteSeatView
  (local remote seats), component sources, a Q3 character over the replica; (3) C payload domains narrower than the
  TS codec: Q1ClientSnapshot slot/shirt/pants and CtfStatus flags/runeItems are int64_t, Q2 model attachedModels is a
  fixed [3], SimulationPresentationEvent.sourceEntity has no null/undefined distinction (named in
  tests/network/test_unified_event_values.c); (4) a decoded damage or transition simulation event raises in the
  client view (unified frames never carry them: permitted() drops both).
- SharedSimulation side by side (2026-11-03, sim-verify): tests/app/test_sim_verify.c and its oracle
  tests/app/oracles/sim_verify.ts construct the real SharedSimulation through loadSimulation on retail data
  (parseApplicationCommand -> loadApplicationContent -> SimulationOptions, guest options on a deterministic
  clock) for TS Q1, TS Q2 classic/rerelease, TS Q3, QuakeC, native Q2 classic/rerelease, the Q3 QVM guest and
  the mixed `--game q2-classic-baseq2 --map base1` / `--preset q2-q1-q3` recipe, and compare a state dump at
  frames 0/1/10/100 (expected dumps tests/app/oracles/sim_verify_<case>.txt, written from the TS tree of
  2026-11-03 01:12). Through frame 100 the state lines match for TS Q3, QuakeC, native Q2 classic and rerelease
  and the Q3 QVM guest. Still differing, not yet assigned: TS Q1 (TS moved monster_army/monster_dog from
  foundation/monsters to the base species registry with a different start schedule; C still spawns them through
  foundation/monsters.c, and base/species.c has no army/dog rows or frame tables); Q2 rerelease monster AI drift
  in ai.ts/perception.ts/index.ts (thirdEye, reachedHoldCombat, noStep, badMoveTime, bumpTime, slideAlongObstacle,
  refusesWater, alternateFly, nearestNoise, faceWall, M_FlyCheck, power-armor effects) and the
  MonsterState fields behind them; the mixed recipe's Q1-movement player wedged at frame 97: TS NetQuake/QuakeWorld
  sweepBody `samePlane` is JS object identity of the trace's source-plane normal (shared geometry plane objects;
  a Q1 hull back-side hit makes a fresh object), while C `sweep_same_plane` compares plane slots only, so a plane
  hit twice is a crease in C (velocity zeroed) and the same plane in TS; carrying a plane identity on
  qa_trace_result through the q1/q2/q3/body producers and the swept services is the fix; checkpoint key orders of launch-built records (resource
  references, mounts, provenance, timing, execution, Q3 movement state, players) and checkpoint schema drift
  (Q2 weapons formatVersion 3 noises, monster state fields, g_select_empty, bodyque spawn values,
  q3:native null parts). Movement results (Q1 contexts, Q3 contacts) and Q3 character animation steps leak
  their arrays: nothing owns a qa_movement_result after it is consumed.
- Application P3 leftovers:
  Not reached: prepareLocalGuestSeat, createClientCvars, uiSourceOptions,
  prepareRestoredGuestSeat, restoreSourceInputAngles, publishLocalGuestSnapshots,
  prepareSavedClients. `q3_rankings.c` keeps account-action contexts in a static
  64-entry table shared by every owner. The C bot asset loader lacks TS
  `legacyProvenance` and loads navigation eagerly, so restored bot provenance can differ.
- WorldSeatPresentation is connected in application_step.c (movie console branch) and
  destroyed after close in application_close.c; the seat-replace close branch goes
  through the seat resource scope: confirm it frees the presentation (leak run).
  Executing a seat frame on the CPU backend aborts with "CPU image dimensions
  disagree" on the "conchars" console font from
  `qa_app_application_assets_load_console_font`. Leaks: `md5.c` pose sampling via
  `qa_model_grip_transform`. Unassigned.
- From app-deps: the rerelease presentation's `prepareImageRefresh` loads skies through
  its host instead of the prepared images; `ApplicationVideoRestart` (`video_restart.c`)
  still reports errors as strings and nothing creates one; leaks in
  `remote_application_config.c:547` (cvar register snapshot), `qa_default_bindings`, the
  startup script reader context (no destroy), and prepared_startup client commands.
- From app-p6 (2026-11-03): under LeakSanitizer, a Q1 dm1 SharedSimulation load + 12 frames (test_match_results) leaks
  Q1 movement contact/effect arrays (movement/q1/common.c push_contact, qa_q1_context_effect), cvar snapshots in
  shared_simulation_source.c `ss_cvar_set` and `qa_cvar_registry_register` callers, `qa_session_mods_open` (mods.c:477)
  and the Q1 combat policy (policies.c qa_create_q1_combat_policy, qa_native_victim_armor). Unassigned.
- Gameplay mods (2026-11-03, app-mods): the declaration writer (content/mods/declaration_writer.c) matches bun
  JSON.stringify for 202 declarations (test_mod_declarations); preparedMods, mod-selection.ts, save_image.c
  `gameplay_mod_value`, native-q2-client.ts and presentOutput are ported (docs/application_layout.md, "app-mods").
  Open: C strings end at NUL, so a declaration string holding "\u0000" (TS fixtures that test its rejection) reads as the
  text before it, and the C reader/validator sees a different string than TS (a C-wide string-model limit, not the
  writer's). The retail corpus has no gameplay-mods.json. presentOutput's graphical branches are not checked at unit level.
- Guest bots (2026-11-03, app-botsnet): done. compat/qvm bot-library, legacy-bot and bot-navigation syscalls take
  the real BotLibrary, SourceBotNavigation and AasBspEntities (the `qa_qvmx_bot_library` / `qa_qvmx_bot_navigation`
  mirror tables are gone); guest_bots.c is a port of guest-bots.ts; `qa_application_prepare_guest_bots` is ported;
  saved bot connections carry the client generation and openSource decodes the saved bots checkpoint. Retail check:
  tests/app/test_guest_bots_verify.c matches tests/app/oracles/guest_bots_verify.ts (bun) line for line on baseq3
  qagame.qvm q3dm1 through 300 frames ("32 bots parsed", addbot, bot command times, bot and player positions).
  Found on the way: the Q3 guest ran BOTAI_START_FRAME synchronously (TS botFrameAsync) and aborted on the first bot
  user command; guest_spatial.c trace/pointContents aborted on pass entity -1 (TS records.reference); guest_spatial.c
  pointContents iterates every entity with owner checks where TS queries the actors at the point and skips only the
  pass slot (not re-ported here). The navigation syscalls stage goal, move-result and target records (the C
  SourceBotNavigation takes values where TS takes live guest references). test_qvm_a's NaN max-frames case was removed:
  the C query fields are int32 words, so the case cannot occur.
- Server browser (2026-11-03, app-browser): done. server-browser.ts, server-master-list.ts, server-browser-addresses.ts
  and server-browser-cache.ts are re-ported over src/network: `qa_app_startup_server_browser_open(config)` binds a
  `qa_udp_transport` (0.0.0.0:0, broadcast) and builds the four ServerBrowser cores (`qa_server_browser_create`) with the
  Q1/QW/Q2/Q3 wires; resolveAddress is `qa_net_resolve_address`; the master list is a libcurl GET (redirects followed,
  1 MiB and 4096-server bounds, fatal UTF-8, invalid lines skipped and counted, the 4 s AbortSignal as a deadline); KEX
  mDNS is `qa_kex_mdns_*`, pumped by poll(). network/q3/discovery uses the shared types (`qa_q3_discovery_wire_init` fills a
  `qa_discovery_wire`; statuses are `qa_server_status`; master records `qa_net_address`); browser_view.h's host returns
  `qa_server_status`. User-input errors raise catchably (TS throws); poll() catches every remote packet failure as TS
  does (no remote datagram can abort: tests/app/test_server_browser.c mutation pass); open() skips and reports invalid
  saved lists and caches; loadQ3Cache reads the live statuses before clearing the lists (the old C read them after);
  `q3List` returns the browser's snapshot (same pointer while the generation holds). openGuestBrowser is
  `qa_application_open_guest_browser` (application_guest_browser.c). Tests: tests/app/test_server_browser.c ports the
  three TS server-browser test files (menu clicks replaced by the browser calls they make), the startup-selection
  browser case against the C Q1/Q2/Q3 hosts on loopback, the Q3BrowserView over the adapter, and a mutation pass; clean
  under ASan/UBSan with leak detection. Fixed on the way: `qa_kex_read_discovery` leaked its partial status when the
  reader raised on a hostile lobby packet. Open: startup_menu.c calls query/favorite/
  connection/discover/scan without qa_try, where the TS StartupMenu `run()` catches and puts the message in
  `browser.status` (`qa_app_startup_server_browser_set_status` exists for it); the Q3BrowserView takes int64
  maxPing/statusResendTime, so a fractional cl_maxPing truncates; a failed settings write reports
  "<strerror>: could not write <name>" where Node reports its fs error text, and a failed mDNS bind "bind <strerror>
  0.0.0.0:5353" where Node prints the errno code; a TS close() during a pending DNS lookup or settings write
  cannot interleave with the synchronous C calls. Next free slot for the menu catch.
- The presentation access view leaves `drain_presentation_events` NULL. On openSource failure the open record and
  closure state are not freed. Next free slots.
- Application concrete collaborators (2026-11-03, app-concrete): capture.ts, application-tools.ts, precache.ts and
  campaign-cinematic.ts are ported to their TS shape over ApplicationInput, the console commands, FrameCapture, the
  NativeRenderer, the spline camera, the diagnostic registrations, MountedContent, the SharedSimulation sources,
  ApplicationAudio/Effects/Assets and the media layer (docs/application_layout.md, "Found by app-concrete"); tests
  tests/app/test_app_concrete_*.c against bun oracles. Follow-up (app-concrete2, 2026-11-03): original-save.ts, the
  rerelease presentation and the Q3 presentation hosts over ApplicationAssets, the ApplicationQ3Client seat view,
  createWorldSeatPresentation and prepareVideoRestart are done (docs/application_layout.md, "Found by app-concrete2";
  restoreOriginalSave existed but was undeclared). Follow-up (app-concrete3): the seat UI asset owners (weapon HUD, Q1 wheel, sound captions, native Q2 HUD,
  game prompt) run on ApplicationAssets, StartupSaves on the catalog; createSeatUi, saveMenu, uiSourceOptions, windowMutator
  and prepareNativeQ2Seat are done (docs/application_layout.md, "Found by app-concrete3"). Follow-up (app-final):
  createQ3SeatClient, openGraphical and publishBorrowed are done; every application_*.o links with no undefined symbol;
  the real Application opened dedicated on retail Q1 e1m1, Q2 base1 and Q3 q3dm1 matches TS line for line and a graphical
  Q1 application runs on the offscreen GL driver (tests/app/test_application_open_real.c; "Found by app-final"). Still
  open: publishBorrowed and prepareVideoRestart have no application-level test (no borrowed ClientBootstrap fixture).
  Found while porting: console/commands.c frees a queued operation's context only when the operation runs, so an
  operation refused by a closed capture owner or a retired script writer keeps it (TS GC); a failed capture reaches
  ApplicationCapture as "Screenshot capture failed" (FrameCapture I/O errors carry no message), while a rejected frame
  read prints its own message as TS; the `path` diagnostic raises "Missing active mount" before printing any order,
  where TS prints the default order first when only a prefix order is broken; the camera `read` keeps mounted bytes
  as-is where TS TextDecoder replaces invalid UTF-8; `qa_frame_capture_*` abort on "No free screenshot filename" where
  TS rejects. The camera module still aborts where the TS queue prints "Camera: <message>". Under LeakSanitizer a CPU
  NativeRenderer window leaks 128 bytes inside the SDL/GL driver (pthread_once in qa_sdl_window_open; suppressed in
  build/agents/app-concrete-asan/lsan.supp). UBSan found `src/audio/streams.c` shifting a negative u8 PCM sample
  (`(u8 - 128) << 8`); it now multiplies by 256 (same value as the JS int32 shift).
- Render: the C SceneModelRenderer has no `modelShadowGroups` (TS stencil-volume and
  projection-shadow groups), so `cg_shadows` 2/3 emits no shadow volumes. `test_qvm_b`
  aborted with "Unknown Q3 native save part graph" (q3/runtime.c) during the Q3 native
  checkpoint writer work; recheck when the verifier finishes. Unassigned.
- SharedSimulation side by side: all 10 cases (q1, q2c, q2r, q3, qc, q2nc, q2nr, q3qvm, mixed,
  preset) match live TS with 0 diff lines, including checkpoint image and provider hashes
  (tests/app/test_sim_verify.c, oracle tests/app/oracles/sim_verify.ts). Open: the oracle's
  Q3 command literal was reordered to the C canonical key order; TS `input.ts` builds
  commands as {kind, serverTimeMilliseconds, weapon, sensitivity, ...}, so check at the
  connection step that a checkpoint taken after real input matches TS key order. qc under
  ASan (-O1) differs by one float32 ulp in a player origin from frame 41-50; -O2 matches.
  q2nr checkpoints stop on "Guest call stopped: budget" identically in TS and C. Leaks
  remaining: q2nr about 227 KB (text_add buffer, native server capture, finish_save,
  rerelease cvar registry, source_client_inventory), q3qvm about 4.6 KB, qc
  `q1_services_candidates`, cvar snapshot strings, Q2 save copy_text, `qa_q3_spawn_loadout`.
- TS drift D3, content/q1 (2026-11-03, d3-q1): every code/types-only/added/removed file under
  src/content/q1 is recorded `drift-done` in docs/port_review.tsv. Open:
  (1) composition/q1/runtime.ts gates the MG developer impulses behind `sv_gate_dev_impulses`
  with `isQ1AddonDeveloperImpulse`; the predicate is ported (`qa_q1ad_is_developer_impulse`,
  addons/commands.c) but the C composition impulse path does not call it yet (composition
  drift, no packet owns content/composition). (2) TS moved foreignQ3Pickup to
  composition/q1/q3-pickups.ts behind `game.foreignPickup`; C installs the hook from
  composition/q1/q3_pickups.c, but the Q3 definitions and their take kinds stay in
  foundation/pickups.c because the pickup definition is private there; moving them needs a
  public pickup type. (3) Q1EntityServices.find's name index (entityEpoch/targetnameEpoch)
  is a cache only; C keeps the linear scan with the same order. (4) TS tests of the packet
  not yet reproduced as C checks: tests/gameplay/q1-addons/monsters/ordinary/nightmare.test.ts,
  ordinary.test.ts (Shub zombie grenade, monster_slime, MG3 shared base), start-flags.test.ts
  (MG3 base pickups, MG3 cells/coop respawn), and the changed hunks of foundation/q1/
  foundation.test.ts, q1-addons/{common,ctf/ctf,items/items,monsters/heavy/source}.test.ts,
  q1-base/base.test.ts, q1-equipment/threewave-grapple.test.ts and q1-missionpacks/{arsenal,
  monsters/bosses/source,monsters/source,world/world}.test.ts (the 2026-11-02 sync tests
  test_q1_sync*.c cover part of them; not audited hunk by hunk).
- TS drift D5, content/q3 and bots (2026-11-03, d5-q3bots): every code/types-only/added/removed
  file under src/content/q3 and src/bots is recorded `drift-done` in docs/port_review.tsv. Open:
  (1) estimate-store.ts: the cross-runtime sharing of estimate tails and AAS caches (one store per
  edge list, keyed by the runtime's eligibility scope string) is not ported; C keeps one private
  LRU per runtime (estimates.c, estimate_aas.c). Estimates are a pure function of eligibility,
  so results match; only eligibility-callback counts and memory differ, which the first test of
  tests/bots/navigation/estimate-sharing.test.ts observes. Porting it needs refcounted (or
  deferred-free) cache values, because estimate_aas.c holds cache pointers across later
  remembers that may evict. manifest row status `partial`. DONE (gate-bots3, 2026-11-11): ported as
  estimate_store.c with reference-counted values; the first estimate-sharing test is reproduced in
  tests/bots/test_bots_gate_review_nav.c.
  (2) app/bootstrap (D1): prediction/presentation.c must pass
  `ladders: movement.ladders !== false` (qa_q3_pres_movement_options.ladders_disabled) into
  the prediction step options, which have no ladders member yet; simulation/q3/guest_bots.c
  can use the new exported `qa_aas_entity_info_empty_bots_a`; simulation/bot-assets.ts calls
  prepareLegacyProvenance (now `qa_bot_asset_files_prepare_legacy_provenance`) and adds
  "arsenal/provenance" with legacy bytes (`qa_bot_asset_files_add_with_legacy`); the
  version 1 bot-save restore path needs those calls. `qa_app_sim_bot_asset_provenance`
  duplicates `qa_bot_asset_files_provenance` and does not skip a path that is also recorded
  unread (TS `unread.has`).
  (3) D7: movement/q3/grapple.ts now owns Q3_GRAPPLE_SPEED and q3GrappleVelocity; C keeps
  `qa_q3_grapple_velocity` in content/q3/base/game/grapple.c (same behaviour). Map the new
  manifest row there or move it, but do not define the symbol twice.
  (4) D8: mission_hud.c carries a private `compress_script_text` mirroring
  content/legacy-script/memory.ts compressScriptText; switch to the D8 port once it exists.
  TS UiRuntime.create no longer takes commands/commandContext/context; mission_hud.c still
  sets them because qa_ui_runtime_options has them.
  (5) TS tests of the packet ported: tests/bots/behavior/asset-files.test.ts,
  tests/bots/navigation/aas-client-trace.test.ts, tests/bots/behavior/rerelease/replan.test.ts,
  the Q3 tests of tests/gameplay/q3/base/peer-input-regressions.test.ts
  (tests/bots/test_bots_d5_drift.c, tests/content/test_q3_d5_drift.c). Updated existing C
  expectations to current TS: test_bots_b (saturating epair, bun), test_library_core
  (projectile dump model offset 80), test_q3_base_ta (pmove_ladders "1", bun),
  test_q3_presentation_depth (compressed menu buffer, bun), test_q3_presentation (cvar table
  90/102, bun), test_q3_director_source (save version 2), test_q3_owner_draw frame
  registration (four cases re-derived from reading frame.ts/weapons.ts, not re-recorded under
  bun: re-record them). Not reproduced: ai-decision, library/botfile-errors,
  library/memory-view (no C counterpart of the cached DataView), rerelease/{frame-snapshot,
  replan-cost}, navigation/{aas-routing,estimate-sharing,q3-routing},
  gameplay/q3/base/train-leg, and the changed hunks of the code-changed tests under
  tests/bots/** and tests/gameplay/q3/** (mostly Biome formatting; not audited hunk by hunk).
  (6) Rerelease brain hazard memo is keyed by the navigation interface ctx (TS: the nodes
  array identity); equal when one interface owns one graph. DONE (gate-bots3): keyed by a per-init
  navigation identity, so a navigation re-initialised in reused storage clears the memo as a new TS
  RuntimeNav does.

- D7 (network, render, world, movement TS drift, 2026-11-03) leftovers. Per-file notes are in docs/port_review.tsv
  (`d7-net`). (1) Model shadows: render/scene/models/renderer.ts `modelShadowGroups` (r_shadows 2 stencil volumes,
  r_shadows 3 RF_SHADOW_PLANE projection) and submissions.ts `SceneShadowGroup`/`shadowVolumeGroup` are not ported.
  stencil-shadow.ts (qa_make_stencil_shadow_volume) and shadowFinishQuad (qa_shadow_finish_quad) are ported and
  world.c already appends shadow-finish, but the C qa_scene_model_group has no shadow-volume kind, so app
  q3_client/scene.c `push_model_group` (D2) must learn it in the same change. (2) D2 call sites: app/bootstrap/network/q2.c
  passes `admin.rcon_guard = NULL` (a fresh guard per packet); TS q2.ts keeps one `rconGuard` per server.
  app/bootstrap/network/q3.c should set userinfo "ip" with qa_q3_peer_userinfo on userinfo updates (TS q3.ts:261).
  app/bootstrap/assets.c calls qa_scene_shader_registry_add_script per file; TS assets.ts:201 calls addScriptFiles once
  (qa_scene_shader_registry_add_script_files, id ScanAndLoadShaderFiles precedence). app renderer.c should call
  qa_scene_image_registry_invalidate_backend where TS renderer.ts:280/442 does. q2-downloads.c resume should use
  qa_download_sink_create_resumable/qa_download_sink_suspend (TS q2-downloads.ts:514/585). (3) D6: QC and QVM mod
  validation should use qa_mod_callback_inputs (world/session/mod_callback_inputs.c) as TS mod-validation.ts does.
  (4) D8: core/files contained-parent errors print strerror text; TS (Node) messages are "ENOTDIR: not a directory,
  open" (tests/network/test_d7_drift fails that one check); content/mounts aborts on "Ambiguous content path", which
  render textures now skip like TS (qa_try around the read) once mounts raise. materials/fog qa_prepare_fog_volume has
  no identityLight parameter (TS fog.ts:35), so WorldSceneOptions.q3IdentityLight is not ported (TS sets it nowhere).
  (5) Kept although TS removed them, because C tests use them: Q2ConnectionlessServer (deleted by gate-net2, 2026-11-11:
  tests/app/test_server_browser.c answers through qa_app_q2_server_network), q3EntityLifetime (tests/world/test_js_semantics_world.c), network/common/loopback.c (TS moved LoopbackHub to
  tests/support). (6) Perf-only TS changes not mirrored (same output): render PERF2 retainedMerge, PERF3 ancestor
  marks and merged-PVS cache, PoseLru pose caches, material snapshot cache, actor registry lowestFree, Q1 brush-cell
  and drawing-node caches, JS row-worker level/lighting-base registries (not applicable to the pthread pool).
  (7) movement/q2/classic.ts and rerelease.ts: the 2a2cb7bd closure-to-class restructure was not compared function by
  function; only the dead-mask change (index.ts) and test-name diffs were checked. Left for the gate read.
  (8) Not ported (need corpus maps or the TS harness): tests/world/collision/q2-source-rules.test.ts, the q2 dead-mask
  movement test, tests/movement/q3/ladders.test.ts, tests/render/{commands/world-translucent-order,
  commands/merge-retention,commands/models/model-shadows,visibility,image-journal,cpu/row-parallel-registry}.test.ts,
  the CPU stencil-pixel case of stencil-shadow.test.ts, tests/network/hardening/*, tests/network/remote-fault-containment,
  unified-event-codec-{generator,symmetry}, q2/mvd-mutation, and the code-changed network/render/world/movement tests
  beyond the cases named in tests/{network,render,world}/test_d7_drift.c (mostly Biome reformatting; not audited
  hunk by hunk). (9) test_sim_verify at 09:46: q2nr 0 mismatches; the other 9 cases differ only in the
  `checkpoint-image` payload hash (every per-part checkpoint line and all state lines match). d1-sim's own binary
  (09:48) gives the same 9; shared_simulation_checkpoint.c and save_image.c were being edited at the time.
- TS drift D1, app/bootstrap/simulation (2026-11-03, d1-sim): every code/types-only/added file under
  src/app/bootstrap/simulation has a row in docs/port_review.tsv (`drift-done` or `drift-partial` with what is left).
  Done: SourceStrategy as `qa_app_source_strategy` (src/app/bootstrap/simulation/source_strategy.{h,c}): one table
  per TS class (Loading, Loaded, Q1, QuakeC, Q2, Q2Native, Q3, Q3Qvm), built by inheritance-style constructors and
  replaced with the source (`qa_ss_set_source_strategy`, TS setSource). Ported members: typed access (asQ1 ...
  asQ2Native), userinfo/storeUserinfo/updateUserinfo/pause, the spawn section (admitClient, prepareBot,
  initialArsenalState, attachEntity, configureAdmitted, attachForeignCharacter, admitCarry, enterWorld,
  captureEntryCarry), q2Gibbed, q3Client, suppliesPunch, skipsTriggerTouch, admitsGuestMonsters, the command section
  (forwardsCommands, preCommand, afterCommand; members qa_ss_*_pre_command/after_command live beside their helpers in
  shared_simulation_players.c), hasGuestUi/guestUi (qa_ss_*_guest_ui in shared_simulation_arsenal.c), and the whole step
  section (lmctf ... watchesTransitions, stepRoute, settles, syncGravity, afterStep, finishFrame, concludeFrame,
  q1Powerups, q1CharacterFrame). SharedSimulation.step is split into the TS phases (beginStepBoundary,
  admitBoundaryInputs, applyBoundaryCommands, advanceBoundaryActors, closeStepBoundary, finishFrameState,
  concludeSourceFrame) in shared_simulation_step.c. No `sim->source.kind == QA_SS_SOURCE_*` comparison remains in
  shared_simulation_*.c: every per-source branch goes through `sim->strategy` (the remaining ones through as_*()).
  Open (D1, updated by D1b and D1c): (1) D1c (2026-11-03, d1c-sim) converted 123 more members; 204 of the 226 TS
  SourceStrategy members are now table members with each class's override (full list with status:
  docs/runtime_layout.md "SourceStrategy members"). `sim->strategy` is now a pointer to a per-class table built once
  (qa_app_create_source_strategy), so qa_app_shared_simulation's layout no longer changes when the table grows (a
  build mixing objects compiled before and after a table change read `stepping` at a shifted offset and failed
  "Simulation step is already running"). D1d (2026-11-03, resumed 2026-11-10,
  d1d-sim) converted the last 22 (save/restore: requireLoaded ... ammoTimer), so all 226 are table members (228 since D9c: TS e06fcfff added importLoading and importOriginal), and did the
  sweep: every remaining `strategy->as_*()` call (132) and `ss_source_is(` call (34, shared_simulation_arsenal.c) is a
  typed access TS makes at the same place, listed per function in docs/runtime_layout.md "Typed access sweep" with the
  branches that were converted instead. test_native_mod_host: the 12 failures of 2026-11-03 were stale expectations
  (TS 2a2cb7bd pooled guest pages moved the guest heap layout; C had ported it); they were regenerated with bun that
  day, and the four classic checkpoint hashes again on 2026-11-10 from TS 6103bb46 after persistence took
  NATIVE_Q2_ORIGINAL_VERSION = 2 (70/70). That same version port makes test_sim_verify q2nc differ from the stored
  oracle in the `q2:classic-native-original` version field and the image hash (8 checkpoint lines, 0 state lines); the
  stored oracle was regenerated by D9c (2026-11-10) and matches again. qa_mod_commands_destroy frees every port bound to it (world/session/
  mod_commands.c). Leaks: the C-only loopback run under LeakSanitizer (all nine families) reports 1,152 bytes, none
  with a frame in this tree (the Q2 carry set, the Q2 cvar binding strings, the QuakeC pusher candidates and the other
  listed sites no longer appear). (2) Done by D1b: native Q3 / Linux qagame through the server game (q3/engine.h,
  qvm_engine.c, server_traps.c, native_guest_source.c; guest_runtime.c drives the engine; guest_artifact
  prepareQ3NativeGame; save.c native-guest checkpoints; content.c selectedQ3NativeGame on D8b's catalog
  native_programs). persistence/save_image.c (D8c area, forced) writes a native-guest checkpoint in
  Q3NativeGuestSource.checkpoint()'s key order (kind, version first; processorLayout spread order). The heap image,
  allocator, Windows heap/TLS/FLS tables and full checkpoint bytes match bun (tests/app/test_q3_native_engine.c vs
  tests/app/oracles/q3_native_engine.ts: authored PE, heap PE with an FLS slot, synthetic ELF; 0 mismatches). ~/qfiles has
  no native qagame, so there is no corpus side-by-side; the full Application `--vm-game 0` run of
  tests/persistence/q3-native-server.test.ts is not reproduced in C. (3) Extractions TS made without behavior change: done by D1d as their own
  C files: foreign-{match-host,tag,lmctf,deathball,horde}-rules.ts (foreign_match_host.{h,c}, foreign_*_rules.c),
  foreign-match-rules.ts (foreign_match_rules.c), monster-admission.ts (monster_admission.{h,c}), diagnostics.ts
  (diagnostics.{h,c}; checked by `test_sim_verify --diagnostics` since D9c). network-q2-guest-core.ts and
  qvm-component-state.ts were extracted by D9c (network_q2_guest_core.{h,c}, qvm_component_state.{h,c}). (4) Bot assets (D5 handoff) done:
  qa_app_sim_load_application_bot_assets_with(content, simulation, legacy) prepares the legacy digest and adds
  "arsenal/provenance" with legacy bytes; application_bots.c (D2 file, call-site change only) passes
  `setup->restore != NULL` and `true` for guest bots as application.ts does. (5) TS tests: of the 19
  tests/first-playable/simulation and 2 prediction test files only physics.test.ts's three new cases are ported
  (tests/app/test_sim_c.c); the other diffs are harness changes (corpusTest/writeCapture) or need a full
  Application on corpus (qc-grants give no-throw, source-round-settlement start 400, q3-catchup, cross-family-q3,
  notarget-selected-monsters, monster-projectile-save, q3-round-restart, prediction/arsenal, prediction/q2-network).

- D2 (app/bootstrap except simulation, TS drift, 2026-11-03) leftovers. Per-file notes are in docs/port_review.tsv
  (`d2-app`). (1) application.ts aacb3b45: `step` must add `simulation.takeAdmissionSettleMilliseconds()` to `elapsed`
  before the pause check (application_step.c, `app->elapsed += frame_milliseconds`); SharedSimulation has no C
  `takeAdmissionSettleMilliseconds`/`admissionSettleMilliseconds` yet (D1). Done by D1b (shared_simulation_players.c,
  application_step.c). (2) Done by D1b: content.ts `selectedQ3NativeGame`
  (`--vm-game 0` selects a mod's qagamex86.dll / qagamei386.so as a native server-game module): the C catalog has no
  `expectation.nativePrograms`, and content.c's prepare step still raises "prepareQ3NativeGame has no C port" (D1/D6).
  The option is parsed (`has_vm_game`/`vm_game`) and the native Q3 module validation and assertRecipeInstalled are in.
  (3) Done: audio/music.ts `player.onFault` ("WARNING: music playback stopped: ...") is wired in
  src/app/bootstrap/audio/music.c to src/audio/music.c's on_fault (D8b hook, D8c wiring). (4) unified-event-codec.ts optional fields `local` (q2
  sound), `helpIcon` (Q2 player view) and `damagePowerupOwner` (attack provenance) are read/written on the save-value
  trees; `local` and `helpIcon` now also convert from/to the typed events (unified_event_values.c, both written only
  when true, as every TS producer sets them); `damagePowerupOwner` rides in simulation events (not a presentation
  payload). (5) Model shadows
  (r_shadows/cg_shadows 2|3): q3_client.c already passes `model_shadows`, but `modelShadowGroups` / the shadow-volume
  kind of qa_scene_model_group (render/scene/models/renderer.ts, world.ts stencil clear) are not ported (D7 area; D7
  added qa_make_stencil_shadow_volume). (6) Not checked (out of scope): AddonLibrary drift (catalog abort/20 s
  timeout, skipped rows, cancelQueue, purgeHidden), StartupMenu draw (menuPanel menuScale). (7) tests/app/
  test_startup_port does not link: src/app/bootstrap/startup_application_{graphics,launch}.c call
  qa_startup_open_graphics/qa_startup_graphics_draw, declared in startup_application_internal.h and defined nowhere
  (StartupMenu openGraphics/draw packet). (8) q3.ts `slice(0, 1023)` of client userinfo counts UTF-16 units; C cuts
  bytes (same for ASCII). (9) Done by D8c: value.c now gives bun's text, "checkpoint: JSON Parse error: Unexpected identifier
  \"not\"" (test_persistence_review; tests/app/test_unified_net.c still checks only the error name). (10) TS tests
  not reproduced in C: the tests/first-playable/application, network and remote-application drift files (66; all need
  a full Application or remote session on corpus, or test harness changes) and tests/app restored-recipe-gate,
  load-path-policy, q2-native-hud-prepare, q3-first-admission, q3-tinfo, addon-library-purge, guest-monsters-entity-text,
  native-pmove-contacts, selected-q3-presentation-collection, prepared-startup, demo-recording (unlink path),
  unified-host, q3-reports (sv_rankingsGameID). New C checks: tests/app/test_d2_drift.c (oracle
  tests/app/oracles/d2_drift_oracle.ts); the unified frame corpus was regenerated for version 10.

- TS drift D9c, app/bootstrap/simulation (2026-11-10, build/agents/d9c): done; per-file notes in docs/port_review.tsv
  (`d9c`), summary and the changed oracle lines in docs/TS_DRIFT.md "D9c status". As run on the final tree, each
  through tools/run_capped.sh: test_sim_verify 10 cases 0 mismatches against the regenerated oracles;
  test_sim_verify --diagnostics 10 cases 0 mismatches; test_q2_rerelease_pak 150/0; test_native_mod_host 70/0;
  test_shared_simulation_save 101/0; test_app_concrete_original_save 9/0; test_sim_a 38/0, test_sim_b 38/0, test_sim_c
  333, test_sim_concrete 108/0, test_sim_bots 459/0, test_sim_network_q3 81/0, test_q3_source ok, test_q3_native_engine
  57 lines 0 mismatches, test_guest_bots_verify 92 lines 0 mismatches, test_sim_native_mod 19/0, test_qvm_mods_depth
  195/0, test_qc_mods_depth 65/0, test_sim_guests 143/0, test_sim_equipment 374/0, test_weapon_behaviors ok,
  test_shared_simulation_core ok, test_shared_simulation_b2 ok. test_remote_loopback: qw, q2, q3 and unified-mixed 0
  differing; q1 105 of 116, unified 1364 of 1458, unified-cross 1352 of 1461, unified-cross-q1 2953 of 3137,
  unified-cross-q3 1352 of 1461 differ, starting at the client's `Unknown command "vid_config_y"` console lines
  (settings/config, D9b); every server transcript 0 differing. test_application_open_real: q2 and q3 0 differing, q1 27
  of 46 (D9b).
  Two stale tests were repaired on the way: test_shared_simulation_b2 built its simulations without a strategy table
  (segfault since D1c; it now calls qa_ss_set_source_strategy), and test_sim_guests aborted in the classic file test
  because classic_guest_files.c compared strerror text with the Node-form message contained.c raises since e171c9680
  (fixed in classic_guest_files.c).
  Open (D9c): (1) Nothing in C runs a world from `options.native_import`: importOriginal/importLoading, the
  nativeRestorePrelude no-level arm and applyQ2LatchedCvars are compiled and reviewed against the TS but not executed
  (TS: tests/first-playable/application/q2-native-original-saves.test.ts, which needs D9b's app/q2-native-saves). The
  import structs are borrowed and must outlive the simulation (its visited levels stay referenced). (2) The autosave
  arm of originalSave (`qa_app_shared_sim_checkpoint_with(sim, {.autosave = true}, ...)`) has no caller yet; D9b's
  application code calls it. (3) linkableClientPrefix has no C test (same TS test). (4) The diagnostics test does not
  call selectArsenalWeapon or recordAttack. (5) src/app/bootstrap/q3_common_cvars.{c,h} (D9b file) gained `logfile`
  because the q3 and q3qvm oracles record it; D9b's console-log.ts port must not register it a second time for Q3
  (TS registerConsoleLog returns early when the cvar exists).

