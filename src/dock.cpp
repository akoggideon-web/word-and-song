// Word & Song for OBS — the control dock.
#include "dock.hpp"

#include "listener.hpp"
#include "lower-third-source.hpp"
#include "reference-parser.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace ws {
namespace {

constexpr int RoleBook = Qt::UserRole;
constexpr int RoleChapter = Qt::UserRole + 1;
constexpr int RoleVerse = Qt::UserRole + 2;
constexpr int RoleId = Qt::UserRole + 3;

const char *kModelsUrl = "https://huggingface.co/ggerganov/whisper.cpp/tree/main";

QString T(const char *s)
{
	return QString::fromUtf8(obs_module_text(s));
}

QString elide(const QString &s, int n)
{
	QString t = s.simplified();
	return t.size() > n ? t.left(n - 1) + QChar(0x2026) : t;
}

// Keeps keyboard focus in the search box useful: Up/Down move through the verse list.
class SearchEdit : public QLineEdit {
public:
	explicit SearchEdit(QListWidget *list, QWidget *parent = nullptr) : QLineEdit(parent), list_(list) {}

protected:
	void keyPressEvent(QKeyEvent *e) override
	{
		if ((e->key() == Qt::Key_Down || e->key() == Qt::Key_Up) && list_->count()) {
			int row = list_->currentRow() + (e->key() == Qt::Key_Down ? 1 : -1);
			list_->setCurrentRow(std::clamp(row, 0, list_->count() - 1));
			return;
		}
		QLineEdit::keyPressEvent(e);
	}

private:
	QListWidget *list_;
};

} // namespace

WordSongDock::WordSongDock(const QString &configDir, QWidget *parent)
	: QWidget(parent),
	  configDir_(configDir),
	  songs_(QDir(configDir).filePath(QStringLiteral("songs.json"))),
	  listener_(std::make_unique<Listener>())
{
	clock_.start();
	songs_.load();

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(6, 6, 6, 6);
	root->setSpacing(6);

	// Live bar
	auto *liveRow = new QHBoxLayout();
	liveLabel_ = new QLabel(this);
	liveLabel_->setTextFormat(Qt::RichText);
	liveLabel_->setMinimumWidth(80);
	liveRow->addWidget(liveLabel_, 1);
	clearButton_ = new QPushButton(T("Clear"), this);
	clearButton_->setToolTip(T("ClearTip"));
	clearButton_->setStyleSheet(QStringLiteral("QPushButton{background:#8b1e1e;color:white;font-weight:bold;"
						   "padding:4px 14px;border-radius:4px;}"
						   "QPushButton:disabled{background:#3a3a3a;color:#888;}"));
	connect(clearButton_, &QPushButton::clicked, this, &WordSongDock::clearScreen);
	liveRow->addWidget(clearButton_);
	auto *menuButton = new QToolButton(this);
	menuButton->setText(QStringLiteral("⋯"));
	menuButton->setPopupMode(QToolButton::InstantPopup);
	buildMenu(menuButton);
	liveRow->addWidget(menuButton);
	root->addLayout(liveRow);

	tabs_ = new QTabWidget(this);
	tabs_->addTab(buildScriptureTab(), T("Tab.Scripture"));
	tabs_->addTab(buildLyricsTab(), T("Tab.Lyrics"));
	tabs_->addTab(buildListenTab(), T("Tab.Listen"));
	connect(tabs_, &QTabWidget::currentChanged, this, [this](int i) {
		// The preview follows the tab you are working in.
		if (i == 0) {
			mode_ = Mode::Scripture;
			onVerseSelection();
		} else if (i == 1) {
			mode_ = Mode::Lyrics;
			onSlideSelection();
		}
	});
	root->addWidget(tabs_, 1);

	// Preview + primary action
	auto *previewBox = new QWidget(this);
	previewBox->setObjectName(QStringLiteral("wsPreview"));
	previewBox->setStyleSheet(QStringLiteral("#wsPreview{background:rgba(255,255,255,0.05);border-radius:6px;}"));
	auto *pv = new QVBoxLayout(previewBox);
	pv->setContentsMargins(8, 6, 8, 8);
	previewTitle_ = new QLabel(previewBox);
	previewTitle_->setStyleSheet(QStringLiteral("color:#f2c14e;font-weight:bold;"));
	previewBody_ = new QLabel(previewBox);
	previewBody_->setWordWrap(true);
	previewBody_->setMaximumHeight(70);
	previewBody_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	pv->addWidget(previewTitle_);
	pv->addWidget(previewBody_);
	auto *actions = new QHBoxLayout();
	auto *prevBtn = new QPushButton(QStringLiteral("◀"), previewBox);
	prevBtn->setToolTip(T("PreviousTip"));
	prevBtn->setFixedWidth(40);
	connect(prevBtn, &QPushButton::clicked, this, &WordSongDock::previous);
	showButton_ = new QPushButton(T("Show"), previewBox);
	showButton_->setToolTip(T("ShowTip"));
	showButton_->setStyleSheet(QStringLiteral("QPushButton{background:white;color:black;font-weight:bold;"
						  "padding:6px;border-radius:4px;}"
						  "QPushButton:disabled{background:#555;color:#999;}"));
	connect(showButton_, &QPushButton::clicked, this, &WordSongDock::showPreview);
	auto *nextBtn = new QPushButton(QStringLiteral("▶"), previewBox);
	nextBtn->setToolTip(T("NextTip"));
	nextBtn->setFixedWidth(40);
	connect(nextBtn, &QPushButton::clicked, this, &WordSongDock::next);
	actions->addWidget(prevBtn);
	actions->addWidget(showButton_, 1);
	actions->addWidget(nextBtn);
	pv->addLayout(actions);
	root->addWidget(previewBox);

	loadSettings();
	onTranslationChanged();
	refreshSetlist();
	refreshAudioSources();
	// Start with the preview of whichever tab was open last, so the Show hotkey works
	// before anyone touches the dock.
	if (mode_ == Mode::Lyrics)
		onSlideSelection();
	else
		onVerseSelection();
	updateLiveLabel();

	connect(listener_.get(), &Listener::transcript, this, &WordSongDock::onTranscript);
	connect(listener_.get(), &Listener::status, this, &WordSongDock::onListenStatus);
	meterTimer_ = new QTimer(this);
	connect(meterTimer_, &QTimer::timeout, this, &WordSongDock::tickMeter);
	meterTimer_->start(100);
}

