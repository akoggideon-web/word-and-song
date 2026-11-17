// Word & Song for OBS — scripture reference parsing (typed and spoken).
#include "reference-parser.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <unordered_map>

namespace ws {
namespace {

struct BookInfo {
	const char *name;
	const char *abbrev;
	int chapters;
	int number;                    // 1, 2, 3 for numbered books, else 0
	std::vector<std::string> full; // spoken/typed full names (without number prefix)
	std::vector<std::string> abbr; // typed-only abbreviations (without number prefix)
};

const std::vector<BookInfo> &books()
{
	static const std::vector<BookInfo> b = {
		{"Genesis", "Gen", 50, 0, {"genesis"}, {"gen", "ge", "gn"}},
		{"Exodus", "Exod", 40, 0, {"exodus"}, {"exod", "exo", "ex"}},
		{"Leviticus", "Lev", 27, 0, {"leviticus"}, {"lev", "le", "lv"}},
		{"Numbers", "Num", 36, 0, {"numbers"}, {"num", "nu", "nm", "nb"}},
		{"Deuteronomy", "Deut", 34, 0, {"deuteronomy"}, {"deut", "deu", "dt", "de"}},
		{"Joshua", "Josh", 24, 0, {"joshua"}, {"josh", "jos", "jsh"}},
		{"Judges", "Judg", 21, 0, {"judges"}, {"judg", "jdg", "jg", "jdgs"}},
		{"Ruth", "Ruth", 4, 0, {"ruth"}, {"rth", "ru"}},
		{"1 Samuel", "1 Sam", 31, 1, {"samuel"}, {"sam", "sa", "sm", "s"}},
		{"2 Samuel", "2 Sam", 24, 2, {"samuel"}, {"sam", "sa", "sm", "s"}},
		{"1 Kings", "1 Kgs", 22, 1, {"kings"}, {"kgs", "ki", "kin", "k"}},
		{"2 Kings", "2 Kgs", 25, 2, {"kings"}, {"kgs", "ki", "kin", "k"}},
		{"1 Chronicles", "1 Chr", 29, 1, {"chronicles"}, {"chr", "chron", "ch"}},
		{"2 Chronicles", "2 Chr", 36, 2, {"chronicles"}, {"chr", "chron", "ch"}},
		{"Ezra", "Ezra", 10, 0, {"ezra"}, {"ezr", "ez"}},
		{"Nehemiah", "Neh", 13, 0, {"nehemiah"}, {"neh", "ne"}},
		{"Esther", "Esth", 10, 0, {"esther"}, {"esth", "est", "es"}},
		{"Job", "Job", 42, 0, {"job"}, {"jb"}},
		{"Psalms", "Ps", 150, 0, {"psalms", "psalm"}, {"ps", "psa", "pss", "psm", "pslm"}},
		{"Proverbs", "Prov", 31, 0, {"proverbs", "proverb"}, {"prov", "pro", "prv", "pr"}},
		{"Ecclesiastes", "Eccl", 12, 0, {"ecclesiastes"}, {"eccl", "ecc", "ec", "qoh"}},
		{"Song of Solomon",
		 "Song",
		 8,
		 0,
		 {"song of solomon", "song of songs", "songs of solomon", "song of song"},
		 {"song", "songs", "sos", "so", "sng", "cant", "canticles"}},
		{"Isaiah", "Isa", 66, 0, {"isaiah"}, {"isa", "is"}},
		{"Jeremiah", "Jer", 52, 0, {"jeremiah"}, {"jer", "je", "jr"}},
		{"Lamentations", "Lam", 5, 0, {"lamentations"}, {"lam", "la"}},
		{"Ezekiel", "Ezek", 48, 0, {"ezekiel"}, {"ezek", "eze", "ezk"}},
		{"Daniel", "Dan", 12, 0, {"daniel"}, {"dan", "da", "dn"}},
		{"Hosea", "Hos", 14, 0, {"hosea"}, {"hos", "ho"}},
		{"Joel", "Joel", 3, 0, {"joel"}, {"jl"}},
		{"Amos", "Amos", 9, 0, {"amos"}, {"am"}},
		{"Obadiah", "Obad", 1, 0, {"obadiah"}, {"obad", "ob"}},
		{"Jonah", "Jonah", 4, 0, {"jonah"}, {"jnh", "jon"}},
		{"Micah", "Mic", 7, 0, {"micah"}, {"mic", "mc"}},
		{"Nahum", "Nah", 3, 0, {"nahum"}, {"nah", "na"}},
		{"Habakkuk", "Hab", 3, 0, {"habakkuk"}, {"hab", "hb"}},
		{"Zephaniah", "Zeph", 3, 0, {"zephaniah"}, {"zeph", "zep", "zp"}},
		{"Haggai", "Hag", 2, 0, {"haggai"}, {"hag", "hg"}},
		{"Zechariah", "Zech", 14, 0, {"zechariah"}, {"zech", "zec", "zc"}},
		{"Malachi", "Mal", 4, 0, {"malachi"}, {"mal", "ml"}},
		{"Matthew", "Matt", 28, 0, {"matthew"}, {"matt", "mat", "mt"}},
		{"Mark", "Mark", 16, 0, {"mark"}, {"mrk", "mar", "mk", "mr"}},
		{"Luke", "Luke", 24, 0, {"luke"}, {"luk", "lk"}},
		{"John", "John", 21, 0, {"john"}, {"joh", "jhn", "jn"}},
		{"Acts", "Acts", 28, 0, {"acts", "acts of the apostles"}, {"act", "ac"}},
		{"Romans", "Rom", 16, 0, {"romans"}, {"rom", "ro", "rm"}},
		{"1 Corinthians", "1 Cor", 16, 1, {"corinthians"}, {"cor", "co"}},
		{"2 Corinthians", "2 Cor", 13, 2, {"corinthians"}, {"cor", "co"}},
		{"Galatians", "Gal", 6, 0, {"galatians"}, {"gal", "ga"}},
		{"Ephesians", "Eph", 6, 0, {"ephesians"}, {"eph", "ephes"}},
		{"Philippians", "Phil", 4, 0, {"philippians"}, {"phil", "php", "pp"}},
		{"Colossians", "Col", 4, 0, {"colossians"}, {"col", "co"}},
		{"1 Thessalonians", "1 Thess", 5, 1, {"thessalonians"}, {"thess", "thes", "th"}},
		{"2 Thessalonians", "2 Thess", 3, 2, {"thessalonians"}, {"thess", "thes", "th"}},
		{"1 Timothy", "1 Tim", 6, 1, {"timothy"}, {"tim", "ti"}},
		{"2 Timothy", "2 Tim", 4, 2, {"timothy"}, {"tim", "ti"}},
		{"Titus", "Titus", 3, 0, {"titus"}, {"tit", "ti"}},
		{"Philemon", "Phlm", 1, 0, {"philemon"}, {"philem", "phm", "phlm"}},
		{"Hebrews", "Heb", 13, 0, {"hebrews"}, {"heb"}},
		{"James", "Jas", 5, 0, {"james"}, {"jas", "jm"}},
		{"1 Peter", "1 Pet", 5, 1, {"peter"}, {"pet", "pe", "pt", "p"}},
		{"2 Peter", "2 Pet", 3, 2, {"peter"}, {"pet", "pe", "pt", "p"}},
		{"1 John", "1 John", 5, 1, {"john"}, {"jn", "jhn", "joh", "j"}},
		{"2 John", "2 John", 1, 2, {"john"}, {"jn", "jhn", "joh", "j"}},
		{"3 John", "3 John", 1, 3, {"john"}, {"jn", "jhn", "joh", "j"}},
		{"Jude", "Jude", 1, 0, {"jude"}, {"jud", "jd"}},
		{"Revelation", "Rev", 22, 0, {"revelation", "revelations", "the revelation"}, {"rev", "re", "rv"}},
	};
	return b;
}

struct AliasTable {
	std::unordered_map<std::string, int> map;
	size_t maxTokens = 1;
};

size_t countTokens(const std::string &s)
{
	return (size_t)std::count(s.begin(), s.end(), ' ') + 1;
}

AliasTable buildAliases(bool spoken)
{
	AliasTable t;
	static const char *romans[] = {"", "i", "ii", "iii"};
	const auto &bs = books();
	auto add = [&](const std::string &key, int book) {
		// The first registration wins, so "co" stays with 1 Corinthians/Colossians order
		// deterministic and unnumbered names keep priority over abbreviations.
		t.map.emplace(key, book);
		t.maxTokens = std::max(t.maxTokens, countTokens(key));
	};
	// Full names first so they win over any clashing abbreviation.
	for (size_t i = 0; i < bs.size(); i++) {
		const auto &b = bs[i];
		int idx = (int)i + 1;
		for (const auto &n : b.full) {
			if (b.number) {
				add(std::to_string(b.number) + " " + n, idx);
				if (!spoken)
					add(std::string(romans[b.number]) + " " + n, idx);
			} else {
				add(n, idx);
			}
		}
	}
	if (spoken)
		return t;
	for (size_t i = 0; i < bs.size(); i++) {
		const auto &b = bs[i];
		int idx = (int)i + 1;
		for (const auto &n : b.abbr) {
			if (b.number) {
				add(std::to_string(b.number) + " " + n, idx);
				add(std::string(romans[b.number]) + " " + n, idx);
			} else {
				add(n, idx);
			}
		}
	}
	return t;
}

const AliasTable &aliases(bool spoken)
{
	static const AliasTable typedT = buildAliases(false);
	static const AliasTable spokenT = buildAliases(true);
	return spoken ? spokenT : typedT;
}

bool isDigits(const std::string &s)
{
	return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); });
}

