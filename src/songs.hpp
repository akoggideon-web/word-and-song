// Word & Song for OBS — song library and setlist storage.
#pragma once

#include <QString>
#include <QStringList>

#include <vector>

namespace ws {

struct Song {
	QString id;
	QString title;
	QString author;
	QString text; // lyrics with section headers and blank lines between slides
};

class SongLibrary {
public:
	explicit SongLibrary(QString path = QString()) : path_(std::move(path)) {}
	void setPath(const QString &path) { path_ = path; }
	bool load();
	bool save() const;

	std::vector<Song> songs; // sorted by title for display
	QStringList setlist;     // song ids in service order
	int linesPerSlide = 2;   // 0 = keep blocks as written

	Song *find(const QString &id);
	QString add(Song s); // returns new id
	void remove(const QString &id);
	void sort();

	// Reads a .txt (first line = title unless it looks like lyrics), ProPresenter-style
	// plain text, or OpenLyrics/ChordPro-free text. Chord lines are dropped.
	static bool importText(const QString &file, Song &out, QString *error);

private:
	QString path_;
};

} // namespace ws