WordSongDock::~WordSongDock()
{
	shutdown();
}

void WordSongDock::shutdown()
{
	if (listener_)
		listener_->stop();
}

void WordSongDock::saveAll()
{
	songs_.save();
	saveSettings();
}

void WordSongDock::buildMenu(QToolButton *button)
{
	auto *menu = new QMenu(button);
	menu->addAction(T("Menu.AddLowerThird"), this, [this] { addDisplayToScene(false); });
	menu->addAction(T("Menu.AddFullScreen"), this, [this] { addDisplayToScene(true); });
	menu->addSeparator();
	menu->addAction(T("Menu.ImportBible"), this, [this] { importTranslation(); });
	menu->addAction(T("Menu.ImportSongs"), this, [this] { importSongs(); });
	menu->addSeparator();
	menu->addAction(T("Menu.OpenDataFolder"), this,
			[this] { QDesktopServices::openUrl(QUrl::fromLocalFile(configDir_)); });
	button->setMenu(menu);
}

// ---------------------------------------------------------------------------
// Scripture

QWidget *WordSongDock::buildScriptureTab()
{
	auto *w = new QWidget(this);
	auto *v = new QVBoxLayout(w);
	v->setContentsMargins(4, 6, 4, 4);

	verseList_ = new QListWidget(w);
	verseList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
	verseList_->setWordWrap(true);
	verseList_->setUniformItemSizes(false);
	verseList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	auto *row1 = new QHBoxLayout();
	translation_ = new QComboBox(w);
	translation_->setToolTip(T("TranslationTip"));
	for (const QString &a : BibleLibrary::instance().abbrevs())
		translation_->addItem(a, a);
	translation_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
	search_ = new SearchEdit(verseList_, w);
	search_->setPlaceholderText(T("SearchPlaceholder"));
	search_->setClearButtonEnabled(true);
	row1->addWidget(translation_);
	row1->addWidget(search_, 1);
	v->addLayout(row1);

	auto *row2 = new QHBoxLayout();
	bookCombo_ = new QComboBox(w);
	for (int b = 1; b <= kBookCount; b++)
		bookCombo_->addItem(QString::fromStdString(bookName(b)), b);
	bookCombo_->setMaxVisibleItems(20);
	chapterCombo_ = new QComboBox(w);
	chapterCombo_->setMinimumContentsLength(3);
	showTranslation_ = new QCheckBox(T("ShowTranslation"), w);
	showTranslation_->setChecked(true);
	row2->addWidget(bookCombo_, 1);
	row2->addWidget(chapterCombo_);
	row2->addWidget(showTranslation_);
	v->addLayout(row2);
	v->addWidget(verseList_, 1);

	searchTimer_ = new QTimer(this);
	searchTimer_->setSingleShot(true);
	searchTimer_->setInterval(220);
	connect(searchTimer_, &QTimer::timeout, this, &WordSongDock::runSearch);
	connect(search_, &QLineEdit::textEdited, this, &WordSongDock::onSearchEdited);
	connect(search_, &QLineEdit::returnPressed, this, [this] {
		runSearch();
		showPreview();
	});
	connect(translation_, &QComboBox::currentIndexChanged, this, &WordSongDock::onTranslationChanged);
	connect(bookCombo_, &QComboBox::currentIndexChanged, this, &WordSongDock::onBookChanged);
	connect(chapterCombo_, &QComboBox::currentIndexChanged, this, &WordSongDock::onChapterChanged);
	connect(verseList_, &QListWidget::itemSelectionChanged, this, &WordSongDock::onVerseSelection);
	connect(verseList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) { showPreview(); });
	connect(verseList_, &QListWidget::itemActivated, this, [this](QListWidgetItem *) { showPreview(); });
	connect(showTranslation_, &QCheckBox::toggled, this, [this] { onVerseSelection(); });
	return w;
}

void WordSongDock::onTranslationChanged()
{
	QString abbrev = translation_->currentData().toString();
	bible_ = BibleLibrary::instance().get(abbrev);
	if (!bible_) {
		verseList_->clear();
		verseList_->addItem(T("NoBible"));
		return;
	}
	// Remember the selected verses and reload them in the new translation.
	int sel = 0, selEnd = 0;
	for (auto *it : verseList_->selectedItems()) {
		int vv = it->data(RoleVerse).toInt();
		sel = sel ? std::min(sel, vv) : vv;
		selEnd = std::max(selEnd, vv);
	}
	if (showingSearchResults_)
		runSearch();
	else
		loadChapter(book_, chapter_, sel, selEnd);
	if (live_.visible && live_.kind == ContentKind::Scripture)
		updateLiveLabel();
}

void WordSongDock::onSearchEdited()
{
	searchTimer_->start();
}

void WordSongDock::runSearch()
{
	searchTimer_->stop();
	if (!bible_)
		return;
	QString q = search_->text().trimmed();
	if (q.isEmpty()) {
		if (showingSearchResults_)
			loadChapter(book_, chapter_);
		return;
	}
	if (auto ref = parseTyped(q.toStdString())) {
		int ch = ref->chapter ? ref->chapter : 1;
		loadChapter(ref->book, ch, ref->verseStart, ref->verseEnd);
		return;
	}
	if (q.size() < 3)
		return;
	auto results = bible_->search(q, 200);
	showingSearchResults_ = true;
	QSignalBlocker block(verseList_);
	verseList_->clear();
	for (const Verse *v : results) {
		Reference r{v->book, v->chapter, v->verse, v->verse};
		auto *item = new QListWidgetItem(
			QStringLiteral("%1  %2").arg(QString::fromStdString(formatReference(r, true)), v->text),
			verseList_);
		item->setData(RoleBook, v->book);
		item->setData(RoleChapter, v->chapter);
		item->setData(RoleVerse, v->verse);
	}
	if (results.empty())
		verseList_->addItem(T("NoResults"));
	else
		verseList_->setCurrentRow(0);
	block.unblock();
	onVerseSelection();
}

