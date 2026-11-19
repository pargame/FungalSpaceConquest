// Copyright Epic Games, Inc. All Rights Reserved.

#include "SpaceMineral.h"

#include "AsteroidFieldManager.h"
#include "Components/SphereComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "SpaceAsteroid.h"
#include "SpaceDrifterPawn.h"

ASpaceMineral::ASpaceMineral()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	MineralMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("MineralMesh"));
	SetRootComponent(MineralMesh);

	MineralMesh->bUseAsyncCooking = false;
	MineralMesh->bUseComplexAsSimpleCollision = false;
	MineralMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MineralMesh->SetSimulatePhysics(false);
	// 자체 발광(Emissive)하는 소형 광석 결정이므로 불필요한 동적 그림자 패스(Shadow Depth) 비활성화
	MineralMesh->SetCastShadow(false);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DefaultMineralMat(
		TEXT("/Game/Sandbox/MiniProject/Materials/M_SpaceMineral.M_SpaceMineral")
	);
	if (DefaultMineralMat.Succeeded())
	{
		MineralMaterial = DefaultMineralMat.Object;
	}
}

void ASpaceMineral::BeginPlay()
{
	Super::BeginPlay();

	if (!bHasBakedGeometry)
	{
		EnterDormantPool();
	}
}

void ASpaceMineral::BakePooledArchetype(int32 ArchetypeSeed)
{
	BuildCrystalClusterGeometry(ArchetypeSeed);
	bHasBakedGeometry = true;
	EnterDormantPool();
}

void ASpaceMineral::BuildCrystalClusterGeometry(int32 ArchetypeSeed)
{
	if (!MineralMesh)
	{
		return;
	}

	FRandomStream Stream(FMath::Max(ArchetypeSeed, 1));

	TArray<FVector> Verts;
	TArray<int32> Tris;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;

	// 중앙 주 결정 + 주변으로 뻗는 3~4개 보조 결정 샤드 생성 (각진 육각 기둥)
	const float MainHeight = Stream.FRandRange(34.0f, 52.0f);
	const float MainRadius = Stream.FRandRange(9.5f, 14.0f);
	const FQuat MainRot = FRotator(
		Stream.FRandRange(-8.0f, 8.0f),
		Stream.FRandRange(0.0f, 360.0f),
		Stream.FRandRange(-8.0f, 8.0f)
	).Quaternion();
	AppendCrystalSpire(
		FVector(0.0f, 0.0f, -6.0f),
		MainRot,
		MainHeight,
		MainRadius,
		6,
		Verts,
		Tris,
		Normals,
		UVs,
		Colors
	);

	const int32 SatelliteCount = Stream.RandRange(3, 4);
	for (int32 i = 0; i < SatelliteCount; ++i)
	{
		const float Azimuth = (360.0f / SatelliteCount) * i + Stream.FRandRange(-22.0f, 22.0f);
		const float Tilt = Stream.FRandRange(22.0f, 48.0f);
		const FQuat SatRot = FQuat(FVector::UpVector, FMath::DegreesToRadians(Azimuth))
			* FQuat(FVector::RightVector, FMath::DegreesToRadians(Tilt));

		const float RadialOffset = MainRadius * Stream.FRandRange(0.55f, 0.95f);
		const FVector Offset(
			FMath::Cos(FMath::DegreesToRadians(Azimuth)) * RadialOffset,
			FMath::Sin(FMath::DegreesToRadians(Azimuth)) * RadialOffset,
			-5.0f
		);

		AppendCrystalSpire(
			Offset,
			SatRot,
			MainHeight * Stream.FRandRange(0.48f, 0.78f),
			MainRadius * Stream.FRandRange(0.50f, 0.72f),
			5,
			Verts,
			Tris,
			Normals,
			UVs,
			Colors
		);
	}

	TArray<FProcMeshTangent> Tangents;
	Tangents.Init(FProcMeshTangent(FVector::ForwardVector, false), Verts.Num());

	MineralMesh->ClearAllMeshSections();
	MineralMesh->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UVs, Colors, Tangents, false);

	UMaterialInterface* BaseMat = MineralMaterial.Get();
	if (!BaseMat)
	{
		BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Sandbox/MiniProject/Materials/M_SpaceMineral.M_SpaceMineral"));
	}
	if (BaseMat)
	{
		MineralMID = UMaterialInstanceDynamic::Create(BaseMat, this);
		if (MineralMID)
		{
			// 시드별 미세한 결정 색조 변주 (청록 ~ 에메랄드 시안)
			const float HueShift = Stream.FRandRange(-0.08f, 0.08f);
			const FLinearColor TipCol(
				FMath::Clamp(0.12f + HueShift * 0.4f, 0.04f, 0.35f),
				FMath::Clamp(0.88f + HueShift * 0.5f, 0.65f, 1.0f),
				FMath::Clamp(0.80f - HueShift * 0.6f, 0.55f, 1.0f),
				1.0f
			);
			MineralMID->SetVectorParameterValue(TEXT("MineralTipColor"), TipCol);
			MineralMID->SetVectorParameterValue(TEXT("MineralEmissiveColor"), TipCol);
			MineralMID->SetScalarParameterValue(TEXT("MineralGlowIntensity"), Stream.FRandRange(1.8f, 2.6f));
			MineralMesh->SetMaterial(0, MineralMID);
		}
	}
}

