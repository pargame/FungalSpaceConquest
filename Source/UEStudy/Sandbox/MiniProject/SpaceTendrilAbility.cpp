// Copyright Epic Games, Inc. All Rights Reserved.

#include "SpaceTendrilAbility.h"

#include "AsteroidFieldManager.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnemyMoldManager.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FXSystemComponent.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "SpaceAsteroid.h"
#include "SpaceColonizableTarget.h"
#include "SpaceDrifterPawn.h"
#include "SpaceEnemyMold.h"
#include "SpaceMineral.h"
#include "TimerManager.h"

USpaceTendrilAbility::USpaceTendrilAbility()
{
}

void USpaceTendrilAbility::InitializeAbility(TScriptInterface<ISpaceTendrilHostInterface> InHost)
{
	Host = InHost;
	DeactivateAbilityImmediate();
}

void USpaceTendrilAbility::SyncCommonParameters(
	float InExtendSpeed,
	float InRetractSpeed,
	float InRootInset,
	float InBodyStretch,
	float InRetractImpulse)
{
	ExtendSpeed = FMath::Max(InExtendSpeed, 0.2f);
	RetractSpeed = FMath::Max(InRetractSpeed, 0.2f);
	RootInsetCm = FMath::Max(InRootInset, 0.0f);
	BodyStretchRatio = FMath::Clamp(InBodyStretch, 0.0f, 0.5f);
	RetractImpulseStrength = FMath::Max(InRetractImpulse, 0.0f);
}

bool USpaceTendrilAbility::IsOnCooldown() const
{
	return GetCooldownRemainingSeconds() > KINDA_SMALL_NUMBER;
}

float USpaceTendrilAbility::GetCooldownRemainingSeconds() const
{
	if (CooldownReadyWorldTime <= 0.0)
	{
		return 0.0f;
	}
	const ISpaceTendrilHostInterface* HostIfc = Host.GetInterface();
	const UWorld* World = HostIfc ? HostIfc->GetTendrilWorld() : nullptr;
	if (!World)
	{
		return 0.0f;
	}
	return static_cast<float>(FMath::Max(CooldownReadyWorldTime - World->GetTimeSeconds(), 0.0));
}

