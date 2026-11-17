// Word & Song for OBS — captures audio from an OBS source and transcribes it offline.
#pragma once

#include <QObject>
#include <QString>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct obs_source;
struct audio_data;
struct audio_resampler;

namespace ws {

class SpeechEngine;

class Listener : public QObject {
	Q_OBJECT
public:
	explicit Listener(QObject *parent = nullptr);
	~Listener() override;

	static bool available(); // false when built without Whisper

	bool start(const QString &audioSourceName, const QString &modelPath, QString *error);
	void stop();
	bool running() const { return running_; }

	// Vocabulary hint passed to Whisper (book names, current song lyrics). Thread-safe.
	void setPrompt(const std::string &prompt);
	// Audio level of the last captured block, 0..1 (for a meter). Thread-safe.
	float level() const { return level_; }

signals:
	void transcript(const QString &text);
	void status(const QString &message);

private:
	static void audioCallback(void *param, obs_source *source, const audio_data *audio, bool muted);
	void onAudio(const audio_data *audio, bool muted);
	void workerLoop(std::string modelPath);
	void detach();

	obs_source *source_ = nullptr;
	audio_resampler *resampler_ = nullptr;
	std::unique_ptr<SpeechEngine> engine_;
	std::thread worker_;
	std::atomic<bool> running_{false};
	std::atomic<float> level_{0.0f};

	std::mutex mutex_; // guards buffer_, total_, prompt_
	std::vector<float> buffer_;
	uint64_t total_ = 0; // total samples ever received
	std::string prompt_;
};

} // namespace ws
