# AyuGram Reworked: connectivity, branding, and session protection

## Scope

This beta branch adds an AyuGram Reworked presentation, a default-enabled local connectivity helper integration, and optional protection against offline transfer of local Telegram session data. The existing bundle identifier and `tdata` profile location remain unchanged.

## Connectivity

The application manages a packaged local MTProto-to-WSS helper bound only to loopback. When it passes a health check, AyuGram uses its loopback MTProto endpoint as the default route. The helper selects a WSS route and falls back to a normal direct route when its preferred route is unavailable.

The client must not redirect unrelated system traffic. The connection settings provide an explicit off switch, fallback selection, and diagnostic status that distinguishes helper startup, DNS, TCP, TLS, and MTProto failures. A failed helper startup or failed route must restore the standard Telegram Desktop connection path without blocking login or messaging.

The helper is introduced as a separately versioned, auditable component. Its code and license must be preserved, and each supported platform receives a native packaged binary rather than a runtime Python dependency.

## Branding

All user-visible product names, tray labels, window titles, installer labels, release artifact names, and branded resources use `AyuGram Reworked`. The bundle identifier, AppUserModelID, working directory, and `tdata` profile path are intentionally retained for continuity with existing AyuGram installations.

## Session protection

Session Protection is opt-in and disabled by default. Enabling it requires a local passcode and creates a random device secret. The secret is stored in the operating-system credential store and participates in protecting the local storage key:

- Windows uses a DPAPI secret scoped to the current user.
- macOS uses a Keychain item scoped to the current user and application access group.

Opening protected `tdata` requires the configured passcode and access to the OS-stored device secret. A copied profile on another machine or user account therefore cannot unlock the stored Telegram authorization data.

An optional stronger mode requires system authentication on startup and after configured inactivity. On macOS it uses LocalAuthentication with Touch ID where available; on Windows it uses Windows Hello or the platform consent API. The feature must fail closed when a user enables it but the platform cannot complete authentication.

Disabling protection requires a successful passcode and OS-vault check, then writes the legacy-compatible local-key wrapping. If the OS credential is lost, the supported recovery path is to remove the local profile and sign in again; the application does not add a bypass or hidden recovery secret.

## Threat boundaries

The feature protects at-rest copied `tdata` files and offline profile exfiltration. It does not claim to protect an already unlocked account from malware executing under the same user, memory scraping, input capture, remote-control abuse, or process injection. The settings UI states this boundary and links users to Telegram's active-session controls for revoking other sessions.

## Testing and release gates

- Unit-test storage-key wrapping, vault-unavailable behavior, migration, and rollback.
- Platform-test Keychain, DPAPI, LocalAuthentication, and Windows Hello adapters with fakes.
- Test helper health-check, direct fallback, disabled mode, and diagnostic status mapping.
- Verify legacy AyuGram profile discovery and unchanged bundle/profile identifiers.
- Run static checks only in this worktree; do not build the project during implementation.

