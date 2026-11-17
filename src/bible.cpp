// Word & Song for OBS — Bible translations: loading, lookup, search, import.
#include "bible.hpp"

#include "matcher.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>

#include <algorithm>

namespace ws {

static uint32_t chapterKey(int book, int chapter)
{
	return ((uint32_t)book << 16) | (uint32_t)chapter;
}

std::shared_ptr<Bible> Bible::loadTsv(const QString &path, QString *error)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
		if (error)
			*error = QStringLiteral("Cannot open %1").arg(path);
		return nullptr;
	}
	auto b = std::make_shared<Bible>();
	b->abbrev_ = QFileInfo(path).completeBaseName();
	b->name_ = b->abbrev_;
	QTextStream in(&f);
	in.setEncoding(QStringConverter::Utf8);
	b->verses_.reserve(31200);
	while (!in.atEnd()) {
		QString line = in.readLine();
		if (line.isEmpty())
			continue;
		if (line.startsWith('#')) {
			if (line.startsWith(QStringLiteral("#name\t")))
				b->name_ = line.mid(6).trimmed();
			continue;
		}
		QStringList p = line.split('\t');
		if (p.size() < 4)
			continue;
		Verse v;
		v.book = p[0].toInt();
		v.chapter = p[1].toInt();
		v.verse = p[2].toInt();
		v.text = p.mid(3).join(' ').trimmed();
		if (v.book < 1 || v.book > kBookCount || v.chapter < 1 || v.verse < 1)
			continue;
		b->verses_.push_back(std::move(v));
	}
	if (b->verses_.empty()) {
		if (error)
			*error = QStringLiteral("No verses found in %1").arg(path);
		return nullptr;
	}
	b->index();
	return b;
}

void Bible::index()
{
	std::stable_sort(verses_.begin(), verses_.end(), [](const Verse &a, const Verse &b) {
		if (a.book != b.book)
			return a.book < b.book;
		if (a.chapter != b.chapter)
			return a.chapter < b.chapter;
		return a.verse < b.verse;
	});
	lowered_.clear();
	lowered_.reserve(verses_.size());
	for (size_t i = 0; i < verses_.size(); i++) {
		const auto &v = verses_[i];
		chapterStart_.emplace(chapterKey(v.book, v.chapter), i);
		lowered_.push_back(v.text.toLower());
	}
}

std::vector<const Verse *> Bible::chapter(int book, int ch) const
{
	std::vector<const Verse *> out;
	auto it = chapterStart_.find(chapterKey(book, ch));
	if (it == chapterStart_.end())
		return out;
	for (size_t i = it->second; i < verses_.size() && verses_[i].book == book && verses_[i].chapter == ch; i++)
		out.push_back(&verses_[i]);
	return out;
}

int Bible::chapterCountFor(int book) const
{
	int n = 0;
	auto it = chapterStart_.lower_bound(chapterKey(book, 0));
	for (; it != chapterStart_.end() && (int)(it->first >> 16) == book; ++it)
		n = std::max(n, (int)(it->first & 0xFFFF));
	return n;
}

const Verse *Bible::verse(int book, int ch, int v) const
{
	for (const Verse *x : chapter(book, ch))
		if (x->verse == v)
			return x;
	return nullptr;
}

std::vector<const Verse *> Bible::lookup(const Reference &ref) const
{
	auto all = chapter(ref.book, ref.chapter ? ref.chapter : 1);
	if (!ref.verseStart)
		return all;
	std::vector<const Verse *> out;
	int end = std::max(ref.verseStart, ref.verseEnd);
	for (const Verse *v : all)
		if (v->verse >= ref.verseStart && v->verse <= end)
			out.push_back(v);
	return out;
}

std::vector<const Verse *> Bible::search(const QString &query, int limit) const
{
	std::vector<const Verse *> out;
	QStringList terms = query.toLower().split(' ', Qt::SkipEmptyParts);
	if (terms.isEmpty())
		return out;
	// A quoted query is matched as an exact phrase.
	QString phrase;
	if (query.size() > 2 && query.startsWith('"') && query.endsWith('"'))
		phrase = query.mid(1, query.size() - 2).toLower();
	for (size_t i = 0; i < verses_.size() && (int)out.size() < limit; i++) {
		const QString &t = lowered_[i];
		bool ok = true;
		if (!phrase.isEmpty()) {
			ok = t.contains(phrase);
		} else {
			for (const QString &term : terms)
				if (!t.contains(term)) {
					ok = false;
					break;
				}
		}
		if (ok)
			out.push_back(&verses_[i]);
	}
	return out;
}

