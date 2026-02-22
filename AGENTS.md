# OpenBC -- Agent Instructions

This file is for AI coding agents (Codex, Claude Code, Copilot, Devin, etc.). The canonical project instructions live in **CLAUDE.md** at the repository root. Read that file in full before doing any work -- it contains the architecture, protocol reference, build system, verified facts, and implementation gotchas that apply to all contributors (human or AI).

Everything below supplements CLAUDE.md with operational workflow and conventions.

---

## Critical: Clean Room Rules

This is a **clean room reimplementation**. These rules are absolute:

- **NEVER** access, search for, or reference original Bridge Commander source code, decompiled code, binary data, or reverse-engineered repositories (GitHub or elsewhere).
- **NEVER** reference binary addresses (`FUN_XXXXXXXX`, `DAT_XXXXXXXX`), struct field offsets, vtable layouts, or decompiled pseudocode.
- All protocol knowledge comes from `docs/` and observable wire behavior (packet traces). If it's not in the clean room docs, it must come from behavioral observation, not binary analysis.

**Legal basis**: Oracle v. Google (2021), Sega v. Accolade (1992). See CLAUDE.md for full details.

---

## Agent Workflow

You are one of several AI agents working concurrently in this repository. Follow these steps in order.

### 1. Set Up Your Branch

Multiple agents may be working at the same time. Never commit directly to `main`.

```bash
git fetch origin
git checkout -b agent/<issue-number>-<short-slug> origin/main
```

Branch naming examples: `agent/42-fix-keepalive`, `agent/55-add-cloak-timer`.

### 2. Read Project Context

Read these files in order before writing any code:

1. **CLAUDE.md** -- architecture, rules, gotchas (mandatory)
2. **This file (AGENTS.md)** -- workflow and conventions
3. **`docs/` relevant to your issue** -- protocol specs, wire formats, game system docs

Do not read the entire codebase upfront. Read only what your issue requires.

### 3. Understand the Issue

Read the assigned issue thoroughly. Before coding, identify:

- **Acceptance criteria** -- what "done" looks like
- **Affected files/modules** -- which source and test files you will touch
- **Edge cases** -- anything mentioned or implied in the issue
- **Clean room implications** -- does this touch protocol or game logic? If so, the answer must come from `docs/`, not guesswork

Post your plan as a comment on the issue before starting implementation.

### 4. Implement

- Follow existing code style and conventions (see below).
- Keep changes scoped tightly to the issue. Do not refactor unrelated code, add unrelated features, or "improve" things outside scope.
- Before each commit, run `git diff` to verify you are only changing what you intend.
- Make small, logical commits with messages that explain *why*, not just *what*.

### 5. Test

Every change must include test coverage:

| Change type | Testing requirement |
|---|---|
| New feature | New test(s) covering the feature |
| Bug fix | Regression test that would have caught the bug |
| Refactor | Existing tests still pass; update if behavior changed |

Run the full suite before opening a PR:

```bash
make all && make test
```

The build must produce **zero warnings** (`-Wall -Wextra -Wpedantic`).

If tests fail:
- **Your changes caused it** -- fix it before proceeding.
- **Pre-existing failure unrelated to your changes** -- note it in the PR description. Do not attempt to fix unrelated broken tests.

### 6. Open a Pull Request

Push your branch and open a PR against `main`:

```bash
git push -u origin agent/<issue-number>-<short-slug>
```

The PR will auto-populate from the repository's pull request template. Fill in every section:

- **Summary** -- what changed and why (1-3 bullets)
- **Changes** -- list of modified/added files with brief descriptions
- **Testing** -- how you verified it works (test names, manual steps)
- **Issue reference** -- `Closes #<number>`

Request review from **@Cadacious**.

### 7. When You Are Stuck

- If blocked after **2 failed attempts** at the same approach, stop. Comment on the issue explaining what you tried and where you are stuck. Do not loop.
- If your change would modify files another agent is likely also touching, note the potential conflict in your PR description.
- **Never** force-push, delete branches you did not create, or run destructive commands (`rm -rf`, `git clean -f`, `git reset --hard`) without explicit approval.

---

## Build & Test

```bash
# Build (cross-compiles from Linux/WSL2 to Win32 via MinGW)
make all

# Run all tests
make test

# Clean (but NEVER delete build/*.log files -- they contain session logs)
make clean
```

Toolchain: `i686-w64-mingw32-gcc` with `-Wall -Wextra -Wpedantic`. Zero warnings policy.

## Project Structure

- `src/` -- C source (checksum, game, json, network, protocol, server)
- `tests/` -- 11 test suites (unit via `test_util.h`, integration via `test_harness.h`)
- `docs/` -- Clean room protocol documentation (the single source of truth)
- `data/` -- Ship/projectile registry (JSON, machine-generated)
- `manifests/` -- Hash manifests for file integrity verification
- `tools/` -- Python 3 CLI tools (data scraper, trace comparison, diagnostics)

## Key Conventions

- C (not C++). No external dependencies beyond MinGW's libc and Winsock2.
- Raw UDP networking, wire-compatible with stock BC 1.1 clients.
- JSON data files drive all game configuration (ships, weapons, maps, rules).
- Test fixtures in `tests/fixtures/` include `.pyc` stub files that must remain tracked.
- Read `docs/` before implementing any protocol or game logic -- do not guess at wire formats.
