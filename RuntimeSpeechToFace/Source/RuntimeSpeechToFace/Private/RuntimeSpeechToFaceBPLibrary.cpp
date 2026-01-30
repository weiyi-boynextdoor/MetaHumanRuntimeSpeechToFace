// Copyright Epic Games, Inc. All Rights Reserved.

#include "RuntimeSpeechToFaceBPLibrary.h"
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
		OnFailed.Broadcast(TEXT("RuntimeSpeechToFaceAsync: Already processing another request."));
		SetReadyToDestroy();
	}
	else if (!SoundWave)
	{
		OnFailed.Broadcast(TEXT("RuntimeSpeechToFaceAsync: No speech input."));
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

	// UAnimSequence* AnimSequence = NewObject<UAnimSequence>(GetTransientPackage(), UAnimSequence::StaticClass(), TEXT("SpeechToFace"));
	// AnimSequence->SetSkeleton(Skeleton);
	// AnimSequence->MarkPackageDirty();

	// IAnimationDataController& AnimationController = AnimSequence->GetController();
	
	// constexpr bool bShouldTransact = false;
	// // Any modifications to the animation sequence MUST be inside this bracket to minimize the likelihood of a race condition
	// // between this thread (game thread) and the anim sequence background tasks which update the animation data cache
	// // (the animation cache is not updated while within brackets)
	// AnimationController.OpenBracket(FText::FromString("SpeechToAnim"), bShouldTransact);

	// // Always reset animation in case we are overriding an existing one
	// AnimationController.RemoveAllBoneTracks(bShouldTransact);
	// AnimationController.RemoveAllCurvesOfType(ERawCurveTrackTypes::RCT_Float, bShouldTransact);
	// AnimationController.RemoveAllCurvesOfType(ERawCurveTrackTypes::RCT_Transform, bShouldTransact);
	// AnimationController.RemoveAllAttributes(bShouldTransact);

	// // Set the frame rate and number of frames as the first thing to avoid issues of resizing
	// AnimationController.SetFrameRate(FrameRate, bShouldTransact);
	// AnimationController.SetNumberOfFrames(NumFrames, bShouldTransact);

	// // Add timecode
	// AnimationController.AddBoneCurve(RootBoneName, bShouldTransact);
	// AnimationController.SetBoneTrackKeys(RootBoneName, { FVector3f::ZeroVector }, { FQuat4f::Identity }, { FVector3f::OneVector }, bShouldTransact);

	// UScriptStruct* IntScriptStruct = FIntegerAnimationAttribute::StaticStruct();
	// UScriptStruct* FloatScriptStruct = FFloatAnimationAttribute::StaticStruct();

	// TArray<FAnimationAttributeIdentifier> TimecodeAttributeIdentifiers;
	// TimecodeAttributeIdentifiers.Reserve(5);

	// for (const FName AttributeName : { TEXT("TCHour"), TEXT("TCMinute"), TEXT("TCSecond"), TEXT("TCFrame") })
	// {
	// 	FAnimationAttributeIdentifier AttributeIdentifier = UAnimationAttributeIdentifierExtensions::CreateAttributeIdentifier(AnimSequence, AttributeName, RootBoneName, IntScriptStruct);
	// 	AnimationController.AddAttribute(AttributeIdentifier, bShouldTransact);

	// 	TimecodeAttributeIdentifiers.Add(AttributeIdentifier);
	// }

	// for (const FName AttributeName : { TEXT("TCRate") })
	// {
	// 	FAnimationAttributeIdentifier AttributeIdentifier = UAnimationAttributeIdentifierExtensions::CreateAttributeIdentifier(AnimSequence, AttributeName, RootBoneName, FloatScriptStruct);
	// 	AnimationController.AddAttribute(AttributeIdentifier, bShouldTransact);

	// 	TimecodeAttributeIdentifiers.Add(AttributeIdentifier);
	// }

	// // Store all the animation curves to be written in the animation sequence in bulk
	// TMap<FAnimationCurveIdentifier, TArray<FRichCurveKey>> AnimationCurveKeys;

	// FFrameRate TimecodeRate{ 30, 1 };
	// FFrameNumber TimecodeFrame;
	// const float TimecodeRateDecimal = 30.0;

	// // Add animation curves
	// TSet<FString> AddedCurves;
	// for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex, ++TimecodeFrame)
	// {
	// 	const ERichCurveInterpMode CurveInterpolation = ERichCurveInterpMode::RCIM_Linear;

	// 	const float FrameTime = FrameIndex / FrameRate.AsDecimal();

	// 	FTimecode Timecode = FTimecode::FromFrameNumber(TimecodeFrame, TimecodeRate);

	// 	AnimationController.SetAttributeKey(TimecodeAttributeIdentifiers[0], FrameTime, &Timecode.Hours, IntScriptStruct, bShouldTransact);
	// 	AnimationController.SetAttributeKey(TimecodeAttributeIdentifiers[1], FrameTime, &Timecode.Minutes, IntScriptStruct, bShouldTransact);
	// 	AnimationController.SetAttributeKey(TimecodeAttributeIdentifiers[2], FrameTime, &Timecode.Seconds, IntScriptStruct, bShouldTransact);
	// 	AnimationController.SetAttributeKey(TimecodeAttributeIdentifiers[3], FrameTime, &Timecode.Frames, IntScriptStruct, bShouldTransact);
	// 	AnimationController.SetAttributeKey(TimecodeAttributeIdentifiers[4], FrameTime, &TimecodeRateDecimal, FloatScriptStruct, bShouldTransact);

	// 	FFrameAnimationData& FrameAnimData = AnimationData[FrameIndex];
	// 	if (!FrameAnimData.ContainsData())
	// 	{
	// 		continue;
	// 	}

	// 	for (const TPair<FString, float>& Sample : FrameAnimData.AnimationData)
	// 	{
	// 		FName SampleCurveName{ Sample.Key };

	// 		const FAnimationCurveIdentifier CurveId{ SampleCurveName, ERawCurveTrackTypes::RCT_Float };

	// 		if (!AddedCurves.Contains(Sample.Key))
	// 		{
	// 			if (!AnimationController.AddCurve(CurveId, AACF_Editable, bShouldTransact))
	// 			{
	// 				UE_LOG(LogTemp, Warning, TEXT("Failed to add animation curve '%s' into '%s'"), *Sample.Key, *AnimSequence->GetName());
	// 				continue;
	// 			}

	// 			AddedCurves.Add(Sample.Key);
	// 		}

	// 		AnimationCurveKeys.FindOrAdd(CurveId).Add(FRichCurveKey{ FrameTime, Sample.Value, 0, 0, CurveInterpolation });
	// 	}
	// }

	// for (TPair<FAnimationCurveIdentifier, TArray<FRichCurveKey>>& CurveKeysPair : AnimationCurveKeys)
	// {
	// 	AnimationController.SetCurveKeys(CurveKeysPair.Key, CurveKeysPair.Value, bShouldTransact);
	// }

	// // Flush the bone tracks that were unnecessarily added to avoid animation mismatch for meshes with different ref poses
	// AnimationController.RemoveAllBoneTracks(bShouldTransact);
	// // Add the root bone track back to avoid timecode attributes to be ignored
	// AnimationController.AddBoneCurve(RootBoneName, bShouldTransact);

	// AnimationController.NotifyPopulated();
	// AnimationController.CloseBracket(bShouldTransact);

	// AnimSequence->MarkPackageDirty();

	OnCompleted.Broadcast(Anim);
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

