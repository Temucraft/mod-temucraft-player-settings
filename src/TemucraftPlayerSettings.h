/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

// Choices a player made in mod-temucraft-player-settings that other Temucraft modules read.
// Stored on the player (CustomData), so a module that reads them still builds and works without the settings module.
// (Same file in mod-temucraft-player-settings and mod-temucraft-classes.)
#pragma once

#include "DataMap.h"
#include "Player.h"

// Spell pushback: -1 = the server's Temucraft.Pushback.Mode, 0 = off (today's game), 1 = Wrath, 2 = Vanilla/TBC.
struct TemucraftPushbackChoice : DataMap::Base
{
    int32 mode = -1;
};

inline int32 TemucraftPlayerPushbackMode(Player const* player, int32 serverMode)
{
    if (player)
        if (TemucraftPushbackChoice const* choice = player->CustomData.Get<TemucraftPushbackChoice>("TemucraftPushback"))
            if (choice->mode >= 0)
                return choice->mode;
    return serverMode;
}

inline void TemucraftSetPlayerPushbackMode(Player* player, int32 mode)
{
    player->CustomData.GetDefault<TemucraftPushbackChoice>("TemucraftPushback")->mode = mode;
}
