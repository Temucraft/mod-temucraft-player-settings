/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

// Arachnophobia Mode, like retail (Options > Accessibility > General): a player who turns it on sees
//   spiders (bone, crystal, fire, lava, shale, spider demons, spiderlings) -> crabs,
//   giant spiders and tarantulas                                         -> big (deep sea) crabs,
//   mechanical spiders (spider tanks, mechanical spider pets)            -> hermit crab,
//   nerubians, crypt fiends, crypt lords, dryders                        -> makrura (lobstrok),
//   flying nerubians                                                     -> wasps (3.3.5 has no skitterfly),
//   spider mounts                                                        -> crab mount.
// Creatures, bosses, companion and hunter pets, players in a spider form and spider mounts: everything that is a
// unit. Webs, icons, spells and loading screens stay as they are.
//
// How: the client draws whatever display id the server sends. When the server builds a unit's values for a player
// with the setting on (UnitScript::OnPatchValuesUpdate), UNIT_FIELD_DISPLAYID, UNIT_FIELD_NATIVEDISPLAYID and
// UNIT_FIELD_MOUNTDISPLAYID are replaced from the world table mod_temucraft_arachnophobia, and OBJECT_FIELD_SCALE_X is
// multiplied by the row's scale so the crab is as big as the spider was. Only that player's packets change: the
// creature keeps its real size, reach, hit box and abilities for everybody. The table is made from the DBCs by
// tools/make_arachnophobia_sql.py. The setting itself (window line, command, account/character, server default) is in
// TemucraftPlayerSettings.cpp like the other settings.

#include "Cell.h"
#include "CellImpl.h"
#include "DatabaseEnv.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Log.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "TemucraftArachnophobia.h"
#include "Unit.h"

#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

namespace
{
    struct Swap
    {
        uint32 display;   // what the player sees instead
        float scale;      // object scale multiplier, so the new model is as big as the old one
    };

    // Read once at startup (after that only read, from every map thread).
    std::unordered_map<uint32, Swap> sSwaps;

    // Players (character guid) who have it on. Written at login, logout and on a change; read while packets are built.
    std::shared_mutex sOnLock;
    std::unordered_set<uint32> sOn;

    Swap const* Find(uint32 display)
    {
        if (!display)
            return nullptr;
        auto it = sSwaps.find(display);
        return it == sSwaps.end() ? nullptr : &it->second;
    }

    bool IsOn(Player const* player)
    {
        std::shared_lock lock(sOnLock);
        return sOn.count(player->GetGUID().GetCounter()) > 0;
    }

    bool Swapped(Unit const* unit)
    {
        return Find(unit->GetDisplayId()) || Find(unit->GetNativeDisplayId()) || Find(unit->GetMountID());
    }

    // Sends the unit's display fields again to everybody who sees it; each viewer gets his own value.
    void Resend(Unit* unit)
    {
        if (Find(unit->GetDisplayId()))
        {
            unit->ForceValuesUpdateAtIndex(UNIT_FIELD_DISPLAYID);
            unit->ForceValuesUpdateAtIndex(OBJECT_FIELD_SCALE_X);
        }
        if (Find(unit->GetNativeDisplayId()))
            unit->ForceValuesUpdateAtIndex(UNIT_FIELD_NATIVEDISPLAYID);
        if (Find(unit->GetMountID()))
            unit->ForceValuesUpdateAtIndex(UNIT_FIELD_MOUNTDISPLAYID);
    }

    struct AnyUnit
    {
        bool operator()(Unit* /*unit*/) const { return true; }
    };

    // Every swapped unit this player has on his screen (and himself) is sent again.
    void RefreshAround(Player* player)
    {
        if (!player->IsInWorld() || sSwaps.empty())
            return;
        std::list<Unit*> units;
        AnyUnit check;
        Acore::UnitListSearcher<AnyUnit> searcher(player, units, check);
        Cell::VisitObjects(player, searcher, player->GetVisibilityRange());
        for (Unit* unit : units)
            if (unit != player && player->HaveAtClient(unit) && Swapped(unit))
                Resend(unit);
        if (Swapped(player))
            Resend(player);
    }

    void Load()
    {
        sSwaps.clear();
        QueryResult result = WorldDatabase.Query("SELECT `display_from`, `display_to`, `scale` FROM `mod_temucraft_arachnophobia`");
        if (!result)
        {
            LOG_INFO("server.loading", ">> Arachnophobia Mode: table mod_temucraft_arachnophobia missing or empty, nothing is swapped.");
            return;
        }
        do
        {
            Field* f = result->Fetch();
            uint32 to = f[1].Get<uint32>();
            float scale = f[2].Get<float>();
            if (!to || to == f[0].Get<uint32>())
                continue;
            sSwaps[f[0].Get<uint32>()] = { to, scale > 0.0f ? scale : 1.0f };
        } while (result->NextRow());
        LOG_INFO("server.loading", ">> Arachnophobia Mode: {} creature displays can be swapped.", sSwaps.size());
    }
}

