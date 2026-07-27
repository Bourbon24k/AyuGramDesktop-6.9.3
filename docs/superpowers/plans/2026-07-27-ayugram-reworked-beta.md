# AyuGram Reworked Beta Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship an AyuGram Reworked beta with an opt-in device-bound local-session protector, optional OS authentication, default-enabled loopback WSS connectivity with direct fallback, and a display-only rebrand that preserves the existing profile.

**Architecture:** Keep Telegram Desktop's existing `MTP::ProxyData` path as the only networking integration point and operate the bypass as a separately versioned loopback MTProto helper. Extend `Storage::Domain` with an explicit protection envelope around the local key; platform adapters own all OS-vault and system-authentication calls. Branding changes are confined to presentation and packaging metadata, never storage identity.

**Tech Stack:** C++20, Qt, Telegram Desktop local storage, Windows DPAPI and Windows Hello APIs, macOS Keychain and LocalAuthentication, platform CMake, static checks only.

---

## File structure

- `Telegram/SourceFiles/ayu/reworked/connectivity/*`: loopback-helper lifecycle, health status, and diagnostics.
- `Telegram/SourceFiles/ayu/reworked/session_protection/*`: common protection state, migration, and policy boundaries.
- `Telegram/SourceFiles/platform/win/reworked_session_vault_win.cpp`: DPAPI and Windows Hello adapter.
- `Telegram/SourceFiles/platform/mac/reworked_session_vault_mac.mm`: Keychain and LocalAuthentication adapter.
- `Telegram/SourceFiles/storage/storage_domain.*`: local-key envelope integration and migration entry points.
- `Telegram/SourceFiles/settings/sections/*`: settings controls and passcode/session-protection workflow.
- `Telegram/Resources/langs/lang.strings`: user-facing strings.
- `Telegram/Resources/*`, `lib/xdg/*`, `.github/workflows/windows-release.yml`: Reworked presentation and packaged artifacts without changing persistent identifiers.

### Task 1: Freeze storage and product compatibility invariants

**Files:**
- Modify: `docs/superpowers/specs/2026-07-27-ayugram-reworked-security-and-connectivity-design.md`
- Create: `Telegram/SourceFiles/ayu/reworked/session_protection/session_protection_contract.h`
- Test: `Telegram/SourceFiles/ayu/reworked/session_protection/session_protection_contract_test.cpp`

- [ ] **Step 1: Write the failing contract test**

```cpp
TEST(SessionProtectionContract, PreservesLegacyProfileIdentity) {
    const auto identity = Reworked::SessionProtection::LegacyIdentity();
    EXPECT_EQ(identity.profileDirectory, u"tdata"_q);
    EXPECT_EQ(identity.bundleIdentifier, u"com.ayugram.desktop"_q);
}
```

- [ ] **Step 2: Run the targeted static test registration check**

Run: `rg -n 'SessionProtectionContract|tdata|com\.ayugram\.desktop' Telegram/SourceFiles/ayu/reworked/session_protection`

Expected: the test is present and initially fails to compile because `LegacyIdentity()` is not defined. Do not build the project.

- [ ] **Step 3: Define the invariant-bearing API**

```cpp
namespace Reworked::SessionProtection {
struct LegacyIdentity {
    QString profileDirectory;
    QString bundleIdentifier;
};

[[nodiscard]] LegacyIdentity CurrentLegacyIdentity();
} // namespace Reworked::SessionProtection
```

Implement it with the existing working directory and bundle constants rather than literals duplicated across migration code.

- [ ] **Step 4: Register the new source and test with the nearest existing Ayu CMake target**

Run: `rg -n 'ayu_state\.cpp|ayu_settings\.cpp' Telegram/SourceFiles/ayu -g CMakeLists.txt`

Expected: every new common source is listed exactly once; no platform source is included in the wrong target.

- [ ] **Step 5: Commit**

```bash
git add docs/superpowers/specs Telegram/SourceFiles/ayu/reworked
git commit -m "Add Reworked session protection contract"
```

### Task 2: Add device-bound local-key envelopes

**Files:**
- Modify: `Telegram/SourceFiles/storage/storage_domain.h`
- Modify: `Telegram/SourceFiles/storage/storage_domain.cpp`
- Create: `Telegram/SourceFiles/ayu/reworked/session_protection/session_protection.h`
- Create: `Telegram/SourceFiles/ayu/reworked/session_protection/session_protection.cpp`
- Test: `Telegram/SourceFiles/ayu/reworked/session_protection/session_protection_test.cpp`

