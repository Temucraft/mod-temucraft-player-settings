/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

// Player settings: every player picks his own rates and options in one window (.temucraft settings).
//
//   Experience       on or off; kill, quest, dungeon finder quest, exploration and pet rates (0 to 3);
//                    kill rates in each battleground (0 to 3).
//   Rest             rested experience growth in game, offline in an inn or city, offline outside, and the most
//                    rested experience you can store (between Blizzard's default and our recommended value).
//   Regeneration     health and mana regeneration, rage and runic power loss out of combat (default to recommended).
//   Reputation       reputation gain (0 to 2).
//   Spell pushback   server default, Vanilla/TBC, Wrath or Retail (no pushback). Read by mod-temucraft-classes.
//   Graveyard        always release at the nearest friendly or neutral graveyard, and a button to go there now.
//   World channel    join the cross-faction World channel at login.
//
// A value can be saved for the whole account or only for this character. A character uses its own value, then the
// account's, then the server's (worldserver.conf). Every rate is relative to the server: a player who picks 2 while
// the server runs 1 gets twice the normal amount; with the server on 2, picking 1 halves it.

#include "Battleground.h"
#include "Channel.h"
#include "ChannelMgr.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "GameGraveyard.h"
#include "GossipDef.h"
#include "Group.h"
#include "Map.h"
#include "Pet.h"
#include "Player.h"
#include "ReputationMgr.h"
#include "ScriptMgr.h"
#include "ScriptedGossip.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "StringConvert.h"
#include "TemucraftModuleText.h"
#include "TemucraftPlayerSettings.h"
#include "World.h"
#include "WorldSession.h"

#include <array>
#include <cmath>
#include <mutex>
#include <optional>
#include <unordered_map>

using namespace Acore::ChatCommands;

namespace
{
    char const* const MODULE = "mod-temucraft-player-settings";
    constexpr uint32 SETTINGS_MENU = 1269900;   // player gossip menu id (own number, not a gossip_menu row)

    enum Kind : uint8 { KIND_TOGGLE, KIND_NUMBER, KIND_CHOICE };

    enum SettingId : uint8
    {
        S_XP_ON, S_XP_KILL, S_XP_QUEST, S_XP_QUEST_DF, S_XP_EXPLORE, S_XP_PET,
        S_BG_AV, S_BG_WSG, S_BG_AB, S_BG_EOTS, S_BG_SOTA, S_BG_IC,
        S_REST_INGAME, S_REST_TAVERN, S_REST_WILD, S_REST_MAX,
        S_REGEN_HEALTH, S_REGEN_MANA, S_RAGE_LOSS, S_RUNIC_LOSS,
        S_REP_GAIN, S_PUSHBACK, S_GRAVEYARD, S_WORLD_CHANNEL,
        S_COUNT
    };

    enum Page : uint8 { PAGE_MAIN, PAGE_XP, PAGE_BG, PAGE_REST, PAGE_REGEN };

    struct SettingInfo
    {
        char const* key;    // database name
        char const* name;
        Kind kind;
        Page page;
        uint32 textId;      // module_string id of the name
    };

    std::array<SettingInfo, S_COUNT> const SETTINGS =
    { {
        { "xp_on",         "Gain experience",                       KIND_TOGGLE, PAGE_XP,    20 },
        { "xp_kill",       "Kill experience",                       KIND_NUMBER, PAGE_XP,    21 },
        { "xp_quest",      "Quest experience",                      KIND_NUMBER, PAGE_XP,    22 },
        { "xp_quest_df",   "Dungeon finder quest experience",       KIND_NUMBER, PAGE_XP,    23 },
        { "xp_explore",    "Exploration experience",                KIND_NUMBER, PAGE_XP,    24 },
        { "xp_pet",        "Pet experience",                        KIND_NUMBER, PAGE_XP,    25 },
        { "xp_bg_av",      "Alterac Valley kills",                  KIND_NUMBER, PAGE_BG,    26 },
        { "xp_bg_wsg",     "Warsong Gulch kills",                   KIND_NUMBER, PAGE_BG,    27 },
        { "xp_bg_ab",      "Arathi Basin kills",                    KIND_NUMBER, PAGE_BG,    28 },
        { "xp_bg_eots",    "Eye of the Storm kills",                KIND_NUMBER, PAGE_BG,    29 },
        { "xp_bg_sota",    "Strand of the Ancients kills",          KIND_NUMBER, PAGE_BG,    30 },
        { "xp_bg_ic",      "Isle of Conquest kills",                KIND_NUMBER, PAGE_BG,    31 },
        { "rest_ingame",   "Rested experience in game",             KIND_NUMBER, PAGE_REST,  32 },
        { "rest_tavern",   "Rested experience offline, inn or city", KIND_NUMBER, PAGE_REST, 33 },
        { "rest_wild",     "Rested experience offline, outside",    KIND_NUMBER, PAGE_REST,  34 },
        { "rest_max",      "Most rested experience (levels)",       KIND_NUMBER, PAGE_REST,  35 },
        { "regen_health",  "Health regeneration",                   KIND_NUMBER, PAGE_REGEN, 36 },
        { "regen_mana",    "Mana regeneration",                     KIND_NUMBER, PAGE_REGEN, 37 },
        { "rage_loss",     "Rage loss out of combat",               KIND_NUMBER, PAGE_REGEN, 38 },
        { "runic_loss",    "Runic power loss out of combat",        KIND_NUMBER, PAGE_REGEN, 39 },
        { "rep_gain",      "Reputation gain",                       KIND_NUMBER, PAGE_MAIN,  40 },
        { "pushback",      "Spell pushback",                        KIND_CHOICE, PAGE_MAIN,  41 },
        { "graveyard",     "Release at the nearest friendly or neutral graveyard", KIND_TOGGLE, PAGE_MAIN, 42 },
        { "world_channel", "Join the World channel at login",       KIND_TOGGLE, PAGE_MAIN,  43 },
    } };

