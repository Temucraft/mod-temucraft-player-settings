# mod-temucraft-player-settings

Every player picks his own rates and options in one window. Open it with `.temucraft settings`.

## What players can change

| Page | Setting | Range |
|---|---|---|
| Experience rates | Gain experience | on / off (off: quests give money instead, as at the top level) |
| | Kill, quest, dungeon finder quest, exploration and pet experience | 0 to 3 |
| Battleground kill experience | Alterac Valley, Warsong Gulch, Arathi Basin, Eye of the Storm, Strand of the Ancients, Isle of Conquest | 0 to 3 |
| Rested experience | In game, offline in an inn or city, offline outside, most rested experience stored | Blizzard's default to our recommended value |
| Regeneration | Health and mana regeneration, rage and runic power loss out of combat | Blizzard's default to our recommended value |
| Main | Reputation gain | 0 to 2 |
| Main | Spell pushback | Server default, Vanilla/TBC, Wrath, Retail (no pushback) |
| Main | Join the World channel at login | on / off |
| Main | Combat skills always at the maximum for my level | off = level them by use (the normal game) / on = always at max (also `.temucraft settings combatskills max\|use`) |
| Main | Arachnophobia Mode (spiders look like crabs) | off (default) / on: spiders, nerubians, crypt fiends and spider mounts are shown as crabs, makrura, wasps and a crab mount, only to you (also `.temucraft settings arachnophobia on\|off`) |
| Main | Ask before teleports and portals | on (default) / off: "Teleport to: <where>" Accept/Cancel before gossip teleports, city, dungeon and mage portals (also `.temucraft settings prompts teleports on\|off`) |
| Main | Ask before opening chests | off (default) / on: "Open: <chest>" Accept/Cancel (`.temucraft settings prompts chests on\|off`) |
| Main | Remind me that prompts can be turned off | on (default) / off: one chat line per login at the first prompt (`.temucraft settings prompts reminder on\|off`) |
| Main | Go to the nearest graveyard | button (also `.temucraft graveyard`); alive or dead, anywhere by default (conf) |

- **Account or character:** the top line of the window says where changes are saved. A character uses its own value,
  then the account's, then the server's. Every line shows the value in use and where it comes from.
- **Relative to the server:** a player who picks 2 while the server runs 1 gets twice the normal amount; with the
  server on 2, picking 1 halves it. Ranges and the quick picks are in the conf.
- **Typing a value:** "Type a value" opens a box; any number in the range works (1.25, 2,5 ...).

## How it works

- Experience: `OnPlayerGiveXP` (kills, battleground kills by map, quests, exploration). Hunter pets: the extra pet
  experience is given right away; a lower pick is taken back on the next update.
- Rested experience: the part the core adds in game or while offline is scaled to the player's rate; the stored
  amount is capped at the player's maximum.
- Regeneration: two hidden passive server-side spells (1269900, 1269901) whose amounts are the percent change.
- Reputation: gains (not losses) are scaled in `OnPlayerReputationChange`.
- Spell pushback: the choice is stored on the player and read by mod-temucraft-classes (`TemucraftPlayerSettings.h`).
- Graveyard button: the nearest graveyard on the same map that `graveyard_zone` links to the player's faction or to
  both; where there is none (most instances), the graveyard the core would use. `Temucraft.PlayerSettings.Graveyard.*`
  decides alive / dead / anywhere and a cooldown. Alive players can't use it in combat, flying or on a transport.
- Combat skills: with the setting on, every weapon skill the character has (Unarmed included) and Defense are raised
  to their maximum for his level at login, on every level, when he turns it on and when he learns a new weapon skill.
  Professions, secondary skills, languages and riding are never touched. Turning it off keeps the values he has
  (they then go up by use again as the level rises). `Temucraft.PlayerSettings.CombatSkills.Max.Default` is what a
  player who never picked gets; `Temucraft.PlayerSettings.CombatSkills.PlayerChoice = 0` hides the line and gives
  everybody the server's value. The core's `AlwaysMaxWeaponSkill` (worldserver.conf) still maxes them for everybody.
- Prompts: the three choices are stored on the player at login and on every change (`TemucraftPlayerSettings.h`);
  mod-temucraft shows the prompts (its `TemucraftPrompts.cpp` and README) and the modules with teleport menus use
  the same header. Turned off, gossip lines come without the popup and portals work at once. Defaults and whether
  players may change them: `Temucraft.PlayerSettings.TeleportConfirm.*`, `.ChestConfirm.*`, `.PromptReminder.*`
  (`PlayerChoice = 0` hides the line and gives everybody the default). The graveyard button asks too
  ("Teleport to: the nearest graveyard"); the typed `.temucraft graveyard` doesn't.
- Arachnophobia Mode (like retail's Accessibility option): `TemucraftArachnophobia.cpp`. When the server builds a
  unit's values for a player who has it on (`UnitScript::OnPatchValuesUpdate`, no core change), the display id, native
  display id and mount display id are replaced from the world table `mod_temucraft_arachnophobia`, and the object
  scale sent to him is multiplied by the row's `scale`, so the crab is as big as the spider was. Only his packets
  change: everybody else sees the spider, and the creature's real size, reach, hit box and spells stay the same.
  Groups (made from the model paths in the DBCs by `tools/make_arachnophobia_sql.py`): spiders (bone, crystal, fire,
  lava, shale, spider demons, spiderlings) -> crabs; giant spiders, tarantulas, Beth'tilac-type bosses -> big (deep sea)
  crabs; mechanical spiders (spider tanks, mechanical spider pets) -> hermit crab; nerubians, crypt fiends, crypt lords,
  dryders -> makrura (lobstrok); flying nerubians -> wasps (no skitterfly in 3.3.5); spider mounts -> crab mount.
  It covers everything that is a unit: creatures, bosses, companion and hunter pets, players in a spider form, mounts.
  Not: webs, spell effects, icons, loading screens, the model in the companion/mount journal and portraits drawn from
  the creature cache. Turning it on or off sends the swapped units in view again, so they change at once.
  `Temucraft.PlayerSettings.Arachnophobia.Default` / `.PlayerChoice`. The client's Options window (Accessibility >
  General) sends `.temucraft settings arachnophobia on|off` and reads the plain reply "Arachnophobia Mode: on|off".
  New spider displays: run `python3 tools/make_arachnophobia_sql.py <CreatureDisplayInfo.dbc> <CreatureModelData.dbc>
  data/sql/db-world/base/mod_temucraft_arachnophobia.sql` again.
- World channel: joined at login. Set `AllowTwoSide.Interaction.Channel = 1` so both factions share it.

## Install

1. Put the module in `modules/`, re-run CMake and build.
2. Apply `data/sql/db-characters`, `data/sql/db-world` and `data/sql/db-auth`.
3. Commands are admin only by default. To give them to every player, apply
   `data/sql/optional/mod_temucraft_player_settings_rbac_players.sql` to the auth database.
4. Copy `conf/mod-temucraft-player-settings.conf.dist` to `mod-temucraft-player-settings.conf` and restart.

## Ids

- Player gossip menu 1269900. RBAC 103850-103899 (103850 settings, 103851 graveyard). Spells 1269900-1269901. World table `mod_temucraft_arachnophobia`. Texts (module_string) 80-84 are Arachnophobia Mode.
