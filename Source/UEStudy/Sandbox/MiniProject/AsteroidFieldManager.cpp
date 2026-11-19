#include "AsteroidFieldManager.h"

#include "Components/SphereComponent.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "ProceduralMeshComponent.h"
#include "SpaceAsteroid.h"
#include "SpaceDrifterHUDWidget.h"
#include "SpaceDrifterPawn.h"
#include "SpaceDrifterPlayerController.h"
#include "SpaceMineral.h"

#if WITH_EDITOR
#include "UObject/UnrealType.h"
#endif

AAsteroidFieldManager::AAsteroidFieldManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
}

void AAsteroidFieldManager::BeginPlay()
{
	Super::BeginPlay();

	SanitizeRuntimeSettings();

	// 레벨에 수동 배치된 기존 소행성(예: 기준 BP_SpaceAsteroid)은 매니저 풀에서 제외해 영구 보존
	ExternalUnmanagedAsteroids.Reset();
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<ASpaceAsteroid> It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				ExternalUnmanagedAsteroids.Add(*It);
			}
		}
	}

	PrewarmPool(PrewarmPoolCount);
	if (bEnableMineralSpawning)
	{
		PrewarmMineralPool(MaxPooledMinerals);
	}

	SpawnSequenceCounter = 0;
	bHadPlayerPawnOnLastScan = (UGameplayStatics::GetPlayerPawn(GetWorld(), 0) != nullptr);
	bHasInitialPopulated = false;

	// 시작 첫 프레임 1회만 영역 D([InitialSpawnInnerRadiusDCm ~ B]) 내부를 랜덤 시딩해 초기 우주 시야 구성
	EvaluateFieldLifecycle(TargetActiveAsteroids, true);
}

void AAsteroidFieldManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseAllActiveImmediate();
	PoolEntries.Reset();
	FreePoolIndices.Reset();
	ActivePoolIndices.Reset();
	PendingDespawnQueue.Reset();
	MineralPoolEntries.Reset();
	ExternalUnmanagedAsteroids.Reset();

	Super::EndPlay(EndPlayReason);
}

void AAsteroidFieldManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bEnableFieldStreaming)
	{
		return;
	}

	// GameMode/Controller 빙의가 BeginPlay보다 한 틱 늦은 경우 플레이어가 스폰되자마자 초기 필드 1회 구성
	const bool bHasPlayerNow = (UGameplayStatics::GetPlayerPawn(GetWorld(), 0) != nullptr);
	if (!bHadPlayerPawnOnLastScan && bHasPlayerNow)
	{
		bHadPlayerPawnOnLastScan = true;
		if (!bHasInitialPopulated)
		{
			EvaluateFieldLifecycle(TargetActiveAsteroids, true);
			return;
		}
	}

	if (bEnableDistancePhysicsTiering)
	{
		StepKinematicAsteroids(DeltaTime);
	}

	const float Interval = FMath::Max(ScanIntervalSeconds, 0.02f);
	if (bDrawDebugField || bEnableDistancePhysicsTiering)
	{
		ScanAccumulator += DeltaTime;
		if (ScanAccumulator >= Interval)
		{
			ScanAccumulator = 0.0f;
			EvaluateFieldLifecycle(MaxSpawnsPerScan, false);
		}

		if (bDrawDebugField)
		{
			FVector PlayerLoc = FVector::ZeroVector;
			FVector FwdDir = FVector::ForwardVector;
			float FwdExt = 0.0f;
			AActor* UnusedLatch = nullptr;
			if (ResolvePlayerFocus(PlayerLoc, FwdDir, FwdExt, UnusedLatch))
			{
				DrawDebugVisualization(PlayerLoc, FwdDir, FwdExt);
			}
		}
	}
	else
	{
		ScanAccumulator = 0.0f;
		EvaluateFieldLifecycle(MaxSpawnsPerScan, false);
	}
}

#if WITH_EDITOR
void AAsteroidFieldManager::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	SanitizeRuntimeSettings();

	const UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || PoolEntries.Num() == 0)
	{
		return;
	}

	const FName PropName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	if (PropName == GET_MEMBER_NAME_CHECKED(AAsteroidFieldManager, WorldSeed))
	{
		ResetFieldWithWorldSeed(WorldSeed);
		return;
	}

	EvaluateFieldLifecycle(MaxSpawnsPerScan, false);
}
#endif

void AAsteroidFieldManager::ForceRefreshField(int32 CustomSpawnBudget)
{
	const int32 Budget = (CustomSpawnBudget >= 0) ? CustomSpawnBudget : MaxSpawnsPerScan;
	EvaluateFieldLifecycle(Budget, false);
}

void AAsteroidFieldManager::ResetFieldWithWorldSeed(int32 NewWorldSeed)
{
	WorldSeed = FMath::Max(NewWorldSeed, 0);
	SpawnSequenceCounter = 0;

	ReleaseAllActiveImmediate();
	TotalSpawnsCount = 0;
	TotalDespawnsCount = 0;
	TotalDespawnRescuesCount = 0;
	bHasInitialPopulated = false;

	EvaluateFieldLifecycle(TargetActiveAsteroids, true);
}

void AAsteroidFieldManager::ReleaseAllActiveImmediate()
{
	for (int32 Idx = 0; Idx < PoolEntries.Num(); ++Idx)
	{
		FAsteroidPoolEntry& Entry = PoolEntries[Idx];
		if (IsValid(Entry.Asteroid))
		{
			Entry.Asteroid->EnterDormantPool();
		}
		Entry.State = EAsteroidPoolState::Free;
		Entry.bPendingDespawn = false;
		Entry.DespawnQueuedTime = 0.0;
	}

	FreePoolIndices.Reset(PoolEntries.Num());
	for (int32 Idx = 0; Idx < PoolEntries.Num(); ++Idx)
	{
		if (IsValid(PoolEntries[Idx].Asteroid))
		{
			FreePoolIndices.Add(Idx);
		}
	}
	ActivePoolIndices.Reset();
	PendingDespawnQueue.Reset();

	for (FMineralPoolEntry& MEntry : MineralPoolEntries)
	{
		if (IsValid(MEntry.Mineral))
		{
			MEntry.Mineral->EnterDormantPool();
		}
		MEntry.bInUse = false;
		MEntry.bIsFloating = false;
		MEntry.bPendingDespawn = false;
		MEntry.DespawnQueuedTime = 0.0;
		MEntry.HostAsteroid.Reset();
	}

	SyncStatsCounters();
}

void AAsteroidFieldManager::SanitizeRuntimeSettings()
{
	InitialSpawnInnerRadiusMultiplier = FMath::Clamp(InitialSpawnInnerRadiusMultiplier, 2.0f, 100.0f);
	InnerNoSpawnRadiusMultiplier = FMath::Clamp(FMath::Max(InnerNoSpawnRadiusMultiplier, InitialSpawnInnerRadiusMultiplier + 5.0f), 10.0f, 2000.0f);
	// C = B - A 껍질 두께가 최소 현재 반지름 × 16 이상 확보되도록 보정
	OuterDespawnRadiusMultiplier = FMath::Clamp(FMath::Max(OuterDespawnRadiusMultiplier, InnerNoSpawnRadiusMultiplier + 16.0f), 26.0f, 3000.0f);
	DespawnGraceSeconds = FMath::Clamp(DespawnGraceSeconds, 0.2f, 15.0f);
	DespawnHardMarginMultiplier = FMath::Clamp(DespawnHardMarginMultiplier, 5.0f, 500.0f);
	MinSpawnSeparationMultiplier = FMath::Clamp(MinSpawnSeparationMultiplier, 1.5f, 50.0f);
	AsteroidSizeGrowthExponent = FMath::Clamp(AsteroidSizeGrowthExponent, 0.0f, 2.0f);
	MinAsteroidRuntimeScale = FMath::Clamp(MinAsteroidRuntimeScale, 0.25f, 2.0f);
	MaxAsteroidRuntimeScale = FMath::Clamp(FMath::Max(MaxAsteroidRuntimeScale, MinAsteroidRuntimeScale), 1.0f, 10.0f);
	ForwardBaseExtensionMultiplier = FMath::Clamp(ForwardBaseExtensionMultiplier, 0.0f, 500.0f);
	ForwardVelocityMaxExtensionMultiplier = FMath::Clamp(ForwardVelocityMaxExtensionMultiplier, 0.0f, 1000.0f);
	VelocityReferenceSpeedCmPerSec = FMath::Clamp(VelocityReferenceSpeedCmPerSec, 100.0f, 10000.0f);
	ForwardInnerExtensionFraction = FMath::Clamp(ForwardInnerExtensionFraction, 0.0f, 1.0f);
	ForwardSpawnBiasWeight = FMath::Clamp(ForwardSpawnBiasWeight, 0.0f, 0.98f);
	DistanceFadeBandCm = FMath::Clamp(DistanceFadeBandCm, 400.0f, FMath::Max(GetEffectiveOuterRadiusB() - 600.0f, 400.0f));

	MaxPooledAsteroids = FMath::Clamp(MaxPooledAsteroids, 8, 2048);
	TargetActiveAsteroids = FMath::Clamp(TargetActiveAsteroids, 8, MaxPooledAsteroids);
	PrewarmPoolCount = FMath::Clamp(PrewarmPoolCount, 0, MaxPooledAsteroids);
	MaxSpawnsPerScan = FMath::Clamp(MaxSpawnsPerScan, 1, 64);
	ScanIntervalSeconds = FMath::Max(ScanIntervalSeconds, 0.02f);

	PhysicsActivationRadiusMultiplier = FMath::Clamp(PhysicsActivationRadiusMultiplier, 10.0f, 500.0f);
	PhysicsHysteresisMultiplier = FMath::Clamp(PhysicsHysteresisMultiplier, 2.0f, 100.0f);
	MaxActivePhysicsAsteroids = FMath::Clamp(MaxActivePhysicsAsteroids, 4, 256);

	MaxPooledMinerals = FMath::Clamp(MaxPooledMinerals, 0, 2048);
	SurfaceMineralChance = FMath::Clamp(SurfaceMineralChance, 0.0f, 1.0f);
	FloatingMineralTargetRatio = FMath::Clamp(FloatingMineralTargetRatio, 0.0f, 1.0f);
	MinAsteroidRadiusForSurfaceMineralCm = FMath::Max(MinAsteroidRadiusForSurfaceMineralCm, 10.0f);

	SyncTickInterval();
}

