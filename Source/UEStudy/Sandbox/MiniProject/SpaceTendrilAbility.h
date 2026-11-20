// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Engine/EngineTypes.h"
#include "SpaceTendrilHostInterface.h"
#include "SpaceTendrilAbility.generated.h"

class AActor;
class UPrimitiveComponent;
class UStaticMeshComponent;
class UFXSystemComponent;
class ASpaceMineral;
class ASpaceEnemyMold;
class AEnemyMoldManager;

/** 촉수 능력의 런타임 상태 머신입니다. Idle 상태일 때는 Tick이 호출되지 않습니다(0-Tick). */
UENUM(BlueprintType)
enum class ETendrilAbilityState : uint8
{
	Idle UMETA(DisplayName = "Idle (휴면 / 0-Tick)"),
	Extending UMETA(DisplayName = "Extending (전개/분사 중)"),
	Retracting UMETA(DisplayName = "Retracting (회수 중)")
};

/**
 * [언리얼 표준 Ability 아키텍처]
 * 모든 촉수 능력(1번 긴 촉수, 2번 그물 촉수, 3번 이동용 분사 촉수)의 추상 베이스 클래스입니다.
 * - UActorComponent의 상시 FTickTaskManager 등록 오버헤드 없이, EditInlineNew UObject로 경량화되었습니다.
 * - 평상시(Idle)에는 Tick이 전혀 호출되지 않으며(0-Tick), 발동(Extending/Retracting) 중에만 구동됩니다.
 * - 유지 시간 제한은 매 프레임 폴링 대신 FTimerManager 원샷 타이머 이벤트로 처리합니다.
 */
UCLASS(Abstract, BlueprintType, Blueprintable, EditInlineNew, DefaultToInstanced)
class UESTUDY_API USpaceTendrilAbility : public UObject
{
	GENERATED_BODY()

public:
	USpaceTendrilAbility();

	/** 호스트 인터페이스를 바인딩하고 능력 초기 상태를 설정합니다. */
	virtual void InitializeAbility(TScriptInterface<ISpaceTendrilHostInterface> InHost);

	/** 이 능력이 담당하는 촉수 스타일 식별자를 반환합니다. */
	virtual ETendrilVisualStyle GetAbilityStyle() const PURE_VIRTUAL(USpaceTendrilAbility::GetAbilityStyle, return ETendrilVisualStyle::MyceliumWeb;);

	/** 화면 디버그/UI에 표시할 능력 이름입니다. */
	virtual FString GetAbilityDisplayName() const PURE_VIRTUAL(USpaceTendrilAbility::GetAbilityDisplayName, return TEXT("Tendril Ability"););

	/** 홀드형(키를 떼면 즉시 회수)인지 단발형(키를 떼도 타이머까지 유지)인지 여부입니다. */
	virtual bool IsHoldToMaintainAbility() const { return true; }

	/** 현재 능력을 새로 발동할 수 있는지 검사합니다(쿨타임·자원·회수 완료 여부). */
	virtual bool CanActivateAbility() const;

	/** 스킬 재사용 대기시간(초)입니다(0이면 쿨타임 없음). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tendril|Cooldown", meta = (ClampMin = "0.0", ClampMax = "60.0"))
	float CooldownSeconds = 0.0f;

	/** 현재 쿨타임 대기 중인지 반환합니다. */
	bool IsOnCooldown() const;

	/** 남은 쿨타임(초)을 반환합니다(준비 완료 시 0). */
	float GetCooldownRemainingSeconds() const;

	/** 입력 시작(Started) 이벤트 핸들러입니다. */
	virtual bool OnInputStarted();

	/** 입력 종료(Completed/Canceled) 이벤트 핸들러입니다. */
	virtual void OnInputEnded();

	/** 능력 전환 또는 강제 회수 시 호출됩니다. */
	virtual void BeginRetract(bool bAllowRetractImpulse = true);

	/** 탭 전환이나 EndPlay 시 즉시 모든 시각·물리 상태를 정리하고 Idle로 복귀합니다. */
	virtual void DeactivateAbilityImmediate();

