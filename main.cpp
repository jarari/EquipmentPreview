#include "CellLoadWatcher.h"
#include "ContainerChangeWatcher.h"
#include "FakePlayerStateTask.h"
#include "InputWatcher.h"
#include "MenuCloseWatcher.h"
#include <skse/GameData.h>
#include <SKSE/PluginAPI.h>
#include <SKSE/skse_version.h>
#include <ShlObj.h>

IDebugLog	gLog; 
const char* logPath = "\\My Games\\Skyrim\\SKSE\\EquipmentPreview.log";
const char* pluginName = "EquipmentPreview";

PluginHandle	g_pluginHandle = kPluginHandle_Invalid;
SKSEPapyrusInterface* g_papyrus = NULL;
SKSEMessagingInterface* g_message = NULL;

extern "C"
{
	bool SKSEPlugin_Query(const SKSEInterface * skse, PluginInfo * info)
	{
		gLog.OpenRelative(CSIDL_MYDOCUMENTS, logPath);

		// populate info structure
		info->infoVersion =	PluginInfo::kInfoVersion;
		info->name = pluginName;
		info->version =	1;

		// store plugin handle so we can identify ourselves later
		g_pluginHandle = skse->GetPluginHandle();

		g_papyrus = (SKSEPapyrusInterface*)skse->QueryInterface(kInterface_Papyrus);
		g_message = (SKSEMessagingInterface*)skse->QueryInterface(kInterface_Messaging);

		if(skse->isEditor)
		{
			_MESSAGE("loaded in editor, marking as incompatible");

			return false;
		}
		else if(skse->runtimeVersion != RUNTIME_VERSION_1_9_32_0)
		{
			_MESSAGE("unsupported runtime version %08X", skse->runtimeVersion);

			return false;
		}
		return true;
	}

	bool SKSEPlugin_Load(const SKSEInterface * skse)
	{
		MenuCloseWatcher::InitHook(skse);
		InputWatcher::InitHook();
		FakePlayerStateTask::InitTask((SKSETaskInterface*)skse->QueryInterface(kInterface_Task));
		g_message->RegisterListener(skse->GetPluginHandle(), "SKSE", [](SKSEMessagingInterface::Message* msg) -> void {
			if (msg->type == SKSEMessagingInterface::kMessage_PreLoadGame) {
				ContainerChangeWatcher::RemoveHook();
				MenuCloseWatcher::ResetHook();
				FakePlayerStateTask::EndTask();
				_MESSAGE("Game pre-load.");
			}
			else if (msg->type == SKSEMessagingInterface::kMessage_PostLoadGame) {
				if ((bool)msg->data) {
					if (!MenuCloseWatcher::GetInstance()->GetFakePlayer())
						MenuCloseWatcher::GetInstance()->InitializeFakePlayer();
					PlayerCharacter* player = *g_thePlayer;
					FakePlayerStateTask::StartTask(player->parentCell);
					_MESSAGE("Game loaded successfully.");
				}
			}
			else if (msg->type == SKSEMessagingInterface::kMessage_NewGame) {
				FakePlayerStateTask::EndTask();
				ContainerChangeWatcher::RemoveHook();
				MenuCloseWatcher::ResetHook();
				CellLoadWatcher::InitHook();
				PlayerCharacter* player = *g_thePlayer;
				FakePlayerStateTask::StartTask(player->parentCell);
			}
			else if (msg->type == SKSEMessagingInterface::kMessage_PostLoad) {
				MenuCloseWatcher::FindCWorld();
			}
			else if (msg->type == SKSEMessagingInterface::kMessage_DataLoaded) {
				MenuCloseWatcher::GetSkyrimVM();
			}
		});
		_MESSAGE("%s has loaded successfully.", pluginName);
		return true;
	}
};
