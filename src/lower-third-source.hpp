// Word & Song for OBS — display source registration and shared rendering helpers.
#pragma once

#include "live-state.hpp"

#include <QImage>

#include <functional>
#include <string>

struct obs_data;

namespace ws {

constexpr const char *kDisplaySourceId = "word_and_song_display";

void registerDisplaySource();

// Called when someone clicks "Edit themes…" in a display's properties.
void setEditThemesHandler(std::function<void()> handler);

// Fills in default values for every look key (font, colours, sizes…).
void applyLookDefaults(obs_data *settings);

// Renders a theme the way a lower-third display would, over a stand-in video frame.
QImage renderThemePreview(const std::string &lookJson, ContentKind kind, int width, int height);

} // namespace ws
