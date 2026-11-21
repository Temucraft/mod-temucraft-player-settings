-- Optional (auth database): let every player open the settings window and use the graveyard command.
-- 195 and 1010 are the player roles.
DELETE FROM `rbac_linked_permissions` WHERE `id` IN (195, 1010) AND `linkedId` IN (103850, 103851);
INSERT INTO `rbac_linked_permissions` (`id`, `linkedId`) VALUES
(195, 103850), (1010, 103850),
(195, 103851), (1010, 103851);
