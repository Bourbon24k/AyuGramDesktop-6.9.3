/*
This file is the source code of AyuGram for Desktop.

For license and copyright information please follow this link:
https://github.com/AyuGram/AyuGramDesktop/blob/dev/LICENSE
*/
#include "ayu/reworked/session_protection/session_protection_platform_impl.h"

#include "base/platform/win/base_windows_winrt.h"

#ifdef Q_OS_WIN

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QSaveFile>
#include <QtWidgets/QWidget>

#include <windows.h>
#include <wincrypt.h>
#include <winrt/Windows.Security.Credentials.UI.h>
#include <UserConsentVerifierInterop.h>

#include <string>
#include <utility>

namespace Reworked::SessionProtection {
namespace {

constexpr auto kEntropy = "AyuGram.SessionProtection.Windows.DPAPI.v1";

[[nodiscard]] QByteArray Entropy(const CompatibilityIdentity &identity) {
	return QByteArray(kEntropy)
		+ identity.applicationIdentifier.toUtf8()
		+ '\0'
		+ identity.profileDirectory.toUtf8();
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

void Complete(VaultAuthenticationCallback callback, VaultResult result) {
	crl::on_main([=] {
		callback(result);
	});
}

void Complete(
		VaultAuthenticationAvailabilityCallback callback,
		bool available) {
	crl::on_main([=] {
		callback(available);
	});
}

[[nodiscard]] VaultResult AuthenticationResult(
		winrt::Windows::Security::Credentials::UI::UserConsentVerificationResult
			result) {
	using namespace winrt::Windows::Security::Credentials::UI;
	switch (result) {
	case UserConsentVerificationResult::Verified:
		return VaultResult::Success;
	case UserConsentVerificationResult::DeviceBusy:
	case UserConsentVerificationResult::RetriesExhausted:
	case UserConsentVerificationResult::Canceled:
		return VaultResult::Denied;
	case UserConsentVerificationResult::DeviceNotPresent:
	case UserConsentVerificationResult::NotConfiguredForUser:
	case UserConsentVerificationResult::DisabledByPolicy:
		return VaultResult::Unavailable;
	default:
		return VaultResult::Corrupt;
	}
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
		SecureZeroMemory(output.pbData, output.cbData);
		LocalFree(output.pbData);
		return secret->isEmpty() ? VaultResult::Corrupt : VaultResult::Success;
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

	void authenticateUser(
			QWidget *parent,
			VaultAuthenticationCallback callback) override {
		using namespace winrt::Windows::Security::Credentials::UI;
		if (!parent || !base::WinRT::Supported()) {
			Complete(std::move(callback), VaultResult::Unavailable);
			return;
		}
		const auto window = parent->window();
		window->createWinId();
		const auto handle = reinterpret_cast<HWND>(window->winId());
		if (!handle) {
			Complete(std::move(callback), VaultResult::Unavailable);
			return;
		}
		const auto started = base::WinRT::Try([&] {
			UserConsentVerifier::CheckAvailabilityAsync().Completed([=](
					winrt::Windows::Foundation::IAsyncOperation<
						UserConsentVerifierAvailability> operation,
					winrt::Windows::Foundation::AsyncStatus status) {
				const auto availability = base::WinRT::Try([&] {
					return operation.GetResults();
				});
				if (status != winrt::Windows::Foundation::AsyncStatus::Completed
					|| !availability
					|| *availability != UserConsentVerifierAvailability::Available) {
					Complete(callback, VaultResult::Unavailable);
					return;
				}
				const auto requested = base::WinRT::Try([&] {
					const auto interop = winrt::get_activation_factory<
						UserConsentVerifier,
						IUserConsentVerifierInterop>();
					if (!interop) {
						return false;
					}
					const auto text = winrt::to_hstring(std::string(
						"Authenticate to access protected local data"));
					winrt::capture<winrt::Windows::Foundation::IAsyncOperation<
						UserConsentVerificationResult>>(
						interop,
						&IUserConsentVerifierInterop::RequestVerificationForWindowAsync,
						handle,
						reinterpret_cast<HSTRING>(winrt::get_abi(text))
					).Completed([=](
							winrt::Windows::Foundation::IAsyncOperation<
								UserConsentVerificationResult> operation,
							winrt::Windows::Foundation::AsyncStatus status) {
						const auto result = base::WinRT::Try([&] {
							return operation.GetResults();
						});
						Complete(
							callback,
							(status == winrt::Windows::Foundation::AsyncStatus::Canceled)
								? VaultResult::Denied
								: (status != winrt::Windows::Foundation::AsyncStatus::Completed
									|| !result)
								? VaultResult::Corrupt
								: AuthenticationResult(*result));
					});
					return true;
				});
				if (!requested || !*requested) {
					Complete(callback, VaultResult::Unavailable);
				}
			});
		});
		if (!started) {
			Complete(std::move(callback), VaultResult::Unavailable);
		}
	}

	void canAuthenticateUser(
			VaultAuthenticationAvailabilityCallback callback) const override {
		using namespace winrt::Windows::Security::Credentials::UI;
		if (!base::WinRT::Supported()) {
			Complete(std::move(callback), false);
			return;
		}
		const auto started = base::WinRT::Try([&] {
			UserConsentVerifier::CheckAvailabilityAsync().Completed([=](
					winrt::Windows::Foundation::IAsyncOperation<
						UserConsentVerifierAvailability> operation,
					winrt::Windows::Foundation::AsyncStatus status) {
				const auto availability = base::WinRT::Try([&] {
					return operation.GetResults();
				});
				Complete(
					callback,
					status == winrt::Windows::Foundation::AsyncStatus::Completed
						&& availability
						&& (*availability == UserConsentVerifierAvailability::Available));
			});
		});
		if (!started) {
			Complete(std::move(callback), false);
		}
	}

};

} // namespace

std::shared_ptr<Vault> CreateWindowsVault() {
	return std::make_shared<WindowsVault>();
}

} // namespace Reworked::SessionProtection

#endif
