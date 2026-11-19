#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemyMoldManager.generated.h"

class ASpaceEnemyMold;
class ASpaceDrifterPawn;
class AAsteroidFieldManager;

struct FEnemyThreatMarkerInfo
{
	FVector WorldLocation = FVector::ZeroVector;
	float RadiusCm = 90.0f;
	float DistanceCm = 0.0f;
	bool bIsThreat = true;
	bool bIsWebSlowed = false;
};

/**
 * 플레이어를 집요하게 추적하는 적군 곰팡이 풀을 관리하는 레벨 배치형 매니저입니다.
 * 적 개체들은 0-Tick을 유지하며, 이 매니저가 일괄 틱으로 추격 조향·거리 기반 접촉 흡수·쿨타임 재배치를 수행합니다.
 */
UCLASS(Blueprintable)
class UESTUDY_API AEnemyMoldManager : public AActor
{
	GENERATED_BODY()

public:
	AEnemyMoldManager();

	/** 스폰할 적 곰팡이 클래스입니다(비워 두면 ASpaceEnemyMold 사용). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|Spawn")
	TSubclassOf<ASpaceEnemyMold> EnemyMoldClass;

	/** 시작 시 동시에 활성 유지할 초기 최대 적 곰팡이 수입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|Scaling", meta = (ClampMin = "1", ClampMax = "20"))
	int32 InitialMaxActiveEnemies = 1;

	/** 게임이 진행되어도 동시에 활성화될 수 있는 절대 최대 적 곰팡이 수(풀 프리웜 크기)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|Scaling", meta = (ClampMin = "1", ClampMax = "20"))
	int32 AbsoluteMaxActiveEnemies = 6;

	/** 생존 시간이 이 간격(초)만큼 지날 때마다 동시 최대 적 수가 1마리 증가합니다(0이면 시간 스케일링 미사용). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|Scaling", meta = (ClampMin = "0.0", ClampMax = "600.0"))
	float SecondsPerAdditionalEnemy = 90.0f;

	/** 흡수한 미네랄이 이 개수만큼 늘 때마다 동시 최대 적 수가 1마리 증가합니다(0이면 미네랄 스케일링 미사용). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|Scaling", meta = (ClampMin = "0", ClampMax = "500"))
	int32 MineralsPerAdditionalEnemy = 50;

	/** true면 시간 기준과 미네랄 기준 증가분 중 큰 값 적용, false면 두 증가분을 합산합니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|Scaling")
	bool bUseMaxOfTimeAndMineralScaling = true;

	/** 동시 최대 적 수가 늘어 빈 슬롯이 생겼을 때 추가 포식자가 출현하는 간격(초)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|Scaling", meta = (ClampMin = "0.5", ClampMax = "60.0"))
	float ExtraEnemySpawnIntervalSeconds = 12.0f;

	/** 게임 시작(타이틀 해제) 후 첫 적이 출현하기까지의 대기 시간(초)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|Spawn", meta = (ClampMin = "0.5", ClampMax = "60.0"))
	float InitialSpawnDelaySeconds = 12.0f;

	/** 적이 역포식으로 처치된 뒤 다음 적이 출현하기까지의 쿨타임(초, 다른 포식자가 남아 있을 때 적용)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|Spawn", meta = (ClampMin = "2.0", ClampMax = "120.0"))
	float DefeatRespawnCooldownSeconds = 10.0f;

	/** 필드에 활성 포식자가 0마리일 때(먹을 포식자가 없을 때) 빠르게 다음 포식자를 출현시키는 단축 리스폰 시간(초)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|Spawn", meta = (ClampMin = "0.5", ClampMax = "30.0"))
	float EmptyFieldRespawnCooldownSeconds = 2.0f;

	/** 카메라가 포식자 중심으로부터 (적 반지름 * 배율 + 45cm) 안쪽으로 침투하면 시각 메시를 숨겨 화면 가림·어지럼을 방지합니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|Visual", meta = (ClampMin = "1.0", ClampMax = "3.0"))
	float CameraOcclusionHideRadiusMultiplier = 1.65f;

	/** 출현 시 플레이어로부터 떨어뜨릴 거리 범위(cm, 소행성 Chaos 물리 반경 3500cm 이내 권장)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|Spawn")
	FVector2D SpawnDistanceRangeCm = FVector2D(2400.0f, 3200.0f);

	/** 출현 시 플레이어 최대 도달 반지름 대비 적 반지름 배율 범위(항상 최대 도달 크기보다 약간 크게 등장)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|Size")
	FVector2D SpawnRadiusMultiplierRange = FVector2D(1.12f, 1.28f);

	/** 적 곰팡이의 추격 가속도(cm/s²)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|AI", meta = (ClampMin = "100.0", ClampMax = "10000.0"))
	float ChaseAcceleration = 600.0f;

	/** 적 곰팡이의 최대 추격 속력(cm/s)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|AI", meta = (ClampMin = "100.0", ClampMax = "5000.0"))
	float MaxChaseSpeedCmPerSec = 480.0f;

	/** 플레이어 이동 방향을 앞질러 추격할 예측 시간(초)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|AI", meta = (ClampMin = "0.0", ClampMax = "2.5"))
	float LeadPursuitSeconds = 0.55f;

	/** 동시에 플레이어에게 근접 공격(돌진)할 수 있는 최대 포식자 수(공격 티켓 수)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|AI", meta = (ClampMin = "1", ClampMax = "10"))
	int32 MaxAttackingEnemies = 2;

	/** 기존 티켓 보유자가 비보유자보다 이 거리(cm) 이상 멀어졌을 때만 티켓을 교체합니다(경계 진동 방지). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|AI", meta = (ClampMin = "0.0", ClampMax = "2000.0"))
	float TicketSwapHysteresisCm = 350.0f;

	/** 위협→사냥 가능 경계에서 티켓을 반납하기 전 연속 비위협 상태를 유지해야 하는 유예 시간(초)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|AI", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float TicketNonThreatGraceSeconds = 0.5f;

	/** 공격 티켓이 없는 대기 포식자가 유지하는 최소 표면 거리(cm)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|AI", meta = (ClampMin = "200.0", ClampMax = "5000.0"))
	float StandoffSurfaceDistanceMinCm = 950.0f;

	/** 공격 티켓이 없는 대기 포식자가 유지하는 최대 표면 거리(cm)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|AI", meta = (ClampMin = "300.0", ClampMax = "6000.0"))
	float StandoffSurfaceDistanceMaxCm = 1450.0f;

	/** 대기 포위 링 구간에서 선회 이동할 때의 최대 추격 속력 대비 비율입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|AI", meta = (ClampMin = "0.1", ClampMax = "1.5"))
	float StandoffOrbitSpeedRatio = 0.65f;

	/** 모든 포식자(공격 티켓 보유자·대기 포식자 공통)가 서로 유지하려는 최소 표면 이격 거리(cm)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|AI", meta = (ClampMin = "0.0", ClampMax = "3000.0"))
	float StandoffSeparationRadiusCm = 550.0f;

	/** 포식자 간 최소 거리 유지 반발 가속 가중치입니다(클수록 서로 뭉치지 않고 강하게 벌어집니다). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|AI", meta = (ClampMin = "0.0", ClampMax = "6.0"))
	float EnemySeparationWeight = 2.2f;

	/** 적과 플레이어 표면 사이 거리가 이 값(cm) 이하일 때 접촉 흡수가 발동합니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|Combat", meta = (ClampMin = "0.0", ClampMax = "200.0"))
	float ContactAbsorbMarginCm = 35.0f;

	/** 접촉 흡수 중 초당 플레이어에게서 빼앗는 생체량 비율(플레이어 가용 생체량 대비 %/s)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|Combat", meta = (ClampMin = "5.0", ClampMax = "200.0"))
	float ContactDrainUsablePercentPerSec = 28.0f;

	/** 적이 역포식으로 처치되었을 때 호출됩니다. */
	void NotifyEnemyDefeated(ASpaceEnemyMold* DefeatedEnemy);

