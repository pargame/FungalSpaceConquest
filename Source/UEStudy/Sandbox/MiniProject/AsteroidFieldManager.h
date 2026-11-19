#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AsteroidFieldManager.generated.h"

class ASpaceAsteroid;
class ASpaceMineral;

/** 무상태 프리베이크 풀 내 소행성 슬롯의 현재 생명주기 상태입니다. */
UENUM(BlueprintType)
enum class EAsteroidPoolState : uint8
{
	Free UMETA(DisplayName = "Free (미할당 대기)"),
	Active UMETA(DisplayName = "Active (활성 시뮬레이션)")
};

/**
 * 플레이어 기준 구형 껍질 C = [A, B]에서만 소행성을 동적 생성하고,
 * 반경 B 이탈 시 삭제 대기 큐(유예 시간 내 <= B 재진입 시 구제)로 소멸을 관리하는 스포너입니다.
 * 청크·좌표 기억 없이 무상태 아키타입 풀만 재사용해 스폰 렉(Convex 베이킹) 0ms를 유지합니다.
 */
UCLASS(Blueprintable)
class UESTUDY_API AAsteroidFieldManager : public AActor
{
	GENERATED_BODY()

public:
	AAsteroidFieldManager();

	/** 소행성 스폰 시 사용할 클래스입니다. 비워 두면 기본 BP_SpaceAsteroid 또는 ASpaceAsteroid를 사용합니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|Pool")
	TSubclassOf<ASpaceAsteroid> AsteroidClass;

	// ============================================================================
	// 01. 시드 & 플레이어 반지름 기준 스케일 (Seed & Player Scale)
	// ============================================================================

	/** 시드 기반 랜덤 소행성 생성을 제어하는 기준 월드 시드입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|01. Seed & Player Scale", meta = (ClampMin = "0"))
	int32 WorldSeed = 202605;

	/** 활성화 시 모든 필드 반경과 전방 확장·물리 반경이 현재 곰팡이폰 반지름에 비례해 자동 스케일링됩니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|01. Seed & Player Scale")
	bool bScaleFieldRadiiToPlayer = true;

	/** 플레이어 반지름 배율(BodyScale) 증가에 따라 필드 반경들이 확장되는 지수입니다(1.0 = 현재 반지름에 정비례). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|01. Seed & Player Scale", meta = (ClampMin = "0.0", ClampMax = "2.5", EditCondition = "bScaleFieldRadiiToPlayer"))
	float FieldRadiiGrowthExponent = 1.0f;

	/** 플레이어 폰을 찾지 못했거나 상대 스케일링이 꺼졌을 때 기준이 되는 기본 반지름(cm)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|01. Seed & Player Scale", meta = (ClampMin = "10.0"))
	float FallbackPlayerRadiusCm = 75.0f;

	/** 활성화 시 새로 스폰되는 소행성의 크기(균일 스케일·질량·전염 비용·보상)가 현재 플레이어 반지름 배율에 따라 점차 커집니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|01. Seed & Player Scale")
	bool bScaleSpawnedAsteroidsToPlayer = true;

	/** 플레이어 현재 반지름 배율 대비 스폰 소행성 크기 성장 지수입니다(0.75 = 성장 보상 체감 유지, 1.25 = 상대 전염 부담 일정). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|01. Seed & Player Scale", meta = (ClampMin = "0.0", ClampMax = "2.0", EditCondition = "bScaleSpawnedAsteroidsToPlayer"))
	float AsteroidSizeGrowthExponent = 0.75f;

	/** 스폰 소행성에 적용되는 최소 런타임 크기 배율입니다(플레이어가 초기 크기보다 줄어들어도 과도한 축소 방지). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|01. Seed & Player Scale", meta = (ClampMin = "0.25", ClampMax = "2.0", EditCondition = "bScaleSpawnedAsteroidsToPlayer"))
	float MinAsteroidRuntimeScale = 1.0f;

	/** 스폰 소행성에 적용되는 최대 런타임 크기 배율 상한입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|01. Seed & Player Scale", meta = (ClampMin = "1.0", ClampMax = "10.0", EditCondition = "bScaleSpawnedAsteroidsToPlayer"))
	float MaxAsteroidRuntimeScale = 3.5f;

	// ============================================================================
	// 02. 생성 · 유지 · 삭제 경계 (Spawn & Despawn Boundaries)
	// ============================================================================

	/** 현재 곰팡이폰 반지름 대비 내부 생성 금지 반경 배율입니다(런타임 보충 스폰은 이 반경 바깥쪽에서만 수행). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|02. Spawn & Despawn Boundaries", meta = (ClampMin = "10.0", ClampMax = "2000.0", DisplayName = "Inner No-Spawn Radius Multiplier"))
	float InnerNoSpawnRadiusMultiplier = 160.0f;

	/** 현재 곰팡이폰 반지름 대비 외곽 유지 한계(디스폰 경계) 반경 배율입니다(이 반경을 벗어나면 삭제 대기 큐로 진입). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|02. Spawn & Despawn Boundaries", meta = (ClampMin = "20.0", ClampMax = "3000.0", DisplayName = "Outer Despawn Radius Multiplier"))
	float OuterDespawnRadiusMultiplier = 295.0f;

	/** 외곽 유지 경계를 벗어난 소행성이 삭제 대기 큐에서 자동 소멸되기 전까지 유지되는 유예 시간(초)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|02. Spawn & Despawn Boundaries", meta = (ClampMin = "0.2", ClampMax = "15.0"))
	float DespawnGraceSeconds = 2.5f;

	/** 급가속 등으로 외곽 유지 경계를 크게 벗어났을 때 유예 시간 없이 즉시 정리하는 추가 이탈 거리 배율(현재 반지름 기준)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|02. Spawn & Despawn Boundaries", meta = (ClampMin = "5.0", ClampMax = "500.0"))
	float DespawnHardMarginMultiplier = 105.0f;

	/** 게임 시작(BeginPlay) 초기 1회 필드를 채울 때만 적용되는 최소 스폰 반경 배율(현재 곰팡이폰 반지름 × 배율)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|02. Spawn & Despawn Boundaries", meta = (ClampMin = "2.0", ClampMax = "100.0", DisplayName = "Initial Spawn Min Radius Multiplier"))
	float InitialSpawnInnerRadiusMultiplier = 5.0f;

	/** 게임 시작 직후 주변이 텅 비어 보이지 않도록 초기 배치 시 근거리 구역에 집중 스폰할 비율(0~0.5)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|02. Spawn & Despawn Boundaries", meta = (ClampMin = "0.0", ClampMax = "0.5", DisplayName = "Initial Near Cluster Ratio"))
	float InitialNearClusterFraction = 0.22f;

	/** 초기 근거리 집중 스폰 구역의 최대 반경 배율(현재 곰팡이폰 반지름 × 배율)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|02. Spawn & Despawn Boundaries", meta = (ClampMin = "8.0", ClampMax = "300.0"))
	float InitialNearClusterMaxRadiusMultiplier = 56.0f;

	/** 스폰 시 인접 소행성끼리 겹쳐 Chaos 충돌이 터지지 않도록 보장하는 최소 이격 거리 배율(현재 곰팡이폰 반지름 × 배율)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|02. Spawn & Despawn Boundaries", meta = (ClampMin = "1.5", ClampMax = "50.0"))
	float MinSpawnSeparationMultiplier = 4.25f;

	// ============================================================================
	// 03. 전방 시선 · 속도 비례 확장 (Forward Expansion)
	// ============================================================================

	/** 정지·저속 시에도 시선 전방의 생성·유지 경계를 앞으로 더 넓혀 자연 출현시키는 기본 전방 확장 배율(현재 반지름 × 배율)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|03. Forward Expansion", meta = (ClampMin = "0.0", ClampMax = "500.0"))
	float ForwardBaseExtensionMultiplier = 46.7f;

	/** 플레이어가 빠르게 이동할 때 속도 방향 전방의 생성·유지 범위를 추가로 확장하는 최대 전방 확장 배율(현재 반지름 × 배율)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|03. Forward Expansion", meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float ForwardVelocityMaxExtensionMultiplier = 186.7f;

	/** 위 최대 전방 확장 거리(100%)에 도달하는 기준 플레이어 이동 속력(cm/s)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|03. Forward Expansion", meta = (ClampMin = "100.0", ClampMax = "10000.0"))
	float VelocityReferenceSpeedCmPerSec = 1600.0f;

	/** 전방 확장 거리 중 내부 생성 금지 경계도 함께 앞으로 밀어낼 비율(0~1)입니다. 클수록 전방 스폰이 먼 곳에서 이루어집니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|03. Forward Expansion", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ForwardInnerExtensionFraction = 0.85f;

	/** 런타임 보충 시 이동·시선 전방 반구 쪽에 소행성을 우선 배치하는 확률 가중치(0=균등 구형, 1=전방 집중)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|03. Forward Expansion", meta = (ClampMin = "0.0", ClampMax = "0.98"))
	float ForwardSpawnBiasWeight = 0.85f;

	// ============================================================================
	// 04. 소행성 이동 · 회전 속도 (Asteroid Speed & Spin)
	// ============================================================================

	/** 활성화 시 필드 매니저에 설정된 소행성 이동·회전 속도 및 분산 지수를 모든 풀 소행성에 덮어씁니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|04. Asteroid Speed & Spin", meta = (DisplayName = "Override Asteroid Speed"))
	bool bOverrideAsteroidSpeedFromFieldManager = true;

	/** 소행성 초기 선형 이동 속력의 최소(X)·최대(Y) 범위(cm/s)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|04. Asteroid Speed & Spin", meta = (DisplayName = "Asteroid Linear Speed Range (cm/s)", EditCondition = "bOverrideAsteroidSpeedFromFieldManager"))
	FVector2D AsteroidLinearSpeedRange = FVector2D(15.0f, 2400.0f);

	/** 소행성 이동 속도 분산도입니다(1.0 = 균등, 클수록 아주 느리거나 아주 빠른 양극단 비율 증가, 1 미만 = 중간 속도 집중). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|04. Asteroid Speed & Spin", meta = (ClampMin = "0.05", ClampMax = "8.0", DisplayName = "Asteroid Linear Speed Dispersion", EditCondition = "bOverrideAsteroidSpeedFromFieldManager"))
	float AsteroidLinearSpeedDistributionExponent = 1.65f;

	/** 소행성 초기 회전 각속력의 최소(X)·최대(Y) 범위(deg/s)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|04. Asteroid Speed & Spin", meta = (DisplayName = "Asteroid Angular Speed Range (deg/s)", EditCondition = "bOverrideAsteroidSpeedFromFieldManager"))
	FVector2D AsteroidAngularSpeedDegRange = FVector2D(2.0f, 140.0f);

	/** 소행성 회전 각속력 분산도입니다(1.0 = 균등, 클수록 아주 느리게 돌거나 아주 빠르게 도는 양극단 비율 증가, 1 미만 = 중간 각속력 집중). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|04. Asteroid Speed & Spin", meta = (ClampMin = "0.05", ClampMax = "8.0", DisplayName = "Asteroid Angular Speed Dispersion", EditCondition = "bOverrideAsteroidSpeedFromFieldManager"))
	float AsteroidAngularSpeedDistributionExponent = 1.50f;

	// ============================================================================
	// 05. 오브젝트 풀 & 스트리밍 (Object Pool & Streaming)
	// ============================================================================

	/** 플레이어 주변에 유지하려는 목표 활성 소행성 개수입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|05. Object Pool & Streaming", meta = (ClampMin = "8", ClampMax = "1536"))
	int32 TargetActiveAsteroids = 950;

	/** 다양한 크기·형태·재질을 미리 구워 보관할 무상태 아키타입 풀 총량입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|05. Object Pool & Streaming", meta = (ClampMin = "8", ClampMax = "2048"))
	int32 MaxPooledAsteroids = 1150;

	/** BeginPlay 시 미리 스폰·프리베이크해 둘 대기(Free) 아키타입 수입니다. MaxPooledAsteroids와 같으면 런타임 메시 재빌드가 0회가 됩니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|05. Object Pool & Streaming", meta = (ClampMin = "0", ClampMax = "2048"))
	int32 PrewarmPoolCount = 1150;

	/** 풀 슬롯의 사전 베이킹 메시·Chaos 콜리전을 재사용해 0ms로 활성화할지 여부입니다. false면 스폰 시마다 새 시드로 실시간 메시를 굽습니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|05. Object Pool & Streaming")
	bool bUsePrebakedSwitchPool = true;

	/** 런타임 스캔 1회당 생성·유지 껍질에서 새로 보충할 수 있는 최대 소행성 수입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|05. Object Pool & Streaming", meta = (ClampMin = "1", ClampMax = "64"))
	int32 MaxSpawnsPerScan = 32;

	/** 거리 판정·삭제 대기 큐 처리·보충 스폰을 수행하는 주기(초)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|05. Object Pool & Streaming", meta = (ClampMin = "0.02", ClampMax = "2.0"))
	float ScanIntervalSeconds = 0.05f;

	// ============================================================================
	// 06. 거리 기반 물리 티어링 (Distance Physics Tiering)
	// ============================================================================

	/** 근거리 소행성만 Chaos 물리 솔버 풀에 넣고 원거리는 등속 Kinematic 유영으로 분리해 물리 부하를 줄입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|06. Distance Physics Tiering")
	bool bEnableDistancePhysicsTiering = true;

	/** 플레이어 기준 Chaos 물리 시뮬레이션을 활성화할 근거리 반경 배율(현재 곰팡이폰 반지름 × 배율)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|06. Distance Physics Tiering", meta = (ClampMin = "10.0", ClampMax = "500.0", EditCondition = "bEnableDistancePhysicsTiering"))
	float PhysicsActivationRadiusMultiplier = 46.7f;

	/** 물리 활성화 경계에서 잦은 전환(Chattering)을 막기 위한 히스테리시스 여유 거리 배율(현재 곰팡이폰 반지름 × 배율)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|06. Distance Physics Tiering", meta = (ClampMin = "2.0", ClampMax = "100.0", EditCondition = "bEnableDistancePhysicsTiering"))
	float PhysicsHysteresisMultiplier = 9.3f;

	/** Chaos 솔버에 동시 등록할 수 있는 최대 근거리 소행성 수입니다. 초과분은 거리순으로 먼 것부터 Kinematic으로 강등됩니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|06. Distance Physics Tiering", meta = (ClampMin = "4", ClampMax = "256", EditCondition = "bEnableDistancePhysicsTiering"))
	int32 MaxActivePhysicsAsteroids = 48;

	// ============================================================================
	// 07. 미네랄 생성 · 분포 (Minerals)
	// ============================================================================

	/** 소행성 표면 및 우주 공간 부유 미네랄 광석을 함께 스폰할지 여부입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|07. Minerals")
	bool bEnableMineralSpawning = true;

	/** 미네랄 광석 스폰 시 사용할 클래스입니다. 비워 두면 기본 ASpaceMineral을 사용합니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|07. Minerals", meta = (EditCondition = "bEnableMineralSpawning"))
	TSubclassOf<ASpaceMineral> MineralClass;

	/** 풀에 미리 프리베이크해 보관할 최대 미네랄 광석 액터 수입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|07. Minerals", meta = (ClampMin = "0", ClampMax = "2048", EditCondition = "bEnableMineralSpawning"))
	int32 MaxPooledMinerals = 640;

	/** 활성 소행성 목표 수에 비례해 유지하려는 독립 부유 미네랄 비율(0~1)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|07. Minerals", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bEnableMineralSpawning"))
	float FloatingMineralTargetRatio = 0.14f;

	/** 기준 반경 이상인 소행성 표면에 미네랄 광석이 자라날 확률(0~1)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|07. Minerals", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bEnableMineralSpawning"))
	float SurfaceMineralChance = 0.40f;

	/** 소행성 1개 표면에 자라나는 미네랄 클러스터 최소·최대 개수 범위입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|07. Minerals", meta = (EditCondition = "bEnableMineralSpawning"))
	FInt32Interval SurfaceMineralCountRange = FInt32Interval(1, 2);

	/** 표면 미네랄 개수 분산도 지수입니다(1.0 = 균등, 클수록 최소 개수 쪽 편향, 1 미만 = 최대 개수 쪽 편향). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|07. Minerals", meta = (ClampMin = "0.1", ClampMax = "8.0", EditCondition = "bEnableMineralSpawning"))
	float SurfaceMineralDistributionExponent = 1.6f;

	/** 표면에 미네랄 광석이 부착되기 위한 소행성 최소 평균 반지름(cm)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|07. Minerals", meta = (ClampMin = "10.0", ClampMax = "500.0", EditCondition = "bEnableMineralSpawning"))
	float MinAsteroidRadiusForSurfaceMineralCm = 55.0f;

	/** 최소 개수 이상으로 추가 미네랄 클러스터가 자라나기 위한 소행성 최소 반지름(cm)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|07. Minerals", meta = (ClampMin = "20.0", ClampMax = "1000.0", EditCondition = "bEnableMineralSpawning"))
	float SurfaceMineralExtraClusterMinRadiusCm = 115.0f;

	// ============================================================================
	// 08. 전염 폭발 보상 (Colonization Rewards)
	// ============================================================================

	/** 전염된 소행성이 폭발할 때 생성된 미네랄을 끊김 없이 플레이어 본체로 즉시 흡수(원테이크)할지 여부입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|08. Colonization Rewards")
	bool bAutoAbsorbColonizedExplosionMinerals = true;

	/** 전염된 소행성 폭발 시 투입된 생체량(Biomass) 1당 생성할 미네랄 개수 비율입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|08. Colonization Rewards", meta = (ClampMin = "0.02", ClampMax = "2.0"))
	float ColonizedExplosionMineralsPerBiomass = 0.35f;

	/** 전염된 소행성 폭발 시 크기와 무관하게 추가되는 기본 보너스 미네랄 개수입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|08. Colonization Rewards", meta = (ClampMin = "0", ClampMax = "20"))
	int32 ColonizedExplosionBonusMinerals = 5;

	/** 전염된 소행성 폭발 시 생성되는 최소/최대 미네랄 개수 클램프입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|08. Colonization Rewards")
	FInt32Interval ColonizedExplosionMineralCountRange = FInt32Interval(6, 26);

	/** 전염된 소행성 폭발 시 미네랄이 사방으로 퍼져 나가는 초기 방출 속력(cm/s) 범위입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|08. Colonization Rewards")
	FVector2D ColonizedExplosionBurstSpeedRange = FVector2D(55.0f, 165.0f);

	/** 전염된 소행성 폭발 시 투입된 생체량 대비 최소한 보장되는 순수익 보상 배율입니다(예: 1.65 = 투자한 생체량의 165% 회수). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|08. Colonization Rewards", meta = (ClampMin = "1.05", ClampMax = "5.0"))
	float ColonizedNetProfitMultiplier = 1.65f;

	/** 전염된 소행성 폭발 시 추가로 보장되는 고정 보너스 생체량입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|08. Colonization Rewards", meta = (ClampMin = "0.0", ClampMax = "500.0"))
	float ColonizedBonusBiomass = 30.0f;

	// ============================================================================
	// 09. 디버그 & 런타임 통계 (Debug & Stats)
	// ============================================================================

	/** 런타임 구형 껍질 스폰·소멸 루프 활성화 여부입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|09. Debug & Stats")
	bool bEnableFieldStreaming = true;

	/** 플레이어 중심 내부 금지·외곽 유지 경계 구체와 활성/삭제 대기 큐 상태를 디버그 와이어로 시각화할지 여부입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsteroidField|09. Debug & Stats")
	bool bDrawDebugField = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AsteroidField|09. Debug & Stats")
	int32 ActiveAsteroidCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AsteroidField|09. Debug & Stats")
	int32 ActivePhysicsAsteroidCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AsteroidField|09. Debug & Stats")
	int32 KinematicAsteroidCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AsteroidField|09. Debug & Stats")
	int32 PendingDespawnCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AsteroidField|09. Debug & Stats")
	int32 FreePoolSlotCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AsteroidField|09. Debug & Stats")
	int32 TotalPooledCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AsteroidField|09. Debug & Stats")
	int32 ActiveMineralCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AsteroidField|09. Debug & Stats")
	int32 TotalSpawnsCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AsteroidField|09. Debug & Stats")
	int32 TotalDespawnsCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AsteroidField|09. Debug & Stats")
	int32 TotalDespawnRescuesCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AsteroidField|09. Debug & Stats")
	int32 TotalBuildsCount = 0;

	/** 미네랄 흡수 완료 알림용 콜백 (무상태 구조에서는 소비 해시 없이 통계/확장 훅으로 유지). */
	void MarkMineralConsumed(uint32 MineralHash);

