/*
This file is the source code of AyuGram for Desktop.

For license and copyright information please follow this link:
https://github.com/AyuGram/AyuGramDesktop/blob/dev/LICENSE
*/
#pragma once

#include <QString>

namespace Reworked::SessionProtection {

enum class ApplicationIdentityPolicy {
	Preserve,
};

struct CompatibilityIdentity {
	QString profileDirectory;
	ApplicationIdentityPolicy applicationIdentityPolicy
		= ApplicationIdentityPolicy::Preserve;
};

[[nodiscard]] CompatibilityIdentity CurrentCompatibilityIdentity();

} // namespace Reworked::SessionProtection