    std::array<char const*, 4> const PUSHBACK_NAMES = { "Server default", "Vanilla/TBC", "Wrath", "Retail (no pushback)" };

    struct Range
    {
        float min = 0.0f;
        float max = 3.0f;
        float blizzard = 1.0f;     // Blizzard's default, shown as a quick pick
        float recommended = 1.0f;  // our recommendation, shown as a quick pick
    };

    struct Config
    {
        bool enable = true;
        bool worldChannel = true;
        std::string worldChannelName = "World";
        uint32 regenHealthSpell = 1269900;
        uint32 regenPowerSpell = 1269901;
        std::array<Range, S_COUNT> ranges;
    } cfg;

    // The server's own value for a setting (what a player gets when neither he nor his account picked one).
    float ServerValue(uint8 id)
    {
        switch (id)
        {
            case S_XP_ON:         return 1.0f;
            case S_XP_KILL:       return sWorld->getRate(RATE_XP_KILL);
            case S_XP_QUEST:      return sWorld->getRate(RATE_XP_QUEST);
            case S_XP_QUEST_DF:   return sWorld->getRate(RATE_XP_QUEST_DF);
            case S_XP_EXPLORE:    return sWorld->getRate(RATE_XP_EXPLORE);
            case S_XP_PET:        return sWorld->getRate(RATE_XP_PET);
            case S_BG_AV:         return sWorld->getRate(RATE_XP_BG_KILL_AV);
            case S_BG_WSG:        return sWorld->getRate(RATE_XP_BG_KILL_WSG);
            case S_BG_AB:         return sWorld->getRate(RATE_XP_BG_KILL_AB);
            case S_BG_EOTS:       return sWorld->getRate(RATE_XP_BG_KILL_EOTS);
            case S_BG_SOTA:       return sWorld->getRate(RATE_XP_BG_KILL_SOTA);
            case S_BG_IC:         return sWorld->getRate(RATE_XP_BG_KILL_IC);
            case S_REST_INGAME:   return sWorld->getRate(RATE_REST_INGAME);
            case S_REST_TAVERN:   return sWorld->getRate(RATE_REST_OFFLINE_IN_TAVERN_OR_CITY);
            case S_REST_WILD:     return sWorld->getRate(RATE_REST_OFFLINE_IN_WILDERNESS);
            case S_REST_MAX:      return sWorld->getRate(RATE_REST_MAX_BONUS);
            case S_REGEN_HEALTH:  return sWorld->getRate(RATE_HEALTH);
            case S_REGEN_MANA:    return sWorld->getRate(RATE_POWER_MANA);
            case S_RAGE_LOSS:     return sWorld->getRate(RATE_POWER_RAGE_LOSS);
            case S_RUNIC_LOSS:    return sWorld->getRate(RATE_POWER_RUNICPOWER_LOSS);
            case S_REP_GAIN:      return sWorld->getRate(RATE_REPUTATION_GAIN);
            case S_PUSHBACK:      return 0.0f;
            case S_GRAVEYARD:     return 0.0f;
            case S_WORLD_CHANNEL: return 1.0f;
            default:              return 0.0f;
        }
    }

    struct PlayerData
    {
        std::array<std::optional<float>, S_COUNT> character;
        std::array<std::optional<float>, S_COUNT> account;
        bool editAccount = true;       // where the window saves changes
        float lastRest = -1.0f;        // rest bonus at the last check
        uint32 restTimer = 0;
        uint32 petXpToRemove = 0;      // pet experience to take back (player picked a lower pet rate than the server)
    };

    std::mutex sLock;
    std::unordered_map<uint32, PlayerData> sData;   // by character guid

    PlayerData* Data(Player const* player)
    {
        auto it = sData.find(player->GetGUID().GetCounter());
        return it == sData.end() ? nullptr : &it->second;
    }

    enum Source : uint8 { SOURCE_SERVER, SOURCE_ACCOUNT, SOURCE_CHARACTER };