void AAsteroidFieldManager::SyncTickInterval()
{
	const float DesiredInterval = (bDrawDebugField || bEnableDistancePhysicsTiering) ? 0.0f : FMath::Max(ScanIntervalSeconds, 0.02f);
	SetActorTickInterval(DesiredInterval);
}

UClass* AAsteroidFieldManager::ResolveAsteroidSpawnClass()
{
	if (AsteroidClass)
	{
		return AsteroidClass.Get();
	}

	if (!ResolvedSpawnClass)
	{
		UClass* LoadedBP = StaticLoadClass(
			ASpaceAsteroid::StaticClass(),
			nullptr,
			TEXT("/Game/Sandbox/MiniProject/Blueprints/BP_SpaceAsteroid.BP_SpaceAsteroid_C")
		);
		ResolvedSpawnClass = LoadedBP ? LoadedBP : ASpaceAsteroid::StaticClass();
	}

	return ResolvedSpawnClass.Get();
}

ASpaceAsteroid* AAsteroidFieldManager::SpawnDeferredPooledAsteroid()
{
	UWorld* World = GetWorld();
	UClass* SpawnClass = ResolveAsteroidSpawnClass();
	if (!World || !SpawnClass)
	{
		return nullptr;
	}

	const FTransform HiddenInitTransform(FRotator::ZeroRotator, GetActorLocation());
	ASpaceAsteroid* NewAsteroid = World->SpawnActorDeferred<ASpaceAsteroid>(
		SpawnClass,
		HiddenInitTransform,
		this,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn
	);

	if (NewAsteroid)
	{
		// 대기 풀 슬롯은 실제 시드 활성화 전까지 불필요한 초기 빌드를 생략
		NewAsteroid->SetDeferInitialBuild(true);
		UGameplayStatics::FinishSpawningActor(NewAsteroid, HiddenInitTransform);
		NewAsteroid->SetActorHiddenInGame(true);
		if (UProceduralMeshComponent* MeshComp = NewAsteroid->GetAsteroidMesh())
		{
			MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			MeshComp->SetSimulatePhysics(false);
		}
	}

	return NewAsteroid;
}

void AAsteroidFieldManager::PrewarmPool(int32 TargetFreeCount)
{
	const int32 DesiredCount = FMath::Clamp(TargetFreeCount, 0, MaxPooledAsteroids);
	while (PoolEntries.Num() < DesiredCount)
	{
		const int32 SlotIdx = PoolEntries.Num();
		ASpaceAsteroid* PooledActor = SpawnDeferredPooledAsteroid();
		if (!PooledActor)
		{
			break;
		}

		if (bUsePrebakedSwitchPool)
		{
			// 서로 다른 크기·모양·질감 아키타입을 사전에 굽고 즉시 대기 상태로 전환
			// 풀 절반(홀수 슬롯)은 3단계(1,280 삼각형)로 프리베이크해 시각 품질 유지와 함께 GPU 정점 부하 절감
			PooledActor->VisualSubdivisionLevel = (SlotIdx % 2 == 0) ? 4 : 3;
			const uint32 ArchetypeHash = (static_cast<uint32>(WorldSeed) * 2654435761u) ^ (static_cast<uint32>(SlotIdx + 1) * 2246822519u);
			const int32 ArchetypeSeed = FMath::Max(static_cast<int32>(ArchetypeHash & 0x7FFFFFFFu), 1);
			PooledActor->BakePooledArchetype(ArchetypeSeed);
			++TotalBuildsCount;
		}

		FAsteroidPoolEntry Entry;
		Entry.Asteroid = PooledActor;
		Entry.State = EAsteroidPoolState::Free;
		const int32 AddedIdx = PoolEntries.Add(Entry);
		FreePoolIndices.Add(AddedIdx);
	}

	SyncStatsCounters();
}

int32 AAsteroidFieldManager::AcquireRandomFreeAsteroidSlot()
{
	while (FreePoolIndices.Num() > 0)
	{
		const int32 PickPos = FMath::RandRange(0, FreePoolIndices.Num() - 1);
		const int32 PoolIdx = FreePoolIndices[PickPos];
		FreePoolIndices.RemoveAtSwap(PickPos, EAllowShrinking::No);

		if (PoolEntries.IsValidIndex(PoolIdx)
			&& PoolEntries[PoolIdx].State == EAsteroidPoolState::Free
			&& IsValid(PoolEntries[PoolIdx].Asteroid))
		{
			return PoolIdx;
		}
	}

	if (PoolEntries.Num() < MaxPooledAsteroids)
	{
		if (ASpaceAsteroid* NewActor = SpawnDeferredPooledAsteroid())
		{
			FAsteroidPoolEntry NewEntry;
			NewEntry.Asteroid = NewActor;
			NewEntry.State = EAsteroidPoolState::Free;
			return PoolEntries.Add(NewEntry);
		}
	}

	return INDEX_NONE;
}

void AAsteroidFieldManager::DespawnAsteroidSlot(int32 PoolIdx)
{
	if (!PoolEntries.IsValidIndex(PoolIdx))
	{
		return;
	}

	FAsteroidPoolEntry& Entry = PoolEntries[PoolIdx];
	if (Entry.State != EAsteroidPoolState::Active)
	{
		return;
	}

	if (IsValid(Entry.Asteroid))
	{
		ReleaseMineralsForHostAsteroid(Entry.Asteroid.Get());
		Entry.Asteroid->EnterDormantPool();
	}

	Entry.State = EAsteroidPoolState::Free;
	Entry.bPendingDespawn = false;
	Entry.DespawnQueuedTime = 0.0;

	ActivePoolIndices.RemoveSingleSwap(PoolIdx, EAllowShrinking::No);
	if (IsValid(Entry.Asteroid))
	{
		FreePoolIndices.Add(PoolIdx);
	}
	++TotalDespawnsCount;
}

