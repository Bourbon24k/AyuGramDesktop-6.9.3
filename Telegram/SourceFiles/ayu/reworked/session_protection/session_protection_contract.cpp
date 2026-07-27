/*
This file is the source code of AyuGram for Desktop.

For license and copyright information please follow this link:
https://github.com/AyuGram/AyuGramDesktop/blob/dev/LICENSE
*/
#include "ayu/reworked/session_protection/session_protection_contract.h"

#include "core/application.h"

#include <QGuiApplication>

namespace Reworked::SessionProtection {

CompatibilityIdentity CurrentCompatibilityIdentity() {
	return {
		.profileDirectory = cWorkingDir() + u"tdata/"_q,
		.bundleIdentifier = QGuiApplication::desktopFileName().section(
			u"._"_q,
			0,
			0),
	};
}

} // namespace Reworked::SessionProtection