void USpaceTendrilAbility::StartCooldown()
{
	if (CooldownSeconds <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	if (const ISpaceTendrilHostInterface* HostIfc = Host.GetInterface())
	{
		if (const UWorld* World = HostIfc->GetTendrilWorld())
		{
			CooldownReadyWorldTime = World->GetTimeSeconds() + static_cast<double>(CooldownSeconds);
		}
	}
}

bool USpaceTendrilAbility::CanActivateAbility() const
{
	return AbilityState == ETendrilAbilityState::Idle
		&& ExtendAlpha <= KINDA_SMALL_NUMBER
		&& Host.GetInterface() != nullptr
		&& !IsOnCooldown();
}

bool USpaceTendrilAbility::OnInputStarted()
{
	if (IsOnCooldown())
	{
		if (ISpaceTendrilHostInterface* HostIfc = Host.GetInterface())
		{
			HostIfc->PostTendrilStatusBanner(
				FString::Printf(TEXT("[쿨타임] %.1f초 후 사용 가능"), GetCooldownRemainingSeconds()),
				FLinearColor(1.0f, 0.72f, 0.28f, 1.0f),
				0.9f
			);
		}
		return false;
	}

	if (!CanActivateAbility())
	{
		if (AbilityState == ETendrilAbilityState::Idle && ExtendAlpha <= KINDA_SMALL_NUMBER && GetAbilityStyle() == ETendrilVisualStyle::SporeJet)
		{
			if (ISpaceTendrilHostInterface* HostIfc = Host.GetInterface())
			{
				HostIfc->PostTendrilStatusBanner(
					TEXT("[V3 분사 불가] 가용 생체량이 부족합니다!"),
					FLinearColor(1.0f, 0.45f, 0.38f, 1.0f),
					1.0f
				);
			}
		}
		return false;
	}

	AbilityState = ETendrilAbilityState::Extending;
	bForceRestartFX = true;
	StartCooldown();

	LatchCameraAimBasis();
	OnAbilityActivated();

	if (ISpaceTendrilHostInterface* HostIfc = Host.GetInterface())
	{
		HostIfc->ApplyHostDeformImpulse(-LockedAimDir * 2.2f);
	}

	return true;
}

void USpaceTendrilAbility::OnInputEnded()
{
	if (!IsHoldToMaintainAbility())
	{
		// 단발형 능력(V2 그물 촉수 등)은 키를 떼도 FTimerManager 타이머 만료 시점까지 전개를 유지합니다.
		return;
	}

	BeginRetract(true);
}

void USpaceTendrilAbility::BeginRetract(bool bAllowRetractImpulse)
{
	if (AbilityState != ETendrilAbilityState::Extending)
	{
		return;
	}

	AbilityState = ETendrilAbilityState::Retracting;
	ClearHoldLimitTimer();

	if (bAllowRetractImpulse && ExtendAlpha > 0.15f)
	{
		if (ISpaceTendrilHostInterface* HostIfc = Host.GetInterface())
		{
			HostIfc->ApplyHostDeformImpulse(LastTendrilDir * (RetractImpulseStrength * FMath::Clamp(ExtendAlpha, 0.35f, 1.0f)));
		}
	}
}

void USpaceTendrilAbility::DeactivateAbilityImmediate()
{
	ClearHoldLimitTimer();
	AbilityState = ETendrilAbilityState::Idle;
	ExtendAlpha = 0.0f;
	bForceRestartFX = false;

	if (ISpaceTendrilHostInterface* HostIfc = Host.GetInterface())
	{
		if (UFXSystemComponent* FX = HostIfc->GetTendrilFXComponent(GetAbilityStyle()))
		{
			FX->SetFloatParameter(TEXT("User.TendrilAlpha"), 0.0f);
			FX->SetFloatParameter(TEXT("User.TendrilReach"), 0.0f);
			FX->SetFloatParameter(TEXT("User.SporeSpawnRate"), 0.0f);
			if (FX->IsActive())
			{
				FX->DeactivateImmediate();
			}
			if (FX->IsVisible())
			{
				FX->SetVisibility(false);
			}
		}
	}
}

void USpaceTendrilAbility::TickActiveAbility(float DeltaTime)
{
	ISpaceTendrilHostInterface* HostIfc = Host.GetInterface();
	if (!HostIfc)
	{
		AbilityState = ETendrilAbilityState::Idle;
		ExtendAlpha = 0.0f;
		return;
	}

	const bool bExtending = (AbilityState == ETendrilAbilityState::Extending);
	const float TargetAlpha = bExtending ? 1.0f : 0.0f;
	const float ActiveSpeed = bExtending ? ExtendSpeed : RetractSpeed;
	const float PrevAlpha = ExtendAlpha;
	ExtendAlpha = FMath::FInterpConstantTo(ExtendAlpha, TargetAlpha, DeltaTime, ActiveSpeed);

	// 최대 전개(1.0)에 처음 도달한 순간 FTimerManager 기반 원샷 유지 시간 타이머를 무장합니다.
	if (bExtending && PrevAlpha < 1.0f - KINDA_SMALL_NUMBER && ExtendAlpha >= 1.0f - KINDA_SMALL_NUMBER)
	{
		ExtendAlpha = 1.0f;
		const float HoldLimitSec = GetHoldDurationLimitSeconds();
		if (HoldLimitSec >= 0.0f)
		{
			ArmHoldLimitTimer(HoldLimitSec);
		}
	}

	const FVector VisualOrigin = HostIfc->GetTendrilVisualOrigin();
	const float EasedReach = bExtending
		? (1.0f - FMath::Pow(1.0f - ExtendAlpha, 2.2f))
		: FMath::SmoothStep(0.0f, 1.0f, ExtendAlpha);
	const FVector StartPos = VisualOrigin + LockedAimDir * RootInsetCm;
	// 기본(월드 앵커 미고정) 상태에서는 본체가 고속 이동하더라도 촉수 끝점이 본체와 함께 병진 이동하도록 StartPos 기준으로 계산합니다.
	const FVector BodyRelativeTarget = StartPos + LockedAimDir * GetEffectiveReachCm();
	const FVector EndPos = FMath::Lerp(StartPos + LockedAimDir * 4.0f, BodyRelativeTarget, EasedReach);

	OnActiveTick(DeltaTime, VisualOrigin, StartPos, EndPos, EasedReach);

	if (ExtendAlpha <= KINDA_SMALL_NUMBER && AbilityState != ETendrilAbilityState::Extending)
	{
		ExtendAlpha = 0.0f;
		ClearHoldLimitTimer();
		if (AbilityState != ETendrilAbilityState::Idle)
		{
			AbilityState = ETendrilAbilityState::Idle;
			OnAbilityReturnedToIdle();
		}
	}
}

float USpaceTendrilAbility::EvaluateHostBodyStretch(float ElapsedTime, FVector& OutStretchDir) const
{
	OutStretchDir = LastTendrilDir;
	if (ExtendAlpha > KINDA_SMALL_NUMBER && AbilityState == ETendrilAbilityState::Extending)
	{
		return BodyStretchRatio * ExtendAlpha;
	}
	return 0.0f;
}

void USpaceTendrilAbility::LatchCameraAimBasis()
{
	ISpaceTendrilHostInterface* HostIfc = Host.GetInterface();
	if (!HostIfc)
	{
		return;
	}

	FVector CamLoc = FVector::ZeroVector;
	FVector CamFwd = FVector::ForwardVector;
	FVector CamRight = FVector::RightVector;
	FVector CamUp = FVector::UpVector;
	HostIfc->GetTendrilCameraBasis(CamLoc, CamFwd, CamRight, CamUp);

	LockedAimDir = CamFwd.IsNearlyZero() ? FVector::ForwardVector : CamFwd.GetSafeNormal();
	LastTendrilDir = LockedAimDir;

	FVector OrthoRight = FVector::VectorPlaneProject(CamRight, LockedAimDir).GetSafeNormal();
	if (OrthoRight.IsNearlyZero())
	{
		OrthoRight = FVector::CrossProduct(FVector::UpVector, LockedAimDir).GetSafeNormal();
	}
	FVector OrthoUp = FVector::VectorPlaneProject(CamUp, LockedAimDir).GetSafeNormal();
	if (OrthoUp.IsNearlyZero())
	{
		OrthoUp = FVector::CrossProduct(LockedAimDir, OrthoRight).GetSafeNormal();
	}

	LockedAimRight = OrthoRight;
	LockedAimUp = OrthoUp;
	LockedAimTarget = HostIfc->GetTendrilVisualOrigin() + LockedAimDir * GetEffectiveReachCm();
}

void USpaceTendrilAbility::ArmHoldLimitTimer(float DurationSeconds)
{
	ClearHoldLimitTimer();

	ISpaceTendrilHostInterface* HostIfc = Host.GetInterface();
	UWorld* World = HostIfc ? HostIfc->GetTendrilWorld() : nullptr;
	if (!World)
	{
		return;
	}

	if (DurationSeconds <= KINDA_SMALL_NUMBER)
	{
		OnHoldLimitTimerExpired();
		return;
	}

	World->GetTimerManager().SetTimer(
		HoldLimitTimerHandle,
		this,
		&USpaceTendrilAbility::OnHoldLimitTimerExpired,
		DurationSeconds,
		false
	);
}

void USpaceTendrilAbility::ClearHoldLimitTimer()
{
	if (!HoldLimitTimerHandle.IsValid())
	{
		return;
	}

	if (ISpaceTendrilHostInterface* HostIfc = Host.GetInterface())
	{
		if (UWorld* World = HostIfc->GetTendrilWorld())
		{
			World->GetTimerManager().ClearTimer(HoldLimitTimerHandle);
		}
	}
	HoldLimitTimerHandle.Invalidate();
}

void USpaceTendrilAbility::OnHoldLimitTimerExpired()
{
	HoldLimitTimerHandle.Invalidate();
	BeginRetract(true);
}

// ============================================================================
// [V1: ULongTendrilAbility]
// ============================================================================

void ULongTendrilAbility::DeactivateAbilityImmediate()
{
	DetachLatchedTarget();
	PeakTrunkLengthCm = 0.0f;
	TrunkRetractOffsetCm = 0.0f;
	bIsReelingIn = false;
	LastPushedSideBranchCount = -1;
	Super::DeactivateAbilityImmediate();
}

bool ULongTendrilAbility::CanActivateAbility() const
{
	if (!Super::CanActivateAbility())
	{
		return false;
	}
	if (PostColonizeLockoutEndTime > 0.0)
	{
		const ISpaceTendrilHostInterface* HostIfc = Host.GetInterface();
		const UWorld* World = HostIfc ? HostIfc->GetTendrilWorld() : nullptr;
		if (World && World->GetTimeSeconds() < PostColonizeLockoutEndTime)
		{
			return false;
		}
	}
	return true;
}

void ULongTendrilAbility::OnAbilityActivated()
{
	ISpaceTendrilHostInterface* HostIfc = Host.GetInterface();
	if (!HostIfc)
	{
		return;
	}

	const FVector VisualOrigin = HostIfc->GetTendrilVisualOrigin();
	AActor* HitActor = nullptr;
	UPrimitiveComponent* HitComp = nullptr;
	FVector HitImpactPoint = FVector::ZeroVector;
	LockedAimTarget = TraceAimTarget(VisualOrigin, HitActor, HitComp, HitImpactPoint);

	DetachLatchedTarget();
	if (HitActor && HitComp)
	{
		if (ASpaceAsteroid* HitAsteroid = Cast<ASpaceAsteroid>(HitActor))
		{
			if (!HitComp->IsSimulatingPhysics())
			{
				HitAsteroid->SetPhysicsSimulationTier(true);
			}
		}

		if (HitComp->IsSimulatingPhysics())
		{
			LatchedTargetActor = HitActor;
			LatchedTargetComponent = HitComp;
			LatchedLocalAnchor = HitComp->GetComponentTransform().InverseTransformPosition(HitImpactPoint);
			bHasLatchedTarget = true;
			LockedAimTarget = HitImpactPoint;
		}
	}

	FVector AimDir = (LockedAimTarget - VisualOrigin).GetSafeNormal();
	if (!AimDir.IsNearlyZero())
	{
		LockedAimDir = AimDir;
		LastTendrilDir = AimDir;
		FVector OrthoRight = FVector::VectorPlaneProject(LockedAimRight, LockedAimDir).GetSafeNormal();
		if (OrthoRight.IsNearlyZero())
		{
			OrthoRight = FVector::CrossProduct(FVector::UpVector, LockedAimDir).GetSafeNormal();
		}
		LockedAimRight = OrthoRight;
		LockedAimUp = FVector::CrossProduct(LockedAimDir, LockedAimRight).GetSafeNormal();
	}

	const FVector StartOrigin = VisualOrigin + LockedAimDir * RootInsetCm;
	LockedInitialTargetLenCm = FMath::Clamp(FVector::Distance(StartOrigin, LockedAimTarget), 50.0f, 50000.0f);
	PeakTrunkLengthCm = 0.0f;
	TrunkRetractOffsetCm = 0.0f;
	bIsReelingIn = false;
	ActiveShotBranchCount = ComputeBranchCountForLength(LockedInitialTargetLenCm);
	ActiveShotBranchLenScale = 0.0f;
}

float ULongTendrilAbility::GetHoldDurationLimitSeconds() const
{
	if (!bLimitHoldDuration)
	{
		return -1.0f;
	}
	if (bHasLatchedTarget && !bLimitHoldWhileLatched)
	{
		return -1.0f;
	}
	return FMath::Max(MaxHoldDurationSeconds, 0.0f);
}

float ULongTendrilAbility::GetEffectiveReachCm() const
{
	const float HostScale = Host.GetInterface() ? FMath::Max(Host.GetInterface()->GetTendrilBodyScale(), 0.2f) : 1.0f;
	const float ScaledMax = MaxLengthCm * FMath::Pow(HostScale, FMath::Max(SizeScaleReachExponent, 0.0f));
	return FMath::Clamp(ScaledMax, 120.0f, 50000.0f);
}

void ULongTendrilAbility::OnActiveTick(
	float DeltaTime,
	const FVector& VisualOrigin,
	const FVector& StartPos,
	const FVector& EndPos,
	float EasedReach)
{
	ISpaceTendrilHostInterface* HostIfc = Host.GetInterface();
	if (!HostIfc)
	{
		return;
	}

	// 1. 락온된 소행성의 실시간 월드 앵커 추적 및 사거리 이탈 검사
	if (bHasLatchedTarget)
	{
		AActor* TargetActor = LatchedTargetActor.Get();
		UPrimitiveComponent* TargetComp = LatchedTargetComponent.Get();
		if (!TargetActor || !TargetComp || TargetActor->IsHidden())
		{
			DetachLatchedTarget();
			if (IsExtending())
			{
				BeginRetract(true);
			}
		}
		else
		{
			const FVector LiveAnchorWorld = TargetComp->GetComponentTransform().TransformPosition(LatchedLocalAnchor);
			const float BreakDist = GetEffectiveReachCm() * FMath::Max(MaxTetherBreakMultiplier, 1.0f);
			if (FVector::Distance(VisualOrigin, LiveAnchorWorld) > BreakDist)
			{
				DetachLatchedTarget();
				if (IsExtending())
				{
					BeginRetract(true);
				}
			}
			else
			{
				LockedAimTarget = LiveAnchorWorld;
				const FVector NewDir = (LiveAnchorWorld - VisualOrigin).GetSafeNormal();
				if (!NewDir.IsNearlyZero())
				{
					LockedAimDir = NewDir;
					LastTendrilDir = NewDir;
					FVector NextRight = FVector::VectorPlaneProject(LockedAimRight, LockedAimDir).GetSafeNormal();
					if (NextRight.IsNearlyZero())
					{
						NextRight = FVector::CrossProduct(FVector::UpVector, LockedAimDir).GetSafeNormal();
					}
					LockedAimRight = NextRight;
					LockedAimUp = FVector::CrossProduct(LockedAimDir, LockedAimRight).GetSafeNormal();
				}
			}
		}
	}

	// 2. 뉴턴 작용·반작용 인력 및 편심 토크 적용 + 근접 시 소행성 전염(식민지화) 진행
	if (IsExtending() && bHasLatchedTarget && ExtendAlpha >= PullActivationAlpha)
	{
		ApplyTetherPullForces(VisualOrigin, LockedAimTarget);
		UpdateTargetColonization(DeltaTime);
	}
	else if (!IsExtending() && InvestedBiomassOnTarget > KINDA_SMALL_NUMBER)
	{
		RefundAndCancelColonization();
	}

	// 3. 줄기 길이 및 곁가지 스케일 계산 (고속 이동·회전 시에도 본체 중심에 완전히 밀착되도록 락온 중에는 중심 VisualOrigin에서 시작)
	const FVector LiveStartPos = bHasLatchedTarget ? VisualOrigin : (VisualOrigin + LockedAimDir * RootInsetCm);
	if (!bHasLatchedTarget)
	{
		// 소행성에 락온되지 않은 허공 발사 상태에서는 촉수가 본체에 붙어 함께 이동하도록 목표점을 본체 기준으로 유지합니다.
		LockedAimTarget = LiveStartPos + LockedAimDir * LockedInitialTargetLenCm;
	}
	const FVector LiveEndPos = FMath::Lerp(LiveStartPos + LockedAimDir * 4.0f, LockedAimTarget, EasedReach);
	if (IsExtending() && !bHasLatchedTarget)
	{
		TryLatchAlongActiveSegment(LiveStartPos, LiveEndPos);
	}
	const float CurrentLiveLenCm = FVector::Distance(LiveStartPos, LiveEndPos);
	if (!IsExtending())
	{
		bIsReelingIn = true;
	}
	if (!bIsReelingIn)
	{
		PeakTrunkLengthCm = FMath::Max(PeakTrunkLengthCm, CurrentLiveLenCm);
		TrunkRetractOffsetCm = 0.0f;
		const float TargetRatio = FMath::Clamp(
			(LockedInitialTargetLenCm - BranchMinTrunkCm) / FMath::Max(MaxLengthCm - BranchMinTrunkCm, 100.0f),
			0.0f,
			1.0f
		);
		const float TargetScale = FMath::Lerp(0.42f, 1.0f, TargetRatio);
		ActiveShotBranchLenScale = FMath::Max(ActiveShotBranchLenScale, TargetScale * FMath::SmoothStep(380.0f, 920.0f, PeakTrunkLengthCm));
	}
	else
	{
		PeakTrunkLengthCm = FMath::Max(PeakTrunkLengthCm, CurrentLiveLenCm);
		TrunkRetractOffsetCm = FMath::Max(PeakTrunkLengthCm - CurrentLiveLenCm, 0.0f);
	}

	// 4. 나이아가라 리본 컴포넌트 동기화
	if (UFXSystemComponent* FX = HostIfc->GetTendrilFXComponent(ETendrilVisualStyle::MyceliumWeb))
	{
		FX->SetWorldLocation(LiveStartPos);
		if (!FX->IsVisible())
		{
			FX->SetVisibility(true);
		}

		const int32 DesiredBranchCount = ComputeCurrentBranchCount();
		const bool bCountChanged = FX->IsActive() && (LastPushedSideBranchCount != DesiredBranchCount);
		PushLongTendrilParametersToFX(FX);

		if (bForceRestartFX || bCountChanged)
		{
			bForceRestartFX = false;
			if (FX->IsActive())
			{
				FX->DeactivateImmediate();
			}
		}

		const float VisualAlpha = FMath::SmoothStep(0.0f, 0.14f, ExtendAlpha);
		FX->SetVectorParameter(TEXT("User.TendrilStart"), LiveStartPos);
		FX->SetVectorParameter(TEXT("User.TendrilEnd"), LiveEndPos);
		FX->SetVectorParameter(TEXT("User.AimDir"), LockedAimDir);
		FX->SetVectorParameter(TEXT("User.AimRight"), LockedAimRight);
		FX->SetVectorParameter(TEXT("User.AimUp"), LockedAimUp);
		FX->SetFloatParameter(TEXT("User.TendrilAlpha"), VisualAlpha);
		FX->SetFloatParameter(TEXT("User.TendrilReach"), EasedReach);

		if (!FX->IsActive())
		{
			FX->Activate(true);
		}
	}
}

void ULongTendrilAbility::OnAbilityReturnedToIdle()
{
	DetachLatchedTarget();
	PeakTrunkLengthCm = 0.0f;
	TrunkRetractOffsetCm = 0.0f;
	bIsReelingIn = false;
	LastPushedSideBranchCount = -1;

	if (ISpaceTendrilHostInterface* HostIfc = Host.GetInterface())
	{
		if (UFXSystemComponent* FX = HostIfc->GetTendrilFXComponent(ETendrilVisualStyle::MyceliumWeb))
		{
			FX->SetFloatParameter(TEXT("User.TendrilAlpha"), 0.0f);
			FX->SetFloatParameter(TEXT("User.TendrilReach"), 0.0f);
			if (FX->IsActive())
			{
				FX->DeactivateImmediate();
			}
			if (FX->IsVisible())
			{
				FX->SetVisibility(false);
			}
		}
	}
}

void ULongTendrilAbility::OnHostCollisionHit(
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	const FVector& NormalImpulse,
	const FHitResult& Hit)
{
	if (bAutoDetachOnTargetHit && IsExtending() && bHasLatchedTarget && OtherActor && OtherActor == LatchedTargetActor.Get())
	{
		BeginRetract(false);
	}
}

int32 ULongTendrilAbility::ComputeBranchCountForLength(float TargetTrunkLenCm) const
{
	if (BranchFrequency <= KINDA_SMALL_NUMBER || TargetTrunkLenCm <= BranchMinTrunkCm)
	{
		return 0;
	}
	const float FullMaxLen = FMath::Max(MaxLengthCm, BranchMinTrunkCm + 100.0f);
	const float RawMaxCount = (FullMaxLen / 100.0f) * BranchFrequency;
	const float LengthRatio = FMath::Clamp(
		(TargetTrunkLenCm - BranchMinTrunkCm) / FMath::Max(FullMaxLen - BranchMinTrunkCm, 100.0f),
		0.0f,
		1.0f
	);
	return FMath::Clamp(FMath::RoundToInt(RawMaxCount * LengthRatio), 1, 64);
}

int32 ULongTendrilAbility::ComputeCurrentBranchCount() const
{
	return (AbilityState != ETendrilAbilityState::Idle)
		? ActiveShotBranchCount
		: ComputeBranchCountForLength(MaxLengthCm);
}

void ULongTendrilAbility::PushLongTendrilParametersToFX(UFXSystemComponent* TargetFX)
{
	if (!TargetFX)
	{
		return;
	}

	const bool bHasActiveShot = (AbilityState != ETendrilAbilityState::Idle);
	const int32 DesiredBranchCount = ComputeCurrentBranchCount();
	LastPushedSideBranchCount = DesiredBranchCount;

	const float EffectiveBranchLen = FMath::Max(BranchLengthCm * (bHasActiveShot ? ActiveShotBranchLenScale : 1.0f), 0.1f);
	const float DeployedRefLenCm = bHasActiveShot
		? FMath::Max(bIsReelingIn ? PeakTrunkLengthCm : LockedInitialTargetLenCm, 50.0f)
		: FMath::Max(MaxLengthCm, 50.0f);

	TargetFX->SetIntParameter(TEXT("User.SideBranchCount"), DesiredBranchCount);
	TargetFX->SetFloatParameter(TEXT("User.TrunkWidth"), FMath::Max(TrunkWidthCm, 0.2f));
	TargetFX->SetFloatParameter(TEXT("User.TipWidthRatio"), FMath::Clamp(TipWidthRatio, 0.0f, 3.0f));
	TargetFX->SetFloatParameter(TEXT("User.SideBranchLength"), EffectiveBranchLen);
	TargetFX->SetFloatParameter(TEXT("User.SideBranchWidth"), FMath::Max(BranchWidthCm, 0.1f));
	TargetFX->SetFloatParameter(TEXT("User.BranchBaseToTipRatio"), FMath::Clamp(BaseToTipLengthRatio, 0.0f, 4.0f));
	TargetFX->SetFloatParameter(TEXT("User.BranchStartRatio"), FMath::Clamp(BranchStartRatio, 0.02f, 0.90f));
	TargetFX->SetFloatParameter(TEXT("User.BranchSpreadScale"), FMath::Max(BranchSpreadScale, 0.05f));
	TargetFX->SetFloatParameter(TEXT("User.DeployedRefLengthCm"), DeployedRefLenCm);
	TargetFX->SetFloatParameter(TEXT("User.TrunkRetractOffsetCm"), bHasActiveShot ? TrunkRetractOffsetCm : 0.0f);
}

FVector ULongTendrilAbility::TraceAimTarget(
	const FVector& RootOrigin,
	AActor*& OutHitActor,
	UPrimitiveComponent*& OutHitComp,
	FVector& OutHitImpactPoint) const
{
	OutHitActor = nullptr;
	OutHitComp = nullptr;
	OutHitImpactPoint = FVector::ZeroVector;

	const float ReachDist = GetEffectiveReachCm();
	ISpaceTendrilHostInterface* HostIfc = Host.GetInterface();
	if (!HostIfc)
	{
		return RootOrigin + FVector::ForwardVector * ReachDist;
	}

	FVector CamLoc = RootOrigin;
	FVector CamForward = FVector::ForwardVector;
	FVector UnusedRight = FVector::RightVector;
	FVector UnusedUp = FVector::UpVector;
	HostIfc->GetTendrilCameraBasis(CamLoc, CamForward, UnusedRight, UnusedUp);

	const float PlayerProjDist = FMath::Max(FVector::DotProduct(RootOrigin - CamLoc, CamForward), 0.0f);
	const FVector TraceStart = CamLoc + CamForward * PlayerProjDist;
	const FVector TraceEnd = TraceStart + CamForward * ReachDist;

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SpaceDrifterTendrilAim), false, HostIfc->GetTendrilHostActor());
	UWorld* World = HostIfc->GetTendrilWorld();
	bool bHit = false;

	if (World)
	{
		if (AimTraceRadiusCm > KINDA_SMALL_NUMBER)
		{
			const FCollisionShape SphereShape = FCollisionShape::MakeSphere(AimTraceRadiusCm);
			bHit = World->SweepSingleByChannel(Hit, TraceStart, TraceEnd, FQuat::Identity, ECC_Visibility, SphereShape, Params);
			if (bHit && Hit.bStartPenetrating)
			{
				bHit = World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, Params);
			}
		}
		else
		{
			bHit = World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, Params);
		}
	}

	if (bHit && !Hit.bStartPenetrating && FVector::Distance(RootOrigin, Hit.ImpactPoint) > 5.0f)
	{
		OutHitActor = Hit.GetActor();
		OutHitComp = Hit.GetComponent();
		OutHitImpactPoint = Hit.ImpactPoint;
		return Hit.ImpactPoint;
	}

	return RootOrigin + (TraceEnd - RootOrigin).GetSafeNormal() * ReachDist;
}