bool AAsteroidFieldManager::ResolvePlayerFocus(
	FVector& OutPlayerLocation,
	FVector& OutForwardDir,
	float& OutForwardExtensionCm,
	AActor*& OutLatchedTarget) const
{
	const float PlayerRad = GetEffectivePlayerRadiusCm();
	OutLatchedTarget = nullptr;
	OutForwardDir = FVector::ForwardVector;
	OutForwardExtensionCm = ForwardBaseExtensionMultiplier * PlayerRad;

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	FVector CamLoc = FVector::ZeroVector;
	FRotator CamRot = FRotator::ZeroRotator;
	bool bHasCam = false;
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
	{
		PC->GetPlayerViewPoint(CamLoc, CamRot);
		bHasCam = true;
	}

	const FVector CamFwd = bHasCam ? CamRot.Vector().GetSafeNormal() : FVector::ForwardVector;

	if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0))
	{
		OutPlayerLocation = PlayerPawn->GetActorLocation();
		FVector PlayerVel = PlayerPawn->GetVelocity();

		if (const ASpaceDrifterPawn* Drifter = Cast<ASpaceDrifterPawn>(PlayerPawn))
		{
			OutLatchedTarget = Drifter->GetLatchedTargetActor();
			if (USphereComponent* Sphere = Drifter->GetCollisionSphere())
			{
				if (Sphere->IsSimulatingPhysics())
				{
					PlayerVel = Sphere->GetPhysicsLinearVelocity();
				}
			}
		}

		const float Speed = PlayerVel.Size();
		const float SpeedRatio = FMath::Clamp(Speed / FMath::Max(VelocityReferenceSpeedCmPerSec, 100.0f), 0.0f, 1.0f);
		const float VelAlignWeight = FMath::SmoothStep(50.0f, 380.0f, Speed);
		const FVector VelDir = (Speed > 50.0f) ? (PlayerVel / Speed) : CamFwd;

		OutForwardDir = FMath::Lerp(CamFwd, VelDir, VelAlignWeight).GetSafeNormal();
		if (OutForwardDir.IsNearlyZero())
		{
			OutForwardDir = CamFwd.IsNearlyZero() ? FVector::ForwardVector : CamFwd;
		}
		OutForwardExtensionCm = (ForwardBaseExtensionMultiplier + ForwardVelocityMaxExtensionMultiplier * SpeedRatio) * PlayerRad;
		return true;
	}

	if (bHasCam && !CamLoc.IsNearlyZero())
	{
		OutPlayerLocation = CamLoc;
		OutForwardDir = CamFwd.IsNearlyZero() ? FVector::ForwardVector : CamFwd;
		OutForwardExtensionCm = ForwardBaseExtensionMultiplier * PlayerRad;
		return true;
	}

	OutPlayerLocation = GetActorLocation();
	OutForwardDir = GetActorForwardVector().GetSafeNormal();
	OutForwardExtensionCm = ForwardBaseExtensionMultiplier * PlayerRad;
	return true;
}

float AAsteroidFieldManager::GetPlayerGrowthRatio(float* OutInitRadiusCm) const
{
	const float FallbackRad = FMath::Max(FallbackPlayerRadiusCm, 10.0f);
	if (OutInitRadiusCm)
	{
		*OutInitRadiusCm = FallbackRad;
	}

	if (const UWorld* World = GetWorld())
	{
		if (const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0))
		{
			if (const ASpaceDrifterPawn* Drifter = Cast<ASpaceDrifterPawn>(PlayerPawn))
			{
				const float InitRad = FMath::Max(Drifter->InitialBodyRadiusCm, 10.0f);
				const float CurRad = FMath::Max(Drifter->GetTendrilBodyRadius(), 10.0f);
				if (OutInitRadiusCm)
				{
					*OutInitRadiusCm = InitRad;
				}
				return FMath::Clamp(CurRad / InitRad, 0.25f, 12.0f);
			}
		}
	}

	return 1.0f;
}

float AAsteroidFieldManager::GetEffectiveAsteroidSpawnScale() const
{
	if (!bScaleSpawnedAsteroidsToPlayer)
	{
		return 1.0f;
	}

	const float Ratio = GetPlayerGrowthRatio();
	const float RawScale = FMath::Pow(Ratio, FMath::Max(AsteroidSizeGrowthExponent, 0.0f));
	const float SafeMin = FMath::Clamp(MinAsteroidRuntimeScale, 0.25f, 2.0f);
	const float SafeMax = FMath::Clamp(FMath::Max(MaxAsteroidRuntimeScale, SafeMin), 1.0f, 10.0f);
	return FMath::Clamp(RawScale, SafeMin, SafeMax);
}

float AAsteroidFieldManager::GetEffectivePlayerRadiusCm() const
{
	const float FallbackRad = FMath::Max(FallbackPlayerRadiusCm, 10.0f);
	if (!bScaleFieldRadiiToPlayer)
	{
		return FallbackRad;
	}

	float InitRad = FallbackRad;
	const float Ratio = GetPlayerGrowthRatio(&InitRad);
	return InitRad * FMath::Pow(Ratio, FMath::Max(FieldRadiiGrowthExponent, 0.0f));
}

float AAsteroidFieldManager::GetEffectiveInnerRadiusA() const
{
	return InnerNoSpawnRadiusMultiplier * GetEffectivePlayerRadiusCm();
}

float AAsteroidFieldManager::GetEffectiveOuterRadiusB() const
{
	return OuterDespawnRadiusMultiplier * GetEffectivePlayerRadiusCm();
}

float AAsteroidFieldManager::GetEffectiveDespawnHardMarginCm() const
{
	return DespawnHardMarginMultiplier * GetEffectivePlayerRadiusCm();
}

float AAsteroidFieldManager::GetEffectiveMinSpawnSeparationCm() const
{
	const float FieldScaledSep = MinSpawnSeparationMultiplier * GetEffectivePlayerRadiusCm();
	const float AsteroidScaledSep = MinSpawnSeparationMultiplier * FMath::Max(FallbackPlayerRadiusCm, 10.0f) * GetEffectiveAsteroidSpawnScale();
	return FMath::Max(FieldScaledSep, AsteroidScaledSep);
}

void AAsteroidFieldManager::ComputeDirectionalRadii(
	const FVector& OffsetFromPlayer,
	const FVector& ForwardDir,
	float ForwardExtensionCm,
	float& OutMinSpawnRadiusCm,
	float& OutMaxDespawnRadiusCm) const
{
	const float PlayerRad = GetEffectivePlayerRadiusCm();
	const float Dist = OffsetFromPlayer.Size();
	const float ForwardAlign = (Dist > KINDA_SMALL_NUMBER)
		? FVector::DotProduct(OffsetFromPlayer / Dist, ForwardDir)
		: 0.0f;

	// 전방 반구(-0.20 ~ +0.85) 구간에 걸쳐 A와 B를 진행 방향으로 확장 (후방은 기본 A/B 고정)
	const float ForwardLobe = FMath::SmoothStep(-0.20f, 0.85f, ForwardAlign);
	const float OuterExt = FMath::Max(ForwardExtensionCm, 0.0f) * ForwardLobe;
	const float InnerExt = OuterExt * FMath::Clamp(ForwardInnerExtensionFraction, 0.0f, 1.0f);

	OutMinSpawnRadiusCm = (InnerNoSpawnRadiusMultiplier * PlayerRad) + InnerExt;
	OutMaxDespawnRadiusCm = FMath::Max((OuterDespawnRadiusMultiplier * PlayerRad) + OuterExt, OutMinSpawnRadiusCm + 16.0f * PlayerRad);
}

FVector AAsteroidFieldManager::SampleDirectionInShell(
	const FVector& ForwardDir,
	bool bUseForwardBias,
	FRandomStream& Stream) const
{
	if (!bUseForwardBias || ForwardSpawnBiasWeight <= KINDA_SMALL_NUMBER || Stream.GetFraction() > ForwardSpawnBiasWeight)
	{
		FVector UnitDir = Stream.VRand();
		return UnitDir.IsNearlyZero() ? FVector::ForwardVector : UnitDir.GetSafeNormal();
	}

	// 전방 반구(코사인 가중 원뿔: cos(theta) in [-0.08 ~ 1.0], 전방 중심 밀집)로 높은 가중치 샘플링
	const FVector SafeFwd = ForwardDir.IsNearlyZero() ? FVector::ForwardVector : ForwardDir.GetSafeNormal();
	const FVector UpRef = (FMath::Abs(SafeFwd.Z) < 0.92f) ? FVector::UpVector : FVector::RightVector;
	const FVector BasisRight = FVector::CrossProduct(UpRef, SafeFwd).GetSafeNormal();
	const FVector BasisUp = FVector::CrossProduct(SafeFwd, BasisRight).GetSafeNormal();

	const float U = Stream.GetFraction();
	const float CosTheta = FMath::Lerp(-0.08f, 1.0f, FMath::Pow(U, 0.58f));
	const float SinTheta = FMath::Sqrt(FMath::Max(0.0f, 1.0f - CosTheta * CosTheta));
	const float Phi = Stream.FRandRange(0.0f, UE_TWO_PI);

	const FVector Dir = SafeFwd * CosTheta + (BasisRight * FMath::Cos(Phi) + BasisUp * FMath::Sin(Phi)) * SinTheta;
	return Dir.IsNearlyZero() ? SafeFwd : Dir.GetSafeNormal();
}

