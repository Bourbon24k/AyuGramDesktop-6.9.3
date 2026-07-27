/*
This file is the source code of AyuGram for Desktop.

For license and copyright information please follow this link:
https://github.com/AyuGram/AyuGramDesktop/blob/dev/LICENSE
*/
#pragma once

#include "base/functional.h"
#include "base/timer.h"
#include "mtproto/mtproto_proxy_data.h"

#include <memory>

namespace Reworked::Connectivity {

enum class State : uchar {
	Disabled,
	Starting,
	Ready,
	FallingBack,
	Failed,
};

enum class FailurePhase : uchar {
	None,
	HelperUnavailable,
	Launch,
	Health,
	Timeout,
	InvalidEndpoint,
};

struct HealthResult {
	FailurePhase phase = FailurePhase::HelperUnavailable;
	uint32 port = 0;
	QString secret;
};

class Helper {
public:
	using HealthCallback = Fn<void(HealthResult)>;

	virtual ~Helper() = default;
	virtual void start(HealthCallback callback) = 0;
	virtual void stop() = 0;

};

using ApplyProxy = Fn<void(const MTP::ProxyData &proxy)>;
using ClearProxy = Fn<void()>;
using IntentChanged = Fn<void(bool enabled)>;
using FailureChanged = Fn<void(FailurePhase phase)>;

class Controller final {
public:
	Controller(
		std::unique_ptr<Helper> helper,
		ApplyProxy applyProxy,
		ClearProxy clearProxy,
		IntentChanged intentChanged,
		FailureChanged failureChanged);
	~Controller();

	void start(bool enabled);
	void setEnabled(bool enabled);
	void configurationFailed();
	[[nodiscard]] State state() const;
	[[nodiscard]] FailurePhase failurePhase() const;

private:
	void healthDone(int request, HealthResult result);
	void fail(FailurePhase phase);
	[[nodiscard]] MTP::ProxyData loopbackProxy(const HealthResult &result) const;

	std::unique_ptr<Helper> _helper;
	ApplyProxy _applyProxy;
	ClearProxy _clearProxy;
	IntentChanged _intentChanged;
	FailureChanged _failureChanged;
	base::Timer _healthTimeout;
	State _state = State::Disabled;
	FailurePhase _failurePhase = FailurePhase::None;
	int _request = 0;
	bool _enabled = true;

};

[[nodiscard]] std::unique_ptr<Helper> CreateUnavailableHelper();

} // namespace Reworked::Connectivity
