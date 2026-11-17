// Word & Song for OBS — captures audio from an OBS source and transcribes it offline.
#include "listener.hpp"

#include "whisper-engine.hpp"

#include <obs-module.h>
#include <media-io/audio-resampler.h>

#include <QFileInfo>

#include <chrono>
#include <cmath>

namespace ws {
namespace {
constexpr int kRate = 16000;
constexpr size_t kWindow = 7 * kRate;   // seconds of audio Whisper sees each pass
constexpr size_t kStep = kRate * 5 / 2; // run every 2.5 s of new audio
constexpr size_t kKeep = 30 * kRate;    // ring buffer length
constexpr float kSilenceRms = 0.004f;   // skip passes quieter than this
} // namespace

Listener::Listener(QObject *parent) : QObject(parent) {}

Listener::~Listener()
{
	stop();
}

bool Listener::available()
{
#ifdef HAVE_WHISPER
	return true;
#else
	return false;
#endif
}

bool Listener::start(const QString &audioSourceName, const QString &modelPath, QString *error)
{
	stop();
	if (!available()) {
		if (error)
			*error = tr("This build was made without speech detection.");
		return false;
	}
	if (modelPath.isEmpty() || !QFileInfo::exists(modelPath)) {
		if (error)
			*error = tr("Choose a Whisper model file (for example ggml-base.en.bin) first.");
		return false;
	}
	obs_source_t *src = obs_get_source_by_name(audioSourceName.toUtf8().constData());
	if (!src) {
		if (error)
			*error = tr("Audio source \"%1\" was not found.").arg(audioSourceName);
		return false;
	}
	if (!(obs_source_get_output_flags(src) & OBS_SOURCE_AUDIO)) {
		obs_source_release(src);
		if (error)
			*error = tr("\"%1\" has no audio.").arg(audioSourceName);
		return false;
	}

	obs_audio_info oai;
	obs_get_audio_info(&oai);
	resample_info from = {};
	from.samples_per_sec = oai.samples_per_sec;
	from.format = AUDIO_FORMAT_FLOAT_PLANAR;
	from.speakers = oai.speakers;
	resample_info to = {};
	to.samples_per_sec = kRate;
	to.format = AUDIO_FORMAT_FLOAT;
	to.speakers = SPEAKERS_MONO;
	resampler_ = audio_resampler_create(&to, &from);
	if (!resampler_) {
		obs_source_release(src);
		if (error)
			*error = tr("Could not set up audio resampling.");
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(mutex_);
		buffer_.clear();
		total_ = 0;
	}
#ifdef HAVE_WHISPER
	engine_ = createWhisperEngine();
#endif
	running_ = true;
	source_ = src;
	obs_source_add_audio_capture_callback(source_, audioCallback, this);
	worker_ = std::thread(&Listener::workerLoop, this, modelPath.toStdString());
	return true;
}

void Listener::detach()
{
	if (source_) {
		obs_source_remove_audio_capture_callback(source_, audioCallback, this);
		obs_source_release(source_);
		source_ = nullptr;
	}
}

void Listener::stop()
{
	if (!running_ && !worker_.joinable() && !source_)
		return;
	running_ = false;
	detach();
	if (worker_.joinable())
		worker_.join();
	engine_.reset();
	if (resampler_) {
		audio_resampler_destroy(resampler_);
		resampler_ = nullptr;
	}
	level_ = 0.0f;
}

void Listener::setPrompt(const std::string &prompt)
{
	std::lock_guard<std::mutex> lock(mutex_);
	prompt_ = prompt;
}

void Listener::audioCallback(void *param, obs_source_t *, const audio_data *audio, bool muted)
{
	static_cast<Listener *>(param)->onAudio(audio, muted);
}

void Listener::onAudio(const audio_data *audio, bool muted)
{
	if (!running_ || !resampler_ || !audio || !audio->frames)
		return;
	uint8_t *out[MAX_AV_PLANES] = {};
	uint32_t outFrames = 0;
	uint64_t tsOffset = 0;
	if (!audio_resampler_resample(resampler_, out, &outFrames, &tsOffset, (const uint8_t *const *)audio->data,
				      audio->frames))
		return;
	const float *pcm = reinterpret_cast<const float *>(out[0]);
	double sum = 0.0;
	for (uint32_t i = 0; i < outFrames; i++)
		sum += (double)pcm[i] * pcm[i];
	level_ = outFrames ? (float)std::sqrt(sum / outFrames) : 0.0f;

	std::lock_guard<std::mutex> lock(mutex_);
	if (muted)
		buffer_.insert(buffer_.end(), outFrames, 0.0f);
	else
		buffer_.insert(buffer_.end(), pcm, pcm + outFrames);
	total_ += outFrames;
	if (buffer_.size() > kKeep)
		buffer_.erase(buffer_.begin(), buffer_.begin() + (ptrdiff_t)(buffer_.size() - kKeep));
}

void Listener::workerLoop(std::string modelPath)
{
	emit status(tr("Loading speech model…"));
	std::string err;
	if (!engine_ || !engine_->load(modelPath, err)) {
		running_ = false;
		emit status(QString::fromStdString(err.empty() ? "Speech engine unavailable." : err));
		return;
	}
	emit status(tr("Listening"));

	uint64_t lastRun = 0;
	std::vector<float> window;
	std::string prompt;
	while (running_) {
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
		{
			std::lock_guard<std::mutex> lock(mutex_);
			if (total_ - lastRun < kStep)
				continue;
			lastRun = total_;
			size_t n = std::min(buffer_.size(), kWindow);
			window.assign(buffer_.end() - (ptrdiff_t)n, buffer_.end());
			prompt = prompt_;
		}
		// Skip if the newest part of the window is silent.
		size_t tail = std::min(window.size(), kStep);
		double sum = 0.0;
		for (size_t i = window.size() - tail; i < window.size(); i++)
			sum += (double)window[i] * window[i];
		if (tail == 0 || std::sqrt(sum / (double)tail) < kSilenceRms)
			continue;
		// Whisper needs at least one second; pad short buffers with silence.
		if (window.size() < (size_t)kRate)
			window.insert(window.begin(), (size_t)kRate - window.size(), 0.0f);

		std::string text = engine_->transcribe(window.data(), window.size(), prompt);
		if (!text.empty() && running_)
			emit transcript(QString::fromStdString(text));
	}
}

} // namespace ws