FVector AAsteroidFieldManager::SampleVolumeUniformInShell(
	const FVector& Center,
	const FVector& ForwardDir,
	float ForwardExtensionCm,
	bool bInitialPopulate,
	float BaseMinRadiusCm,
	float BaseMaxRadiusCm,
	FRandomStream& Stream) const
{
	const FVector UnitDir = SampleDirectionInShell(ForwardDir, !bInitialPopulate, Stream);

	float DirMinR = BaseMinRadiusCm;
	float DirMaxR = BaseMaxRadiusCm;
	if (!bInitialPopulate)
	{
		ComputeDirectionalRadii(UnitDir * 100.0f, ForwardDir, ForwardExtensionCm, DirMinR, DirMaxR);
	}
	else
	{
		float UnusedMin = 0.0f;
		ComputeDirectionalRadii(UnitDir * 100.0f, ForwardDir, ForwardBaseExtensionMultiplier * GetEffectivePlayerRadiusCm(), UnusedMin, DirMaxR);
	}

	const double SafeMin = FMath::Max(static_cast<double>(DirMinR), 0.0);
	const double SafeMax = FMath::Max(static_cast<double>(DirMaxR), SafeMin + 10.0);
	// 시작 시와 비행 중 공간 밀도가 완벽히 균일하도록 초기 배치·런타임 모두 체적 균등(R^3) 분포를 사용합니다.
	const double RMinCubed = SafeMin * SafeMin * SafeMin;
	const double RMaxCubed = SafeMax * SafeMax * SafeMax;
	const double Radius = FMath::Pow(FMath::Lerp(RMinCubed, RMaxCubed, static_cast<double>(Stream.GetFraction())), 1.0 / 3.0);

	return Center + UnitDir * Radius;
}

bool AAsteroidFieldManager::IsSpawnLocationClear(
	const FVector& CandidateLocation,
	const FVector& PlayerLocation,
	const FVector& ForwardDir,
	float ForwardExtensionCm,
	float BaseMinRadiusCm,
	bool bInitialPopulate,
	const TArray<FVector>& ActiveLocations) const
{
	const FVector Offset = CandidateLocation - PlayerLocation;
	float EffectiveMinClearCm = BaseMinRadiusCm;
	if (!bInitialPopulate)
	{
		float UnusedMaxR = 0.0f;
		ComputeDirectionalRadii(Offset, ForwardDir, ForwardExtensionCm, EffectiveMinClearCm, UnusedMaxR);
		EffectiveMinClearCm = FMath::Max(EffectiveMinClearCm, GetEffectiveInnerRadiusA());
	}

	// [규칙 1] 플레이어 반경 A(전방 이동 시에는 확장된 A_eff) 내부 생성 원천 차단
	if (Offset.SizeSquared() < FMath::Square(static_cast<double>(EffectiveMinClearCm)))
	{
		return false;
	}

	const double MinSepSq = FMath::Square(static_cast<double>(GetEffectiveMinSpawnSeparationCm()));
	for (const TWeakObjectPtr<ASpaceAsteroid>& ExtPtr : ExternalUnmanagedAsteroids)
	{
		if (const ASpaceAsteroid* Ext = ExtPtr.Get())
		{
			if (FVector::DistSquared(CandidateLocation, Ext->GetActorLocation()) < MinSepSq)
			{
				return false;
			}
		}
	}

	for (const FVector& ActiveLoc : ActiveLocations)
	{
		if (FVector::DistSquared(CandidateLocation, ActiveLoc) < MinSepSq)
		{
			return false;
		}
	}

	return true;
}

bool AAsteroidFieldManager::SpawnAsteroidInShell(
	const FVector& PlayerLocation,
	const FVector& ForwardDir,
	float ForwardExtensionCm,
	float BaseMinRadiusCm,
	float BaseMaxRadiusCm,
	bool bImmediateFade,
	TArray<FVector>& InOutActiveLocations,
	FRandomStream& Stream)
{
	FVector SpawnLoc = FVector::ZeroVector;
	bool bFoundSpot = false;
	for (int32 Attempt = 0; Attempt < 6; ++Attempt)
	{
		const FVector Candidate = SampleVolumeUniformInShell(
			PlayerLocation,
			ForwardDir,
			ForwardExtensionCm,
			bImmediateFade,
			BaseMinRadiusCm,
			BaseMaxRadiusCm,
			Stream
		);
		if (IsSpawnLocationClear(Candidate, PlayerLocation, ForwardDir, ForwardExtensionCm, BaseMinRadiusCm, bImmediateFade, InOutActiveLocations))
		{
			SpawnLoc = Candidate;
			bFoundSpot = true;
			break;
		}
	}

	if (!bFoundSpot)
	{
		return false;
	}

	const int32 PoolIdx = AcquireRandomFreeAsteroidSlot();
	if (PoolIdx == INDEX_NONE)
	{
		return false;
	}

	const uint32 SeedMix = (static_cast<uint32>(WorldSeed) * 2654435761u)
		^ (static_cast<uint32>(++SpawnSequenceCounter) * 2246822519u)
		^ 0x9E3779B9u;
	const int32 NewSeed = FMath::Max(static_cast<int32>(SeedMix & 0x7FFFFFFFu), 1);

	const FRotator RandomRot(
		Stream.FRandRange(-85.0f, 85.0f),
		Stream.FRandRange(0.0f, 360.0f),
		Stream.FRandRange(-180.0f, 180.0f)
	);

	const float FadeEndCm = FMath::Max(GetEffectiveOuterRadiusB() - 200.0f, 1200.0f);
	const float FadeStartCm = FMath::Max(FadeEndCm - DistanceFadeBandCm, 500.0f);

	const float SpawnScale = GetEffectiveAsteroidSpawnScale();
	FAsteroidPoolEntry& Entry = PoolEntries[PoolIdx];
	if (Entry.Asteroid)
	{
		const float MinPlayerClearanceCm = Entry.Asteroid->GetBaseGeneratedRadiusCm() * SpawnScale + 450.0f;
		const FVector ToSpawn = SpawnLoc - PlayerLocation;
		const float DistToPlayer = ToSpawn.Size();
		if (DistToPlayer < MinPlayerClearanceCm)
		{
			const FVector SafeOutDir = (DistToPlayer > KINDA_SMALL_NUMBER) ? (ToSpawn / DistToPlayer) : FVector::ForwardVector;
			SpawnLoc = PlayerLocation + SafeOutDir * MinPlayerClearanceCm;
		}
	}

	Entry.State = EAsteroidPoolState::Active;
	Entry.bPendingDespawn = false;
	Entry.DespawnQueuedTime = 0.0;
	ActivePoolIndices.Add(PoolIdx);

	const bool bHadPrebakedMesh = bUsePrebakedSwitchPool && Entry.Asteroid->HasBuiltGeometry();
	Entry.Asteroid->SetDistanceFadeRange(FadeStartCm, FadeEndCm);
	if (bOverrideAsteroidSpeedFromFieldManager)
	{
		Entry.Asteroid->ApplyFieldSpeedOverrides(
			AsteroidLinearSpeedRange,
			AsteroidLinearSpeedDistributionExponent,
			AsteroidAngularSpeedDegRange,
			AsteroidAngularSpeedDistributionExponent
		);
	}
	Entry.Asteroid->ActivatePooledWithSeed(
		NewSeed,
		FTransform(RandomRot, SpawnLoc, FVector(SpawnScale)),
		bUsePrebakedSwitchPool,
		bImmediateFade
	);
	if (bEnableDistancePhysicsTiering)
	{
		const bool bNearPlayer = FVector::DistSquared(SpawnLoc, PlayerLocation) <= FMath::Square(static_cast<double>(PhysicsActivationRadiusMultiplier * GetEffectivePlayerRadiusCm()));
		Entry.Asteroid->SetPhysicsSimulationTier(bNearPlayer);
	}

	if (!bHadPrebakedMesh)
	{
		++TotalBuildsCount;
	}

	InOutActiveLocations.Add(SpawnLoc);
	++TotalSpawnsCount;

	if (bEnableMineralSpawning)
	{
		SpawnSurfaceMineralsForAsteroid(Entry.Asteroid.Get(), NewSeed);
	}

	return true;
}

