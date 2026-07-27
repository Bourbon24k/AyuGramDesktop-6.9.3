/*
This file is the source code of AyuGram for Desktop.

For license and copyright information please follow this link:
https://github.com/AyuGram/AyuGramDesktop/blob/dev/LICENSE
*/
#include "ayu/reworked/session_protection/session_protection.h"

#include <mutex>
#include <utility>

namespace Reworked::SessionProtection {
namespace {

constexpr auto kHeaderPrefix = "AyuGram.SessionProtection";
constexpr auto kHeaderVersion1 = "AyuGram.SessionProtection\x01";

std::mutex VaultMutex;
std::shared_ptr<Vault> VaultInstance;

[[nodiscard]] std::shared_ptr<Vault> CurrentVault() {
	const auto lock = std::lock_guard(VaultMutex);
	return VaultInstance;
}

} // namespace

QByteArray EnvelopeHeader(EnvelopeVersion version) {
	return (version == EnvelopeVersion::V1)
		? QByteArray(kHeaderVersion1)
		: QByteArray();
}

EnvelopeVersion ParseEnvelopeHeader(const QByteArray &header) {
	if (header == kHeaderVersion1) {
		return EnvelopeVersion::V1;
	} else if (header.startsWith(kHeaderPrefix)) {
		return EnvelopeVersion::Unsupported;
	}
	return EnvelopeVersion::None;
}

void SetVault(std::shared_ptr<Vault> vault) {
	const auto lock = std::lock_guard(VaultMutex);
	VaultInstance = std::move(vault);
}

VaultResult ReadVaultSecret(QByteArray *secret) {
	const auto vault = CurrentVault();
	if (!vault) {
		return VaultResult::Unavailable;
	}
	return vault->read(CurrentCompatibilityIdentity(), secret);
}

VaultResult WriteVaultSecret(const QByteArray &secret) {
	const auto vault = CurrentVault();
	if (!vault) {
		return VaultResult::Unavailable;
	}
	return vault->write(CurrentCompatibilityIdentity(), secret);
}

} // namespace Reworked::SessionProtection
