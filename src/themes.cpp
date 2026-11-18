// Word & Song for OBS — lower-third themes (built-in and user-saved).
#include "themes.hpp"

#include <obs-module.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <mutex>

namespace ws {
namespace {

std::mutex g_mutex;
std::vector<ThemeEntry> g_user;
std::vector<ThemeEntry> g_builtIn;
std::atomic<uint64_t> g_revision{1};

// Colours are 0xAABBGGRR, as OBS stores them.
constexpr long long kWhite = 0xFFFFFFFF;
constexpr long long kGold = 0xFF4EC1F2; // #F2C14E
constexpr long long kRed = 0xFF3B3BE5;  // #E53B3B
constexpr long long kBlue = 0xFFB06A2B; // #2B6AB0
constexpr long long kInk = 0xFF1E1E1E;  // #1E1E1E
constexpr long long kTeal = 0xFF9DB52A; // #2AB59D

struct Look {
	int style = StyleCard;
	long long text = kWhite, title = kGold, accent = kGold, box = 0xD9141010;
	int radius = 18, padding = 36, width = 84, margin = 56, titleSize = 30, bodySize = 46;
	bool center = false, upper = true, shadow = true, bold = false;
};

std::string toJson(const Look &l)
{
	obs_data_t *d = obs_data_create();
	obs_data_set_int(d, "style", l.style);
	obs_data_set_int(d, "text_color", l.text);
	obs_data_set_int(d, "title_color", l.title);
	obs_data_set_int(d, "accent_color", l.accent);
	obs_data_set_int(d, "box_color", l.box);
	obs_data_set_int(d, "radius", l.radius);
	obs_data_set_int(d, "padding", l.padding);
	obs_data_set_int(d, "box_width", l.width);
	obs_data_set_int(d, "margin", l.margin);
	obs_data_set_int(d, "title_size", l.titleSize);
	obs_data_set_int(d, "align", l.center ? 1 : 0);
	obs_data_set_bool(d, "upper_title", l.upper);
	obs_data_set_bool(d, "shadow", l.shadow);
	// Font size/weight only: the face stays the platform default so themes work everywhere.
	obs_data_t *font = obs_data_create();
	obs_data_set_int(font, "size", l.bodySize);
	obs_data_set_int(font, "flags", l.bold ? OBS_FONT_BOLD : 0);
	obs_data_set_obj(d, "font", font);
	obs_data_release(font);
	std::string json = obs_data_get_json(d);
	obs_data_release(d);
	return json;
}

std::vector<ThemeEntry> makeBuiltIns()
{
	std::vector<ThemeEntry> t;
	auto add = [&](const char *id, const char *name, const Look &l) {
		t.push_back({id, name, true, toJson(l)});
	};

	Look midnight; // the original look
	add("midnight", "Midnight Card", midnight);

	Look bar;
	bar.style = StyleBar;
	bar.box = 0xE6000000;
	bar.radius = 0;
	bar.margin = 0;
	bar.padding = 30;
	add("gold-bar", "Gold Bar", bar);

	Look white;
	white.box = 0xF2FFFFFF;
	white.text = kInk;
	white.title = kBlue;
	white.accent = kBlue;
	white.shadow = false;
	white.radius = 10;
	add("clean-white", "Clean White", white);

	Look tag;
	tag.style = StyleTag;
	tag.box = 0xE01A1A1A;
	tag.accent = kRed;
	tag.title = kWhite;
	tag.radius = 6;
	add("red-tag", "Red Tag", tag);

	Look split;
	split.style = StyleSplit;
	split.box = 0xE6181818;
	split.accent = kBlue;
	split.title = kWhite;
	split.radius = 12;
	add("blue-split", "Blue Split", split);

	Look minimal;
	minimal.style = StyleClean;
	minimal.box = 0x00000000;
	minimal.center = true;
	minimal.title = kGold;
	minimal.bodySize = 50;
	add("minimal", "Minimal (text only)", minimal);

	Look lyrics;
	lyrics.style = StyleClean;
	lyrics.box = 0x00000000;
	lyrics.center = true;
	lyrics.bodySize = 56;
	lyrics.bold = true;
	lyrics.upper = true;
	lyrics.title = 0xB3FFFFFF;
	add("lyrics-bold", "Lyrics – Bold Centered", lyrics);

	Look teal;
	teal.style = StyleBar;
	teal.box = 0xF0201A14;
	teal.accent = kTeal;
	teal.title = kTeal;
	teal.radius = 0;
	teal.margin = 40;
	teal.width = 100;
	add("teal-bar", "Teal Bar", teal);
	return t;
}

std::string slug(const std::string &name)
{
	std::string s;
	for (unsigned char c : name) {
		if (std::isalnum(c))
			s += (char)std::tolower(c);
		else if (!s.empty() && s.back() != '-')
			s += '-';
	}
	while (!s.empty() && s.back() == '-')
		s.pop_back();
	return "user-" + (s.empty() ? std::string("theme") : s);
}

} // namespace

ThemeLibrary::ThemeLibrary()
{
	g_builtIn = makeBuiltIns();
}

ThemeLibrary &ThemeLibrary::instance()
{
	static ThemeLibrary lib;
	return lib;
}

const std::vector<std::string> &ThemeLibrary::lookKeys()
{
	static const std::vector<std::string> keys = {"style",       "font",           "title_size",  "text_color",
						      "title_color", "accent_color",   "box_color",   "box_width",
						      "margin",      "padding",        "radius",      "align",
						      "show_title",  "title_position", "upper_title", "shadow",
						      "max_lines",   "fade_ms"};
	return keys;
}

std::string ThemeLibrary::extractLook(obs_data *settings)
{
	obs_data_t *out = obs_data_create();
	for (const auto &k : lookKeys()) {
		obs_data_item_t *item = obs_data_item_byname(settings, k.c_str());
		if (!item)
			continue;
		// Includes values that are only defaults, so the theme is complete.
		switch (obs_data_item_gettype(item)) {
		case OBS_DATA_NUMBER:
			obs_data_set_int(out, k.c_str(), obs_data_item_get_int(item));
			break;
		case OBS_DATA_BOOLEAN:
			obs_data_set_bool(out, k.c_str(), obs_data_item_get_bool(item));
			break;
		case OBS_DATA_OBJECT: {
			obs_data_t *o = obs_data_item_get_obj(item);
			obs_data_set_obj(out, k.c_str(), o);
			obs_data_release(o);
			break;
		}
		case OBS_DATA_STRING:
			obs_data_set_string(out, k.c_str(), obs_data_item_get_string(item));
			break;
		default:
			break;
		}
		obs_data_item_release(&item);
	}
	std::string json = obs_data_get_json(out);
	obs_data_release(out);
	return json;
}

void ThemeLibrary::load(const std::string &path)
{
	path_ = path;
	obs_data_t *root = obs_data_create_from_json_file(path.c_str());
	std::vector<ThemeEntry> user;
	if (root) {
		obs_data_array_t *arr = obs_data_get_array(root, "themes");
		for (size_t i = 0; arr && i < obs_data_array_count(arr); i++) {
			obs_data_t *t = obs_data_array_item(arr, i);
			obs_data_t *look = obs_data_get_obj(t, "look");
			ThemeEntry e{obs_data_get_string(t, "id"), obs_data_get_string(t, "name"), false,
				     look ? obs_data_get_json(look) : "{}"};
			if (!e.id.empty() && !e.name.empty())
				user.push_back(e);
			obs_data_release(look);
			obs_data_release(t);
		}
		obs_data_array_release(arr);
		obs_data_release(root);
	}
	std::lock_guard<std::mutex> lock(g_mutex);
	g_user = std::move(user);
	g_revision++;
}

bool ThemeLibrary::save() const
{
	if (path_.empty())
		return false;
	obs_data_t *root = obs_data_create();
	obs_data_array_t *arr = obs_data_array_create();
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		for (const auto &e : g_user) {
			obs_data_t *t = obs_data_create();
			obs_data_set_string(t, "id", e.id.c_str());
			obs_data_set_string(t, "name", e.name.c_str());
			obs_data_t *look = obs_data_create_from_json(e.json.c_str());
			obs_data_set_obj(t, "look", look);
			obs_data_release(look);
			obs_data_array_push_back(arr, t);
			obs_data_release(t);
		}
	}
	obs_data_set_array(root, "themes", arr);
	obs_data_array_release(arr);
	bool ok = obs_data_save_json_safe(root, path_.c_str(), "tmp", "bak");
	obs_data_release(root);
	return ok;
}