void WordSongDock::loadChapter(int book, int chapter, int selectStart, int selectEnd)
{
	if (!bible_)
		return;
	book_ = book;
	chapter_ = std::max(1, chapter);
	showingSearchResults_ = false;
	{
		QSignalBlocker b1(bookCombo_), b2(chapterCombo_);
		bookCombo_->setCurrentIndex(book_ - 1);
		int n = std::max(1, bible_->chapterCountFor(book_));
		if (chapterCombo_->count() != n) {
			chapterCombo_->clear();
			for (int c = 1; c <= n; c++)
				chapterCombo_->addItem(QString::number(c), c);
		}
		chapter_ = std::min(chapter_, n);
		chapterCombo_->setCurrentIndex(chapter_ - 1);
	}
	QSignalBlocker block(verseList_);
	verseList_->clear();
	QListWidgetItem *first = nullptr;
	for (const Verse *v : bible_->chapter(book_, chapter_)) {
		auto *item = new QListWidgetItem(QStringLiteral("%1  %2").arg(v->verse).arg(v->text), verseList_);
		item->setData(RoleBook, v->book);
		item->setData(RoleChapter, v->chapter);
		item->setData(RoleVerse, v->verse);
		int end = std::max(selectStart, selectEnd);
		if (selectStart && v->verse >= selectStart && v->verse <= end) {
			item->setSelected(true);
			if (!first)
				first = item;
		}
	}
	if (first) {
		verseList_->setCurrentItem(first, QItemSelectionModel::NoUpdate);
		verseList_->scrollToItem(first, QAbstractItemView::PositionAtCenter);
	} else {
		verseList_->scrollToTop();
	}
	block.unblock();
	onVerseSelection();
}

void WordSongDock::onBookChanged()
{
	loadChapter(bookCombo_->currentData().toInt(), 1);
}

void WordSongDock::onChapterChanged()
{
	if (chapterCombo_->currentIndex() >= 0)
		loadChapter(book_, chapterCombo_->currentData().toInt());
}

void WordSongDock::onVerseSelection()
{
	if (!bible_)
		return;
	std::vector<const Verse *> verses;
	auto items = verseList_->selectedItems();
	std::sort(items.begin(), items.end(),
		  [this](QListWidgetItem *a, QListWidgetItem *b) { return verseList_->row(a) < verseList_->row(b); });
	for (auto *it : items) {
		const Verse *v = bible_->verse(it->data(RoleBook).toInt(), it->data(RoleChapter).toInt(),
					       it->data(RoleVerse).toInt());
		if (v)
			verses.push_back(v);
	}
	previewVerses(verses);
}

LiveContent WordSongDock::scriptureContent(const std::vector<const Verse *> &verses) const
{
	LiveContent c;
	if (verses.empty())
		return c;
	c.kind = ContentKind::Scripture;
	c.visible = true;
	const Verse *a = verses.front();
	const Verse *z = verses.back();
	Reference r{a->book, a->chapter, a->verse, a->chapter == z->chapter ? z->verse : a->verse};
	QString title = QString::fromStdString(formatReference(r));
	if (showTranslation_->isChecked() && bible_)
		title += QStringLiteral(" (%1)").arg(bible_->abbrev());
	c.title = title.toStdString();
	QString body;
	for (const Verse *v : verses) {
		if (!body.isEmpty())
			body += ' ';
		if (verses.size() > 1)
			body += QString::number(v->verse) + QChar(0x00A0);
		body += v->text;
	}
	c.body = body.toStdString();
	return c;
}

void WordSongDock::previewVerses(const std::vector<const Verse *> &verses)
{
	// The lyrics tab owns the preview while it is open.
	if (mode_ == Mode::Scripture)
		setPreview(scriptureContent(verses));
}

void WordSongDock::stepVerse(int delta)
{
	if (!bible_ || showingSearchResults_) {
		int row = verseList_->currentRow() + delta;
		if (row >= 0 && row < verseList_->count()) {
			verseList_->clearSelection();
			verseList_->setCurrentRow(row);
		}
		return;
	}
	auto items = verseList_->selectedItems();
	int cur = 0;
	for (auto *it : items)
		cur = delta > 0 ? std::max(cur, it->data(RoleVerse).toInt())
				: (cur ? std::min(cur, it->data(RoleVerse).toInt()) : it->data(RoleVerse).toInt());
	int target = cur + delta;
	auto verses = bible_->chapter(book_, chapter_);
	if (!cur) {
		target = 1;
	} else if (target < 1 || target > (int)verses.size()) {
		// Cross into the neighbouring chapter (and book).
		int b = book_, c = chapter_ + (delta > 0 ? 1 : -1);
		if (c < 1) {
			if (--b < 1)
				return;
			c = bible_->chapterCountFor(b);
		} else if (c > bible_->chapterCountFor(b)) {
			if (++b > kBookCount)
				return;
			c = 1;
		}
		int v = delta > 0 ? 1 : (int)bible_->chapter(b, c).size();
		loadChapter(b, c, v, v);
		return;
	}
	loadChapter(book_, chapter_, target, target);
}

void WordSongDock::importTranslation()
{
	QString file = QFileDialog::getOpenFileName(this, T("Import.Title"), QString(),
						    T("Import.Filter") + QStringLiteral(" (*.tsv *.txt *.csv *.json)"));
	if (file.isEmpty())
		return;
	bool ok = false;
	QString abbrev =
		QInputDialog::getText(this, T("Import.Title"), T("Import.Abbrev"), QLineEdit::Normal, QString(), &ok);
	if (!ok || abbrev.trimmed().isEmpty())
		return;
	QString name = QInputDialog::getText(this, T("Import.Title"), T("Import.Name"), QLineEdit::Normal,
					     abbrev.trimmed().toUpper(), &ok);
	if (!ok)
		return;
	QString err;
	if (!BibleLibrary::instance().importFile(file, abbrev, name, &err)) {
		QMessageBox::warning(this, T("Import.Title"), err);
		return;
	}
	QString key = abbrev.trimmed().toUpper();
	key.remove(QRegularExpression(QStringLiteral("[^A-Z0-9_-]")));
	if (translation_->findData(key) < 0)
		translation_->addItem(key, key);
	translation_->setCurrentIndex(translation_->findData(key));
	QMessageBox::information(this, T("Import.Title"), T("Import.Done").arg(key));
}

