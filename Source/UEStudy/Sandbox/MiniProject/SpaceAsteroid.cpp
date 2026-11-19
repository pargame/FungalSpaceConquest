#include "SpaceAsteroid.h"

#include "AsteroidFieldManager.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "ProceduralMeshComponent.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

ASpaceAsteroid::ASpaceAsteroid()
{
	PrimaryActorTick.bCanEverTick = false;

	AsteroidMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("AsteroidMesh"));
	SetRootComponent(AsteroidMesh);

	AsteroidMesh->bUseComplexAsSimpleCollision = false;
	AsteroidMesh->bUseAsyncCooking = false;
	AsteroidMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	AsteroidMesh->SetCollisionObjectType(ECC_PhysicsBody);
	AsteroidMesh->SetCollisionResponseToAllChannels(ECR_Block);
	// 플레이어 본체 CollisionSphere가 충돌 이벤트를 처리하므로 소행성 간 불필요한 Hit 콜백 디스패치 비활성화
	AsteroidMesh->SetNotifyRigidBodyCollision(false);
	AsteroidMesh->SetEnableGravity(false);
	AsteroidMesh->SetGenerateOverlapEvents(false);

	static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> DefaultMaterialFinder(
		TEXT("/Game/Sandbox/MiniProject/Materials/M_SpaceAsteroid.M_SpaceAsteroid")
	);
	if (DefaultMaterialFinder.Succeeded())
	{
		AsteroidMaterial = DefaultMaterialFinder.Get();
	}

	if (FRichCurve* Curve = SizeDistributionCurve.GetRichCurve())
	{
		Curve->AddKey(0.0f, 0.0f);
		Curve->AddKey(0.55f, 0.25f);
		Curve->AddKey(0.85f, 0.62f);
		Curve->AddKey(1.0f, 1.0f);
	}

	if (FRichCurve* SpeedCurve = SpeedDistributionCurve.GetRichCurve())
	{
		SpeedCurve->AddKey(0.0f, 0.0f);
		SpeedCurve->AddKey(0.30f, 0.06f);
		SpeedCurve->AddKey(0.72f, 0.38f);
		SpeedCurve->AddKey(0.92f, 0.78f);
		SpeedCurve->AddKey(1.0f, 1.0f);
	}
}

void ASpaceAsteroid::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (bDeferInitialBuild)
	{
		SetActorHiddenInGame(true);
		if (AsteroidMesh)
		{
			AsteroidMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			AsteroidMesh->SetSimulatePhysics(false);
		}
		return;
	}

	const uint32 CurrentSig = ComputeBuildSignature();
	if (!bHasBuiltOnce || CurrentSig != LastBuiltSignature)
	{
		RebuildAsteroidFromSeed();
	}
}

void ASpaceAsteroid::BeginPlay()
{
	Super::BeginPlay();

	if (bDeferInitialBuild)
	{
		SetActorHiddenInGame(true);
		if (AsteroidMesh)
		{
			AsteroidMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			AsteroidMesh->SetSimulatePhysics(false);
		}
		return;
	}

	const uint32 CurrentSig = ComputeBuildSignature();
	if (!bHasBuiltOnce || CurrentSig != LastBuiltSignature)
	{
		RebuildAsteroidFromSeed();
	}
	else
	{
		ApplyChaosPhysicsSettings();
	}

	ApplySeededInitialDrift();
}

#if WITH_EDITOR
void ASpaceAsteroid::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (!bAutoRebuildOnEdit)
	{
		return;
	}

	static const FName NAME_RelativeLocation(TEXT("RelativeLocation"));
	static const FName NAME_RelativeRotation(TEXT("RelativeRotation"));
	static const FName NAME_RelativeScale3D(TEXT("RelativeScale3D"));
	const FName PropName = PropertyChangedEvent.GetPropertyName();
	if (PropName == NAME_RelativeLocation || PropName == NAME_RelativeRotation || PropName == NAME_RelativeScale3D)
	{
		return;
	}

	RebuildAsteroidFromSeed();
}
#endif

void ASpaceAsteroid::InitializeWithSeed(int32 NewSeed)
{
	Seed = FMath::Max(NewSeed, 0);
	RebuildAsteroidFromSeed();
	ApplySeededInitialDrift();
}

void ASpaceAsteroid::SetDeferInitialBuild(bool bDefer)
{
	bDeferInitialBuild = bDefer;
}

