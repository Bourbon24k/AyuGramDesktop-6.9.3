/*
This file is the source code of AyuGram for Desktop.

For license and copyright information please follow this link:
https://github.com/AyuGram/AyuGramDesktop/blob/dev/LICENSE
*/
#pragma once

#include "ayu/reworked/session_protection/session_protection_contract.h"

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
};

enum class EnvelopeVersion : uchar {
	None,
	V1,
	Unsupported,
};

[[nodiscard]] QByteArray EnvelopeHeader(EnvelopeVersion version);
[[nodiscard]] EnvelopeVersion ParseEnvelopeHeader(const QByteArray &header);

void SetVault(Vault *vault);
[[nodiscard]] VaultResult ReadVaultSecret(QByteArray *secret);
[[nodiscard]] VaultResult WriteVaultSecret(const QByteArray &secret);

} // namespace Reworked::SessionProtection