// ---------------------------------------------------------------------------
// Lyrics

QWidget *WordSongDock::buildLyricsTab()
{
	auto *w = new QWidget(this);
	auto *v = new QVBoxLayout(w);
	v->setContentsMargins(4, 6, 4, 4);

	auto *bar = new QHBoxLayout();
	auto *newBtn = new QToolButton(w);
	newBtn->setText(T("Song.New"));
	connect(newBtn, &QToolButton::clicked, this, &WordSongDock::newSong);
	auto *addBtn = new QToolButton(w);
	addBtn->setText(T("Song.FromLibrary"));
	connect(addBtn, &QToolButton::clicked, this, &WordSongDock::addFromLibrary);
	auto *importBtn = new QToolButton(w);
	importBtn->setText(T("Song.Import"));
	connect(importBtn, &QToolButton::clicked, this, &WordSongDock::importSongs);
	auto *editBtn = new QToolButton(w);
	editBtn->setText(T("Song.Edit"));
	connect(editBtn, &QToolButton::clicked, this, [this] {
		if (!currentSongId().isEmpty())
			editSong(currentSongId());
	});
	auto *removeBtn = new QToolButton(w);
	removeBtn->setText(QStringLiteral("−"));
	removeBtn->setToolTip(T("Song.RemoveTip"));
	connect(removeBtn, &QToolButton::clicked, this, &WordSongDock::removeFromSetlist);
	linesPerSlide_ = new QSpinBox(w);
	linesPerSlide_->setRange(0, 8);
	linesPerSlide_->setSpecialValueText(T("Song.AsWritten"));
	linesPerSlide_->setSuffix(T("Song.LinesSuffix"));
	linesPerSlide_->setToolTip(T("Song.LinesTip"));
	linesPerSlide_->setValue(songs_.linesPerSlide);
	connect(linesPerSlide_, &QSpinBox::valueChanged, this, [this](int n) {
		songs_.linesPerSlide = n;
		songs_.save();
		loadSlides(0);
	});
	bar->addWidget(newBtn);
	bar->addWidget(addBtn);
	bar->addWidget(importBtn);
	bar->addWidget(editBtn);
	bar->addWidget(removeBtn);
	bar->addStretch(1);
	bar->addWidget(linesPerSlide_);
	v->addLayout(bar);

	auto *split = new QSplitter(Qt::Vertical, w);
	setlist_ = new QListWidget(split);
	setlist_->setDragDropMode(QAbstractItemView::InternalMove);
	setlist_->setToolTip(T("Setlist.Tip"));
	slideList_ = new QListWidget(split);
	slideList_->setWordWrap(true);
	slideList_->setSpacing(2);
	slideList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	split->addWidget(setlist_);
	split->addWidget(slideList_);
	split->setStretchFactor(0, 1);
	split->setStretchFactor(1, 3);
	v->addWidget(split, 1);

	connect(setlist_, &QListWidget::currentRowChanged, this, &WordSongDock::onSetlistSelection);
	connect(setlist_->model(), &QAbstractItemModel::rowsMoved, this, [this] {
		songs_.setlist.clear();
		for (int i = 0; i < setlist_->count(); i++)
			songs_.setlist << setlist_->item(i)->data(RoleId).toString();
		songs_.save();
	});
	connect(setlist_, &QListWidget::itemDoubleClicked, this,
		[this](QListWidgetItem *it) { editSong(it->data(RoleId).toString()); });
	connect(slideList_, &QListWidget::currentRowChanged, this, &WordSongDock::onSlideSelection);
	connect(slideList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) { showPreview(); });
	connect(slideList_, &QListWidget::itemActivated, this, [this](QListWidgetItem *) { showPreview(); });
	return w;
}

QString WordSongDock::currentSongId() const
{
	auto *it = setlist_->currentItem();
	return it ? it->data(RoleId).toString() : QString();
}

void WordSongDock::refreshSetlist(const QString &selectId)
{
	QString keep = selectId.isEmpty() ? currentSongId() : selectId;
	QSignalBlocker block(setlist_);
	setlist_->clear();
	int row = 0, idx = 0;
	for (const QString &id : songs_.setlist) {
		const Song *s = songs_.find(id);
		if (!s)
			continue;
		auto *item = new QListWidgetItem(QStringLiteral("%1. %2").arg(idx + 1).arg(s->title), setlist_);
		item->setData(RoleId, id);
		if (id == keep)
			row = idx;
		idx++;
	}
	if (!setlist_->count()) {
		auto *hint = new QListWidgetItem(T("Setlist.Empty"), setlist_);
		hint->setFlags(Qt::NoItemFlags);
	} else {
		setlist_->setCurrentRow(row);
	}
	block.unblock();
	onSetlistSelection();
}

void WordSongDock::onSetlistSelection()
{
	loadSlides(0);
}

void WordSongDock::loadSlides(int selectIndex)
{
	slides_.clear();
	slideBigrams_.clear();
	QSignalBlocker block(slideList_);
	slideList_->clear();
	const Song *s = songs_.find(currentSongId());
	if (s) {
		slides_ = splitSlides(s->text.toStdString(), songs_.linesPerSlide);
		for (size_t i = 0; i < slides_.size(); i++) {
			const Slide &sl = slides_[i];
			QString label = QString::fromStdString(sl.label);
			QString text = QString::fromStdString(sl.text);
			auto *item = new QListWidgetItem(label.isEmpty() || !sl.sectionStart
								 ? text
								 : QStringLiteral("%1\n%2").arg(label.toUpper(), text),
							 slideList_);
			if (sl.sectionStart && !label.isEmpty()) {
				QFont f = item->font();
				f.setBold(false);
				item->setFont(f);
			}
			item->setToolTip(label);
			slideBigrams_.push_back(bigramHashes(words(sl.text)));
		}
	}
	if (!slides_.empty())
		slideList_->setCurrentRow(std::clamp(selectIndex, 0, (int)slides_.size() - 1));
	block.unblock();
	onSlideSelection();
	updatePrompt();
}

