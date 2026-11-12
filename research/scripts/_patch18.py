p='mine/SYNC-DATA-MINE.md'
s=open(p,encoding='utf-8').read()
anchor = """`[FACT]` Narrative write path is the tag API: `S_AddNarrativeTag(Tag)` / `S_RemoveNarrativeTag(Tag)`
(\u00a710) and the save-object container at `BPSO_QuestLogComponent_C.NarrativeTags +0xC0`, whose layout is
`TArray<FGameplayTag> GameplayTags @+0x0 (0x10)`, `TArray<FGameplayTag> ParentTags @+0x10 (0x10)`.
Both are exactly what a `[PROPOSAL]` diff-based tag sync needs: read the array of 8-byte FNames,
diff, add/remove by literal."""
assert anchor in s
add = anchor + """

### 13.5 The live quest state is far richer than its save object

`[FACT]` `BPC_QuestLogComponent_C` (`/Game/Sparta/Dialogue/QuestSystem/…`, super **`SpartaSaveComponent`**
\u2014 so it both holds live state and owns a save object) carries everything a quest sync needs:

| Member | Type | Offset |
|---|---|---|
| `CurrentActiveQuests` | array | `0x128` |
| `CompletedQuests` | array | `0x138` |
| `CurrentTrackedQuest` | FName | `0x148` |
| `ActiveQuests` | array | `0x150` |
| `NPCRelationships` | map | `0x170` |
| `PlayerChoice` | `FGameplayTag` | `0x1D0` |
| `NarrativeEvents` | map | `0x1F8` |
| `NarrativeTags` | `FGameplayTagContainer` | `0x258` |
| `BlisterBoiDonations` | int32 | `0x288` |
| `ImmortalBrigSpoils` | map | `0x290` |
| `OnQuestStarted` / `OnQuestCompleted` / `OnNarrativeTagsLoaded` | delegates | `0x160` / `0x1C0` / `0x278` |

`[DERIVED]` Compare with its save object (only `NarrativeTags @0xC0`): the *persisted* form is a
subset, so **reading the save object is not enough** \u2014 `CurrentActiveQuests`, `CompletedQuests`,
`CurrentTrackedQuest`, `NPCRelationships` and `PlayerChoice` exist only on the component.

`[FACT]` `BPC_NarrativeEventsTracker_C` (super `SpartaSaveComponent`): `MySaveContext @0x128`,
`CachedEventsTimeTracker: map @0x158`, `OnTrackerRefresh @0x1B0`;
functions `FindEventTrackedTime(EventTag: FGameplayTag) -> double`,
`AddEventTimeTracker(EventTag)`, `RemoveEventTimeTracker(EventTag)`,
`GetMySaveData() -> BPSO_NarrativeEventsTracker_C`, `InitEventsTimeTracker(EventData, EventKey)`.

`[FACT]` `BPC_HubEventComponent_C` (super `HubEventComponent`): `EventPointsPerEnemyKill @0xC8`,
`GiftEventPointBank @0xD0`, `GiftEventDefaultThreshold @0xD8`, `CurrentGiftEventLevel @0xE0`,
`GiftEventLeaderTable: DataTable @0xE8`, `GiftsPickedUp: map @0xF0`, `WasInitialized @0x150`,
`OvumsToOpenFinalLevel @0x154`, `OvumStairsEventBroadcasted @0x158`, `StartShellMemory @0x15C`;
functions `UpdateOvumsCollected(OvumsToAdd)`, `LoadOvumsCollectedFromSave()`,
`SaveHubData(L_Players)`, `OnSkyDive(EventData, L_PlayerSaveObject)`.

`[FACT]` `BPC_Achievements_C`: `SlayerSealLockout @0xC0`, `AchievementUpdatesQueue: map @0xC8`,
`IsUnlockingAchievements @0x118`, `CurrentAchievementName: FString @0x120`,
`CurrentAchievementProgress: F_AchievementProgress @0x130`; plus
`UpdateSlayerSealLockoutFromSave()` and the platform `WriteAchievement` callbacks
(`OnSuccess_…` / `OnFailure_…` with `WrittenAchievementName, WrittenProgress, WrittenUserTag`).
`[DERIVED]` Achievements are written per-platform by each peer independently; the co-op-relevant state
is `BP_SpartaPlayerSaveObject_C.EarnedAchievements @0x650` plus `SlayerSealLockout`, not the queue."""
s = s.replace(anchor, add)

# zone identity facts
old17 = """Functions: `GetActiveArea() -> USpartaAreaId`, `IsAreaActive(Area) -> bool`,"""
assert old17 in s
s = s.replace("""`[FACT]` `USpartaAreaSubsystem` (WorldSubsystem): `ActiveArea: USpartaAreaId* @0x30`,
`CompletedAreas: set @0x38`, delegates `OnAreaActivated @0x88`, `OnAreaDeactivated @0x98`,
`OnAreaComplete @0xA8`.""",
"""`[FACT]` `USpartaAreaSubsystem` (WorldSubsystem): `ActiveArea: USpartaAreaId* @0x30`,
`CompletedAreas: set @0x38`, delegates `OnAreaActivated @0x88`, `OnAreaDeactivated @0x98`,
`OnAreaComplete @0xA8`.
`[FACT]` **Zone identity is printable**: `USpartaZoneData` (a `DataAsset`) has
`ZoneId: FName @0x30`, `AmbienceData @0x38`, `MusicData @0x40`; and `USpartaZoneVolume`
(a `TriggerVolume`) has `ZoneData: USpartaZoneData* @0x2E0`, `Priority: int32 @0x2E8`,
`CollisionProfile @0x2EC`. `[DERIVED]` so the wire can carry a real, human-readable zone id instead of
inventing one, and `USpartaZoneVolumeManager.ZoneVolumeStack @0x40` gives the local stack to compare.""")
open(p,'w',encoding='utf-8',newline='\n').write(s)
print('doc updated')