	/**
	 * 오직 IsAbilityTickActive() == true 일 때만 호스트에서 호출되는 조건부 Tick입니다.
	 * Idle 상태일 때는 호출되지 않습니다.
	 */
	virtual void TickActiveAbility(float DeltaTime);

	/** 호스트 본체가 외부 액터와 물리 충돌했을 때 전달되는 이벤트 훅입니다. */
	virtual void OnHostCollisionHit(AActor* OtherActor, UPrimitiveComponent* OtherComp, const FVector& NormalImpulse, const FHitResult& Hit) {}

	/** 현재 이 능력이 Tick 구동이 필요한 활성 상태인지 반환합니다. */
	FORCEINLINE bool IsAbilityTickActive() const
	{
		return AbilityState != ETendrilAbilityState::Idle || ExtendAlpha > KINDA_SMALL_NUMBER || HasPendingActiveWork();
	}

	FORCEINLINE ETendrilAbilityState GetAbilityState() const { return AbilityState; }
	FORCEINLINE bool IsExtending() const { return AbilityState == ETendrilAbilityState::Extending; }
	FORCEINLINE float GetExtendAlpha() const { return ExtendAlpha; }
	FORCEINLINE FVector GetLockedAimDir() const { return LockedAimDir; }
	FORCEINLINE FVector GetLastTendrilDir() const { return LastTendrilDir; }

	/** 현재 물리 락온된 대상 액터가 있으면 반환합니다(소행성 디스폰 유예 보호용). */
	virtual AActor* GetLatchedTargetActor() const { return nullptr; }

	/** 현재 본체 크기가 반영된 촉수의 최대 유효 사거리(cm)를 반환합니다. */
	FORCEINLINE float GetMaxEffectiveReachCm() const { return GetEffectiveReachCm(); }

	/** 현재 능력이 호스트 본체에 요구하는 시각적 스트레치 비율(0~1)과 방향을 반환합니다. */
	virtual float EvaluateHostBodyStretch(float ElapsedTime, FVector& OutStretchDir) const;

	/** 공통 파라미터(속도·인셋·스트레치·회수 반동)를 호스트 설정과 동기화합니다. */
	void SyncCommonParameters(float InExtendSpeed, float InRetractSpeed, float InRootInset, float InBodyStretch, float InRetractImpulse);

protected:
	/** 파생 클래스에서 발사 순간 조준점·초기 상태를 설정할 때 오버라이드합니다. */
	virtual void OnAbilityActivated() {}

	/** 파생 클래스에서 매 활성 프레임 고유 물리/이펙트 로직을 수행할 때 오버라이드합니다. */
	virtual void OnActiveTick(float DeltaTime, const FVector& VisualOrigin, const FVector& StartPos, const FVector& EndPos, float EasedReach) {}

	/** 파생 클래스에서 회수가 완전히 끝나 Idle로 돌아갈 때 호출됩니다. */
	virtual void OnAbilityReturnedToIdle() {}

	/** 발사체 잔상 등 ExtendAlpha가 0이 된 뒤에도 잠시 추가 업데이트가 필요한 경우 true를 반환합니다. */
	virtual bool HasPendingActiveWork() const { return false; }

	/** 이번 발사에 적용할 최대 유지 시간(초)을 반환합니다. 0 이하면 무제한(홀드 유지)입니다. */
	virtual float GetHoldDurationLimitSeconds() const { return -1.0f; }

	/** 이 능력의 최대 도달 사거리(cm)를 반환합니다. */
	virtual float GetEffectiveReachCm() const { return 1000.0f; }

	/** 카메라 기준 조준 방향과 직교 기저(Right/Up)를 계산해 잠급니다. */
	virtual void LatchCameraAimBasis();

	/** FTimerManager 기반 최대 유지 시간 타이머를 무장하거나 해제합니다. */
	void ArmHoldLimitTimer(float DurationSeconds);
	void ClearHoldLimitTimer();
	virtual void OnHoldLimitTimerExpired();
	void StartCooldown();

	UPROPERTY(Transient)
	TScriptInterface<ISpaceTendrilHostInterface> Host;

