// Word & Song for OBS — the control dock.
#pragma once

#include "bible.hpp"
#include "live-state.hpp"
#include "matcher.hpp"
#include "songs.hpp"

#include <QElapsedTimer>
#include <QWidget>

#include <map>
#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTimer;
class QToolButton;

namespace ws {

class Listener;

class WordSongDock : public QWidget {
	Q_OBJECT
public:
	WordSongDock(const QString &configDir, QWidget *parent = nullptr);
	~WordSongDock() override;

	void saveAll();
	void shutdown();

public slots:
	void showPreview(); // send the previewed item to the screen
	void clearScreen();
	void next();
	void previous();

private slots:
	void onSearchEdited();
	void runSearch();
	void onBookChanged();
	void onChapterChanged();
	void onVerseSelection();
	void onTranslationChanged();
	void onSetlistSelection();
	void onSlideSelection();
	void onTranscript(const QString &text);
	void onListenStatus(const QString &message);
	void toggleListening();
	void refreshAudioSources();
	void tickMeter();

private:
	QWidget *buildScriptureTab();
	QWidget *buildLyricsTab();
	QWidget *buildListenTab();
	void buildMenu(QToolButton *button);

	// Scripture
	void loadChapter(int book, int chapter, int selectStart = 0, int selectEnd = 0);
	void previewVerses(const std::vector<const Verse *> &verses);
	LiveContent scriptureContent(const std::vector<const Verse *> &verses) const;
	void stepVerse(int delta);
	void importTranslation();

	// Lyrics
	void refreshSetlist(const QString &selectId = QString());
	void loadSlides(int selectIndex = 0);
	void previewSlide(int index);
	void editSong(const QString &id);
	void newSong();
	void importSongs();
	void addFromLibrary();
	void removeFromSetlist();
	void stepSlide(int delta);
	QString currentSongId() const;

	// Listening
	void handleSermon(const QString &text);
	void handleWorship(const QString &text);
	void addSuggestion(const QString &label, const LiveContent &content, int slideIndex = -1,
			   const QString &songId = QString());
	void updatePrompt();

	// Live / preview
	void setPreview(const LiveContent &content);
	void goLive(const LiveContent &content);
	void updateLiveLabel();
	void addDisplayToScene(bool fullScreen);
	void loadSettings();
	void saveSettings() const;

	QString configDir_;
	std::shared_ptr<Bible> bible_;
	SongLibrary songs_;
	std::unique_ptr<Listener> listener_;

	LiveContent preview_;
	LiveContent live_;
	enum class Mode { Scripture, Lyrics } mode_ = Mode::Scripture;

	// scripture state
	int book_ = 43, chapter_ = 3;
	bool showingSearchResults_ = false;

	// lyrics state
	std::vector<Slide> slides_;
	std::vector<std::vector<uint64_t>> slideBigrams_;
	int liveSlide_ = -1;
	QString liveSongId_;

	// listening state
	struct Suggestion {
		LiveContent content;
		int slide = -1;
		QString songId;
	};
	std::vector<Suggestion> suggestionData_;
	std::map<QString, qint64> recentRefs_;
	QElapsedTimer clock_;

	// widgets
	QLabel *liveLabel_ = nullptr;
	QPushButton *clearButton_ = nullptr;
	QTabWidget *tabs_ = nullptr;
	QLabel *previewTitle_ = nullptr;
	QLabel *previewBody_ = nullptr;
	QPushButton *showButton_ = nullptr;

	QComboBox *translation_ = nullptr;
	QLineEdit *search_ = nullptr;
	QTimer *searchTimer_ = nullptr;
	QComboBox *bookCombo_ = nullptr;
	QComboBox *chapterCombo_ = nullptr;
	QListWidget *verseList_ = nullptr;
	QCheckBox *showTranslation_ = nullptr;

	QListWidget *setlist_ = nullptr;
	QListWidget *slideList_ = nullptr;
	QSpinBox *linesPerSlide_ = nullptr;

	QComboBox *audioSource_ = nullptr;
	QLineEdit *modelPath_ = nullptr;
	QComboBox *listenMode_ = nullptr;
	QCheckBox *autoShow_ = nullptr;
	QPushButton *listenButton_ = nullptr;
	QProgressBar *meter_ = nullptr;
	QLabel *listenStatus_ = nullptr;
	QLabel *transcriptLabel_ = nullptr;
	QListWidget *suggestions_ = nullptr;
	QTimer *meterTimer_ = nullptr;
};

} // namespace ws