void ASpaceAsteroid::EnterDormantPool()
{
	if (bManagedDormant)
	{
		return;
	}

	ResetColonizationState();
	bManagedDormant = true;
	bPhysicsTierActive = false;
	RuntimeSizeScale = 1.0f;
	if (AsteroidMesh && AsteroidMesh->IsSimulatingPhysics())
	{
		SavedDormantLinearVelocity = AsteroidMesh->GetPhysicsLinearVelocity();
		SavedDormantAngularVelocityDeg = AsteroidMesh->GetPhysicsAngularVelocityInDegrees();
		CachedDriftLinearVelocity = SavedDormantLinearVelocity;
		CachedDriftAngularVelocityDeg = SavedDormantAngularVelocityDeg;
	}
	else
	{
		SavedDormantLinearVelocity = CachedDriftLinearVelocity;
		SavedDormantAngularVelocityDeg = CachedDriftAngularVelocityDeg;
	}

	if (AsteroidMesh)
	{
		AsteroidMesh->SetSimulatePhysics(false);
		AsteroidMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	SetActorHiddenInGame(true);
	SetActorTickEnabled(false);
}

void ASpaceAsteroid::WakeFromDormantPool(bool bImmediateFade)
{
	bDeferInitialBuild = false;
	bManagedDormant = false;

	SetActorHiddenInGame(false);
	ApplySpawnMaterialFade(bImmediateFade);
	if (AsteroidMesh)
	{
		AsteroidMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
	ApplyChaosPhysicsSettings();

	if (AsteroidMesh && AsteroidMesh->IsSimulatingPhysics())
	{
		bPhysicsTierActive = true;
		AsteroidMesh->WakeRigidBody();
		AsteroidMesh->SetPhysicsLinearVelocity(SavedDormantLinearVelocity, false);
		AsteroidMesh->SetPhysicsAngularVelocityInDegrees(SavedDormantAngularVelocityDeg, false);
	}
	else
	{
		bPhysicsTierActive = false;
	}
}

void ASpaceAsteroid::SetPhysicsSimulationTier(bool bEnableChaosPhysics)
{
	const bool bIsGameWorld = GetWorld() && GetWorld()->IsGameWorld();
	const bool bTargetChaos = bEnableChaosPhysics && bSimulatePhysics && bIsGameWorld && !bManagedDormant;

	if (bTargetChaos == bPhysicsTierActive)
	{
		return;
	}

	if (!AsteroidMesh)
	{
		bPhysicsTierActive = false;
		return;
	}

	if (bTargetChaos)
	{
		bPhysicsTierActive = true;
		AsteroidMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		AsteroidMesh->SetSimulatePhysics(true);
		AsteroidMesh->WakeRigidBody();
		AsteroidMesh->SetPhysicsLinearVelocity(CachedDriftLinearVelocity, false);
		AsteroidMesh->SetPhysicsAngularVelocityInDegrees(CachedDriftAngularVelocityDeg, false);
	}
	else
	{
		if (AsteroidMesh->IsSimulatingPhysics())
		{
			CachedDriftLinearVelocity = AsteroidMesh->GetPhysicsLinearVelocity();
			CachedDriftAngularVelocityDeg = AsteroidMesh->GetPhysicsAngularVelocityInDegrees();
			AsteroidMesh->SetSimulatePhysics(false);
		}
		AsteroidMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		bPhysicsTierActive = false;
	}
}

void ASpaceAsteroid::StepKinematicDrift(float DeltaTime)
{
	if (bManagedDormant || bPhysicsTierActive || DeltaTime <= 0.0f)
	{
		return;
	}

	const FVector NextLoc = GetActorLocation() + CachedDriftLinearVelocity * DeltaTime;
	const float AngSpeedDeg = CachedDriftAngularVelocityDeg.Size();
	if (AngSpeedDeg > KINDA_SMALL_NUMBER)
	{
		const FVector Axis = CachedDriftAngularVelocityDeg / AngSpeedDeg;
		const FQuat DeltaQuat(Axis, FMath::DegreesToRadians(AngSpeedDeg * DeltaTime));
		const FQuat NextRot = (DeltaQuat * GetActorQuat()).GetNormalized();
		SetActorLocationAndRotation(NextLoc, NextRot, false, nullptr, ETeleportType::TeleportPhysics);
	}
	else
	{
		SetActorLocation(NextLoc, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void ASpaceAsteroid::BakePooledArchetype(int32 ArchetypeSeed)
{
	bDeferInitialBuild = false;
	Seed = FMath::Max(ArchetypeSeed, 0);
	RebuildAsteroidFromSeed();
	EnterDormantPool();
}

void ASpaceAsteroid::ActivatePooledWithSeed(int32 NewSeed, const FTransform& NewWorldTransform, bool bReusePrebakedGeometry, bool bImmediateFade)
{
	ResetColonizationState();
	bDeferInitialBuild = false;
	bManagedDormant = false;
	RuntimeSizeScale = FMath::Clamp(NewWorldTransform.GetScale3D().X, 0.25f, 10.0f);
	SavedDormantLinearVelocity = FVector::ZeroVector;
	SavedDormantAngularVelocityDeg = FVector::ZeroVector;

	// 이동 중 충돌 스위프 방지를 위해 콜리전을 끈 뒤 순간이동하고 다시 활성화
	if (AsteroidMesh)
	{
		AsteroidMesh->SetSimulatePhysics(false);
		AsteroidMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	SetActorTransform(NewWorldTransform, false, nullptr, ETeleportType::TeleportPhysics);
	SetActorHiddenInGame(false);

	if (AsteroidMesh)
	{
		AsteroidMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}

	if (bReusePrebakedGeometry && bHasBuiltOnce)
	{
		// 완제품 프리베이크 스위치 ON: 이미 구워진 메시·Convex 콜리전·재질은 그대로 유지하고 물리 상태와 유영 속도만 즉시 부여
		Seed = FMath::Max(NewSeed, 0);
		ApplySpawnMaterialFade(bImmediateFade);
		ApplyChaosPhysicsSettings();
		ApplySeededInitialDrift();
	}
	else
	{
		InitializeWithSeed(NewSeed);
		ApplySpawnMaterialFade(bImmediateFade);
	}
}

void ASpaceAsteroid::SetDistanceFadeRange(float InFadeStartCm, float InFadeEndCm)
{
	DistanceFadeEndCm = FMath::Max(InFadeEndCm, 800.0f);
	DistanceFadeStartCm = FMath::Clamp(InFadeStartCm, 200.0f, DistanceFadeEndCm - 100.0f);

	if (AsteroidMID)
	{
		AsteroidMID->SetScalarParameterValue(TEXT("FadeStartDistance"), DistanceFadeStartCm);
		AsteroidMID->SetScalarParameterValue(TEXT("FadeEndDistance"), DistanceFadeEndCm);
	}
}

void ASpaceAsteroid::RandomizeSeed()
{
	Seed = FMath::RandRange(1, 99999);
	RebuildAsteroidFromSeed();
}

bool ASpaceAsteroid::SampleSurfaceAnchor(int32 SampleSeed, FVector& OutLocalPoint, FVector& OutLocalNormal) const
{
	if (!AsteroidMesh)
	{
		return false;
	}

	const FProcMeshSection* Section = AsteroidMesh->GetProcMeshSection(0);
	if (!Section || Section->ProcVertexBuffer.Num() == 0)
	{
		return false;
	}

	const int32 NumVerts = Section->ProcVertexBuffer.Num();
	const int32 VtxIndex = static_cast<int32>(static_cast<uint32>(SampleSeed) % static_cast<uint32>(NumVerts));
	const FProcMeshVertex& Vtx = Section->ProcVertexBuffer[VtxIndex];

	OutLocalPoint = Vtx.Position;
	OutLocalNormal = Vtx.Normal.GetSafeNormal();
	if (OutLocalNormal.IsNearlyZero())
	{
		OutLocalNormal = OutLocalPoint.GetSafeNormal();
	}
	if (OutLocalNormal.IsNearlyZero())
	{
		OutLocalNormal = FVector::UpVector;
	}
	return true;
}

uint32 ASpaceAsteroid::ComputeBuildSignature() const
{
	uint32 Hash = GetTypeHash(Seed);
	Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(MinMassKg * 100.0f)));
	Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(MaxMassKg * 100.0f)));
	Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(MassDistributionExponent * 100.0f)));
	Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(RockDensityKgPerM3 * 100.0f)));
	Hash = HashCombine(Hash, GetTypeHash(bOverrideSeedMass));
	Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(ManualMassKg * 100.0f)));
	Hash = HashCombine(Hash, GetTypeHash(VisualSubdivisionLevel));
	Hash = HashCombine(Hash, GetTypeHash(CollisionSubdivisionLevel));
	Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(MacroDeformStrength * 1000.0f)));
	Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(RidgeSharpness * 1000.0f)));
	Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(MaxAnisotropy * 1000.0f)));
	Hash = HashCombine(Hash, GetTypeHash(MinCraterCount));
	Hash = HashCombine(Hash, GetTypeHash(MaxCraterCount));
	Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(CraterDepthScale * 1000.0f)));
	Hash = HashCombine(Hash, GetTypeHash(AsteroidMaterial.Get()));
	Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(BaseRimIntensity * 1000.0f)));
	Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(BaseBumpStrength * 1000.0f)));
	Hash = HashCombine(Hash, GetTypeHash(bSimulatePhysics));
	Hash = HashCombine(Hash, GetTypeHash(bOverridePhysicsMaterialValues));
	Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(Restitution * 1000.0f)));
	Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(Friction * 1000.0f)));
	Hash = HashCombine(Hash, GetTypeHash(OverridePhysicalMaterial.Get()));
	Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(LinearDamping * 1000.0f)));
	Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(AngularDamping * 1000.0f)));
	return Hash;
}

