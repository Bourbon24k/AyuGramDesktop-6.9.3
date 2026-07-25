# Implementation plan: AyuGram → Telegram Desktop v7.0.5

## Goal

Merge Telegram Desktop `v7.0.5` (peeled commit
`56bd59a32a3b304326ec5dab79de2ba71a4c6101`) into the
`ayugram-telegram-v6.9.3` fork without losing any AyuGram functionality. The
required outputs are a native macOS universal Release app and a native Windows
x64 Release app, each checked against existing AyuGram data in a disposable
profile.

The complete implementation checklist and conflict ledger format are in the
[canonical task plan](../../../.ai/tdesktop-7.0.5/a/plan.md).

## 1. Source acquisition and branch boundary

1. Confirm a clean source worktree and retain unrelated planning artifacts.
2. Fetch only the official tag and verify it before any merge:

   ```sh
   test "$(git remote get-url upstream)" = 'https://github.com/telegramdesktop/tdesktop.git'
   git fetch --no-tags upstream refs/tags/v7.0.5:refs/tags/v7.0.5
   git rev-parse v7.0.5^{commit}
   git verify-tag v7.0.5
   ```

3. Start `codex/tdesktop-7.0.5` from `ayugram-telegram-v6.9.3`; do not rebase
   AyuGram commits and do not overwrite an existing update branch.
4. Record a pre-merge, six-queue inventory of every Ayu-touched source,
   resource, identity and build path, including automatic-merge paths. Keep it
   and the conflict ledger outside the merge commit.
5. Start a reviewable merge, without an automatic merge commit:

   ```sh
   git merge --no-ff --no-commit v7.0.5^{commit}
   git diff --name-only --diff-filter=U
   ```

## 2. Conflict-resolution strategy

Resolve each queue after comparing merge-base, fork and upstream versions; do
not apply blanket `--ours` or `--theirs` resolutions.

| Queue | Areas | Preservation requirement |
| --- | --- | --- |
| Foundation | `Telegram/SourceFiles/ayu/{ayu_*,data,utils,libs,workers,lang,url_handlers}` | Retain settings, JSON/SQLite persistence, migrations, language and URL handling. |
| Features | `ayu/features/{filters,forward,message_shot,streamer_mode,translator}` | Retain filters, forwarding/sync, message shots, streamer mode and Google/Yandex translation. |
| Settings/UI | `ayu/ui/{settings,boxes,components,context_menu,message_history}`, styles | Retain settings pages, appearance/font/theme, dialogs, menus, history, icons and toasts. |
| Host integration | `settings`, `history`, `data`, `chat_helpers`, `boxes`, `ui`, `window`, `core`, `api`, `media`, `calls`, `platform`, `info`, `intro`, `dialogs` | Adopt v7.0.5 structure while reconnecting every Ayu hook to its current upstream lifecycle/API. |
| Resources/identity | `Telegram/Resources`, `Telegram/Telegram`, plist, README, `.github` | Retain AyuGram product identity, assets, translations and release content. |
| Build/release | CMake, `Telegram/build`, `cmake`, submodules, Windows workflow | Take current upstream toolchain/dependency layout then re-register every Ayu source/resource and platform compatibility fix. |

For each manual resolution, record path, category, retained behavior, upstream
adaptation, and evidence. Stage only complete queues and run:

```sh
git diff --cached --check
git diff --cached --submodule=log
git diff --name-only --diff-filter=U
```

Commit only when all unmerged paths are resolved:

```sh
git commit -m 'Merge Telegram Desktop v7.0.5 into AyuGram'
```

## 3. Feature-preservation checks

Before release, inspect all pre-existing Ayu integration points and prove:

- CMake/resources include every Ayu source, style and generated input;
- existing JSON settings, SQLite local history, language strings and deep links
  safely load from an old profile;
- ghost/read controls, anti-recall/history, local premium and appearance/font/
  theme controls are reachable and persist;
- filters, forwarding/sync, message shots, streamer mode and both translators
  remain connected to current message/UI lifecycles;
- chat context menus, composer confirmations, tray/profile/menu extensions,
  calls, media and macOS force-click hooks still compile and run;
- AyuGram name, icons, translations, bundle metadata and Windows release naming
  remain consistent.

Run at minimum:

```sh
git diff --check 94540dd219..HEAD
git fsck --no-reflogs
git submodule status --recursive
rg -n 'ayu/|Ayu|AYU_' CMakeLists.txt Telegram/CMakeLists.txt Telegram/SourceFiles
```

Smoke-test both platform artifacts with copied pre-existing AyuGram data rather
than a user profile: record profile, feature, action and outcome.

## 4. macOS universal Release handoff

On a clean authorized macOS build worktree, use Xcode 26.1, at least 55 GB
free disk space and current Homebrew prerequisites. Initialize submodules and
prepare universal dependencies:

```sh
git submodule sync --recursive
git submodule update --init --recursive
brew install git automake libtool cmake wget pkg-config gnu-tar ninja nasm meson
sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
./Telegram/build/prepare/mac.sh
cd Telegram
./configure.sh \
  -D TDESKTOP_API_ID=2040 \
  -D TDESKTOP_API_HASH=b18441a1ff607e10a989891a5462e627 \
  -D DESKTOP_APP_MAC_ARCH="arm64;x86_64"
cmake --build ../out --config Release --target Telegram --parallel
```

Do not use the production packaging script: it requires signing, notarization,
symbol-upload and backup infrastructure. Confirm both actual architectures:

```sh
test -d ../out/Release/Telegram.app
lipo -info ../out/Release/Telegram.app/Contents/MacOS/Telegram
file ../out/Release/Telegram.app/Contents/MacOS/Telegram
codesign --verify --deep --strict ../out/Release/Telegram.app
```

`lipo` must name both `arm64` and `x86_64`. Smoke test natively and, when
available, under Rosetta.

## 5. Windows x64 Release handoff

Build natively on Windows 2022 or Windows with VS 2022, Windows SDK
`10.0.26100.0`, x64 MFC/ATL, Python 3.10 and Git, using an x64 Native Tools
prompt:

```bat
Telegram\build\prepare\win.bat
cd Telegram
configure.bat x64 -D TDESKTOP_API_ID=2040 -D TDESKTOP_API_HASH=b18441a1ff607e10a989891a5462e627
cmake --build ..\out --config Release --target Telegram --parallel 1
```

If local Windows tooling is unavailable, dispatch the checked-in
`.github/workflows/windows-release.yml` at the merge commit. Preserve its
`windows-2022`, recursive-submodule, x64 MSVC setup, serialized build, and
`TDESKTOP_SKIP_DUMP_SYMS=1` safeguards unless an upstream change requires and
proves a replacement. Upload `out/Release` excluding debug/link artifacts;
launch smoke-test the executable and retain the Actions URL/log/artifact name.

## 6. Final review and completion

Independently review every conflict-resolution file, build graph, platform
file, workflow and feature contract. Fix only narrow regressions and repeat the
relevant validation. The final report must include merge/tag hashes, conflict
ledger, changed files, preservation results, macOS artifact and `lipo` proof,
Windows artifact or Actions URL, smoke-test evidence and any explicit deferred
risk.

This repository's agent instruction says not to build in the present checkout.
Therefore this plan records exact reproducible native-build commands, but does
not authorize reporting local artifacts until an authorized macOS/Windows build
worktree or runner has executed them.
