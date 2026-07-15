// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DSP/MultithreadedPatching.h"
#include "AudioProducer.h"
#include "SampleBuffer.h"
#include "TickableTask.h"

#define UE_API PIXELSTREAMING2_API

namespace UE::PixelStreaming2
{
	class FAudioCapturer;

	class FAudioPatchMixer : public Audio::FPatchMixer
	{
	public:
		UE_API FAudioPatchMixer(uint8 NumChannels, uint32 SampleRate, float SampleSizeSeconds);
		virtual ~FAudioPatchMixer() = default;

		UE_API uint32 GetMaxBufferSize() const;
		UE_API uint8  GetNumChannels() const;
		UE_API uint32 GetSampleRate() const;

	protected:
		uint8  NumChannels;
		uint32 SampleRate;
		float  SampleSizeSeconds;
	};

	class FPatchInputProxy
	{
	public:
		UE_API FPatchInputProxy(TSharedPtr<FAudioPatchMixer> InMixer);
		UE_API virtual ~FPatchInputProxy();

		UE_API void OnNewAudioFrame(const float* AudioData, int32 InNumSamples, int32 InNumChannels, int32 InSampleRate);

	protected:
		TSharedPtr<FAudioPatchMixer> Mixer;
		Audio::FResampler			 Resampler;
		Audio::FPatchInput			 PatchInput;
		uint8						 NumChannels;
		uint32						 SampleRate;
	};

	class FMixAudioTask : public FPixelStreamingTickableTask
	{
	public:
		UE_API FMixAudioTask(FAudioCapturer* Capturer, TSharedPtr<FAudioPatchMixer> Mixer);

		virtual ~FMixAudioTask() = default;

		// Begin FPixelStreamingTickableTask
		UE_API virtual void			  Tick(float DeltaMs) override;
		UE_API virtual const FString& GetName() const override;
		// End FPixelStreamingTickableTask

	protected:
		bool								  bIsRunning;
		Audio::VectorOps::FAlignedFloatBuffer MixingBuffer;

		FAudioCapturer*				 Capturer;
		TSharedPtr<FAudioPatchMixer> Mixer;
	};

	class FAudioCapturer
	{
	public:
		static UE_API TSharedPtr<FAudioCapturer> Create(const int InSampleRate = 48000, const int InNumChannels = 2, const float InSampleSizeInSeconds = 0.5f);
		virtual ~FAudioCapturer() = default;

		// Mixed audio input will push its audio to an FPatchInputProxy for mixing
		UE_API void						  CreateAudioProducer(Audio::FDeviceId AudioDeviceId);
		UE_API void						  RemoveAudioProducer(Audio::FDeviceId AudioDeviceId);

		// Methods for managing custom audio producers that will have their audio mixed in with engine audio
		UE_API void AddAudioProducer(TSharedPtr<IPixelStreaming2AudioProducer> AudioProducer);
		UE_API void RemoveAudioProducer(TSharedPtr<IPixelStreaming2AudioProducer> AudioProducer);

		// Called when mixed audio has been produced (and optionally dumped) and is ready to be sent for encoding
		UE_API virtual void PushAudio(const float* AudioData, int32 NumSamples, int32 NumChannels, int32 SampleRate);

		/**
		 * This is broadcast each time audio is captured. Tracks should bind to this and push the audio into the track
		 */
		DECLARE_TS_MULTICAST_DELEGATE_FourParams(FOnAudioBuffer, const int16_t*, int32, int32, const int32);
		FOnAudioBuffer OnAudioBuffer;

	protected:
		UE_API FAudioCapturer(const int SampleRate = 48000, const int NumChannels = 2, const float SampleSizeInSeconds = 0.5f);

		UE_API void OnDebugDumpAudioChanged(IConsoleVariable* Var);
		UE_API void OnEnginePreExit();
		UE_API void WriteDebugAudio();
		// Allow the mix audio task to call OnAudio.
		friend class FMixAudioTask;
		UE_API void OnAudio(const float* AudioData, int32 NumSamples, int32 NumChannels, int32 SampleRate);

	protected:
		TSharedPtr<FAudioPatchMixer> Mixer;
		TSharedPtr<FMixAudioTask>	 MixerTask;

		// Audio producers created by the engine that capture audio from specific devices
		TMap<Audio::FDeviceId, TSharedPtr<FAudioProducer>> EngineAudioProducers;
		// Audio producers created by the user that push audio from arbitrary sources
		TMap<TSharedPtr<IPixelStreaming2AudioProducer>, TSharedPtr<FAudioProducer>> UserAudioProducers;

		int				  SampleRate;
		int				  NumChannels;
		float			  SampleSizeSeconds;
		Audio::FResampler Resampler;

		Audio::TSampleBuffer<int16_t> DebugDumpAudioBuffer;
	};
} // namespace UE::PixelStreaming2

#undef UE_API
