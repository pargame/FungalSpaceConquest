# MiniProject (Fungal Space Conquest) 언리얼 아키텍처 및 객체 모델링 가이드

이 문서는 `Source/UEStudy/Sandbox/MiniProject/` 및 `/Game/Sandbox/MiniProject/`의 객체지향 설계(OOP), 언리얼 엔진 5.9 표준 아키텍처, 성능 최적화 원칙을 정리한 레퍼런스입니다. 이후 작업을 이어받는 AI 에이전트는 반드시 이 설계 원칙과 확장 포인트를 준수해야 합니다.

> **🔥 IMPORTANT:** C++로 바퀴를 다시 발명하지 마십시오. 언리얼 엔진이 기본 제공하는 내장 컴포넌트, 에셋 옵션, 리플렉션 기능(`Niagara Local Space`, `Niagara Mesh/Ribbon Renderer`, `FRuntimeFloatCurve`/`UCurveFloat`, `USpringArmComponent`, `FTimerManager`, `UPhysicsConstraintComponent`, `TickGroup`, `AddTickPrerequisiteActor` 등)을 최우선으로 활용해야 합니다.

---

## 1. 전체 객체 모델링 및 클래스 계층 구조

```text
[ISpaceTendrilHostInterface]  <-- (구현) -- [ASpaceDrifterPawn : APawn]
         ^                                        |
         | (TScriptInterface 의존성 역전)         +-- (Instanced 소유) --> [USpaceTendrilAbility : UObject (Abstract)]
         |                                                                          |-- [ULongTendrilAbility] (V1 긴 촉수)
         +--------------------------------------------------------------------------+-- [UNetTendrilAbility]  (V2 그물 촉수)
                                                                                    +-- [UJetTendrilAbility]  (V3 분사 촉수)

[AAsteroidFieldManager : AActor]
  |-- (오브젝트 풀 & 2-Tier 물리 스케줄링) --> [ASpaceAsteroid : AActor] (0-Tick, FRuntimeFloatCurve 크기 분포)
  +-- (활성 미네랄 레지스트리 관리)       --> [ASpaceMineral : AActor]
```

