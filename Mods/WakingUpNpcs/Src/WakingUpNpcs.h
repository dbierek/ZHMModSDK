#pragma once

#include <random>
#include <unordered_map>

#include "IPluginInterface.h"

class WakingUpNpcs : public IPluginInterface
{
public:
	WakingUpNpcs();
	~WakingUpNpcs() override;
	
	void Init() override;
	void OnEngineInitialized() override;

private:
	void OnFrameUpdate(const SGameUpdateEvent& p_UpdateEvent);

private:
	DEFINE_PLUGIN_DETOUR(WakingUpNpcs, void, OnLoadScene, ZEntitySceneContext*, const ZSceneData&)
	DEFINE_PLUGIN_LISTENER(WakingUpNpcs, OnConsoleCommand)

private:
	std::unordered_map<ZActor*, double> m_PacifiedTimes;
	std::random_device m_RandomDevice;
	std::mt19937 m_Generator;
};

DEFINE_ZHM_PLUGIN(WakingUpNpcs)