void ASpaceAsteroid::RebuildAsteroidFromSeed()
{
	if (!AsteroidMesh)
	{
		return;
	}

	const int32 ClampedSeed = FMath::Max(Seed, 0);
	FShapeSeedContext Context;
	Context.Stream.Initialize(ClampedSeed);
	BuildShapeContext(Context);

	// 1. 시드 -> 선형 반경 스케일(V^(1/3)) 공간에서 거듭제곱 분포로 샘플링 -> 세제곱하여 질량 및 1:1 목표 부피(cm³) 계산
	// (질량을 먼저 선형 보간한 뒤 세제곱근을 취하면 상위 88% 구간이 0.5~1.0 R_max로 압축되어 크기가 비슷해 보이는 현상을 방지합니다.)
	const float LowMass = FMath::Max(MinMassKg, 1.0f);
	const float HighMass = FMath::Max(MaxMassKg, LowMass);
	const float SafeDensity = FMath::Max(RockDensityKgPerM3, 10.0f);
	const double MinVolM3 = static_cast<double>(LowMass) / static_cast<double>(SafeDensity);
	const double MaxVolM3 = static_cast<double>(HighMass) / static_cast<double>(SafeDensity);
	const double MinRadiusScale = FMath::Pow(MinVolM3, 1.0 / 3.0);
	const double MaxRadiusScale = FMath::Pow(MaxVolM3, 1.0 / 3.0);
	const float RawFraction = Context.Stream.GetFraction();
	const FRichCurve* RichCurve = SizeDistributionCurve.GetRichCurveConst();
	const double RadiusAlpha = (RichCurve && RichCurve->GetNumKeys() > 0)
		? static_cast<double>(FMath::Clamp(RichCurve->Eval(RawFraction), 0.0f, 1.0f))
		: static_cast<double>(FMath::Pow(RawFraction, FMath::Max(MassDistributionExponent, 0.2f)));
	const double SampledRadiusScale = FMath::Lerp(MinRadiusScale, MaxRadiusScale, RadiusAlpha);
	const double SampledMassKg = static_cast<double>(SafeDensity) * SampledRadiusScale * SampledRadiusScale * SampledRadiusScale;

	GeneratedMassKg = bOverrideSeedMass
		? FMath::Max(ManualMassKg, 1.0f)
		: static_cast<float>(FMath::Clamp(SampledMassKg, static_cast<double>(LowMass), static_cast<double>(HighMass)));

	const double TargetVolumeM3 = static_cast<double>(GeneratedMassKg) / static_cast<double>(SafeDensity);
	const double TargetVolumeCm3 = TargetVolumeM3 * 1000000.0;

	// 2. 캐시된 단위 아이코스피어 토폴로지 참조 + 3D 노이즈·크레이터 변형 적용 (시드별 비대칭 + 요철 포함)
	const int32 ClampedVisualSubdiv = FMath::Clamp(VisualSubdivisionLevel, 1, 4);
	const FUnitIcosphereTemplate& VisualTemplate = GetCachedUnitIcosphere(ClampedVisualSubdiv);
	const TArray<FVector>& UnitVerts = VisualTemplate.UnitVerts;
	const TArray<int32>& Triangles = VisualTemplate.Triangles;

	const int32 NumVerts = UnitVerts.Num();
	TArray<FVector> RawDeformedVerts;
	RawDeformedVerts.SetNumUninitialized(NumVerts);

	TArray<FLinearColor> VertexColors;
	VertexColors.SetNumUninitialized(NumVerts);

	for (int32 Idx = 0; Idx < NumVerts; ++Idx)
	{
		const FVector& UnitDir = UnitVerts[Idx];
		float CraterMask = 0.5f;
		float RidgeMask = 0.0f;
		const float Displacement = EvaluateDisplacementAtUnitDir(UnitDir, Context, CraterMask, RidgeMask);

		// 비등방성 타원 변형 후 시드 회전 적용해 암석 장축 방향 결정
		FVector DeformedDir = UnitDir * Context.AxisStretch;
		DeformedDir = Context.StretchRotation.RotateVector(DeformedDir);
		RawDeformedVerts[Idx] = DeformedDir * Displacement;

		// VertexColor: R = 구덩이 어둠/능선 밝기, G = 노출된 모서리(광물 정맥/서리 림)
		const float HeightBrightness = FMath::Clamp(0.45f + (Displacement - 1.0f) * 1.4f - CraterMask * 0.42f, 0.06f, 1.0f);
		VertexColors[Idx] = FLinearColor(HeightBrightness, RidgeMask, 0.5f, 1.0f);
	}

	// 3. 실제 변형된 폐곡면 부피를 적분해 목표 부피(TargetVolumeCm3)와 정확히 1:1 일치하는 스케일 도출
	const double UnitMeshVolume = FMath::Max(ComputeSignedVolumeUnitMesh(RawDeformedVerts, Triangles), 1e-4);
	const double LinearScaleCm = FMath::Pow(TargetVolumeCm3 / UnitMeshVolume, 1.0 / 3.0);
	const float ScaleFloat = static_cast<float>(LinearScaleCm);

	TArray<FVector> Vertices;
	Vertices.SetNumUninitialized(NumVerts);
	for (int32 Idx = 0; Idx < NumVerts; ++Idx)
	{
		Vertices[Idx] = RawDeformedVerts[Idx] * ScaleFloat;
	}

	GeneratedVolumeM3 = static_cast<float>(TargetVolumeM3);
	GeneratedRadiusCm = static_cast<float>(FMath::Pow((3.0 * TargetVolumeCm3) / (4.0 * UE_DOUBLE_PI), 1.0 / 3.0));
	GeneratedVertexCount = NumVerts;
	GeneratedTriangleCount = Triangles.Num() / 3;
	GeneratedCraterCount = Context.Craters.Num();

	// 4. O(V + T) 면적 가중 스무스 노멀 및 직교 탄젠트 계산 (UE 왼손 CW 전면 규칙 준수)
	TArray<FVector> Normals;
	Normals.Init(FVector::ZeroVector, NumVerts);

	for (int32 TriIdx = 0; TriIdx + 2 < Triangles.Num(); TriIdx += 3)
	{
		const int32 I0 = Triangles[TriIdx];
		const int32 I1 = Triangles[TriIdx + 1];
		const int32 I2 = Triangles[TriIdx + 2];

		const FVector& A = Vertices[I0];
		const FVector& B = Vertices[I1];
		const FVector& C = Vertices[I2];

		const FVector FaceNormal = FVector::CrossProduct(C - A, B - A);
		Normals[I0] += FaceNormal;
		Normals[I1] += FaceNormal;
		Normals[I2] += FaceNormal;
	}

	TArray<FProcMeshTangent> Tangents;
	Tangents.SetNumUninitialized(NumVerts);
	for (int32 Idx = 0; Idx < NumVerts; ++Idx)
	{
		FVector Normal = Normals[Idx].GetSafeNormal();
		if (Normal.IsNearlyZero())
		{
			Normal = Vertices[Idx].GetSafeNormal();
		}
		Normals[Idx] = Normal;

		const FVector UpRef = (FMath::Abs(Normal.Z) < 0.9f) ? FVector::UpVector : FVector::ForwardVector;
		const FVector TangentDir = FVector::CrossProduct(UpRef, Normal).GetSafeNormal();
		Tangents[Idx] = FProcMeshTangent(TangentDir, false);
	}

	// 5. Chaos Convex 콜리전 메시 생성 (시각 메시와 동일 3D 변형 필드를 저분할 캐시로 샘플링)
	TArray<FVector> ConvexVerts;
	const int32 ClampedCollisionSubdiv = FMath::Clamp(CollisionSubdivisionLevel, 1, 3);
	if (ClampedCollisionSubdiv == ClampedVisualSubdiv)
	{
		ConvexVerts = Vertices;
	}
	else
	{
		const FUnitIcosphereTemplate& CollisionTemplate = GetCachedUnitIcosphere(ClampedCollisionSubdiv);
		const TArray<FVector>& ColUnitVerts = CollisionTemplate.UnitVerts;
		ConvexVerts.SetNumUninitialized(ColUnitVerts.Num());
		for (int32 Idx = 0; Idx < ColUnitVerts.Num(); ++Idx)
		{
			const FVector& UnitDir = ColUnitVerts[Idx];
			float UnusedCrater = 0.0f;
			float UnusedRidge = 0.0f;
			const float Disp = EvaluateDisplacementAtUnitDir(UnitDir, Context, UnusedCrater, UnusedRidge);
			const FVector DeformedDir = Context.StretchRotation.RotateVector(UnitDir * Context.AxisStretch);
			ConvexVerts[Idx] = DeformedDir * (Disp * ScaleFloat);
		}
	}

	AsteroidMesh->CreateMeshSection_LinearColor(
		0,
		Vertices,
		Triangles,
		Normals,
		VisualTemplate.SphericalUVs,
		VertexColors,
		Tangents,
		false
	);

	TArray<TArray<FVector>> ConvexMeshList;
	ConvexMeshList.Add(MoveTemp(ConvexVerts));
	AsteroidMesh->SetCollisionConvexMeshes(ConvexMeshList);

	// 6. 시드 기반 표면 질감 머티리얼 및 Chaos 물리 파라미터 반영
	ApplySeededSurfaceMaterial(Context.DetailNoiseOffset);
	ApplyChaosPhysicsSettings();

	LastBuiltSignature = ComputeBuildSignature();
	bHasBuiltOnce = true;
}