void ASpaceMineral::AppendCrystalSpire(
	const FVector& BaseOffset,
	const FQuat& Orientation,
	float HeightCm,
	float BaseRadiusCm,
	int32 Sides,
	TArray<FVector>& OutVerts,
	TArray<int32>& OutTris,
	TArray<FVector>& OutNormals,
	TArray<FVector2D>& OutUVs,
	TArray<FLinearColor>& OutColors) const
{
	const float ShaftHeight = HeightCm * 0.72f;
	const float TopRadius = BaseRadiusCm * 0.72f;
	const FVector TipLocal(0.0f, 0.0f, HeightCm);

	auto TransformPt = [&BaseOffset, &Orientation](const FVector& LocalPt) -> FVector
	{
		return BaseOffset + Orientation.RotateVector(LocalPt);
	};

	auto AddFlatTri = [&](const FVector& P0, const FVector& P1, const FVector& P2, float C0, float C1, float C2)
	{
		const FVector FaceNormal = FVector::CrossProduct(P2 - P0, P1 - P0).GetSafeNormal();
		const int32 BaseIdx = OutVerts.Num();
		OutVerts.Add(P0);
		OutVerts.Add(P1);
		OutVerts.Add(P2);

		OutNormals.Add(FaceNormal);
		OutNormals.Add(FaceNormal);
		OutNormals.Add(FaceNormal);

		OutUVs.Add(FVector2D(0.0f, C0));
		OutUVs.Add(FVector2D(1.0f, C1));
		OutUVs.Add(FVector2D(0.5f, C2));

		OutColors.Add(FLinearColor(C0, C0, C0, 1.0f));
		OutColors.Add(FLinearColor(C1, C1, C1, 1.0f));
		OutColors.Add(FLinearColor(C2, C2, C2, 1.0f));

		OutTris.Add(BaseIdx);
		OutTris.Add(BaseIdx + 1);
		OutTris.Add(BaseIdx + 2);
	};

	const FVector TipWorld = TransformPt(TipLocal);
	const FVector BaseCenterWorld = TransformPt(FVector::ZeroVector);

	for (int32 i = 0; i < Sides; ++i)
	{
		const float A0 = (UE_TWO_PI * i) / Sides;
		const float A1 = (UE_TWO_PI * ((i + 1) % Sides)) / Sides;

		const FVector B0 = TransformPt(FVector(FMath::Cos(A0) * BaseRadiusCm, FMath::Sin(A0) * BaseRadiusCm, 0.0f));
		const FVector B1 = TransformPt(FVector(FMath::Cos(A1) * BaseRadiusCm, FMath::Sin(A1) * BaseRadiusCm, 0.0f));
		const FVector T0 = TransformPt(FVector(FMath::Cos(A0) * TopRadius, FMath::Sin(A0) * TopRadius, ShaftHeight));
		const FVector T1 = TransformPt(FVector(FMath::Cos(A1) * TopRadius, FMath::Sin(A1) * TopRadius, ShaftHeight));

		// 기둥 옆면 (외향 CW 규칙: B0 -> T0 -> T1, B0 -> T1 -> B1)
		AddFlatTri(B0, T0, T1, 0.0f, 0.72f, 0.72f);
		AddFlatTri(B0, T1, B1, 0.0f, 0.72f, 0.0f);

		// 뾰족한 피라미드 첨단면
		AddFlatTri(T0, TipWorld, T1, 0.72f, 1.0f, 0.72f);

		// 바닥 캡 (부유형이나 뽑혔을 때 뒷면 빈틈 방지)
		AddFlatTri(BaseCenterWorld, B0, B1, 0.0f, 0.0f, 0.0f);
	}
}

