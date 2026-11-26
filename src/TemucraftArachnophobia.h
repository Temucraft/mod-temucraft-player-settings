/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

// Arachnophobia Mode (mod-temucraft-player-settings, TemucraftArachnophobia.cpp): a player who turns it on sees
// spiders as crabs, nerubians as makrura and so on. Only the packets sent to him change; nobody else sees a
// difference and the creatures' size, reach and abilities stay the same.
#pragma once

class Player;

// Turns the swap on or off for this player (the settings window calls it at login and on every change). When the
// value changes while he is in the world, every swapped unit he can see is sent again, so it changes at once.
void TemucraftArachnophobiaSet(Player* player, bool on);

// Whether the swap is on for this player.
bool TemucraftArachnophobiaIsOn(Player const* player);

// How many displays the world table swaps (0: the table is missing or empty).
uint32 TemucraftArachnophobiaDisplayCount();