void ASpaceAsteroid::BuildShapeContext(FShapeSeedContext& OutContext)
{
	FRandomStream& S = OutContext.Stream;

	OutContext.StretchRotation = FQuat(FRotator(
		S.FRandRange(-180.0f, 180.0f),
		S.FRandRange(-180.0f, 180.0f),
		S.FRandRange(-180.0f, 180.0f)
	));

	const float Aniso = FMath::Clamp(MaxAnisotropy, 0.0f, 0.45f);
	OutContext.AxisStretch = FVector(
		S.FRandRange(1.0f - Aniso, 1.0f + Aniso),
		S.FRandRange(1.0f - Aniso, 1.0f + Aniso),
		S.FRandRange(1.0f - Aniso, 1.0f + Aniso)
	);

	OutContext.MacroNoiseOffset = FVector(S.FRandRange(-200.0f, 200.0f), S.FRandRange(-200.0f, 200.0f), S.FRandRange(-200.0f, 200.0f));
	OutContext.RidgeNoiseOffset = FVector(S.FRandRange(-200.0f, 200.0f), S.FRandRange(-200.0f, 200.0f), S.FRandRange(-200.0f, 200.0f));
	OutContext.DetailNoiseOffset = FVector(S.FRandRange(-120.0f, 120.0f), S.FRandRange(-120.0f, 120.0f), S.FRandRange(-120.0f, 120.0f));

	OutContext.MacroFreq = S.FRandRange(1.35f, 2.05f);
	OutContext.RidgeFreq = S.FRandRange(2.2f, 3.4f);

	const int32 MinC = FMath::Clamp(MinCraterCount, 0, 16);
	const int32 MaxC = FMath::Clamp(FMath::Max(MaxCraterCount, MinC), MinC, 16);
	const int32 CraterCount = S.RandRange(MinC, MaxC);

	OutContext.Craters.Reset(CraterCount);
	for (int32 i = 0; i < CraterCount; ++i)
	{
		FCraterSpec Crater;
		Crater.Direction = S.GetUnitVector();
		Crater.AngularRadiusRad = FMath::DegreesToRadians(S.FRandRange(16.0f, 36.0f));
		Crater.CosOuterRadius = FMath::Cos(FMath::Min(Crater.AngularRadiusRad * 1.33f, UE_PI));
		Crater.Depth = CraterDepthScale * S.FRandRange(0.55f, 1.15f);
		Crater.RimHeight = Crater.Depth * S.FRandRange(0.22f, 0.38f);
		OutContext.Craters.Add(Crater);
	}
}