| 구분 | 클래스 / 인터페이스 | 파일 경로 | 역할 및 언리얼 설계 패턴 |
| :--- | :--- | :--- | :--- |
| **인터페이스** | `ISpaceTendrilHostInterface` | `SpaceTendrilHostInterface.h` | 촉수 능력이 호스트 액터(`ASpaceDrifterPawn` 또는 향후 적/NPC)의 코어 위치·카메라 기저·물리 루트·나이아가라 컴포넌트를 조회하는 `UINTERFACE` 계약 |
| **전염 계약** | `ISpaceColonizableTarget` | `SpaceColonizableTarget.h` | V1 긴 촉수로 전염·배양 폭발 가능한 대상(`ASpaceAsteroid`, `ASpaceEnemyMold`)의 공통 `UINTERFACE` 계약 |
| **적군 곰팡이** | `ASpaceEnemyMold` | `SpaceEnemyMold.h / .cpp` | 0-Tick 물리 구체 적 액터. `M_EnemyMold`(베놈 심비오트 스타일 WPO 가시·검은 타르 광택·`IsHuntable` 림 전환) 및 V2 그물 둔화(`ApplyWebSlow`)·`ISpaceColonizableTarget` 구현 |
| **적 매니저** | `AEnemyMoldManager` | `EnemyMoldManager.h / .cpp` | 레벨 배치형 매니저. 공격 티켓(`MaxAttackingEnemies=2`, 히스테리시스·V2 둔화 보호·비위협 0.5s 유예·비티켓 포위 링 `950~1450cm` 선회·`Colonizing` 조향 정지), 최대 도달 크기(`MaxReachedBodyRadiusCm`) 기준 스폰, 단일 진실 `IsEnemyThreatToPlayer` 기반 접촉 흡수, 시간/미네랄 완만 스케일링(`90s`/`50` 미네랄, 둘 중 큰 값), 원거리 추격 가속(표면 거리 `1200~4800cm`에서 최대 `×2.6`) 및 개별 마커 데이터(`GatherAllThreatInfos`) 제공 |
| **리더보드** | `USpaceDrifterLeaderboardSave` | `SpaceDrifterLeaderboardSave.h` | `USaveGame` 기반 로컬 리더보드 영구 저장(최대 20건, HUD에 TOP 5 표시·초기화). 정렬 키는 `USpaceDrifterGameRulesSettings`에서 정의(기본: 클리어 여부 → 클리어 시간 → 처치 수 → 미네랄 → 최대 반지름) |
| **게임 규칙** | `USpaceDrifterGameRulesSettings` | `SpaceDrifterGameRules.h / .cpp` | `UDeveloperSettings`(`Config=Game`) 기반 클리어 조건(AND/OR 조건 그룹)과 리더보드 정렬 키 정의. 기본 클리어 조건은 포식자 6마리 처치 AND 미네랄 200개 |
| **능력 베이스** | `USpaceTendrilAbility` | `SpaceTendrilAbility.h / .cpp` | `EditInlineNew`, `DefaultToInstanced` 추상 `UObject`. 평상시 `0-Tick`, 발동 중에만 조건부 Tick, `FTimerManager` 유지 시간 제한 및 공통 쿨타임(`CooldownSeconds`) 관리 |
| **1번 능력** | `ULongTendrilAbility` | `SpaceTendrilAbility.h / .cpp` | 홀드형 그래플링 로프. 스피어 스윕 조준, `ISpaceColonizableTarget` 락온, 길이 비례 인력·최대 속도 증폭, 가용 생체량 대비 `%/s` 전염 속도·사전 실패 판정 및 전염 완료 직후 재발사 잠금(`PostColonizationLockoutSeconds = 1.4s`) |
| **2번 능력** | `UNetTendrilAbility` | `SpaceTendrilAbility.h / .cpp` | 단발형(One-Shot) 광역 포자 그물망(포식자 2마리 처치 시 해금, 쿨타임 7.8초). 미네랄 대량 포획 + 광역 원뿔 내 모든 포식자 이동 속도 35% 영구 둔화(재적중 시 ×0.85, 하한 20%) + 반지름 -15%·생체량 연동 영구 축소(최소 50% 하한) |
| **3번 능력** | `UJetTendrilAbility` | `SpaceTendrilAbility.h / .cpp` | 단발형(One-Shot) 긴급 탈출 분사(포식자 3마리 처치 시 해금, 쿨타임 1.0초). 가용 생체량 8%(최소 4) 소모 + 즉각 전방 고속 임펄스 탈출(`+1400 cm/s`) |
| **플레이어 폰** | `ASpaceDrifterPawn` | `SpaceDrifterPawn.h / .cpp` | 물리 구체(`CollisionSphere`) + 시각 메시(`DrifterMesh`) + 성장 체감 카메라(`CameraArmBodyScaleExponent=0.72`, `BiomassToRadiusExponent=0.40`) + 판 상태(`ESpaceRunState::Victory/GameOver`) 관리. 클리어 판정은 `USpaceDrifterGameRulesSettings`의 규칙을 따르며, V2/V3 해금은 처치 수 기준(`V2UnlockDefeatedEnemyCount=2`, `V3UnlockDefeatedEnemyCount=3`) |
| **소행성** | `ASpaceAsteroid` | `SpaceAsteroid.h / .cpp` | `PrimaryActorTick.bCanEverTick = false` (`0-Tick`). 시드 결정론적 절차적 메시 및 `FRuntimeFloatCurve`(`SizeDistributionCurve`) 기반 크기 분포 |
| **필드 매니저** | `AAsteroidFieldManager` | `AsteroidFieldManager.h / .cpp` | 프리웜 오브젝트 풀링, 거리 기반 2-Tier 물리 전환(근거리 Chaos 강체 ↔ 원거리 Kinematic 드리프트), 플레이어 현재 크기 비례 0ms 스폰 소행성 스케일링(`AsteroidSizeGrowthExponent=0.75`, `1.0~3.5x`), 공용 미네랄 폭발(`SpawnMineralBurst`) 관리 |
| **컨트롤러** | `ASpaceDrifterPlayerController` | `SpaceDrifterPlayerController.h / .cpp` | Enhanced Input 등록, 타이틀 이름 입력(Enter 확정 → `SPACE` 전용 시작 + 관통 방지 릴리즈 가드) 및 종료 시 `SPACE` 재시작 관리 |
| **통합 UI** | `USpaceDrifterHUDWidget` | `SpaceDrifterHUDWidget.h / .cpp` | 순수 Slate(`RebuildWidget`) 기반 `UUserWidget`. 타이틀 이름 자동 포커스·TOP 5 리더보드/초기화, 인게임 이중 세그먼트 생체량 % 바·4면 피격 붉은 비네트·포식자별 개별 마커 풀(최대 8개)·쿨타임 슬롯 제공 |