	/** 흡수가 끝난 미네랄 광석을 즉시 풀의 Free 상태로 반납합니다. */
	void ReturnMineralToPool(ASpaceMineral* Mineral);

	/** 전염 배양이 끝난 소행성을 미네랄 폭발로 전환하고 소행성 슬롯을 풀에 반납합니다. */
	void ExplodeColonizedAsteroid(ASpaceAsteroid* Asteroid);

	/** 지정 위치에서 총 생체량 보상을 담은 미네랄 폭발을 생성합니다(소행성·적군 곰팡이 공용). */
	int32 SpawnMineralBurst(
		const FVector& ExplosionCenter,
		float SourceRadiusCm,
		float TotalGuaranteedBiomass,
		int32 DesiredSpawnCount,
		int32 BurstSeed,
		bool bAutoAbsorbByPlayer = true
	);

	/** TActorIterator 월드 전수 순회 없이 현재 활성(표면 부착·우주 부유) 미네랄 목록을 반환합니다. */
	void GatherAvailableMinerals(TArray<ASpaceMineral*>& OutMinerals) const;

	/** C = [A, B] 보충 및 삭제 대기 큐 평가를 1회 강제 수행합니다. */
	UFUNCTION(BlueprintCallable, Category = "AsteroidField")
	void ForceRefreshField(int32 CustomSpawnBudget = -1);