void AAsteroidFieldManager::EvaluateFieldLifecycle(int32 SpawnBudget, bool bInitialPopulate)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FVector PlayerLocation = FVector::ZeroVector;
	FVector ForwardDir = FVector::ForwardVector;
	float ForwardExtensionCm = 0.0f;
	AActor* LatchedTarget = nullptr;
	if (!ResolvePlayerFocus(PlayerLocation, ForwardDir, ForwardExtensionCm, LatchedTarget))
	{
		return;
	}

	const double NowTime = World->GetTimeSeconds();

	// 1단계 [규칙 5·6]: 활성 소행성의 방향별 외곽 반경 B_eff 이탈 여부를 검사하고,
	// > B_eff 이면 삭제 대기 큐에 추가 / 다시 <= B_eff 로 가까워지면 큐에서 즉시 복귀
	for (int32 Idx = ActivePoolIndices.Num() - 1; Idx >= 0; --Idx)
	{
		const int32 PoolIdx = ActivePoolIndices[Idx];
		if (!PoolEntries.IsValidIndex(PoolIdx)
			|| PoolEntries[PoolIdx].State != EAsteroidPoolState::Active
			|| !IsValid(PoolEntries[PoolIdx].Asteroid))
		{
			ActivePoolIndices.RemoveAtSwap(Idx, EAllowShrinking::No);
			continue;
		}

		FAsteroidPoolEntry& Entry = PoolEntries[PoolIdx];
		const FVector Offset = Entry.Asteroid->GetActorLocation() - PlayerLocation;
		float UnusedMinR = 0.0f;
		float DirOuterB = GetEffectiveOuterRadiusB();
		ComputeDirectionalRadii(Offset, ForwardDir, ForwardExtensionCm, UnusedMinR, DirOuterB);

		const double DistSq = Offset.SizeSquared();
		const double OuterBSq = FMath::Square(static_cast<double>(DirOuterB));

		if (DistSq <= OuterBSq)
		{
			if (Entry.bPendingDespawn)
			{
				Entry.bPendingDespawn = false;
				Entry.DespawnQueuedTime = 0.0;
				++TotalDespawnRescuesCount;
			}
		}
		else if (!Entry.bPendingDespawn)
		{
			Entry.bPendingDespawn = true;
			Entry.DespawnQueuedTime = NowTime;
			PendingDespawnQueue.Add(PoolIdx);
		}
	}

	// 2단계 [규칙 5·6]: 삭제 대기 큐 순회 (구제된 항목 제거, 기준 만료/강제 이탈 시 소멸)
	for (int32 QIdx = PendingDespawnQueue.Num() - 1; QIdx >= 0; --QIdx)
	{
		const int32 PoolIdx = PendingDespawnQueue[QIdx];
		if (!PoolEntries.IsValidIndex(PoolIdx))
		{
			PendingDespawnQueue.RemoveAtSwap(QIdx, EAllowShrinking::No);
			continue;
		}

		FAsteroidPoolEntry& Entry = PoolEntries[PoolIdx];
		if (Entry.State != EAsteroidPoolState::Active || !Entry.bPendingDespawn || !IsValid(Entry.Asteroid))
		{
			PendingDespawnQueue.RemoveAtSwap(QIdx, EAllowShrinking::No);
			continue;
		}

		// 촉수로 잡고 있는 대상은 삭제 큐 만료에서 유예 보호
		if (LatchedTarget && Entry.Asteroid.Get() == LatchedTarget)
		{
			Entry.DespawnQueuedTime = NowTime;
			continue;
		}

		const FVector Offset = Entry.Asteroid->GetActorLocation() - PlayerLocation;
		float UnusedMinR = 0.0f;
		float DirOuterB = GetEffectiveOuterRadiusB();
		ComputeDirectionalRadii(Offset, ForwardDir, ForwardExtensionCm, UnusedMinR, DirOuterB);

		const double DistSq = Offset.SizeSquared();
		const double HardDespawnSq = FMath::Square(static_cast<double>(DirOuterB + GetEffectiveDespawnHardMarginCm()));
		const bool bGraceExpired = (NowTime - Entry.DespawnQueuedTime) >= static_cast<double>(DespawnGraceSeconds);
		const bool bHardExceeded = DistSq > HardDespawnSq;

		if (bGraceExpired || bHardExceeded)
		{
			DespawnAsteroidSlot(PoolIdx);
			PendingDespawnQueue.RemoveAtSwap(QIdx, EAllowShrinking::No);
		}
	}

	// 3단계: 독립 부유 미네랄도 동일하게 B_eff 기준 삭제 대기 큐/구제 적용
	for (int32 MIdx = 0; MIdx < MineralPoolEntries.Num(); ++MIdx)
	{
		FMineralPoolEntry& MEntry = MineralPoolEntries[MIdx];
		if (!MEntry.bInUse || !MEntry.bIsFloating || !IsValid(MEntry.Mineral))
		{
			continue;
		}
		if (MEntry.Mineral->IsBeingAbsorbed())
		{
			MEntry.bPendingDespawn = false;
			continue;
		}

		const FVector Offset = MEntry.Mineral->GetActorLocation() - PlayerLocation;
		float UnusedMinR = 0.0f;
		float DirOuterB = GetEffectiveOuterRadiusB();
		ComputeDirectionalRadii(Offset, ForwardDir, ForwardExtensionCm, UnusedMinR, DirOuterB);

		const double DistSq = Offset.SizeSquared();
		const double OuterBSq = FMath::Square(static_cast<double>(DirOuterB));
		const double HardDespawnSq = FMath::Square(static_cast<double>(DirOuterB + GetEffectiveDespawnHardMarginCm()));

		if (DistSq <= OuterBSq)
		{
			MEntry.bPendingDespawn = false;
			MEntry.DespawnQueuedTime = 0.0;
		}
		else if (!MEntry.bPendingDespawn)
		{
			MEntry.bPendingDespawn = true;
			MEntry.DespawnQueuedTime = NowTime;
		}
		else if ((NowTime - MEntry.DespawnQueuedTime) >= static_cast<double>(DespawnGraceSeconds) || DistSq > HardDespawnSq)
		{
			ReleaseMineralSlot(MIdx);
		}
	}

	// 4단계 [규칙 1·2·3·4]: 부족분을 런타임에는 오직 전방 확장 가중치가 반영된 C = [A_eff, B_eff] 외곽 껍질에서만 보충
	const int32 Deficit = TargetActiveAsteroids - ActivePoolIndices.Num();
	const int32 Budget = FMath::Min(Deficit, FMath::Max(SpawnBudget, 0));

	if (Budget > 0 && FreePoolIndices.Num() > 0)
	{
		TArray<FVector> ActiveLocations;
		ActiveLocations.Reserve(ActivePoolIndices.Num() + Budget);
		for (int32 PoolIdx : ActivePoolIndices)
		{
			if (PoolEntries.IsValidIndex(PoolIdx) && IsValid(PoolEntries[PoolIdx].Asteroid))
			{
				ActiveLocations.Add(PoolEntries[PoolIdx].Asteroid->GetActorLocation());
			}
		}

		const uint32 ScanMix = (static_cast<uint32>(WorldSeed) * 1664525u)
			^ (static_cast<uint32>(SpawnSequenceCounter + 1) * 1013904223u);
		FRandomStream ShellStream(static_cast<int32>(ScanMix & 0x7FFFFFFFu));

		const float PlayerRad = GetEffectivePlayerRadiusCm();
		const float BaseMinSpawnR = bInitialPopulate ? (InitialSpawnInnerRadiusMultiplier * PlayerRad) : GetEffectiveInnerRadiusA();
		const float BaseMaxSpawnR = GetEffectiveOuterRadiusB();
		const int32 NearClusterCount = bInitialPopulate
			? FMath::Clamp(FMath::RoundToInt(static_cast<float>(Budget) * FMath::Clamp(InitialNearClusterFraction, 0.0f, 0.5f)), 0, Budget)
			: 0;
		const float NearMaxR = FMath::Max(InitialNearClusterMaxRadiusMultiplier * PlayerRad, BaseMinSpawnR + 4.0f * PlayerRad);

		for (int32 Spawned = 0; Spawned < Budget; ++Spawned)
		{
			const bool bSpawnInNearCluster = (Spawned < NearClusterCount);
			const float TargetMaxR = bSpawnInNearCluster ? FMath::Min(NearMaxR, BaseMaxSpawnR) : BaseMaxSpawnR;
			const float TargetFwdExt = bSpawnInNearCluster ? 0.0f : ForwardExtensionCm;
			if (!SpawnAsteroidInShell(PlayerLocation, ForwardDir, TargetFwdExt, BaseMinSpawnR, TargetMaxR, bInitialPopulate, ActiveLocations, ShellStream))
			{
				break;
			}
		}
	}

	if (bEnableMineralSpawning && MineralPoolEntries.Num() > 0)
	{
		const int32 DesiredFloating = FMath::RoundToInt(static_cast<float>(TargetActiveAsteroids) * FloatingMineralTargetRatio);
		int32 CurrentFloating = 0;
		for (const FMineralPoolEntry& MEntry : MineralPoolEntries)
		{
			if (MEntry.bInUse && MEntry.bIsFloating && IsValid(MEntry.Mineral))
			{
				++CurrentFloating;
			}
		}

		const int32 FloatingDeficit = FMath::Min(DesiredFloating - CurrentFloating, bInitialPopulate ? DesiredFloating : 3);
		if (FloatingDeficit > 0)
		{
			const uint32 MinMix = (static_cast<uint32>(WorldSeed) ^ 0xA341316Cu) + SpawnSequenceCounter;
			FRandomStream FloatStream(static_cast<int32>(MinMix & 0x7FFFFFFFu));
			const float PlayerRad = GetEffectivePlayerRadiusCm();
			const float BaseMinSpawnR = bInitialPopulate ? (InitialSpawnInnerRadiusMultiplier * PlayerRad) : GetEffectiveInnerRadiusA();
			const float BaseMaxSpawnR = GetEffectiveOuterRadiusB();
			const int32 NearMineralCount = bInitialPopulate
				? FMath::Clamp(FMath::RoundToInt(static_cast<float>(FloatingDeficit) * FMath::Clamp(InitialNearClusterFraction, 0.0f, 0.5f)), 0, FloatingDeficit)
				: 0;
			const float NearMaxR = FMath::Max(InitialNearClusterMaxRadiusMultiplier * PlayerRad, BaseMinSpawnR + 4.0f * PlayerRad);

			for (int32 i = 0; i < FloatingDeficit; ++i)
			{
				const bool bSpawnNear = (i < NearMineralCount);
				const float TargetMaxR = bSpawnNear ? FMath::Min(NearMaxR, BaseMaxSpawnR) : BaseMaxSpawnR;
				const float TargetFwdExt = bSpawnNear ? 0.0f : ForwardExtensionCm;
				if (!TrySpawnFloatingMineralInShell(PlayerLocation, ForwardDir, TargetFwdExt, BaseMinSpawnR, TargetMaxR, FloatStream))
				{
					break;
				}
			}
		}
	}

	if (bEnableDistancePhysicsTiering)
	{
		UpdatePhysicsSimulationTiers(PlayerLocation, LatchedTarget);
	}

	if (bInitialPopulate)
	{
		bHasInitialPopulated = true;
	}

	SyncStatsCounters();
}

