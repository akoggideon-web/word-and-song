// Word & Song for OBS — song library and setlist storage.
#include "songs.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>

namespace ws {

bool SongLibrary::load()
{
	QFile f(path_);
	if (!f.open(QIODevice::ReadOnly))
		return false;
	QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
	songs.clear();
	for (const QJsonValue &v : root.value(QStringLiteral("songs")).toArray()) {
		QJsonObject o = v.toObject();
		Song s{o.value(QStringLiteral("id")).toString(), o.value(QStringLiteral("title")).toString(),
		       o.value(QStringLiteral("author")).toString(), o.value(QStringLiteral("text")).toString()};
		if (s.id.isEmpty())
			s.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
		songs.push_back(s);
	}
	setlist.clear();
	for (const QJsonValue &v : root.value(QStringLiteral("setlist")).toArray())
		if (find(v.toString()))
			setlist << v.toString();
	linesPerSlide = root.value(QStringLiteral("linesPerSlide")).toInt(2);
	sort();
	return true;
}

bool SongLibrary::save() const
{
	if (path_.isEmpty())
		return false;
	QJsonArray arr;
	for (const Song &s : songs)
		arr.append(QJsonObject{{QStringLiteral("id"), s.id},
				       {QStringLiteral("title"), s.title},
				       {QStringLiteral("author"), s.author},
				       {QStringLiteral("text"), s.text}});
	QJsonObject root{{QStringLiteral("version"), 1},
			 {QStringLiteral("songs"), arr},
			 {QStringLiteral("setlist"), QJsonArray::fromStringList(setlist)},
			 {QStringLiteral("linesPerSlide"), linesPerSlide}};
	QDir().mkpath(QFileInfo(path_).absolutePath());
	QSaveFile f(path_);
	if (!f.open(QIODevice::WriteOnly))
		return false;
	f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
	return f.commit();
}

Song *SongLibrary::find(const QString &id)
{
	for (Song &s : songs)
		if (s.id == id)
			return &s;
	return nullptr;
}

QString SongLibrary::add(Song s)
{
	if (s.id.isEmpty())
		s.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	QString id = s.id;
	songs.push_back(std::move(s));
	sort();
	return id;
}

void SongLibrary::remove(const QString &id)
{
	songs.erase(std::remove_if(songs.begin(), songs.end(), [&](const Song &s) { return s.id == id; }), songs.end());
	setlist.removeAll(id);
}

void SongLibrary::sort()
{
	std::stable_sort(songs.begin(), songs.end(), [](const Song &a, const Song &b) {
		return QString::localeAwareCompare(a.title, b.title) < 0;
	});
}

static bool isChordLine(const QString &line)
{
	// "G   D/F#   Em7   C" — every token is a chord symbol.
	static const QRegularExpression chord(QStringLiteral(
		"^[A-G](#|b)?(m|maj|min|sus|dim|aug|add)?\\d*(sus\\d*|add\\d*|maj\\d*)?(/[A-G](#|b)?)?$"));
	const QStringList toks = line.simplified().split(' ', Qt::SkipEmptyParts);
	if (toks.isEmpty())
		return false;
	for (const QString &t : toks)
		if (!chord.match(t).hasMatch() && t != QStringLiteral("|") && t != QStringLiteral("-"))
			return false;
	return true;
}

bool SongLibrary::importText(const QString &file, Song &out, QString *error)
{
	QFile f(file);
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
		if (error)
			*error = QStringLiteral("Cannot open %1").arg(file);
		return false;
	}
	QString text = QString::fromUtf8(f.readAll());
	text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
	QStringList lines = text.split('\n');
	// Drop chord lines and inline ChordPro chords like "[G]Amazing [D]grace".
	static const QRegularExpression inlineChord(QStringLiteral("\\[[A-G][^\\]\\s]{0,7}\\]"));
	QStringList kept;
	for (QString l : lines) {
		if (isChordLine(l))
			continue;
		if (l.startsWith(QStringLiteral("{")) && l.trimmed().endsWith('}')) {
			// ChordPro directive: {title: ...}, {c: Chorus}, {soc}
			QString d = l.trimmed().mid(1).chopped(1);
			int colon = d.indexOf(':');
			QString key = (colon < 0 ? d : d.left(colon)).trimmed().toLower();
			QString val = colon < 0 ? QString() : d.mid(colon + 1).trimmed();
			if ((key == QStringLiteral("title") || key == QStringLiteral("t")) && out.title.isEmpty())
				out.title = val;
			else if (key == QStringLiteral("c") || key == QStringLiteral("comment"))
				kept << QStringLiteral("[%1]").arg(val);
			else if (key == QStringLiteral("soc") || key == QStringLiteral("start_of_chorus"))
				kept << QStringLiteral("[Chorus]");
			continue;
		}
		l.remove(inlineChord);
		kept << l.trimmed();
	}
	while (!kept.isEmpty() && kept.first().isEmpty())
		kept.removeFirst();
	if (out.title.isEmpty()) {
		// Use the first line as the title when it is followed by a blank line.
		if (kept.size() > 2 && !kept[0].isEmpty() && kept[1].isEmpty()) {
			out.title = kept.takeFirst();
		} else {
			out.title = QFileInfo(file).completeBaseName();
		}
	}
	out.text = kept.join('\n').trimmed();
	if (out.text.isEmpty()) {
		if (error)
			*error = QStringLiteral("No lyrics found in %1").arg(file);
		return false;
	}
	return true;
}

} // namespace ws
