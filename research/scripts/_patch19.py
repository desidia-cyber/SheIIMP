p='mine_map.py'
s=open(p,encoding='utf-8').read()
old = """    ('QUEST', '/Script/Sparta.QuestObjective', 'prop', []),"""
new = """    ('QUEST', '/Script/Sparta.QuestObjective', 'prop', []),
    ('QUEST', E + 'Dialogue/QuestSystem/BPC_QuestLogComponent.BPC_QuestLogComponent_C', 'prop',
     ['CurrentActiveQuests', 'CompletedQuests', 'CurrentTrackedQuest', 'ActiveQuests',
      'NPCRelationships', 'PlayerChoice', 'NarrativeEvents', 'NarrativeTags', 'BlisterBoiDonations',
      'ImmortalBrigSpoils', 'OnQuestStarted', 'OnQuestCompleted', 'OnNarrativeTagsLoaded']),
    ('QUEST', E + 'Dialogue/QuestSystem/BPC_NarrativeEventsTracker.BPC_NarrativeEventsTracker_C',
     'prop', ['MySaveContext', 'CachedEventsTimeTracker', 'OnTrackerRefresh']),
    ('QUEST', E + 'Dialogue/QuestSystem/BPC_NarrativeEventsTracker.BPC_NarrativeEventsTracker_C',
     'func', ['FindEventTrackedTime', 'AddEventTimeTracker', 'RemoveEventTimeTracker',
              'GetMySaveData', 'InitEventsTimeTracker']),
    ('QUEST', E + 'Dialogue/Blueprints/DiscipleSystem/BPC_HubEventComponent.BPC_HubEventComponent_C',
     'prop', ['EventPointsPerEnemyKill', 'GiftEventPointBank', 'GiftEventDefaultThreshold',
              'CurrentGiftEventLevel', 'GiftEventLeaderTable', 'GiftsPickedUp', 'WasInitialized',
              'OvumsToOpenFinalLevel', 'OvumStairsEventBroadcasted', 'StartShellMemory']),
    ('QUEST', E + 'Dialogue/Blueprints/DiscipleSystem/BPC_HubEventComponent.BPC_HubEventComponent_C',
     'func', ['UpdateOvumsCollected', 'LoadOvumsCollectedFromSave', 'SaveHubData', 'OnSkyDive']),
    ('QUEST', E + 'Core/Achievements/BPC_Achievements.BPC_Achievements_C', 'prop',
     ['SlayerSealLockout', 'AchievementUpdatesQueue', 'IsUnlockingAchievements',
      'CurrentAchievementName', 'CurrentAchievementProgress']),"""
assert old in s
s=s.replace(old,new,1)

oldz = """    ('FLOW', '/Script/Sparta.SpartaZoneVolumeManager', 'prop',
     ['ZoneVolumeStack', 'HighestPriorityZoneVolumeUpdated']),"""
newz = """    ('FLOW', '/Script/Sparta.SpartaZoneVolumeManager', 'prop',
     ['ZoneVolumeStack', 'HighestPriorityZoneVolumeUpdated']),
    ('FLOW', '/Script/Sparta.SpartaZoneData', 'prop', ['ZoneId', 'AmbienceData', 'MusicData']),
    ('FLOW', '/Script/Sparta.SpartaZoneVolume', 'prop',
     ['ZoneData', 'Priority', 'CollisionProfile']),"""
assert oldz in s
s=s.replace(oldz,newz,1)
open(p,'w',encoding='utf-8',newline='\n').write(s)
print('spec extended')
