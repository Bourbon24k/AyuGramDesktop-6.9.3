/*
This file is the source code of AyuGram for Desktop.

For license and copyright information please follow this link:
https://github.com/AyuGram/AyuGramDesktop/blob/dev/LICENSE
*/
#include "ayu/reworked/session_protection/session_protection_platform_impl.h"

#ifdef Q_OS_WIN

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QSaveFile>

#include <windows.h>
#include <wincrypt.h>
#include <winrt/Windows.Security.Credentials.UI.h>

#include <utility>

namespace Reworked::SessionProtection {
namespace {

constexpr auto kEntropy = "AyuGram.SessionProtection.Windows.DPAPI.v1";

[[nodiscard]] QByteArray Entropy(const CompatibilityIdentity &identity) {
	return QByteArray(kEntropy)
		+ identity.applicationIdentifier.toUtf8();
}

[[nodiscard]] QString VaultPath(const CompatibilityIdentity &identity) {
	return identity.profileDirectory
		+ u"session_protection_vault_dpapi"_q;
}

[[nodiscard]] DATA_BLOB Blob(const QByteArray &bytes) {
	return {
		.cbData = DWORD(bytes.size()),
		.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(bytes.constData())),
	};
}

class WindowsVault final : public Vault {
public:
	[[nodiscard]] VaultResult read(
			const CompatibilityIdentity &identity,
			QByteArray *secret) override {
		const auto path = VaultPath(identity);
		auto source = QFile(path);
		if (!source.open(QIODevice::ReadOnly)) {
			return VaultResult::Corrupt;
		}
		const auto encrypted = source.readAll();
		if (encrypted.isEmpty()) {
			return VaultResult::Corrupt;
		}
		const auto entropy = Entropy(identity);
		auto input = Blob(encrypted);
		auto optionalEntropy = Blob(entropy);
		DATA_BLOB output = {};
		if (!CryptUnprotectData(
				&input,
				nullptr,
				&optionalEntropy,
				nullptr,
				nullptr,
				CRYPTPROTECT_UI_FORBIDDEN,
				&output)) {
			return VaultResult::Corrupt;
		}
		*secret = QByteArray(
			reinterpret_cast<const char*>(output.pbData),
			int(output.cbData));
		LocalFree(output.pbData);
		return VaultResult::Success;
	}

	[[nodiscard]] VaultResult write(
			const CompatibilityIdentity &identity,
			const QByteArray &secret) override {
		if (secret.isEmpty()) {
			return VaultResult::Corrupt;
		}
		const auto entropy = Entropy(identity);
		auto input = Blob(secret);
		auto optionalEntropy = Blob(entropy);
		DATA_BLOB output = {};
		if (!CryptProtectData(
				&input,
				nullptr,
				&optionalEntropy,
				nullptr,
				nullptr,
				CRYPTPROTECT_UI_FORBIDDEN,
				&output)) {
			return VaultResult::Unavailable;
		}
		const auto path = VaultPath(identity);
		const auto directory = QFileInfo(path).dir();
		if (!directory.exists() && !QDir().mkpath(directory.path())) {
			LocalFree(output.pbData);
			return VaultResult::Unavailable;
		}
		auto destination = QSaveFile(path);
		const auto written = destination.open(QIODevice::WriteOnly)
			&& (destination.write(
				reinterpret_cast<const char*>(output.pbData),
				output.cbData) == output.cbData)
			&& destination.commit();
		LocalFree(output.pbData);
		return written ? VaultResult::Success : VaultResult::Unavailable;
	}

	[[nodiscard]] VaultResult remove(
			const CompatibilityIdentity &identity) override {
		const auto path = VaultPath(identity);
		return (!QFile::exists(path) || QFile::remove(path))
			? VaultResult::Success
			: VaultResult::Unavailable;
	}

	[[nodiscard]] VaultResult authenticateUser(
			const CompatibilityIdentity &) override {
		using namespace winrt::Windows::Security::Credentials::UI;
		try {
			if (UserConsentVerifier::CheckAvailabilityAsync().get()
				!= UserConsentVerifierAvailability::Available) {
				return VaultResult::Unavailable;
			}
			return (UserConsentVerifier::RequestVerificationAsync(
				L"Authenticate to access protected local data").get()
				== UserConsentVerificationResult::Verified)
				? VaultResult::Success
				: VaultResult::Denied;
		} catch (const winrt::hresult_error &) {
			return VaultResult::Unavailable;
		}
	}

	[[nodiscard]] bool canAuthenticateUser() const override {
		using namespace winrt::Windows::Security::Credentials::UI;
		try {
			return UserConsentVerifier::CheckAvailabilityAsync().get()
				== UserConsentVerifierAvailability::Available;
		} catch (const winrt::hresult_error &) {
			return false;
		}
	}
};

} // namespace

std::shared_ptr<Vault> CreateWindowsVault() {
	return std::make_shared<WindowsVault>();
}

} // namespace Reworked::SessionProtection

#endif