	ETendrilAbilityState AbilityState = ETendrilAbilityState::Idle;
	FTimerHandle HoldLimitTimerHandle;
	double CooldownReadyWorldTime = -1.0;

	float ExtendSpeed = 2.8f;
	float RetractSpeed = 5.2f;
	float RootInsetCm = 2.0f;
	float BodyStretchRatio = 0.34f;
	float RetractImpulseStrength = 7.2f;

	float ExtendAlpha = 0.0f;
	FVector LockedAimTarget = FVector::ForwardVector * 1800.0f;
	FVector LockedAimDir = FVector::ForwardVector;
	FVector LockedAimRight = FVector::RightVector;
	FVector LockedAimUp = FVector::UpVector;
	FVector LastTendrilDir = FVector::ForwardVector;
	bool bForceRestartFX = false;
};

/**
 * [V1: 1번 긴 촉수 (ULongTendrilAbility)]
 * - 카메라 조준 레이캐스트/스피어스윕으로 소행성 표면 로컬 앵커에 락온합니다.
 * - 뉴턴 작용·반작용 인력(AddForce)과 편심 토크(AddForceAtLocation)로 소행성과 플레이어를 서로 끌어당깁니다.
 */
UCLASS(BlueprintType, Blueprintable, EditInlineNew, DefaultToInstanced)
class UESTUDY_API ULongTendrilAbility : public USpaceTendrilAbility
{
	GENERATED_BODY()

public:
	virtual ETendrilVisualStyle GetAbilityStyle() const override { return ETendrilVisualStyle::MyceliumWeb; }
	virtual FString GetAbilityDisplayName() const override { return TEXT("Tendril Style: V1 긴 촉수 (Long Tendril / Hold)"); }
	virtual bool IsHoldToMaintainAbility() const override { return true; }

	virtual void DeactivateAbilityImmediate() override;
	virtual bool CanActivateAbility() const override;
	virtual void OnHostCollisionHit(AActor* OtherActor, UPrimitiveComponent* OtherComp, const FVector& NormalImpulse, const FHitResult& Hit) override;
	virtual AActor* GetLatchedTargetActor() const override { return LatchedTargetActor.Get(); }

	FORCEINLINE float GetInvestedBiomassOnTarget() const { return InvestedBiomassOnTarget; }

	int32 ComputeBranchCountForLength(float TargetTrunkLenCm) const;
	int32 ComputeCurrentBranchCount() const;
	void PushLongTendrilParametersToFX(UFXSystemComponent* TargetFX);

