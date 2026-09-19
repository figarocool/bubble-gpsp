//
// Vita "game bubble" generator for Emu4VitaPlus: takes the currently
// selected rom and installs it as its own standalone LiveArea bubble (own
// icon, own title, launches straight into the game, no rom browser).
//
// Ported from bubble-mgba/pnes-bubble/psnes-bubble's bubble_maker.cpp - same
// approach, adapted to Emu4VitaPlus's own File:: helpers, RomNameMap
// (No-Intro CRC32 -> title) and Network (thumbnail download) instead of
// libcross2d's Io/Curl abstractions.
//
#pragma once

#include <cstdint>
#include <string>

// returns empty string on success, or an error message on failure
std::string CreateBubble(const std::string &rom_path, uint32_t rom_crc32);
