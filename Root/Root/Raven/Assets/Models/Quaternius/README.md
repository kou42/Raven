# Quaternius animation test asset

CharacterControllerDemoLayer の実Humanoid Animation検証では、Quaternius Universal Animation Library [Standard] の通常版GLBを使用します。

次のファイルをこのディレクトリへ配置してください。

```text
Raven/Assets/Models/Quaternius/UAL1_Standard.glb
```

使用するのは `_RM` 付きRoot Motion版ではなく、まず通常版の `UAL1_Standard.glb` です。

初期Locomotion Profileは `Raven/Assets/Profiles/Quaternius_UAL1_Standard.json` にあり、以下を使用します。

- Idle: `Idle_Loop`
- Walk: `Walk_Loop`
- Run: `Jog_Fwd_Loop`
- Sprint: `Sprint_Loop`

CharacterController側では通常移動をWalk、`Left Shift` / `RT` をRun、`Left Ctrl` / `RB` をSprintとして分離しています。SprintはRunの別名ではなく独立したGameplay要求として保持するため、将来Stamina消費やSprint禁止状態を追加してもRunへ影響させず拡張できます。

この段階ではQuaternius GLB自身のMesh/Skeleton/Animationを同時に読み込み、Retargetingを介さず既存のSkinnedBlendTreeRuntime経路を検証します。`Raven_human_test.glb` へ適用するRetargetingは別実装単位とします。