void TemucraftArachnophobiaSet(Player* player, bool on)
{
    bool changed;
    {
        std::unique_lock lock(sOnLock);
        changed = on ? sOn.insert(player->GetGUID().GetCounter()).second : sOn.erase(player->GetGUID().GetCounter()) > 0;
    }
    if (changed)
        RefreshAround(player);
}

bool TemucraftArachnophobiaIsOn(Player const* player)
{
    return IsOn(player);
}

uint32 TemucraftArachnophobiaDisplayCount()
{
    return uint32(sSwaps.size());
}

class TemucraftArachnophobia_Unit : public UnitScript
{
public:
    TemucraftArachnophobia_Unit() : UnitScript("TemucraftArachnophobia_Unit", true, {
        UNITHOOK_SHOULD_TRACK_VALUES_UPDATE_POS_BY_INDEX, UNITHOOK_ON_PATCH_VALUES_UPDATE, UNITHOOK_ON_DISPLAYID_CHANGE }) { }

    // Remember where these fields are in the values block, so they can be changed per player.
    // (UNIT_FIELD_DISPLAYID is always remembered by the core.)
    bool ShouldTrackValuesUpdatePosByIndex(Unit const* unit, uint8 /*updateType*/, uint16 index) override
    {
        switch (index)
        {
            case OBJECT_FIELD_SCALE_X:       return Find(unit->GetDisplayId()) != nullptr;
            case UNIT_FIELD_NATIVEDISPLAYID: return Find(unit->GetNativeDisplayId()) != nullptr;
            case UNIT_FIELD_MOUNTDISPLAYID:  return Find(unit->GetMountID()) != nullptr;
            default:                         return false;
        }
    }

    void OnPatchValuesUpdate(Unit const* unit, ByteBuffer& buffer, BuildValuesCachePosPointers& pos, Player* target) override
    {
        if (!target || sSwaps.empty())
            return;

        Swap const* display = pos.UnitFieldDisplayPos >= 0 ? Find(buffer.read<uint32>(pos.UnitFieldDisplayPos)) : nullptr;
        auto scalePos = pos.other.find(OBJECT_FIELD_SCALE_X);
        auto nativePos = pos.other.find(UNIT_FIELD_NATIVEDISPLAYID);
        auto mountPos = pos.other.find(UNIT_FIELD_MOUNTDISPLAYID);
        Swap const* scale = scalePos != pos.other.end() ? Find(unit->GetDisplayId()) : nullptr;
        Swap const* native = nativePos != pos.other.end() ? Find(buffer.read<uint32>(nativePos->second)) : nullptr;
        Swap const* mount = mountPos != pos.other.end() ? Find(buffer.read<uint32>(mountPos->second)) : nullptr;
        if (!display && !scale && !native && !mount)
            return;   // nothing spider-like in this block (almost every block): no lock taken
        if (!IsOn(target))
            return;

        if (display)
            buffer.put<uint32>(pos.UnitFieldDisplayPos, display->display);
        if (scale)
            buffer.put<float>(scalePos->second, buffer.read<float>(scalePos->second) * scale->scale);
        if (native)
            buffer.put<uint32>(nativePos->second, native->display);
        if (mount)
            buffer.put<uint32>(mountPos->second, mount->display);
    }

    // A unit changed its model (transform, polymorph, shapeshift ...): send the scale again with it, so a player who
    // saw a bigger crab doesn't keep that size for the new model (and the other way round).
    void OnDisplayIdChange(Unit* unit, uint32 /*displayId*/) override
    {
        if (!sSwaps.empty() && unit->IsInWorld())
            unit->ForceValuesUpdateAtIndex(OBJECT_FIELD_SCALE_X);
    }
};

class TemucraftArachnophobia_Player : public PlayerScript
{
public:
    TemucraftArachnophobia_Player() : PlayerScript("TemucraftArachnophobia_Player", { PLAYERHOOK_ON_LOGOUT }) { }

    void OnPlayerLogout(Player* player) override
    {
        std::unique_lock lock(sOnLock);
        sOn.erase(player->GetGUID().GetCounter());
    }
};

class TemucraftArachnophobia_World : public WorldScript
{
public:
    TemucraftArachnophobia_World() : WorldScript("TemucraftArachnophobia_World", { WORLDHOOK_ON_STARTUP }) { }

    void OnStartup() override
    {
        Load();
    }
};

void AddTemucraftArachnophobiaScripts()
{
    new TemucraftArachnophobia_Unit();
    new TemucraftArachnophobia_Player();
    new TemucraftArachnophobia_World();
}
