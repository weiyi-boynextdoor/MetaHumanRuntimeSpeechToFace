// Copyright Epic Games, Inc. All Rights Reserved.

#include "RuntimeSpeechToFaceAsyncTask.h"
#include "RuntimeSpeechToFace.h"
#include "Animation/BuiltInAttributeTypes.h"
#include "AudioDecompress.h"
#include "Interfaces/IAudioFormat.h"
#include "NNEModelData.h"
#include "NNE.h"
#include "AudioResampler.h"
#include "SampleBuffer.h"

using FloatSamples = Audio::VectorOps::FAlignedFloatBuffer;

static const FName RootBoneName = TEXT("root");
static constexpr uint32 AudioEncoderSampleRateHz = 16000;

TSharedPtr<UE::MetaHuman::Pipeline::FSpeechToAnimNode> URuntimeSpeechToFaceAsync::SpeechToAnimSolver;
TSharedPtr<UE::NNE::IModelInstanceCPU> URuntimeSpeechToFaceAsync::AudioExtractor;
TSharedPtr<UE::NNE::IModelInstanceCPU> URuntimeSpeechToFaceAsync::RigLogicPredictor;

bool URuntimeSpeechToFaceAsync::bIsProcessing = false;

static TSharedPtr<UE::NNE::IModelInstanceCPU> TryLoadModelData(const FSoftObjectPath& InModelAssetPath)
{
	const FSoftObjectPtr ModelAsset(InModelAssetPath);
	UNNEModelData* ModelData = Cast<UNNEModelData>(ModelAsset.LoadSynchronous());

	if (!IsValid(ModelData))
	{
		check(false);
		UE_LOG(LogTemp, Error, TEXT("Failed to load model, it is invalid (nullptr)"));
		return nullptr;
	}

	if (!FModuleManager::Get().LoadModule(TEXT("NNERuntimeORT")))
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to load model, could not load NNE Runtime module (NNERuntimeORT): %s"), *ModelData->GetPathName());
		return nullptr;
	}

	const TWeakInterfacePtr<INNERuntimeCPU> NNERuntimeCPU = UE::NNE::GetRuntime<INNERuntimeCPU>(TEXT("NNERuntimeORTCpu"));

	if (!NNERuntimeCPU.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to load model, could not load NNE Runtime: %s"), *ModelData->GetPathName());
		return nullptr;
	}

	TSharedPtr<UE::NNE::IModelCPU> ModelCpu = NNERuntimeCPU->CreateModelCPU(ModelData);

	if (!ModelCpu.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to load model, could not create model CPU: %s"), *ModelData->GetPathName());
		return nullptr;
	}

	TSharedPtr<UE::NNE::IModelInstanceCPU> ModelInstance = ModelCpu->CreateModelInstanceCPU();

	if (ModelInstance.IsValid())
	{
		UE_LOG(LogTemp, Display, TEXT("Loaded model: %s"), *ModelData->GetPathName());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to load model, could not create model instance: %s"), *ModelData->GetPathName());
	}

	return ModelInstance;
}

URuntimeSpeechToFaceAsync* URuntimeSpeechToFaceAsync::SpeechToFaceAnim(UObject* WorldContextObject, USoundWave* SoundWave, USkeleton* Skeleton, EAudioDrivenAnimationMood Mood, float MoodIntensity, EAudioDrivenAnimationOutputControls AudioDrivenAnimationOutputControls)
{
	URuntimeSpeechToFaceAsync* Action = NewObject<URuntimeSpeechToFaceAsync>();
	Action->RegisterWithGameInstance(WorldContextObject);
	Action->SoundWave = SoundWave;
	Action->Skeleton = Skeleton;
	Action->Mood = Mood;
	Action->MoodIntensity = MoodIntensity;
	Action->AudioDrivenAnimationOutputControls = AudioDrivenAnimationOutputControls;
	return Action;
}