bool ULongTendrilAbility::TryLatchAlongActiveSegment(const FVector& SegmentStart, const FVector& SegmentEnd)
{
	ISpaceTendrilHostInterface* HostIfc = Host.GetInterface();
	UWorld* World = HostIfc ? HostIfc->GetTendrilWorld() : nullptr;
	if (!HostIfc || !World || FVector::DistSquared(SegmentStart, SegmentEnd) < 100.0f)
	{
		return false;
	}

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SpaceDrifterTendrilMidSweep), false, HostIfc->GetTendrilHostActor());
	const float SweepRadius = FMath::Max(AimTraceRadiusCm, TrunkWidthCm * 0.5f);
	const FCollisionShape SphereShape = FCollisionShape::MakeSphere(SweepRadius);
	bool bHit = World->SweepSingleByChannel(Hit, SegmentStart, SegmentEnd, FQuat::Identity, ECC_Visibility, SphereShape, Params);
	if (bHit && Hit.bStartPenetrating)
	{
		bHit = World->LineTraceSingleByChannel(Hit, SegmentStart, SegmentEnd, ECC_Visibility, Params);
	}

	AActor* HitActor = bHit ? Hit.GetActor() : nullptr;
	UPrimitiveComponent* HitComp = bHit ? Hit.GetComponent() : nullptr;
	if (!HitActor || !HitComp)
	{
		return false;
	}

	if (ASpaceAsteroid* HitAsteroid = Cast<ASpaceAsteroid>(HitActor))
	{
		if (!HitComp->IsSimulatingPhysics())
		{
			HitAsteroid->SetPhysicsSimulationTier(true);
		}
	}

	if (!HitComp->IsSimulatingPhysics())
	{
		return false;
	}

	const FVector ImpactPos = Hit.ImpactPoint.IsNearlyZero() ? Hit.Location : Hit.ImpactPoint;
	LatchedTargetActor = HitActor;
	LatchedTargetComponent = HitComp;
	LatchedLocalAnchor = HitComp->GetComponentTransform().InverseTransformPosition(ImpactPos);
	bHasLatchedTarget = true;
	LockedAimTarget = ImpactPos;
	LockedInitialTargetLenCm = FMath::Max(FVector::Distance(SegmentStart, ImpactPos), 30.0f);
	ExtendAlpha = FMath::Max(ExtendAlpha, PullActivationAlpha);
	ActiveShotBranchCount = ComputeBranchCountForLength(LockedInitialTargetLenCm);

	const FVector NewDir = (ImpactPos - SegmentStart).GetSafeNormal();
	if (!NewDir.IsNearlyZero())
	{
		LockedAimDir = NewDir;
		LastTendrilDir = NewDir;
	}

	if (bLimitHoldDuration && !bLimitHoldWhileLatched)
	{
		ClearHoldLimitTimer();
	}

	return true;
}