void ASpaceMineral::ActivateOnAsteroidSurface(
	AAsteroidFieldManager* InManager,
	ASpaceAsteroid* HostAsteroid,
	uint32 InMineralHash,
	const FVector& LocalSurfacePoint,
	const FVector& LocalSurfaceNormal,
	float ClusterScale)
{
	if (!HostAsteroid || !HostAsteroid->GetAsteroidMesh())
	{
		return;
	}

	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

	OwnerManager = InManager;
	HostAsteroidPtr = HostAsteroid;
	CapturerPtr.Reset();
	ActiveMineralHash = InMineralHash;
	MineralState = ESpaceMineralState::AttachedToAsteroid;
	PullVelocity = FVector::ZeroVector;
	AdsorbStartWorldPos = FVector::ZeroVector;
	AbsorbStartWorldPos = FVector::ZeroVector;
	AbsorbStartScale3D = FVector::OneVector;
	NetBindBlendAlpha = 0.0f;
	AdsorbElapsed = 0.0f;
	AbsorbElapsed = 0.0f;
	bFallbackDirectPull = false;

	const FVector SafeNormal = LocalSurfaceNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	const FQuat SurfaceQuat = FQuat::FindBetweenNormals(FVector::UpVector, SafeNormal);
	BaseScale3D = FVector(FMath::Clamp(ClusterScale, 0.55f, 1.6f));

	// 암석 표면에서 살짝(-3cm) 박힌 채 법선 방향으로 돋아나도록 로컬 부착
	SetActorRelativeScale3D(BaseScale3D);
	AttachToComponent(HostAsteroid->GetAsteroidMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	SetActorRelativeLocation(LocalSurfacePoint - SafeNormal * 3.0f);
	SetActorRelativeRotation(SurfaceQuat);
	SetActorScale3D(BaseScale3D);

	SetActorHiddenInGame(false);
	PrimaryActorTick.SetTickFunctionEnable(false);
}

void ASpaceMineral::ActivateFreeFloating(
	AAsteroidFieldManager* InManager,
	uint32 InMineralHash,
	const FVector& WorldLocation,
	const FRotator& WorldRotation,
	float ClusterScale,
	const FVector& InSpinRatesDeg,
	const FVector& InInitialDriftVelocity)
{
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

	OwnerManager = InManager;
	HostAsteroidPtr.Reset();
	CapturerPtr.Reset();
	ActiveMineralHash = InMineralHash;
	BiomassRewardOverride = -1.0f;
	MineralState = ESpaceMineralState::FreeFloating;
	FloatingSpinDeg = InSpinRatesDeg;
	FloatingDriftVelocity = InInitialDriftVelocity;
	PullVelocity = FVector::ZeroVector;
	AdsorbStartWorldPos = FVector::ZeroVector;
	AbsorbStartWorldPos = FVector::ZeroVector;
	AbsorbStartScale3D = FVector::OneVector;
	NetBindBlendAlpha = 0.0f;
	AdsorbElapsed = 0.0f;
	AbsorbElapsed = 0.0f;
	bFallbackDirectPull = false;

	BaseScale3D = FVector(FMath::Clamp(ClusterScale, 0.65f, 1.5f));
	SetActorLocationAndRotation(WorldLocation, WorldRotation, false, nullptr, ETeleportType::TeleportPhysics);
	SetActorScale3D(BaseScale3D);

	SetActorHiddenInGame(false);
	PrimaryActorTick.SetTickFunctionEnable(true);
}

void ASpaceMineral::EnterDormantPool()
{
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

	MineralState = ESpaceMineralState::Dormant;
	BiomassRewardOverride = -1.0f;
	HostAsteroidPtr.Reset();
	CapturerPtr.Reset();
	FloatingDriftVelocity = FVector::ZeroVector;
	PullVelocity = FVector::ZeroVector;
	AdsorbStartWorldPos = FVector::ZeroVector;
	AbsorbStartWorldPos = FVector::ZeroVector;
	AbsorbStartScale3D = FVector::OneVector;
	NetBindBlendAlpha = 0.0f;
	AdsorbElapsed = 0.0f;
	AbsorbElapsed = 0.0f;
	bFallbackDirectPull = false;

	SetActorHiddenInGame(true);
	PrimaryActorTick.SetTickFunctionEnable(false);
}

bool ASpaceMineral::BeginAdsorption(
	ASpaceDrifterPawn* CapturerPawn,
	float InNetAnchorT,
	const FVector2D& InRadialDir2D,
	float InRadialFraction)
{
	if (!CapturerPawn || !IsAvailableForAdsorption())
	{
		return false;
	}

	const FVector OutwardUp = GetActorUpVector();
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	HostAsteroidPtr.Reset();

	if (AAsteroidFieldManager* Mgr = OwnerManager.Get())
	{
		Mgr->MarkMineralConsumed(ActiveMineralHash);
	}

	CapturerPtr = CapturerPawn;
	MineralState = ESpaceMineralState::Adsorbed;
	AdsorbStartWorldPos = GetActorLocation() + OutwardUp * 12.0f;
	NetAnchorT = FMath::Clamp(InNetAnchorT, 0.08f, 0.99f);
	NetRadialDir2D = InRadialDir2D.IsNearlyZero() ? FVector2D(1.0f, 0.0f) : InRadialDir2D.GetSafeNormal();
	NetRadialFraction = FMath::Clamp(InRadialFraction, 0.0f, 0.88f);
	NetBindBlendAlpha = 0.0f;
	AdsorbElapsed = 0.0f;
	AbsorbElapsed = 0.0f;
	bFallbackDirectPull = false;

	// 그물 촉수에 매달려 있는 동안은 촉수 틱에서 0프레임 오차로 좌표를 동기화
	PrimaryActorTick.SetTickFunctionEnable(false);
	return true;
}

bool ASpaceMineral::BeginAbsorption(ASpaceDrifterPawn* CapturerPawn)
{
	if (!CapturerPawn)
	{
		return false;
	}

	// 흡착 단계(Adsorbed)에서 전이될 수도 있고, 본체 직접 접촉 시에는 일반 상태(Attached/FreeFloating)에서 흡착 없이 바로 진입
	if (MineralState == ESpaceMineralState::Dormant || MineralState == ESpaceMineralState::Absorbing)
	{
		return false;
	}

	if (MineralState != ESpaceMineralState::Adsorbed)
	{
		DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		HostAsteroidPtr.Reset();
		if (AAsteroidFieldManager* Mgr = OwnerManager.Get())
		{
			Mgr->MarkMineralConsumed(ActiveMineralHash);
		}
	}

	CapturerPtr = CapturerPawn;
	MineralState = ESpaceMineralState::Absorbing;
	AbsorbStartWorldPos = GetActorLocation();
	AbsorbStartScale3D = GetActorScale3D();
	AbsorbElapsed = 0.0f;
	bFallbackDirectPull = false;

	PrimaryActorTick.SetTickFunctionEnable(true);
	return true;
}

bool ASpaceMineral::SyncAdsorbedPosition(
	float DeltaTime,
	const FVector& NetWorldPos,
	const FVector& PawnBodyPos,
	float AbsorbContactRadius,
	bool bIsRetracting,
	float NetEasedReach)
{
	if (MineralState != ESpaceMineralState::Adsorbed)
	{
		return true;
	}

	AdsorbElapsed += DeltaTime;
	NetBindBlendAlpha = FMath::Min(1.0f, NetBindBlendAlpha + DeltaTime * 14.0f);
	const float SmoothBind = FMath::SmoothStep(0.0f, 1.0f, NetBindBlendAlpha);
	const FVector SyncedLoc = FMath::Lerp(AdsorbStartWorldPos, NetWorldPos, SmoothBind);

	SetActorLocation(SyncedLoc, false, nullptr, ETeleportType::TeleportPhysics);
	AddActorLocalRotation(FRotator(150.0f * DeltaTime, 220.0f * DeltaTime, 110.0f * DeltaTime));
	SetActorScale3D(BaseScale3D);

	// 플레이어 본체에 닿거나 그물이 완전히 회수되면 독립 상태인 흡수(Absorbing)로 매끄럽게 전이
	const float DistToPawn = FVector::Distance(SyncedLoc, PawnBodyPos);
	if (DistToPawn <= AbsorbContactRadius || (bIsRetracting && NetEasedReach <= 0.06f) || AdsorbElapsed >= 5.0f)
	{
		BeginAbsorption(CapturerPtr.Get());
		return true;
	}

	return false;
}

void ASpaceMineral::SwitchToFallbackDirectPull()
{
	if (MineralState != ESpaceMineralState::Adsorbed)
	{
		return;
	}

	bFallbackDirectPull = true;
	PullVelocity = FVector::ZeroVector;
	PrimaryActorTick.SetTickFunctionEnable(true);
}

void ASpaceMineral::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (MineralState == ESpaceMineralState::FreeFloating)
	{
		if (!FloatingDriftVelocity.IsNearlyZero(1.0f))
		{
			AddActorWorldOffset(FloatingDriftVelocity * DeltaTime, false);
			FloatingDriftVelocity = FMath::VInterpTo(FloatingDriftVelocity, FVector::ZeroVector, DeltaTime, 1.35f);
		}
		AddActorLocalRotation(FRotator(FloatingSpinDeg.Y * DeltaTime, FloatingSpinDeg.Z * DeltaTime, FloatingSpinDeg.X * DeltaTime));
		return;
	}

	if (MineralState == ESpaceMineralState::Adsorbed && bFallbackDirectPull)
	{
		ASpaceDrifterPawn* Capturer = CapturerPtr.Get();
		if (!Capturer)
		{
			FinishAbsorption();
			return;
		}

		AdsorbElapsed += DeltaTime;
		const FVector MyLoc = GetActorLocation();
		const FVector TargetLoc = Capturer->GetActorLocation();
		const FVector ToTarget = TargetLoc - MyLoc;
		const float Dist = ToTarget.Size();

		float AbsorbContactRadius = 56.0f;
		if (const USphereComponent* Sphere = Capturer->GetCollisionSphere())
		{
			AbsorbContactRadius = Sphere->GetScaledSphereRadius() + 22.0f;
		}

		if (Dist <= AbsorbContactRadius || AdsorbElapsed >= 2.2f)
		{
			BeginAbsorption(Capturer);
			return;
		}

		const FVector Dir = ToTarget / FMath::Max(Dist, 1.0f);
		const float DesiredSpeed = FMath::Clamp(1400.0f + AdsorbElapsed * 2400.0f, 1400.0f, 3000.0f);
		PullVelocity = FMath::VInterpTo(PullVelocity, Dir * DesiredSpeed, DeltaTime, 16.0f);

		SetActorLocation(MyLoc + PullVelocity * DeltaTime, false, nullptr, ETeleportType::TeleportPhysics);
		AddActorLocalRotation(FRotator(360.0f * DeltaTime, 480.0f * DeltaTime, 240.0f * DeltaTime));
		return;
	}

	if (MineralState == ESpaceMineralState::Absorbing)
	{
		ASpaceDrifterPawn* Capturer = CapturerPtr.Get();
		if (!Capturer)
		{
			FinishAbsorption();
			return;
		}

		AbsorbElapsed += DeltaTime;
		const float Duration = FMath::Max(AbsorbDurationSeconds, 0.04f);
		const float Alpha = FMath::Clamp(AbsorbElapsed / Duration, 0.0f, 1.0f);
		const float PullAlpha = FMath::InterpEaseIn(0.0f, 1.0f, Alpha, 2.2f);

		const FVector TargetLoc = Capturer->GetActorLocation();
		const FVector CurLoc = FMath::Lerp(AbsorbStartWorldPos, TargetLoc, PullAlpha);
		SetActorLocation(CurLoc, false, nullptr, ETeleportType::TeleportPhysics);
		AddActorLocalRotation(FRotator(540.0f * DeltaTime, 720.0f * DeltaTime, 360.0f * DeltaTime));

		const float ShrinkFactor = FMath::Lerp(1.0f, 0.08f, PullAlpha);
		SetActorScale3D(AbsorbStartScale3D * ShrinkFactor);

		if (Alpha >= 1.0f)
		{
			FinishAbsorption();
		}
	}
}

void ASpaceMineral::FinishAbsorption()
{
	const FVector AbsorbLocation = GetActorLocation();
	if (ASpaceDrifterPawn* Capturer = CapturerPtr.Get())
	{
		Capturer->NotifyMineralAbsorbed(this, AbsorbLocation);
	}

	if (AAsteroidFieldManager* Mgr = OwnerManager.Get())
	{
		Mgr->ReturnMineralToPool(this);
	}
	else
	{
		EnterDormantPool();
	}
}
