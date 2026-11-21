-- mod-temucraft-player-settings: two hidden passive server-side spells that carry a player's regeneration choices.
-- The module sets their amounts (percent change from the server rate) when the player logs in or changes a value.
-- Passive auras are never sent to the client, so the client needs no spell data for them.
--   1269900: health regeneration % (aura 88), mana regeneration % (aura 110, mana), rage loss % (aura 110, rage)
--   1269901: runic power loss % (aura 110, runic power)
-- Attributes 0xC0 = passive + hidden; AttributesEx3 0x100000 = stays through death. Duration 21 = forever.
SET @SPELL := 1269900;
DELETE FROM `spell_dbc` WHERE `ID` IN (@SPELL, @SPELL + 1);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `AttributesEx3`, `CastingTimeIndex`, `DurationIndex`, `RangeIndex`, `EquippedItemClass`, `SchoolMask`,
  `Effect_1`, `Effect_2`, `Effect_3`, `ImplicitTargetA_1`, `ImplicitTargetA_2`, `ImplicitTargetA_3`,
  `EffectAura_1`, `EffectAura_2`, `EffectAura_3`, `EffectMiscValue_1`, `EffectMiscValue_2`, `EffectMiscValue_3`, `Name_Lang_enUS`) VALUES
(@SPELL,     192, 1048576, 1, 21, 1, -1, 1, 6, 6, 6, 1, 1, 1, 88, 110, 110, 0, 0, 1, 'Temucraft Regeneration'),
(@SPELL + 1, 192, 1048576, 1, 21, 1, -1, 1, 6, 0, 0, 1, 0, 0, 110, 0, 0, 6, 0, 0, 'Temucraft Runic Power');