void ULongTendrilAbility::ApplyTetherPullForces(const FVector& VisualOrigin, const FVector& TargetAnchorWorld)
{
	if (PullForce <= KINDA_SMALL_NUMBER || !bHasLatchedTarget)
	{
		return;
	}

	ISpaceTendrilHostInterface* HostIfc = Host.GetInterface();
	if (!HostIfc)
	{
		return;
	}

	const FVector PullDir = (TargetAnchorWorld - VisualOrigin).GetSafeNormal();
	if (PullDir.IsNearlyZero())
	{
		return;
	}

	UPrimitiveComponent* HostPhys = HostIfc->GetTendrilPhysicsComponent();
	UPrimitiveComponent* TargetComp = LatchedTargetComponent.Get();
	const FVector PlayerVel = HostIfc->GetTendrilBodyVelocity();
	const FVector TargetVel = (TargetComp && TargetComp->IsSimulatingPhysics())
		? TargetComp->GetPhysicsLinearVelocity()
		: FVector::ZeroVector;
	const FVector RelVel = PlayerVel - TargetVel;

	// 모든 거리에서 기본 접선 감쇠(BaseTangentialDamping)로 공전을 서서히 줄이고,
	// 근접할수록 CloseRangeTangentialDamping + MinInwardPullAccelCmPerSec2로 원심력을 압도해 확실히 맞붙게 합니다.
	const float AnchorDist = FVector::Distance(VisualOrigin, TargetAnchorWorld);
	const float ApproxSurfaceDist = FMath::Max(AnchorDist - HostIfc->GetTendrilBodyRadius(), 0.0f);
	const float DampingStartDist = FMath::Max(ColonizeTriggerSurfaceDistanceCm * FMath::Max(CloseRangeDampingRadiusMultiplier, 1.0f), 150.0f);
	const float ProximityAlpha = (ApproxSurfaceDist <= DampingStartDist)
		? (1.0f - FMath::Clamp(ApproxSurfaceDist / DampingStartDist, 0.0f, 1.0f))
		: 0.0f;

	const float HostMass = (HostPhys && HostPhys->IsSimulatingPhysics()) ? FMath::Max(HostPhys->GetMass(), 1.0f) : 350.0f;
	const float ActiveTangentialDamping = FMath::Lerp(FMath::Max(BaseTangentialDamping, 0.0f), FMath::Max(CloseRangeTangentialDamping, BaseTangentialDamping), ProximityAlpha);

	if (HostPhys && HostPhys->IsSimulatingPhysics() && ActiveTangentialDamping > KINDA_SMALL_NUMBER)
	{
		const FVector RadialVel = PullDir * FVector::DotProduct(RelVel, PullDir);
		const FVector TangentialVel = RelVel - RadialVel;
		const FVector TangentialBrakeForce = -TangentialVel * (HostMass * ActiveTangentialDamping);
		HostPhys->WakeRigidBody();
		HostPhys->AddForce(TangentialBrakeForce);
	}

	// V1 유효 사거리(길이) 배율에 비례해 끌어당김 최대 속도·최소 구심 가속도·인력을 함께 증폭
	const float ReachScale = FMath::Max(GetEffectiveReachCm() / FMath::Max(MaxLengthCm, 100.0f), 1.0f);
	const float PullSpeedScale = FMath::Pow(ReachScale, FMath::Max(PullSpeedLengthExponent, 0.0f));
	const float PullForceScale = FMath::Pow(ReachScale, FMath::Max(PullForceLengthExponent, 0.0f));

	const float ApproachSpeed = FVector::DotProduct(RelVel, PullDir);
	const float SafeMaxSpeed = FMath::Max(MaxRelativePullSpeedCmPerSec, 50.0f) * PullSpeedScale;
	const float SpeedScale = (ApproachSpeed < 0.0f)
		? 1.5f
		: FMath::Clamp(1.0f - (ApproachSpeed / SafeMaxSpeed), 0.0f, 1.0f);
	if (SpeedScale <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const float CloseBoost = FMath::Lerp(1.0f, FMath::Max(CloseRangePullBoostMultiplier, 1.0f), ProximityAlpha);
	const float ScaledMinInwardAccel = FMath::Max(MinInwardPullAccelCmPerSec2, 500.0f) * PullForceScale;
	const float GuaranteedPullForce = FMath::Max(PullForce * PullForceScale, HostMass * ScaledMinInwardAccel);
	const FVector ForceVec = PullDir * (GuaranteedPullForce * SpeedScale * CloseBoost);

	if (HostPhys && HostPhys->IsSimulatingPhysics())
	{
		HostPhys->WakeRigidBody();
		HostPhys->AddForce(ForceVec);
	}

	if (TargetComp && TargetComp->IsSimulatingPhysics())
	{
		TargetComp->WakeRigidBody();
		// 가벼운 소행성이 과도하게 가속되어 상대 속도 상한을 즉시 포화시키지 않도록 타겟 반작용 힘을 질량 비례로 상한 제한
		const float TargetMass = FMath::Max(TargetComp->GetMass(), 1.0f);
		const float MaxTargetCounterForce = TargetMass * ScaledMinInwardAccel * 2.5f;
		const FVector CounterForce = (-ForceVec).GetClampedToMaxSize(MaxTargetCounterForce);
		const float TorqueRatio = FMath::Clamp(TorqueTransferRatio, 0.0f, 1.0f);
		if (TorqueRatio > KINDA_SMALL_NUMBER)
		{
			TargetComp->AddForceAtLocation(CounterForce * TorqueRatio, TargetAnchorWorld);
		}
		if (1.0f - TorqueRatio > KINDA_SMALL_NUMBER)
		{
			TargetComp->AddForce(CounterForce * (1.0f - TorqueRatio));
		}
	}
}

void ULongTendrilAbility::UpdateTargetColonization(float DeltaTime)
{
	ISpaceTendrilHostInterface* HostIfc = Host.GetInterface();
	AActor* TargetActor = LatchedTargetActor.Get();
	ISpaceColonizableTarget* ColonizableTarget = Cast<ISpaceColonizableTarget>(TargetActor);
	if (!HostIfc || !TargetActor || !ColonizableTarget || !ColonizableTarget->CanBeColonized() || DeltaTime <= 0.0f)
	{
		return;
	}

	// 조건 1: V1으로 전염 가능한 대상을 붙잡고 있는가
	// 조건 2: 플레이어와 대상 표면 사이 거리가 일정 이하인가
	const float CenterDist = FVector::Distance(HostIfc->GetTendrilBodyLocation(), TargetActor->GetActorLocation());
	const float SurfaceDist = CenterDist - HostIfc->GetTendrilBodyRadius() - ColonizableTarget->GetColonizeRadiusCm();
	if (SurfaceDist > ColonizeTriggerSurfaceDistanceCm)
	{
		return;
	}

	const float RequiredBiomass = ColonizableTarget->GetRequiredColonizeBiomass();
	const float CurrentUsable = FMath::Max(HostIfc->GetTendrilBiomass() - HostIfc->GetTendrilMinGameOverBiomass(), 0.0f);
	const float TotalUsablePool = CurrentUsable + InvestedBiomassOnTarget;

	// 사전 실패 판정: 필요량이 가용 풀 이상이면 헛투자 없이 즉시 실패 처리 및 회수
	if (RequiredBiomass >= TotalUsablePool)
	{
		const float RequiredPercent = (TotalUsablePool > KINDA_SMALL_NUMBER)
			? (RequiredBiomass / TotalUsablePool) * 100.0f
			: 999.0f;
		HostIfc->PostTendrilStatusBanner(
			FString::Printf(TEXT("[V1 전염 실패] 크기 부족 (가용 대비 %.0f%% 필요) -> 생체량 회수"), RequiredPercent),
			FLinearColor(1.0f, 0.45f, 0.38f, 1.0f),
			1.8f
		);
		RefundAndCancelColonization();
		DetachLatchedTarget();
		BeginRetract(true);
		return;
	}

	const float RemainingBiomass = FMath::Max(RequiredBiomass - InvestedBiomassOnTarget, 0.0f);
	const float TransferRatePerSec = FMath::Max(TotalUsablePool * (FMath::Max(ColonizeUsablePercentPerSec, 5.0f) * 0.01f), 1.0f);
	const float StepBiomass = FMath::Min(TransferRatePerSec * DeltaTime, RemainingBiomass);

	if (StepBiomass > KINDA_SMALL_NUMBER)
	{
		// 안전망: CanSpendTendrilBiomass 실패 시 투자분 회수 및 연결 해제
		if (!HostIfc->CanSpendTendrilBiomass(StepBiomass, false))
		{
			const float RequiredPercent = (TotalUsablePool > KINDA_SMALL_NUMBER)
				? (RequiredBiomass / TotalUsablePool) * 100.0f
				: 999.0f;
			HostIfc->PostTendrilStatusBanner(
				FString::Printf(TEXT("[V1 전염 실패] 크기 부족 (가용 대비 %.0f%% 필요) -> 생체량 회수"), RequiredPercent),
				FLinearColor(1.0f, 0.45f, 0.38f, 1.0f),
				1.8f
			);
			RefundAndCancelColonization();
			DetachLatchedTarget();
			BeginRetract(true);
			return;
		}

		HostIfc->SpendTendrilBiomass(StepBiomass, false);
		InvestedBiomassOnTarget += StepBiomass;
	}

	const float Progress = FMath::Clamp(InvestedBiomassOnTarget / FMath::Max(RequiredBiomass, 1.0f), 0.0f, 1.0f);
	ColonizableTarget->SetColonizationProgressVisual(Progress);

	const float InvestedUsablePercent = (TotalUsablePool > KINDA_SMALL_NUMBER)
		? FMath::Clamp((InvestedBiomassOnTarget / TotalUsablePool) * 100.0f, 0.0f, 100.0f)
		: 0.0f;

	HostIfc->PostTendrilStatusBanner(
		FString::Printf(TEXT("[V1 전염 진행] %.0f%% · 가용 %.0f%% 투입"), Progress * 100.0f, InvestedUsablePercent),
		FLinearColor(0.45f, 0.98f, 0.76f, 1.0f),
		0.25f
	);

	if (Progress >= 1.0f - KINDA_SMALL_NUMBER)
	{
		// 전염 성공: 투자한 생체량은 대상에 남기고 배양 폭발까지 촉수 연결을 유지(원테이크 흡수)하되, 폭발 직후 오발사 방지를 위해 재발사 잠금을 설정합니다.
		InvestedBiomassOnTarget = 0.0f;
		ColonizableTarget->CompleteColonization(HostIfc->GetTendrilBodyMaterial(), HostIfc->GetActiveAsteroidFieldManager());
		if (PostColonizationLockoutSeconds > KINDA_SMALL_NUMBER)
		{
			if (const UWorld* World = HostIfc->GetTendrilWorld())
			{
				PostColonizeLockoutEndTime = World->GetTimeSeconds() + static_cast<double>(PostColonizationLockoutSeconds);
			}
		}
		HostIfc->PostTendrilStatusBanner(
			TEXT("[V1 전염 완료] 배양 중... 곧 폭발합니다!"),
			FLinearColor(0.38f, 0.95f, 1.0f, 1.0f),
			1.5f
		);
	}
}

void ULongTendrilAbility::RefundAndCancelColonization()
{
	if (InvestedBiomassOnTarget > KINDA_SMALL_NUMBER)
	{
		if (ISpaceTendrilHostInterface* HostIfc = Host.GetInterface())
		{
			HostIfc->RefundTendrilBiomass(InvestedBiomassOnTarget);
		}
		InvestedBiomassOnTarget = 0.0f;
	}

	if (ISpaceColonizableTarget* ColonizableTarget = Cast<ISpaceColonizableTarget>(LatchedTargetActor.Get()))
	{
		if (ColonizableTarget->IsColonizingInProgress())
		{
			ColonizableTarget->ResetColonizationState();
		}
	}
}

void ULongTendrilAbility::DetachLatchedTarget()
{
	RefundAndCancelColonization();
	bHasLatchedTarget = false;
	LatchedTargetActor.Reset();
	LatchedTargetComponent.Reset();
	LatchedLocalAnchor = FVector::ZeroVector;
}

// ============================================================================
// [V2: UNetTendrilAbility]
// ============================================================================

UNetTendrilAbility::UNetTendrilAbility()
{
	RetractSpeed = 1.2f;
	CooldownSeconds = 7.8f;
}

void UNetTendrilAbility::LatchCameraAimBasis()
{
	Super::LatchCameraAimBasis();

	ISpaceTendrilHostInterface* HostIfc = Host.GetInterface();
	if (!HostIfc)
	{
		return;
	}

	const FVector VisualOrigin = HostIfc->GetTendrilVisualOrigin();
	FVector CamLoc = VisualOrigin;
	FVector CamFwd = FVector::ForwardVector;
	FVector CamRight = FVector::RightVector;
	FVector CamUp = FVector::UpVector;
	HostIfc->GetTendrilCameraBasis(CamLoc, CamFwd, CamRight, CamUp);
	CamFwd = CamFwd.IsNearlyZero() ? FVector::ForwardVector : CamFwd.GetSafeNormal();

	// 3인칭 카메라 소켓 오프셋으로 인한 시차(Parallax)를 제거하기 위해
	// 화면 정중앙 크로스헤어 레이(TraceStart -> TraceEnd) 상의 목표점으로 수렴하도록 조준 축을 정렬합니다.
	const float ReachDist = GetEffectiveReachCm();
	const float PlayerProjDist = FMath::Max(FVector::DotProduct(VisualOrigin - CamLoc, CamFwd), 0.0f);
	const FVector TraceStart = CamLoc + CamFwd * PlayerProjDist;
	FVector ReticleAimPoint = TraceStart + CamFwd * ReachDist;

	if (UWorld* World = HostIfc->GetTendrilWorld())
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(SpaceDrifterNetAim), false, HostIfc->GetTendrilHostActor());
		const bool bHit = World->LineTraceSingleByChannel(Hit, TraceStart, ReticleAimPoint, ECC_Visibility, Params);
		if (bHit && !Hit.bStartPenetrating)
		{
			const float MinConvergeDist = FMath::Min(ReachDist, 180.0f);
			if (FVector::Distance(VisualOrigin, Hit.ImpactPoint) >= MinConvergeDist)
			{
				ReticleAimPoint = Hit.ImpactPoint;
			}
		}
	}

	const FVector ConvergedDir = (ReticleAimPoint - VisualOrigin).GetSafeNormal();
	if (!ConvergedDir.IsNearlyZero())
	{
		LockedAimDir = ConvergedDir;
		LastTendrilDir = ConvergedDir;

		FVector OrthoRight = FVector::VectorPlaneProject(CamRight, LockedAimDir).GetSafeNormal();
		if (OrthoRight.IsNearlyZero())
		{
			OrthoRight = FVector::CrossProduct(FVector::UpVector, LockedAimDir).GetSafeNormal();
		}
		if (OrthoRight.IsNearlyZero())
		{
			OrthoRight = FVector::CrossProduct(FVector::RightVector, LockedAimDir).GetSafeNormal();
		}

		FVector OrthoUp = FVector::CrossProduct(LockedAimDir, OrthoRight).GetSafeNormal();
		if (OrthoUp.IsNearlyZero())
		{
			OrthoUp = FVector::VectorPlaneProject(CamUp, LockedAimDir).GetSafeNormal();
		}

		LockedAimRight = OrthoRight;
		LockedAimUp = OrthoUp;
		LockedAimTarget = VisualOrigin + LockedAimDir * ReachDist;
	}
}

