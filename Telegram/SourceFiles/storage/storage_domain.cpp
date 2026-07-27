/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/storage_domain.h"

#include "ayu/reworked/session_protection/session_protection.h"
#include "core/version.h"
#include "storage/details/storage_file_utilities.h"
#include "storage/serialize_common.h"
#include "mtproto/mtproto_config.h"
#include "main/main_domain.h"
#include "main/main_account.h"
#include "base/random.h"

#include <QtCore/QCryptographicHash>

namespace Storage {

QString GlobalDataPath() {
	return cWorkingDir() + u"tdata/"_q;
}

namespace {

using namespace details;
namespace SessionProtection = Reworked::SessionProtection;

constexpr auto kVaultSecretSize = 32;

[[nodiscard]] QString ComputeKeyName(const QString &dataName) {
	// We dropped old test authorizations when migrated to multi auth.
	//return "key_" + dataName + (cTestMode() ? "[test]" : "");
	return "key_" + dataName;
}

[[nodiscard]] SessionProtectionResult VaultResultToSessionProtectionResult(
		SessionProtection::VaultResult result) {
	switch (result) {
	case SessionProtection::VaultResult::Success:
		return SessionProtectionResult::Success;
	case SessionProtection::VaultResult::Unavailable:
		return SessionProtectionResult::Unavailable;
	case SessionProtection::VaultResult::Denied:
		return SessionProtectionResult::Denied;
	case SessionProtection::VaultResult::Corrupt:
		return SessionProtectionResult::Corrupt;
	}
	Unexpected("Vault result.");
}

[[nodiscard]] MTP::AuthKeyPtr CreateSessionProtectionKey(
		const QByteArray &passcode,
		const QByteArray &vaultSecret,
		const QByteArray &salt) {
	auto material = QByteArray();
	auto stream = QDataStream(&material, QIODevice::WriteOnly);
	stream << passcode << vaultSecret;
	return CreateLocalKey(
		QCryptographicHash::hash(material, QCryptographicHash::Sha256),
		salt);
}

[[nodiscard]] bool VaultSecretMatchesLocalKey(
		const QByteArray &passcode,
		const QByteArray &vaultSecret,
		const QByteArray &salt,
		const QByteArray &encrypted,
		const MTP::AuthKeyPtr &localKey) {
	auto data = EncryptedDescriptor();
	if (!DecryptLocal(
				data,
				encrypted,
				CreateSessionProtectionKey(passcode, vaultSecret, salt))) {
		return false;
	}
	const auto key = Serialize::read<MTP::AuthKey::Data>(data.stream);
	return (data.stream.status() == QDataStream::Ok)
		&& data.stream.atEnd()
		&& MTP::AuthKey(key).equals(localKey);
}

} // namespace

Domain::Domain(not_null<Main::Domain*> owner, const QString &dataName)
: _owner(owner)
, _dataName(dataName) {
}

Domain::~Domain() = default;

StartResult Domain::start(const QByteArray &passcode) {
	const auto modern = startModern(passcode);
	if (modern == StartModernResult::Success) {
		if (_oldVersion < AppVersion) {
			writeAccounts();
		}
		return StartResult::Success;
	} else if (modern == StartModernResult::IncorrectPasscode) {
		return StartResult::IncorrectPasscode;
	} else if (modern == StartModernResult::SessionProtectionUnavailable) {
		return StartResult::SessionProtectionUnavailable;
	} else if (modern == StartModernResult::SessionProtectionDenied) {
		return StartResult::SessionProtectionDenied;
	} else if (modern == StartModernResult::SessionProtectionCorrupt) {
		return StartResult::SessionProtectionCorrupt;
	} else if (modern == StartModernResult::Failed) {
		startFromScratch();
		return StartResult::Success;
	}
	auto legacy = std::make_unique<Main::Account>(_owner, _dataName, 0);
	const auto result = legacy->legacyStart(passcode);
	if (result == StartResult::Success) {
		_oldVersion = legacy->local().oldMapVersion();
		startWithSingleAccount(passcode, std::move(legacy));
	}
	return result;
}

void Domain::startAdded(
		not_null<Main::Account*> account,
		std::unique_ptr<MTP::Config> config) {
	Expects(_localKey != nullptr);

	account->prepareToStartAdded(_localKey);
	account->start(std::move(config));
}

void Domain::startWithSingleAccount(
		const QByteArray &passcode,
		std::unique_ptr<Main::Account> account) {
	Expects(account != nullptr);

	if (auto localKey = account->local().peekLegacyLocalKey()) {
		_localKey = std::move(localKey);
		encryptLocalKey(passcode);
		account->start(nullptr);
	} else {
		generateLocalKey();
		account->start(account->prepareToStart(_localKey));
	}
	_owner->accountAddedInStorage(Main::Domain::AccountWithIndex{
		.account = std::move(account)
	});
	writeAccounts();
}

void Domain::generateLocalKey() {
	Expects(_localKey == nullptr);
	Expects(_passcodeKeySalt.isEmpty());
	Expects(_passcodeKeyEncrypted.isEmpty());

	auto pass = QByteArray(MTP::AuthKey::kSize, Qt::Uninitialized);
	auto salt = QByteArray(LocalEncryptSaltSize, Qt::Uninitialized);
	base::RandomFill(pass.data(), pass.size());
	base::RandomFill(salt.data(), salt.size());
	_localKey = CreateLocalKey(pass, salt);

	encryptLocalKey(QByteArray());
}

void Domain::encryptLocalKey(const QByteArray &passcode) {
	_passcodeKeySalt.resize(LocalEncryptSaltSize);
	base::RandomFill(_passcodeKeySalt.data(), _passcodeKeySalt.size());
	_passcodeKey = CreateLocalKey(passcode, _passcodeKeySalt);

	EncryptedDescriptor passKeyData(MTP::AuthKey::kSize);
	_localKey->write(passKeyData.stream);
	_passcodeKeyEncrypted = PrepareEncrypted(passKeyData, _passcodeKey);
	_localKeyEnvelope = LocalKeyEnvelope::Legacy;
	_hasLocalPasscode = !passcode.isEmpty();
}

Domain::StartModernResult Domain::startModern(
		const QByteArray &passcode) {
	const auto name = ComputeKeyName(_dataName);

	FileReadDescriptor keyData;
	if (!ReadFile(keyData, name, GlobalDataPath())) {
		return StartModernResult::Empty;
	}
	LOG(("App Info: reading accounts info..."));

	QByteArray headerOrSalt, salt, keyEncrypted, infoEncrypted;
	keyData.stream >> headerOrSalt;
	const auto envelope = SessionProtection::ParseEnvelopeHeader(headerOrSalt);
	if (envelope == SessionProtection::EnvelopeVersion::V1) {
		keyData.stream >> salt >> keyEncrypted >> infoEncrypted;
	} else {
		salt = headerOrSalt;
		keyData.stream >> keyEncrypted >> infoEncrypted;
	}
	if (!CheckStreamStatus(keyData.stream)) {
		return StartModernResult::Failed;
	}
	if (envelope == SessionProtection::EnvelopeVersion::Unsupported) {
		return StartModernResult::SessionProtectionCorrupt;
	}

	if (salt.size() != LocalEncryptSaltSize) {
		LOG(("App Error: bad salt in info file, size: %1").arg(salt.size()));
		return StartModernResult::Failed;
	}
	_passcodeKey = CreateLocalKey(passcode, salt);

	EncryptedDescriptor keyInnerData, info;
	auto wrappingKey = _passcodeKey;
	if (envelope == SessionProtection::EnvelopeVersion::V1) {
		auto vaultSecret = QByteArray();
		const auto vaultResult = SessionProtection::ReadVaultSecret(&vaultSecret);
		if (vaultResult != SessionProtection::VaultResult::Success) {
			switch (VaultResultToSessionProtectionResult(vaultResult)) {
			case SessionProtectionResult::Unavailable:
				return StartModernResult::SessionProtectionUnavailable;
			case SessionProtectionResult::Denied:
				return StartModernResult::SessionProtectionDenied;
			default:
				return StartModernResult::SessionProtectionCorrupt;
			}
		}
		if (vaultSecret.size() != kVaultSecretSize) {
			return StartModernResult::SessionProtectionCorrupt;
		}
		wrappingKey = CreateSessionProtectionKey(passcode, vaultSecret, salt);
	}
	if (!DecryptLocal(keyInnerData, keyEncrypted, wrappingKey)) {
		LOG(("App Info: could not decrypt pass-protected key from info file, "
			"maybe bad password..."));
		return StartModernResult::IncorrectPasscode;
	}
	auto key = Serialize::read<MTP::AuthKey::Data>(keyInnerData.stream);
	if (keyInnerData.stream.status() != QDataStream::Ok
		|| !keyInnerData.stream.atEnd()) {
		LOG(("App Error: could not read pass-protected key from info file"));
		return StartModernResult::Failed;
	}
	_localKey = std::make_shared<MTP::AuthKey>(key);

	_passcodeKeyEncrypted = keyEncrypted;
	_passcodeKeySalt = salt;
	_localKeyEnvelope = (envelope == SessionProtection::EnvelopeVersion::V1)
		? LocalKeyEnvelope::SessionProtectionV1
		: LocalKeyEnvelope::Legacy;
	_hasLocalPasscode = (_localKeyEnvelope
		== LocalKeyEnvelope::SessionProtectionV1) || !passcode.isEmpty();

	if (!DecryptLocal(info, infoEncrypted, _localKey)) {
		LOG(("App Error: could not decrypt info."));
		return StartModernResult::Failed;
	}
	LOG(("App Info: reading encrypted info..."));
	auto count = qint32();
	info.stream >> count;
	if (count <= 0 || count > Main::Domain::kPremiumMaxAccounts) {
		LOG(("App Error: bad accounts count: %1").arg(count));
		return StartModernResult::Failed;
	}

	_oldVersion = keyData.version;

	auto tried = base::flat_set<int>();
	auto sessions = base::flat_set<uint64>();
	auto active = 0;
	for (auto i = 0; i != count; ++i) {
		auto index = qint32();
		info.stream >> index;
		if (index >= 0
			&& index < Main::Domain::kPremiumMaxAccounts
			&& tried.emplace(index).second) {
			auto account = std::make_unique<Main::Account>(
				_owner,
				_dataName,
				index);
			auto config = account->prepareToStart(_localKey);
			const auto sessionId = account->willHaveSessionUniqueId(
				config.get());
			if (!sessions.contains(sessionId)
				&& (sessionId != 0 || (sessions.empty() && i + 1 == count))) {
				if (sessions.empty()) {
					active = index;
				}
				account->start(std::move(config));
				_owner->accountAddedInStorage({
					.index = index,
					.account = std::move(account)
				});
				sessions.emplace(sessionId);
			}
		}
	}
	if (sessions.empty()) {
		LOG(("App Error: no accounts read."));
		return StartModernResult::Failed;
	}

	if (!info.stream.atEnd()) {
		info.stream >> active;
	}
	_owner->activateFromStorage(active);

	Ensures(!sessions.empty());
	return StartModernResult::Success;
}

void Domain::writeAccounts() {
	Expects(!_owner->accounts().empty());

	const auto path = GlobalDataPath();
	if (!QDir().exists(path)) {
		QDir().mkpath(path);
	}

	FileWriteDescriptor key(ComputeKeyName(_dataName), path);
	if (_localKeyEnvelope == LocalKeyEnvelope::SessionProtectionV1) {
		key.writeData(SessionProtection::EnvelopeHeader(
			SessionProtection::EnvelopeVersion::V1));
	}
	key.writeData(_passcodeKeySalt);
	key.writeData(_passcodeKeyEncrypted);

	const auto &list = _owner->accounts();

	auto keySize = sizeof(qint32) + sizeof(qint32) * list.size();

	EncryptedDescriptor keyData(keySize);
	keyData.stream << qint32(list.size());
	for (const auto &[index, account] : list) {
		keyData.stream << qint32(index);
	}
	keyData.stream << qint32(_owner->activeForStorage());
	key.writeEncrypted(keyData, _localKey);
}

void Domain::startFromScratch() {
	startWithSingleAccount(
		QByteArray(),
		std::make_unique<Main::Account>(_owner, _dataName, 0));
}

bool Domain::checkPasscode(const QByteArray &passcode) const {
	Expects(!_passcodeKeySalt.isEmpty());
	Expects(_passcodeKey != nullptr);

	const auto checkKey = CreateLocalKey(passcode, _passcodeKeySalt);
	return checkKey->equals(_passcodeKey);
}

SessionProtectionResult Domain::setPasscode(const QByteArray &passcode) {
	Expects(!_passcodeKeySalt.isEmpty());
	Expects(_localKey != nullptr);

	if (_localKeyEnvelope == LocalKeyEnvelope::SessionProtectionV1) {
		auto vaultSecret = QByteArray();
		const auto vaultResult = SessionProtection::ReadVaultSecret(&vaultSecret);
		if (vaultResult != SessionProtection::VaultResult::Success) {
			return VaultResultToSessionProtectionResult(vaultResult);
		}
		const auto result = encryptSessionProtectedLocalKey(passcode, vaultSecret);
		if (result != SessionProtectionResult::Success) {
			return result;
		}
	} else {
		encryptLocalKey(passcode);
	}
	writeAccounts();

	_passcodeKeyChanged.fire({});
	return SessionProtectionResult::Success;
}

int Domain::oldVersion() const {
	return _oldVersion;
}

void Domain::clearOldVersion() {
	_oldVersion = 0;
}

rpl::producer<> Domain::localPasscodeChanged() const {
	return _passcodeKeyChanged.events();
}

bool Domain::hasLocalPasscode() const {
	return _hasLocalPasscode;
}

SessionProtectionResult Domain::encryptSessionProtectedLocalKey(
		const QByteArray &passcode,
		const QByteArray &vaultSecret) {
	if (vaultSecret.size() != kVaultSecretSize) {
		return SessionProtectionResult::Corrupt;
	}
	_passcodeKeySalt.resize(LocalEncryptSaltSize);
	base::RandomFill(_passcodeKeySalt.data(), _passcodeKeySalt.size());
	_passcodeKey = CreateLocalKey(passcode, _passcodeKeySalt);
	const auto wrappingKey = CreateSessionProtectionKey(
		passcode,
		vaultSecret,
		_passcodeKeySalt);
	EncryptedDescriptor passKeyData(MTP::AuthKey::kSize);
	_localKey->write(passKeyData.stream);
	_passcodeKeyEncrypted = PrepareEncrypted(passKeyData, wrappingKey);
	_localKeyEnvelope = LocalKeyEnvelope::SessionProtectionV1;
	_hasLocalPasscode = true;
	return SessionProtectionResult::Success;
}

SessionProtectionResult Domain::enableSessionProtection(
		const QByteArray &passcode) {
	if (_localKeyEnvelope == LocalKeyEnvelope::SessionProtectionV1) {
		return SessionProtectionResult::Success;
	}
	if (!_hasLocalPasscode || !checkPasscode(passcode)) {
		return SessionProtectionResult::IncorrectPasscode;
	}
	auto vaultSecret = QByteArray(kVaultSecretSize, Qt::Uninitialized);
	base::RandomFill(vaultSecret.data(), vaultSecret.size());
	const auto vaultResult = SessionProtection::WriteVaultSecret(vaultSecret);
	if (vaultResult != SessionProtection::VaultResult::Success) {
		return VaultResultToSessionProtectionResult(vaultResult);
	}
	const auto result = encryptSessionProtectedLocalKey(passcode, vaultSecret);
	if (result != SessionProtectionResult::Success) {
		return result;
	}
	writeAccounts();
	_passcodeKeyChanged.fire({});
	return SessionProtectionResult::Success;
}

SessionProtectionResult Domain::disableSessionProtection(
		const QByteArray &passcode) {
	if (_localKeyEnvelope != LocalKeyEnvelope::SessionProtectionV1) {
		return SessionProtectionResult::NotEnabled;
	}
	if (!checkPasscode(passcode)) {
		return SessionProtectionResult::IncorrectPasscode;
	}
	auto vaultSecret = QByteArray();
	const auto vaultResult = SessionProtection::ReadVaultSecret(&vaultSecret);
	if (vaultResult != SessionProtection::VaultResult::Success) {
		return VaultResultToSessionProtectionResult(vaultResult);
	}
	if (vaultSecret.size() != kVaultSecretSize) {
		return SessionProtectionResult::Corrupt;
	}
	if (!VaultSecretMatchesLocalKey(
				passcode,
				vaultSecret,
				_passcodeKeySalt,
				_passcodeKeyEncrypted,
				_localKey)) {
		return SessionProtectionResult::Corrupt;
	}
	encryptLocalKey(passcode);
	writeAccounts();
	_passcodeKeyChanged.fire({});
	return SessionProtectionResult::Success;
}

bool Domain::sessionProtectionEnabled() const {
	return _localKeyEnvelope == LocalKeyEnvelope::SessionProtectionV1;
}

} // namespace Storage