const std::map<std::string, int> &unitWords()
{
	static const std::map<std::string, int> m = {
		{"zero", 0},     {"one", 1},      {"two", 2},        {"three", 3},     {"four", 4},
		{"five", 5},     {"six", 6},      {"seven", 7},      {"eight", 8},     {"nine", 9},
		{"ten", 10},     {"eleven", 11},  {"twelve", 12},    {"thirteen", 13}, {"fourteen", 14},
		{"fifteen", 15}, {"sixteen", 16}, {"seventeen", 17}, {"eighteen", 18}, {"nineteen", 19},
	};
	return m;
}

const std::map<std::string, int> &tensWords()
{
	static const std::map<std::string, int> m = {
		{"twenty", 20}, {"thirty", 30},  {"forty", 40},  {"fourty", 40}, {"fifty", 50},
		{"sixty", 60},  {"seventy", 70}, {"eighty", 80}, {"ninety", 90},
	};
	return m;
}

int lookup(const std::map<std::string, int> &m, const std::string &k)
{
	auto it = m.find(k);
	return it == m.end() ? -1 : it->second;
}

// Converts runs of number words into digit tokens.
std::vector<std::string> convertNumberWords(const std::vector<std::string> &in)
{
	std::vector<std::string> out;
	const auto &units = unitWords();
	const auto &tens = tensWords();
	size_t i = 0;
	auto peek = [&](size_t k) -> const std::string & {
		static const std::string empty;
		return k < in.size() ? in[k] : empty;
	};
	while (i < in.size()) {
		const std::string &w = in[i];
		if (w == "first" || w == "1st") {
			out.push_back("1");
			i++;
			continue;
		}
		if (w == "second" || w == "2nd") {
			out.push_back("2");
			i++;
			continue;
		}
		if (w == "third" || w == "3rd") {
			out.push_back("3");
			i++;
			continue;
		}
		int value = -1;
		size_t j = i;
		// [unit|a] hundred [and]
		int u = lookup(units, peek(j));
		if (((u >= 1 && u <= 9) || peek(j) == "a") && peek(j + 1) == "hundred") {
			value = (u >= 1 ? u : 1) * 100;
			j += 2;
			if (peek(j) == "and" && (lookup(units, peek(j + 1)) >= 0 || lookup(tens, peek(j + 1)) >= 0))
				j++;
		} else if (peek(j) == "hundred") {
			value = 100;
			j++;
		}
		int t = lookup(tens, peek(j));
		if (t >= 0) {
			value = (value < 0 ? 0 : value) + t;
			j++;
			int u2 = lookup(units, peek(j));
			if (u2 >= 1 && u2 <= 9) {
				value += u2;
				j++;
			}
		} else {
			int u3 = lookup(units, peek(j));
			if (u3 >= 0) {
				value = (value < 0 ? 0 : value) + u3;
				j++;
			}
		}
		if (value >= 0 && j > i) {
			out.push_back(std::to_string(value));
			i = j;
		} else {
			out.push_back(w);
			i++;
		}
	}
	return out;
}

