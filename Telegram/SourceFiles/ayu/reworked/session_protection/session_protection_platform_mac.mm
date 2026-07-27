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

#include <dispatch/dispatch.h>

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

	[[nodiscard]] VaultResult authenticateUser(
			const CompatibilityIdentity &) override {
		@autoreleasepool {
			const auto context = [[LAContext alloc] init];
			NSError *error = nil;
			if (![context canEvaluatePolicy:LAPolicyDeviceOwnerAuthentication
				error:&error]) {
				[context release];
				return VaultResult::Unavailable;
			}
			__block BOOL authenticated = NO;
			const auto complete = dispatch_semaphore_create(0);
			[context evaluatePolicy:LAPolicyDeviceOwnerAuthentication
			localizedReason:@"Authenticate to access protected local data"
			reply:^(BOOL success, NSError *) {
			authenticated = success;
			dispatch_semaphore_signal(complete);
			}];
			dispatch_semaphore_wait(complete, DISPATCH_TIME_FOREVER);
			[context release];
			if (authenticated) {
				return VaultResult::Success;
			}
			return VaultResult::Denied;
		}
	}

	[[nodiscard]] bool canAuthenticateUser() const override {
		@autoreleasepool {
			const auto context = [[LAContext alloc] init];
			const auto result = [context
				canEvaluatePolicy:LAPolicyDeviceOwnerAuthentication
				error:nil];
			[context release];
			return result;
		}
	}
};

} // namespace

std::shared_ptr<Vault> CreateMacVault() {
	return std::make_shared<MacVault>();
}

} // namespace Reworked::SessionProtection

#endif