void WordSongDock::onSlideSelection()
{
	previewSlide(slideList_->currentRow());
}

void WordSongDock::previewSlide(int index)
{
	if (mode_ != Mode::Lyrics)
		return;
	if (index < 0 || index >= (int)slides_.size()) {
		setPreview({});
		return;
	}
	LiveContent c;
	c.kind = ContentKind::Lyrics;
	c.visible = true;
	c.title = slides_[(size_t)index].label;
	c.body = slides_[(size_t)index].text;
	setPreview(c);
}

void WordSongDock::stepSlide(int delta)
{
	int row = slideList_->currentRow() + delta;
	if (row >= 0 && row < slideList_->count()) {
		slideList_->setCurrentRow(row);
		return;
	}
	// Past the end of a song: move to the neighbouring song in the setlist.
	int songRow = setlist_->currentRow() + (delta > 0 ? 1 : -1);
	if (songRow >= 0 && songRow < setlist_->count() && (setlist_->item(songRow)->flags() & Qt::ItemIsEnabled)) {
		setlist_->setCurrentRow(songRow);
		if (delta < 0)
			slideList_->setCurrentRow(slideList_->count() - 1);
	}
}

static bool songDialog(QWidget *parent, Song &song, bool &deleteRequested)
{
	QDialog dlg(parent);
	dlg.setWindowTitle(song.id.isEmpty() ? T("Song.NewTitle") : T("Song.EditTitle"));
	dlg.resize(520, 620);
	auto *v = new QVBoxLayout(&dlg);
	auto *form = new QFormLayout();
	auto *title = new QLineEdit(song.title, &dlg);
	auto *author = new QLineEdit(song.author, &dlg);
	form->addRow(T("Song.Name"), title);
	form->addRow(T("Song.Author"), author);
	v->addLayout(form);
	auto *help = new QLabel(T("Song.Help"), &dlg);
	help->setWordWrap(true);
	help->setStyleSheet(QStringLiteral("color:#999;"));
	v->addWidget(help);
	auto *text = new QPlainTextEdit(song.text, &dlg);
	text->setPlaceholderText(
		QStringLiteral("[Verse 1]\nAmazing grace how sweet the sound\nThat saved a wretch like me\n\n"
			       "[Chorus]\n..."));
	v->addWidget(text, 1);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
	if (!song.id.isEmpty()) {
		auto *del = buttons->addButton(T("Song.Delete"), QDialogButtonBox::DestructiveRole);
		QObject::connect(del, &QPushButton::clicked, &dlg, [&] {
			if (QMessageBox::question(&dlg, T("Song.Delete"), T("Song.DeleteConfirm").arg(song.title)) ==
			    QMessageBox::Yes) {
				deleteRequested = true;
				dlg.reject();
			}
		});
	}
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	v->addWidget(buttons);
	if (dlg.exec() != QDialog::Accepted)
		return false;
	song.title = title->text().trimmed().isEmpty() ? T("Song.Untitled") : title->text().trimmed();
	song.author = author->text().trimmed();
	song.text = text->toPlainText();
	return true;
}

void WordSongDock::newSong()
{
	Song s;
	bool del = false;
	if (!songDialog(this, s, del))
		return;
	QString id = songs_.add(s);
	songs_.setlist << id;
	songs_.save();
	refreshSetlist(id);
}

void WordSongDock::editSong(const QString &id)
{
	Song *s = songs_.find(id);
	if (!s)
		return;
	Song copy = *s;
	bool del = false;
	bool saved = songDialog(this, copy, del);
	if (del) {
		songs_.remove(id);
		songs_.save();
		refreshSetlist();
		return;
	}
	if (!saved)
		return;
	*songs_.find(id) = copy;
	songs_.sort();
	songs_.save();
	int keepSlide = slideList_->currentRow();
	refreshSetlist(id);
	loadSlides(keepSlide);
}

void WordSongDock::importSongs()
{
	QStringList files = QFileDialog::getOpenFileNames(this, T("Song.ImportTitle"), QString(),
							  T("Song.ImportFilter") +
								  QStringLiteral(" (*.txt *.cho *.chordpro *.pro)"));
	if (files.isEmpty())
		return;
	QStringList errors;
	QString lastId;
	for (const QString &f : files) {
		Song s;
		QString err;
		if (!SongLibrary::importText(f, s, &err)) {
			errors << err;
			continue;
		}
		lastId = songs_.add(s);
		songs_.setlist << lastId;
	}
	songs_.save();
	refreshSetlist(lastId);
	if (!errors.isEmpty())
		QMessageBox::warning(this, T("Song.ImportTitle"), errors.join('\n'));
}

void WordSongDock::addFromLibrary()
{
	QMenu menu(this);
	for (const Song &s : songs_.songs) {
		QAction *a =
			menu.addAction(s.author.isEmpty() ? s.title : QStringLiteral("%1 — %2").arg(s.title, s.author));
		a->setData(s.id);
		a->setEnabled(!songs_.setlist.contains(s.id));
	}
	if (songs_.songs.empty())
		menu.addAction(T("Library.Empty"))->setEnabled(false);
	QAction *chosen = menu.exec(QCursor::pos());
	if (!chosen || chosen->data().toString().isEmpty())
		return;
	songs_.setlist << chosen->data().toString();
	songs_.save();
	refreshSetlist(chosen->data().toString());
}

void WordSongDock::removeFromSetlist()
{
	QString id = currentSongId();
	if (id.isEmpty())
		return;
	songs_.setlist.removeAll(id);
	songs_.save();
	refreshSetlist();
}

// ---------------------------------------------------------------------------
// Listening