void AAsteroidFieldManager::UpdatePhysicsSimulationTiers(const FVector& PlayerLocation, AActor* LatchedTarget)
{
	const float PlayerRad = GetEffectivePlayerRadiusCm();
	const double ActRadiusSq = FMath::Square(static_cast<double>(PhysicsActivationRadiusMultiplier * PlayerRad));
	const double DeactRadiusSq = FMath::Square(static_cast<double>((PhysicsActivationRadiusMultiplier + PhysicsHysteresisMultiplier) * PlayerRad));

	struct FPhysicsCandidate
	{
		int32 PoolIdx = INDEX_NONE;
		double DistSq = 0.0;
		bool bProtected = false;
	};

	TArray<FPhysicsCandidate> Candidates;
	Candidates.Reserve(FMath::Max(MaxActivePhysicsAsteroids + 16, 64));

	for (const int32 PoolIdx : ActivePoolIndices)
	{
		if (!PoolEntries.IsValidIndex(PoolIdx) || PoolEntries[PoolIdx].State != EAsteroidPoolState::Active)
		{
			continue;
		}
		ASpaceAsteroid* Asteroid = PoolEntries[PoolIdx].Asteroid.Get();
		if (!IsValid(Asteroid))
		{
			continue;
		}

		const bool bProtected = (LatchedTarget && Asteroid == LatchedTarget);
		const double DistSq = FVector::DistSquared(Asteroid->GetActorLocation(), PlayerLocation);
		const bool bCurrentlyChaos = Asteroid->IsPhysicsTierActive();
		const bool bEligible = bProtected || (bCurrentlyChaos ? (DistSq <= DeactRadiusSq) : (DistSq <= ActRadiusSq));

		if (bEligible)
		{
			Candidates.Add({ PoolIdx, DistSq, bProtected });
		}
		else if (bCurrentlyChaos)
		{
			Asteroid->SetPhysicsSimulationTier(false);
		}
	}

	if (Candidates.Num() > MaxActivePhysicsAsteroids)
	{
		Candidates.Sort([](const FPhysicsCandidate& A, const FPhysicsCandidate& B)
		{
			if (A.bProtected != B.bProtected)
			{
				return A.bProtected;
			}
			return A.DistSq < B.DistSq;
		});

		for (int32 i = MaxActivePhysicsAsteroids; i < Candidates.Num(); ++i)
		{
			if (ASpaceAsteroid* Asteroid = PoolEntries[Candidates[i].PoolIdx].Asteroid.Get())
			{
				Asteroid->SetPhysicsSimulationTier(false);
			}
		}
		Candidates.SetNum(MaxActivePhysicsAsteroids, EAllowShrinking::No);
	}

	for (const FPhysicsCandidate& Cand : Candidates)
	{
		if (ASpaceAsteroid* Asteroid = PoolEntries[Cand.PoolIdx].Asteroid.Get())
		{
			Asteroid->SetPhysicsSimulationTier(true);
		}
	}
}

void AAsteroidFieldManager::StepKinematicAsteroids(float DeltaTime)
{
	if (DeltaTime <= 0.0f)
	{
		return;
	}

	for (const int32 PoolIdx : ActivePoolIndices)
	{
		if (!PoolEntries.IsValidIndex(PoolIdx) || PoolEntries[PoolIdx].State != EAsteroidPoolState::Active)
		{
			continue;
		}
		ASpaceAsteroid* Asteroid = PoolEntries[PoolIdx].Asteroid.Get();
		if (IsValid(Asteroid) && !Asteroid->IsPhysicsTierActive())
		{
			Asteroid->StepKinematicDrift(DeltaTime);
		}
	}
}

void AAsteroidFieldManager::SyncStatsCounters()
{
	ActiveAsteroidCount = ActivePoolIndices.Num();
	FreePoolSlotCount = FreePoolIndices.Num();
	TotalPooledCount = PoolEntries.Num();

	int32 ChaosActive = 0;
	int32 KinematicActive = 0;
	for (const int32 PoolIdx : ActivePoolIndices)
	{
		if (PoolEntries.IsValidIndex(PoolIdx) && IsValid(PoolEntries[PoolIdx].Asteroid))
		{
			if (PoolEntries[PoolIdx].Asteroid->IsPhysicsTierActive())
			{
				++ChaosActive;
			}
			else
			{
				++KinematicActive;
			}
		}
	}
	ActivePhysicsAsteroidCount = ChaosActive;
	KinematicAsteroidCount = KinematicActive;

	int32 Queued = 0;
	for (int32 PoolIdx : PendingDespawnQueue)
	{
		if (PoolEntries.IsValidIndex(PoolIdx)
			&& PoolEntries[PoolIdx].State == EAsteroidPoolState::Active
			&& PoolEntries[PoolIdx].bPendingDespawn)
		{
			++Queued;
		}
	}
	PendingDespawnCount = Queued;

	int32 MineralsInUse = 0;
	for (const FMineralPoolEntry& MEntry : MineralPoolEntries)
	{
		if (MEntry.bInUse && IsValid(MEntry.Mineral))
		{
			++MineralsInUse;
		}
	}
	ActiveMineralCount = MineralsInUse;
}

void AAsteroidFieldManager::DrawDebugVisualization(
	const FVector& PlayerLocation,
	const FVector& ForwardDir,
	float ForwardExtensionCm) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector SafeFwd = ForwardDir.IsNearlyZero() ? FVector::ForwardVector : ForwardDir.GetSafeNormal();
	const FVector UpRef = (FMath::Abs(SafeFwd.Z) < 0.92f) ? FVector::UpVector : FVector::RightVector;
	const FVector BasisRight = FVector::CrossProduct(UpRef, SafeFwd).GetSafeNormal();
	const FVector BasisUp = FVector::CrossProduct(SafeFwd, BasisRight).GetSafeNormal();

	// 전방 타원구(A_eff 청록, B_eff 주황)의 수평·수직·대각 경선 링과 진행 방향 화살표 렌더링
	auto DrawEllipsoidRing = [&](const FVector& AxisA, const FVector& AxisB)
	{
		constexpr int32 Segments = 48;
		FVector PrevInner = FVector::ZeroVector;
		FVector PrevOuter = FVector::ZeroVector;
		for (int32 i = 0; i <= Segments; ++i)
		{
			const float Angle = (static_cast<float>(i) / static_cast<float>(Segments)) * UE_TWO_PI;
			const FVector Dir = (AxisA * FMath::Cos(Angle) + AxisB * FMath::Sin(Angle)).GetSafeNormal();
			float MinR = GetEffectiveInnerRadiusA();
			float MaxR = GetEffectiveOuterRadiusB();
			ComputeDirectionalRadii(Dir * 100.0f, SafeFwd, ForwardExtensionCm, MinR, MaxR);

			const FVector CurrInner = PlayerLocation + Dir * MinR;
			const FVector CurrOuter = PlayerLocation + Dir * MaxR;
			if (i > 0)
			{
				// Thickness = 0.0f 를 사용하면 원거리(1km 이상)에서도 월드 두께로 소실되지 않고 1px 스크린 스페이스 라인으로 선명하게 보입니다.
				DrawDebugLine(World, PrevInner, CurrInner, FColor(70, 210, 255), false, -1.0f, 0, 0.0f);
				DrawDebugLine(World, PrevOuter, CurrOuter, FColor(255, 175, 60), false, -1.0f, 0, 0.0f);
			}
			PrevInner = CurrInner;
			PrevOuter = CurrOuter;
		}
	};

	DrawEllipsoidRing(SafeFwd, BasisRight);
	DrawEllipsoidRing(SafeFwd, BasisUp);
	DrawEllipsoidRing(BasisRight, BasisUp);
	DrawDebugDirectionalArrow(
		World,
		PlayerLocation,
		PlayerLocation + SafeFwd * (GetEffectiveOuterRadiusB() + ForwardExtensionCm),
		1200.0f,
		FColor::White,
		false,
		-1.0f,
		0,
		0.0f
	);
	if (bEnableDistancePhysicsTiering)
	{
		DrawDebugSphere(World, PlayerLocation, PhysicsActivationRadiusMultiplier * GetEffectivePlayerRadiusCm(), 24, FColor(80, 255, 120), false, -1.0f, 0, 0.0f);
	}

	for (int32 PoolIdx : ActivePoolIndices)
	{
		if (!PoolEntries.IsValidIndex(PoolIdx) || !IsValid(PoolEntries[PoolIdx].Asteroid))
		{
			continue;
		}
		const FAsteroidPoolEntry& Entry = PoolEntries[PoolIdx];
		const FColor DotColor = Entry.bPendingDespawn ? FColor::Yellow : FColor::Green;
		DrawDebugPoint(World, Entry.Asteroid->GetActorLocation(), 9.0f, DotColor, false);
	}
}