	/** HUD 개별 마커용: 현재 활성 상태인 모든 포식자의 위치·반지름·거리·위협/사냥가능 여부를 반환합니다. */
	void GatherAllThreatInfos(TArray<FEnemyThreatMarkerInfo>& OutMarkers) const;

	/** 현재 활성 상태인 적군 곰팡이 목록을 반환합니다(V2 그물 속박 판정용). */
	void GatherActiveEnemies(TArray<ASpaceEnemyMold*>& OutEnemies) const;

	/** 현재 활성 적 수와 현재 시점 목표 동시 최대 적 수를 반환합니다(HUD 표시용). */
	int32 GetActiveEnemyCount() const;
	int32 GetCurrentTargetActiveEnemyCount(const ASpaceDrifterPawn* PlayerPawn) const;

	/** 해당 적이 현재 플레이어에게 위협(사냥 불가 → 피격 가능)인지 단일 진실 기준으로 판정합니다. */
	bool IsEnemyThreatToPlayer(const ASpaceEnemyMold* Enemy, const ASpaceDrifterPawn* PlayerPawn) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

private:
	struct FAttackTicketEntry
	{
		TWeakObjectPtr<ASpaceEnemyMold> Enemy;
		float NonThreatElapsed = 0.0f;
	};

	void PrewarmPool();
	void ScheduleNextSpawn(float DelaySeconds, bool bForceReset = false);
	void OnSpawnTimerExpired();
	bool TrySpawnOneEnemy(ASpaceDrifterPawn* PlayerPawn);
	void UpdateAttackTickets(const ASpaceDrifterPawn* PlayerPawn, const FVector& PlayerLoc, float DeltaTime);
	bool HasAttackTicket(const ASpaceEnemyMold* Enemy) const;
	ASpaceDrifterPawn* ResolvePlayerPawn() const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ASpaceEnemyMold>> EnemyPool;

	TArray<FAttackTicketEntry> AttackTicketHolders;
	mutable TWeakObjectPtr<ASpaceDrifterPawn> CachedPlayerPawn;
	FTimerHandle SpawnTimerHandle;
	bool bInitialSpawnScheduled = false;
	bool bHasSpawnedFirstEnemy = false;
	double LastDrainBannerTime = -10.0;
};