    float Value(Player const* player, uint8 id, Source* source = nullptr)
    {
        std::lock_guard lock(sLock);
        if (PlayerData const* data = Data(player))
        {
            if (data->character[id])
            {
                if (source) *source = SOURCE_CHARACTER;
                return *data->character[id];
            }
            if (data->account[id])
            {
                if (source) *source = SOURCE_ACCOUNT;
                return *data->account[id];
            }
        }
        if (source) *source = SOURCE_SERVER;
        return ServerValue(id);
    }

    // How much a player's pick changes the server amount: 1 = unchanged.
    float Ratio(Player const* player, uint8 id)
    {
        float server = ServerValue(id);
        if (server <= 0.0f)
            return 1.0f;   // nothing to scale from
        return Value(player, id) / server;
    }

    bool Toggle(Player const* player, uint8 id) { return Value(player, id) >= 0.5f; }

    std::string Format(float value)
    {
        std::string text = Acore::StringFormat("{:.2f}", value);
        while (text.size() > 1 && text.back() == '0')
            text.pop_back();
        if (text.back() == '.')
            text.pop_back();
        return text;
    }

    char const* Text(Player* player, uint32 id, char const* fallback) { return TemucraftModuleText(MODULE, player, id, fallback); }

    std::string SourceText(Player* player, Source source)
    {
        switch (source)
        {
            case SOURCE_CHARACTER: return Text(player, 3, "this character");
            case SOURCE_ACCOUNT:   return Text(player, 4, "account");
            default:               return Text(player, 5, "server");
        }
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Saving and loading

    void Save(Player* player, uint8 id, std::optional<float> value, bool account)
    {
        uint32 owner = account ? player->GetSession()->GetAccountId() : player->GetGUID().GetCounter();
        {
            std::lock_guard lock(sLock);
            PlayerData& data = sData[player->GetGUID().GetCounter()];
            (account ? data.account : data.character)[id] = value;
        }
        if (value)
            CharacterDatabase.Execute("REPLACE INTO `mod_temucraft_player_settings` (`scope`, `owner`, `setting`, `value`) VALUES ({}, {}, '{}', {})",
                account ? 0 : 1, owner, SETTINGS[id].key, *value);
        else
            CharacterDatabase.Execute("DELETE FROM `mod_temucraft_player_settings` WHERE `scope` = {} AND `owner` = {} AND `setting` = '{}'",
                account ? 0 : 1, owner, SETTINGS[id].key);
    }

    int8 IdByKey(std::string const& key)
    {
        for (uint8 i = 0; i < S_COUNT; ++i)
            if (key == SETTINGS[i].key)
                return int8(i);
        return -1;
    }

    float Clamp(uint8 id, float value)
    {
        switch (SETTINGS[id].kind)
        {
            case KIND_TOGGLE: return value >= 0.5f ? 1.0f : 0.0f;
            case KIND_CHOICE: return std::clamp(std::round(value), 0.0f, float(PUSHBACK_NAMES.size() - 1));
            default:          return std::clamp(value, cfg.ranges[id].min, cfg.ranges[id].max);
        }
    }

    void Load(Player* player)
    {
        PlayerData data;
        if (QueryResult result = CharacterDatabase.Query("SELECT `scope`, `setting`, `value` FROM `mod_temucraft_player_settings` WHERE (`scope` = 0 AND `owner` = {}) OR (`scope` = 1 AND `owner` = {})",
            player->GetSession()->GetAccountId(), player->GetGUID().GetCounter()))
        {
            do
            {
                Field* f = result->Fetch();
                int8 id = IdByKey(f[1].Get<std::string>());
                if (id < 0)
                    continue;
                float value = Clamp(uint8(id), f[2].Get<float>());   // the server may have narrowed a range since
                (f[0].Get<uint8>() == 0 ? data.account : data.character)[id] = value;
            } while (result->NextRow());
        }
        std::lock_guard lock(sLock);
        sData[player->GetGUID().GetCounter()] = data;
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Effects

    void ApplyPushback(Player* player)
    {
        // window choice: 0 server, 1 Vanilla/TBC, 2 Wrath, 3 Retail -> pushback modes -1 server, 2 classic, 1 Wrath, 0 off
        static constexpr std::array<int32, 4> MODES = { -1, 2, 1, 0 };
        uint32 choice = uint32(Value(player, S_PUSHBACK));
        TemucraftSetPlayerPushbackMode(player, MODES[std::min<uint32>(choice, 3)]);
    }

    // Regeneration and power loss through two hidden passive auras whose amounts are the player's change in percent.
    void SetAuraAmounts(Player* player, uint32 spellId, std::array<int32, 3> const& amounts)
    {
        bool needed = amounts[0] || amounts[1] || amounts[2];
        Aura* aura = player->GetAura(spellId);
        if (!needed)
        {
            if (aura)
                player->RemoveAurasDueToSpell(spellId);
            return;
        }
        if (!aura)
            aura = player->AddAura(spellId, player);
        if (!aura)
            return;   // spell row missing: the world SQL was not applied
        for (uint8 i = 0; i < 3; ++i)
            if (AuraEffect* effect = aura->GetEffect(i))
                effect->ChangeAmount(amounts[i]);
    }

    int32 Percent(Player const* player, uint8 id)
    {
        return int32(std::lround((Ratio(player, id) - 1.0f) * 100.0f));
    }

    void ApplyRegen(Player* player)
    {
        SetAuraAmounts(player, cfg.regenHealthSpell, { Percent(player, S_REGEN_HEALTH), Percent(player, S_REGEN_MANA), Percent(player, S_RAGE_LOSS) });
        SetAuraAmounts(player, cfg.regenPowerSpell, { Percent(player, S_RUNIC_LOSS), 0, 0 });
        player->UpdateManaRegen();
    }

    float RestCap(Player* player)
    {
        return float(player->GetUInt32Value(PLAYER_NEXT_LEVEL_XP)) * Value(player, S_REST_MAX) / 2.0f;
    }

    void SetRest(Player* player, float value)
    {
        player->SetRestBonus(std::min(value, RestCap(player)));
        std::lock_guard lock(sLock);
        if (PlayerData* data = Data(player))
            data->lastRest = player->GetRestBonus();
    }

    // In game: the core adds rested experience at the server rate; the part over the player's rate is taken back.
    void UpdateRest(Player* player)
    {
        float last;
        {
            std::lock_guard lock(sLock);
            PlayerData* data = Data(player);
            if (!data)
                return;
            last = data->lastRest;
        }
        float now = player->GetRestBonus();
        float target = now;
        if (last >= 0.0f && now > last)
            target = last + (now - last) * Ratio(player, S_REST_INGAME);
        if (target > RestCap(player))
            target = RestCap(player);
        if (std::fabs(target - now) > 0.01f)
            SetRest(player, target);
        else
        {
            std::lock_guard lock(sLock);
            if (PlayerData* data = Data(player))
                data->lastRest = now;
        }
    }

    // Offline: the core added rested experience while loading the character; scale that part to the player's rate.
    void ApplyOfflineRest(Player* player)
    {
        float before = -1.0f;
        bool inn = false;
        if (QueryResult result = CharacterDatabase.Query("SELECT `rest_bonus`, `is_logout_resting` FROM `characters` WHERE `guid` = {}", player->GetGUID().GetCounter()))
        {
            before = result->Fetch()[0].Get<float>();
            inn = result->Fetch()[1].Get<uint8>() > 0;
        }
        float now = player->GetRestBonus();
        float target = now;
        if (before >= 0.0f && now > before)
            target = before + (now - before) * Ratio(player, inn ? S_REST_TAVERN : S_REST_WILD);
        SetRest(player, target);
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Graveyards: the nearest graveyard on this map that is linked to a zone for the player's faction or for both.

    std::unordered_map<uint32, uint8> sGraveyardTeams;   // graveyard id -> bit 1 Alliance, bit 2 Horde

    void LoadGraveyardTeams()
    {
        sGraveyardTeams.clear();
        if (QueryResult result = WorldDatabase.Query("SELECT `ID`, `Faction` FROM `graveyard_zone`"))
        {
            do
            {
                Field* f = result->Fetch();
                uint32 faction = f[1].Get<uint32>();
                uint8 bits = faction == ALLIANCE ? 1 : faction == HORDE ? 2 : 3;
                sGraveyardTeams[f[0].Get<uint32>()] |= bits;
            } while (result->NextRow());
        }
    }

    bool OpenWorld(Player* player)
    {
        Map* map = player->GetMap();
        return map && !map->Instanceable() && !player->InBattleground() && !player->InArena();
    }

    GraveyardStruct const* NearestGraveyard(Player* player, WorldLocation const& from)
    {
        uint8 bit = player->GetTeamId() == TEAM_ALLIANCE ? 1 : 2;
        GraveyardStruct const* best = nullptr;
        float bestDist = 0.0f;
        for (auto const& [id, graveyard] : sGraveyard->GetGraveyardData())
        {
            if (graveyard.Map != from.GetMapId())
                continue;
            auto team = sGraveyardTeams.find(id);
            if (team == sGraveyardTeams.end() || !(team->second & bit))
                continue;   // not linked to any zone for this faction: some are battleground or event graveyards
            float dist = from.GetExactDist2dSq(graveyard.x, graveyard.y);
            if (!best || dist < bestDist)
            {
                best = &graveyard;
                bestDist = dist;
            }
        }
        return best;
    }

    // ---------------------------------------------------------------------------------------------------------------
    // The window

    enum Sender : uint32
    {
        SENDER_PAGE = 1,     // action = page
        SENDER_SETTING,      // action = setting: open its page of quick picks
        SENDER_TOGGLE,       // action = setting
        SENDER_PICK,         // action = setting * 16 + pick
        SENDER_CLEAR,        // action = setting
        SENDER_SCOPE,
        SENDER_GRAVEYARD_NOW,
        SENDER_CODE,         // action = setting: typed value
        SENDER_CLOSE,
        SENDER_INFO
    };

    void Send(Player* player)
    {
        player->PlayerTalkClass->GetGossipMenu().SetMenuId(SETTINGS_MENU);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, player->GetGUID());
    }

    std::string ValueText(Player* player, uint8 id)
    {
        Source source;
        float value = Value(player, id, &source);
        std::string shown;
        switch (SETTINGS[id].kind)
        {
            case KIND_TOGGLE: shown = value >= 0.5f ? Text(player, 6, "on") : Text(player, 7, "off"); break;
            case KIND_CHOICE: shown = PUSHBACK_NAMES[std::min<uint32>(uint32(value), 3)]; break;
            default:          shown = Format(value); break;
        }
        return Acore::StringFormat("{}: |cff0070dd{}|r ({})", Text(player, SETTINGS[id].textId, SETTINGS[id].name), shown, SourceText(player, source));
    }

    void AddSettingLine(Player* player, uint8 id)
    {
        if (SETTINGS[id].kind == KIND_TOGGLE)
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, ValueText(player, id), SENDER_TOGGLE, id);
        else
            AddGossipItemFor(player, GOSSIP_ICON_TRAINER, ValueText(player, id), SENDER_SETTING, id);
    }

    bool EditAccount(Player* player)
    {
        std::lock_guard lock(sLock);
        PlayerData* data = Data(player);
        return !data || data->editAccount;
    }

    void SendPage(Player* player, Page page)
    {
        ClearGossipMenuFor(player);
        bool account = EditAccount(player);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, account
            ? Text(player, 8, "Changes are saved for: |cff0070ddthe whole account|r (click for this character only)")
            : Text(player, 9, "Changes are saved for: |cff0070ddthis character only|r (click for the whole account)"), SENDER_SCOPE, page);
        for (uint8 id = 0; id < S_COUNT; ++id)
            if (SETTINGS[id].page == page)
                AddSettingLine(player, id);
        if (page == PAGE_MAIN)
        {
            AddGossipItemFor(player, GOSSIP_ICON_TALK, Text(player, 10, "Experience rates"), SENDER_PAGE, PAGE_XP);
            AddGossipItemFor(player, GOSSIP_ICON_TALK, Text(player, 11, "Battleground kill experience"), SENDER_PAGE, PAGE_BG);
            AddGossipItemFor(player, GOSSIP_ICON_TALK, Text(player, 12, "Rested experience"), SENDER_PAGE, PAGE_REST);
            AddGossipItemFor(player, GOSSIP_ICON_TALK, Text(player, 13, "Regeneration"), SENDER_PAGE, PAGE_REGEN);
            if (player->isDead() && OpenWorld(player))
                AddGossipItemFor(player, GOSSIP_ICON_TAXI, Text(player, 14, "Go to the nearest friendly or neutral graveyard now"), SENDER_GRAVEYARD_NOW, 0);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, Text(player, 15, "Close"), SENDER_CLOSE, 0);
        }
        else
            AddGossipItemFor(player, GOSSIP_ICON_TALK, Text(player, 16, "Back"), SENDER_PAGE, PAGE_MAIN);
        Send(player);
    }

    void SendSetting(Player* player, uint8 id)
    {
        ClearGossipMenuFor(player);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, ValueText(player, id), SENDER_INFO, id);
        bool account = EditAccount(player);
        if (SETTINGS[id].kind == KIND_CHOICE)
        {
            for (uint8 pick = 0; pick < PUSHBACK_NAMES.size(); ++pick)
                AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, PUSHBACK_NAMES[pick], SENDER_PICK, id * 16 + pick);
        }
        else
        {
            Range const& range = cfg.ranges[id];
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, Acore::StringFormat(Text(player, 17, "Range {} to {}. Server: {}."), Format(range.min), Format(range.max), Format(ServerValue(id))), SENDER_INFO, id);
            if (range.max > range.min)
                AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, Acore::StringFormat(Text(player, 18, "Type a value ({} to {})"), Format(range.min), Format(range.max)), SENDER_CODE, id, "", 0, true);
            // quick picks: 0 Blizzard default, 1 recommended, 2 lowest, 3 highest
            std::array<std::pair<char const*, float>, 4> const picks = { {
                { Text(player, 44, "Blizzard default"), range.blizzard },
                { Text(player, 45, "Recommended"), range.recommended },
                { Text(player, 46, "Lowest"), range.min },
                { Text(player, 47, "Highest"), range.max } } };
            std::array<float, 4> shown = { -1.0f, -1.0f, -1.0f, -1.0f };
            for (uint8 pick = 0; pick < picks.size(); ++pick)
            {
                float value = std::clamp(picks[pick].second, range.min, range.max);
                if (std::find(shown.begin(), shown.end(), value) != shown.end())
                    continue;   // same number as an earlier pick
                shown[pick] = value;
                AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, Acore::StringFormat("{}: {}", picks[pick].first, Format(value)), SENDER_PICK, id * 16 + pick);
            }
        }
        AddGossipItemFor(player, GOSSIP_ICON_INTERACT_2, account
            ? Text(player, 19, "Remove the account's choice (use the server's)")
            : Text(player, 48, "Remove this character's choice (use the account's)"), SENDER_CLEAR, id);
        AddGossipItemFor(player, GOSSIP_ICON_TALK, Text(player, 16, "Back"), SENDER_PAGE, SETTINGS[id].page);
        Send(player);
    }

    void Changed(Player* player, uint8 id)
    {
        switch (id)
        {
            case S_PUSHBACK: ApplyPushback(player); break;
            case S_REGEN_HEALTH: case S_REGEN_MANA: case S_RAGE_LOSS: case S_RUNIC_LOSS: ApplyRegen(player); break;
            case S_REST_MAX: SetRest(player, player->GetRestBonus()); break;
            default: break;
        }
    }

    void Set(Player* player, uint8 id, std::optional<float> value)
    {
        if (value)
            value = Clamp(id, *value);
        Save(player, id, value, EditAccount(player));
        Changed(player, id);
    }

    void GoToNearestGraveyard(Player* player)
    {
        if (!player->isDead() || !OpenWorld(player))
        {
            ChatHandler(player->GetSession()).SendSysMessage(Text(player, 49, "You can only do this while dead, outside instances and battlegrounds."));
            return;
        }
        WorldLocation from = player->HasPlayerFlag(PLAYER_FLAGS_GHOST) ? player->GetWorldLocation() : player->GetCorpseLocation();
        if (from.GetMapId() != player->GetMapId())
            from = player->GetWorldLocation();
        GraveyardStruct const* graveyard = NearestGraveyard(player, from);
        if (!graveyard)
        {
            ChatHandler(player->GetSession()).SendSysMessage(Text(player, 50, "There is no friendly or neutral graveyard on this map."));
            return;
        }
        if (!player->HasPlayerFlag(PLAYER_FLAGS_GHOST))
            player->BuildPlayerRepop();
        player->TeleportTo(graveyard->Map, graveyard->x, graveyard->y, graveyard->z, player->GetOrientation());
    }

    void JoinWorldChannel(Player* player)
    {
        if (!cfg.worldChannel || cfg.worldChannelName.empty() || !Toggle(player, S_WORLD_CHANNEL))
            return;
        if (ChannelMgr* mgr = ChannelMgr::forTeam(player->GetTeamId()))
            if (Channel* channel = mgr->GetJoinChannel(cfg.worldChannelName, 0))
                channel->JoinChannel(player, "");
    }

    uint8 BattlegroundSetting(uint32 mapId)
    {
        switch (mapId)
        {
            case 30:  return S_BG_AV;
            case 489: return S_BG_WSG;
            case 529: return S_BG_AB;
            case 566: return S_BG_EOTS;
            case 607: return S_BG_SOTA;
            case 628: return S_BG_IC;
            default:  return S_COUNT;
        }
    }
}