UClass* AAsteroidFieldManager::ResolveMineralSpawnClass() const
{
	return MineralClass ? MineralClass.Get() : ASpaceMineral::StaticClass();
}

void AAsteroidFieldManager::PrewarmMineralPool(int32 TargetCount)
{
	UWorld* World = GetWorld();
	UClass* SpawnCls = ResolveMineralSpawnClass();
	if (!World || !SpawnCls)
	{
		return;
	}

	const int32 DesiredCount = FMath::Clamp(TargetCount, 0, MaxPooledMinerals);
	const FTransform HiddenInit(FRotator::ZeroRotator, GetActorLocation());

	while (MineralPoolEntries.Num() < DesiredCount)
	{
		const int32 SlotIdx = MineralPoolEntries.Num();
		FActorSpawnParameters Params;
		Params.Owner = this;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		ASpaceMineral* NewMineral = World->SpawnActor<ASpaceMineral>(SpawnCls, HiddenInit, Params);
		if (!NewMineral)
		{
			break;
		}

		const uint32 ArchetypeHash = (static_cast<uint32>(WorldSeed) * 1664525u) ^ (static_cast<uint32>(SlotIdx + 1) * 1013904223u);
		const int32 ArchetypeSeed = FMath::Max(static_cast<int32>(ArchetypeHash & 0x7FFFFFFFu), 1);
		NewMineral->BakePooledArchetype(ArchetypeSeed);

		FMineralPoolEntry MEntry;
		MEntry.Mineral = NewMineral;
		MEntry.bInUse = false;
		MEntry.bIsFloating = false;
		MineralPoolEntries.Add(MEntry);
	}
}

int32 AAsteroidFieldManager::AcquireFreeMineralSlotIndex()
{
	for (int32 Idx = 0; Idx < MineralPoolEntries.Num(); ++Idx)
	{
		if (!MineralPoolEntries[Idx].bInUse && IsValid(MineralPoolEntries[Idx].Mineral))
		{
			return Idx;
		}
	}
	return INDEX_NONE;
}

void AAsteroidFieldManager::ReleaseMineralSlot(int32 MineralIdx)
{
	if (!MineralPoolEntries.IsValidIndex(MineralIdx))
	{
		return;
	}

	FMineralPoolEntry& MEntry = MineralPoolEntries[MineralIdx];
	if (IsValid(MEntry.Mineral))
	{
		MEntry.Mineral->EnterDormantPool();
	}
	MEntry.bInUse = false;
	MEntry.bIsFloating = false;
	MEntry.bPendingDespawn = false;
	MEntry.DespawnQueuedTime = 0.0;
	MEntry.HostAsteroid.Reset();
}

void AAsteroidFieldManager::ReleaseMineralsForHostAsteroid(ASpaceAsteroid* HostAsteroid)
{
	if (!HostAsteroid)
	{
		return;
	}

	for (int32 Idx = 0; Idx < MineralPoolEntries.Num(); ++Idx)
	{
		const FMineralPoolEntry& MEntry = MineralPoolEntries[Idx];
		if (!MEntry.bInUse || MEntry.bIsFloating || !IsValid(MEntry.Mineral))
		{
			continue;
		}
		if (MEntry.HostAsteroid.Get() == HostAsteroid && !MEntry.Mineral->IsBeingAbsorbed())
		{
			ReleaseMineralSlot(Idx);
		}
	}
}

void AAsteroidFieldManager::SpawnSurfaceMineralsForAsteroid(ASpaceAsteroid* HostAsteroid, int32 AsteroidSeed)
{
	if (!HostAsteroid || MineralPoolEntries.Num() == 0)
	{
		return;
	}

	const float HostRadius = HostAsteroid->GetGeneratedRadiusCm();
	if (HostRadius < MinAsteroidRadiusForSurfaceMineralCm)
	{
		return;
	}

	FRandomStream Stream(static_cast<int32>((static_cast<uint32>(AsteroidSeed) * 2654435761u) ^ 0x517CC1B7u));
	if (Stream.GetFraction() > SurfaceMineralChance)
	{
		return;
	}

	const int32 MinClusters = FMath::Max(SurfaceMineralCountRange.Min, 1);
	const int32 MaxClusters = FMath::Max(SurfaceMineralCountRange.Max, MinClusters);
	int32 DesiredClusters = MinClusters;
	if (HostRadius >= SurfaceMineralExtraClusterMinRadiusCm && MaxClusters > MinClusters)
	{
		const float RandU = Stream.GetFraction();
		const float DistAlpha = FMath::Pow(RandU, FMath::Max(SurfaceMineralDistributionExponent, 0.1f));
		DesiredClusters = FMath::Clamp(
			FMath::RoundToInt(FMath::Lerp(static_cast<float>(MinClusters), static_cast<float>(MaxClusters), DistAlpha)),
			MinClusters,
			MaxClusters
		);
	}
	for (int32 SubIdx = 0; SubIdx < DesiredClusters; ++SubIdx)
	{
		const uint32 AnchorMix = (static_cast<uint32>(AsteroidSeed) * 747796405u) ^ static_cast<uint32>(SubIdx + 1000);
		const int32 AnchorSeed = static_cast<int32>(AnchorMix & 0x7FFFFFFFu);

		FVector LocalPt = FVector::ZeroVector;
		FVector LocalNormal = FVector::UpVector;
		if (!HostAsteroid->SampleSurfaceAnchor(AnchorSeed, LocalPt, LocalNormal))
		{
			continue;
		}

		const int32 PoolIdx = AcquireFreeMineralSlotIndex();
		if (PoolIdx == INDEX_NONE)
		{
			break;
		}

		const float ClusterScale = FMath::Clamp(HostRadius / 120.0f, 0.72f, 1.35f);
		FMineralPoolEntry& MEntry = MineralPoolEntries[PoolIdx];
		MEntry.bInUse = true;
		MEntry.bIsFloating = false;
		MEntry.bPendingDespawn = false;
		MEntry.DespawnQueuedTime = 0.0;
		MEntry.HostAsteroid = HostAsteroid;

		MEntry.Mineral->ActivateOnAsteroidSurface(this, HostAsteroid, AnchorMix, LocalPt, LocalNormal, ClusterScale);
	}
}

bool AAsteroidFieldManager::TrySpawnFloatingMineralInShell(
	const FVector& PlayerLocation,
	const FVector& ForwardDir,
	float ForwardExtensionCm,
	float BaseMinRadiusCm,
	float BaseMaxRadiusCm,
	FRandomStream& Stream)
{
	const int32 PoolIdx = AcquireFreeMineralSlotIndex();
	if (PoolIdx == INDEX_NONE)
	{
		return false;
	}

	const FVector SpawnLoc = SampleVolumeUniformInShell(
		PlayerLocation,
		ForwardDir,
		ForwardExtensionCm,
		false,
		BaseMinRadiusCm,
		BaseMaxRadiusCm,
		Stream
	);
	const FRotator RandomRot(
		Stream.FRandRange(-85.0f, 85.0f),
		Stream.FRandRange(0.0f, 360.0f),
		Stream.FRandRange(-180.0f, 180.0f)
	);
	const float Scale = Stream.FRandRange(0.78f, 1.20f);
	const FVector SpinRates(
		Stream.FRandRange(10.0f, 28.0f),
		Stream.FRandRange(18.0f, 44.0f),
		Stream.FRandRange(8.0f, 24.0f)
	);
	const uint32 MineralHash = ++SpawnSequenceCounter;

	FMineralPoolEntry& MEntry = MineralPoolEntries[PoolIdx];
	MEntry.bInUse = true;
	MEntry.bIsFloating = true;
	MEntry.bPendingDespawn = false;
	MEntry.DespawnQueuedTime = 0.0;
	MEntry.HostAsteroid.Reset();

	MEntry.Mineral->ActivateFreeFloating(
		this,
		MineralHash,
		SpawnLoc,
		RandomRot,
		Scale,
		SpinRates
	);
	return true;
}

