SELECT '['||addr||'] '||type||' '||path||rest AS line FROM obj WHERE type='AnimSequence' AND path IN (
 '/Game/Sparta/Characters/Shells/_Shared/Animation/Locomotion/Idles/A_Shared_Idle_H.A_Shared_Idle_H',
 '/Game/Sparta/Characters/Shells/_Shared/Animation/Locomotion/Walk/H2/A_Shared_Locomotion_Walk_Fwd_H2.A_Shared_Locomotion_Walk_Fwd_H2',
 '/Game/Sparta/Characters/Shells/_Shared/Animation/Locomotion/A_Shared_Locomotion_Sprint_800.A_Shared_Locomotion_Sprint_800')
ORDER BY path