	/** 긴 촉수(V1) 본 가지 최대 사거리(cm)입니다. (실행 시 폰의 LongTendrilMaxLength 값으로 자동 동기화됨) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LongTendril")
	float MaxLengthCm = 1800.0f;

	/** 긴 촉수(V1) 본 가지 뿌리 쪽 시작 굵기(cm)입니다. (실행 시 폰의 LongTendrilTrunkWidth 값으로 자동 동기화됨) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LongTendril")
	float TrunkWidthCm = 11.5f;

	/** 긴 촉수(V1) 뿌리 대비 끝단 굵기 테이퍼 비율입니다. (실행 시 폰의 LongTendrilTipWidthRatio 값으로 자동 동기화됨) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LongTendril")
	float TipWidthRatio = 0.08f;

	/** 긴 촉수(V1) 본 가지 1m당 생성되는 곁가지 빈도(개/m)입니다. (실행 시 폰의 LongTendrilBranchFrequency 값으로 자동 동기화됨) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LongTendril")
	float BranchFrequency = 0.70f;

	/** 곁가지가 생성되기 시작하는 최소 본 가지 길이(cm)입니다. (실행 시 폰의 LongTendrilBranchMinTrunkCm 값으로 자동 동기화됨) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LongTendril")
	float BranchMinTrunkCm = 420.0f;

	/** 긴 촉수(V1) 끝단 부근 곁가지의 최대 기준 길이(cm)입니다. (실행 시 폰의 LongTendrilBranchLength 값으로 자동 동기화됨) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LongTendril")
	float BranchLengthCm = 180.0f;

	/** 긴 촉수(V1) 곁가지의 시작 굵기(cm)입니다. (실행 시 폰의 LongTendrilBranchWidth 값으로 자동 동기화됨) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LongTendril")
	float BranchWidthCm = 4.8f;

	/** 끝단 대비 뿌리쪽 곁가지의 길이·굵기·퍼짐 비율입니다. (실행 시 폰의 LongTendrilBaseToTipLengthRatio 값으로 자동 동기화됨) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LongTendril")
	float BaseToTipLengthRatio = 0.35f;

	/** 본 가지 전체 길이 중 곁가지가 처음 돋아나는 시작 위치 비율(0.02~0.90)입니다. (실행 시 폰의 LongTendrilBranchStartRatio 값으로 자동 동기화됨) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LongTendril")
	float BranchStartRatio = 0.22f;

	/** 곁가지들이 옆으로 퍼지는 각도·폭 배율입니다. (실행 시 폰의 LongTendrilBranchSpreadScale 값으로 자동 동기화됨) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LongTendril")
	float BranchSpreadScale = 1.0f;

	/** 긴 촉수(V1) 최대 길이 도달 후 유지 시간 제한 사용 여부입니다. (실행 시 폰의 bLimitLongTendrilHoldDuration 값으로 자동 동기화됨) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LongTendril")
	bool bLimitHoldDuration = true;

	/** 긴 촉수(V1) 최대 길이 상태로 유지할 수 있는 최대 시간(초)입니다. (실행 시 폰의 LongTendrilMaxHoldDuration 값으로 자동 동기화됨) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LongTendril")
	float MaxHoldDurationSeconds = 1.5f;

	/** 소행성·포식자에 꽂혀 있는 동안에도 최대 유지 시간 제한을 적용할지 여부입니다. (실행 시 폰의 bLimitHoldDurationWhileLatched 값으로 자동 동기화됨) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LongTendril")
	bool bLimitHoldWhileLatched = false;

	/** 카메라 중앙 조준 레이 주변을 포착하는 스피어 트레이스 반경(cm)입니다. (실행 시 폰의 TendrilAimTraceRadius 값으로 자동 동기화됨) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LongTendril")
	float AimTraceRadiusCm = 14.0f;

	/** 락온 대상과 플레이어를 서로 끌어당기는 기본 인력 세기(Chaos Force)입니다. (실행 시 폰의 TendrilPullForce 값으로 자동 동기화됨) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LongTendril")
	float PullForce = 240000.0f;

	/** 촉수로 끌어당길 때 대상과의 기본 최대 상대 접근 속력(cm/s)입니다. (실행 시 폰의 MaxRelativePullSpeed 값으로 자동 동기화됨) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LongTendril")
	float MaxRelativePullSpeedCmPerSec = 1100.0f;

	/** 꽂힌 표면 지점의 편심 회전 토크를 소행성에 전달하는 비율(0~1)입니다. (실행 시 폰의 TendrilTorqueTransferRatio 값으로 자동 동기화됨) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LongTendril")
	float TorqueTransferRatio = 0.25f;

	/** 촉수가 목표 표면까지 뻗어나가 인력이 발동되기 시작하는 진행도(0~1) 임계값입니다. (실행 시 폰의 TendrilPullActivationAlpha 값으로 자동 동기화됨) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LongTendril")
	float PullActivationAlpha = 0.85f;

	/** 끌어당기는 중 락온된 대상과 물리 충돌했을 때 즉시 촉수를 해제할지 여부입니다. (실행 시 폰의 bAutoDetachTendrilOnTargetHit 값으로 자동 동기화됨) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LongTendril")
	bool bAutoDetachOnTargetHit = false;

	/** 락온 상태에서 유효 사거리 대비 몇 배까지 벌어지면 촉수가 끊어질지 결정하는 배율입니다. (실행 시 폰의 MaxTetherBreakDistanceMultiplier 값으로 자동 동기화됨) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LongTendril")
	float MaxTetherBreakMultiplier = 1.45f;

	/** 소행성 근처로 당겨질 때 위성처럼 빙글빙글 공전하지 않도록 접선 속도를 억제하는 최대 감쇠 강도입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LongTendril", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float CloseRangeTangentialDamping = 10.0f;

	/** 락온 직후 원거리에서도 공전을 서서히 줄여주는 기본 접선 감쇠 강도입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LongTendril", meta = (ClampMin = "0.0", ClampMax = "15.0"))
	float BaseTangentialDamping = 2.0f;

	/** 전염 트리거 거리 대비 몇 배 거리 이내부터 접선 공전 감쇠를 최대로 끌어올릴지 결정하는 배율입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LongTendril", meta = (ClampMin = "1.0", ClampMax = "10.0"))
	float CloseRangeDampingRadiusMultiplier = 3.5f;

	/** 원심력을 압도하고 플레이어를 소행성 쪽으로 확실히 끌어당기는 최소 구심 가속도(cm/s²)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LongTendril", meta = (ClampMin = "500.0", ClampMax = "20000.0"))
	float MinInwardPullAccelCmPerSec2 = 3400.0f;

	/** 근접 시 원심력을 이기고 확실히 맞붙도록 인력을 증폭하는 최대 배율입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LongTendril", meta = (ClampMin = "1.0", ClampMax = "5.0"))
	float CloseRangePullBoostMultiplier = 1.65f;

	/** 본체 크기 배율(BodyScale)이 증가할 때 V1 사거리가 비례해 늘어나는 지수입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LongTendril|Colonization", meta = (ClampMin = "0.0", ClampMax = "2.5"))
	float SizeScaleReachExponent = 1.0f;

	/** V1 유효 사거리(길이) 배율에 비례해 끌어당기는 힘·최소 구심 가속도가 커지는 지수입니다(1.0 = 길이와 정비례). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LongTendril", meta = (ClampMin = "0.0", ClampMax = "2.5"))
	float PullForceLengthExponent = 1.0f;

	/** V1 유효 사거리(길이) 배율에 비례해 끌어당김 최대 상대 속도가 커지는 지수입니다(1.0 = 길이와 정비례). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LongTendril", meta = (ClampMin = "0.0", ClampMax = "2.5"))
	float PullSpeedLengthExponent = 1.0f;

	/** V1 락온 중 플레이어와 소행성 표면 간 거리가 이 값(cm) 이하일 때 전염(몸 뒤덮기)이 시작됩니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LongTendril|Colonization", meta = (ClampMin = "10.0", ClampMax = "1000.0"))
	float ColonizeTriggerSurfaceDistanceCm = 140.0f;

	/** 전염 중 초당 소행성 표면으로 전이되는 생체량 비율(가용 생체량 대비 %/s)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LongTendril|Colonization", meta = (ClampMin = "5.0", ClampMax = "500.0"))
	float ColonizeUsablePercentPerSec = 50.0f;

	/** 전염 완료("곧 폭발합니다!") 트리거 발동 직후 V1 긴 촉수 재발사를 조용히 차단하는 잠금 시간(초)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LongTendril|Colonization", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float PostColonizationLockoutSeconds = 1.4f;

protected:
	virtual void OnAbilityActivated() override;
	virtual void OnActiveTick(float DeltaTime, const FVector& VisualOrigin, const FVector& StartPos, const FVector& EndPos, float EasedReach) override;
	virtual void OnAbilityReturnedToIdle() override;
	virtual float GetHoldDurationLimitSeconds() const override;
	virtual float GetEffectiveReachCm() const override;

private:
	FVector TraceAimTarget(const FVector& RootOrigin, AActor*& OutHitActor, UPrimitiveComponent*& OutHitComp, FVector& OutHitImpactPoint) const;
	bool TryLatchAlongActiveSegment(const FVector& SegmentStart, const FVector& SegmentEnd);
	void ApplyTetherPullForces(const FVector& VisualOrigin, const FVector& TargetAnchorWorld);
	void UpdateTargetColonization(float DeltaTime);
	void RefundAndCancelColonization();
	void DetachLatchedTarget();

	bool bHasLatchedTarget = false;
	TWeakObjectPtr<AActor> LatchedTargetActor;
	TWeakObjectPtr<UPrimitiveComponent> LatchedTargetComponent;
	FVector LatchedLocalAnchor = FVector::ZeroVector;
	float InvestedBiomassOnTarget = 0.0f;

	float LockedInitialTargetLenCm = 1800.0f;
	float PeakTrunkLengthCm = 0.0f;
	float TrunkRetractOffsetCm = 0.0f;
	int32 ActiveShotBranchCount = 13;
	int32 LastPushedSideBranchCount = -1;
	float ActiveShotBranchLenScale = 1.0f;
	bool bIsReelingIn = false;
	double PostColonizeLockoutEndTime = -1.0;
};

/**
 * [V2: 2번 그물 촉수 (UNetTendrilAbility)]
 * - 단발(One-Shot) 발사 후 FTimerManager 타이머로 일정 시간 전개 상태를 유지한 뒤 자동 회수됩니다.
 * - TActorIterator 전수 순회 대신 AAsteroidFieldManager의 활성 미네랄 목록만 조회해 깔때기 범위 내 광석을 흡착·회수합니다.
 */
UCLASS(BlueprintType, Blueprintable, EditInlineNew, DefaultToInstanced)
class UESTUDY_API UNetTendrilAbility : public USpaceTendrilAbility
{
	GENERATED_BODY()

public:
	UNetTendrilAbility();

