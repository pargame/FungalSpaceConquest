#include "SpaceEnemyMold.h"

#include "AsteroidFieldManager.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnemyMoldManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "SpaceDrifterHUDWidget.h"
#include "SpaceDrifterPawn.h"
#include "SpaceDrifterPlayerController.h"
#include "TimerManager.h"

ASpaceEnemyMold::ASpaceEnemyMold()
{
	PrimaryActorTick.bCanEverTick = false;

	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	CollisionSphere->InitSphereRadius(90.0f);
	CollisionSphere->SetCollisionProfileName(TEXT("PhysicsActor"));
	// 포식자가 플레이어 뒤쪽 카메라 레이에 걸려 스프링암을 급격히 당기거나 화면이 어지럽게 흔들리지 않도록 카메라 채널 무시
	CollisionSphere->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	CollisionSphere->SetEnableGravity(false);
	CollisionSphere->SetSimulatePhysics(false);
	CollisionSphere->SetLinearDamping(LinearDamping);
	CollisionSphere->SetAngularDamping(1.2f);
	CollisionSphere->SetNotifyRigidBodyCollision(false);
	SetRootComponent(CollisionSphere);

	MoldMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MoldMesh"));
	MoldMesh->SetupAttachment(CollisionSphere);
	MoldMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MoldMesh->SetGenerateOverlapEvents(false);
	MoldMesh->SetBoundsScale(1.6f);
}

void ASpaceEnemyMold::BeginPlay()
{
	Super::BeginPlay();
	EnsureVisualAssets();
}

void ASpaceEnemyMold::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(IncubationTimerHandle);
		World->GetTimerManager().ClearTimer(WebSlowTimerHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void ASpaceEnemyMold::EnsureVisualAssets()
{
	if (!MoldStaticMesh)
	{
		MoldStaticMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Sandbox/MiniProject/Meshes/SM_DustPuffSphere.SM_DustPuffSphere"));
		if (!MoldStaticMesh)
		{
			MoldStaticMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		}
	}

	if (MoldMesh && MoldStaticMesh && MoldMesh->GetStaticMesh() != MoldStaticMesh)
	{
		MoldMesh->SetStaticMesh(MoldStaticMesh);
	}

	if (!MoldBaseMaterial)
	{
		MoldBaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Sandbox/MiniProject/Materials/M_EnemyMold.M_EnemyMold"));
		if (!MoldBaseMaterial)
		{
			MoldBaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Sandbox/MiniProject/Materials/M_SpaceMoteOrb.M_SpaceMoteOrb"));
		}
	}

	if (MoldMesh && MoldBaseMaterial && !EnemyMID)
	{
		EnemyMID = UMaterialInstanceDynamic::Create(MoldBaseMaterial, this);
		if (EnemyMID)
		{
			EnemyMID->SetVectorParameterValue(TEXT("BaseColor"), EnemyTintColor);
			EnemyMID->SetVectorParameterValue(TEXT("Color"), EnemyTintColor);
			EnemyMID->SetVectorParameterValue(TEXT("EmissiveColor"), EnemyTintColor * 1.4f);
			EnemyMID->SetVectorParameterValue(TEXT("RimColor"), FLinearColor(0.95f, 0.08f, 0.28f, 1.0f));
			MoldMesh->SetMaterial(0, EnemyMID);
		}
	}

	if (!RuntimePhysicalMaterial && CollisionSphere)
	{
		RuntimePhysicalMaterial = NewObject<UPhysicalMaterial>(this);
		if (RuntimePhysicalMaterial)
		{
			RuntimePhysicalMaterial->Restitution = 0.72f;
			RuntimePhysicalMaterial->Friction = 0.25f;
			CollisionSphere->SetPhysMaterialOverride(RuntimePhysicalMaterial);
		}
	}
}

void ASpaceEnemyMold::ApplyBodySize(float InRadiusCm)
{
	CurrentRadiusCm = FMath::Max(InRadiusCm, 30.0f);

	if (CollisionSphere)
	{
		CollisionSphere->SetSphereRadius(CurrentRadiusCm, true);
		const float ScaleFactor = CurrentRadiusCm / 75.0f;
		const float MassKg = FMath::Max(BaseMassKg * FMath::Pow(ScaleFactor, 3.0f), 60.0f);
		CollisionSphere->SetMassOverrideInKg(NAME_None, MassKg, true);
	}

	if (MoldMesh)
	{
		const float MeshRadius = (MoldStaticMesh ? FMath::Max(MoldStaticMesh->GetBounds().SphereRadius, 1.0f) : 50.0f);
		const float UniformScale = CurrentRadiusCm / MeshRadius;
		MoldMesh->SetRelativeScale3D(FVector(UniformScale));
	}
}

void ASpaceEnemyMold::ActivateAt(AEnemyMoldManager* InManager, const FVector& WorldLocation, float InBiomass, float InRadiusCm)
{
	OwnerManager = InManager;
	EnsureVisualAssets();
	ResetColonizationState();

	CurrentBiomass = FMath::Max(InBiomass, 20.0f);
	InitialSpawnRadiusCm = FMath::Max(InRadiusCm, 30.0f);
	bIsDormant = false;

	SetActorLocationAndRotation(WorldLocation, FRotator::ZeroRotator, false, nullptr, ETeleportType::TeleportPhysics);
	ApplyBodySize(InRadiusCm);

	SetActorHiddenInGame(false);
	SetCameraProximityHidden(false);
	SetActorEnableCollision(true);

	if (CollisionSphere)
	{
		CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		CollisionSphere->SetLinearDamping(LinearDamping);
		CollisionSphere->SetSimulatePhysics(true);
		CollisionSphere->SetPhysicsLinearVelocity(FVector::ZeroVector);
		CollisionSphere->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}
}

void ASpaceEnemyMold::EnterDormant()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(IncubationTimerHandle);
		World->GetTimerManager().ClearTimer(WebSlowTimerHandle);
	}

	bIsDormant = true;
	bIsWebSlowed = false;
	WebSlowSpeedMultiplier = 1.0f;
	ColonizationState = EEnemyMoldColonizationState::Uncolonized;
	ColonizationProgress = 0.0f;
	OwnerFieldManager.Reset();

	if (CollisionSphere)
	{
		CollisionSphere->SetSimulatePhysics(false);
		CollisionSphere->SetPhysicsLinearVelocity(FVector::ZeroVector);
		CollisionSphere->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		CollisionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	SetActorEnableCollision(false);
	SetActorHiddenInGame(true);
}