float ASpaceAsteroid::EvaluateDisplacementAtUnitDir(
	const FVector& UnitDir,
	const FShapeSeedContext& Context,
	float& OutCraterMask,
	float& OutRidgeMask) const
{
	// 저주파 다옥타브 FBM: 전체 덩어리 기복과 깎인 암반 평면 형성
	const float N1 = FMath::PerlinNoise3D(UnitDir * Context.MacroFreq + Context.MacroNoiseOffset);
	const float N2 = FMath::PerlinNoise3D(UnitDir * (Context.MacroFreq * 2.15f) + Context.MacroNoiseOffset * 1.37f) * 0.45f;
	const float SmoothFbm = N1 + N2;

	// Ridge 노이즈(1 - |noise|): 날카로운 능선과 각진 암석 모서리 형성
	const float RawRidge = FMath::PerlinNoise3D(UnitDir * Context.RidgeFreq + Context.RidgeNoiseOffset);
	const float Ridge = 1.0f - FMath::Abs(RawRidge);
	const float SharpenedRidge = FMath::Pow(FMath::Clamp(Ridge, 0.0f, 1.0f), 1.8f) * 2.0f - 0.85f;

	const float RidgeBlend = FMath::Clamp(RidgeSharpness, 0.0f, 1.0f);
	const float MacroDeform = FMath::Lerp(SmoothFbm, SharpenedRidge, RidgeBlend) * MacroDeformStrength;

	// 미세 요철 고주파 노이즈 (실루엣 핀칭 방지를 위해 정점 요철은 절제하고 미세 거칠기는 셰이더 노멀에 위임)
	const float DetailNoise = FMath::PerlinNoise3D(UnitDir * 3.8f + Context.DetailNoiseOffset) * (MacroDeformStrength * 0.10f);

	// 시드 기반 운석 충돌 크레이터 합산 (중앙 함몰 + 외곽 분출 림)
	float CraterDelta = 0.0f;
	float MaxCraterDepthRatio = 0.0f;
	float MaxRimFactor = 0.0f;

	for (const FCraterSpec& Crater : Context.Craters)
	{
		const float Dot = FVector::DotProduct(UnitDir, Crater.Direction);
		if (Dot <= Crater.CosOuterRadius)
		{
			continue;
		}

		const float Angle = FMath::Acos(FMath::Clamp(Dot, -1.0f, 1.0f));
		const float NormDist = Angle / FMath::Max(Crater.AngularRadiusRad, 0.01f);

		if (NormDist < 1.32f)
		{
			if (NormDist < 1.0f)
			{
				const float Bowl = 1.0f - NormDist * NormDist;
				const float BowlFactor = FMath::Pow(Bowl, 1.35f);
				CraterDelta -= Crater.Depth * BowlFactor;
				MaxCraterDepthRatio = FMath::Max(MaxCraterDepthRatio, BowlFactor);
			}

			// 크레이터 외곽 림 (0.78 ~ 1.30 구간에 부드럽게 솟는 분출 테두리)
			if (NormDist >= 0.72f)
			{
				const float RimT = (NormDist - 0.72f) / 0.58f;
				const float RimWave = FMath::Sin(FMath::Clamp(RimT, 0.0f, 1.0f) * UE_PI);
				const float RimBoost = RimWave * RimWave;
				CraterDelta += Crater.RimHeight * RimBoost;
				MaxRimFactor = FMath::Max(MaxRimFactor, RimBoost);
			}
		}
	}

	OutCraterMask = FMath::Clamp(MaxCraterDepthRatio, 0.0f, 1.0f);
	OutRidgeMask = FMath::Clamp(FMath::Max(SharpenedRidge * 0.55f + 0.35f, MaxRimFactor * 1.15f), 0.0f, 1.0f);

	// 극단적 반전(0 이하 함몰) 방지를 위해 최소 반경 계수 보장
	return FMath::Max(1.0f + MacroDeform + DetailNoise + CraterDelta, 0.38f);
}

const ASpaceAsteroid::FUnitIcosphereTemplate& ASpaceAsteroid::GetCachedUnitIcosphere(int32 SubdivisionLevel)
{
	struct FUnitIcosphereBank
	{
		FUnitIcosphereTemplate Levels[4];

		FUnitIcosphereBank()
		{
			for (int32 Subdiv = 1; Subdiv <= 4; ++Subdiv)
			{
				FUnitIcosphereTemplate& Template = Levels[Subdiv - 1];
				BuildUnitIcosphere(Subdiv, Template.UnitVerts, Template.Triangles);

				const int32 NumVerts = Template.UnitVerts.Num();
				Template.SphericalUVs.SetNumUninitialized(NumVerts);
				for (int32 Idx = 0; Idx < NumVerts; ++Idx)
				{
					const FVector& UnitDir = Template.UnitVerts[Idx];
					const float U = 0.5f + FMath::Atan2(UnitDir.Y, UnitDir.X) / UE_TWO_PI;
					const float V = FMath::Acos(FMath::Clamp(UnitDir.Z, -1.0f, 1.0f)) / UE_PI;
					Template.SphericalUVs[Idx] = FVector2D(U, V);
				}
			}
		}
	};

	// C++11+ 정적 지역 변수 초기화는 스레드 안전하며 이후 불변(const) 캐시로 락 없이 재사용됩니다.
	static const FUnitIcosphereBank Bank;
	const int32 Index = FMath::Clamp(SubdivisionLevel, 1, 4) - 1;
	return Bank.Levels[Index];
}