float UNetTendrilAbility::GetHoldDurationLimitSeconds() const
{
	const float ScaledHold = FMath::Max(HoldDurationSeconds, 0.0f) * FMath::Max(NetVisibilityHoldMultiplier, 0.5f);
	return FMath::Max(ScaledHold, FMath::Max(MinHoldDurationSeconds, 0.0f));
}

void UNetTendrilAbility::DeactivateAbilityImmediate()
{
	Super::DeactivateAbilityImmediate();
	SlowedEnemiesThisShot.Reset();
	HideCastNetMesh();
}

void UNetTendrilAbility::OnAbilityActivated()
{
	RetractSpeed = FMath::Max(NetRetractSpeed * FMath::Clamp(NetRetractSpeedScale, 0.15f, 2.0f), 0.45f);
	SlowedEnemiesThisShot.Reset();
}

void UNetTendrilAbility::OnActiveTick(
	float DeltaTime,
	const FVector& VisualOrigin,
	const FVector& StartPos,
	const FVector& EndPos,
	float EasedReach)
{
	ISpaceTendrilHostInterface* HostIfc = Host.GetInterface();
	if (!HostIfc)
	{
		return;
	}

	const UWorld* World = HostIfc->GetTendrilWorld();
	const float TimeSec = World ? World->GetTimeSeconds() : 0.0f;

	// V2 투망이 포식자 곰팡이에 닿으면 포획망에 감겨 즉시 감속·이동속도 둔화·크기 축소(약화)
	if (IsExtending() && EasedReach > 0.08f)
	{
		if (!CachedEnemyManager.IsValid() && World)
		{
			CachedEnemyManager = Cast<AEnemyMoldManager>(UGameplayStatics::GetActorOfClass(World, AEnemyMoldManager::StaticClass()));
		}

		if (const AEnemyMoldManager* EnemyMgr = CachedEnemyManager.Get())
		{
			TArray<ASpaceEnemyMold*> ActiveEnemies;
			EnemyMgr->GatherActiveEnemies(ActiveEnemies);

			const float EffectiveNetLen = FMath::Max(GetEffectiveReachCm() * EasedReach, 80.0f);
			const float BodyRadius = FMath::Max(HostIfc->GetTendrilBodyRadius(), 20.0f);
			const float SafeSlowDuration = (CooldownSeconds > 0.5f)
				? FMath::Min(WebSlowDurationSeconds, CooldownSeconds - 0.5f)
				: WebSlowDurationSeconds;

			int32 NewlySlowedCount = 0;
			for (ASpaceEnemyMold* Enemy : ActiveEnemies)
			{
				if (!IsValid(Enemy) || SlowedEnemiesThisShot.Contains(Enemy))
				{
					continue;
				}

				const FVector EnemyLoc = Enemy->GetActorLocation();
				const float EnemyRadius = Enemy->GetCurrentRadiusCm();
				const float AlongDist = FVector::DotProduct(EnemyLoc - StartPos, LockedAimDir);
				if (AlongDist < -(BodyRadius * 1.2f + EnemyRadius) || AlongDist > EffectiveNetLen + EnemyRadius + BodyRadius * 1.5f)
				{
					continue;
				}

				const float NormT = FMath::Clamp(AlongDist / FMath::Max(EffectiveNetLen, 1.0f), 0.0f, 1.0f);
				const FVector FunnelCenter = EvaluateFunnelCenter(StartPos, EffectiveNetLen, NormT, TimeSec);
				const float NetHitRadius = EvaluateFunnelRadiusCm(NormT, EasedReach) + CapturePaddingCm + EnemyRadius;

				if (FVector::DistSquared(EnemyLoc, FunnelCenter) <= FMath::Square(NetHitRadius))
				{
					SlowedEnemiesThisShot.Add(Enemy);
					Enemy->ApplyWebSlow(SafeSlowDuration, WebSlowSpeedMultiplier);
					if (WebShrinkPercentPerHit > KINDA_SMALL_NUMBER)
					{
						Enemy->ApplyWebShrink(WebShrinkPercentPerHit, WebMinShrinkRadiusRatio);
					}
					// 포획망에 걸린 즉각적인 타격감: 현재 이동 관성을 즉시 감쇄
					if (USphereComponent* EnemySphere = Enemy->GetCollisionSphere())
					{
						if (EnemySphere->IsSimulatingPhysics())
						{
							EnemySphere->SetPhysicsLinearVelocity(EnemySphere->GetPhysicsLinearVelocity() * 0.30f);
						}
					}
					++NewlySlowedCount;
				}
			}

			if (NewlySlowedCount > 0)
			{
				const int32 TotalSlowed = SlowedEnemiesThisShot.Num();
				HostIfc->PostTendrilStatusBanner(
					FString::Printf(TEXT("포획망 적중! 포식자 %d마리 영구 약화 (크기 -%.0f%% · 이속 %.0f%% 영구 감소)"), TotalSlowed, WebShrinkPercentPerHit, WebSlowSpeedMultiplier * 100.0f),
					FLinearColor(0.32f, 0.94f, 1.0f, 1.0f),
					2.2f
				);
			}
		}
	}

	// 기존 단일 긴 촉수형 나이아가라(GossamerSporesFX)는 비활성화하고, 시작부터 넓게 펼쳐지는 3D 투망 메시로 렌더링
	if (UFXSystemComponent* FX = HostIfc->GetTendrilFXComponent(ETendrilVisualStyle::GossamerSpores))
	{
		FX->SetFloatParameter(TEXT("User.TendrilAlpha"), 0.0f);
		FX->SetFloatParameter(TEXT("User.TendrilReach"), 0.0f);
		FX->SetFloatParameter(TEXT("User.SporeSpawnRate"), 0.0f);
		if (FX->IsActive())
		{
			FX->Deactivate();
		}
	}

	UpdateCastNetMesh(StartPos, EasedReach, TimeSec);
}

