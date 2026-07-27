/*
This file is the source code of AyuGram for Desktop.

For license and copyright information please follow this link:
https://github.com/AyuGram/AyuGramDesktop/blob/dev/LICENSE
*/
#include "ayu/reworked/session_protection/session_protection_platform.h"

#include "ayu/reworked/session_protection/session_protection_platform_impl.h"

namespace Reworked::SessionProtection {

std::shared_ptr<Vault> CreatePlatformVault() {
#if defined Q_OS_MAC
	return CreateMacVault();
#elif defined Q_OS_WIN
	return CreateWindowsVault();
#else
	return nullptr;
#endif
}

} // namespace Reworked::SessionProtection
