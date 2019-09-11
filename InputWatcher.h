#pragma once
#include "SKSE/PapyrusEvents.h"

class InputWatcher : public BSTEventSink <InputEvent> {
protected:
	static InputWatcher* instance;
	static std::string className;
	Actor* fakePlayer;
	bool mouseRightDown = false;
	float camOffsetXFar = -75;
	float camOffsetYFar = -50;
	float camOffsetZFar = -45;
	float camOffsetXNear = -25;
	float camOffsetYNear = 100;
	float camOffsetZNear = 0;
	float camZoom = 1;
	float camDiffZHeight = 0;
public:
	bool shouldUpdate = false;
	InputWatcher() {
		if (instance)
			delete(instance);
		instance = this;
		_MESSAGE((className + std::string(" instance created.")).c_str());
	}

	static InputWatcher* GetInstance() {
		return instance;
	}
	virtual ~InputWatcher();

	static void InitHook();

	static void RemoveHook();

	void AdjustToScale(float scale);

	virtual EventResult ReceiveEvent(InputEvent** evns, InputEventDispatcher* src);
};