static bool GetImportedSoundWaveData(USoundWave* SoundWave, TArray<uint8>& OutRawPCMData, uint32& OutSampleRate, uint16& OutNumChannels)
{
	if (!SoundWave)
	{
		return false;
	}
	OutSampleRate = SoundWave->GetSampleRateForCurrentPlatform();
	OutNumChannels = SoundWave->NumChannels;
	int BufferLen = FMath::CeilToInt(sizeof(int16) * OutSampleRate * OutNumChannels * SoundWave->Duration);
	OutRawPCMData.Reserve(BufferLen);

	FName RuntimeFormat = SoundWave->GetRuntimeFormat();
	if (!SoundWave->HasCompressedData(RuntimeFormat))
	{
#if WITH_EDITOR
		return SoundWave->GetImportedSoundWaveData(OutRawPCMData, OutSampleRate, OutNumChannels);
#else
		//return false;
#endif
	}

	FByteBulkData* BulkData = SoundWave->GetCompressedData(RuntimeFormat);
	if (!BulkData || BulkData->GetBulkDataSize() <= 0)
	{
		return false;
	}

	const void* CompressedData = BulkData->LockReadOnly();
	int32 CompressedDataSize = BulkData->GetBulkDataSize();

	ICompressedAudioInfo* AudioInfo = IAudioInfoFactoryRegistry::Get().Create(SoundWave->GetRuntimeFormat());

	if (!AudioInfo)
	{
		BulkData->Unlock();
		return false;
	}

	FSoundQualityInfo QualityInfo = { 0 };

	// Get the header information of our compressed format
	if (!AudioInfo->StreamCompressedInfo(SoundWave, &QualityInfo))
	{
		BulkData->Unlock();
		return false;
	}

	// Stream read
	while (OutRawPCMData.Num() < BufferLen)
	{
		int32 NumBytesStreamed = 19200;
		int OldSize = OutRawPCMData.Num();
		OutRawPCMData.AddZeroed(NumBytesStreamed);
		AudioInfo->StreamCompressedData(OutRawPCMData.GetData() + OldSize, false, 19200, NumBytesStreamed);
	}

	delete AudioInfo;
	BulkData->Unlock();
	
	return true;
}

static bool ResampleAudio(FloatSamples InSamples, int32 InSampleRate, int32 InResampleRate, FloatSamples& OutResampledSamples)
{
	const Audio::FResamplingParameters Params = {
		Audio::EResamplingMethod::Linear,
		1, // NumChannels
		static_cast<float>(InSampleRate),
		static_cast<float>(InResampleRate),
		InSamples
	};

	const int32 ExpectedSampleCount = GetOutputBufferSize(Params);
	OutResampledSamples.SetNumUninitialized(ExpectedSampleCount);

	Audio::FResamplerResults Result;
	Result.OutBuffer = &OutResampledSamples;

	const bool bIsSuccess = Audio::Resample(Params, Result);
	if (!bIsSuccess)
	{
		return false;
	}

	if (Result.OutputFramesGenerated != ExpectedSampleCount)
	{
		OutResampledSamples.SetNum(Result.OutputFramesGenerated, EAllowShrinking::No);
	}

	return true;
}

