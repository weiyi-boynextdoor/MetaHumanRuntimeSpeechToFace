// Copyright Epic Games, Inc. All Rights Reserved.

#include "RuntimeSpeechToFaceAsyncTask.h"
#include "RuntimeSpeechToFace.h"
#include "Animation/BuiltInAttributeTypes.h"

static const FName RootBoneName = TEXT("root");

TSharedPtr<UE::MetaHuman::Pipeline::FSpeechToAnimNode> URuntimeSpeechToFaceAsync::SpeechToAnimSolver;

bool URuntimeSpeechToFaceAsync::bIsProcessing = false;

URuntimeSpeechToFaceAsync* URuntimeSpeechToFaceAsync::SpeechToFaceAnim(UObject* WorldContextObject, USoundWave* SoundWave, USkeleton* Skeleton, EAudioDrivenAnimationMood Mood, float MoodIntensity, EAudioDrivenAnimationOutputControls AudioDrivenAnimationOutputControls)
{
	if (!SpeechToAnimSolver.IsValid())
	{
		SpeechToAnimSolver = MakeShared<UE::MetaHuman::Pipeline::FSpeechToAnimNode>(TEXT("RuntimeSpeechToFace_SpeechToAnimNode"));
		SpeechToAnimSolver->LoadModels();
	}

	URuntimeSpeechToFaceAsync* Action = NewObject<URuntimeSpeechToFaceAsync>();
	Action->RegisterWithGameInstance(WorldContextObject);
	Action->SoundWave = SoundWave;
	Action->Skeleton = Skeleton;
	Action->Mood = Mood;
	Action->MoodIntensity = MoodIntensity;
	Action->AudioDrivenAnimationOutputControls = AudioDrivenAnimationOutputControls;
	return Action;
}

void URuntimeSpeechToFaceAsync::Activate()
{
	if (bIsProcessing)
	{
		OnFailed.Broadcast(nullptr, TEXT("RuntimeSpeechToFaceAsync: Already processing another request."));
		SetReadyToDestroy();
	}
	else if (!SoundWave)
	{
		OnFailed.Broadcast(nullptr, TEXT("RuntimeSpeechToFaceAsync: No speech input."));
		SetReadyToDestroy();
	}
	else
	{
		bIsProcessing = true;
		SpeechToAnimSolver->SetMood(Mood);
		SpeechToAnimSolver->SetMoodIntensity(MoodIntensity);
		SpeechToAnimSolver->SetOutputControls(AudioDrivenAnimationOutputControls);

		Pipeline = MakeShared<UE::MetaHuman::Pipeline::FPipeline>();
		Pipeline->AddNode(SpeechToAnimSolver);
		SpeechToAnimSolver->Audio = SoundWave;
		SpeechToAnimSolver->bDownmixChannels = true;
		SpeechToAnimSolver->AudioChannelIndex = 0;
		SpeechToAnimSolver->OffsetSec = 0;
		SpeechToAnimSolver->FrameRate = 30;
		SpeechToAnimSolver->ProcessingStartFrameOffset = 0;
		SpeechToAnimSolver->bGenerateBlinks = true;

		AnimationResultsPinName = SpeechToAnimSolver-> Name + ".Animation Out";

		UE::MetaHuman::Pipeline::FFrameComplete OnFrameComplete;
		UE::MetaHuman::Pipeline::FProcessComplete OnProcessComplete;
		OnFrameComplete.AddUObject(this, &URuntimeSpeechToFaceAsync::FrameComplete);
		OnProcessComplete.AddUObject(this, &URuntimeSpeechToFaceAsync::ProcessComplete);

		const int NumFrames = static_cast<int>(SoundWave->Duration * 30);

		AnimationData.Reset(NumFrames);
		AnimationData.AddDefaulted(NumFrames);

		UE::MetaHuman::Pipeline::FPipelineRunParameters PipelineRunParameters;
		PipelineRunParameters.SetStartFrame(0);
		PipelineRunParameters.SetEndFrame(NumFrames);
		PipelineRunParameters.SetOnFrameComplete(OnFrameComplete);
		PipelineRunParameters.SetOnProcessComplete(OnProcessComplete);
		PipelineRunParameters.SetGpuToUse(UE::MetaHuman::Pipeline::FPipeline::PickPhysicalDevice());
		PipelineRunParameters.SetMode(UE::MetaHuman::Pipeline::EPipelineMode::PushAsyncNodes);
		Pipeline->Run(PipelineRunParameters);
	}
}

void URuntimeSpeechToFaceAsync::BeginDestroy()
{
	if (Pipeline)
	{
		bIsProcessing = false;
	}
	Super::BeginDestroy();
}

void URuntimeSpeechToFaceAsync::FrameComplete(TSharedPtr<UE::MetaHuman::Pipeline::FPipelineData> InPipelineData)
{
	const int32 FrameNumber = InPipelineData->GetFrameNumber();
	UE_LOG(LogTemp, Verbose, TEXT("Processed Frame %d"), FrameNumber);

	FFrameAnimationData& AnimationFrame = AnimationData[FrameNumber];
	AnimationFrame = InPipelineData->MoveData<FFrameAnimationData>(AnimationResultsPinName);
}

void URuntimeSpeechToFaceAsync::ProcessComplete(TSharedPtr<UE::MetaHuman::Pipeline::FPipelineData> InPipelineData)
{
	const FFrameRate FrameRate{ 30, 1 };
	const int NumFrames = static_cast<int>(SoundWave->Duration * 30);

	URuntimeAnimation* Anim = NewObject<URuntimeAnimation>(GetTransientPackage(), URuntimeAnimation::StaticClass(), TEXT("FaceAnim"));
	Anim->Duration = SoundWave->Duration;

	for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
	{
		const float FrameTime = FrameIndex / 30.0f;
		FFrameAnimationData& FrameAnimData = AnimationData[FrameIndex];
		if (!FrameAnimData.ContainsData())
		{
			continue;
		}
		if (Anim->FloatCurves.Num() == 0)
		{
			Anim->FloatCurves.Reserve(FrameAnimData.AnimationData.Num());
			for (const TPair<FString, float>& Sample : FrameAnimData.AnimationData)
			{
				FName Key(*Sample.Key);
				Anim->FloatCurves.Add(FFloatCurve(*Sample.Key, 0));
			}
		}
		int CurveIndex = 0;
		for (const TPair<FString, float>& Sample : FrameAnimData.AnimationData)
		{
			Anim->FloatCurves[CurveIndex].FloatCurve.AddKey(FrameTime, Sample.Value);
			++CurveIndex;
		}		
	}

	OnCompleted.Broadcast(Anim, TEXT("Success"));
	Pipeline.Reset();
	bIsProcessing = false;
	SetReadyToDestroy();
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