class TemucraftPlayerSettings_Player : public PlayerScript
{
public:
    TemucraftPlayerSettings_Player() : PlayerScript("TemucraftPlayerSettings_Player", {
        PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_LOGOUT, PLAYERHOOK_ON_UPDATE, PLAYERHOOK_ON_GIVE_EXP,
        PLAYERHOOK_ON_REPUTATION_CHANGE, PLAYERHOOK_ON_GOSSIP_SELECT, PLAYERHOOK_ON_GOSSIP_SELECT_CODE,
        PLAYERHOOK_ON_BEFORE_CHOOSE_GRAVEYARD, PLAYERHOOK_SHOULD_BE_REWARDED_WITH_MONEY_INSTEAD_OF_EXP,
        PLAYERHOOK_ON_LEVEL_CHANGED }) { }

    void OnPlayerLogin(Player* player) override
    {
        if (!cfg.enable)
            return;
        Load(player);
        ApplyPushback(player);
        ApplyRegen(player);
        ApplyOfflineRest(player);
        JoinWorldChannel(player);
    }

    void OnPlayerLogout(Player* player) override
    {
        std::lock_guard lock(sLock);
        sData.erase(player->GetGUID().GetCounter());
    }

    void OnPlayerLevelChanged(Player* player, uint8 /*oldLevel*/) override
    {
        if (!cfg.enable)
            return;
        std::lock_guard lock(sLock);
        if (PlayerData* data = Data(player))
            data->lastRest = player->GetRestBonus();
    }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        if (!cfg.enable)
            return;
        uint32 petTake = 0;
        {
            std::lock_guard lock(sLock);
            PlayerData* data = Data(player);
            if (!data)
                return;
            data->restTimer += diff;
            if (data->restTimer < 1000)
                return;
            data->restTimer = 0;
            std::swap(petTake, data->petXpToRemove);
        }
        UpdateRest(player);
        if (petTake)
            if (Pet* pet = player->GetPet())
            {
                uint32 xp = pet->GetUInt32Value(UNIT_FIELD_PETEXPERIENCE);
                pet->SetUInt32Value(UNIT_FIELD_PETEXPERIENCE, xp > petTake ? xp - petTake : 0);
            }
    }

    void OnPlayerGiveXP(Player* player, uint32& amount, Unit* /*victim*/, uint8 xpSource) override
    {
        if (!cfg.enable || !amount)
            return;
        if (!Toggle(player, S_XP_ON))
        {
            amount = 0;
            return;
        }
        uint8 id = S_COUNT;
        switch (xpSource)
        {
            case XPSOURCE_KILL:
                id = player->InBattleground() ? BattlegroundSetting(player->GetMapId()) : S_XP_KILL;
                break;
            case XPSOURCE_QUEST:    id = S_XP_QUEST; break;
            case XPSOURCE_QUEST_DF: id = S_XP_QUEST_DF; break;
            case XPSOURCE_EXPLORE:  id = S_XP_EXPLORE; break;
            default: break;
        }
        if (id != S_COUNT)
            amount = uint32(std::lround(amount * Ratio(player, id)));

        // The core gives a hunter pet a share of the player's kill experience times Rate.XP.Pet right after this hook.
        if (xpSource == XPSOURCE_KILL && amount)
        {
            Pet* pet = player->GetPet();
            float server = ServerValue(S_XP_PET);
            if (pet && pet->getPetType() == HUNTER_PET && server > 0.0f)
            {
                float share = player->GetGroup() ? amount / 2.0f : float(amount);
                float wanted = share * Value(player, S_XP_PET);
                float given = share * server;
                if (wanted > given)
                    pet->GivePetXP(uint32((wanted - given) / server));
                else if (wanted < given)
                {
                    std::lock_guard lock(sLock);
                    if (PlayerData* data = Data(player))
                        data->petXpToRemove += uint32(given - wanted);
                }
            }
        }
    }

    bool OnPlayerShouldBeRewardedWithMoneyInsteadOfExp(Player* player) override
    {
        return cfg.enable && !Toggle(player, S_XP_ON);
    }

    bool OnPlayerReputationChange(Player* player, uint32 factionID, int32& standing, bool incremental) override
    {
        if (!cfg.enable || !incremental)
            return true;
        int32 old = player->GetReputationMgr().GetReputation(factionID);
        int32 gain = standing - old;
        if (gain <= 0)
            return true;   // losses stay as they are
        float ratio = Ratio(player, S_REP_GAIN);
        if (ratio != 1.0f)
            standing = std::min<int32>(old + int32(std::lround(gain * ratio)), ReputationMgr::Reputation_Cap);
        return true;
    }

    void OnPlayerBeforeChooseGraveyard(Player* player, TeamId /*teamId*/, bool nearCorpse, uint32& graveyardOverride) override
    {
        if (!cfg.enable || graveyardOverride || !Toggle(player, S_GRAVEYARD) || !OpenWorld(player))
            return;
        WorldLocation from = nearCorpse ? player->GetCorpseLocation() : player->GetWorldLocation();
        if (GraveyardStruct const* graveyard = NearestGraveyard(player, from))
            graveyardOverride = graveyard->ID;
    }

    void OnPlayerGossipSelect(Player* player, uint32 menuId, uint32 sender, uint32 action) override
    {
        if (menuId != SETTINGS_MENU || !cfg.enable)
            return;
        switch (sender)
        {
            case SENDER_PAGE:
                SendPage(player, Page(std::min<uint32>(action, PAGE_REGEN)));
                return;
            case SENDER_SETTING:
                if (action < S_COUNT)
                    SendSetting(player, uint8(action));
                return;
            case SENDER_TOGGLE:
                if (action < S_COUNT)
                {
                    Set(player, uint8(action), Toggle(player, uint8(action)) ? 0.0f : 1.0f);
                    SendPage(player, SETTINGS[action].page);
                }
                return;
            case SENDER_PICK:
            {
                uint8 id = uint8(action / 16), pick = uint8(action % 16);
                if (id >= S_COUNT)
                    return;
                if (SETTINGS[id].kind == KIND_CHOICE)
                    Set(player, id, float(pick));
                else
                {
                    Range const& range = cfg.ranges[id];
                    std::array<float, 4> const values = { range.blizzard, range.recommended, range.min, range.max };
                    Set(player, id, values[std::min<uint8>(pick, 3)]);
                }
                SendSetting(player, id);
                return;
            }
            case SENDER_CLEAR:
                if (action < S_COUNT)
                {
                    Set(player, uint8(action), std::nullopt);
                    SendSetting(player, uint8(action));
                }
                return;
            case SENDER_SCOPE:
            {
                {
                    std::lock_guard lock(sLock);
                    if (PlayerData* data = Data(player))
                        data->editAccount = !data->editAccount;
                }
                SendPage(player, Page(std::min<uint32>(action, PAGE_REGEN)));
                return;
            }
            case SENDER_GRAVEYARD_NOW:
                CloseGossipMenuFor(player);
                GoToNearestGraveyard(player);
                return;
            case SENDER_INFO:
                if (action < S_COUNT)
                    SendSetting(player, uint8(action));
                return;
            default:
                CloseGossipMenuFor(player);
                return;
        }
    }

    void OnPlayerGossipSelectCode(Player* player, uint32 menuId, uint32 sender, uint32 action, char const* code) override
    {
        if (menuId != SETTINGS_MENU || !cfg.enable || sender != SENDER_CODE || action >= S_COUNT || !code)
            return;
        std::string text = code;
        std::replace(text.begin(), text.end(), ',', '.');
        Optional<float> value = Acore::StringTo<float>(text);
        if (!value || !std::isfinite(*value))
            ChatHandler(player->GetSession()).SendSysMessage(Text(player, 51, "Please type a number, for example 1.5"));
        else
            Set(player, uint8(action), *value);
        SendSetting(player, uint8(action));
    }
};

