/*
This file is the source code of AyuGram for Desktop.

For license and copyright information please follow this link:
https://github.com/AyuGram/AyuGramDesktop/blob/dev/LICENSE
*/
#pragma once

#include "ayu/reworked/session_protection/session_protection_contract.h"

#include <memory>

namespace Reworked::SessionProtection {

enum class VaultResult : uchar {
	Success,
	Unavailable,
	Denied,
	Corrupt,
};

class Vault {
public:
	virtual ~Vault() = default;

	[[nodiscard]] virtual VaultResult read(
		const CompatibilityIdentity &identity,
		QByteArray *secret) = 0;
	[[nodiscard]] virtual VaultResult write(
		const CompatibilityIdentity &identity,
		const QByteArray &secret) = 0;
	[[nodiscard]] virtual VaultResult remove(
		const CompatibilityIdentity &identity) = 0;
	[[nodiscard]] virtual VaultResult authenticateUser(
		const CompatibilityIdentity &identity) = 0;
	[[nodiscard]] virtual bool canAuthenticateUser() const = 0;
};

enum class EnvelopeVersion : uchar {
	None,
	V1,
	Unsupported,
};

[[nodiscard]] QByteArray EnvelopeHeader(EnvelopeVersion version);
[[nodiscard]] EnvelopeVersion ParseEnvelopeHeader(const QByteArray &header);

void SetVault(std::shared_ptr<Vault> vault);
[[nodiscard]] VaultResult ReadVaultSecret(QByteArray *secret);
[[nodiscard]] VaultResult WriteVaultSecret(const QByteArray &secret);
[[nodiscard]] VaultResult RemoveVaultSecret();
[[nodiscard]] VaultResult AuthenticateVaultUser();
[[nodiscard]] bool CanAuthenticateVaultUser();

} // namespace Reworked::SessionProtection
