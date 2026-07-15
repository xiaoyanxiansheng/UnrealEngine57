// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioCapturer.h"

#define UE_API PIXELSTREAMING2RTC_API

namespace UE::PixelStreaming2
{
	/**
	 * FEpicRtcAudioCapturer overrides the default PushAudio behaviour of the FAudioCapturer in order to
	 * break up the pushed audio into 10ms chunks
	 */
	class FEpicRtcAudioCapturer : public FAudioCapturer
	{
	public:
		static UE_API TSharedPtr<FEpicRtcAudioCapturer> Create();
		virtual ~FEpicRtcAudioCapturer() = default;

		// Override the push audio method as EpicRtc needs the broadcasted audio to be in 10ms chunks
		UE_API virtual void PushAudio(const float* AudioData, int32 NumSamples, int32 NumChannels, int32 SampleRate) override;

	protected:
		FEpicRtcAudioCapturer() = default;

	private:
		TArray<int16_t> RecordingBuffer;
	};
} // namespace UE::PixelStreaming2

#undef UE_API
