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
- World channel: joined at login. Set `AllowTwoSide.Interaction.Channel = 1` so both factions share it.

## Install

1. Put the module in `modules/`, re-run CMake and build.
2. Apply `data/sql/db-characters`, `data/sql/db-world` and `data/sql/db-auth`.
3. Commands are admin only by default. To give them to every player, apply
   `data/sql/optional/mod_temucraft_player_settings_rbac_players.sql` to the auth database.
4. Copy `conf/mod-temucraft-player-settings.conf.dist` to `mod-temucraft-player-settings.conf` and restart.

## Ids

- Player gossip menu 1269900. RBAC 103850-103899 (103850 settings, 103851 graveyard). Spells 1269900-1269901.
