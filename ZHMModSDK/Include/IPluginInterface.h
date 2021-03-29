#pragma once

#include "Hooks.h"
#include "IModSDK.h"

class IPluginInterface
{
public:
	virtual ~IPluginInterface() {}
	virtual void PreInit() {}
	virtual void Init() {}
	virtual void OnEngineInitialized() {}
	virtual void OnDrawUI(bool p_HasFocus) {}
	virtual void OnDraw3D() {}

private:
	virtual void SetupUI()
	{
		auto* s_Context = SDK()->GetImGuiContext();

		if (!s_Context)
			return;

		ImGui::SetCurrentContext(s_Context);
		ImGui::SetAllocatorFunctions(SDK()->GetImGuiAlloc(), SDK()->GetImGuiFree(), SDK()->GetImGuiAllocatorUserData());
	}

	friend class ModSDK;
};

typedef IPluginInterface* (__cdecl* GetPluginInterface_t)();

#define DEFINE_ZHM_PLUGIN(PluginClass) extern "C" __declspec(dllexport) IPluginInterface* GetPluginInterface();\
	\
	inline PluginClass* Plugin()\
	{\
		return reinterpret_cast<PluginClass*>(GetPluginInterface());\
	}

#define DECLARE_ZHM_PLUGIN(PluginClass) \
	static IPluginInterface* g_ ## PluginClass ## _Instance = nullptr;\
	\
	extern "C" __declspec(dllexport) IPluginInterface* GetPluginInterface()\
	{\
		if (g_ ## PluginClass ## _Instance == nullptr)\
			g_ ## PluginClass ## _Instance = new PluginClass();\
		\
		return g_ ## PluginClass ## _Instance;\
	}

#define DEFINE_PLUGIN_DETOUR(PluginClass, ReturnType, DetourName, ...) DEFINE_DETOUR_WITH_CONTEXT(PluginClass, ReturnType, DetourName, __VA_ARGS__)

#define DECLARE_PLUGIN_DETOUR(PluginClass, ReturnType, DetourName, ...) DECLARE_DETOUR_WITH_CONTEXT(PluginClass, ReturnType, DetourName, __VA_ARGS__)

#define DEFINE_PLUGIN_LISTENER(PluginClass, EventName, ...) \
	template <class... Args>\
	static void EventName(void* th, Args... p_Args)\
	{\
		return reinterpret_cast<PluginClass*>(th)->EventName ## _Internal(p_Args...);\
	}\
	\
	void EventName ## _Internal(__VA_ARGS__);

#define DECLARE_PLUGIN_LISTENER(PluginClass, EventName, ...) \
	void PluginClass::EventName ## _Internal(__VA_ARGS__)