QWidget *WordSongDock::buildListenTab()
{
	auto *w = new QWidget(this);
	auto *v = new QVBoxLayout(w);
	v->setContentsMargins(4, 6, 4, 4);

	if (!Listener::available()) {
		auto *l = new QLabel(T("Listen.Unavailable"), w);
		l->setWordWrap(true);
		v->addWidget(l);
		v->addStretch(1);
		return w;
	}

	auto *form = new QFormLayout();
	auto *srcRow = new QHBoxLayout();
	audioSource_ = new QComboBox(w);
	audioSource_->setToolTip(T("Listen.SourceTip"));
	auto *refresh = new QToolButton(w);
	refresh->setText(QStringLiteral("↻"));
	connect(refresh, &QToolButton::clicked, this, &WordSongDock::refreshAudioSources);
	srcRow->addWidget(audioSource_, 1);
	srcRow->addWidget(refresh);
	form->addRow(T("Listen.Source"), srcRow);

	auto *modelRow = new QHBoxLayout();
	modelPath_ = new QLineEdit(w);
	modelPath_->setPlaceholderText(QStringLiteral("ggml-base.en.bin"));
	auto *browse = new QToolButton(w);
	browse->setText(QStringLiteral("…"));
	connect(browse, &QToolButton::clicked, this, [this] {
		QString f = QFileDialog::getOpenFileName(this, T("Listen.Model"), QString(),
							 QStringLiteral("Whisper model (*.bin)"));
		if (!f.isEmpty()) {
			modelPath_->setText(f);
			saveSettings();
		}
	});
	auto *get = new QToolButton(w);
	get->setText(T("Listen.GetModel"));
	get->setToolTip(T("Listen.GetModelTip"));
	connect(get, &QToolButton::clicked, this,
		[] { QDesktopServices::openUrl(QUrl(QString::fromLatin1(kModelsUrl))); });
	modelRow->addWidget(modelPath_, 1);
	modelRow->addWidget(browse);
	modelRow->addWidget(get);
	form->addRow(T("Listen.Model"), modelRow);

	listenMode_ = new QComboBox(w);
	listenMode_->addItem(T("Listen.ModeSermon"), 0);
	listenMode_->addItem(T("Listen.ModeWorship"), 1);
	connect(listenMode_, &QComboBox::currentIndexChanged, this, [this] { updatePrompt(); });
	form->addRow(T("Listen.Mode"), listenMode_);
	v->addLayout(form);

	autoShow_ = new QCheckBox(T("Listen.AutoShow"), w);
	autoShow_->setToolTip(T("Listen.AutoShowTip"));
	v->addWidget(autoShow_);

	auto *row = new QHBoxLayout();
	listenButton_ = new QPushButton(T("Listen.Start"), w);
	listenButton_->setCheckable(true);
	connect(listenButton_, &QPushButton::clicked, this, &WordSongDock::toggleListening);
	meter_ = new QProgressBar(w);
	meter_->setRange(0, 100);
	meter_->setTextVisible(false);
	meter_->setMaximumHeight(8);
	row->addWidget(listenButton_);
	row->addWidget(meter_, 1);
	v->addLayout(row);

	listenStatus_ = new QLabel(T("Listen.Idle"), w);
	listenStatus_->setStyleSheet(QStringLiteral("color:#999;"));
	v->addWidget(listenStatus_);
	transcriptLabel_ = new QLabel(w);
	transcriptLabel_->setWordWrap(true);
	transcriptLabel_->setStyleSheet(QStringLiteral("font-style:italic;color:#bbb;"));
	transcriptLabel_->setMinimumHeight(36);
	v->addWidget(transcriptLabel_);

	auto *sugLabel = new QLabel(T("Listen.Suggestions"), w);
	sugLabel->setStyleSheet(QStringLiteral("font-weight:bold;"));
	v->addWidget(sugLabel);
	suggestions_ = new QListWidget(w);
	suggestions_->setWordWrap(true);
	suggestions_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	suggestions_->setToolTip(T("Listen.SuggestionsTip"));
	v->addWidget(suggestions_, 1);
	connect(suggestions_, &QListWidget::currentRowChanged, this, [this](int row) {
		if (row < 0)
			return;
		int idx = suggestions_->item(row)->data(RoleId).toInt();
		if (idx >= 0 && idx < (int)suggestionData_.size())
			setPreview(suggestionData_[(size_t)idx].content);
	});
	auto activate = [this](QListWidgetItem *it) {
		int idx = it->data(RoleId).toInt();
		if (idx < 0 || idx >= (int)suggestionData_.size())
			return;
		const Suggestion &s = suggestionData_[(size_t)idx];
		if (s.slide >= 0 && s.songId == currentSongId())
			slideList_->setCurrentRow(s.slide);
		goLive(s.content);
	};
	connect(suggestions_, &QListWidget::itemDoubleClicked, this, activate);
	connect(suggestions_, &QListWidget::itemActivated, this, activate);
	return w;
}

void WordSongDock::refreshAudioSources()
{
	if (!audioSource_)
		return;
	QString keep = audioSource_->currentText();
	if (keep.isEmpty())
		keep = audioSource_->property("saved").toString();
	audioSource_->clear();
	QStringList names;
	obs_enum_sources(
		[](void *param, obs_source_t *src) {
			if (obs_source_get_output_flags(src) & OBS_SOURCE_AUDIO)
				static_cast<QStringList *>(param)->append(QString::fromUtf8(obs_source_get_name(src)));
			return true;
		},
		&names);
	// Global audio devices (Desktop Audio, Mic/Aux) live in output channels 1-6.
	for (uint32_t ch = 1; ch <= 6; ch++) {
		obs_source_t *src = obs_get_output_source(ch);
		if (src) {
			QString n = QString::fromUtf8(obs_source_get_name(src));
			if (!names.contains(n))
				names.prepend(n);
			obs_source_release(src);
		}
	}
	audioSource_->addItems(names);
	int idx = audioSource_->findText(keep);
	if (idx >= 0)
		audioSource_->setCurrentIndex(idx);
}

void WordSongDock::toggleListening()
{
	if (listener_->running()) {
		listener_->stop();
		listenButton_->setChecked(false);
		listenButton_->setText(T("Listen.Start"));
		listenStatus_->setText(T("Listen.Idle"));
		return;
	}
	QString err;
	updatePrompt();
	if (!listener_->start(audioSource_->currentText(), modelPath_->text().trimmed(), &err)) {
		listenButton_->setChecked(false);
		QMessageBox::warning(this, T("Tab.Listen"), err);
		return;
	}
	saveSettings();
	listenButton_->setChecked(true);
	listenButton_->setText(T("Listen.Stop"));
}