bool isAlpha(unsigned char c)
{
	return std::isalpha(c) != 0;
}

std::optional<int> toInt(const std::string &s)
{
	if (!isDigits(s) || s.size() > 4)
		return std::nullopt;
	return std::stoi(s);
}

// Matches the longest book alias starting at token i. Returns book index and token count.
std::pair<int, size_t> matchBook(const std::vector<std::string> &tok, size_t i, bool spoken)
{
	const auto &t = aliases(spoken);
	size_t maxLen = std::min(t.maxTokens, tok.size() - i);
	for (size_t len = maxLen; len >= 1; len--) {
		std::string key = tok[i];
		for (size_t k = 1; k < len; k++)
			key += " " + tok[i + k];
		auto it = t.map.find(key);
		if (it != t.map.end())
			return {it->second, len};
	}
	return {0, 0};
}

bool isChapterWord(const std::string &w)
{
	return w == "chapter" || w == "chapters" || w == "chap" || w == "chp";
}
bool isVerseWord(const std::string &w)
{
	return w == "verse" || w == "verses" || w == "v" || w == "vs" || w == "vv" || w == "vers";
}
bool isRangeWord(const std::string &w)
{
	return w == "-" || w == "to" || w == "through" || w == "thru" || w == "till" || w == "until" || w == "and";
}