void UNetTendrilAbility::OnAbilityReturnedToIdle()
{
	if (ISpaceTendrilHostInterface* HostIfc = Host.GetInterface())
	{
		if (UFXSystemComponent* FX = HostIfc->GetTendrilFXComponent(ETendrilVisualStyle::GossamerSpores))
		{
			FX->SetFloatParameter(TEXT("User.TendrilAlpha"), 0.0f);
			FX->SetFloatParameter(TEXT("User.TendrilReach"), 0.0f);
			FX->SetFloatParameter(TEXT("User.SporeSpawnRate"), 0.0f);
		}
	}
	HideCastNetMesh();
}

FVector UNetTendrilAbility::EvaluateFunnelCenter(
	const FVector& StartPos,
	float MaxNetLenCm,
	float NormalizedT,
	float TimeSec) const
{
	const float ClampedT = FMath::Clamp(NormalizedT, 0.0f, 1.0f);
	return StartPos
		+ LockedAimDir * (MaxNetLenCm * ClampedT)
		+ LockedAimRight * (FMath::Sin(TimeSec * 2.2f + ClampedT * 3.0f) * 3.5f * ClampedT)
		+ LockedAimUp * (FMath::Cos(TimeSec * 1.9f + ClampedT * 2.5f) * 3.0f * ClampedT);
}

float UNetTendrilAbility::GetEffectiveReachCm() const
{
	const float BodyRadius = Host.GetInterface() ? FMath::Max(Host.GetInterface()->GetTendrilBodyRadius(), 20.0f) : 75.0f;
	const float ScaledReach = BodyRadius * FMath::Max(NetReachBodyRadiusMultiplier, 2.5f);
	return FMath::Clamp(ScaledReach, 150.0f, 50000.0f);
}

float UNetTendrilAbility::EvaluateFunnelRadiusCm(float NormalizedT, float CurrentReachAlpha) const
{
	const float ClampedT = FMath::Clamp(NormalizedT, 0.0f, 1.0f);
	const float BodyRadius = Host.GetInterface() ? FMath::Max(Host.GetInterface()->GetTendrilBodyRadius(), 20.0f) : 75.0f;
	const float StartR = BodyRadius * FMath::Max(NetStartSpreadBodyRadiusMultiplier, 0.05f);
	const float MaxR = BodyRadius * FMath::Max(NetMaxSpreadBodyRadiusMultiplier, NetStartSpreadBodyRadiusMultiplier + 0.5f);

	// 꼭짓점(StartR)에서 앞쪽 넓은 입구(MaxR)로 자연스럽게 퍼지는 원뿔(Cone) 투망 곡선
	const float ConeCurve = FMath::Pow(ClampedT, FMath::Clamp(CastNetBellExponent, 0.2f, 1.8f));
	const float OpenProgress = FMath::Lerp(0.35f, 1.0f, FMath::Pow(FMath::Clamp(CurrentReachAlpha, 0.0f, 1.0f), 0.65f));
	return FMath::Lerp(StartR, MaxR * OpenProgress, ConeCurve);
}