- [ ] **Step 1: Write failing tests for the three required states**

```cpp
TEST(SessionProtection, LegacyEnvelopeNeedsOnlyPasscode) { }
TEST(SessionProtection, ProtectedEnvelopeRejectsMissingVaultSecret) { }
TEST(SessionProtection, ProtectedEnvelopeRejectsCopiedVaultSecret) { }
```

Each test uses a fake `Vault` that returns deterministic random bytes; no test reads a real Keychain or DPAPI entry.

- [ ] **Step 2: Add a versioned envelope, never change legacy decoding in place**

```cpp
enum class EnvelopeVersion : uchar { Legacy = 0, DeviceBound = 1 };
struct ProtectedKeyEnvelope {
    EnvelopeVersion version;
    QByteArray salt;
    QByteArray encryptedLocalKey;
};
```

Derive the wrapping key from the user passcode and the vault secret, serialize the version before the payload, and reject malformed or unavailable vault data before decrypting account maps.

- [ ] **Step 3: Wire envelope load, enable, disable, and migration through `Storage::Domain`**

```cpp
[[nodiscard]] StartResult start(const QByteArray &passcode);
[[nodiscard]] bool enableSessionProtection(const QByteArray &passcode);
[[nodiscard]] bool disableSessionProtection(const QByteArray &passcode);
```

`enableSessionProtection()` writes the vault secret first and commits the new envelope only after that succeeds. `disableSessionProtection()` requires both successful passcode validation and vault retrieval before writing a legacy envelope.

- [ ] **Step 4: Static review**

Run: `git diff --check && rg -n 'DeviceBound|enableSessionProtection|disableSessionProtection' Telegram/SourceFiles/storage Telegram/SourceFiles/ayu/reworked/session_protection`

Expected: migration API is reachable only from protection settings, and no plaintext local key is written to logs or settings.

- [ ] **Step 5: Commit**

```bash
git add Telegram/SourceFiles/storage Telegram/SourceFiles/ayu/reworked/session_protection
git commit -m "Protect local session keys with a device vault"
```

### Task 3: Implement platform vault and strong-auth adapters

**Files:**
- Create: `Telegram/SourceFiles/platform/platform_reworked_session_vault.h`
- Create: `Telegram/SourceFiles/platform/win/reworked_session_vault_win.cpp`
- Create: `Telegram/SourceFiles/platform/mac/reworked_session_vault_mac.mm`
- Modify: platform CMake source lists that compile `specific_win.cpp` and `specific_mac.mm`
- Test: fake-vault tests from Task 2

- [ ] **Step 1: Define the platform-neutral boundary**

```cpp
namespace Platform::ReworkedSessionVault {
enum class Result { Success, Unavailable, Denied, Corrupt };
Result Store(const QByteArray &secret);
Result Load(QByteArray *secret);
Result Remove();
Result AuthenticateUser();
[[nodiscard]] bool CanAuthenticateUser();
} // namespace Platform::ReworkedSessionVault
```

- [ ] **Step 2: Implement Windows scope**

Use `CryptProtectData` and `CryptUnprotectData` with current-user scope and a fixed product-specific entropy label. Ask for Windows Hello consent only when strong authentication is enabled; return `Unavailable` when Windows Hello is not configured instead of silently weakening the setting.

- [ ] **Step 3: Implement macOS scope**

Store the secret in a Keychain item whose service and account names are product-specific. Use `LAContext` for Touch ID/device-owner authentication only when strong authentication is enabled. Return `Denied` on cancellation and never fall back to an unprotected secret.

- [ ] **Step 4: Check compilation ownership without building**

Run: `rg -n 'reworked_session_vault_(win|mac)' Telegram/SourceFiles -g CMakeLists.txt && git diff --check`

Expected: exactly one source selected per platform, none selected by Linux.

- [ ] **Step 5: Commit**

```bash
git add Telegram/SourceFiles/platform Telegram/SourceFiles/ayu/reworked/session_protection
git commit -m "Add platform session vault adapters"
```

### Task 4: Add Session Protection settings and lock flow

**Files:**
- Modify: `Telegram/SourceFiles/settings/sections/settings_local_passcode.cpp`
- Modify: `Telegram/SourceFiles/settings/sections/settings_local_passcode.h`
- Modify: `Telegram/SourceFiles/core/application.cpp`
- Modify: `Telegram/Resources/langs/lang.strings`
- Modify: relevant `.style` file for the section