// Parses "<chapter>[:<verse>[-<verse>]]" and its spoken forms starting at token j.
bool parseNumbers(const std::vector<std::string> &tok, size_t j, bool spoken, Reference &ref)
{
	auto at = [&](size_t k) -> std::string {
		return k < tok.size() ? tok[k] : std::string();
	};
	if (isChapterWord(at(j)))
		j++;
	auto ch = toInt(at(j));
	if (!ch) {
		if (spoken)
			return false;
		ref.tokenEnd = j;
		return true; // book only
	}
	ref.chapter = *ch;
	j++;

	std::optional<int> verse;
	size_t save = j;
	if (at(j) == "and" && isVerseWord(at(j + 1)))
		j++;
	if (at(j) == ":" || isVerseWord(at(j))) {
		verse = toInt(at(j + 1));
		j = verse ? j + 2 : save;
	} else if (auto v = toInt(at(j))) {
		verse = v;
		j++;
	}

	if (verse) {
		ref.verseStart = ref.verseEnd = *verse;
		if (isRangeWord(at(j))) {
			size_t k = j + 1;
			if (isVerseWord(at(k)))
				k++;
			auto end = toInt(at(k));
			// "16 and 17" is a range; "3:16 and 5" is not.
			bool ok = end && *end > *verse && (at(j) != "and" || *end == *verse + 1);
			// "John 3:16-4:2": ignore the second chapter, keep the first verse.
			if (ok && at(k + 1) == ":")
				ok = false;
			if (ok) {
				ref.verseEnd = *end;
				j = k + 1;
			}
		}
	}

	int maxCh = chapterCount(ref.book);
	if (maxCh == 1 && !verse && ref.chapter > 1) {
		// "Jude 3" means Jude 1:3.
		ref.verseStart = ref.verseEnd = ref.chapter;
		ref.chapter = 1;
	}
	if (ref.chapter < 1 || ref.chapter > maxCh)
		return false;
	if (ref.verseStart < 0 || ref.verseStart > 176)
		return false;
	ref.tokenEnd = j;
	return true;
}

} // namespace

const std::string &bookName(int book)
{
	static std::vector<std::string> names = [] {
		std::vector<std::string> n;
		for (const auto &b : books())
			n.emplace_back(b.name);
		return n;
	}();
	static const std::string empty;
	return book >= 1 && book <= kBookCount ? names[(size_t)book - 1] : empty;
}

const std::string &bookAbbrev(int book)
{
	static std::vector<std::string> names = [] {
		std::vector<std::string> n;
		for (const auto &b : books())
			n.emplace_back(b.abbrev);
		return n;
	}();
	static const std::string empty;
	return book >= 1 && book <= kBookCount ? names[(size_t)book - 1] : empty;
}

int chapterCount(int book)
{
	return book >= 1 && book <= kBookCount ? books()[(size_t)book - 1].chapters : 0;
}