bool ASpaceEnemyMold::CanBeColonized() const
{
	return !bIsDormant && !IsHidden() && ColonizationState != EEnemyMoldColonizationState::ColonizedIncubating;
}

float ASpaceEnemyMold::GetRequiredColonizeBiomass() const
{
	return FMath::Max(CurrentBiomass * FMath::Max(ColonizeCostMultiplier, 0.2f), 10.0f);
}

void ASpaceEnemyMold::SetColonizationProgressVisual(float Progress)
{
	if (ColonizationState == EEnemyMoldColonizationState::ColonizedIncubating)
	{
		return;
	}

	const float ClampedProgress = FMath::Clamp(Progress, 0.0f, 1.0f);
	ColonizationProgress = ClampedProgress;
	ColonizationState = (ClampedProgress > KINDA_SMALL_NUMBER)
		? EEnemyMoldColonizationState::Colonizing
		: EEnemyMoldColonizationState::Uncolonized;

	if (EnemyMID)
	{
		EnemyMID->SetScalarParameterValue(TEXT("InfectionProgress"), ClampedProgress);
		const FLinearColor InfectedMint(0.24f, 0.96f, 0.74f, 1.0f);
		const FLinearColor Blended = FMath::Lerp(EnemyTintColor, InfectedMint, ClampedProgress);
		EnemyMID->SetVectorParameterValue(TEXT("BaseColor"), Blended);
		EnemyMID->SetVectorParameterValue(TEXT("Color"), Blended);
		EnemyMID->SetVectorParameterValue(TEXT("EmissiveColor"), Blended * (1.4f + ClampedProgress * 2.0f));
	}
}

