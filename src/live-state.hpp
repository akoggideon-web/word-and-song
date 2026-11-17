// Word & Song for OBS — what is currently on screen, shared by the dock and display sources.
#pragma once

#include <cstdint>
#include <string>

namespace ws {

enum class ContentKind { None, Scripture, Lyrics };

struct LiveContent {
	ContentKind kind = ContentKind::None;
	std::string title; // "John 3:16 (KJV)" or song section, may be empty
	std::string body;  // verse text or lyric lines ('\n' separated)
	bool visible = false;
};

// Thread-safe. Every change bumps a revision number that display sources poll each frame.
void setLive(const LiveContent &content);
void clearLive();
LiveContent getLive(uint64_t *revision = nullptr);
uint64_t liveRevision();

} // namespace ws
