/*
This file is the source code of AyuGram for Desktop.

For license and copyright information please follow this link:
https://github.com/AyuGram/AyuGramDesktop/blob/dev/LICENSE
*/
#include "ayu/reworked/session_protection/session_protection_platform_impl.h"

#ifdef Q_OS_MAC

#include <QtCore/QCryptographicHash>

#import <LocalAuthentication/LocalAuthentication.h>
#import <Security/Security.h>

#include <memory>
#include <utility>

namespace Reworked::SessionProtection {
namespace {

constexpr auto kService = "AyuGram.SessionProtection.Keychain.v1";

[[nodiscard]] QString Account(const CompatibilityIdentity &identity) {
	const auto scope = identity.applicationIdentifier.toUtf8()
		+ '\0'
		+ identity.profileDirectory.toUtf8();
	return u"vault-"_q
		+ QString::fromLatin1(QCryptographicHash::hash(
			scope,
			QCryptographicHash::Sha256).toHex());
}

[[nodiscard]] VaultResult Result(OSStatus status) {
	if (status == errSecSuccess) {
		return VaultResult::Success;
	} else if (status == errSecAuthFailed || status == errSecUserCanceled) {
		return VaultResult::Denied;
	} else if (status == errSecDecode) {
		return VaultResult::Corrupt;
	}
	return VaultResult::Unavailable;
}

[[nodiscard]] NSDictionary *Query(const CompatibilityIdentity &identity) {
	return @{
		(id)kSecClass: (id)kSecClassGenericPassword,
		(id)kSecAttrService: [NSString stringWithUTF8String:kService],
		(id)kSecAttrAccount: Account(identity).toNSString(),
	};
}

void Complete(
		VaultCallbackContext context,
		VaultAuthenticationCallback callback,
		VaultResult result) {
	crl::on_main([=] {
		if (context) {
			callback(result);
		}
	});
}

void Complete(
		std::shared_ptr<LAContext> context,
		VaultCallbackContext callbackContext,
		VaultAuthenticationCallback callback,
		VaultResult result) {
	crl::on_main([=] {
		if (callbackContext) {
			callback(result);
		}
		static_cast<void>(context);
	});
}

void Complete(
		VaultCallbackContext context,
		VaultAuthenticationAvailabilityCallback callback,
		bool available) {
	crl::on_main([=] {
		if (context) {
			callback(available);
		}
	});
}

[[nodiscard]] VaultResult AuthenticationResult(NSError *error) {
	const auto code = error.code;
	if (code == LAErrorUserCancel
		|| code == LAErrorSystemCancel
		|| code == LAErrorAppCancel
		|| code == LAErrorUserFallback
		|| code == LAErrorAuthenticationFailed) {
		return VaultResult::Denied;
	} else if (code == LAErrorBiometryNotAvailable
		|| code == LAErrorBiometryNotEnrolled
		|| code == LAErrorPasscodeNotSet) {
		return VaultResult::Unavailable;
	}
	return VaultResult::Corrupt;
}

class MacVault final : public Vault {
public:
	[[nodiscard]] VaultResult read(
			const CompatibilityIdentity &identity,
			QByteArray *secret) override {
		@autoreleasepool {
			auto query = [Query(identity) mutableCopy];
			query[(id)kSecReturnData] = @YES;
			query[(id)kSecMatchLimit] = (id)kSecMatchLimitOne;
			CFTypeRef result = nullptr;
			const auto status = SecItemCopyMatching(
				(CFDictionaryRef)query,
				&result);
			[query release];
			if (status == errSecItemNotFound) {
				return VaultResult::Corrupt;
			} else if (status != errSecSuccess) {
				return Result(status);
			}
			const auto data = static_cast<CFDataRef>(result);
			*secret = QByteArray(
				reinterpret_cast<const char*>(CFDataGetBytePtr(data)),
				int(CFDataGetLength(data)));
			CFRelease(result);
			return secret->isEmpty() ? VaultResult::Corrupt : VaultResult::Success;
		}
	}

	[[nodiscard]] VaultResult write(
			const CompatibilityIdentity &identity,
			const QByteArray &secret) override {
		if (secret.isEmpty()) {
			return VaultResult::Corrupt;
		}
		@autoreleasepool {
			const auto data = [NSData dataWithBytes:secret.constData()
				length:secret.size()];
			auto query = Query(identity);
			const auto updates = @{ (id)kSecValueData: data };
			auto status = SecItemUpdate(
				(CFDictionaryRef)query,
				(CFDictionaryRef)updates);
			if (status == errSecItemNotFound) {
				auto attributes = [query mutableCopy];
				attributes[(id)kSecValueData] = data;
				status = SecItemAdd((CFDictionaryRef)attributes, nullptr);
				[attributes release];
			}
			return Result(status);
		}
	}

	[[nodiscard]] VaultResult remove(
			const CompatibilityIdentity &identity) override {
		@autoreleasepool {
			const auto status = SecItemDelete((CFDictionaryRef)Query(identity));
			return (status == errSecItemNotFound)
				? VaultResult::Success
				: Result(status);
		}
	}

	void authenticateUser(
			QPointer<QWidget>,
			VaultCallbackContext callbackContext,
			VaultAuthenticationCallback callback) override {
		@autoreleasepool {
			const auto context = std::shared_ptr<LAContext>(
				[[LAContext alloc] init],
				[](LAContext *value) {
					[value release];
				});
			NSError *error = nil;
			if (![context.get()
				canEvaluatePolicy:LAPolicyDeviceOwnerAuthentication
				error:&error]) {
				Complete(
					std::move(callbackContext),
					std::move(callback),
					VaultResult::Unavailable);
				return;
			}
			[context.get() evaluatePolicy:LAPolicyDeviceOwnerAuthentication
			localizedReason:@"Authenticate to access protected local data"
			reply:^(BOOL success, NSError *replyError) {
				Complete(
					context,
					callbackContext,
					callback,
					success
						? VaultResult::Success
						: AuthenticationResult(replyError));
			}];
		}
	}

	void canAuthenticateUser(
			VaultCallbackContext callbackContext,
			VaultAuthenticationAvailabilityCallback callback) const override {
		@autoreleasepool {
			const auto context = [[LAContext alloc] init];
			const auto result = [context
				canEvaluatePolicy:LAPolicyDeviceOwnerAuthentication
				error:nil];
			[context release];
			Complete(std::move(callbackContext), std::move(callback), result);
		}
	}

};

} // namespace

std::shared_ptr<Vault> CreateMacVault() {
	return std::make_shared<MacVault>();
}

} // namespace Reworked::SessionProtection

#endif
