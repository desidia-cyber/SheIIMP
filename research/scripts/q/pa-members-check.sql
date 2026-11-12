SELECT path, type, off FROM obj WHERE owner=(SELECT addr FROM obj WHERE path='/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C') ORDER BY line LIMIT 6
