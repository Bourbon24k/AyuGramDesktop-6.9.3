# AyuGram Desktop update to Telegram Desktop 7.0.5

## Goal

Update the `ayugram-telegram-v6.9.3` codebase to the official Telegram Desktop
tag `v7.0.5`, while preserving AyuGram functionality. Produce a macOS universal
build and a Windows x64 build or a reproducible Windows build workflow when the
macOS environment cannot cross-compile it.

## Integration approach

The working branch is based on the fork's `ayugram-telegram-v6.9.3` branch.
Telegram Desktop is configured as `upstream`, and tag `v7.0.5`
(`56bd59a32a3b304326ec5dab79de2ba71a4c6101`) is merged into a dedicated
`codex/tdesktop-7.0.5` branch. Conflicts are resolved case-by-case: retain
intentional AyuGram behavior while adapting it to the current upstream API and
build layout. No unrelated cleanup is included.

## Validation

Review merge conflicts and changed AyuGram integration points, run the
project's supported configuration/build checks, and build release artifacts:

- macOS: independent arm64 and x86_64 release builds, combined into a universal
  application with `lipo` where the upstream build supports it;
- Windows: x64 release build through an installed cross-build chain. If no
  compatible chain is available locally, add or update a reproducible GitHub
  Actions Windows workflow and report the exact command/workflow instead of
  claiming a local Windows artifact.

## Deliverables

The repository contains the merge commit, any narrow compatibility fixes needed
for the two target platforms, documented build commands, and locally generated
artifact paths where builds complete.
