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
#include "DataDefs.h"
#include "GuiToRawControlsUtils.h"
#include "RuntimeSpeechToFaceSettings.h"
#include "SpeechSoundWave.h"

using FloatSamples = Audio::VectorOps::FAlignedFloatBuffer;

using FAnimationFrame = TMap<FString, float>;

static const FName RootBoneName = TEXT("root");

static constexpr uint32 AudioEncoderSampleRateHz = 16000;
static constexpr float RigLogicPredictorOutputFps = 50.0f;
static constexpr float RigLogicPredictorMaxAudioSamples = AudioEncoderSampleRateHz * 30;
static constexpr float RigLogicPredictorFrameDuration = 1.f / RigLogicPredictorOutputFps;
static constexpr float SamplesPerFrame = AudioEncoderSampleRateHz * RigLogicPredictorFrameDuration;

static constexpr int32 StreamBufferSize = 19200;

TSharedPtr<UE::NNE::IModelInstanceCPU> URuntimeSpeechToFaceAsync::AudioExtractor;
TSharedPtr<UE::NNE::IModelInstanceCPU> URuntimeSpeechToFaceAsync::RigLogicPredictor;

bool URuntimeSpeechToFaceAsync::bHasProcessingInstance = false;

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

URuntimeSpeechToFaceAsync* URuntimeSpeechToFaceAsync::SpeechToFaceAnim(UObject* WorldContextObject, USoundWave* SoundWave, USkeleton* Skeleton, EAudioDrivenAnimationMood Mood, float MoodIntensity)
{
	URuntimeSpeechToFaceAsync* Action = NewObject<URuntimeSpeechToFaceAsync>();
	Action->RegisterWithGameInstance(WorldContextObject);
	Action->SoundWave = SoundWave;
	Action->Skeleton = Skeleton;
	Action->Mood = Mood;
	Action->MoodIntensity = MoodIntensity;
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

	USpeechSoundWave* SpeechSoundWave = Cast<USpeechSoundWave>(SoundWave);
	if (SpeechSoundWave)
	{
		OutRawPCMData = SpeechSoundWave->GetPCMData();
		return true;
	}

	int BufferLen = FMath::CeilToInt(sizeof(int16) * OutSampleRate * OutNumChannels * SoundWave->Duration);
	OutRawPCMData.Reserve(BufferLen);

	if (SoundWave->bProcedural)
	{
		OutRawPCMData.Reset(BufferLen);
		SoundWave->GeneratePCMData(OutRawPCMData.GetData(), BufferLen);
		return true;
	}

	FName RuntimeFormat = SoundWave->GetRuntimeFormat();
	TArray<uint8> RawPCMData;

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
		int32 NumBytesStreamed = FMath::Min(StreamBufferSize, BufferLen - OutRawPCMData.Num());
		int OldSize = OutRawPCMData.Num();
		OutRawPCMData.AddZeroed(NumBytesStreamed);
		AudioInfo->StreamCompressedData(OutRawPCMData.GetData() + OldSize, false, NumBytesStreamed, NumBytesStreamed);
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

static bool ExtractAudioFeatures(const FloatSamples& Samples, const TSharedPtr<UE::NNE::IModelInstanceCPU>& AudioExtractor, TArray<float>& OutAudioData)
{
	using namespace UE::NNE;

	OutAudioData.Empty((Samples.Num() / SamplesPerFrame) * 512);

	// Restrict extracting of audio features to 30 second chunks as the model does not support more
	for (int32 SampleIndex = 0; SampleIndex < Samples.Num(); SampleIndex += RigLogicPredictorMaxAudioSamples)
	{
		const uint32 SamplesCount = FMath::Clamp(Samples.Num() - SampleIndex, 0, RigLogicPredictorMaxAudioSamples);

		TArray<uint32, TInlineAllocator<2>> ExtractorInputShapesData = { 1, SamplesCount };
		TArray<FTensorShape, TInlineAllocator<1>> ExtractorInputShapes = { FTensorShape::Make(ExtractorInputShapesData) };
		if (AudioExtractor->SetInputTensorShapes(ExtractorInputShapes) != IModelInstanceCPU::ESetInputTensorShapesStatus::Ok)
		{
			UE_LOG(LogTemp, Error, TEXT("Could not set the audio extractor input tensor shapes"));
			return false;
		}

		// Todo: last frame of the last chunk will not be complete (if not multiple of SamplesPerFrame). Should we ceil/pad/0-fill? 
		const uint32 NumFrames = static_cast<uint32>(SamplesCount / SamplesPerFrame);
		TArray<uint32, TInlineAllocator<3>> ExtractorOutputShapeData = { 1, NumFrames, 512 };
		FTensorShape ExtractorOutputShape = FTensorShape::Make(ExtractorOutputShapeData);
		TArray<float> ExtractorOutputData;
		ExtractorOutputData.SetNumUninitialized(ExtractorOutputShape.Volume());

		TArray<FTensorBindingCPU, TInlineAllocator<1>> ExtractorInputBindings = { {(void*)(Samples.GetData() + SampleIndex), SamplesCount * sizeof(float)} };
		TArray<FTensorBindingCPU, TInlineAllocator<1>> ExtractorOutputBindings = { {(void*)ExtractorOutputData.GetData(), ExtractorOutputData.Num() * sizeof(float)} };
		if (AudioExtractor->RunSync(ExtractorInputBindings, ExtractorOutputBindings) != IModelInstanceCPU::ESetInputTensorShapesStatus::Ok)
		{
			UE_LOG(LogTemp, Error, TEXT("The audio extractor NNE model failed to execute"));
			return false;
		}

		OutAudioData.Append(ExtractorOutputData.GetData(), ExtractorOutputData.Num());
	}
	return true;
}

static bool RunPredictor(
	const TSharedPtr<UE::NNE::IModelInstanceCPU>& RigLogicPredictor,
	const uint32 InFaceControlNum,
	const uint32 InBlinkControlNum,
	const uint32 InSamplesNum,
	const TArray<float>& InAudioData,
	const EAudioDrivenAnimationMood& Mood,
	const float DesiredMoodIntensity,
	TArray<float>& OutRigLogicValues,
	TArray<float>& OutRigLogicBlinkValues,
	TArray<float>& OutRigLogicHeadValues
)
{
	using namespace UE::NNE;

	const uint32 NumFrames = static_cast<uint32>(InSamplesNum / SamplesPerFrame);
	TArray<uint32, TInlineAllocator<2>> AudioShapeData = { 1, NumFrames, 512 };

	int32 MoodIndex = Mood == EAudioDrivenAnimationMood::AutoDetect ? -1 : static_cast<int32>(Mood);
	const TArray<int32, TInlineAllocator<1>> MoodIndexArray = { MoodIndex, };
	TArray<uint32, TInlineAllocator<1>> MoodIndexShapeData = { 1, };

	const TArray<float, TInlineAllocator<1>> MoodIntensityArray = { DesiredMoodIntensity, };
	TArray<uint32, TInlineAllocator<1>> MoodIntensityShapeData = { 1, };

	TArray<FTensorShape, TInlineAllocator<3>> InputTensorShapes = {
		FTensorShape::Make(AudioShapeData),
		FTensorShape::Make(MoodIndexShapeData),
		FTensorShape::Make(MoodIntensityShapeData)
	};

	check(RigLogicPredictor);

	if (RigLogicPredictor->SetInputTensorShapes(InputTensorShapes) != IModelInstanceCPU::ESetInputTensorShapesStatus::Ok)
	{
		return false;
	}

	// Bind the inputs

	// Tensor binding requires non-const void* - we're trusting it not to mutate the input data.
	void* AudioDataPtr = const_cast<void*>(static_cast<const void*>(InAudioData.GetData()));
	void* MoodIndexDataPtr = const_cast<void*>(static_cast<const void*>(MoodIndexArray.GetData()));
	void* MoodIntensityDataPtr = const_cast<void*>(static_cast<const void*>(MoodIntensityArray.GetData()));

	TArray<FTensorBindingCPU, TInlineAllocator<3>> InputBindings = {
		{AudioDataPtr, InAudioData.Num() * sizeof(float)},
		{MoodIndexDataPtr, MoodIndexArray.Num() * sizeof(float)},
		{MoodIntensityDataPtr, MoodIntensityArray.Num() * sizeof(float)}
	};

	// Bind the outputs
	TArray<float> FaceParameters;
	TArray<uint32, TInlineAllocator<3>> FaceParametersShapeData = { 1, NumFrames,  InFaceControlNum };
	TArray<FTensorShape, TInlineAllocator<1>> FaceParametersShape = { FTensorShape::Make(FaceParametersShapeData) };
	FaceParameters.SetNumUninitialized(FaceParametersShape[0].Volume());

	TArray<float> BlinkParameters;
	TArray<uint32, TInlineAllocator<3>> BlinkParametersShapeData = { 1, NumFrames,  InBlinkControlNum };
	TArray<FTensorShape, TInlineAllocator<1>> BlinkParametersShape = { FTensorShape::Make(BlinkParametersShapeData) };
	BlinkParameters.SetNumUninitialized(BlinkParametersShape[0].Volume());

	const uint32 NumOutputHeadControls = static_cast<uint32>(ModelHeadControls.Num());

	TArray<float> HeadParameters;
	TArray<uint32, TInlineAllocator<3>> HeadParametersShapeData = { 1, NumFrames,  NumOutputHeadControls };
	TArray<FTensorShape, TInlineAllocator<1>> HeadParametersShape = { FTensorShape::Make(HeadParametersShapeData) };
	HeadParameters.SetNumUninitialized(HeadParametersShape[0].Volume());

	void* FaceParametersPtr = static_cast<void*>(FaceParameters.GetData());
	void* BlinkParametersPtr = static_cast<void*>(BlinkParameters.GetData());
	void* HeadParametersPtr = static_cast<void*>(HeadParameters.GetData());

	TArray<FTensorBindingCPU, TInlineAllocator<2>> OutputBindings = {
		{FaceParametersPtr, FaceParameters.Num() * sizeof(float)},
		{BlinkParametersPtr, BlinkParameters.Num() * sizeof(float)},
		{HeadParametersPtr, HeadParameters.Num() * sizeof(float) }
	};

	if (RigLogicPredictor->RunSync(InputBindings, OutputBindings) != IModelInstanceCPU::ESetInputTensorShapesStatus::Ok)
	{
		UE_LOG(LogTemp, Error, TEXT("The rig logic model failed to execute"));
		return false;
	}

	OutRigLogicValues = MoveTemp(FaceParameters);
	OutRigLogicBlinkValues = MoveTemp(BlinkParameters);
	OutRigLogicHeadValues = MoveTemp(HeadParameters);

	return true;
}

TArray<FAnimationFrame> ResampleAnimation(TArrayView<const float> InRawAnimation, TArrayView<const FString> InRigControlNames, uint32 ControlNum, float InOutputFps)
{
	const uint32 RawFrameCount = InRawAnimation.Num() / ControlNum;
	const float AnimationLengthSec = RawFrameCount * RigLogicPredictorFrameDuration;
	const uint32 ResampledFrameCount = FMath::FloorToInt32(AnimationLengthSec * InOutputFps);

	// Resample using linear interpolation
	TArray<FAnimationFrame> ResampledAnimation;
	ResampledAnimation.AddDefaulted(ResampledFrameCount);

	for (uint32 ResampledFrameIndex = 0; ResampledFrameIndex < ResampledFrameCount; ++ResampledFrameIndex)
	{
		// Get corresponding raw frame time
		const float FrameStartSec = ResampledFrameIndex / InOutputFps;
		const float RawFrameIndex = FMath::Clamp(FrameStartSec * RigLogicPredictorOutputFps, 0, RawFrameCount - 1);

		// Get nearest full frames and distance between the two
		const uint32 PrevRawFrameIndex = FMath::FloorToInt32(RawFrameIndex);
		const uint32 NextRawFrameIndex = FMath::CeilToInt32(RawFrameIndex);
		const float RawFramesDelta = RawFrameIndex - PrevRawFrameIndex;

		// Add interpolated control values for the given frames
		ResampledAnimation[ResampledFrameIndex].Reserve(ControlNum);
		for (uint32 ControlIndex = 0; ControlIndex < ControlNum; ++ControlIndex)
		{
			const float PrevRawControlValue = InRawAnimation[PrevRawFrameIndex * ControlNum + ControlIndex];
			const float NextRawControlValue = InRawAnimation[NextRawFrameIndex * ControlNum + ControlIndex];
			const float ResampledValue = FMath::Lerp(PrevRawControlValue, NextRawControlValue, RawFramesDelta);

			ResampledAnimation[ResampledFrameIndex].Add(InRigControlNames[ControlIndex], ResampledValue);
		}
	}

	return ResampledAnimation;
}

void URuntimeSpeechToFaceAsync::Activate()
{
	if (!(AudioExtractor.IsValid() && RigLogicPredictor.IsValid()))
	{
		FSoftObjectPath AudioEncoder = GetDefault<URuntimeSpeechToFaceSettings>()->AudioEncoder;
		FSoftObjectPath AnimationDecoder = GetDefault<URuntimeSpeechToFaceSettings>()->AnimationDecoder;
		AudioExtractor = TryLoadModelData(AudioEncoder);
		RigLogicPredictor = TryLoadModelData(AnimationDecoder);
	}

	if (!(AudioExtractor.IsValid() && RigLogicPredictor.IsValid()))
	{
		OnFailed.Broadcast(nullptr, TEXT("RuntimeSpeechToFaceAsync: Failed to load models."));
		SetReadyToDestroy();
		return;
	}

	if (bHasProcessingInstance)
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
	bHasProcessingInstance = true;

	const int NumFrames = static_cast<int>(SoundWave->Duration * 30);

	Anim = NewObject<URuntimeAnimation>(GetTransientPackage(), URuntimeAnimation::StaticClass(), TEXT("FaceAnim"));
	Anim->Duration = SoundWave->Duration;

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
		bIsProcessing = false;
		return;
	}

	// Step 2: extract audio features
	TArray<float> ExtractedAudioData;
	if (!ExtractAudioFeatures(Samples, AudioExtractor, ExtractedAudioData))
	{
		OnFailed.Broadcast(nullptr, TEXT("RuntimeSpeechToFaceAsync: ExtractAudioFeatures."));
		SetReadyToDestroy();
		bIsProcessing = false;
		return;
	}

	// Step 3: run rig logic predictor to get animation data
	TArray<float> RigLogicValues;
	TArray<float> RigLogicBlinkValues;
	TArray<float> RigLogicHeadValues;

	if (!RunPredictor(RigLogicPredictor, RigControlNames.Num(), BlinkRigControlNames.Num(), Samples.Num(), ExtractedAudioData, Mood, MoodIntensity, RigLogicValues, RigLogicBlinkValues, RigLogicHeadValues))
	{
		OnFailed.Broadcast(nullptr, TEXT("RuntimeSpeechToFaceAsync: RunPredictor."));
		SetReadyToDestroy();
		bIsProcessing = false;
		return;
	}

	// Step 4: resample animation
	TArray<FAnimationFrame> OutAnimationData = ResampleAnimation(RigLogicValues, RigControlNames, RigControlNames.Num(), 30.0f);
	for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
	{
		TMap<FString, float> AnimationFrame = GuiToRawControlsUtils::ConvertGuiToRawControls(OutAnimationData[FrameIndex]);
		if (FrameIndex == 0)
		{
			for (const auto& Sample : AnimationFrame)
			{
				Anim->FloatCurves.Add(FFloatCurve(*Sample.Key, 0));
			}
		}
	
		int CurveIndex = 0;
		const float FrameTime = FrameIndex / 30.0f;
		for (const TPair<FString, float>& Sample : AnimationFrame)
		{
			Anim->FloatCurves[CurveIndex].FloatCurve.AddKey(FrameTime, Sample.Value);
			++CurveIndex;
		}
	}

	OnCompleted.Broadcast(Anim, TEXT("Success"));
	bHasProcessingInstance = false;
	SetReadyToDestroy();
	bIsProcessing = false;
	return;
}

void URuntimeSpeechToFaceAsync::BeginDestroy()
{
	if (bIsProcessing)
	{
		bHasProcessingInstance = false;
	}
	Super::BeginDestroy();
}
