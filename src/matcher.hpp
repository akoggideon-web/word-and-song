// Word & Song for OBS — lyric slides and fuzzy text matching (no OBS/Qt dependency).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ws {

struct Slide {
	std::string label; // "Verse 1", "Chorus", ... (may be empty)
	std::string text;  // lines separated by '\n'
	bool sectionStart = false;
};

// Splits song text into slides. A section header line ("[Chorus]", "Verse 2:", "Bridge")
// starts a new section, a blank line starts a new slide, and any slide longer than
// maxLines lines is split further. maxLines <= 0 keeps blocks as written.
std::vector<Slide> splitSlides(const std::string &songText, int maxLines);

// Lower-case word tokens with punctuation removed.
std::vector<std::string> words(const std::string &text);

// Sorted, de-duplicated hashes of consecutive word pairs.
std::vector<uint64_t> bigramHashes(const std::vector<std::string> &w);

// Fraction (0..1) of the target's word pairs that appear in the heard set.
// heard must be sorted (as returned by bigramHashes).
double bigramCoverage(const std::vector<uint64_t> &heard, const std::vector<uint64_t> &target);

} // namespace ws
