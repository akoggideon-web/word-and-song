// Word & Song for OBS — shared on-screen state.
#include "live-state.hpp"

#include <atomic>
#include <mutex>

namespace ws {
namespace {
std::mutex g_mutex;
LiveContent g_content;
std::atomic<uint64_t> g_revision{1};
} // namespace

void setLive(const LiveContent &content)
{
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_content = content;
	}
	g_revision++;
}

void clearLive()
{
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_content.visible = false;
	}
	g_revision++;
}

LiveContent getLive(uint64_t *revision)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	if (revision)
		*revision = g_revision.load();
	return g_content;
}

uint64_t liveRevision()
{
	return g_revision.load();
}

} // namespace ws
