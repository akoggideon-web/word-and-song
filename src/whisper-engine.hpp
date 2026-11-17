// Word & Song for OBS — speech-to-text engine interface.
#pragma once

#include <memory>
#include <string>

namespace ws {

class SpeechEngine {
public:
	virtual ~SpeechEngine() = default;
	virtual bool load(const std::string &modelPath, std::string &error) = 0;
	// pcm: mono float samples at 16 kHz. prompt: vocabulary hint (may be empty).
	virtual std::string transcribe(const float *pcm, size_t samples, const std::string &prompt) = 0;
};

// Returns nullptr when the plugin was built without Whisper.
std::unique_ptr<SpeechEngine> createWhisperEngine();

} // namespace ws
