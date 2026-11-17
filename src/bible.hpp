// Word & Song for OBS — Bible translations: loading, lookup, search, import.
#pragma once

#include "reference-parser.hpp"

#include <QString>
#include <QStringList>

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace ws {

struct Verse {
	int book = 0, chapter = 0, verse = 0;
	QString text;
};

class Bible {
public:
	// Loads the bundled "#name\t...\n book\tchapter\tverse\ttext" format.
	static std::shared_ptr<Bible> loadTsv(const QString &path, QString *error = nullptr);

	QString abbrev() const { return abbrev_; }
	QString name() const { return name_; }

	// All verses of a chapter (empty if missing).
	std::vector<const Verse *> chapter(int book, int chapter) const;
	int chapterCountFor(int book) const;
	const Verse *verse(int book, int chapter, int verse) const;
	// Verses for a reference; a chapter-only reference returns the whole chapter.
	std::vector<const Verse *> lookup(const Reference &ref) const;

	// Case-insensitive search: every word must appear in the verse. Max `limit` results.
	std::vector<const Verse *> search(const QString &query, int limit = 200) const;

	// Finds the verse a speaker is quoting, if any. Returns nullptr below the threshold.
	const Verse *matchQuote(const std::vector<uint64_t> &heardBigrams, double minCoverage = 0.6,
				size_t minBigrams = 5) const;

	const std::vector<Verse> &verses() const { return verses_; }

private:
	void index();
	QString abbrev_, name_;
	std::vector<Verse> verses_;
	std::vector<QString> lowered_;
	std::map<uint32_t, size_t> chapterStart_;            // key = book<<16 | chapter
	mutable std::vector<std::vector<uint64_t>> bigrams_; // built lazily for quote matching
	mutable bool bigramsBuilt_ = false;
};

class BibleLibrary {
public:
	static BibleLibrary &instance();

	// Scans bundled and user-imported translations (call once at startup).
	void scan(const QString &bundledDir, const QString &userDir);
	QStringList abbrevs() const;
	QString displayName(const QString &abbrev) const;
	// Loads on first use.
	std::shared_ptr<Bible> get(const QString &abbrev);

	// Imports a licensed translation from TSV/CSV ("Book<TAB>Chapter<TAB>Verse<TAB>Text",
	// book as a name or 1-66) or JSON ({"books":[{"name","chapters":[{"chapter","verses":
	// [{"verse","text"}]}]}]}). Writes "<abbrev>.tsv" to the user directory.
	bool importFile(const QString &file, const QString &abbrev, const QString &name, QString *error);

private:
	struct Entry {
		QString path, name;
		std::shared_ptr<Bible> bible;
	};
	std::map<QString, Entry> entries_;
	QString userDir_;
};

} // namespace ws
