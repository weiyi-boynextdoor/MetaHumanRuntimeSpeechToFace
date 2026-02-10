// Copyright Epic Games, Inc. All Rights Reserved.

#include "RuntimeSpeechToFace.h"

#define LOCTEXT_NAMESPACE "FRuntimeSpeechToFaceModule"

DEFINE_LOG_CATEGORY(LogRuntimeSpeechToFace);

void FRuntimeSpeechToFaceModule::StartupModule()
{	
}

void FRuntimeSpeechToFaceModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FRuntimeSpeechToFaceModule, RuntimeSpeechToFace)
