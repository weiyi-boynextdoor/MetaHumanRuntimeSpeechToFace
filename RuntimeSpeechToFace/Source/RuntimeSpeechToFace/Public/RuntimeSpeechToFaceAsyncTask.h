// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Nodes/SpeechToAnimNode.h"
#include "SpeechSoundWave.h"
#include "AudioDrivenAnimationMood.h"
#include "AudioDrivenAnimationConfig.h"
#include "Pipeline/Pipeline.h"
#include "Animation/Skeleton.h"
#include "RuntimeAnimation.h"
#include "NNERuntimeCPU.h"
#include "RuntimeSpeechToFaceAsyncTask.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRuntimeSpeechToFaceAsyncDelegate, URuntimeAnimation*, Anim, FString, Reason);

UCLASS()
class URuntimeSpeechToFaceAsync : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FRuntimeSpeechToFaceAsyncDelegate OnCompleted;

	UPROPERTY(BlueprintAssignable)
	FRuntimeSpeechToFaceAsyncDelegate OnFailed;

	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", DisplayName = "Speech To Face Anim"), Category = "RuntimeSpeechToFace")
	static URuntimeSpeechToFaceAsync* SpeechToFaceAnim(UObject* WorldContextObject, USoundWave* SoundWave, USkeleton* Skeleton, EAudioDrivenAnimationMood Mood = EAudioDrivenAnimationMood::AutoDetect, float MoodIntensity = 1.0f, EAudioDrivenAnimationOutputControls AudioDrivenAnimationOutputControls = EAudioDrivenAnimationOutputControls::FullFace);

	void Activate() override;

	void BeginDestroy() override;

private:
	void FrameComplete(TSharedPtr<UE::MetaHuman::Pipeline::FPipelineData> InPipelineData);
	void ProcessComplete(TSharedPtr<UE::MetaHuman::Pipeline::FPipelineData> InPipelineData);

private:
	bool bIsProcessing = false;
	TObjectPtr<USoundWave> SoundWave;
	TObjectPtr<USkeleton> Skeleton;
	EAudioDrivenAnimationMood Mood = EAudioDrivenAnimationMood::AutoDetect;
	float MoodIntensity = 1.0f;
	EAudioDrivenAnimationOutputControls AudioDrivenAnimationOutputControls = EAudioDrivenAnimationOutputControls::FullFace;

	TObjectPtr<URuntimeAnimation> Anim;

public:
	static bool bHasProcessingInstance;

	static TSharedPtr<UE::NNE::IModelInstanceCPU> AudioExtractor;
	static TSharedPtr<UE::NNE::IModelInstanceCPU> RigLogicPredictor;
};