std::vector<ThemeEntry> ThemeLibrary::list() const
{
	std::lock_guard<std::mutex> lock(g_mutex);
	std::vector<ThemeEntry> all = g_builtIn;
	all.insert(all.end(), g_user.begin(), g_user.end());
	return all;
}

bool ThemeLibrary::find(const std::string &id, ThemeEntry &out) const
{
	std::lock_guard<std::mutex> lock(g_mutex);
	for (const auto *v : {&g_user, &g_builtIn})
		for (const auto &e : *v)
			if (e.id == id) {
				out = e;
				return true;
			}
	return false;
}

std::string ThemeLibrary::addOrReplace(const std::string &name, const std::string &lookJson)
{
	std::string id = slug(name);
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		auto it = std::find_if(g_user.begin(), g_user.end(), [&](const ThemeEntry &e) { return e.id == id; });
		if (it != g_user.end()) {
			it->name = name;
			it->json = lookJson;
		} else {
			g_user.push_back({id, name, false, lookJson});
		}
	}
	g_revision++;
	save();
	return id;
}

bool ThemeLibrary::remove(const std::string &id)
{
	bool removed = false;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		auto it = std::remove_if(g_user.begin(), g_user.end(), [&](const ThemeEntry &e) { return e.id == id; });
		removed = it != g_user.end();
		g_user.erase(it, g_user.end());
	}
	if (removed) {
		g_revision++;
		save();
	}
	return removed;
}

uint64_t ThemeLibrary::revision() const
{
	return g_revision.load();
}

} // namespace ws
