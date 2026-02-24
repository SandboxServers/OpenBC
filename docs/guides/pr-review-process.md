# PR Review Process — CodeRabbit Autonomous Loop

**MANDATORY**: Follow this loop for all PRs that have CodeRabbit enabled.

## Overview

CodeRabbit automatically reviews every push to an open PR. The review process is autonomous — fixes are applied iteratively until CodeRabbit is satisfied and CI passes. No human intervention needed until the final approval step.

## The Loop

### 1. Push a commit

Push your changes to the PR branch.

### 2. Monitor CodeRabbit status

Check the CodeRabbit summary comment (first auto-generated comment on the PR). It indicates whether the review is:
- **In progress**: "Currently processing new changes..."
- **Rate limited**: Review delayed due to rate limits
- **Config issues**: Configuration problems noted
- **Complete**: None of the above flags present

### 3. Wait for review

Check for new review comments since the last commit.

**Important**: Reviews can contain "outside diff range" comments in the top-level review body — always read the **full review body**, not just inline comments.

### 4. Evaluate findings

For each actionable comment CodeRabbit posts:
- **Verify** each finding against the actual code before fixing
- **Fix every issue**, no matter how trivial
- **Do NOT respond** to or reply to CodeRabbit's comments
- **Open a GitHub issue** if a comment requires substantial rearchitecting or goes against the spirit of the change — include detailed reasoning

### 5. Rate limit discipline

- Free plan allows **3 back-to-back reviews** upfront, then throttles to **~2 reviews per hour** (summary-only thereafter)
- Must wait at least **30 minutes** after the last review completed before pushing (allows for both the minimum cooldown and the throttled cadence)
- **After a rate limit**: CodeRabbit does NOT auto-retry. You must retrigger by commenting `@coderabbitai review` on the PR or pushing a new commit

### 6. Push the fix commit

- **Local build** (cross-compile from WSL2 to Win32):
  - `make PLATFORM=Windows all` — zero warnings required
  - `make PLATFORM=Windows test` — all tests must pass
- **CI validates all three platforms** (Windows, Linux, macOS) per the three-OS hard rule. Direct Linux/macOS builds require native toolchains provided by CI; the local cross-compile targets Windows only.
- If any remote CI build failures were found on the previous push, bundle that fix into this push
- Commit and push

### 7. Loop

Go back to step 2 and wait for the next CodeRabbit review.

### 8. Exit condition

CodeRabbit is considered satisfied when:
- CodeRabbit's status check is **green**, AND
- Either: (a) CodeRabbit posted a review with **no actionable comments**, OR (b) **no new review** appears within 5 minutes of the status check going green

### 9. Notify for human review

Ping the user for human review and **stop processing**.

## Checking PR Status

```bash
# Check CI build status
gh pr view <PR> --json statusCheckRollup

# Check review list with commit SHAs and timestamps
gh pr view <PR> --json reviews

# Get full review body (includes outside-diff comments)
gh api repos/SandboxServers/OpenBC/pulls/<PR>/reviews

# Check summary comment for "in progress" / "rate limited" flags
gh pr view <PR> --json comments
```

## Key Rules

| Rule | Details |
|------|---------|
| Fix all feedback | Every CodeRabbit comment gets addressed, no matter how trivial |
| Don't reply | Never respond to CodeRabbit's reviews or comments |
| Open issues for substantial changes | Substantial/rearchitecture feedback → GitHub issue with reasons |
| Tests must pass | All tests green, zero warnings before every push |
| Rate limits | 3 upfront reviews, then ~2/hour; wait ~30 min between pushes; retrigger after rate limit |
| Never push to main | Always get user confirmation before pushing to main |
