// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"

#include "RuntimeSpeechToFaceSettings.generated.h"

/**
 * Project Settings for the MetaHuman SDK
 */
UCLASS(MinimalAPI, defaultconfig, config = RuntimeSpeechToFace, meta = (DisplayName = "Runtime Speech To Face"))
class URuntimeSpeechToFaceSettings : public UObject
{
	GENERATED_BODY()

public:
	URuntimeSpeechToFaceSettings();

public:
	UPROPERTY(EditAnywhere, Config, Category = "NNE Models", meta = (ContentDir, DisplayName = "Audio Encoder", AllowedClasses = "/Script/NNE.NNEModelData"))
	FSoftObjectPath AudioEncoder;

	UPROPERTY(EditAnywhere, Config, Category = "NNE Models", meta = (ContentDir, DisplayName = "Animation Decoder", AllowedClasses = "/Script/NNE.NNEModelData"))
	FSoftObjectPath AnimationDecoder;
};
