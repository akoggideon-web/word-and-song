// Word & Song for OBS — what is currently on screen, shared by the dock and display sources.
#pragma once

#include <cstdint>
#include <string>

namespace ws {

enum class ContentKind { None, Scripture, Lyrics, Card, Graphic };

struct LiveContent {
	ContentKind kind = ContentKind::None;
	std::string title;     // "John 3:16 (KJV)", song section, or a card's small line
	std::string body;      // verse text, lyric lines ('\n' separated), or a card's main line
	std::string themeId;   // theme chosen in the dock for this content (may be empty)
	std::string imagePath; // Graphic only: image file to show full frame
	bool visible = false;
};

// Thread-safe. Every change bumps a revision number that display sources poll each frame.
void setLive(const LiveContent &content);
void clearLive();
LiveContent getLive(uint64_t *revision = nullptr);
uint64_t liveRevision();

} // namespace ws