const Verse *Bible::matchQuote(const std::vector<uint64_t> &heard, double minCoverage, size_t minBigrams) const
{
	if (!bigramsBuilt_) {
		bigrams_.resize(verses_.size());
		for (size_t i = 0; i < verses_.size(); i++)
			bigrams_[i] = bigramHashes(words(verses_[i].text.toStdString()));
		bigramsBuilt_ = true;
	}
	if (heard.size() < minBigrams)
		return nullptr;
	const Verse *best = nullptr;
	double bestScore = 0.0;
	for (size_t i = 0; i < verses_.size(); i++) {
		const auto &t = bigrams_[i];
		if (t.size() < minBigrams)
			continue;
		// Long verses are rarely quoted in full: score against the best part we could
		// have heard, capped by how much was heard.
		double cov = bigramCoverage(heard, t);
		double denom = std::min<double>((double)t.size(), (double)heard.size());
		double score = cov * (double)t.size() / denom;
		size_t hits = (size_t)(cov * (double)t.size() + 0.5);
		if (hits >= minBigrams && score > bestScore) {
			bestScore = score;
			best = &verses_[i];
		}
	}
	return bestScore >= minCoverage ? best : nullptr;
}

// ---------------------------------------------------------------------------

BibleLibrary &BibleLibrary::instance()
{
	static BibleLibrary lib;
	return lib;
}

static QString readName(const QString &path)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
		return {};
	QString first = QString::fromUtf8(f.readLine()).trimmed();
	return first.startsWith(QStringLiteral("#name\t")) ? first.mid(6).trimmed() : QString();
}

void BibleLibrary::scan(const QString &bundledDir, const QString &userDir)
{
	userDir_ = userDir;
	entries_.clear();
	for (const QString &dir : {bundledDir, userDir}) {
		if (dir.isEmpty())
			continue;
		QDir d(dir);
		for (const QFileInfo &fi : d.entryInfoList({QStringLiteral("*.tsv")}, QDir::Files, QDir::Name)) {
			QString abbrev = fi.completeBaseName();
			QString name = readName(fi.absoluteFilePath());
			entries_[abbrev] = Entry{fi.absoluteFilePath(), name.isEmpty() ? abbrev : name, nullptr};
		}
	}
}

QStringList BibleLibrary::abbrevs() const
{
	QStringList out;
	// Bundled favourites first, then everything else alphabetically.
	for (const char *pref : {"KJV", "NKJV", "NIV", "ESV", "NLT", "AMP", "WEB", "BSB", "ASV"}) {
		QString p = QString::fromLatin1(pref);
		if (entries_.count(p))
			out << p;
	}
	for (const auto &[k, e] : entries_)
		if (!out.contains(k))
			out << k;
	return out;
}

QString BibleLibrary::displayName(const QString &abbrev) const
{
	auto it = entries_.find(abbrev);
	return it == entries_.end() ? abbrev : it->second.name;
}

std::shared_ptr<Bible> BibleLibrary::get(const QString &abbrev)
{
	auto it = entries_.find(abbrev);
	if (it == entries_.end())
		return nullptr;
	if (!it->second.bible) {
		it->second.bible = Bible::loadTsv(it->second.path);
		if (it->second.bible)
			it->second.name = it->second.bible->name();
	}
	return it->second.bible;
}

static int bookFromField(const QString &field)
{
	bool ok = false;
	int n = field.trimmed().toInt(&ok);
	if (ok)
		return n >= 1 && n <= kBookCount ? n : 0;
	auto ref = parseTyped(field.trimmed().toStdString());
	return ref && !ref->chapter ? ref->book : 0;
}

static QString unquote(QString s)
{
	s = s.trimmed();
	if (s.size() >= 2 && s.startsWith('"') && s.endsWith('"'))
		s = s.mid(1, s.size() - 2).replace(QStringLiteral("\"\""), QStringLiteral("\""));
	return s;
}

