/*
This file is the source code of AyuGram for Desktop.

For license and copyright information please follow this link:
https://github.com/AyuGram/AyuGramDesktop/blob/dev/LICENSE
*/
#include "ayu/reworked/connectivity/connectivity_controller.h"

#include <utility>

namespace Reworked::Connectivity {
namespace {

constexpr auto kHealthTimeout = crl::time(5000);

class UnavailableHelper final : public Helper {
public:
	void start(HealthCallback callback) override {
		auto result = HealthResult();
		result.phase = FailurePhase::HelperUnavailable;
		callback(std::move(result));
	}

	void stop() override {
	}
};

} // namespace

Controller::Controller(
		std::unique_ptr<Helper> helper,
		ApplyProxy applyProxy,
		ClearProxy clearProxy,
		IntentChanged intentChanged,
		FailureChanged failureChanged)
: _helper(std::move(helper))
, _applyProxy(std::move(applyProxy))
, _clearProxy(std::move(clearProxy))
, _intentChanged(std::move(intentChanged))
, _failureChanged(std::move(failureChanged))
, _healthTimeout([=] { fail(FailurePhase::Timeout); }) {
}

Controller::~Controller() {
	++_request;
	_healthTimeout.cancel();
	if (_helper) {
		_helper->stop();
	}
}

void Controller::start(bool enabled) {
	_enabled = enabled;
	if (!_enabled) {
		_state = State::Disabled;
		return;
	}
	_state = State::Starting;
	_failurePhase = FailurePhase::None;
	_failureChanged(_failurePhase);
	const auto request = ++_request;
	_healthTimeout.callOnce(kHealthTimeout);
	if (_helper) {
		_helper->start([=](HealthResult result) {
			healthDone(request, std::move(result));
		});
	} else {
		fail(FailurePhase::HelperUnavailable);
	}
}

void Controller::setEnabled(bool enabled) {
	if (_enabled == enabled && _state != State::Disabled) {
		return;
	}
	_enabled = enabled;
	_intentChanged(_enabled);
	if (_enabled) {
		start(true);
		return;
	}
	++_request;
	_healthTimeout.cancel();
	if (_helper) {
		_helper->stop();
	}
	if (_state == State::Ready) {
		_state = State::FallingBack;
		_clearProxy();
	}
	_state = State::Disabled;
}

State Controller::state() const {
	return _state;
}

FailurePhase Controller::failurePhase() const {
	return _failurePhase;
}

void Controller::healthDone(int request, HealthResult result) {
	if (request != _request || _state != State::Starting) {
		return;
	}
	_healthTimeout.cancel();
	if (result.phase != FailurePhase::None) {
		fail(result.phase);
		return;
	}
	const auto proxy = loopbackProxy(result);
	if (!proxy.valid()) {
		fail(FailurePhase::InvalidEndpoint);
		return;
	}
	_state = State::Ready;
	_failurePhase = FailurePhase::None;
	_failureChanged(_failurePhase);
	_applyProxy(proxy);
}

void Controller::fail(FailurePhase phase) {
	if (_state != State::Starting && _state != State::Ready) {
		return;
	}
	++_request;
	_healthTimeout.cancel();
	if (_helper) {
		_helper->stop();
	}
	_state = State::FallingBack;
	_clearProxy();
	_failurePhase = phase;
	_failureChanged(_failurePhase);
	_state = State::Failed;
}

MTP::ProxyData Controller::loopbackProxy(const HealthResult &result) const {
	auto proxy = MTP::ProxyData();
	proxy.type = MTP::ProxyData::Type::Mtproto;
	proxy.host = u"127.0.0.1"_q;
	proxy.port = result.port;
	proxy.password = result.secret;
	return proxy;
}

std::unique_ptr<Helper> CreateUnavailableHelper() {
	return std::make_unique<UnavailableHelper>();
}

} // namespace Reworked::Connectivity