std::vector<std::string> normalizeTokens(const std::string &text, bool spoken)
{
	(void)spoken;
	std::string s;
	s.reserve(text.size() + 16);
	for (size_t i = 0; i < text.size(); i++) {
		unsigned char c = (unsigned char)text[i];
		unsigned char prev = i ? (unsigned char)text[i - 1] : 0;
		unsigned char next = i + 1 < text.size() ? (unsigned char)text[i + 1] : 0;
		// En dash / em dash (UTF-8 E2 80 93 / E2 80 94) act as range separators.
		if (c == 0xE2 && next == 0x80 && i + 2 < text.size() &&
		    ((unsigned char)text[i + 2] == 0x93 || (unsigned char)text[i + 2] == 0x94)) {
			s += " - ";
			i += 2;
			continue;
		}
		if (std::isalnum(c)) {
			s += (char)std::tolower(c);
		} else if ((c == '.' || c == ':') && std::isdigit(prev) && std::isdigit(next)) {
			s += " : ";
		} else if (c == '-') {
			s += (isAlpha(prev) && isAlpha(next)) ? " " : " - ";
		} else if (c == '\'' || (c == 0xE2 && next == 0x80)) {
			// Drop apostrophes ("god's" -> "gods"); skip the rest of a curly quote.
			if (c == 0xE2)
				i += 2;
		} else {
			s += ' ';
		}
	}
	std::vector<std::string> raw;
	size_t i = 0;
	while (i < s.size()) {
		while (i < s.size() && s[i] == ' ')
			i++;
		size_t st = i;
		while (i < s.size() && s[i] != ' ')
			i++;
		if (i > st)
			raw.push_back(s.substr(st, i - st));
	}
	// Split "1cor" / "jn3", but keep ordinals like "1st" whole.
	std::vector<std::string> split;
	for (const auto &w : raw) {
		if (w.size() >= 3 && std::isdigit((unsigned char)w[0])) {
			std::string suf = w.substr(w.size() - 2);
			std::string num = w.substr(0, w.size() - 2);
			if ((suf == "st" || suf == "nd" || suf == "rd" || suf == "th") && isDigits(num)) {
				split.push_back(num == "1" ? "1st" : num == "2" ? "2nd" : num == "3" ? "3rd" : num);
				continue;
			}
		}
		std::string cur;
		for (size_t k = 0; k < w.size(); k++) {
			if (k &&
			    (bool)std::isdigit((unsigned char)w[k]) != (bool)std::isdigit((unsigned char)w[k - 1])) {
				split.push_back(cur);
				cur.clear();
			}
			cur += w[k];
		}
		if (!cur.empty())
			split.push_back(cur);
	}
	return convertNumberWords(split);
}

std::optional<Reference> parseTyped(const std::string &text)
{
	auto tok = normalizeTokens(text, false);
	if (tok.empty())
		return std::nullopt;
	size_t start = 0;
	if (tok.size() > 3 && tok[0] == "the" && tok[1] == "book" && tok[2] == "of")
		start = 3;
	auto [book, len] = matchBook(tok, start, false);
	if (!book)
		return std::nullopt;
	Reference ref;
	ref.book = book;
	if (!parseNumbers(tok, start + len, false, ref))
		return std::nullopt;
	// Anything left over that isn't a number means this was probably a word search
	// ("job interview", "mark of the beast").
	if (ref.tokenEnd < tok.size())
		return std::nullopt;
	return ref;
}

std::vector<Reference> findSpoken(const std::string &transcript)
{
	std::vector<Reference> out;
	auto tok = normalizeTokens(transcript, true);
	size_t i = 0;
	while (i < tok.size()) {
		auto [book, len] = matchBook(tok, i, true);
		if (book) {
			Reference ref;
			ref.book = book;
			if (parseNumbers(tok, i + len, true, ref)) {
				out.push_back(ref);
				i = ref.tokenEnd;
				continue;
			}
		}
		i++;
	}
	return out;
}

std::string formatReference(const Reference &ref, bool abbreviate)
{
	if (!ref.book)
		return {};
	std::string name = abbreviate ? bookAbbrev(ref.book) : bookName(ref.book);
	if (ref.book == 19 && ref.chapter && !abbreviate)
		name = "Psalm";
	if (!ref.chapter)
		return name;
	std::string s = name + " " + std::to_string(ref.chapter);
	if (ref.verseStart) {
		s += ":" + std::to_string(ref.verseStart);
		if (ref.verseEnd > ref.verseStart)
			s += "-" + std::to_string(ref.verseEnd);
	}
	return s;
}

} // namespace ws
