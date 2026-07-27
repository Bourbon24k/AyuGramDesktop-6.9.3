/*
This file is the source code of AyuGram for Desktop.

For license and copyright information please follow this link:
https://github.com/AyuGram/AyuGramDesktop/blob/dev/LICENSE
*/
#pragma once

#include "ayu/reworked/session_protection/session_protection.h"

#include <memory>

namespace Reworked::SessionProtection {

[[nodiscard]] std::shared_ptr<Vault> CreatePlatformVault();

} // namespace Reworked::SessionProtection
