// Word & Song for OBS — lyric slides and fuzzy text matching.
#include "matcher.hpp"

#include <algorithm>
#include <cctype>

namespace ws {
namespace {

std::string trim(const std::string &s)
{
	size_t a = 0, b = s.size();
	while (a < b && std::isspace((unsigned char)s[a]))
		a++;
	while (b > a && std::isspace((unsigned char)s[b - 1]))
		b--;
	return s.substr(a, b - a);
}

std::string lower(std::string s)
{
	for (auto &c : s)
		c = (char)std::tolower((unsigned char)c);
	return s;
}

// Returns the display label if the line is a section header, else empty.
std::string sectionLabel(const std::string &rawLine)
{
	std::string line = trim(rawLine);
	if (line.size() >= 3 &&
	    ((line.front() == '[' && line.back() == ']') || (line.front() == '(' && line.back() == ')'))) {
		std::string inner = trim(line.substr(1, line.size() - 2));
		std::string l = lower(inner);
		static const char *known[] = {"verse",      "chorus",     "bridge",     "pre-chorus",   "prechorus",
					      "pre chorus", "tag",        "intro",      "outro",        "ending",
					      "refrain",    "vamp",       "interlude",  "instrumental", "hook",
					      "coda",       "turnaround", "spontaneous"};
		// Square brackets are always a header; parentheses only for known names, so
		// sung asides like "(Hallelujah)" stay as lyrics.
		if (line.front() == '[')
			return inner;
		for (const char *k : known)
			if (l.rfind(k, 0) == 0)
				return inner;
		return {};
	}
	std::string l = lower(line);
	if (!l.empty() && l.back() == ':')
		l.pop_back();
	static const char *names[] = {"verse",      "chorus", "bridge",    "pre-chorus", "prechorus",
				      "pre chorus", "tag",    "intro",     "outro",      "ending",
				      "refrain",    "vamp",   "interlude", "hook",       "coda"};
	for (const char *n : names) {
		std::string ns = n;
		if (l.rfind(ns, 0) != 0)
			continue;
		std::string rest = trim(l.substr(ns.size()));
		// "Verse", "Verse 2", "Chorus x2", "Bridge 1b"
		bool ok = rest.empty() || std::isdigit((unsigned char)rest[0]) || rest[0] == 'x' || rest.size() <= 2;
		if (ok && line.size() <= 24) {
			std::string out = trim(line);
			if (!out.empty() && out.back() == ':')
				out.pop_back();
			return out;
		}
	}
	return {};
}

} // namespace

std::vector<Slide> splitSlides(const std::string &songText, int maxLines)
{
	std::vector<Slide> slides;
	std::string label;
	bool newSection = true;
	std::vector<std::string> block;

	auto flush = [&]() {
		if (block.empty())
			return;
		size_t per = maxLines > 0 ? (size_t)maxLines : block.size();
		for (size_t i = 0; i < block.size(); i += per) {
			Slide s;
			s.label = label;
			s.sectionStart = newSection;
			newSection = false;
			for (size_t k = i; k < std::min(block.size(), i + per); k++) {
				if (!s.text.empty())
					s.text += '\n';
				s.text += block[k];
			}
			slides.push_back(std::move(s));
		}
		block.clear();
	};

	size_t pos = 0;
	std::string text = songText;
	while (pos <= text.size()) {
		size_t nl = text.find('\n', pos);
		if (nl == std::string::npos)
			nl = text.size();
		std::string line = text.substr(pos, nl - pos);
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		pos = nl + 1;

		std::string t = trim(line);
		std::string hdr = sectionLabel(t);
		if (!hdr.empty()) {
			flush();
			label = hdr;
			newSection = true;
		} else if (t.empty()) {
			flush();
		} else {
			block.push_back(t);
		}
		if (nl == text.size())
			break;
	}
	flush();
	return slides;
}

std::vector<std::string> words(const std::string &text)
{
	std::vector<std::string> out;
	std::string cur;
	for (size_t i = 0; i < text.size(); i++) {
		unsigned char c = (unsigned char)text[i];
		if (std::isalnum(c)) {
			cur += (char)std::tolower(c);
		} else if (c == '\'') {
			// "you're" -> "youre"
		} else if (c == 0xE2 && i + 2 < text.size()) {
			// 3-byte punctuation: a curly apostrophe (U+2019) joins, dashes/quotes split.
			bool apostrophe = (unsigned char)text[i + 1] == 0x80 && (unsigned char)text[i + 2] == 0x99;
			i += 2;
			if (!apostrophe && !cur.empty()) {
				out.push_back(cur);
				cur.clear();
			}
		} else if (c >= 0x80) {
			cur += (char)c; // accented letters
		} else if (!cur.empty()) {
			out.push_back(cur);
			cur.clear();
		}
	}
	if (!cur.empty())
		out.push_back(cur);
	return out;
}

std::vector<uint64_t> bigramHashes(const std::vector<std::string> &w)
{
	std::vector<uint64_t> h;
	if (w.size() < 2)
		return h;
	h.reserve(w.size() - 1);
	auto fnv = [](const std::string &s, uint64_t seed) {
		uint64_t x = seed;
		for (unsigned char c : s) {
			x ^= c;
			x *= 1099511628211ULL;
		}
		return x;
	};
	for (size_t i = 0; i + 1 < w.size(); i++)
		h.push_back(fnv(w[i + 1], fnv(w[i], 1469598103934665603ULL) ^ 0x9E3779B97F4A7C15ULL));
	std::sort(h.begin(), h.end());
	h.erase(std::unique(h.begin(), h.end()), h.end());
	return h;
}

double bigramCoverage(const std::vector<uint64_t> &heard, const std::vector<uint64_t> &target)
{
	if (target.empty() || heard.empty())
		return 0.0;
	size_t hit = 0;
	for (uint64_t t : target)
		if (std::binary_search(heard.begin(), heard.end(), t))
			hit++;
	return (double)hit / (double)target.size();
}

} // namespace ws
