// Copyright Epic Games, Inc. All Rights Reserved.

#include "RuntimeSpeechToFaceDeveloper.h"
#include "RuntimeSpeechToFaceSettings.h"
#include "ISettingsModule.h"

#define LOCTEXT_NAMESPACE "FRuntimeSpeechToFaceDeveloperModule"

void FRuntimeSpeechToFaceDeveloperModule::StartupModule()
{
	// Register settings
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->RegisterSettings("Project", "Plugins", "RuntimeSpeechToFace",
			LOCTEXT("RuntimeSpeechToFaceSettingsName", "Runtime Speech To Face"),
			LOCTEXT("RuntimeSpeechToFaceSettingsDescription", "Configure the Runtime Speech To Face plugin settings"),
			GetMutableDefault<URuntimeSpeechToFaceSettings>()
		);
	}
}

void FRuntimeSpeechToFaceDeveloperModule::ShutdownModule()
{
	// Unregister settings
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "RuntimeSpeechToFace");
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRuntimeSpeechToFaceDeveloperModule, RuntimeSpeechToFaceDeveloper)