---

## 2. 핵심 언리얼 설계 원칙 (고려된 엔지니어링 포인트)

### 2.1 `UActorComponent` 대신 `EditInlineNew` + `DefaultToInstanced` `UObject` 채택
- **이유**: 각 능력을 `UActorComponent`로 만들면 비활성 상태의 능력까지 에디터/런타임 컴포넌트 트리와 `FTickTaskManager`에 상주합니다.
- **적용**: `UCLASS(Abstract, BlueprintType, Blueprintable, EditInlineNew, DefaultToInstanced)`가 지정된 `USpaceTendrilAbility` 서브오브젝트를 사용합니다. 블루프린트(`BP_SpaceDrifterPawn`) 디테일 패널에서 각 능력의 프로퍼티를 인라인으로 편집할 수 있으면서도, `FTickTaskManager` 등록 오버헤드가 전혀 없습니다.

### 2.2 조건부 0-Tick 및 `FTimerManager` 이벤트 드리븐 설계
- **능력 0-Tick**: `ASpaceDrifterPawn::Tick`에서 `ActiveTendrilAbility->IsAbilityTickActive()`가 `true`(전개·회수 중)일 때만 `TickActiveAbility(DeltaTime)`를 호출합니다. 평상시(`Idle`)에는 능력 로직이 전혀 실행되지 않습니다.
- **타이머 이벤트**: V1 최대 홀드 시간 및 V2 단발 그물 유지 시간은 매 프레임 시간을 차감하는 폴링 코드 대신 언리얼 내장 `UWorld::GetTimerManager().SetTimer(...)` 원샷 타이머(`ArmHoldLimitTimer`)로 처리하며, V1 전염 완료 직후 배양·폭발 구간의 오발사는 월드 시간 기반 `PostColonizationLockoutSeconds`(`CanActivateAbility` 오버라이드)로 조용히 차단합니다.
- **소행성 0-Tick 및 Hit 콜백 차단**: `ASpaceAsteroid`는 자체 `Tick`을 완전히 껐고(`bCanEverTick = false`), 소행성끼리의 무의미한 충돌 이벤트 디스패치를 막기 위해 `AsteroidMesh->SetNotifyRigidBodyCollision(false)`를 적용했습니다(플레이어의 `CollisionSphere`에서만 Hit 이벤트 수신).

### 2.3 고속 이동 시 촉수 분리(Lag) 원천 차단 (`TickGroup` & `bLocalSpace`)
- **문제 원인**: 플레이어 물리 본체(`CollisionSphere`)는 Chaos 물리 스레드(`TG_DuringPhysics`)에서 위치가 갱신되는데, 액터 기본 틱(`TG_PrePhysics`)에서 촉수 시작점을 읽으면 항상 1프레임 과거 위치를 참조하여 고속 이동 시 본체와 촉수가 떨어져 보입니다.
- **언리얼 내장 해결책**:
  1. `ASpaceDrifterPawn`의 틱 그룹을 `TG_PostPhysics`로 설정(`PrimaryActorTick.TickGroup = TG_PostPhysics` 및 `BeginPlay` 내 `SetTickGroup(TG_PostPhysics)`).
  2. 나이아가라 컴포넌트(`MyceliumWebFX`, `GossamerSporesFX`, `SporeJetFX`) 역시 `TG_PostPhysics`로 맞추고 `FX->AddTickPrerequisiteActor(this)`를 등록해 **물리 적분 → Pawn의 촉수 좌표 갱신 → 나이아가라 시뮬레이션**이 동일 프레임 안에서 순서대로 실행되도록 보장했습니다.
  3. V3 분사 촉수(`NS_Tendril_SporeJet`)는 이미터의 내장 `bLocalSpace = true` 옵션을 활성화하여 별도 좌표 보정 코드 없이도 본체와 100% 일체화되어 움직입니다.

