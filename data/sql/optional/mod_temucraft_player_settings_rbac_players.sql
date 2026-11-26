-- Optional (auth database): let players open the settings window and use the graveyard command.
-- The auth database is shared by Live and the PTR, so link the permissions to the role that matches the realm:
--   PTR only (role 1000 "PTR Player", what normal accounts get on realm 3):
INSERT IGNORE INTO `rbac_linked_permissions` (`id`, `linkedId`) VALUES (1000, 103850), (1000, 103851);
--   Every realm, Live included (player roles 195 and 1010): uncomment only when Live should get them too.
-- INSERT IGNORE INTO `rbac_linked_permissions` (`id`, `linkedId`) VALUES (195, 103850), (1010, 103850), (195, 103851), (1010, 103851);
