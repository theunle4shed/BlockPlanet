#pragma once

// Research (2026-11-21): the frame's display list as the game hands it to the
// RSP. BLOCKPLANET_DL_DUMP=dir turns it on; the research_dl_dump command
// writes the next frame's list (every command, following calls) to
// dir/dl-N.txt. BLOCKPLANET_DL_MUTE=a[,b...] (hex) blanks every call to those
// display lists and every vertex load from those addresses, each frame, to
// see what they draw.

#include <cstdint>

namespace tetrisphere::dl_research {

bool enabled();
void observe(const std::uint8_t* rdram);
void request_dump();

} // namespace tetrisphere::dl_research
