// Copyright Epic Games, Inc. All Rights Reserved.

#include "SpeechSoundWave.h"

#include "AudioDevice.h"
#include "Engine/Engine.h"
#include "UObject/AssetRegistryTagsContext.h"
#include "SoundFileIO/SoundFileIO.h"
#include "Interfaces/IAudioFormat.h"
#include "Decoders/VorbisAudioInfo.h"
#include "RuntimeSpeechToFace.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(SpeechSoundWave)

struct FSpeechSoundWaveInfo
{
    int32 SampleRate;
    int32 NumChannels;
    int32 NumSamples;
    float Duration;
    float TotalSamples;
    TArray<uint8> PCMData;

    FSpeechSoundWaveInfo() = default;
    FSpeechSoundWaveInfo(const FSpeechSoundWaveInfo& Other) = default;
    FSpeechSoundWaveInfo(FSpeechSoundWaveInfo&& Other) = default;
    FSpeechSoundWaveInfo& operator=(const FSpeechSoundWaveInfo& Other) = default;
    FSpeechSoundWaveInfo& operator=(FSpeechSoundWaveInfo&& Other) = default;
};

USpeechSoundWave::USpeechSoundWave(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bProcedural = true;
	NumBufferUnderrunSamples = 512;
	NumSamplesToGeneratePerCallback = DEFAULT_PROCEDURAL_SOUNDWAVE_BUFFER_SIZE;
	static_assert(DEFAULT_PROCEDURAL_SOUNDWAVE_BUFFER_SIZE >= 512, "Should generate more samples than this per callback.");
	//checkf(NumSamplesToGeneratePerCallback >= NumBufferUnderrunSamples, TEXT("Should generate more samples than this per callback."));

	// If the main audio device has been set up, we can use this to define our callback size.
	// We need to do this for procedural sound waves that we do not process asynchronously,
	// to ensure that we do not underrun.
	
	SampleByteSize = 2;
}

USpeechSoundWave* USpeechSoundWave::MakeShallowCopy() const
{
	USpeechSoundWave* NewSoundWave = NewObject<USpeechSoundWave>();
	NewSoundWave->Duration = Duration;
	NewSoundWave->SampleRate = SampleRate;
	NewSoundWave->NumChannels = NumChannels;
	NewSoundWave->TotalSamples = TotalSamples;
	NewSoundWave->AudioBuffer = AudioBuffer;
	return NewSoundWave;
}

void USpeechSoundWave::SetAudio(TArray<uint8> PCMData)
{
	Audio::EAudioMixerStreamDataFormat::Type Format = GetGeneratedPCMDataFormat();
	SampleByteSize = (Format == Audio::EAudioMixerStreamDataFormat::Int16) ? 2 : 4;

	auto BufferSize = PCMData.Num();
	if (BufferSize == 0 || !ensure((BufferSize % SampleByteSize) == 0))
	{
		return;
	}

	{
		FWriteScopeLock WriteLock(AudioLock);
		AudioBuffer = MakeShared<TArray<uint8>>(MoveTemp(PCMData));
	}

	AvailableByteCount.Add(BufferSize);
}

int32 USpeechSoundWave::GeneratePCMData(uint8* PCMData, const int32 SamplesNeeded)
{
	FReadScopeLock ReadLock(AudioLock);
	if (AudioBuffer)
	{
		auto& AudioBufferRef = *AudioBuffer;

		Audio::EAudioMixerStreamDataFormat::Type Format = GetGeneratedPCMDataFormat();
		SampleByteSize = (Format == Audio::EAudioMixerStreamDataFormat::Int16) ? 2 : 4;

		int32 SamplesAvailable = AudioBufferRef.Num() / SampleByteSize - SampleIndex;
		int32 BytesAvailable = SamplesAvailable * SampleByteSize;
		int32 SamplesToGenerate = FMath::Min(NumSamplesToGeneratePerCallback, SamplesNeeded);

		check(SamplesToGenerate >= NumBufferUnderrunSamples);

		// Wait until we have enough samples that are requested before starting.
		if (SamplesAvailable > 0)
		{
			const int32 SamplesToCopy = FMath::Min<int32>(SamplesToGenerate, SamplesAvailable);
			const int32 BytesToCopy = SamplesToCopy * SampleByteSize;

			FMemory::Memcpy((void*)PCMData, &AudioBufferRef[SampleIndex * SampleByteSize], BytesToCopy);
			SampleIndex += SamplesToCopy;

			return BytesToCopy;
		}
	}

	// There wasn't enough data ready, write out zeros
	const int32 BytesCopied = NumBufferUnderrunSamples * SampleByteSize;
	FMemory::Memzero(PCMData, BytesCopied);
	return BytesCopied;
}

void USpeechSoundWave::Seek(uint32_t Index)
{
	FWriteScopeLock WriteLock(AudioLock);
	SampleIndex = Index;
}

int32 USpeechSoundWave::GetResourceSizeForFormat(FName Format)
{
	return 0;
}

void USpeechSoundWave::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS;
	Super::GetAssetRegistryTags(OutTags);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS;
}

void USpeechSoundWave::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
	Super::GetAssetRegistryTags(Context);
}

bool USpeechSoundWave::HasCompressedData(FName Format, ITargetPlatform* TargetPlatform) const
{
	return false;
}

void USpeechSoundWave::BeginGetCompressedData(FName Format, const FPlatformAudioCookOverrides* CompressionOverrides, const ITargetPlatform* InTargetPlatform)
{
	// SpeechSoundWave does not have compressed data and should generally not be asked about it
}

