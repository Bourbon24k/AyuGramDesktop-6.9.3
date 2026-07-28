/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/session_protection_box.h"

#include "core/application.h"
#include "lang/lang_keys.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "storage/storage_domain.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/box_content.h"
#include "ui/widgets/fields/password_input.h"
#include "window/window_session_controller.h"

#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

[[nodiscard]] QString SessionProtectionError(
		Storage::SessionProtectionResult result) {
	switch (result) {
	case Storage::SessionProtectionResult::Unavailable:
		return tr::lng_session_protection_unavailable(tr::now);
	case Storage::SessionProtectionResult::Denied:
		return tr::lng_session_protection_denied(tr::now);
	case Storage::SessionProtectionResult::ProtectionMustBeDisabled:
		return tr::lng_session_protection_disable_first(tr::now);
	default:
		return tr::lng_session_protection_corrupt(tr::now);
	}
}

class SessionProtectionPasscodeBox final : public Ui::BoxContent {
public:
	SessionProtectionPasscodeBox(
		QWidget*,
		not_null<Window::SessionController*> controller,
		bool enabling,
		Fn<void()> done)
	: _controller(controller)
	, _enabling(enabling)
	, _done(std::move(done))
	, _passcode(this, st::settingLocalPasscodeInputField, tr::lng_passcode_enter_old()) {
	}

protected:
	void prepare() override {
		setTitle(tr::lng_session_protection_title());

		_passcode.resize(
			st::boxWidth - st::boxPadding.left() - st::boxPadding.right(),
			_passcode.height());
		_passcode.moveToLeft(st::boxPadding.left(), st::boxPadding.top());
		connect(&_passcode, &Ui::MaskedInputField::submitted, [=] { submit(); });
		addButton(
			_enabling
				? tr::lng_session_protection_enable()
				: tr::lng_session_protection_disable(),
			[=] { submit(); });
		addButton(tr::lng_cancel(), [=] { closeBox(); });
		setDimensions(
			st::boxWidth,
			st::boxPadding.top()
				+ _passcode.height()
				+ st::boxPadding.bottom());
	}

	void setInnerFocus() override {
		_passcode.setFocusFast();
	}

private:
	void submit() {
		const auto passcode = _passcode.text();
		if (passcode.isEmpty()) {
			_passcode.showError();
			return;
		}
		if (!passcodeCanTry()) {
			_passcode.setFocus();
			_passcode.showError();
			return;
		}
		const auto result = _enabling
			? _controller->session().domain().local().enableSessionProtection(
				passcode.toUtf8())
			: _controller->session().domain().local().disableSessionProtection(
				passcode.toUtf8());
		if (result == Storage::SessionProtectionResult::Success) {
			cSetPasscodeBadTries(0);
			_done();
			closeBox();
			return;
		}
		_passcode.showError();
		if (result == Storage::SessionProtectionResult::IncorrectPasscode) {
			cSetPasscodeBadTries(cPasscodeBadTries() + 1);
			cSetPasscodeLastTry(crl::now());
			_passcode.selectAll();
			_passcode.setFocus();
			return;
		}
		_controller->show(Ui::MakeInformBox(SessionProtectionError(result)));
	}

	const not_null<Window::SessionController*> _controller;
	const bool _enabling;
	const Fn<void()> _done;
	Ui::PasswordInput _passcode;
};

} // namespace

void ShowSessionProtectionPasscodeBox(
		not_null<Window::SessionController*> controller,
		bool enabling,
		Fn<void()> done) {
	controller->show(Box<SessionProtectionPasscodeBox>(
		controller,
		enabling,
		std::move(done)));
}

} // namespace Settings
