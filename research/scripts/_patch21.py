p='mine/README.md'
s=open(p,encoding='utf-8').read()
old = "| Residual validator warning (1) | cross-class name collisions in the checker's own heuristic (`AbilitySystemComponent` exists on both `SpartaCharacter` (0xAA8) and `SpartaHealthComponent` (0x118)); both offsets were confirmed by hand against the dump |"
new = "| Residual validator warnings (2) | cross-class name collisions inside the checker's own heuristic: `AbilitySystemComponent` exists on both `SpartaCharacter` (0xAA8) and `SpartaHealthComponent` (0x118); `NarrativeTags` exists on both `BPC_QuestLogComponent_C` (0x258) and `BPSO_QuestLogComponent_C` (0xC0). All four offsets were confirmed by hand against the dump |"
assert old in s
s=s.replace(old,new)
s=s.replace('~1 510 lines','~1 560 lines')
open(p,'w',encoding='utf-8',newline='\n').write(s)
print('readme ok')