void ASpaceEnemyMold::CompleteColonization(UMaterialInterface* HostSkinMaterial, AAsteroidFieldManager* FieldMgr)
{
	if (ColonizationState == EEnemyMoldColonizationState::ColonizedIncubating)
	{
		return;
	}

	ColonizationState = EEnemyMoldColonizationState::ColonizedIncubating;
	ColonizationProgress = 1.0f;
	OwnerFieldManager = FieldMgr;

	if (MoldMesh && HostSkinMaterial)
	{
		MoldMesh->SetMaterial(0, HostSkinMaterial);
	}
	else
	{
		SetColonizationProgressVisual(1.0f);
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(IncubationTimerHandle);
		World->GetTimerManager().SetTimer(
			IncubationTimerHandle,
			this,
			&ASpaceEnemyMold::OnColonizationIncubationExpired,
			FMath::Max(ColonizeIncubationSeconds, 0.2f),
			false
		);
	}
}

void ASpaceEnemyMold::ResetColonizationState()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(IncubationTimerHandle);
	}

	ColonizationState = EEnemyMoldColonizationState::Uncolonized;
	ColonizationProgress = 0.0f;
	OwnerFieldManager.Reset();

	if (MoldMesh && EnemyMID)
	{
		MoldMesh->SetMaterial(0, EnemyMID);
		bIsHuntableVisual = false;
		EnemyMID->SetScalarParameterValue(TEXT("InfectionProgress"), 0.0f);
		EnemyMID->SetScalarParameterValue(TEXT("IsHuntable"), 0.0f);
		EnemyMID->SetVectorParameterValue(TEXT("BaseColor"), EnemyTintColor);
		EnemyMID->SetVectorParameterValue(TEXT("Color"), EnemyTintColor);
		EnemyMID->SetVectorParameterValue(TEXT("EmissiveColor"), EnemyTintColor * 1.4f);
		EnemyMID->SetVectorParameterValue(TEXT("RimColor"), FLinearColor(0.95f, 0.08f, 0.28f, 1.0f));
	}
}

void ASpaceEnemyMold::OnColonizationIncubationExpired()
{
	if (bIsDormant || ColonizationState != EEnemyMoldColonizationState::ColonizedIncubating)
	{
		return;
	}

	const FVector ExplosionCenter = GetActorLocation();
	const float RewardBiomass = FMath::Max(CurrentBiomass * FMath::Max(DefeatRewardBiomassMultiplier, 1.0f), 30.0f);
	const int32 BurstCount = FMath::Clamp(FMath::RoundToInt(CurrentRadiusCm / 12.0f), 8, 22);

	int32 Spawned = 0;
	if (AAsteroidFieldManager* FieldMgr = OwnerFieldManager.Get())
	{
		Spawned = FieldMgr->SpawnMineralBurst(
			ExplosionCenter,
			CurrentRadiusCm,
			RewardBiomass,
			BurstCount,
			static_cast<int32>(FMath::RoundToInt(CurrentBiomass * 17.0f)),
			true
		);
	}

	if (const UWorld* World = GetWorld())
	{
		if (ASpaceDrifterPawn* DrifterPawn = Cast<ASpaceDrifterPawn>(UGameplayStatics::GetPlayerPawn(World, 0)))
		{
			DrifterPawn->NotifyEnemyDefeatedByPlayer();
			if (!DrifterPawn->IsRunEnded())
			{
				if (const ASpaceDrifterPlayerController* PC = Cast<ASpaceDrifterPlayerController>(DrifterPawn->GetController()))
				{
					if (USpaceDrifterHUDWidget* HUD = PC->GetHUDWidget())
					{
						HUD->SetStatusBannerMessage(
							FString::Printf(TEXT("[적군 곰팡이 역포식! +1점] 미네랄 %d개 대량 흡수!"), Spawned),
							FLinearColor(0.35f, 1.0f, 0.78f, 1.0f),
							2.4f
						);
					}
				}
			}
		}
	}

	AEnemyMoldManager* Manager = OwnerManager.Get();
	EnterDormant();

	if (Manager)
	{
		Manager->NotifyEnemyDefeated(this);
	}
}

