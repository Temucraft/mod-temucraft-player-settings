-- mod-temucraft-player-settings: its commands (all under .temucraft) as RBAC permissions 103850-103899. Every command is
-- admin only by default: linked to the administrator roles 192 and 1014. To give them to players, run
-- data/sql/optional/mod_temucraft_player_settings_rbac_players.sql (or grant per account with .rbac account grant).
-- Re-runnable without touching other roles' links (e.g. role 1000 from the optional players file): the permissions are
-- updated in place (deleting them would cascade to every link), only unused ids in the range are removed, and the
-- admin links are added if missing.
DELETE FROM `rbac_permissions` WHERE `id` BETWEEN 103852 AND 103899;
INSERT INTO `rbac_permissions` (`id`, `name`) VALUES
(103850, 'Command: temucraft settings'),
(103851, 'Command: temucraft graveyard')
ON DUPLICATE KEY UPDATE `name` = VALUES(`name`);
INSERT IGNORE INTO `rbac_linked_permissions` (`id`, `linkedId`) VALUES
(192, 103850), (1014, 103850),
(192, 103851), (1014, 103851);
