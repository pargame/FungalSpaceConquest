#include "EnemyMoldManager.h"

#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "SpaceDrifterHUDWidget.h"
#include "SpaceDrifterPawn.h"
#include "SpaceDrifterPlayerController.h"
#include "SpaceEnemyMold.h"
#include "TimerManager.h"

AEnemyMoldManager::AEnemyMoldManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
}

void AEnemyMoldManager::BeginPlay()
{
	Super::BeginPlay();
	PrewarmPool();
}

void AEnemyMoldManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SpawnTimerHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void AEnemyMoldManager::PrewarmPool()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UClass* SpawnCls = EnemyMoldClass ? EnemyMoldClass.Get() : ASpaceEnemyMold::StaticClass();
	const int32 TargetCount = FMath::Max(AbsoluteMaxActiveEnemies, FMath::Max(InitialMaxActiveEnemies, 1));

	while (EnemyPool.Num() < TargetCount)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ASpaceEnemyMold* Enemy = World->SpawnActor<ASpaceEnemyMold>(SpawnCls, FVector::ZeroVector, FRotator::ZeroRotator, Params))
		{
			Enemy->EnterDormant();
			EnemyPool.Add(Enemy);
		}
		else
		{
			break;
		}
	}
}

ASpaceDrifterPawn* AEnemyMoldManager::ResolvePlayerPawn() const
{
	if (CachedPlayerPawn.IsValid())
	{
		return CachedPlayerPawn.Get();
	}
	if (const UWorld* World = GetWorld())
	{
		ASpaceDrifterPawn* Pawn = Cast<ASpaceDrifterPawn>(UGameplayStatics::GetPlayerPawn(World, 0));
		CachedPlayerPawn = Pawn;
		return Pawn;
	}
	return nullptr;
}

void AEnemyMoldManager::ScheduleNextSpawn(float DelaySeconds, bool bForceReset)
{
	if (UWorld* World = GetWorld())
	{
		if (bForceReset || !World->GetTimerManager().IsTimerActive(SpawnTimerHandle))
		{
			World->GetTimerManager().ClearTimer(SpawnTimerHandle);
			World->GetTimerManager().SetTimer(
				SpawnTimerHandle,
				this,
				&AEnemyMoldManager::OnSpawnTimerExpired,
				FMath::Max(DelaySeconds, 0.2f),
				false
			);
		}
	}
}

void AEnemyMoldManager::OnSpawnTimerExpired()
{
	ASpaceDrifterPawn* PlayerPawn = ResolvePlayerPawn();
	if (!PlayerPawn || PlayerPawn->IsRunEnded())
	{
		return;
	}
	if (const ASpaceDrifterPlayerController* PC = Cast<ASpaceDrifterPlayerController>(PlayerPawn->GetController()))
	{
		if (PC->IsInTitleScreen())
		{
			ScheduleNextSpawn(1.0f);
			return;
		}
	}

	TrySpawnOneEnemy(PlayerPawn);
}