void WordSongDock::onListenStatus(const QString &message)
{
	if (!listenStatus_)
		return;
	listenStatus_->setText(message);
	if (!listener_->running()) {
		listenButton_->setChecked(false);
		listenButton_->setText(T("Listen.Start"));
	}
}

void WordSongDock::tickMeter()
{
	if (meter_)
		meter_->setValue(listener_->running() ? std::min(100, (int)(listener_->level() * 400.0f)) : 0);
}

void WordSongDock::updatePrompt()
{
	if (!listenMode_)
		return;
	std::string prompt;
	if (listenMode_->currentData().toInt() == 1) {
		// Worship: the lyrics around the current slide help Whisper hear the words.
		int cur = std::max(0, slideList_->currentRow());
		for (int i = cur; i < (int)slides_.size() && prompt.size() < 600; i++)
			prompt += slides_[(size_t)i].text + "\n";
	} else {
		prompt = "Sermon with Bible references such as John 3:16, Romans 8:28, 1 Corinthians 13:4, "
			 "Psalm 23, Philippians 4:13, Ephesians 2:8-9, Hebrews 11:1, Isaiah 40:31.";
	}
	listener_->setPrompt(prompt);
}

void WordSongDock::onTranscript(const QString &text)
{
	if (transcriptLabel_)
		transcriptLabel_->setText(QStringLiteral("“%1”").arg(elide(text, 220)));
	if (listenMode_ && listenMode_->currentData().toInt() == 1)
		handleWorship(text);
	else
		handleSermon(text);
}

void WordSongDock::handleSermon(const QString &text)
{
	if (!bible_)
		return;
	const qint64 now = clock_.elapsed();
	// Forget references after 45 s so a re-read verse can be suggested again.
	for (auto it = recentRefs_.begin(); it != recentRefs_.end();)
		it = now - it->second > 45000 ? recentRefs_.erase(it) : std::next(it);

	bool found = false;
	for (const Reference &ref : findSpoken(text.toStdString())) {
		auto verses = bible_->lookup(ref);
		if (verses.empty())
			continue;
		found = true;
		if (!ref.verseStart && verses.size() > 1)
			verses.resize(1); // "John chapter 3" -> start at verse 1
		QString key = QString::fromStdString(formatReference(ref));
		if (recentRefs_.count(key))
			continue;
		recentRefs_[key] = now;
		addSuggestion(key, scriptureContent(verses));
	}
	if (found)
		return;
	// No spoken reference: is the speaker quoting a verse?
	auto heard = bigramHashes(words(text.toStdString()));
	if (const Verse *v = bible_->matchQuote(heard)) {
		Reference r{v->book, v->chapter, v->verse, v->verse};
		QString key = QString::fromStdString(formatReference(r));
		if (!recentRefs_.count(key)) {
			recentRefs_[key] = now;
			addSuggestion(key + QStringLiteral("  ") + T("Listen.Quote"), scriptureContent({v}));
		}
	}
}

void WordSongDock::handleWorship(const QString &text)
{
	if (slides_.empty())
		return;
	// Only the most recent words matter for "where are we in the song".
	auto w = words(text.toStdString());
	if (w.size() > 18)
		w.erase(w.begin(), w.end() - 18);
	auto heard = bigramHashes(w);
	if (heard.size() < 3)
		return;

	int cur = std::max(0, slideList_->currentRow());
	std::vector<int> candidates = {cur, cur + 1, cur + 2};
	for (int i = 0; i < (int)slides_.size(); i++)
		if (slides_[(size_t)i].sectionStart)
			candidates.push_back(i);

	double curScore = bigramCoverage(heard, slideBigrams_[(size_t)cur]);
	int best = -1;
	double bestScore = 0.0;
	for (int c : candidates) {
		if (c < 0 || c >= (int)slides_.size() || c == cur)
			continue;
		double s = bigramCoverage(heard, slideBigrams_[(size_t)c]);
		// Prefer the next slide slightly: songs mostly move forward.
		if (c == cur + 1)
			s += 0.05;
		if (s > bestScore) {
			bestScore = s;
			best = c;
		}
	}
	if (best < 0 || bestScore < 0.5 || bestScore < curScore + 0.15)
		return;
	const Slide &sl = slides_[(size_t)best];
	LiveContent c;
	c.kind = ContentKind::Lyrics;
	c.visible = true;
	c.title = sl.label;
	c.body = sl.text;
	QString label =
		QStringLiteral("%1: %2").arg(sl.label.empty() ? T("Listen.Slide") : QString::fromStdString(sl.label),
					     elide(QString::fromStdString(sl.text), 60));
	addSuggestion(label, c, best, currentSongId());
}

void WordSongDock::addSuggestion(const QString &label, const LiveContent &content, int slideIndex,
				 const QString &songId)
{
	if (!suggestions_)
		return;
	// Don't repeat the newest suggestion.
	if (!suggestionData_.empty()) {
		const auto &last = suggestionData_.back();
		if (last.content.body == content.body && last.content.title == content.title)
			return;
	}
	suggestionData_.push_back({content, slideIndex, songId});
	auto *item = new QListWidgetItem(
		QStringLiteral("%1\n%2").arg(label, elide(QString::fromStdString(content.body), 90)));
	item->setData(RoleId, (int)suggestionData_.size() - 1);
	suggestions_->insertItem(0, item);
	while (suggestions_->count() > 30)
		delete suggestions_->takeItem(suggestions_->count() - 1);

	if (autoShow_ && autoShow_->isChecked()) {
		if (slideIndex >= 0 && songId == currentSongId())
			slideList_->setCurrentRow(slideIndex);
		goLive(content);
	} else {
		QSignalBlocker b(suggestions_);
		suggestions_->setCurrentRow(0);
	}
}

// ---------------------------------------------------------------------------
// Preview / live