UProceduralMeshComponent* UNetTendrilAbility::EnsureCastNetMeshComponent()
{
	if (CastNetMesh.IsValid())
	{
		return CastNetMesh.Get();
	}

	ISpaceTendrilHostInterface* HostIfc = Host.GetInterface();
	AActor* HostActor = HostIfc ? HostIfc->GetTendrilHostActor() : nullptr;
	if (!HostActor)
	{
		return nullptr;
	}

	UProceduralMeshComponent* NewNetMesh = NewObject<UProceduralMeshComponent>(HostActor, NAME_None, RF_Transient);
	if (!NewNetMesh)
	{
		return nullptr;
	}

	NewNetMesh->SetUsingAbsoluteLocation(true);
	NewNetMesh->SetUsingAbsoluteRotation(true);
	NewNetMesh->SetUsingAbsoluteScale(true);
	NewNetMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NewNetMesh->SetGenerateOverlapEvents(false);
	NewNetMesh->SetCastShadow(false);
	NewNetMesh->bUseAsyncCooking = false;

	if (!CachedCastNetMaterial.IsValid())
	{
		// M_SpaceMineral은 UProceduralMeshComponent에서 Emissive 발광이 검증된 머티리얼입니다(M_MyceliumRibbon은 나이아가라 리본 전용이라 프로시저럴 메시에서 투명해짐)
		UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Sandbox/MiniProject/Materials/M_SpaceMineral.M_SpaceMineral"));
		if (!BaseMat)
		{
			BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		}
		if (BaseMat)
		{
			if (UMaterialInstanceDynamic* NetMID = UMaterialInstanceDynamic::Create(BaseMat, HostActor))
			{
				const FLinearColor CyanNetColor(0.18f, 0.96f, 0.92f, 1.0f);
				NetMID->SetVectorParameterValue(TEXT("MineralTipColor"), CyanNetColor);
				NetMID->SetVectorParameterValue(TEXT("MineralEmissiveColor"), CyanNetColor);
				NetMID->SetScalarParameterValue(TEXT("MineralGlowIntensity"), 3.2f);
				CachedCastNetMaterial = NetMID;
			}
			else
			{
				CachedCastNetMaterial = BaseMat;
			}
		}
	}

	if (UMaterialInterface* Mat = CachedCastNetMaterial.Get())
	{
		NewNetMesh->SetMaterial(0, Mat);
	}

	if (USceneComponent* RootComp = HostActor->GetRootComponent())
	{
		NewNetMesh->AttachToComponent(RootComp, FAttachmentTransformRules::KeepWorldTransform);
	}
	NewNetMesh->RegisterComponent();
	NewNetMesh->SetWorldTransform(FTransform::Identity);
	NewNetMesh->SetVisibility(false);
	CastNetMesh = NewNetMesh;
	return NewNetMesh;
}

void UNetTendrilAbility::HideCastNetMesh()
{
	if (UProceduralMeshComponent* NetMesh = CastNetMesh.Get())
	{
		NetMesh->ClearAllMeshSections();
		NetMesh->SetVisibility(false);
	}
}

void UNetTendrilAbility::UpdateCastNetMesh(const FVector& StartPos, float CurrentReachAlpha, float TimeSec)
{
	ISpaceTendrilHostInterface* HostIfc = Host.GetInterface();
	if (!HostIfc || CurrentReachAlpha <= KINDA_SMALL_NUMBER)
	{
		HideCastNetMesh();
		return;
	}

	UProceduralMeshComponent* NetMesh = EnsureCastNetMeshComponent();
	if (!NetMesh)
	{
		return;
	}

	const float BodyRadius = FMath::Max(HostIfc->GetTendrilBodyRadius(), 20.0f);
	const float CurrentNetLen = FMath::Max(GetEffectiveReachCm() * CurrentReachAlpha, BodyRadius * 1.2f);
	const float StrandHalfWidth = FMath::Max(BodyRadius * FMath::Clamp(NetStrandThicknessBodyRadiusRatio, 0.01f, 0.2f), 2.5f);
	const int32 RadialCount = FMath::Clamp(NetRadialStrandCount, 6, 24);
	const int32 RingCount = FMath::Clamp(NetConcentricRingCount, 3, 14);
	const float FadeAlpha = FMath::SmoothStep(0.0f, 0.12f, ExtendAlpha);

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;

	const int32 EstimatedQuads = RadialCount * RingCount * 3 + SlowedEnemiesThisShot.Num() * 48;
	Vertices.Reserve(EstimatedQuads * 4);
	Triangles.Reserve(EstimatedQuads * 12);
	Normals.Reserve(EstimatedQuads * 4);
	UVs.Reserve(EstimatedQuads * 4);
	Colors.Reserve(EstimatedQuads * 4);

	auto AppendRibbonSegment = [&](const FVector& P0, const FVector& P1, const FVector& SideAxis, float HalfW, const FLinearColor& C0, const FLinearColor& C1)
	{
		const FVector SegDir = (P1 - P0).GetSafeNormal();
		if (SegDir.IsNearlyZero())
		{
			return;
		}
		FVector Side = FVector::CrossProduct(SegDir, SideAxis).GetSafeNormal();
		if (Side.IsNearlyZero())
		{
			Side = FVector::CrossProduct(SegDir, LockedAimUp).GetSafeNormal();
		}
		if (Side.IsNearlyZero())
		{
			Side = FVector::CrossProduct(SegDir, LockedAimRight).GetSafeNormal();
		}
		const FVector Nrm = FVector::CrossProduct(Side, SegDir).GetSafeNormal();
		const int32 BaseIdx = Vertices.Num();

		Vertices.Add(P0 - Side * HalfW);
		Vertices.Add(P0 + Side * HalfW);
		Vertices.Add(P1 + Side * HalfW);
		Vertices.Add(P1 - Side * HalfW);

		Normals.Add(Nrm);
		Normals.Add(Nrm);
		Normals.Add(Nrm);
		Normals.Add(Nrm);

		UVs.Add(FVector2D(0.0f, 0.0f));
		UVs.Add(FVector2D(1.0f, 0.0f));
		UVs.Add(FVector2D(1.0f, 1.0f));
		UVs.Add(FVector2D(0.0f, 1.0f));

		Colors.Add(C0);
		Colors.Add(C0);
		Colors.Add(C1);
		Colors.Add(C1);

		// 양면 렌더링 (앞/뒤 어느 각도에서도 투망이 선명하게 보이도록)
		Triangles.Add(BaseIdx + 0); Triangles.Add(BaseIdx + 1); Triangles.Add(BaseIdx + 2);
		Triangles.Add(BaseIdx + 0); Triangles.Add(BaseIdx + 2); Triangles.Add(BaseIdx + 3);
		Triangles.Add(BaseIdx + 0); Triangles.Add(BaseIdx + 2); Triangles.Add(BaseIdx + 1);
		Triangles.Add(BaseIdx + 0); Triangles.Add(BaseIdx + 3); Triangles.Add(BaseIdx + 2);
	};

	// 격자 노드 미리 계산: [RingIdx][RadialIdx]
	TArray<TArray<FVector>> NetNodes;
	NetNodes.SetNum(RingCount + 1);
	for (int32 r = 0; r <= RingCount; ++r)
	{
		const float T = static_cast<float>(r) / static_cast<float>(RingCount);
		const float BaseRadius = EvaluateFunnelRadiusCm(T, CurrentReachAlpha);
		// 투망 끝단(Rim)이 낚시 그물처럼 앞쪽으로 살짝 말리며 감싸는 형태
		const float RimCupForward = FMath::Sin(T * UE_PI * 0.85f) * BodyRadius * 0.25f;
		const FVector RingCenter = EvaluateFunnelCenter(StartPos, CurrentNetLen, T, TimeSec) + LockedAimDir * RimCupForward;

		NetNodes[r].SetNum(RadialCount);
		for (int32 a = 0; a < RadialCount; ++a)
		{
			const float Angle = (2.0f * UE_PI * static_cast<float>(a)) / static_cast<float>(RadialCount);
			// 무중력 공간에서 투망이 일렁이는 미세 파동
			const float Billow = 1.0f + 0.055f * FMath::Sin(TimeSec * 5.2f + Angle * 3.0f - T * 4.5f) * T;
			const FVector RadialDir = LockedAimRight * FMath::Cos(Angle) + LockedAimUp * FMath::Sin(Angle);
			NetNodes[r][a] = RingCenter + RadialDir * (BaseRadius * Billow);
		}
	}

	const FLinearColor InnerColor(0.18f, 0.96f, 0.88f, FadeAlpha);
	const FLinearColor OuterRimColor(0.45f, 0.92f, 1.0f, FadeAlpha);
	const FLinearColor WebCrossColor(0.28f, 0.84f, 0.98f, FadeAlpha * 0.92f);

	// 1) 방사형 주 가닥 (Radial Strands)
	for (int32 a = 0; a < RadialCount; ++a)
	{
		const float Angle = (2.0f * UE_PI * static_cast<float>(a)) / static_cast<float>(RadialCount);
		const FVector Outward = LockedAimRight * FMath::Cos(Angle) + LockedAimUp * FMath::Sin(Angle);
		for (int32 r = 0; r < RingCount; ++r)
		{
			const float T0 = static_cast<float>(r) / static_cast<float>(RingCount);
			const float T1 = static_cast<float>(r + 1) / static_cast<float>(RingCount);
			const FLinearColor C0 = FMath::Lerp(InnerColor, OuterRimColor, T0);
			const FLinearColor C1 = FMath::Lerp(InnerColor, OuterRimColor, T1);
			AppendRibbonSegment(NetNodes[r][a], NetNodes[r + 1][a], Outward, StrandHalfWidth * 1.15f, C0, C1);
		}
	}

	// 2) 동심원 가로 그물 링 (Concentric Hoops) + 다이아몬드 교차 그물코
	for (int32 r = 1; r <= RingCount; ++r)
	{
		const float T = static_cast<float>(r) / static_cast<float>(RingCount);
		const bool bIsOuterRim = (r == RingCount);
		const float HoopHalfW = bIsOuterRim ? (StrandHalfWidth * 1.45f) : (StrandHalfWidth * 0.85f);
		const FLinearColor RingCol = bIsOuterRim ? OuterRimColor : FMath::Lerp(InnerColor, OuterRimColor, T);

		for (int32 a = 0; a < RadialCount; ++a)
		{
			const int32 NextA = (a + 1) % RadialCount;
			AppendRibbonSegment(NetNodes[r][a], NetNodes[r][NextA], LockedAimDir, HoopHalfW, RingCol, RingCol);

			// 엇갈린 대각선 그물코(교차 그물 느낌 강화)
			if (r < RingCount && ((r + a) % 2 == 0))
			{
				AppendRibbonSegment(NetNodes[r][a], NetNodes[r + 1][NextA], LockedAimDir, StrandHalfWidth * 0.65f, WebCrossColor, WebCrossColor);
			}
		}
	}

	// 3) 포획망에 걸린 포식자 주변을 감싸는 청록색 속박 그물 케이지 연출
	const FLinearColor SnareColor(0.42f, 1.0f, 0.96f, FadeAlpha);
	for (const TWeakObjectPtr<ASpaceEnemyMold>& WeakEnemy : SlowedEnemiesThisShot)
	{
		const ASpaceEnemyMold* Enemy = WeakEnemy.Get();
		if (!IsValid(Enemy) || Enemy->IsDormant())
		{
			continue;
		}
		const FVector EnemyCenter = Enemy->GetActorLocation();
		const float SnareR = Enemy->GetCurrentRadiusCm() * 1.14f;
		const float SnareW = FMath::Max(SnareR * 0.055f, 3.0f);
		constexpr int32 CageSegs = 12;

		// 3개의 직교 경사 그물 링으로 포식자가 그물에 감긴 시각적 피드백 강화
		const FVector Axes[3][2] = {
			{ FVector::ForwardVector, FVector::RightVector },
			{ FVector::RightVector, FVector::UpVector },
			{ FVector::UpVector, FVector::ForwardVector }
		};
		for (int32 RingIdx = 0; RingIdx < 3; ++RingIdx)
		{
			const float Spin = TimeSec * (RingIdx == 0 ? 1.8f : -1.4f) + static_cast<float>(RingIdx);
			const FVector UAxis = Axes[RingIdx][0];
			const FVector VAxis = Axes[RingIdx][1];
			const FVector RingNormal = FVector::CrossProduct(UAxis, VAxis).GetSafeNormal();
			for (int32 s = 0; s < CageSegs; ++s)
			{
				const float A0 = Spin + (2.0f * UE_PI * static_cast<float>(s)) / static_cast<float>(CageSegs);
				const float A1 = Spin + (2.0f * UE_PI * static_cast<float>(s + 1)) / static_cast<float>(CageSegs);
				const FVector P0 = EnemyCenter + (UAxis * FMath::Cos(A0) + VAxis * FMath::Sin(A0)) * SnareR;
				const FVector P1 = EnemyCenter + (UAxis * FMath::Cos(A1) + VAxis * FMath::Sin(A1)) * SnareR;
				AppendRibbonSegment(P0, P1, RingNormal, SnareW, SnareColor, SnareColor);
			}
		}
	}

	NetMesh->SetVisibility(true);
	NetMesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
}

