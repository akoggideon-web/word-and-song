// Word & Song for OBS — scripture reference parsing (typed and spoken).
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace ws {

constexpr int kBookCount = 66;

struct Reference {
	int book = 0;        // 1..66, 0 = none
	int chapter = 0;     // 0 = book only
	int verseStart = 0;  // 0 = whole chapter
	int verseEnd = 0;    // == verseStart for a single verse
	size_t tokenEnd = 0; // internal: token index after the match

	bool operator==(const Reference &o) const
	{
		return book == o.book && chapter == o.chapter && verseStart == o.verseStart && verseEnd == o.verseEnd;
	}
};

// Canonical English book name, 1-based.
const std::string &bookName(int book);
// Short display abbreviation (e.g. "1 Cor"), 1-based.
const std::string &bookAbbrev(int book);
// Number of chapters in the standard 66-book Protestant canon.
int chapterCount(int book);

// Lower-cases and normalises text: number words become digits ("three hundred and
// sixteen" -> "316", "first" -> "1"), "3.16" becomes "3:16", punctuation is dropped.
std::vector<std::string> normalizeTokens(const std::string &text, bool spoken);

// Parses what an operator types into the search box: "jn 3:16-18", "1 cor 13 4",
// "Psalm 23", "song of songs 2". Returns nothing if the text does not start with a book.
std::optional<Reference> parseTyped(const std::string &text);

// Finds every reference spoken in a transcript, e.g. "turn with me to John chapter three
// verse sixteen". A chapter number is required, so a bare name such as "John said" or
// "the book of Acts" is ignored.
std::vector<Reference> findSpoken(const std::string &transcript);

// "John 3:16", "John 3:16-18", "Psalm 23", "Jude 1:3".
std::string formatReference(const Reference &ref, bool abbreviate = false);

} // namespace ws