- [ ] **Step 1: Add failing state tests around enable/disable actions**

```cpp
TEST(SessionProtectionSettings, DoesNotEnableWithoutPasscode) { }
TEST(SessionProtectionSettings, RefusesStrongModeWhenPlatformUnavailable) { }
TEST(SessionProtectionSettings, RequiresVaultAndPasscodeToDisable) { }
```

- [ ] **Step 2: Add controls to the existing Local Passcode section**

Add a disabled-by-default Session Protection toggle, a strong-authentication toggle visible only after protection is enabled, and an idle-duration selector that feeds existing `Application::checkAutoLock()` timing. Use localized strings and style constants only.

- [ ] **Step 3: Authenticate before unlocking a protected session**

After existing passcode validation and before `unlockPasscode()`, call `AuthenticateUser()` only when strong mode is enabled. Leave the application locked on `Denied`, `Unavailable`, or `Corrupt`.

- [ ] **Step 4: Run static checks**

Run: `git diff --check && rg -n 'Session Protection|AuthenticateUser|enableSessionProtection' Telegram/SourceFiles/settings Telegram/SourceFiles/core Telegram/Resources/langs/lang.strings`

Expected: no hardcoded UI dimensions or untranslated user-facing strings.

- [ ] **Step 5: Commit**

```bash
git add Telegram/SourceFiles/settings Telegram/SourceFiles/core Telegram/Resources/langs
git commit -m "Expose opt-in session protection settings"
```

### Task 5: Add the loopback connectivity controller

**Files:**
- Create: `Telegram/SourceFiles/ayu/reworked/connectivity/reworked_connectivity_controller.h`
- Create: `Telegram/SourceFiles/ayu/reworked/connectivity/reworked_connectivity_controller.cpp`
- Modify: `Telegram/SourceFiles/core/application.cpp`
- Modify: `Telegram/SourceFiles/core/core_settings_proxy.*`
- Test: `Telegram/SourceFiles/ayu/reworked/connectivity/reworked_connectivity_controller_test.cpp`

- [ ] **Step 1: Write failing controller tests**

```cpp
TEST(ReworkedConnectivity, UsesLoopbackOnlyAfterHealthyHelper) { }
TEST(ReworkedConnectivity, RestoresPreviousProxyWhenHelperFails) { }
TEST(ReworkedConnectivity, NeverEditsSystemProxySettings) { }
```

- [ ] **Step 2: Define a state machine with explicit fallback**

```cpp
enum class ConnectivityState { Disabled, Starting, Ready, FallingBack, Failed };
[[nodiscard]] MTP::ProxyData loopbackProxy() const;
void start();
void stop();
void healthCheck();
```

The loopback proxy is `MTP::ProxyData::Type::Mtproto` and is selected only after a bounded health check succeeds. On failure, restore the user's prior `SettingsProxy` selection and set the diagnostic phase; do not alter OS proxy settings.

- [ ] **Step 3: Persist only user intent and safe diagnostics**

Add a default-enabled `reworkedConnectivityEnabled` flag, an explicit user disable flag, and a non-secret last failure phase. Never store helper credentials in logs or use a broad system proxy configuration.

- [ ] **Step 4: Static checks**

Run: `git diff --check && rg -n 'ConnectivityState|loopbackProxy|reworkedConnectivityEnabled' Telegram/SourceFiles/ayu/reworked Telegram/SourceFiles/core`

Expected: all fallback paths call `Application::setCurrentProxy()` with the prior state.

- [ ] **Step 5: Commit**

```bash
git add Telegram/SourceFiles/ayu/reworked/connectivity Telegram/SourceFiles/core
git commit -m "Add Reworked loopback connectivity controller"
```

### Task 6: Package and supervise the helper

**Files:**
- Create: `third_party/tg-ws-proxy/LICENSE`
- Create: `third_party/tg-ws-proxy/REVISION`
- Modify: platform packaging CMake files for Windows and macOS
- Modify: `.github/workflows/windows-release.yml`
- Modify: release documentation

- [ ] **Step 1: Vendor only the audited helper source and license metadata**

Record the exact upstream commit, license text, source checksum, supported loopback port, and command-line arguments. Reject helper binaries whose checksum does not match the packaged manifest.

- [ ] **Step 2: Add a platform launcher boundary**

