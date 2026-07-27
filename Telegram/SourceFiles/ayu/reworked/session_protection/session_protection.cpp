/*
This file is the source code of AyuGram for Desktop.

For license and copyright information please follow this link:
https://github.com/AyuGram/AyuGramDesktop/blob/dev/LICENSE
*/
#include "ayu/reworked/session_protection/session_protection.h"

namespace Reworked::SessionProtection {
namespace {

constexpr auto kHeaderPrefix = "AyuGram.SessionProtection";
constexpr auto kHeaderPrefixSize = sizeof(kHeaderPrefix) - 1;
constexpr auto kHeaderVersion1 = "AyuGram.SessionProtection\x01";

Vault *VaultInstance = nullptr;

} // namespace

QByteArray EnvelopeHeader(EnvelopeVersion version) {
	return (version == EnvelopeVersion::V1)
		? QByteArray(kHeaderVersion1)
		: QByteArray();
}

EnvelopeVersion ParseEnvelopeHeader(const QByteArray &header) {
	if (header == kHeaderVersion1) {
		return EnvelopeVersion::V1;
	} else if ((header.size() == int(kHeaderPrefixSize) + 1)
		&& header.startsWith(kHeaderPrefix)) {
		return EnvelopeVersion::Unsupported;
	}
	return EnvelopeVersion::None;
}

void SetVault(Vault *vault) {
	VaultInstance = vault;
}

VaultResult ReadVaultSecret(QByteArray *secret) {
	if (!VaultInstance) {
		return VaultResult::Unavailable;
	}
	return VaultInstance->read(CurrentCompatibilityIdentity(), secret);
}

VaultResult WriteVaultSecret(const QByteArray &secret) {
	if (!VaultInstance) {
		return VaultResult::Unavailable;
	}
	return VaultInstance->write(CurrentCompatibilityIdentity(), secret);
}

} // namespace Reworked::SessionProtection