	/** 지정된 시드로 활성 소행성을 모두 회수한 뒤 새 초기 우주 필드를 구성합니다. */
	UFUNCTION(BlueprintCallable, Category = "AsteroidField")
	void ResetFieldWithWorldSeed(int32 NewWorldSeed);

	virtual void Tick(float DeltaTime) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	struct FAsteroidPoolEntry
	{
		TObjectPtr<ASpaceAsteroid> Asteroid = nullptr;
		EAsteroidPoolState State = EAsteroidPoolState::Free;
		bool bPendingDespawn = false;
		double DespawnQueuedTime = 0.0;
	};

	struct FMineralPoolEntry
	{
		TObjectPtr<ASpaceMineral> Mineral = nullptr;
		bool bInUse = false;
		bool bIsFloating = false;
		bool bPendingDespawn = false;
		double DespawnQueuedTime = 0.0;
		TWeakObjectPtr<ASpaceAsteroid> HostAsteroid;
	};

	void SanitizeRuntimeSettings();
	void SyncTickInterval();
	void ReleaseAllActiveImmediate();

	UClass* ResolveAsteroidSpawnClass();
	ASpaceAsteroid* SpawnDeferredPooledAsteroid();
	void PrewarmPool(int32 TargetFreeCount);
	int32 AcquireRandomFreeAsteroidSlot();
	void DespawnAsteroidSlot(int32 PoolIdx);