### 2.4 나이아가라 내장 렌더러 활용 및 머티리얼 사용 플래그 주의사항
- V3 분사 촉수(`NS_Tendril_SporeJet`)는 C++에서 `UStaticMeshComponent`를 생성·풀링하지 않고, 나이아가라 내장 **`NiagaraMeshRendererProperties`**에 본체와 동일한 메시(`SM_DustPuffSphere`)와 머티리얼 인스턴스(`MI_DustPuff`)를 지정해 렌더링합니다.
- **주의사항**: 나이아가라 메시 렌더러에 커스텀 머티리얼을 연결할 때는 부모 머티리얼(`M_DustPuff`)의 **`bUsedWithNiagaraMeshParticles = true`** 플래그가 반드시 켜져 있어야 디폴트 격자 머티리얼로 폴백되지 않습니다.

### 2.5 소행성 2-Tier 물리 스케줄링 및 `FRuntimeFloatCurve` 분포
- **2-Tier 물리**: `AAsteroidFieldManager`는 680개 활성 소행성 중 플레이어 반경 `3500cm`(`PhysicsActivationRadiusCm`, 히스테리시스 `700cm`) 이내의 최대 `48개`(`MaxActivePhysicsAsteroids`)만 Chaos 강체 물리를 켜고, 원거리 소행성은 Chaos 솔버에서 제외한 채 `StepKinematicDrift`로 등속 유영만 수행합니다.
- **내장 커브 기반 크기 분산**: `ASpaceAsteroid`는 하드코딩된 수식 대신 내장 `FRuntimeFloatCurve`(`SizeDistributionCurve`)를 통해 `0~1` 난수를 반경 스케일(`V^(1/3)`) 비율로 매핑한 뒤 세제곱하여 질량(`MinMassKg = 25` ~ `MaxMassKg = 420000`, 최대 직경 약 `14.5m`)을 도출합니다. 에디터 디테일 패널에서 마우스로 그래프를 직접 수정하거나 외부 `UCurveFloat` 에셋을 꽂아 쓸 수 있습니다.

---

## 3. 향후 기능 확장 가이드 (다른 AI 모델을 위한 체크리스트)

1. **V3 분사 촉수에 스태미나/미네랄 소모 또는 과열 쿨타임 추가 시**:
   - `ASpaceDrifterPawn`이나 베이스 클래스를 수정하지 마십시오.
   - `SpaceTendrilAbility.h`의 `UJetTendrilAbility::CanConsumeJetResource()`와 `ConsumeJetResource(float DeltaTime)` 두 가상 함수만 오버라이드하면 발사 차단 및 분사 중 자동 회수(`BeginRetract`)가 즉시 동작합니다.
2. **새로운 4번 촉수 능력 추가 시**:
   - `USpaceTendrilAbility`를 상속받는 새 서브클래스를 만들고 `OnAbilityActivated()`, `OnActiveTick()`, `OnAbilityReturnedToIdle()`만 구현하십시오.
3. **새로운 연출·물리 기능 추가 시**:
   - C++로 수동 보간기·파티클 풀·물리 솔버를 직접 작성하기 전에, 반드시 언리얼 내장 기능(`UPhysicsConstraintComponent`, `USpringArmComponent`, `FTimerManager`, `UTimelineComponent`, `FRuntimeFloatCurve`, `Niagara` 내장 모듈)으로 해결 가능한지 먼저 확인하십시오.