// [V3: UJetTendrilAbility — 단발 긴급 탈출 분사]

UJetTendrilAbility::UJetTendrilAbility()
{
	CooldownSeconds = 1.0f;
	ExtendSpeed = 9.0f;
	RetractSpeed = 7.5f;
}

float UJetTendrilAbility::ComputeDashBiomassCost() const
{
	const ISpaceTendrilHostInterface* HostIfc = Host.GetInterface();
	if (!HostIfc)
	{
		return 0.0f;
	}
	const float Usable = FMath::Max(HostIfc->GetTendrilBiomass() - HostIfc->GetTendrilMinGameOverBiomass(), 0.0f);
	const float PercentCost = Usable * (FMath::Clamp(DashUsableBiomassPercent, 0.0f, 50.0f) * 0.01f);
	return FMath::Max(PercentCost, FMath::Max(DashMinBiomassCost, 0.0f));
}

bool UJetTendrilAbility::CanConsumeJetResource() const
{
	const ISpaceTendrilHostInterface* HostIfc = Host.GetInterface();
	if (!HostIfc)
	{
		return false;
	}
	const float Cost = ComputeDashBiomassCost();
	return HostIfc->CanSpendTendrilBiomass(Cost, false);
}

bool UJetTendrilAbility::ConsumeJetResource(float DeltaTime)
{
	return true;
}

bool UJetTendrilAbility::CanActivateAbility() const
{
	return Super::CanActivateAbility() && CanConsumeJetResource();
}

void UJetTendrilAbility::OnAbilityActivated()
{
	ISpaceTendrilHostInterface* HostIfc = Host.GetInterface();
	if (!HostIfc)
	{
		return;
	}

	const float Cost = ComputeDashBiomassCost();
	if (Cost > KINDA_SMALL_NUMBER)
	{
		HostIfc->SpendTendrilBiomass(Cost, false);
	}

	if (UPrimitiveComponent* HostPhys = HostIfc->GetTendrilPhysicsComponent())
	{
		if (HostPhys->IsSimulatingPhysics())
		{
			HostPhys->WakeRigidBody();
			HostPhys->AddImpulse(LockedAimDir * FMath::Max(EmergencyDashImpulseSpeedCmPerSec, 200.0f), NAME_None, true);
		}
	}

	HostIfc->ApplyHostDeformImpulse(-LockedAimDir * 6.5f);
	HostIfc->PostTendrilStatusBanner(
		TEXT("[V3 긴급 탈출!] 고속 분사 추진!"),
		FLinearColor(0.45f, 0.92f, 1.0f, 1.0f),
		1.0f
	);
	ArmHoldLimitTimer(FMath::Max(BurstDurationSeconds, 0.1f));
}

float UJetTendrilAbility::EvaluateHostBodyStretch(float ElapsedTime, FVector& OutStretchDir) const
{
	OutStretchDir = LastTendrilDir;
	if (ExtendAlpha > KINDA_SMALL_NUMBER && IsExtending())
	{
		return BodyStretchRatio * ExtendAlpha;
	}
	return 0.0f;
}

void UJetTendrilAbility::OnActiveTick(
	float DeltaTime,
	const FVector& VisualOrigin,
	const FVector& StartPos,
	const FVector& EndPos,
	float EasedReach)
{
	ISpaceTendrilHostInterface* HostIfc = Host.GetInterface();
	if (!HostIfc)
	{
		return;
	}

	const float ActiveSpawnRate = IsExtending() ? FMath::Clamp(ProjectileSpawnRateScale * 0.85f, 60.0f, 160.0f) : 0.0f;

	// 언리얼 내장 나이아가라(NS_Tendril_SporeJet: bLocalSpace=true + MeshRenderer SM_DustPuffSphere/MI_DustPuff) 구동
	// 카메라가 바라보는 전방(+LockedAimDir)으로 추진하기 위해 분사 이펙트는 후방(-LockedAimDir)을 향합니다.
	if (UFXSystemComponent* FX = HostIfc->GetTendrilFXComponent(ETendrilVisualStyle::SporeJet))
	{
		const FVector JetSprayDir = -LockedAimDir;
		const float ParticleScale = FMath::Max(HostIfc->GetTendrilBodyScale() * FMath::Max(JetParticleToBodySizeRatio, 0.05f), 0.05f);
		FX->SetWorldLocationAndRotation(VisualOrigin + JetSprayDir * 3.0f, JetSprayDir.Rotation());
		FX->SetWorldScale3D(FVector(ParticleScale));
		if (!FX->IsVisible())
		{
			FX->SetVisibility(true);
		}

		if (bForceRestartFX)
		{
			bForceRestartFX = false;
			if (FX->IsActive())
			{
				FX->DeactivateImmediate();
			}
		}

		FX->SetFloatParameter(TEXT("User.TendrilAlpha"), ExtendAlpha);
		FX->SetFloatParameter(TEXT("User.SporeSpawnRate"), ActiveSpawnRate);

		if (!FX->IsActive())
		{
			FX->Activate(true);
		}
	}
}

void UJetTendrilAbility::OnAbilityReturnedToIdle()
{
	if (ISpaceTendrilHostInterface* HostIfc = Host.GetInterface())
	{
		if (UFXSystemComponent* FX = HostIfc->GetTendrilFXComponent(ETendrilVisualStyle::SporeJet))
		{
			FX->SetFloatParameter(TEXT("User.TendrilAlpha"), 0.0f);
			FX->SetFloatParameter(TEXT("User.SporeSpawnRate"), 0.0f);
		}
	}
}