void AAsteroidFieldManager::MarkMineralConsumed(uint32 MineralHash)
{
	(void)MineralHash;
}

void AAsteroidFieldManager::ReturnMineralToPool(ASpaceMineral* Mineral)
{
	if (!Mineral)
	{
		return;
	}

	for (int32 Idx = 0; Idx < MineralPoolEntries.Num(); ++Idx)
	{
		if (MineralPoolEntries[Idx].Mineral.Get() == Mineral)
		{
			ReleaseMineralSlot(Idx);
			break;
		}
	}

	SyncStatsCounters();
}

void AAsteroidFieldManager::GatherAvailableMinerals(TArray<ASpaceMineral*>& OutMinerals) const
{
	OutMinerals.Reset();
	for (const FMineralPoolEntry& MEntry : MineralPoolEntries)
	{
		if (MEntry.bInUse && IsValid(MEntry.Mineral) && MEntry.Mineral->IsAvailableForAdsorption())
		{
			OutMinerals.Add(MEntry.Mineral.Get());
		}
	}
}

void AAsteroidFieldManager::ExplodeColonizedAsteroid(ASpaceAsteroid* Asteroid)
{
	if (!IsValid(Asteroid))
	{
		return;
	}

	const FVector ExplosionCenter = Asteroid->GetActorLocation();
	const float AsteroidRadius = FMath::Max(Asteroid->GetGeneratedRadiusCm(), 25.0f);
	const float RequiredBiomass = Asteroid->GetRequiredColonizeBiomass();

	const int32 MinCount = FMath::Max(ColonizedExplosionMineralCountRange.Min, 1);
	const int32 MaxCount = FMath::Max(ColonizedExplosionMineralCountRange.Max, MinCount);
	const int32 RawCount = FMath::RoundToInt(RequiredBiomass * FMath::Max(ColonizedExplosionMineralsPerBiomass, 0.01f))
		+ FMath::Max(ColonizedExplosionBonusMinerals, 0);
	const int32 SpawnCount = FMath::Clamp(RawCount, MinCount, MaxCount);
	const float TotalGuaranteedBiomass = RequiredBiomass * FMath::Max(ColonizedNetProfitMultiplier, 1.05f) + FMath::Max(ColonizedBonusBiomass, 0.0f);

	ASpaceDrifterPawn* DrifterPawn = nullptr;
	if (const UWorld* World = GetWorld())
	{
		DrifterPawn = Cast<ASpaceDrifterPawn>(UGameplayStatics::GetPlayerPawn(World, 0));
	}

	if (bAutoAbsorbColonizedExplosionMinerals && DrifterPawn)
	{
		// 소행성 표면에 이미 자라나 있던 미네랄도 폭발과 함께 놓치지 않고 플레이어로 즉시 끌어옵니다.
		for (FMineralPoolEntry& MEntry : MineralPoolEntries)
		{
			if (MEntry.bInUse && !MEntry.bIsFloating && IsValid(MEntry.Mineral)
				&& MEntry.HostAsteroid.Get() == Asteroid && !MEntry.Mineral->IsBeingAbsorbed())
			{
				MEntry.Mineral->BeginAbsorption(DrifterPawn);
			}
		}
	}

	const int32 ActualSpawned = SpawnMineralBurst(
		ExplosionCenter,
		AsteroidRadius,
		TotalGuaranteedBiomass,
		SpawnCount,
		Asteroid->Seed,
		bAutoAbsorbColonizedExplosionMinerals
	);

	if (DrifterPawn)
	{
		if (const ASpaceDrifterPlayerController* PC = Cast<ASpaceDrifterPlayerController>(DrifterPawn->GetController()))
		{
			if (USpaceDrifterHUDWidget* HUD = PC->GetHUDWidget())
			{
				const FString BannerMsg = bAutoAbsorbColonizedExplosionMinerals
					? FString::Printf(TEXT("[소행성 폭발!] 미네랄 %d개 즉시 흡수!"), ActualSpawned)
					: FString::Printf(TEXT("[소행성 폭발!] 미네랄 %d개 방출! (2번 그물로 포획하세요)"), ActualSpawned);
				HUD->SetStatusBannerMessage(
					BannerMsg,
					FLinearColor(1.0f, 0.88f, 0.36f, 1.0f),
					2.2f
				);
			}
		}
	}

	// 소행성 자체를 풀로 반납하여 폭발 소멸 처리
	for (int32 PoolIdx : ActivePoolIndices)
	{
		if (PoolEntries.IsValidIndex(PoolIdx) && PoolEntries[PoolIdx].Asteroid.Get() == Asteroid)
		{
			DespawnAsteroidSlot(PoolIdx);
			break;
		}
	}

	SyncStatsCounters();
}

int32 AAsteroidFieldManager::SpawnMineralBurst(
	const FVector& ExplosionCenter,
	float SourceRadiusCm,
	float TotalGuaranteedBiomass,
	int32 DesiredSpawnCount,
	int32 BurstSeed,
	bool bAutoAbsorbByPlayer)
{
	const int32 SpawnCount = FMath::Max(DesiredSpawnCount, 1);
	const float PerMineralBiomass = FMath::Max(TotalGuaranteedBiomass, 1.0f) / static_cast<float>(SpawnCount);
	const float EffectiveRadius = FMath::Max(SourceRadiusCm, 25.0f);

	FRandomStream ExplosionStream(BurstSeed ^ static_cast<int32>(++SpawnSequenceCounter));
	const float MinBurstSpeed = FMath::Min(ColonizedExplosionBurstSpeedRange.X, ColonizedExplosionBurstSpeedRange.Y);
	const float MaxBurstSpeed = FMath::Max(ColonizedExplosionBurstSpeedRange.X, ColonizedExplosionBurstSpeedRange.Y);

	ASpaceDrifterPawn* DrifterPawn = nullptr;
	if (const UWorld* World = GetWorld())
	{
		DrifterPawn = Cast<ASpaceDrifterPawn>(UGameplayStatics::GetPlayerPawn(World, 0));
	}

	int32 ActualSpawned = 0;
	for (int32 i = 0; i < SpawnCount; ++i)
	{
		int32 MineralIdx = AcquireFreeMineralSlotIndex();
		if (MineralIdx == INDEX_NONE)
		{
			const int32 NeededTotal = MineralPoolEntries.Num() + FMath::Max(SpawnCount - i, 4);
			MaxPooledMinerals = FMath::Clamp(FMath::Max(MaxPooledMinerals, NeededTotal), 0, 2048);
			PrewarmMineralPool(NeededTotal);
			MineralIdx = AcquireFreeMineralSlotIndex();
			if (MineralIdx == INDEX_NONE)
			{
				break;
			}
		}

		const FVector OutwardDir = ExplosionStream.GetUnitVector();
		const float OffsetDist = EffectiveRadius * ExplosionStream.FRandRange(0.15f, 0.85f);
		const FVector SpawnLoc = ExplosionCenter + OutwardDir * OffsetDist;
		const FRotator RandomRot(
			ExplosionStream.FRandRange(-85.0f, 85.0f),
			ExplosionStream.FRandRange(0.0f, 360.0f),
			ExplosionStream.FRandRange(-180.0f, 180.0f)
		);
		const float Scale = ExplosionStream.FRandRange(0.85f, 1.35f);
		const FVector SpinRates(
			ExplosionStream.FRandRange(18.0f, 48.0f),
			ExplosionStream.FRandRange(24.0f, 64.0f),
			ExplosionStream.FRandRange(12.0f, 36.0f)
		);
		const FVector BurstVel = OutwardDir * ExplosionStream.FRandRange(MinBurstSpeed, MaxBurstSpeed);
		const uint32 MineralHash = ++SpawnSequenceCounter;

		FMineralPoolEntry& MEntry = MineralPoolEntries[MineralIdx];
		MEntry.bInUse = true;
		MEntry.bIsFloating = true;
		MEntry.bPendingDespawn = false;
		MEntry.DespawnQueuedTime = 0.0;
		MEntry.HostAsteroid.Reset();

		MEntry.Mineral->ActivateFreeFloating(
			this,
			MineralHash,
			SpawnLoc,
			RandomRot,
			Scale,
			SpinRates,
			BurstVel
		);
		MEntry.Mineral->SetBiomassRewardOverride(PerMineralBiomass);

		if (bAutoAbsorbByPlayer && DrifterPawn)
		{
			MEntry.Mineral->BeginAbsorption(DrifterPawn);
		}
		++ActualSpawned;
	}

	return ActualSpawned;
}