void ASpaceEnemyMold::ApplyWebSlow(float DurationSeconds, float SpeedMultiplier)
{
	if (bIsDormant || ColonizationState == EEnemyMoldColonizationState::ColonizedIncubating)
	{
		return;
	}

	const float TargetMul = FMath::Clamp(SpeedMultiplier, 0.1f, 1.0f);
	if (bIsWebSlowed)
	{
		// 이미 그물에 걸린 포식자가 다시 맞으면 영구 이동속도가 추가 감소 (최소 20% 하한)
		WebSlowSpeedMultiplier = FMath::Max(FMath::Min(WebSlowSpeedMultiplier * 0.85f, TargetMul), 0.20f);
	}
	else
	{
		bIsWebSlowed = true;
		WebSlowSpeedMultiplier = TargetMul;
	}

	// 그물에 감긴 순간 즉각 속력을 꺾어 속박 손맛 부여
	if (CollisionSphere && CollisionSphere->IsSimulatingPhysics())
	{
		const FVector CurVel = CollisionSphere->GetPhysicsLinearVelocity();
		CollisionSphere->SetPhysicsLinearVelocity(CurVel * WebSlowSpeedMultiplier);
	}

	if (EnemyMID)
	{
		// 사냥 가능(녹색/시안)과 혼동되지 않도록 약화(위협 유지) 상태는 보라/마젠타 림 색상으로 표시
		EnemyMID->SetVectorParameterValue(TEXT("RimColor"), FLinearColor(0.72f, 0.14f, 0.82f, 1.0f));
	}

	// 영구 감소 적용: 만료 타이머를 돌리지 않고 처치(풀 반환)될 때까지 둔화 상태 유지
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(WebSlowTimerHandle);
	}
	(void)DurationSeconds;
}

void ASpaceEnemyMold::OnWebSlowTimerExpired()
{
	bIsWebSlowed = false;
	WebSlowSpeedMultiplier = 1.0f;

	if (EnemyMID)
	{
		EnemyMID->SetVectorParameterValue(TEXT("RimColor"), FLinearColor(0.95f, 0.08f, 0.28f, 1.0f));
	}
}

bool ASpaceEnemyMold::ApplyWebShrink(float ShrinkPercent, float MinRadiusRatio)
{
	if (bIsDormant || ColonizationState == EEnemyMoldColonizationState::ColonizedIncubating || ShrinkPercent <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const float OldRadius = FMath::Max(CurrentRadiusCm, 30.0f);
	const float MinAllowedRadius = FMath::Max(InitialSpawnRadiusCm * FMath::Clamp(MinRadiusRatio, 0.25f, 0.95f), 30.0f);
	const float ShrinkMul = FMath::Clamp(1.0f - (ShrinkPercent * 0.01f), 0.25f, 1.0f);
	const float NewRadius = FMath::Max(OldRadius * ShrinkMul, MinAllowedRadius);

	if (NewRadius >= OldRadius - KINDA_SMALL_NUMBER)
	{
		return false;
	}

	// 반지름 축소에 맞춰 생체량도 성장 역지수(1/0.40 = 2.5)로 연동 감소시켜 역포식(사냥 가능) 전환 유도
	const float Ratio = NewRadius / OldRadius;
	CurrentBiomass = FMath::Max(CurrentBiomass * FMath::Pow(Ratio, 2.5f), 15.0f);
	ApplyBodySize(NewRadius);
	return true;
}

void ASpaceEnemyMold::SetHuntableVisual(bool bInHuntable)
{
	if (bIsHuntableVisual == bInHuntable)
	{
		return;
	}
	bIsHuntableVisual = bInHuntable;
	if (EnemyMID)
	{
		EnemyMID->SetScalarParameterValue(TEXT("IsHuntable"), bInHuntable ? 1.0f : 0.0f);
	}
}

void ASpaceEnemyMold::SetCameraProximityHidden(bool bShouldHide)
{
	if (MoldMesh)
	{
		const bool bTargetVisible = !bShouldHide;
		if (MoldMesh->IsVisible() != bTargetVisible)
		{
			MoldMesh->SetVisibility(bTargetVisible, false);
		}
	}
}