	virtual ETendrilVisualStyle GetAbilityStyle() const override { return ETendrilVisualStyle::GossamerSpores; }
	virtual FString GetAbilityDisplayName() const override { return TEXT("Tendril Style: V2 그물 촉수 (Net Tendril / One-Shot)"); }
	virtual bool IsHoldToMaintainAbility() const override { return false; }

	/** 그물 촉수(V2)가 한 번 발사되었을 때 최대로 펼쳐지는 기본 거리(cm)입니다. (실행 시 폰의 NetTendrilMaxReach 값으로 자동 동기화됨) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NetTendril")
	float MaxReachCm = 920.0f;

	/** 그물 촉수(V2)가 회수될 때 되감기는 정규화 속도(1/s)입니다. (실행 시 폰의 NetTendrilRetractSpeed 값으로 자동 동기화됨) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NetTendril")
	float NetRetractSpeed = 2.6f;

	/** 그물 촉수(V2) 발사 후 최대 전개 상태를 유지하는 시간(초)입니다. (실행 시 폰의 NetTendrilHoldDuration 값으로 자동 동기화됨) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NetTendril")
	float HoldDurationSeconds = 0.55f;

	/** 그물 깔때기 반경 바깥쪽으로 미네랄 포획을 허용하는 여유 반경(cm)입니다. (실행 시 폰의 NetCapturePaddingCm 값으로 자동 동기화됨) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NetTendril")
	float CapturePaddingCm = 24.0f;

	/** 본체 구체 반경 바깥쪽으로 직접 접촉 흡수를 허용하는 여유 거리(cm)입니다. (실행 시 폰의 직접 접촉 흡수 설정으로 자동 계산됨) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NetTendril")
	float DirectContactMarginCm = 24.0f;

	/** 본체 크기 배율에 따라 V2 그물 사거리가 커지는 지수입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NetTendril|Growth", meta = (ClampMin = "0.0", ClampMax = "2.5"))
	float SizeScaleReachExponent = 1.0f;

	/** 본체 크기 배율에 따라 V2 그물 깔때기 포획 반경이 커지는 지수입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NetTendril|Growth", meta = (ClampMin = "0.0", ClampMax = "2.5"))
	float SizeScaleFunnelRadiusExponent = 1.0f;

	/** V2 그물이 포식자 곰팡이에 적중했을 때 이동속도를 둔화시키는 시간(초)입니다(쿨타임보다 짧아야 함). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NetTendril|Slow", meta = (ClampMin = "0.2", ClampMax = "15.0"))
	float WebSlowDurationSeconds = 2.5f;

	/** V2 그물 둔화 시 포식자 이동속도 배율입니다(0.35 = 35% 속도). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NetTendril|Slow", meta = (ClampMin = "0.1", ClampMax = "0.95"))
	float WebSlowSpeedMultiplier = 0.35f;

	/** V2 광역 포식자 속박 판정의 근거리 최소 반경(cm, 본체 크기 배율 비례)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NetTendril|Slow", meta = (ClampMin = "100.0", ClampMax = "3000.0"))
	float WebAoEMinRadiusCm = 420.0f;

	/** V2 광역 포식자 속박 판정의 그물 끝단 최대 반경(cm, 본체 크기 배율 비례)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NetTendril|Slow", meta = (ClampMin = "200.0", ClampMax = "5000.0"))
	float WebAoEMaxRadiusCm = 900.0f;

	/** V2 광역 적중 시 포식자 반지름을 영구 감소시키는 비율(%)입니다(생체량도 연동 감소). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NetTendril|Slow", meta = (ClampMin = "0.0", ClampMax = "50.0"))
	float WebShrinkPercentPerHit = 15.0f;

	/** 그물 촉수(V2) 유지 시간 시인성 보정 배율입니다(폰 동기화 HoldDurationSeconds에 곱해져 그물이 충분히 오래 보이도록 합니다). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NetTendril|Visibility", meta = (ClampMin = "0.5", ClampMax = "6.0"))
	float NetVisibilityHoldMultiplier = 2.5f;

	/** 그물 촉수(V2) 최대 전개 상태 최소 유지 시간(초) 하한입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NetTendril|Visibility", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float MinHoldDurationSeconds = 1.35f;

	/** 그물 촉수(V2) 회수 속도 배율입니다(1.0 미만으로 낮춰 그물이 너무 빨리 사라지지 않고 부드럽게 회수되게 합니다). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NetTendril|Visibility", meta = (ClampMin = "0.15", ClampMax = "2.0"))
	float NetRetractSpeedScale = 0.45f;

	/** 그물 촉수(V2) 나이아가라 포자(GossamerSporesFX) 최대 생성률(User.SporeSpawnRate)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NetTendril|Visibility", meta = (ClampMin = "10.0", ClampMax = "400.0"))
	float NetSporeSpawnRate = 95.0f;

	/** V2 광역 축소로 줄어들 수 있는 포식자 최소 반지름 하한(초기 스폰 반지름 대비 비율)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NetTendril|Slow", meta = (ClampMin = "0.25", ClampMax = "0.95"))
	float WebMinShrinkRadiusRatio = 0.50f;

protected:
	virtual void LatchCameraAimBasis() override;
	virtual void OnAbilityActivated() override;
	virtual void OnActiveTick(float DeltaTime, const FVector& VisualOrigin, const FVector& StartPos, const FVector& EndPos, float EasedReach) override;
	virtual void OnAbilityReturnedToIdle() override;
	virtual float GetHoldDurationLimitSeconds() const override;
	virtual float GetEffectiveReachCm() const override;

private:
	FVector EvaluateFunnelCenter(const FVector& StartPos, float MaxNetLenCm, float NormalizedT, float TimeSec) const;
	float EvaluateFunnelRadiusCm(float NormalizedT, float CurrentReachAlpha) const;

	TWeakObjectPtr<AEnemyMoldManager> CachedEnemyManager;
	TSet<TWeakObjectPtr<ASpaceEnemyMold>> SlowedEnemiesThisShot;
};

/**
 * [V3: 3번 이동용 분사 촉수 (UJetTendrilAbility)]
 * - 스페이스바를 누르는 동안 언리얼 내장 나이아가라(NS_Tendril_SporeJet: Local Space + Mesh Renderer SM_DustPuffSphere/MI_DustPuff)로
 *   본체 일부를 후방(-LockedAimDir) 원뿔로 분사하며, 카메라가 바라보는 전방(+LockedAimDir)으로 지속 추진 가속도를 부여합니다.
 * - [확장 포인트]: 추후 무한 분사 방지(미네랄/에너지 소모, 과열 쿨타임 등) 규칙을 추가할 때는
 *   CanConsumeJetResource()와 ConsumeJetResource() 두 함수만 수정하면 호스트 코드 변경 없이 즉시 적용됩니다.
 */
UCLASS(BlueprintType, Blueprintable, EditInlineNew, DefaultToInstanced)
class UESTUDY_API UJetTendrilAbility : public USpaceTendrilAbility
{
	GENERATED_BODY()

public:
	UJetTendrilAbility();