bool AEnemyMoldManager::TrySpawnOneEnemy(ASpaceDrifterPawn* PlayerPawn)
{
	if (!PlayerPawn)
	{
		return false;
	}

	ASpaceEnemyMold* FreeEnemy = nullptr;
	int32 ActiveCount = 0;
	for (ASpaceEnemyMold* Enemy : EnemyPool)
	{
		if (!IsValid(Enemy))
		{
			continue;
		}
		if (Enemy->IsDormant())
		{
			if (!FreeEnemy)
			{
				FreeEnemy = Enemy;
			}
		}
		else
		{
			++ActiveCount;
		}
	}

	const int32 TargetMaxActive = GetCurrentTargetActiveEnemyCount(PlayerPawn);
	if (!FreeEnemy || ActiveCount >= TargetMaxActive)
	{
		return false;
	}

	FVector CamLoc, CamFwd, CamRight, CamUp;
	PlayerPawn->GetTendrilCameraBasis(CamLoc, CamFwd, CamRight, CamUp);

	// 시야 뒤쪽/측면 반구에서 출현시켜 갑작스러운 팝인을 방지
	const float SideSign = FMath::RandBool() ? 1.0f : -1.0f;
	FVector SpawnDir = (-CamFwd * FMath::FRandRange(0.55f, 0.95f)
		+ CamRight * (SideSign * FMath::FRandRange(0.35f, 0.85f))
		+ CamUp * FMath::FRandRange(-0.35f, 0.35f)).GetSafeNormal();
	if (SpawnDir.IsNearlyZero())
	{
		SpawnDir = -PlayerPawn->GetActorForwardVector();
	}

	const float MinDist = FMath::Min(SpawnDistanceRangeCm.X, SpawnDistanceRangeCm.Y);
	const float MaxDist = FMath::Max(SpawnDistanceRangeCm.X, SpawnDistanceRangeCm.Y);
	const float SpawnDist = FMath::FRandRange(MinDist, MaxDist);
	const FVector SpawnLoc = PlayerPawn->GetActorLocation() + SpawnDir * SpawnDist;

	const float ReferenceRadius = PlayerPawn->MaxReachedBodyRadiusCm;
	const float MinMul = FMath::Min(SpawnRadiusMultiplierRange.X, SpawnRadiusMultiplierRange.Y);
	const float MaxMul = FMath::Max(SpawnRadiusMultiplierRange.X, SpawnRadiusMultiplierRange.Y);
	const float RadiusMul = FMath::FRandRange(MinMul, MaxMul);
	const float EnemyRadius = FMath::Clamp(ReferenceRadius * RadiusMul, 45.0f, PlayerPawn->MaxBodyRadiusCm * 1.15f);

	const float RadiusExp = FMath::Clamp(PlayerPawn->BiomassToRadiusExponent, 0.2f, 0.8f);
	const float InvExp = 1.0f / RadiusExp;
	const float BaseRadius = FMath::Max(PlayerPawn->InitialBodyRadiusCm, 10.0f);
	const float EnemyBiomass = FMath::Max(PlayerPawn->InitialBiomass, 10.0f) * FMath::Pow(EnemyRadius / BaseRadius, InvExp);

	FreeEnemy->ActivateAt(this, SpawnLoc, EnemyBiomass, EnemyRadius);
	bHasSpawnedFirstEnemy = true;

	if (const ASpaceDrifterPlayerController* PC = Cast<ASpaceDrifterPlayerController>(PlayerPawn->GetController()))
	{
		if (USpaceDrifterHUDWidget* HUD = PC->GetHUDWidget())
		{
			HUD->SetStatusBannerMessage(
				TEXT("[경고] 포식자 곰팡이가 당신을 추적하기 시작했습니다!"),
				FLinearColor(1.0f, 0.30f, 0.38f, 1.0f),
				2.6f
			);
		}
	}

	return true;
}

void AEnemyMoldManager::NotifyEnemyDefeated(ASpaceEnemyMold* DefeatedEnemy)
{
	AttackTicketHolders.RemoveAll([DefeatedEnemy](const FAttackTicketEntry& Entry)
	{
		return !Entry.Enemy.IsValid() || Entry.Enemy.Get() == DefeatedEnemy;
	});
	const float NextDelay = (GetActiveEnemyCount() == 0)
		? FMath::Max(EmptyFieldRespawnCooldownSeconds, 0.5f)
		: DefeatRespawnCooldownSeconds;
	ScheduleNextSpawn(NextDelay, true);
}

bool AEnemyMoldManager::HasAttackTicket(const ASpaceEnemyMold* Enemy) const
{
	if (!IsValid(Enemy))
	{
		return false;
	}
	for (const FAttackTicketEntry& Entry : AttackTicketHolders)
	{
		if (Entry.Enemy.Get() == Enemy)
		{
			return true;
		}
	}
	return false;
}

