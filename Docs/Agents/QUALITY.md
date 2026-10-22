# Code quality pass: rubric and rules

Goal: every area scores **10/10** on this rubric, and reads like it was written by an experienced
Unreal gameplay programmer on a shipping team. **No behaviour changes.** Same gameplay, same visuals, same
outputs: refactor, don't redesign.

## Rubric (score each 1-10, report before and after per area)

1. **Structure.** Files have one job. No god files: split anything over ~800 lines into cohesive units
   (e.g. `XGame.cpp` into game flow, presentation, input/bots, camera). Headers are short and readable: public
   API first, then protected, then private; no implementation detail leaks into public headers.
2. **Naming and UE conventions.** Unreal coding standard: `F`/`U`/`A`/`S`/`E`/`I` prefixes, `b` bools,
   PascalCase, `TObjectPtr` UPROPERTY members, `FName` for identifiers that are compared, `TEXT()`, `UE_LOG` with a
   per-module or per-game `DEFINE_LOG_CATEGORY_STATIC` category. No abbreviations a newcomer can't decode.
3. **Tunables.** No unexplained magic numbers in gameplay or presentation code. Values a designer would tune
   live in named `constexpr` constants grouped at the top of the file, or, where tuning in editor makes sense,
   in a `USTRUCT` of tunables exposed through `UPROPERTY(EditAnywhere/EditDefaultsOnly, Category=...)` on the
   actor/game mode, with the same defaults. Port parity numbers keep their source reference in one short comment.
4. **Functions.** Short and single-purpose; early returns over deep nesting; no 300-line Tick. Pure helpers in
   anonymous namespaces or the rules module. `const` everywhere it applies (methods, locals, params).
5. **Ownership and lifetime.** UPROPERTY on every UObject pointer member; weak pointers for callbacks that can
   outlive the owner; timers via `CreateWeakLambda` and cleared in `EndPlay`; components created with unique names;
   no raw `new`; no global mutable state unless it's a deliberate, documented session cache.
6. **Duplication.** Shared patterns live once: game-mode boilerplate (options, command-line seats, test hooks,
   room binding, live scores, completion), HUD helpers (projection, cached text, standings), venue helpers
   (instanced groups, material caches), FX emit recipes. Use the shared base/helpers the framework provides.
7. **Comments.** Explain *why* and the non-obvious *what*; never narrate the code. A short header comment per
   class (what it is, who owns it, how it's used). Keep port references (`engine.ts:428`) where they justify a
   number or rule. Remove stale, chatty or essay comments; fix ones that are wrong.
8. **Robustness.** `check`/`ensure` on programmer errors, graceful handling of runtime ones (missing assets,
   room disconnects); no silent failure; logging at the right verbosity (Verbose for per-frame, never Display/Log
   in hot paths).
9. **Performance hygiene.** No per-frame allocations, string building, linear searches or redundant component
   updates in hot paths (Tick, OnPaint, sensor frames). Already-audited items stay fixed (see `Docs/Audit/`).
10. **Tests and test hooks.** Rules and pure logic are covered (golden tests untouched and passing). Test/debug
    command-line flags are grouped in one place per game (a small `F<Game>DebugFlags` parsed once), documented,
    and compiled out of Shipping (`#if !UE_BUILD_SHIPPING`) where they change gameplay.

## Hard rules

- **No behaviour change.** Every existing test passes untouched; golden/determinism tests are bit-exact.
  Sim/ and every `*Rules` module must produce identical outputs. Take before/after screenshots of the same
  moments and compare them; they must match except for incidental particle randomness.
- Keep public names that other areas call unless you update every caller *inside your own area*. Don't edit
  files outside your area; if a shared change is needed, describe it in your report instead.
- Build with private TMP/TEMP, `-Log=<private>` and `-NoMutex` (see BRIEF.md). Commit in small, reviewable
  steps (`refactor(<area>): ...`), each building and passing tests.
- Report: before/after score per rubric line with one sentence of evidence each, the commits, files split or
  added, anything you chose not to change and why, and the before/after screenshot pairs you compared.
