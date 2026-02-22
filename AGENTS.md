# OpenBC -- Agent Instructions

This file is for AI coding agents (Codex, Claude Code, Copilot, Devin, etc.). The canonical project instructions live in **CLAUDE.md** at the repository root. Read that file in full before doing any work -- it contains the architecture, protocol reference, build system, verified facts, and implementation gotchas that apply to all contributors (human or AI).

Everything below supplements CLAUDE.md with operational workflow and conventions.

---

## Critical: Clean Room Rules

This is a **clean room reimplementation**. These rules are absolute:

- **NEVER** access, search for, or reference original Bridge Commander source code, decompiled code, binary data, or reverse-engineered repositories. The `SandboxServers/STBC-Reverse-Engineering` repo and any other RE repositories are **strictly off-limits** from this codebase.
- **NEVER** reference the original game's binary addresses (`FUN_XXXXXXXX`, `DAT_XXXXXXXX`), struct field offsets, vtable layouts, or decompiled pseudocode. OpenBC's own symbols and addresses are fine -- only the original game's internals are prohibited.
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
- **See something, say something.** If you observe bugs, code smells, or missing tests unrelated to your current issue, do not fix them inline. Instead, open a new issue describing the problem so it can be triaged and assigned separately.
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

**All tests must pass before you open a PR. No exceptions.** If a test is failing -- whether your changes caused it or not -- fix it before proceeding. Do not open a PR with a failing test suite.

### 6. Open a Pull Request

Before pushing, rebase onto the latest `main` to avoid merge conflicts. This is a fast-moving project -- always verify nothing merged while you were working:

```bash
git fetch origin
git rebase origin/main
```

Resolve any conflicts, then push:

```bash
git push -u origin agent/<issue-number>-<short-slug>
```

The PR will auto-populate from the repository's pull request template. Fill in every section:

- **Summary** -- what changed and why (1-3 bullets)
- **Changes** -- list of modified/added files with brief descriptions
- **Testing** -- how you verified it works (test names, manual steps)
- **Issue reference** -- `Closes #<number>`

When filing new issues (see step 4), use the most specific template available (bug, feature, etc.).

Request review from **@SandboxServers/reviewers**.

### 7. When You Are Stuck

- If blocked after **2 failed attempts** at the same approach, stop. Comment on the issue explaining what you tried and where you are stuck. Do not loop.
- **Need protocol or behavioral analysis?** You can request help from the reverse engineering room. Open a new issue in the OpenBC repo describing what behavior or protocol detail you need clarified. A human will relay it to the RE room, where a separate Claude instance will analyze the original binary, produce documentation, and synthesize it into a clean room acceptable behavior document. You must **never** access the RE room or its repos directly.
- If your change would modify files another agent is likely also touching, note the potential conflict in your PR description.
- **Never** force-push, delete branches you did not create, or run destructive commands (`rm -rf`, `git clean -f`, `git reset --hard`) without explicit approval.

---

## Build & Test

The build system supports native Linux/macOS and cross-compilation to Win32:

```bash
# Native build (auto-detects Linux/macOS)
make all

# Cross-compile to Win32 from Linux/WSL2
make PLATFORM=Windows

# Run all tests
make test

# Clean (but NEVER delete build/*.log files -- they contain session logs)
make clean
```

| Platform | Toolchain | Notes |
|---|---|---|
| Linux / macOS | `cc` (system GCC or Clang) | Native build, auto-detected |
| Windows (cross) | `i686-w64-mingw32-gcc` | Cross-compile from Linux/WSL2 |

All platforms use `-std=c11 -Wall -Wextra -Wpedantic`. Zero warnings policy.

## Project Structure

- `src/` -- C source (checksum, game, json, network, protocol, server)
- `tests/` -- 11 test suites (unit via `test_util.h`, integration via `test_harness.h`)
- `docs/` -- Clean room protocol documentation (the single source of truth)
- `data/` -- Ship/projectile registry (JSON, machine-generated)
- `manifests/` -- Hash manifests for file integrity verification
- `tools/` -- Python 3 CLI tools (data scraper, trace comparison, diagnostics)

## Key Conventions

- C (not C++). No external dependencies beyond libc and platform socket APIs (Winsock2 on Windows, BSD sockets on Linux/macOS).
- Raw UDP networking, wire-compatible with stock BC 1.1 clients.
- **Data formats**: JSON for machine-generated/scraped data (ship registries, manifests). TOML for human-managed configuration.
- Test fixtures in `tests/fixtures/` include `.pyc` stub files that must remain tracked.
- Read `docs/` before implementing any protocol or game logic -- do not guess at wire formats.
