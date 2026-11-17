// Word & Song for OBS — whisper.cpp speech-to-text (fully offline).
#include "whisper-engine.hpp"

#include <whisper.h>

#include <obs-module.h>

#include <algorithm>
#include <cctype>
#include <thread>

namespace ws {
namespace {

void quietLog(enum ggml_log_level level, const char *text, void *)
{
	if (level >= GGML_LOG_LEVEL_ERROR && text)
		blog(LOG_WARNING, "[word-and-song] whisper: %s", text);
}

// Whisper emits placeholders for non-speech: "[BLANK_AUDIO]", "(music)", "[Music]", "*applause*".
std::string stripNonSpeech(const std::string &s)
{
	std::string out;
	int depth = 0;
	for (char c : s) {
		if (c == '[' || c == '(' || c == '*') {
			if (c == '*' && depth) {
				depth--;
				continue;
			}
			depth++;
			continue;
		}
		if ((c == ']' || c == ')') && depth) {
			depth--;
			continue;
		}
		if (!depth)
			out += c;
	}
	return out;
}

class WhisperEngine final : public SpeechEngine {
public:
	~WhisperEngine() override
	{
		if (ctx_)
			whisper_free(ctx_);
	}

	bool load(const std::string &modelPath, std::string &error) override
	{
		whisper_log_set(quietLog, nullptr);
		whisper_context_params cp = whisper_context_default_params();
		cp.use_gpu = true;
		ctx_ = whisper_init_from_file_with_params(modelPath.c_str(), cp);
		if (!ctx_) {
			error = "Could not load the Whisper model file. Check the path in Listen settings.";
			return false;
		}
		multilingual_ = whisper_is_multilingual(ctx_) != 0;
		return true;
	}

	std::string transcribe(const float *pcm, size_t samples, const std::string &prompt) override
	{
		if (!ctx_ || !samples)
			return {};
		whisper_full_params p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
		p.n_threads = (int)std::clamp(std::thread::hardware_concurrency() / 2, 2u, 8u);
		p.language = "en";
		p.detect_language = false;
		p.translate = false;
		p.no_context = true;
		p.no_timestamps = true;
		p.single_segment = false;
		p.print_progress = false;
		p.print_realtime = false;
		p.print_special = false;
		p.print_timestamps = false;
		p.suppress_blank = true;
		p.suppress_nst = true;
		p.temperature_inc = 0.0f; // no fallback re-decodes: keep latency predictable
		p.initial_prompt = prompt.empty() ? nullptr : prompt.c_str();
		if (whisper_full(ctx_, p, pcm, (int)samples) != 0)
			return {};
		std::string text;
		int n = whisper_full_n_segments(ctx_);
		for (int i = 0; i < n; i++) {
			const char *seg = whisper_full_get_segment_text(ctx_, i);
			if (seg)
				text += seg;
		}
		text = stripNonSpeech(text);
		// Trim and collapse whitespace.
		std::string out;
		bool space = false;
		for (char c : text) {
			if (std::isspace((unsigned char)c)) {
				space = !out.empty();
			} else {
				if (space)
					out += ' ';
				out += c;
				space = false;
			}
		}
		return out;
	}

private:
	whisper_context *ctx_ = nullptr;
	bool multilingual_ = false;
};

} // namespace

std::unique_ptr<SpeechEngine> createWhisperEngine()
{
	return std::make_unique<WhisperEngine>();
}

} // namespace ws