void ASpaceAsteroid::BuildUnitIcosphere(int32 SubdivisionLevel, TArray<FVector>& OutUnitVerts, TArray<int32>& OutTriangles)
{
	OutUnitVerts.Reset();
	OutTriangles.Reset();

	const float T = (1.0f + FMath::Sqrt(5.0f)) * 0.5f;
	auto AddUnitVert = [&OutUnitVerts](float X, float Y, float Z)
	{
		OutUnitVerts.Add(FVector(X, Y, Z).GetSafeNormal());
	};

	AddUnitVert(-1, T, 0); AddUnitVert(1, T, 0); AddUnitVert(-1, -T, 0); AddUnitVert(1, -T, 0);
	AddUnitVert(0, -1, T); AddUnitVert(0, 1, T); AddUnitVert(0, -1, -T); AddUnitVert(0, 1, -T);
	AddUnitVert(T, 0, -1); AddUnitVert(T, 0, 1); AddUnitVert(-T, 0, -1); AddUnitVert(-T, 0, 1);

	const int32 BaseFaces[20][3] = {
		{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
		{1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
		{3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
		{4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}
	};

	// UE 왼손 좌표계 CW 전면 규칙((C - A) x (B - A)가 바깥쪽)으로 초기 면 방향 보장
	for (int32 FaceIdx = 0; FaceIdx < 20; ++FaceIdx)
	{
		const int32 A = BaseFaces[FaceIdx][0];
		int32 B = BaseFaces[FaceIdx][1];
		int32 C = BaseFaces[FaceIdx][2];

		const FVector OutCheck = FVector::CrossProduct(OutUnitVerts[C] - OutUnitVerts[A], OutUnitVerts[B] - OutUnitVerts[A]);
		const FVector Centroid = OutUnitVerts[A] + OutUnitVerts[B] + OutUnitVerts[C];
		if (FVector::DotProduct(OutCheck, Centroid) < 0.0f)
		{
			Swap(B, C);
		}

		OutTriangles.Add(A);
		OutTriangles.Add(B);
		OutTriangles.Add(C);
	}

	TMap<uint64, int32> MidpointCache;
	auto GetMidpoint = [&OutUnitVerts, &MidpointCache](int32 I0, int32 I1) -> int32
	{
		const uint64 Small = static_cast<uint64>(FMath::Min(I0, I1));
		const uint64 Large = static_cast<uint64>(FMath::Max(I0, I1));
		const uint64 EdgeKey = (Small << 32) | Large;

		if (const int32* Cached = MidpointCache.Find(EdgeKey))
		{
			return *Cached;
		}

		const FVector MidDir = ((OutUnitVerts[I0] + OutUnitVerts[I1]) * 0.5f).GetSafeNormal();
		const int32 NewIdx = OutUnitVerts.Add(MidDir);
		MidpointCache.Add(EdgeKey, NewIdx);
		return NewIdx;
	};

	for (int32 Sub = 0; Sub < SubdivisionLevel; ++Sub)
	{
		MidpointCache.Reset();
		TArray<int32> NextTris;
		NextTris.Reserve(OutTriangles.Num() * 4);

		for (int32 i = 0; i + 2 < OutTriangles.Num(); i += 3)
		{
			const int32 V0 = OutTriangles[i];
			const int32 V1 = OutTriangles[i + 1];
			const int32 V2 = OutTriangles[i + 2];

			const int32 A = GetMidpoint(V0, V1);
			const int32 B = GetMidpoint(V1, V2);
			const int32 C = GetMidpoint(V2, V0);

			NextTris.Append({ V0, A, C });
			NextTris.Append({ V1, B, A });
			NextTris.Append({ V2, C, B });
			NextTris.Append({ A, B, C });
		}

		OutTriangles = MoveTemp(NextTris);
	}
}

double ASpaceAsteroid::ComputeSignedVolumeUnitMesh(const TArray<FVector>& Verts, const TArray<int32>& Triangles)
{
	double VolumeSix = 0.0;
	for (int32 i = 0; i + 2 < Triangles.Num(); i += 3)
	{
		const FVector& A = Verts[Triangles[i]];
		const FVector& B = Verts[Triangles[i + 1]];
		const FVector& C = Verts[Triangles[i + 2]];
		VolumeSix += FVector::DotProduct(A, FVector::CrossProduct(C, B));
	}
	return FMath::Abs(VolumeSix) / 6.0;
}

void ASpaceAsteroid::ApplySeededSurfaceMaterial(const FVector& MaterialSeedOffset)
{
	if (!AsteroidMesh || !AsteroidMaterial)
	{
		return;
	}

	if (!AsteroidMID || AsteroidMID->Parent != AsteroidMaterial)
	{
		AsteroidMID = UMaterialInstanceDynamic::Create(AsteroidMaterial, this);
	}
	if (!AsteroidMID)
	{
		return;
	}

	AsteroidMesh->SetMaterial(0, AsteroidMID);

	// 질감용 독립 시드 스트림: 우주 배경/보라색 폰과 조화되는 3가지 암석 계열 중 연속 혼합
	FRandomStream SurfStream(FMath::Max(Seed, 0) ^ 0x5A17C3);
	const int32 ArchetypeRoll = SurfStream.RandRange(0, 2);

	FLinearColor DarkColor;
	FLinearColor LightColor;
	FLinearColor VeinColor;

	if (ArchetypeRoll == 0)
	{
		GeneratedArchetypeLabel = TEXT("Carbonaceous Basalt");
		DarkColor = FLinearColor(0.032f + SurfStream.FRandRange(0.0f, 0.015f), 0.035f + SurfStream.FRandRange(0.0f, 0.015f), 0.048f + SurfStream.FRandRange(0.0f, 0.018f), 1.0f);
		LightColor = FLinearColor(0.135f + SurfStream.FRandRange(0.0f, 0.045f), 0.145f + SurfStream.FRandRange(0.0f, 0.045f), 0.175f + SurfStream.FRandRange(0.0f, 0.05f), 1.0f);
		VeinColor = FLinearColor(0.36f, 0.44f, 0.56f, 1.0f);
	}
	else if (ArchetypeRoll == 1)
	{
		GeneratedArchetypeLabel = TEXT("Stony Silicate");
		DarkColor = FLinearColor(0.052f + SurfStream.FRandRange(0.0f, 0.016f), 0.048f + SurfStream.FRandRange(0.0f, 0.014f), 0.054f + SurfStream.FRandRange(0.0f, 0.016f), 1.0f);
		LightColor = FLinearColor(0.205f + SurfStream.FRandRange(0.0f, 0.055f), 0.195f + SurfStream.FRandRange(0.0f, 0.05f), 0.215f + SurfStream.FRandRange(0.0f, 0.055f), 1.0f);
		VeinColor = FLinearColor(0.46f, 0.49f, 0.58f, 1.0f);
	}
	else
	{
		GeneratedArchetypeLabel = TEXT("Cosmic Chondrite");
		DarkColor = FLinearColor(0.038f + SurfStream.FRandRange(0.0f, 0.014f), 0.045f + SurfStream.FRandRange(0.0f, 0.016f), 0.072f + SurfStream.FRandRange(0.0f, 0.022f), 1.0f);
		LightColor = FLinearColor(0.165f + SurfStream.FRandRange(0.0f, 0.05f), 0.185f + SurfStream.FRandRange(0.0f, 0.05f), 0.245f + SurfStream.FRandRange(0.0f, 0.055f), 1.0f);
		VeinColor = FLinearColor(0.44f, 0.62f, 0.88f, 1.0f);
	}

	// 반지름에 완만히 반비례하는 스케일 보정으로 거대 소행성도 고밀도 암반 질감 유지
	const float RadiusScaleFactor = FMath::Clamp(FMath::Pow(85.0f / FMath::Max(GeneratedRadiusCm, 20.0f), 0.72f), 0.48f, 1.75f);
	const float SurfScale = 0.026f * RadiusScaleFactor * SurfStream.FRandRange(0.82f, 1.22f);
	const float DetScale = 0.088f * RadiusScaleFactor * SurfStream.FRandRange(0.85f, 1.25f);

	const float VeinIntensityVal = SurfStream.FRandRange(0.28f, 0.54f);
	BaseSurfaceVeinColor = VeinColor;
	BaseSurfaceRimColor = FLinearColor(0.48f, 0.64f, 0.96f, 1.0f);
	BaseSurfaceVeinIntensity = VeinIntensityVal;
	BaseSurfaceRimIntensity = BaseRimIntensity;

	AsteroidMID->SetVectorParameterValue(TEXT("SeedOffset"), FLinearColor(MaterialSeedOffset.X, MaterialSeedOffset.Y, MaterialSeedOffset.Z, 0.0f));
	AsteroidMID->SetVectorParameterValue(TEXT("BaseColorDark"), DarkColor);
	AsteroidMID->SetVectorParameterValue(TEXT("BaseColorLight"), LightColor);
	AsteroidMID->SetVectorParameterValue(TEXT("VeinColor"), BaseSurfaceVeinColor);
	AsteroidMID->SetVectorParameterValue(TEXT("RimColor"), BaseSurfaceRimColor);

	AsteroidMID->SetScalarParameterValue(TEXT("SurfaceScale"), SurfScale);
	AsteroidMID->SetScalarParameterValue(TEXT("DetailScale"), DetScale);
	AsteroidMID->SetScalarParameterValue(TEXT("VeinIntensity"), BaseSurfaceVeinIntensity);
	AsteroidMID->SetScalarParameterValue(TEXT("RoughnessMin"), SurfStream.FRandRange(0.56f, 0.72f));
	AsteroidMID->SetScalarParameterValue(TEXT("RoughnessMax"), SurfStream.FRandRange(0.88f, 0.97f));
	AsteroidMID->SetScalarParameterValue(TEXT("MicroBumpStrength"), BaseBumpStrength * SurfStream.FRandRange(0.82f, 1.24f));
	AsteroidMID->SetScalarParameterValue(TEXT("RimIntensity"), BaseSurfaceRimIntensity);

	// 수동 배치·에디터 프리뷰·초기 베이킹 시에는 기본적으로 즉시 표시되도록 초기화
	ApplySpawnMaterialFade(true);
}

void ASpaceAsteroid::ApplySpawnMaterialFade(bool bImmediateFade)
{
	if (!AsteroidMID && AsteroidMesh && AsteroidMaterial)
	{
		AsteroidMID = UMaterialInstanceDynamic::Create(AsteroidMaterial, this);
		if (AsteroidMID)
		{
			AsteroidMesh->SetMaterial(0, AsteroidMID);
		}
	}
	if (!AsteroidMID)
	{
		return;
	}

	const float ClampedDur = FMath::Max(SpawnFadeDuration, 0.05f);
	const float SafeEndDist = FMath::Max(DistanceFadeEndCm, 800.0f);
	const float SafeStartDist = FMath::Clamp(DistanceFadeStartCm, 200.0f, SafeEndDist - 100.0f);

	const UWorld* World = GetWorld();
	const float WorldTime = (World && World->IsGameWorld()) ? static_cast<float>(World->GetTimeSeconds()) : 0.0f;
	const float SpawnTimestamp = bImmediateFade ? -100.0f : WorldTime;

	AsteroidMID->SetScalarParameterValue(TEXT("SpawnTime"), SpawnTimestamp);
	AsteroidMID->SetScalarParameterValue(TEXT("SpawnFadeDuration"), ClampedDur);
	AsteroidMID->SetScalarParameterValue(TEXT("FadeStartDistance"), SafeStartDist);
	AsteroidMID->SetScalarParameterValue(TEXT("FadeEndDistance"), SafeEndDist);
}

void ASpaceAsteroid::ApplyChaosPhysicsSettings()
{
	if (!AsteroidMesh)
	{
		return;
	}

	AsteroidMesh->SetEnableGravity(false);
	AsteroidMesh->SetLinearDamping(FMath::Max(LinearDamping, 0.0f));
	AsteroidMesh->SetAngularDamping(FMath::Max(AngularDamping, 0.0f));
	AsteroidMesh->SetMassOverrideInKg(NAME_None, GetEffectiveMassKg(), true);

	if (bOverridePhysicsMaterialValues)
	{
		if (!RuntimePhysicalMaterial)
		{
			RuntimePhysicalMaterial = NewObject<UPhysicalMaterial>(this);
		}
		if (RuntimePhysicalMaterial)
		{
			RuntimePhysicalMaterial->Restitution = FMath::Clamp(Restitution, 0.0f, 1.0f);
			RuntimePhysicalMaterial->Friction = FMath::Max(Friction, 0.0f);
			RuntimePhysicalMaterial->StaticFriction = RuntimePhysicalMaterial->Friction;
			RuntimePhysicalMaterial->bOverrideRestitutionCombineMode = true;
			RuntimePhysicalMaterial->RestitutionCombineMode = EFrictionCombineMode::Average;
			RuntimePhysicalMaterial->SleepLinearVelocityThreshold = 0.0f;
			RuntimePhysicalMaterial->SleepAngularVelocityThreshold = 0.0f;
			AsteroidMesh->SetPhysMaterialOverride(RuntimePhysicalMaterial);
		}
	}
	else
	{
		AsteroidMesh->SetPhysMaterialOverride(OverridePhysicalMaterial);
	}

	const bool bIsGameWorld = GetWorld() && GetWorld()->IsGameWorld();
	const bool bShouldSim = bSimulatePhysics && bIsGameWorld && !bManagedDormant;
	AsteroidMesh->SetSimulatePhysics(bShouldSim);
	bPhysicsTierActive = bShouldSim;
}

namespace
{
	float EvaluateSymmetricDispersion(float RandU, float Dispersion)
	{
		const float ClampedU = FMath::Clamp(RandU, 0.0f, 1.0f);
		const float SafeDispersion = FMath::Clamp(Dispersion, 0.05f, 8.0f);
		const float SignedU = ClampedU * 2.0f - 1.0f;
		const float AbsPow = FMath::Pow(FMath::Abs(SignedU), 1.0f / SafeDispersion);
		const float Mapped = (SignedU >= 0.0f) ? AbsPow : -AbsPow;
		return FMath::Clamp((Mapped + 1.0f) * 0.5f, 0.0f, 1.0f);
	}
}

void ASpaceAsteroid::ApplyFieldSpeedOverrides(
	const FVector2D& InLinearSpeedRange,
	float InLinearSpeedDispersion,
	const FVector2D& InAngularSpeedDegRange,
	float InAngularSpeedDispersion)
{
	InitialLinearSpeedRange = InLinearSpeedRange;
	LinearSpeedDistributionExponent = FMath::Clamp(InLinearSpeedDispersion, 0.05f, 8.0f);
	InitialAngularSpeedDegRange = InAngularSpeedDegRange;
	AngularSpeedDistributionExponent = FMath::Clamp(InAngularSpeedDispersion, 0.05f, 8.0f);
	if (FRichCurve* SpeedCurve = SpeedDistributionCurve.GetRichCurve())
	{
		SpeedCurve->Reset();
	}
}

void ASpaceAsteroid::ApplySeededInitialDrift()
{
	if (!bApplySeededInitialDrift)
	{
		CachedDriftLinearVelocity = FVector::ZeroVector;
		CachedDriftAngularVelocityDeg = FVector::ZeroVector;
		return;
	}

	FRandomStream DriftStream(FMath::Max(Seed, 0) ^ 0x3D91F7);
	const float MinLin = FMath::Min(InitialLinearSpeedRange.X, InitialLinearSpeedRange.Y);
	const float MaxLin = FMath::Max(InitialLinearSpeedRange.X, InitialLinearSpeedRange.Y);
	const float RawSpeedFraction = DriftStream.GetFraction();
	const FRichCurve* SpeedCurve = SpeedDistributionCurve.GetRichCurveConst();
	const float SpeedAlpha = (SpeedCurve && SpeedCurve->GetNumKeys() > 0)
		? FMath::Clamp(SpeedCurve->Eval(RawSpeedFraction), 0.0f, 1.0f)
		: EvaluateSymmetricDispersion(RawSpeedFraction, LinearSpeedDistributionExponent);

	// 질량 대비 관성 편차와 개별 소행성만의 무작위 역학 배수를 결합해 거의 멈춘 바위부터 총알처럼 스쳐 지나가는 유성체까지 혼돈스러운 속도 분산을 형성합니다.
	const float MassDampFactor = FMath::Clamp(FMath::Pow(650.0f / FMath::Max(GetEffectiveMassKg(), 25.0f), 0.24f), 0.28f, 1.65f);
	const float ChaosBurstMultiplier = (DriftStream.GetFraction() < 0.14f)
		? DriftStream.FRandRange(1.25f, 1.65f)
		: DriftStream.FRandRange(0.55f, 1.25f);
	const float LinearSpeed = FMath::Lerp(MinLin, MaxLin, SpeedAlpha) * MassDampFactor * ChaosBurstMultiplier;
	const FVector DriftVel = DriftStream.GetUnitVector() * LinearSpeed;

	const float MinAng = FMath::Min(InitialAngularSpeedDegRange.X, InitialAngularSpeedDegRange.Y);
	const float MaxAng = FMath::Max(InitialAngularSpeedDegRange.X, InitialAngularSpeedDegRange.Y);
	const float AngAlpha = EvaluateSymmetricDispersion(DriftStream.GetFraction(), AngularSpeedDistributionExponent);
	const float AngularDegSpeed = FMath::Lerp(MinAng, MaxAng, AngAlpha) * FMath::Clamp(MassDampFactor, 0.30f, 1.45f);
	const FVector SpinDegVel = DriftStream.GetUnitVector() * AngularDegSpeed;

	CachedDriftLinearVelocity = DriftVel;
	CachedDriftAngularVelocityDeg = SpinDegVel;

	if (bSimulatePhysics && AsteroidMesh && AsteroidMesh->IsSimulatingPhysics())
	{
		AsteroidMesh->SetPhysicsLinearVelocity(DriftVel, false);
		AsteroidMesh->SetPhysicsAngularVelocityInDegrees(SpinDegVel, false);
	}
}

float ASpaceAsteroid::GetRequiredColonizeBiomass() const
{
	const float RadiusRatio = FMath::Max(GetGeneratedRadiusCm(), 20.0f) / FMath::Max(ColonizeReferenceRadiusCm, 10.0f);
	return FMath::Max(ColonizeRequiredBiomassBase, 1.0f) * FMath::Pow(RadiusRatio, FMath::Clamp(ColonizeCostRadiusExponent, 0.5f, 4.0f));
}

void ASpaceAsteroid::SetColonizationProgressVisual(float Progress)
{
	if (ColonizationState == EAsteroidColonizationState::ColonizedIncubating)
	{
		return;
	}

	const float ClampedProgress = FMath::Clamp(Progress, 0.0f, 1.0f);
	ColonizationProgress = ClampedProgress;
	ColonizationState = (ClampedProgress > KINDA_SMALL_NUMBER)
		? EAsteroidColonizationState::Colonizing
		: EAsteroidColonizationState::Uncolonized;

	if (AsteroidMID)
	{
		AsteroidMID->SetVectorParameterValue(TEXT("VeinColor"), FMath::Lerp(BaseSurfaceVeinColor, ColonizedSkinTint, ClampedProgress));
		AsteroidMID->SetVectorParameterValue(TEXT("RimColor"), FMath::Lerp(BaseSurfaceRimColor, ColonizedSkinTint, ClampedProgress));
		AsteroidMID->SetScalarParameterValue(TEXT("VeinIntensity"), FMath::Lerp(BaseSurfaceVeinIntensity, 1.8f, ClampedProgress));
		AsteroidMID->SetScalarParameterValue(TEXT("RimIntensity"), FMath::Lerp(BaseSurfaceRimIntensity, 2.4f, ClampedProgress));
	}
}

void ASpaceAsteroid::CompleteColonization(UMaterialInterface* HostSkinMaterial, AAsteroidFieldManager* FieldMgr)
{
	if (ColonizationState == EAsteroidColonizationState::ColonizedIncubating)
	{
		return;
	}

	ColonizationState = EAsteroidColonizationState::ColonizedIncubating;
	ColonizationProgress = 1.0f;
	OwnerFieldManager = FieldMgr;

	if (AsteroidMesh && HostSkinMaterial)
	{
		AsteroidMesh->SetMaterial(0, HostSkinMaterial);
	}
	else
	{
		SetColonizationProgressVisual(1.0f);
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ColonizationIncubationTimerHandle);
		World->GetTimerManager().SetTimer(
			ColonizationIncubationTimerHandle,
			this,
			&ASpaceAsteroid::OnColonizationIncubationExpired,
			FMath::Max(ColonizeIncubationSeconds, 0.2f),
			false
		);
	}
}

void ASpaceAsteroid::ResetColonizationState()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ColonizationIncubationTimerHandle);
	}

	ColonizationState = EAsteroidColonizationState::Uncolonized;
	ColonizationProgress = 0.0f;
	OwnerFieldManager.Reset();

	if (AsteroidMesh && AsteroidMID)
	{
		AsteroidMesh->SetMaterial(0, AsteroidMID);
		AsteroidMID->SetVectorParameterValue(TEXT("VeinColor"), BaseSurfaceVeinColor);
		AsteroidMID->SetVectorParameterValue(TEXT("RimColor"), BaseSurfaceRimColor);
		AsteroidMID->SetScalarParameterValue(TEXT("VeinIntensity"), BaseSurfaceVeinIntensity);
		AsteroidMID->SetScalarParameterValue(TEXT("RimIntensity"), BaseSurfaceRimIntensity);
	}
}

void ASpaceAsteroid::OnColonizationIncubationExpired()
{
	if (bManagedDormant || ColonizationState != EAsteroidColonizationState::ColonizedIncubating)
	{
		return;
	}

	if (AAsteroidFieldManager* FieldMgr = OwnerFieldManager.Get())
	{
		FieldMgr->ExplodeColonizedAsteroid(this);
	}
}
