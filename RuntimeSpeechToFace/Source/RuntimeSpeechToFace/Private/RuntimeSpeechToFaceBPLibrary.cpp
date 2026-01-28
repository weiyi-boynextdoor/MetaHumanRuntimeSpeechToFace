// Copyright Epic Games, Inc. All Rights Reserved.

#include "RuntimeSpeechToFaceBPLibrary.h"
#include "RuntimeSpeechToFace.h"

TSharedPtr<UE::MetaHuman::Pipeline::FSpeechToAnimNode> URuntimeSpeechToFaceAsync::SpeechToAnimSolver;

bool URuntimeSpeechToFaceAsync::bIsProcessing = false;

URuntimeSpeechToFaceAsync* URuntimeSpeechToFaceAsync::SpeechToFaceAnim(UObject* WorldContextObject, USpeechSoundWave* SoundWave, EAudioDrivenAnimationMood Mood, float MoodIntensity, EAudioDrivenAnimationOutputControls AudioDrivenAnimationOutputControls)
{
	if (!SpeechToAnimSolver.IsValid())
	{
		SpeechToAnimSolver = MakeShared<UE::MetaHuman::Pipeline::FSpeechToAnimNode>(TEXT("RuntimeSpeechToFace_SpeechToAnimNode"));
		SpeechToAnimSolver->LoadModels();
	}

	URuntimeSpeechToFaceAsync* Action = NewObject<URuntimeSpeechToFaceAsync>();
	Action->RegisterWithGameInstance(WorldContextObject);
	Action->SoundWave = SoundWave;
	Action->Mood = Mood;
	Action->MoodIntensity = MoodIntensity;
	Action->AudioDrivenAnimationOutputControls = AudioDrivenAnimationOutputControls;
	return Action;
}

void URuntimeSpeechToFaceAsync::Activate()
{
	if (bIsProcessing)
	{
		OnFailed.Broadcast(TEXT("RuntimeSpeechToFaceAsync: Already processing another request."));
		SetReadyToDestroy();
	}
	else
	{
		bIsProcessing = true;
		SpeechToAnimSolver->SetMood(Mood);
		SpeechToAnimSolver->SetMoodIntensity(MoodIntensity);
		SpeechToAnimSolver->SetOutputControls(AudioDrivenAnimationOutputControls);

		Pipeline = MakeShared<UE::MetaHuman::Pipeline::FPipeline>();

		OnCompleted.Broadcast(nullptr);
		bIsProcessing = false;
		SetReadyToDestroy();
	}
}

URuntimeSpeechToFaceBPLibrary::URuntimeSpeechToFaceBPLibrary(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
}

bool URuntimeSpeechToFaceBPLibrary::Initialize()
{
	return false;
}

float URuntimeSpeechToFaceBPLibrary::RuntimeSpeechToFaceSampleFunction(float Param)
{
	return -1;
}

