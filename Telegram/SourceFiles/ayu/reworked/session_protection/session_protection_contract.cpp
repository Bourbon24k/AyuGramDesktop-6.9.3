/*
This file is the source code of AyuGram for Desktop.

For license and copyright information please follow this link:
https://github.com/AyuGram/AyuGramDesktop/blob/dev/LICENSE
*/
#include "ayu/reworked/session_protection/session_protection_contract.h"

#include "platform/platform_specific.h"
#include "storage/storage_domain.h"

namespace Reworked::SessionProtection {

CompatibilityIdentity CurrentCompatibilityIdentity() {
	return {
		.profileDirectory = Storage::GlobalDataPath(),
		.applicationIdentifier = Platform::PersistentApplicationIdentifier(),
	};
}

} // namespace Reworked::SessionProtection