The launcher starts the helper with loopback binding only, a private runtime directory, no shell interpolation, and a bounded startup deadline. It terminates the child on application shutdown and does not expose a listener on a LAN interface.

- [ ] **Step 3: Wire artifacts**

Include the helper only in Reworked Windows and macOS portable/package outputs. Keep the helper's license and revision beside the binary and label the release notes with the embedded helper version.

- [ ] **Step 4: Static checks**

Run: `git diff --check && rg -n '127\.0\.0\.1|localhost|tg-ws-proxy|LICENSE' third_party Telegram .github`

Expected: no `0.0.0.0` listener, no unchecked downloaded binary, and every packaged helper has license metadata.

- [ ] **Step 5: Commit**

```bash
git add third_party Telegram .github docs
git commit -m "Package loopback connectivity helper"
```

### Task 7: Rebrand presentation without changing data identity

**Files:**
- Modify: `Telegram/Resources/langs/lang.strings`
- Modify: `Telegram/SourceFiles/tray.cpp`
- Modify: `Telegram/SourceFiles/core/version.h`
- Modify: `Telegram/Resources/winrc/Telegram.rc`
- Modify: `lib/xdg/com.ayugram.desktop.desktop`
- Modify: `lib/xdg/com.ayugram.desktop.metainfo.xml`
- Modify: `README.md`
- Modify: `README-RU.md`
- Modify: `changelog.txt`

- [ ] **Step 1: Inventory each display name before editing**

Run: `rg -n 'AyuGram' Telegram/Resources Telegram/SourceFiles lib README.md README-RU.md changelog.txt`

Classify every hit as presentation, compatibility identifier, external upstream reference, or historical changelog entry.

- [ ] **Step 2: Replace presentation-only names with `AyuGram Reworked`**

Keep `com.ayugram.desktop`, the working directory, and `tdata` untouched. Preserve upstream links and historical entries when they name the original project.

- [ ] **Step 3: Update release workflow labels**

Rename workflow and artifact display labels to `AyuGram-Reworked-7.0.5-*` without changing build paths or the Windows AppUserModelID.

- [ ] **Step 4: Static checks**

Run: `git diff --check && rg -n 'com\.ayugram\.desktop|tdata' Telegram lib && rg -n 'AyuGram Reworked' Telegram/Resources Telegram/SourceFiles lib README.md README-RU.md`

Expected: compatibility identifiers remain present and all changed presentation strings are localized or metadata labels.

- [ ] **Step 5: Commit**

```bash
git add Telegram/Resources Telegram/SourceFiles lib .github README.md README-RU.md changelog.txt
git commit -m "Rebrand beta as AyuGram Reworked"
```

### Task 8: Security review and release verification

**Files:**
- Create: `docs/security/ayugram-reworked-session-protection.md`
- Modify: `changelog.txt`

- [ ] **Step 1: Document the threat boundary**

State that copied-at-rest `tdata` is protected only when Session Protection is enabled, and explicitly exclude unlocked-process malware, keylogging, remote control, and same-user memory scraping.

- [ ] **Step 2: Verify source integrity and sensitive-data handling**

Run: `git diff --check && rg -n -i 'password|passcode|vault|dpapi|keychain|secret' Telegram/SourceFiles/ayu/reworked Telegram/SourceFiles/storage Telegram/SourceFiles/platform`

Expected: no secret is formatted into a log, diagnostic, crash report, or UI string.

- [ ] **Step 3: Verify packaging metadata**

Run: `rg -n 'tg-ws-proxy|AyuGram Reworked|Session Protection' README.md README-RU.md changelog.txt .github/workflows`

Expected: release materials name both platform artifacts, helper license, opt-in protection default, and macOS/Windows caveats.

- [ ] **Step 4: Commit**

```bash
git add docs/security changelog.txt
git commit -m "Document Reworked security boundaries"
```

## Plan self-review

- Spec coverage: Tasks 2-4 cover passcode, vault binding, strong authentication, migration, and recovery; Tasks 5-6 cover loopback helper, default behavior, diagnostics, and fallback; Task 7 covers the visual rebrand without changing profile identity; Task 8 documents the security boundary and release checks.
- Placeholder scan: no deferred implementation markers are used; every task names its files, validation command, and commit.
- Type consistency: `Platform::ReworkedSessionVault`, `Reworked::SessionProtection`, `ConnectivityState`, and `Storage::Domain` are the only new boundaries referenced across tasks.