void AEnemyMoldManager::UpdateAttackTickets(const ASpaceDrifterPawn* PlayerPawn, const FVector& PlayerLoc, float DeltaTime)
{
	const int32 MaxTickets = FMath::Max(MaxAttackingEnemies, 1);

	// 1. 휴면·전염/배양 중인 보유자는 즉시 반납, 사냥 가능 전환은 유예 시간 경과 후 반납
	for (int32 Idx = AttackTicketHolders.Num() - 1; Idx >= 0; --Idx)
	{
		ASpaceEnemyMold* Holder = AttackTicketHolders[Idx].Enemy.Get();
		if (!IsValid(Holder) || Holder->IsDormant()
			|| Holder->GetColonizationState() != EEnemyMoldColonizationState::Uncolonized)
		{
			AttackTicketHolders.RemoveAtSwap(Idx);
			continue;
		}

		if (IsEnemyThreatToPlayer(Holder, PlayerPawn))
		{
			AttackTicketHolders[Idx].NonThreatElapsed = 0.0f;
		}
		else
		{
			AttackTicketHolders[Idx].NonThreatElapsed += DeltaTime;
			if (AttackTicketHolders[Idx].NonThreatElapsed >= TicketNonThreatGraceSeconds)
			{
				AttackTicketHolders.RemoveAtSwap(Idx);
			}
		}
	}

	// 2. MaxAttackingEnemies가 런타임에 줄어든 경우 초과분 정리 (둔화되지 않은 먼 적 우선)
	while (AttackTicketHolders.Num() > MaxTickets)
	{
		int32 RemoveIdx = 0;
		float MaxDistSq = -1.0f;
		for (int32 Idx = 0; Idx < AttackTicketHolders.Num(); ++Idx)
		{
			const ASpaceEnemyMold* Holder = AttackTicketHolders[Idx].Enemy.Get();
			const float DistSq = FVector::DistSquared(Holder->GetActorLocation(), PlayerLoc);
			const float Score = Holder->IsWebSlowed() ? (DistSq * 0.01f) : DistSq;
			if (Score > MaxDistSq)
			{
				MaxDistSq = Score;
				RemoveIdx = Idx;
			}
		}
		AttackTicketHolders.RemoveAtSwap(RemoveIdx);
	}

	// 3. 비보유 위협 적 후보군 수집 후 거리 오름차순 정렬
	struct FCandidateInfo
	{
		ASpaceEnemyMold* Enemy = nullptr;
		float DistCm = 0.0f;
	};
	TArray<FCandidateInfo> Candidates;
	for (ASpaceEnemyMold* Enemy : EnemyPool)
	{
		if (!IsValid(Enemy) || Enemy->IsDormant() || HasAttackTicket(Enemy))
		{
			continue;
		}
		if (!IsEnemyThreatToPlayer(Enemy, PlayerPawn))
		{
			continue;
		}
		Candidates.Add({ Enemy, static_cast<float>(FVector::Distance(Enemy->GetActorLocation(), PlayerLoc)) });
	}
	Candidates.Sort([](const FCandidateInfo& A, const FCandidateInfo& B)
	{
		return A.DistCm < B.DistCm;
	});

	// 4. 빈 슬롯 보충
	int32 CandidateCursor = 0;
	while (AttackTicketHolders.Num() < MaxTickets && CandidateCursor < Candidates.Num())
	{
		FAttackTicketEntry NewEntry;
		NewEntry.Enemy = Candidates[CandidateCursor++].Enemy;
		NewEntry.NonThreatElapsed = 0.0f;
		AttackTicketHolders.Add(NewEntry);
	}

	// 5. 만석 시 히스테리시스 기반 교체 (V2 둔화 중인 보유자는 교체 대상에서 보호, 둔화 해제 시 즉시 복귀)
	while (CandidateCursor < Candidates.Num())
	{
		int32 FarthestSwappableIdx = INDEX_NONE;
		float FarthestDistCm = -1.0f;
		for (int32 Idx = 0; Idx < AttackTicketHolders.Num(); ++Idx)
		{
			const ASpaceEnemyMold* Holder = AttackTicketHolders[Idx].Enemy.Get();
			if (!IsValid(Holder) || Holder->IsWebSlowed())
			{
				continue;
			}
			const float DistCm = FVector::Distance(Holder->GetActorLocation(), PlayerLoc);
			if (DistCm > FarthestDistCm)
			{
				FarthestDistCm = DistCm;
				FarthestSwappableIdx = Idx;
			}
		}

		if (FarthestSwappableIdx == INDEX_NONE)
		{
			break;
		}

		const FCandidateInfo& ClosestCandidate = Candidates[CandidateCursor];
		if (FarthestDistCm > ClosestCandidate.DistCm + TicketSwapHysteresisCm)
		{
			AttackTicketHolders[FarthestSwappableIdx].Enemy = ClosestCandidate.Enemy;
			AttackTicketHolders[FarthestSwappableIdx].NonThreatElapsed = 0.0f;
			++CandidateCursor;
		}
		else
		{
			break;
		}
	}
}

void AEnemyMoldManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	ASpaceDrifterPawn* PlayerPawn = ResolvePlayerPawn();
	if (!PlayerPawn)
	{
		return;
	}

	if (PlayerPawn->IsRunEnded())
	{
		AttackTicketHolders.Reset();
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(SpawnTimerHandle);
		}
		for (ASpaceEnemyMold* Enemy : EnemyPool)
		{
			if (IsValid(Enemy) && !Enemy->IsDormant())
			{
				Enemy->EnterDormant();
			}
		}
		return;
	}

	if (const ASpaceDrifterPlayerController* PC = Cast<ASpaceDrifterPlayerController>(PlayerPawn->GetController()))
	{
		if (PC->IsInTitleScreen())
		{
			return;
		}
	}

	if (!bInitialSpawnScheduled)
	{
		bInitialSpawnScheduled = true;
		ScheduleNextSpawn(InitialSpawnDelaySeconds);
	}
	else
	{
		const int32 ActiveNow = GetActiveEnemyCount();
		const int32 TargetMax = GetCurrentTargetActiveEnemyCount(PlayerPawn);
		if (ActiveNow < TargetMax)
		{
			if (ActiveNow == 0 && bHasSpawnedFirstEnemy)
			{
				const float FastDelay = FMath::Max(EmptyFieldRespawnCooldownSeconds, 0.5f);
				const UWorld* CurWorld = GetWorld();
				const float Remaining = (CurWorld && CurWorld->GetTimerManager().IsTimerActive(SpawnTimerHandle))
					? CurWorld->GetTimerManager().GetTimerRemaining(SpawnTimerHandle)
					: -1.0f;
				if (Remaining < 0.0f || Remaining > FastDelay + 0.1f)
				{
					ScheduleNextSpawn(FastDelay, true);
				}
			}
			else
			{
				ScheduleNextSpawn(ExtraEnemySpawnIntervalSeconds);
			}
		}
	}

	const FVector PlayerLoc = PlayerPawn->GetActorLocation();
	const FVector PlayerVel = PlayerPawn->GetTendrilBodyVelocity();
	const float PlayerRadius = PlayerPawn->GetTendrilBodyRadius();
	FVector CamLoc = PlayerLoc;
	FVector UnusedCamFwd, UnusedCamRight, UnusedCamUp;
	PlayerPawn->GetTendrilCameraBasis(CamLoc, UnusedCamFwd, UnusedCamRight, UnusedCamUp);
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;

	UpdateAttackTickets(PlayerPawn, PlayerLoc, DeltaTime);

	const float StandoffMin = FMath::Min(StandoffSurfaceDistanceMinCm, StandoffSurfaceDistanceMaxCm);
	const float StandoffMax = FMath::Max(StandoffSurfaceDistanceMinCm, StandoffSurfaceDistanceMaxCm);
	const float StandoffMid = 0.5f * (StandoffMin + StandoffMax);
	const float StandoffHalfSpan = FMath::Max(0.5f * (StandoffMax - StandoffMin), 100.0f);

	for (int32 PoolIndex = 0; PoolIndex < EnemyPool.Num(); ++PoolIndex)
	{
		ASpaceEnemyMold* Enemy = EnemyPool[PoolIndex];
		if (!IsValid(Enemy) || Enemy->IsDormant())
		{
			continue;
		}

		// 배양 폭발 대기 중인 적은 추격·흡수 중단
		if (Enemy->GetColonizationState() == EEnemyMoldColonizationState::ColonizedIncubating)
		{
			continue;
		}

		const FVector EnemyLoc = Enemy->GetActorLocation();
		const float DistToPlayer = FVector::Distance(EnemyLoc, PlayerLoc);
		const float EnemyRadius = Enemy->GetCurrentRadiusCm();

		// 카메라가 포식자(WPO 가시 포함) 내부/초근접 영역에 진입하면 시각 메시만 임시 숨겨 화면 가림·어지럼 방지
		const float CamHideThreshold = EnemyRadius * FMath::Max(CameraOcclusionHideRadiusMultiplier, 1.0f) + 45.0f;
		const bool bCameraInsideEnemy = FVector::DistSquared(CamLoc, EnemyLoc) < FMath::Square(CamHideThreshold);
		Enemy->SetCameraProximityHidden(bCameraInsideEnemy);

		const float SurfaceDist = DistToPlayer - PlayerRadius - EnemyRadius;
		const bool bEnemyIsThreat = IsEnemyThreatToPlayer(Enemy, PlayerPawn);
		Enemy->SetHuntableVisual(!bEnemyIsThreat);

		// V1 전염 진행 중에는 대기 링으로 밀려나지 않도록 조향을 일시 정지
		if (Enemy->GetColonizationState() != EEnemyMoldColonizationState::Colonizing)
		{
			if (USphereComponent* EnemySphere = Enemy->GetCollisionSphere())
			{
				if (EnemySphere->IsSimulatingPhysics())
				{
					const float SpeedMul = Enemy->GetMoveSpeedMultiplier();
					const float EffectiveMaxSpeed = FMath::Max(MaxChaseSpeedCmPerSec * SpeedMul, 40.0f);
					const float EffectiveAccel = ChaseAcceleration * SpeedMul;

					// 모든 활성 포식자(공격 티켓 보유자 포함) 간 최소 표면 이격 거리 유지 반발 벡터 산출
					FVector SeparationVector = FVector::ZeroVector;
					if (StandoffSeparationRadiusCm > KINDA_SMALL_NUMBER && EnemySeparationWeight > KINDA_SMALL_NUMBER)
					{
						for (const ASpaceEnemyMold* Other : EnemyPool)
						{
							if (Other == Enemy || !IsValid(Other) || Other->IsDormant())
							{
								continue;
							}
							const FVector Diff = EnemyLoc - Other->GetActorLocation();
							const float OtherDist = Diff.Size();
							const float MinSep = StandoffSeparationRadiusCm + EnemyRadius + Other->GetCurrentRadiusCm();
							if (OtherDist < MinSep)
							{
								const FVector PushDir = (OtherDist > KINDA_SMALL_NUMBER)
									? (Diff / OtherDist)
									: ((PoolIndex % 2 == 0) ? FVector::RightVector : -FVector::RightVector);
								const float OverlapAlpha = 1.0f - (OtherDist / FMath::Max(MinSep, 1.0f));
								SeparationVector += PushDir * FMath::Clamp(OverlapAlpha * 1.4f, 0.1f, 1.4f);
							}
						}
					}

					if (HasAttackTicket(Enemy))
					{
						// 공격 티켓 보유자: 리드 추격(Lead Pursuit) 돌진 + 서로 뭉치지 않도록 측면 분산 반발
						const float LeadScale = FMath::Clamp(DistToPlayer / FMath::Max(EffectiveMaxSpeed, 100.0f), 0.0f, LeadPursuitSeconds);
						const FVector PredictedTarget = PlayerLoc + PlayerVel * LeadScale;
						const FVector RawChaseDir = (PredictedTarget - EnemyLoc).GetSafeNormal();
						const FVector ChaseDir = (RawChaseDir + SeparationVector * (EnemySeparationWeight * 0.75f)).GetSafeNormal();

						if (!ChaseDir.IsNearlyZero())
						{
							EnemySphere->AddForce(ChaseDir * EffectiveAccel, NAME_None, true);
						}
					}
					else
					{
						// 비티켓 적(대기 포식자·사냥 가능 적): 넓은 포위 링(Standoff) 거리를 유지하며 선회 추종
						FVector RadialFromPlayer = (EnemyLoc - PlayerLoc).GetSafeNormal();
						if (RadialFromPlayer.IsNearlyZero())
						{
							RadialFromPlayer = FVector::ForwardVector;
						}

						const FVector RefAxis = (PoolIndex % 3 == 0)
							? FVector::UpVector
							: ((PoolIndex % 3 == 1) ? FVector::RightVector : FVector::ForwardVector);
						FVector OrbitDir = FVector::CrossProduct(RadialFromPlayer, RefAxis).GetSafeNormal();
						if (OrbitDir.IsNearlyZero())
						{
							OrbitDir = FVector::CrossProduct(RadialFromPlayer, FVector::UpVector).GetSafeNormal();
						}
						if (PoolIndex % 2 != 0)
						{
							OrbitDir = -OrbitDir;
						}

						if (SurfaceDist > StandoffMax)
						{
							const float LeadScale = FMath::Clamp(DistToPlayer / FMath::Max(EffectiveMaxSpeed, 100.0f), 0.0f, LeadPursuitSeconds);
							const FVector PredictedTarget = PlayerLoc + PlayerVel * LeadScale;
							const FVector ApproachDir = ((PredictedTarget - EnemyLoc).GetSafeNormal() + SeparationVector * EnemySeparationWeight).GetSafeNormal();
							if (!ApproachDir.IsNearlyZero())
							{
								EnemySphere->AddForce(ApproachDir * EffectiveAccel, NAME_None, true);
							}
						}
						else if (SurfaceDist < StandoffMin)
						{
							const FVector RetreatDir = (RadialFromPlayer * 0.85f + OrbitDir * 0.45f + SeparationVector * EnemySeparationWeight).GetSafeNormal();
							if (!RetreatDir.IsNearlyZero())
							{
								EnemySphere->AddForce(RetreatDir * EffectiveAccel, NAME_None, true);
							}
						}
						else
						{
							const float RadialCorrection = FMath::Clamp((StandoffMid - SurfaceDist) / StandoffHalfSpan, -1.0f, 1.0f);
							const FVector DesiredOrbitDir = (RadialFromPlayer * (RadialCorrection * 0.65f)
								+ OrbitDir * 0.85f
								+ SeparationVector * EnemySeparationWeight).GetSafeNormal();
							const float RingSpeed = FMath::Max(EffectiveMaxSpeed * FMath::Clamp(StandoffOrbitSpeedRatio, 0.1f, 1.5f), 40.0f);
							const FVector CurVel = EnemySphere->GetPhysicsLinearVelocity();
							const FVector DesiredVel = PlayerVel.GetClampedToMaxSize(EffectiveMaxSpeed) + DesiredOrbitDir * RingSpeed;
							const FVector VelError = DesiredVel - CurVel;
							if (!VelError.IsNearlyZero())
							{
								EnemySphere->AddForce(VelError.GetSafeNormal() * EffectiveAccel, NAME_None, true);
							}
						}
					}

					// 최소 거리 침범 시 별도의 직접 반발 가속도를 추가해 겹침을 빠르게 해소
					if (!SeparationVector.IsNearlyZero())
					{
						const FVector SepAccel = SeparationVector.GetClampedToMaxSize(1.5f) * (EffectiveAccel * EnemySeparationWeight * 0.65f);
						EnemySphere->AddForce(SepAccel, NAME_None, true);
					}

					const FVector CurVel = EnemySphere->GetPhysicsLinearVelocity();
					const float CurSpeed = CurVel.Size();
					if (CurSpeed > EffectiveMaxSpeed)
					{
						EnemySphere->SetPhysicsLinearVelocity(CurVel * (EffectiveMaxSpeed / CurSpeed));
					}
				}
			}
		}

		// 거리 기반 접촉 흡수 판정 (사냥 가능 상태이거나 전염 중이면 피격되지 않음; 티켓은 조향만 결정)
		if (bEnemyIsThreat && SurfaceDist <= ContactAbsorbMarginCm)
		{
			const float Usable = FMath::Max(PlayerPawn->CurrentBiomass - PlayerPawn->GetTendrilMinGameOverBiomass(), 0.0f);
			const float DrainRate = FMath::Max(Usable * (ContactDrainUsablePercentPerSec * 0.01f), 14.0f);
			const float DrainAmount = DrainRate * DeltaTime;

			PlayerPawn->SpendTendrilBiomass(DrainAmount, true);

			if (const ASpaceDrifterPlayerController* PC = Cast<ASpaceDrifterPlayerController>(PlayerPawn->GetController()))
			{
				if (USpaceDrifterHUDWidget* HUD = PC->GetHUDWidget())
				{
					HUD->TriggerDamageEdgeFlash(1.0f);
				}
			}

			if (Now - LastDrainBannerTime >= 0.45)
			{
				LastDrainBannerTime = Now;
				const FVector PushDir = (PlayerLoc - EnemyLoc).GetSafeNormal();
				if (!PushDir.IsNearlyZero())
				{
					PlayerPawn->ApplyDeformImpulse(PushDir, 3.2f);
				}
				PlayerPawn->PostTendrilStatusBanner(
					TEXT("[위협!] 포식자 곰팡이에게 흡수당하고 있습니다! (1번 촉수로 소행성을 당겨 탈출)"),
					FLinearColor(1.0f, 0.24f, 0.30f, 1.0f),
					0.6f
				);
			}
		}
	}
}

