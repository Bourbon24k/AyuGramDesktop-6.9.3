/*
This file is the source code of AyuGram for Desktop.

For license and copyright information please follow this link:
https://github.com/AyuGram/AyuGramDesktop/blob/dev/LICENSE
*/
#pragma once

#include <QString>

namespace Reworked::SessionProtection {

struct CompatibilityIdentity {
	QString profileDirectory;
	QString applicationIdentifier;
};

[[nodiscard]] CompatibilityIdentity CurrentCompatibilityIdentity();

} // namespace Reworked::SessionProtection