bool BibleLibrary::importFile(const QString &file, const QString &abbrevIn, const QString &nameIn, QString *error)
{
	QString abbrev = abbrevIn.trimmed().toUpper();
	abbrev.remove(QRegularExpression(QStringLiteral("[^A-Z0-9_-]")));
	if (abbrev.isEmpty()) {
		if (error)
			*error = QStringLiteral("Please enter a short name such as NKJV.");
		return false;
	}
	QFile f(file);
	if (!f.open(QIODevice::ReadOnly)) {
		if (error)
			*error = QStringLiteral("Cannot open %1").arg(file);
		return false;
	}
	QByteArray data = f.readAll();
	struct Row {
		int b, c, v;
		QString t;
	};
	std::vector<Row> rows;
	QString name = nameIn.trimmed();

	QJsonParseError jerr{};
	QJsonDocument doc = QJsonDocument::fromJson(data, &jerr);
	if (jerr.error == QJsonParseError::NoError && (doc.isObject() || doc.isArray())) {
		QJsonArray booksArr = doc.isObject() ? doc.object().value(QStringLiteral("books")).toArray()
						     : doc.array();
		if (doc.isObject() && name.isEmpty())
			name = doc.object().value(QStringLiteral("translation")).toString();
		int bookIdx = 0;
		for (const QJsonValue &bv : booksArr) {
			QJsonObject bo = bv.toObject();
			bookIdx++;
			if (bo.contains(QStringLiteral("chapters"))) {
				int b = bo.contains(QStringLiteral("name"))
						? bookFromField(bo.value(QStringLiteral("name")).toString())
						: bookIdx;
				if (!b)
					b = bookIdx;
				for (const QJsonValue &cv : bo.value(QStringLiteral("chapters")).toArray()) {
					QJsonObject co = cv.toObject();
					int c = co.value(QStringLiteral("chapter")).toInt();
					for (const QJsonValue &vv : co.value(QStringLiteral("verses")).toArray()) {
						QJsonObject vo = vv.toObject();
						rows.push_back({b, c, vo.value(QStringLiteral("verse")).toInt(),
								vo.value(QStringLiteral("text")).toString()});
					}
				}
			} else {
				// Flat list: {"book": "John" | 43, "chapter": 3, "verse": 16, "text": "..."}
				QJsonValue bk = bo.value(QStringLiteral("book"));
				int b = bk.isDouble() ? bk.toInt() : bookFromField(bk.toString());
				rows.push_back({b, bo.value(QStringLiteral("chapter")).toInt(),
						bo.value(QStringLiteral("verse")).toInt(),
						bo.value(QStringLiteral("text")).toString()});
			}
		}
	} else {
		QString text = QString::fromUtf8(data);
		const QStringList lines = text.split('\n');
		for (QString line : lines) {
			line = line.trimmed();
			if (line.isEmpty() || line.startsWith('#'))
				continue;
			QStringList p;
			if (line.contains('\t')) {
				p = line.split('\t');
			} else {
				// CSV: the first three fields are simple, the rest is the (maybe quoted) text.
				int c1 = line.indexOf(','), c2 = c1 < 0 ? -1 : line.indexOf(',', c1 + 1),
				    c3 = c2 < 0 ? -1 : line.indexOf(',', c2 + 1);
				if (c3 < 0)
					continue;
				p = {line.left(c1), line.mid(c1 + 1, c2 - c1 - 1), line.mid(c2 + 1, c3 - c2 - 1),
				     line.mid(c3 + 1)};
			}
			if (p.size() < 4)
				continue;
			bool okc = false, okv = false;
			int c = unquote(p[1]).toInt(&okc), v = unquote(p[2]).toInt(&okv);
			if (!okc || !okv)
				continue; // header row
			rows.push_back({bookFromField(unquote(p[0])), c, v, unquote(p.mid(3).join(' '))});
		}
	}

	rows.erase(
		std::remove_if(rows.begin(), rows.end(),
			       [](const Row &r) { return r.b < 1 || r.c < 1 || r.v < 1 || r.t.trimmed().isEmpty(); }),
		rows.end());
	if (rows.size() < 100) {
		if (error)
			*error = QStringLiteral(
					 "Only %1 verses could be read. Expected columns: Book, Chapter, Verse, Text "
					 "(tab or comma separated), or a JSON Bible.")
					 .arg(rows.size());
		return false;
	}
	QDir().mkpath(userDir_);
	QString out = QDir(userDir_).filePath(abbrev + QStringLiteral(".tsv"));
	QSaveFile sf(out);
	if (!sf.open(QIODevice::WriteOnly | QIODevice::Text)) {
		if (error)
			*error = QStringLiteral("Cannot write %1").arg(out);
		return false;
	}
	QByteArray buf;
	buf += "#name\t" + (name.isEmpty() ? abbrev : name).toUtf8() + "\n";
	for (const Row &r : rows) {
		QString t = r.t.simplified();
		t.replace('\t', ' ');
		buf += QByteArray::number(r.b) + '\t' + QByteArray::number(r.c) + '\t' + QByteArray::number(r.v) +
		       '\t' + t.toUtf8() + '\n';
	}
	sf.write(buf);
	if (!sf.commit()) {
		if (error)
			*error = QStringLiteral("Cannot write %1").arg(out);
		return false;
	}
	entries_[abbrev] = Entry{out, name.isEmpty() ? abbrev : name, nullptr};
	return true;
}

} // namespace ws