void WordSongDock::setPreview(const LiveContent &content)
{
	preview_ = content;
	if (!previewTitle_)
		return;
	bool has = content.kind != ContentKind::None && !content.body.empty();
	previewTitle_->setText(has ? QString::fromStdString(content.title) : T("Preview.Nothing"));
	previewBody_->setText(has ? elide(QString::fromStdString(content.body), 260) : QString());
	showButton_->setEnabled(has);
}

void WordSongDock::showPreview()
{
	if (preview_.kind == ContentKind::None || preview_.body.empty())
		return;
	goLive(preview_);
}

void WordSongDock::goLive(const LiveContent &content)
{
	live_ = content;
	live_.visible = true;
	setLive(live_);
	if (content.kind == ContentKind::Lyrics) {
		liveSongId_ = currentSongId();
		liveSlide_ = slideList_->currentRow();
		updatePrompt();
	}
	updateLiveLabel();
}

void WordSongDock::clearScreen()
{
	live_.visible = false;
	clearLive();
	updateLiveLabel();
}

void WordSongDock::next()
{
	if (mode_ == Mode::Lyrics)
		stepSlide(1);
	else
		stepVerse(1);
	// If something of this kind is on screen, follow along live.
	if (live_.visible && ((mode_ == Mode::Lyrics && live_.kind == ContentKind::Lyrics) ||
			      (mode_ == Mode::Scripture && live_.kind == ContentKind::Scripture)))
		showPreview();
}

void WordSongDock::previous()
{
	if (mode_ == Mode::Lyrics)
		stepSlide(-1);
	else
		stepVerse(-1);
	if (live_.visible && ((mode_ == Mode::Lyrics && live_.kind == ContentKind::Lyrics) ||
			      (mode_ == Mode::Scripture && live_.kind == ContentKind::Scripture)))
		showPreview();
}

void WordSongDock::updateLiveLabel()
{
	if (!live_.visible || live_.kind == ContentKind::None) {
		liveLabel_->setText(QStringLiteral("<span style='color:#888'>%1</span>").arg(T("Live.Nothing")));
		clearButton_->setEnabled(false);
		return;
	}
	QString what = live_.title.empty() ? elide(QString::fromStdString(live_.body), 40)
					   : QString::fromStdString(live_.title);
	liveLabel_->setText(QStringLiteral("<span style='color:#e5484d;font-weight:bold'>● %1</span> %2")
				    .arg(T("Live.OnScreen"), what.toHtmlEscaped()));
	clearButton_->setEnabled(true);
}

void WordSongDock::addDisplayToScene(bool fullScreen)
{
	obs_source_t *sceneSource = obs_frontend_get_current_scene();
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	if (!scene) {
		obs_source_release(sceneSource);
		return;
	}
	QString base = fullScreen ? T("Display.FullScreenName") : T("Display.LowerThirdName");
	QString name = base;
	for (int i = 2;; i++) {
		obs_source_t *existing = obs_get_source_by_name(name.toUtf8().constData());
		if (!existing)
			break;
		obs_source_release(existing);
		name = QStringLiteral("%1 %2").arg(base).arg(i);
	}
	obs_data_t *settings = obs_data_create();
	if (fullScreen) {
		obs_data_set_int(settings, "layout", 1);
		obs_data_set_int(settings, "align", 1);
		obs_data_set_int(settings, "box_color", 0xE6000000);
		obs_data_set_int(settings, "radius", 0);
		obs_data_set_int(settings, "margin", 0);
		obs_data_set_int(settings, "padding", 120);
		obs_data_t *font = obs_data_create();
		obs_data_set_int(font, "size", 72);
		obs_data_set_obj(settings, "font", font);
		obs_data_release(font);
		obs_data_set_int(settings, "title_size", 40);
	}
	obs_source_t *src = obs_source_create(kDisplaySourceId, name.toUtf8().constData(), settings, nullptr);
	obs_data_release(settings);
	if (src) {
		obs_scene_add(scene, src);
		obs_source_release(src);
	}
	obs_source_release(sceneSource);
}

// ---------------------------------------------------------------------------
// Settings

void WordSongDock::loadSettings()
{
	QFile f(QDir(configDir_).filePath(QStringLiteral("dock.json")));
	if (!f.open(QIODevice::ReadOnly))
		return;
	QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
	int idx = translation_->findData(o.value(QStringLiteral("translation")).toString());
	if (idx >= 0) {
		QSignalBlocker b(translation_);
		translation_->setCurrentIndex(idx);
	}
	showTranslation_->setChecked(o.value(QStringLiteral("showTranslation")).toBool(true));
	book_ = std::clamp(o.value(QStringLiteral("book")).toInt(43), 1, kBookCount);
	chapter_ = std::max(1, o.value(QStringLiteral("chapter")).toInt(3));
	if (audioSource_) {
		audioSource_->setProperty("saved", o.value(QStringLiteral("audioSource")).toString());
		modelPath_->setText(o.value(QStringLiteral("model")).toString());
		listenMode_->setCurrentIndex(std::clamp(o.value(QStringLiteral("listenMode")).toInt(0), 0, 1));
		autoShow_->setChecked(o.value(QStringLiteral("autoShow")).toBool(false));
	}
	tabs_->setCurrentIndex(std::clamp(o.value(QStringLiteral("tab")).toInt(0), 0, 2));
}

void WordSongDock::saveSettings() const
{
	QJsonObject o{{QStringLiteral("translation"), translation_->currentData().toString()},
		      {QStringLiteral("showTranslation"), showTranslation_->isChecked()},
		      {QStringLiteral("book"), book_},
		      {QStringLiteral("chapter"), chapter_},
		      {QStringLiteral("tab"), tabs_->currentIndex()}};
	if (audioSource_) {
		o.insert(QStringLiteral("audioSource"), audioSource_->currentText());
		o.insert(QStringLiteral("model"), modelPath_->text());
		o.insert(QStringLiteral("listenMode"), listenMode_->currentIndex());
		o.insert(QStringLiteral("autoShow"), autoShow_->isChecked());
	}
	QDir().mkpath(configDir_);
	QSaveFile f(QDir(configDir_).filePath(QStringLiteral("dock.json")));
	if (f.open(QIODevice::WriteOnly)) {
		f.write(QJsonDocument(o).toJson());
		f.commit();
	}
}

} // namespace ws