	UClass* ResolveMineralSpawnClass() const;
	void PrewarmMineralPool(int32 TargetCount);
	int32 AcquireFreeMineralSlotIndex();
	void ReleaseMineralSlot(int32 MineralIdx);
	void ReleaseMineralsForHostAsteroid(ASpaceAsteroid* HostAsteroid);
	void SpawnSurfaceMineralsForAsteroid(ASpaceAsteroid* HostAsteroid, int32 AsteroidSeed);
	bool TrySpawnFloatingMineralInShell(
		const FVector& PlayerLocation,
		const FVector& ForwardDir,
		float ForwardExtensionCm,
		float BaseMinRadiusCm,
		float BaseMaxRadiusCm,
		FRandomStream& Stream);

	bool ResolvePlayerFocus(
		FVector& OutPlayerLocation,
		FVector& OutForwardDir,
		float& OutForwardExtensionCm,
		AActor*& OutLatchedTarget) const;
	void ComputeDirectionalRadii(
		const FVector& OffsetFromPlayer,
		const FVector& ForwardDir,
		float ForwardExtensionCm,
		float& OutMinSpawnRadiusCm,
		float& OutMaxDespawnRadiusCm) const;
	float GetPlayerGrowthRatio(float* OutInitRadiusCm = nullptr) const;
	float GetEffectiveAsteroidSpawnScale() const;
	float GetEffectivePlayerRadiusCm() const;
	float GetEffectiveInnerRadiusA() const;
	float GetEffectiveOuterRadiusB() const;
	float GetEffectiveDespawnHardMarginCm() const;
	float GetEffectiveMinSpawnSeparationCm() const;
	FVector SampleDirectionInShell(
		const FVector& ForwardDir,
		bool bUseForwardBias,
		FRandomStream& Stream) const;
	FVector SampleVolumeUniformInShell(
		const FVector& Center,
		const FVector& ForwardDir,
		float ForwardExtensionCm,
		bool bInitialPopulate,
		float BaseMinRadiusCm,
		float BaseMaxRadiusCm,
		FRandomStream& Stream) const;
	bool IsSpawnLocationClear(
		const FVector& CandidateLocation,
		const FVector& PlayerLocation,
		const FVector& ForwardDir,
		float ForwardExtensionCm,
		float BaseMinRadiusCm,
		bool bInitialPopulate,
		const TArray<FVector>& ActiveLocations) const;
	bool SpawnAsteroidInShell(
		const FVector& PlayerLocation,
		const FVector& ForwardDir,
		float ForwardExtensionCm,
		float BaseMinRadiusCm,
		float BaseMaxRadiusCm,
		bool bImmediateFade,
		TArray<FVector>& InOutActiveLocations,
		FRandomStream& Stream);

	void EvaluateFieldLifecycle(int32 SpawnBudget, bool bInitialPopulate);
	void UpdatePhysicsSimulationTiers(const FVector& PlayerLocation, AActor* LatchedTarget);
	void StepKinematicAsteroids(float DeltaTime);
	void SyncStatsCounters();
	void DrawDebugVisualization(const FVector& PlayerLocation, const FVector& ForwardDir, float ForwardExtensionCm) const;

	TArray<FAsteroidPoolEntry> PoolEntries;
	TArray<int32> FreePoolIndices;
	TArray<int32> ActivePoolIndices;
	TArray<int32> PendingDespawnQueue;

	TArray<FMineralPoolEntry> MineralPoolEntries;
	TArray<TWeakObjectPtr<ASpaceAsteroid>> ExternalUnmanagedAsteroids;

	UPROPERTY(Transient)
	TObjectPtr<UClass> ResolvedSpawnClass;

	uint32 SpawnSequenceCounter = 0;
	float DistanceFadeBandCm = 2200.0f;
	float ScanAccumulator = 0.0f;
	bool bHasInitialPopulated = false;
	bool bHadPlayerPawnOnLastScan = false;
};