static bool GetFloatSamples(const TWeakObjectPtr<const USoundWave>& SoundWave, const TArray<uint8>& PcmData, uint32 SampleRate, bool bDownmixChannels, uint32 ChannelToUse, float SecondsToSkip, FloatSamples& OutSamples)
{
	int16 Sample;
	const uint32 TotalSampleCount = PcmData.Num() / sizeof(Sample);
	const uint32 TotalSamplesToSkip = SecondsToSkip * SampleRate * SoundWave->NumChannels;
	if (TotalSamplesToSkip >= TotalSampleCount)
	{
		UE_LOG(LogTemp, Error, TEXT("Could not get float samples with %d skipped samples from %d samples for SoundWave %s"), TotalSamplesToSkip, TotalSampleCount, *SoundWave->GetName());
		return false;
	}

	// Audio data is stored as 16 bit signed samples with channels interleaved so that must be taken into account
	const uint8* PcmDataPtr = PcmData.GetData() + TotalSamplesToSkip * sizeof(Sample);

	const uint32 SamplesToSkipPerChannel = SecondsToSkip * SampleRate;
	const uint32 SampleCountPerChannel = PcmData.Num() / (sizeof(Sample) * SoundWave->NumChannels) - SamplesToSkipPerChannel;
	OutSamples.SetNumUninitialized(SampleCountPerChannel);

	if (bDownmixChannels && SoundWave->NumChannels > 1)
	{
		const int32 SampleCount = TotalSampleCount - TotalSamplesToSkip;

		Audio::FAlignedFloatBuffer Buffer;
		Buffer.SetNumUninitialized(SampleCount);
		Audio::ArrayPcm16ToFloat(MakeArrayView((int16*)PcmDataPtr, SampleCount), Buffer);

		Audio::TSampleBuffer<float> FloatSampleBuffer(Buffer, SoundWave->NumChannels, SampleRate);
		FloatSampleBuffer.MixBufferToChannels(1);
		Audio::FAlignedFloatBuffer MonoBuffer;
		MonoBuffer.SetNumUninitialized(FloatSampleBuffer.GetNumSamples());
		MonoBuffer = FloatSampleBuffer.GetArrayView();
		const float MaxValue = Audio::ArrayMaxAbsValue(MonoBuffer);
		if (MaxValue > 1.f)
		{
			Audio::ArrayMultiplyByConstantInPlace(MonoBuffer, 1.f / MaxValue);
		}

		OutSamples = MonoBuffer;
	}
	else
	{
		for (uint32 SampleIndex = 0; SampleIndex < SampleCountPerChannel; SampleIndex++)
		{
			// Position ourselves at the sample of appropriate channel, taking into account the channel layout
			const uint8* SampleData = PcmDataPtr + ChannelToUse * sizeof(uint16);
			FMemory::Memcpy(&Sample, SampleData, sizeof(Sample));
			// Convert to range [-1.0, 1.0)
			OutSamples[SampleIndex] = Sample / 32768.0f;

			PcmDataPtr += sizeof(Sample) * SoundWave->NumChannels;
		}
	}

	if (SampleRate != AudioEncoderSampleRateHz)
	{
		FloatSamples ResampledAudio;
		if (!ResampleAudio(MoveTemp(OutSamples), SampleRate, AudioEncoderSampleRateHz, ResampledAudio))
		{
			UE_LOG(LogTemp, Error, TEXT("Could not resample audio from %d to %d for SoundWave %s"), SampleRate, AudioEncoderSampleRateHz, *SoundWave->GetName());
			return false;
		}
		OutSamples = MoveTemp(ResampledAudio);
	}

	return true;
}

void URuntimeSpeechToFaceAsync::Activate()
{
	if (!(AudioExtractor.IsValid() && RigLogicPredictor.IsValid()))
	{
		SpeechToAnimSolver = MakeShared<UE::MetaHuman::Pipeline::FSpeechToAnimNode>(TEXT("RuntimeSpeechToFace_SpeechToAnimNode"));
		SpeechToAnimSolver->LoadModels();
		FAudioDrivenAnimationModels ModelNames;
		AudioExtractor = TryLoadModelData(ModelNames.AudioEncoder);
		RigLogicPredictor = TryLoadModelData(ModelNames.AnimationDecoder);
	}

	if (!(AudioExtractor.IsValid() && RigLogicPredictor.IsValid()))
	{
		OnFailed.Broadcast(nullptr, TEXT("RuntimeSpeechToFaceAsync: Failed to load models."));
		SetReadyToDestroy();
		return;
	}

	if (bIsProcessing)
	{
		OnFailed.Broadcast(nullptr, TEXT("RuntimeSpeechToFaceAsync: Already processing another request."));
		SetReadyToDestroy();
		return;
	}

	if (!SoundWave)
	{
		OnFailed.Broadcast(nullptr, TEXT("RuntimeSpeechToFaceAsync: No speech input."));
		SetReadyToDestroy();
		return;
	}

	bIsProcessing = true;

	// Step 1: get PCM data
	TArray<uint8> PcmData;
	uint16 ChannelNum;
	uint32 SampleRate;
	GetImportedSoundWaveData(SoundWave, PcmData, SampleRate, ChannelNum);

	FloatSamples Samples;
	if (!GetFloatSamples(SoundWave, PcmData, SampleRate, true, 0, 0, Samples))
	{
		OnFailed.Broadcast(nullptr, TEXT("RuntimeSpeechToFaceAsync: GetFloatSamples."));
		SetReadyToDestroy();
		return;
	}

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