	virtual ETendrilVisualStyle GetAbilityStyle() const override { return ETendrilVisualStyle::SporeJet; }
	virtual FString GetAbilityDisplayName() const override { return TEXT("Tendril Style: V3 긴급 탈출 분사 (Jet Dash / One-Shot)"); }
	virtual bool IsHoldToMaintainAbility() const override { return false; }

	virtual bool CanActivateAbility() const override;
	virtual float EvaluateHostBodyStretch(float ElapsedTime, FVector& OutStretchDir) const override;

	/** 단발 긴급 탈출 시 즉각 부여되는 전방(+LockedAimDir) 탈출 속도 변화(cm/s)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JetTendril", meta = (ClampMin = "200.0", ClampMax = "5000.0"))
	float EmergencyDashImpulseSpeedCmPerSec = 1400.0f;

	/** 단발 분사 이펙트 유지 시간(초)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JetTendril", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float BurstDurationSeconds = 0.35f;

	/** 단발 긴급 탈출 1회 발동 시 소모되는 가용 생체량 비율(%)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JetTendril|Biomass", meta = (ClampMin = "0.0", ClampMax = "50.0"))
	float DashUsableBiomassPercent = 8.0f;

	/** 단발 긴급 탈출 1회 발동 시 최소 소모 생체량입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JetTendril|Biomass", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float DashMinBiomassCost = 4.0f;

	/** V3 지속 분사 시 기본 추진 가속도(cm/s²)입니다(레거시 호환용, 실행 시 폰의 JetTendrilThrustAccel 값으로 자동 동기화됨). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JetTendril")
	float ThrustAccelerationCmPerSec2 = 1400.0f;

	/** 현재 속력이 빨라질수록 가속도가 반비례(`1 / (1 + Ratio * (Speed / RefSpeed))`)로 줄어드는 기준 속력(cm/s)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JetTendril", meta = (ClampMin = "100.0"))
	float SpeedDampingReferenceCmPerSec = 900.0f;

	/** 속력 반비례 가속도 감쇠 강도 비율입니다. 0이면 속도 무관 일정 가속도, 값이 클수록 고속에서 속력이 거의 오르지 않습니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JetTendril", meta = (ClampMin = "0.0"))
	float SpeedInverseDecayRatio = 1.2f;

	/** V3 분사 나이아가라 이펙트의 기준 길이(cm)입니다. (실행 시 폰의 JetTendrilVisualLength 값으로 자동 동기화됨) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JetTendril")
	float VisualReachCm = 720.0f;

	/** V3 분사 시 초당 포자 입자 생성 배율입니다. (실행 시 폰의 JetTendrilSpawnRate 값으로 자동 동기화됨) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JetTendril")
	float ProjectileSpawnRateScale = 160.0f;

	/** V3 분사 시 초당 영구 소모되는 생체량(Biomass/s)입니다(레거시 호환용). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JetTendril|Biomass", meta = (ClampMin = "0.0", ClampMax = "200.0"))
	float JetBiomassCostPerSecond = 8.0f;

	/** 내 본체 크기(BodyScale) 대비 V3 분사 입자 크기 비율(`내 크기 : 분사 크기`)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JetTendril|Biomass", meta = (ClampMin = "0.05", ClampMax = "3.0"))
	float JetParticleToBodySizeRatio = 0.45f;

protected:
	virtual void OnAbilityActivated() override;
	virtual void OnActiveTick(float DeltaTime, const FVector& VisualOrigin, const FVector& StartPos, const FVector& EndPos, float EasedReach) override;
	virtual void OnAbilityReturnedToIdle() override;
	virtual float GetHoldDurationLimitSeconds() const override { return FMath::Max(BurstDurationSeconds, 0.1f); }
	virtual float GetEffectiveReachCm() const override { return FMath::Clamp(VisualReachCm, 120.0f, 10000.0f); }

	virtual bool CanConsumeJetResource() const;
	float ComputeDashBiomassCost() const;
	virtual bool ConsumeJetResource(float DeltaTime);
};
