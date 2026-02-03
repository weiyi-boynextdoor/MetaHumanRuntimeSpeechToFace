#include "RuntimeSpeechToFaceSettings.h"

URuntimeSpeechToFaceSettings::URuntimeSpeechToFaceSettings()
{
	AudioEncoder = TEXT("/MetaHuman/Speech2Face/NNE_AudioDrivenAnimation_AudioEncoder.NNE_AudioDrivenAnimation_AudioEncoder");
	AnimationDecoder = TEXT("/MetaHuman/Speech2Face/NNE_AudioDrivenAnimation_AnimationDecoder.NNE_AudioDrivenAnimation_AnimationDecoder");
}