FByteBulkData* USpeechSoundWave::GetCompressedData(FName Format, const FPlatformAudioCookOverrides* CompressionOverrides, const ITargetPlatform* InTargetPlatform )
{
	// SpeechSoundWave does not have compressed data and should generally not be asked about it
	return nullptr;
}

void USpeechSoundWave::Serialize(FArchive& Ar)
{
	// Do not call the USoundWave version of serialize
	USoundBase::Serialize(Ar);

#if WITH_EDITORONLY_DATA
	// Due to "skipping" USoundWave::Serialize above, modulation
	// versioning is required to be called explicitly here.
	if (Ar.IsLoading())
	{
		ModulationSettings.VersionModulators();
	}
#endif // WITH_EDITORONLY_DATA
}

void USpeechSoundWave::InitAudioResource(FByteBulkData& CompressedData)
{
	// Should never be pushing compressed data to a SpeechSoundWave
	check(false);
}

bool USpeechSoundWave::InitAudioResource(FName Format)
{
	// Nothing to be done to initialize a USpeechSoundWave
	return true;
}

static bool GetSoundWaveInfoFromWav(FSpeechSoundWaveInfo& Info, TArray<uint8> RawWaveData)
{
    FWaveModInfo WaveInfo;
    FString ErrorMessage;
    if (!WaveInfo.ReadWaveInfo(RawWaveData.GetData(), RawWaveData.Num(), &ErrorMessage))
    {
        UE_LOG(LogRuntimeSpeechToFace, Error, TEXT("Unable to read wave file - \"%s\""), *ErrorMessage);
        return false;
    }

    int32 ChannelCount = (int32)*WaveInfo.pChannels;
    check(ChannelCount > 0);
    int32 SizeOfSample = (*WaveInfo.pBitsPerSample) / 8;
    int32 NumSamples = WaveInfo.SampleDataSize / SizeOfSample;
    int32 NumFrames = NumSamples / ChannelCount;

	Info.SampleRate = *WaveInfo.pSamplesPerSec;
    Info.Duration = (float)NumFrames / *WaveInfo.pSamplesPerSec;
	Info.NumChannels = ChannelCount;
	Info.TotalSamples = *WaveInfo.pSamplesPerSec * Info.Duration;
    Info.PCMData.AddUninitialized(WaveInfo.SampleDataSize);
    FMemory::Memcpy(Info.PCMData.GetData(), WaveInfo.SampleDataStart, WaveInfo.SampleDataSize);

    return true;
}

static bool GetSoundWaveInfoFromOgg(FSpeechSoundWaveInfo& Info, const TArray<uint8>& OggData)
{
    FVorbisAudioInfo	AudioInfo;
    FSoundQualityInfo	QualityInfo;
    if (!AudioInfo.ReadCompressedInfo(OggData.GetData(), OggData.Num(), &QualityInfo))
    {
        return false;
    }
    TArray<uint8> PCMData;
    PCMData.AddUninitialized(QualityInfo.SampleDataSize);
    AudioInfo.ReadCompressedData(PCMData.GetData(), false, QualityInfo.SampleDataSize);

    Info.SampleRate = QualityInfo.SampleRate;
    Info.Duration = QualityInfo.Duration;
    Info.NumChannels = QualityInfo.NumChannels;
    Info.TotalSamples = QualityInfo.Duration * QualityInfo.Duration;
    Info.PCMData = MoveTemp(PCMData);
    return true;
}

void USpeechSoundWave::CreateSpeechSoundWaveFromFile(const FString& FilePath, const FOnSoundWaveDelegate& SoundWaveCallback)
{
	AsyncTask(ENamedThreads::AnyBackgroundHiPriTask, [FilePath, SoundWaveCallback]()
		{
            TArray<uint8> FileContent;
            if (!FFileHelper::LoadFileToArray(FileContent, *FilePath))
            {
                UE_LOG(LogRuntimeSpeechToFace, Error, TEXT("Failed to load file at path: %s"), *FilePath);
                return;
            }
            double LoadingStartTime = FPlatformTime::Seconds();
            bool bSuccess = false;
            FSpeechSoundWaveInfo SoundWaveInfo;
            if (FilePath.ToLower().EndsWith(".wav"))
            {
				if (GetSoundWaveInfoFromWav(SoundWaveInfo, FileContent))
				{
					bSuccess = true;
				}
            }
            else if (FilePath.ToLower().EndsWith(".ogg"))
            {
				if (GetSoundWaveInfoFromOgg(SoundWaveInfo, FileContent))
				{
					bSuccess = true;
				}
            }
            else
            {
                // UE runtime only supports wav or ogg
            }
            AsyncTask(ENamedThreads::GameThread, [SoundWaveCallback, bSuccess, SoundWaveInfo, FilePath]()
                {
                    if (!bSuccess)
                    {
                        UE_LOG(LogRuntimeSpeechToFace, Error, TEXT("Failed to create sound wave from file at path: %s"), *FilePath);
                        SoundWaveCallback.ExecuteIfBound(nullptr);
                        return;
                    }
					USpeechSoundWave* SoundWave = NewObject<USpeechSoundWave>();
                    SoundWave->SetAudio(SoundWaveInfo.PCMData);
                    SoundWave->Duration = SoundWaveInfo.Duration;
                    SoundWave->SetImportedSampleRate(SoundWaveInfo.SampleRate);
                    SoundWave->SetSampleRate(SoundWaveInfo.SampleRate);
                    SoundWave->NumChannels = SoundWaveInfo.NumChannels;
                    SoundWave->TotalSamples = SoundWaveInfo.TotalSamples;
					SoundWaveCallback.ExecuteIfBound(SoundWave);
                });
		});
}