class TemucraftPlayerSettings_Command : public CommandScript
{
public:
    TemucraftPlayerSettings_Command() : CommandScript("TemucraftPlayerSettings_Command") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable commandTable =
        {
            { "temucraft settings", HandleSettings, 103850, Console::No },
            { "temucraft graveyard", HandleGraveyard, 103851, Console::No },
        };
        return commandTable;
    }

    static bool HandleSettings(ChatHandler* handler)
    {
        if (!cfg.enable)
        {
            handler->SendSysMessage(TemucraftModuleText(MODULE, handler, 1, "Player settings are off on this server."));
            return true;
        }
        SendPage(handler->GetSession()->GetPlayer(), PAGE_MAIN);
        return true;
    }

    static bool HandleGraveyard(ChatHandler* handler)
    {
        if (cfg.enable)
            GoToNearestGraveyard(handler->GetSession()->GetPlayer());
        return true;
    }
};

class TemucraftPlayerSettings_World : public WorldScript
{
public:
    TemucraftPlayerSettings_World() : WorldScript("TemucraftPlayerSettings_World", { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP }) { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        cfg.enable = sConfigMgr->GetOption<bool>("Temucraft.PlayerSettings.Enable", true);
        cfg.worldChannel = sConfigMgr->GetOption<bool>("Temucraft.PlayerSettings.WorldChannel.Enable", true);
        cfg.worldChannelName = sConfigMgr->GetOption<std::string>("Temucraft.PlayerSettings.WorldChannel.Name", "World");
        cfg.regenHealthSpell = sConfigMgr->GetOption<uint32>("Temucraft.PlayerSettings.Regen.Spell1", 1269900);
        cfg.regenPowerSpell = sConfigMgr->GetOption<uint32>("Temucraft.PlayerSettings.Regen.Spell2", 1269901);

        float xpMax = sConfigMgr->GetOption<float>("Temucraft.PlayerSettings.XP.Max", 3.0f);
        float bgMax = sConfigMgr->GetOption<float>("Temucraft.PlayerSettings.XP.Battleground.Max", 3.0f);
        float repMax = sConfigMgr->GetOption<float>("Temucraft.PlayerSettings.Reputation.Max", 2.0f);
        for (uint8 id = S_XP_KILL; id <= S_XP_PET; ++id)
            cfg.ranges[id] = { 0.0f, xpMax, 1.0f, 1.0f };
        for (uint8 id = S_BG_AV; id <= S_BG_IC; ++id)
            cfg.ranges[id] = { 0.0f, bgMax, 1.0f, 1.0f };
        cfg.ranges[S_REP_GAIN] = { 0.0f, repMax, 1.0f, repMax };

        // Between Blizzard's default and our recommendation, in either order (less rage loss is "more").
        auto between = [](uint8 id, char const* name, float blizzard, float recommended)
        {
            float a = sConfigMgr->GetOption<float>(Acore::StringFormat("Temucraft.PlayerSettings.{}.Default", name), blizzard);
            float b = sConfigMgr->GetOption<float>(Acore::StringFormat("Temucraft.PlayerSettings.{}.Recommended", name), recommended);
            cfg.ranges[id] = { std::min(a, b), std::max(a, b), a, b };
        };
        between(S_REST_INGAME, "Rest.InGame", 1.0f, 2.0f);
        between(S_REST_TAVERN, "Rest.Offline.InTavernOrCity", 1.0f, 2.0f);
        between(S_REST_WILD, "Rest.Offline.InWilderness", 1.0f, 2.0f);
        between(S_REST_MAX, "Rest.MaxBonus", 1.5f, 1.5f);
        between(S_REGEN_HEALTH, "Health", 1.0f, 2.0f);
        between(S_REGEN_MANA, "Mana", 1.0f, 1.5f);
        between(S_RAGE_LOSS, "Rage.Loss", 1.0f, 0.5f);
        between(S_RUNIC_LOSS, "RunicPower.Loss", 1.0f, 0.5f);
    }

    void OnStartup() override
    {
        LoadGraveyardTeams();
    }
};

void AddTemucraftPlayerSettingsScripts()
{
    new TemucraftPlayerSettings_Player();
    new TemucraftPlayerSettings_Command();
    new TemucraftPlayerSettings_World();
}
