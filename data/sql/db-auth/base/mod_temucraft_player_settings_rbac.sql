-- mod-temucraft-player-settings: its commands (all under .temucraft) as RBAC permissions 103850-103899. Every command is
-- admin only by default: linked to the administrator roles 192 and 1014. To give them to players, run
-- data/sql/optional/mod_temucraft_player_settings_rbac_players.sql (or grant per account with .rbac account grant).
DELETE FROM `rbac_linked_permissions` WHERE `linkedId` BETWEEN 103850 AND 103899;
DELETE FROM `rbac_permissions` WHERE `id` BETWEEN 103850 AND 103899;
INSERT INTO `rbac_permissions` (`id`, `name`) VALUES
(103850, 'Command: temucraft settings'),
(103851, 'Command: temucraft graveyard');
INSERT INTO `rbac_linked_permissions` (`id`, `linkedId`) VALUES
(192, 103850), (1014, 103850),
(192, 103851), (1014, 103851);