void AEnemyMoldManager::GatherAllThreatInfos(TArray<FEnemyThreatMarkerInfo>& OutMarkers) const
{
	OutMarkers.Reset();
	const ASpaceDrifterPawn* PlayerPawn = ResolvePlayerPawn();
	if (!PlayerPawn)
	{
		return;
	}

	const FVector PlayerLoc = PlayerPawn->GetActorLocation();
	for (const ASpaceEnemyMold* Enemy : EnemyPool)
	{
		if (!IsValid(Enemy) || Enemy->IsDormant()
			|| Enemy->GetColonizationState() == EEnemyMoldColonizationState::ColonizedIncubating)
		{
			continue;
		}

		FEnemyThreatMarkerInfo Info;
		Info.WorldLocation = Enemy->GetActorLocation();
		Info.RadiusCm = Enemy->GetCurrentRadiusCm();
		Info.DistanceCm = FVector::Distance(PlayerLoc, Info.WorldLocation);
		Info.bIsThreat = IsEnemyThreatToPlayer(Enemy, PlayerPawn);
		Info.bIsWebSlowed = Enemy->IsWebSlowed();
		OutMarkers.Add(Info);
	}
}

int32 AEnemyMoldManager::GetActiveEnemyCount() const
{
	int32 Count = 0;
	for (const ASpaceEnemyMold* Enemy : EnemyPool)
	{
		if (IsValid(Enemy) && !Enemy->IsDormant())
		{
			++Count;
		}
	}
	return Count;
}

