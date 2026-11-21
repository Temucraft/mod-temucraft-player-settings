-- mod-temucraft-player-settings: what each account or character picked in the settings window.
-- scope 0 = account (owner = account id), 1 = character (owner = character guid). No row = use the next level up.
CREATE TABLE IF NOT EXISTS `mod_temucraft_player_settings` (
  `scope` TINYINT UNSIGNED NOT NULL,
  `owner` INT UNSIGNED NOT NULL,
  `setting` VARCHAR(32) NOT NULL,
  `value` FLOAT NOT NULL,
  PRIMARY KEY (`scope`, `owner`, `setting`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='Temucraft player settings';
