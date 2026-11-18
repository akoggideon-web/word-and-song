// Word & Song for OBS — lower-third themes (built-in and user-saved), like ProPresenter themes.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct obs_data;

namespace ws {

// Visual style families a theme can use.
enum ThemeStyle { StyleCard = 0, StyleBar = 1, StyleTag = 2, StyleClean = 3, StyleSplit = 4 };

struct ThemeEntry {
	std::string id;
	std::string name;
	bool builtIn = false;
	std::string json; // OBS settings JSON holding only the "look" keys
};

class ThemeLibrary {
public:
	static ThemeLibrary &instance();

	// Loads user themes from a JSON file (built-ins are always present). Thread-safe.
	void load(const std::string &path);
	bool save() const;

	std::vector<ThemeEntry> list() const;
	bool find(const std::string &id, ThemeEntry &out) const;
	// Saves a user theme; replaces one with the same name. Returns its id.
	std::string addOrReplace(const std::string &name, const std::string &lookJson);
	bool remove(const std::string &id); // user themes only
	uint64_t revision() const;

	// The settings keys that make up a "look" (everything except layout and content filters).
	static const std::vector<std::string> &lookKeys();
	// Copies only the look keys out of a display source's settings, as JSON.
	static std::string extractLook(obs_data *settings);

private:
	ThemeLibrary();
	std::string path_;
};

} // namespace ws