int32 AEnemyMoldManager::GetCurrentTargetActiveEnemyCount(const ASpaceDrifterPawn* PlayerPawn) const
{
	const int32 BaseMax = FMath::Max(InitialMaxActiveEnemies, 1);
	const int32 CapMax = FMath::Max(AbsoluteMaxActiveEnemies, BaseMax);
	if (!PlayerPawn)
	{
		return BaseMax;
	}

	const int32 TimeBonus = (SecondsPerAdditionalEnemy > KINDA_SMALL_NUMBER)
		? FMath::FloorToInt(FMath::Max(PlayerPawn->ElapsedSurvivalSeconds, 0.0f) / SecondsPerAdditionalEnemy)
		: 0;
	const int32 MineralBonus = (MineralsPerAdditionalEnemy > 0)
		? (FMath::Max(PlayerPawn->ConsumedMineralCount, 0) / MineralsPerAdditionalEnemy)
		: 0;

	const int32 TotalBonus = bUseMaxOfTimeAndMineralScaling
		? FMath::Max(TimeBonus, MineralBonus)
		: (TimeBonus + MineralBonus);

	return FMath::Clamp(BaseMax + TotalBonus, BaseMax, CapMax);
}

bool AEnemyMoldManager::IsEnemyThreatToPlayer(const ASpaceEnemyMold* Enemy, const ASpaceDrifterPawn* PlayerPawn) const
{
	if (!IsValid(Enemy) || !IsValid(PlayerPawn) || Enemy->IsDormant())
	{
		return false;
	}
	if (Enemy->GetColonizationState() != EEnemyMoldColonizationState::Uncolonized)
	{
		return false;
	}

	// V1으로 전염(사냥) 가능한 상태(필요 생체량 < 가용 생체량)면 위협이 아니며 피격되지 않음
	const float PlayerUsable = FMath::Max(PlayerPawn->CurrentBiomass - PlayerPawn->GetTendrilMinGameOverBiomass(), 0.0f)
		+ PlayerPawn->GetInvestedColonizeBiomass();
	const float RequiredToHunt = Enemy->GetRequiredColonizeBiomass();
	return RequiredToHunt >= PlayerUsable;
}

void AEnemyMoldManager::GatherActiveEnemies(TArray<ASpaceEnemyMold*>& OutEnemies) const
{
	OutEnemies.Reset();
	for (ASpaceEnemyMold* Enemy : EnemyPool)
	{
		if (IsValid(Enemy) && !Enemy->IsDormant())
		{
			OutEnemies.Add(Enemy);
		}
	}
}
