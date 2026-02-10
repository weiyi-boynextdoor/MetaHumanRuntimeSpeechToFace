// Resembles SoundWaveProcedural, but can play multiple times

#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeCounter.h"
#include "UObject/ObjectMacros.h"
#include "HAL/ThreadSafeBool.h"
#include "Containers/Queue.h"
#include "Sound/SoundWave.h"
#include "misc/ScopeRWLock.h"
#include "SpeechSoundWave.generated.h"

#if PLATFORM_IOS
#define DEFAULT_PROCEDURAL_SOUNDWAVE_BUFFER_SIZE (8 * 1024)
#else
#define DEFAULT_PROCEDURAL_SOUNDWAVE_BUFFER_SIZE 1024
#endif

DECLARE_DYNAMIC_DELEGATE_OneParam(FOnSoundWaveDelegate, USpeechSoundWave*, SoundWave);

UCLASS(MinimalAPI)
class USpeechSoundWave : public USoundWave
{
	GENERATED_BODY()

private:
	FRWLock AudioLock;

	// The amount of bytes queued and not yet consumed
	FThreadSafeCounter AvailableByteCount;

	// The actual audio buffer that can be consumed. QueuedAudio is fed to this buffer. Accessed only audio thread.
	TSharedPtr<TArray<uint8>> AudioBuffer;

	uint32_t SampleIndex;

protected:

	// Number of samples to pad with 0 if there isn't enough audio available
	int32 NumBufferUnderrunSamples;

	// The number of PCM samples we want to generate. This can't be larger than SamplesNeeded in GeneratePCMData callback, but can be less.
	int32 NumSamplesToGeneratePerCallback;

	// Procedural Sounds don't represent a wav file, don't do anything when serializing cue points
	virtual void SerializeCuePoints(FArchive& Ar, const bool bIsLoadingFromCookedArchive) {}

public:
	USpeechSoundWave(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintCallable)
	static void CreateSpeechSoundWaveFromFile(const FString& FilePath, const FOnSoundWaveDelegate& SoundWaveCallback);

	/** Make a copy that shares AudioBuffer **/
	UFUNCTION(BlueprintCallable)
	USpeechSoundWave* MakeShallowCopy() const;

	//~ Begin UObject Interface. 
	virtual void Serialize(FArchive& Ar) override;
	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;
	UE_DEPRECATED(5.4, "Implement the version that takes FAssetRegistryTagsContext instead.")
	virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;
	//~ End UObject Interface. 

	//~ Begin USoundWave Interface.
	virtual int32 GeneratePCMData(uint8* PCMData, const int32 SamplesNeeded) override;
	virtual bool HasCompressedData(FName Format, ITargetPlatform* TargetPlatform) const override;
	virtual void BeginGetCompressedData(FName Format, const FPlatformAudioCookOverrides* CompressionOverrides, const ITargetPlatform* InTargetPlatform) override;
	virtual FByteBulkData* GetCompressedData(FName Format, const FPlatformAudioCookOverrides* CompressionOverrides, const ITargetPlatform* InTargetPlatform) override;
	virtual void InitAudioResource( FByteBulkData& CompressedData ) override;
	virtual bool InitAudioResource(FName Format) override;
	virtual int32 GetResourceSizeForFormat(FName Format) override;
	virtual bool IsSeekable() const override { return false; }
	//~ End USoundWave Interface.

	/** Set AudioBuffer data */
	void SetAudio(TArray<uint8> PCMData);

	void Seek(uint32_t Index);

	/** Size in bytes of a single sample of audio in the procedural audio buffer. */
	int32 SampleByteSize;
};