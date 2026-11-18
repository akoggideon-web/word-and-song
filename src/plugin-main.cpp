/*
Word & Song for OBS — scripture and song lyrics for church livestreams.
Copyright (C) 2026 Stories by Akog

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "bible.hpp"
#include "dock.hpp"
#include "live-state.hpp"
#include "lower-third-source.hpp"
#include "themes.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QMainWindow>
#include <QMetaObject>
#include <QPointer>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

QPointer<ws::WordSongDock> g_dock;

struct HotkeyDef {
	const char *name;
	const char *textKey;
	const char *slot;
	obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
};

HotkeyDef g_hotkeys[] = {
	{"word_and_song.show", "Hotkey.Show", "showPreview"},
	{"word_and_song.clear", "Hotkey.Clear", "clearScreen"},
	{"word_and_song.next", "Hotkey.Next", "next"},
	{"word_and_song.previous", "Hotkey.Previous", "previous"},
};

void hotkeyPressed(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed || !g_dock)
		return;
	auto *def = static_cast<HotkeyDef *>(data);
	// Hotkeys fire on OBS's hotkey thread; hop to the UI thread.
	QMetaObject::invokeMethod(g_dock, def->slot, Qt::QueuedConnection);
}

void saveCallback(obs_data_t *saveData, bool saving, void *)
{
	if (saving) {
		obs_data_t *obj = obs_data_create();
		for (auto &h : g_hotkeys) {
			obs_data_array_t *arr = obs_hotkey_save(h.id);
			obs_data_set_array(obj, h.name, arr);
			obs_data_array_release(arr);
		}
		obs_data_set_obj(saveData, "word-and-song", obj);
		obs_data_release(obj);
		if (g_dock)
			g_dock->saveAll();
	} else {
		obs_data_t *obj = obs_data_get_obj(saveData, "word-and-song");
		if (!obj)
			return;
		for (auto &h : g_hotkeys) {
			obs_data_array_t *arr = obs_data_get_array(obj, h.name);
			obs_hotkey_load(h.id, arr);
			obs_data_array_release(arr);
		}
		obs_data_release(obj);
	}
}

void frontendEvent(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_EXIT || event == OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN) {
		if (g_dock) {
			g_dock->saveAll();
			g_dock->shutdown();
		}
		ws::clearLive();
	}
}

} // namespace

bool obs_module_load(void)
{
	ws::registerDisplaySource();

	char *bundled = obs_module_file("bibles");
	char *config = obs_module_config_path("");
	QString configDir = QString::fromUtf8(config ? config : "");
	ws::BibleLibrary::instance().scan(QString::fromUtf8(bundled ? bundled : ""),
					  configDir + QStringLiteral("/bibles"));
	bfree(bundled);
	bfree(config);
	ws::ThemeLibrary::instance().load((configDir + QStringLiteral("/themes.json")).toStdString());

	auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	auto *dock = new ws::WordSongDock(configDir, main);
	g_dock = dock;
	if (!obs_frontend_add_dock_by_id("word_and_song_dock", obs_module_text("DockTitle"), dock)) {
		obs_log(LOG_WARNING, "could not add the control dock");
		delete dock;
	}

	for (auto &h : g_hotkeys)
		h.id = obs_hotkey_register_frontend(h.name, obs_module_text(h.textKey), hotkeyPressed, &h);
	obs_frontend_add_save_callback(saveCallback, nullptr);
	obs_frontend_add_event_callback(frontendEvent, nullptr);

	obs_log(LOG_INFO, "plugin loaded (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_save_callback(saveCallback, nullptr);
	obs_frontend_remove_event_callback(frontendEvent, nullptr);
	for (auto &h : g_hotkeys)
		obs_hotkey_unregister(h.id);
	obs_log(LOG_INFO, "plugin unloaded");
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Scripture and song lyrics for church livestreams, with offline speech detection.";
}
