#include "SpaceDrifterPawn.h"

#include "AsteroidFieldManager.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "FXSystemComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "SpaceDrifterHUDWidget.h"
#include "SpaceDrifterLeaderboardSave.h"
#include "SpaceDrifterPlayerController.h"
#include "SpaceMineral.h"
#include "SpaceTendrilAbility.h"
#include "TimerManager.h"
#include "UEStudy.h"

ASpaceDrifterPawn::ASpaceDrifterPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	// Chaos 물리 시뮬레이션(TG_DuringPhysics)이 본체 위치를 갱신한 직후 동일 프레임에서 촉수 시작점을 동기화하도록 TG_PostPhysics로 설정합니다.
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	CollisionSphere->InitSphereRadius(50.0f);
	CollisionSphere->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);
	CollisionSphere->SetEnableGravity(false);
	CollisionSphere->SetNotifyRigidBodyCollision(true);
	CollisionSphere->BodyInstance.bLockXRotation = true;
	CollisionSphere->BodyInstance.bLockYRotation = true;
	CollisionSphere->BodyInstance.bLockZRotation = true;
	SetRootComponent(CollisionSphere);

	DrifterMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DrifterMesh"));
	DrifterMesh->SetupAttachment(CollisionSphere);
	DrifterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	CameraPivot = CreateDefaultSubobject<USceneComponent>(TEXT("CameraPivot"));
	CameraPivot->SetupAttachment(CollisionSphere);

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(CameraPivot);
	CameraBoom->TargetArmLength = 390.0f;
	CameraBoom->SocketOffset = FVector(0.0f, 0.0f, 78.0f);
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->bInheritPitch = true;
	CameraBoom->bInheritYaw = true;
	CameraBoom->bInheritRoll = true;
	CameraBoom->bDoCollisionTest = true;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 6.0f;
	CameraBoom->bEnableCameraRotationLag = false;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	LongTendrilAbility = CreateDefaultSubobject<ULongTendrilAbility>(TEXT("LongTendrilAbility"));
	NetTendrilAbility = CreateDefaultSubobject<UNetTendrilAbility>(TEXT("NetTendrilAbility"));
	JetTendrilAbility = CreateDefaultSubobject<UJetTendrilAbility>(TEXT("JetTendrilAbility"));
}

void ASpaceDrifterPawn::BeginPlay()
{
	Super::BeginPlay();

	// 블루프린트 CDO에 이전 틱 그룹이 직렬화되어 있어도 런타임에 반드시 물리 적분 이후(TG_PostPhysics)에 Tick하도록 보장합니다.
	SetTickGroup(TG_PostPhysics);

	PreviousActorLocation = GetActorLocation();
	PreviousWorldVelocity = FVector::ZeroVector;
	bHasVelocityHistory = false;
	bSuppressAccelDeformThisTick = false;
	bIsExtendInputHeld = false;
	bIsGameOver = false;
	ConsumedMineralCount = 0;
	CurrentBiomass = FMath::Max(InitialBiomass, 10.0f);

	if (CollisionSphere)
	{
		CollisionSphere->OnComponentHit.AddUniqueDynamic(this, &ASpaceDrifterPawn::OnCollisionSphereHit);
	}
	ApplyChaosPhysicsSettings();

	if (DrifterMesh)
	{
		InitialMeshRelativeLocation = DrifterMesh->GetRelativeLocation();
		InitialMeshRelativeScale = DrifterMesh->GetRelativeScale3D();
		if (DrifterMesh->GetMaterial(0))
		{
			DrifterMID = DrifterMesh->CreateAndSetMaterialInstanceDynamic(0);
		}
	}

	UpdateBodyScaleFromBiomass();
	MaxReachedBodyRadiusCm = CurrentBodyRadiusCm;

	if (CameraBoom)
	{
		if (CameraPivot && CameraBoom->GetAttachParent() != CameraPivot)
		{
			CameraBoom->AttachToComponent(CameraPivot, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		}
		BaseCameraArmLength = FMath::Clamp(CameraBoom->TargetArmLength, MinCameraDistance, MaxCameraDistance);
		BaseCameraSocketOffset = CameraBoom->SocketOffset;
		CameraBoom->SocketOffset = BaseCameraSocketOffset * FMath::Pow(GetTendrilBodyScale(), FMath::Max(CameraSocketOffsetBodyScaleExponent, 0.0f));
		CameraBoom->CameraLagSpeed = FMath::Max(BaseCameraLagSpeed, 1.0f);
		CameraBoom->CameraLagMaxDistance = FMath::Max(MaxCameraLagDistanceCm, 0.0f);
	}

	if (CameraPivot)
	{
		TargetCameraOrbitQuat = CameraPivot->GetRelativeTransform().GetRotation();
		SmoothedCameraOrbitQuat = TargetCameraOrbitQuat;
	}
	else
	{
		TargetCameraOrbitQuat = FQuat::Identity;
		SmoothedCameraOrbitQuat = FQuat::Identity;
	}

	RunState = ESpaceRunState::Playing;
	bIsGameOver = false;
	ElapsedSurvivalSeconds = 0.0f;
	DefeatedEnemyCount = 0;

	ResolveTendrilComponents();
	InitializeTendrilAbilities();
	PushDeformToMaterial();
}

void ASpaceDrifterPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	ElapsedFloatTime += DeltaTime;

	if (!IsRunEnded())
	{
		bool bInTitle = false;
		if (const ASpaceDrifterPlayerController* PC = Cast<ASpaceDrifterPlayerController>(GetController()))
		{
			bInTitle = PC->IsInTitleScreen();
		}
		if (!bInTitle)
		{
			ElapsedSurvivalSeconds += DeltaTime;
		}
	}

	if (bEnableZeroGFloat && DrifterMesh)
	{
		const float VerticalOffset = FMath::Sin(ElapsedFloatTime * FloatFrequency * UE_TWO_PI) * FloatAmplitude;
		DrifterMesh->SetRelativeLocation(InitialMeshRelativeLocation + FVector(0.0f, 0.0f, VerticalOffset));
		DrifterMesh->AddLocalRotation(IdleTumbleRate * DeltaTime);
	}

	UpdateCameraOrbit(DeltaTime);

	// [언리얼 조건부 0-Tick 설계]: 평상시(Idle)에는 촉수 능력을 Tick하지 않으며, 발동/회수 중에만 구동합니다.
	if (ActiveTendrilAbility && ActiveTendrilAbility->IsAbilityTickActive())
	{
		ActiveTendrilAbility->TickActiveAbility(DeltaTime);
	}

	SweepDirectContactMinerals();
	UpdateElasticity(DeltaTime);
	PushDeformToMaterial();

	if (GEngine)
	{
		if (bShowOnScreenDebugMessages && !bIsGameOver)
		{
			const float SpeedCm = GetTendrilBodyVelocity().Size();
			const float SpeedM = SpeedCm * 0.01f;
			const float RadiusCm = GetTendrilBodyRadius();
			const float RadiusM = RadiusCm * 0.01f;
			const float BodyScale = GetTendrilBodyScale();
			const TCHAR* TendrilLabel = TEXT("V1 긴 촉수");
			if (TendrilVisualStyle == ETendrilVisualStyle::GossamerSpores)
			{
				TendrilLabel = TEXT("V2 그물 촉수");
			}
			else if (TendrilVisualStyle == ETendrilVisualStyle::SporeJet)
			{
				TendrilLabel = TEXT("V3 분사 촉수");
			}

			const FString StatusLine = FString::Printf(
				TEXT("[Fungal Drifter]  속도: %.2f m/s (%.0f cm/s)  |  반지름: %.2f m (%.1f cm, x%.2f)  |  생체량: %.0f  |  촉수: %s"),
				SpeedM, SpeedCm, RadiusM, RadiusCm, BodyScale, CurrentBiomass, TendrilLabel
			);
			GEngine->AddOnScreenDebugMessage(1001, 0.0f, FColor(135, 245, 210), StatusLine, false, FVector2D(1.15f, 1.15f));
		}
		else
		{
			GEngine->ClearOnScreenDebugMessages();
		}
	}
}

void ASpaceDrifterPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (PlayerInputComponent)
	{
		PlayerInputComponent->BindKey(EKeys::One, IE_Pressed, this, &ASpaceDrifterPawn::OnSelectTendrilV1);
		PlayerInputComponent->BindKey(EKeys::Two, IE_Pressed, this, &ASpaceDrifterPawn::OnSelectTendrilV2);
		PlayerInputComponent->BindKey(EKeys::Three, IE_Pressed, this, &ASpaceDrifterPawn::OnSelectTendrilV3);
	}

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (LookAction)
		{
			EnhancedInput->BindAction(LookAction, ETriggerEvent::Triggered, this, &ASpaceDrifterPawn::Look);
		}
		if (MouseLookAction)
		{
			EnhancedInput->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &ASpaceDrifterPawn::Look);
		}
		if (TestBounceAction)
		{
			EnhancedInput->BindAction(TestBounceAction, ETriggerEvent::Started, this, &ASpaceDrifterPawn::OnTestBounce);
		}
		if (ExtendTendrilAction)
		{
			EnhancedInput->BindAction(ExtendTendrilAction, ETriggerEvent::Started, this, &ASpaceDrifterPawn::OnExtendTendrilStarted);
			EnhancedInput->BindAction(ExtendTendrilAction, ETriggerEvent::Completed, this, &ASpaceDrifterPawn::OnExtendTendrilEnded);
			EnhancedInput->BindAction(ExtendTendrilAction, ETriggerEvent::Canceled, this, &ASpaceDrifterPawn::OnExtendTendrilEnded);
		}
	}
	else
	{
		UE_LOG(LogUEStudy, Error, TEXT("'%s' failed to find an EnhancedInputComponent."), *GetNameSafe(this));
	}
}

void ASpaceDrifterPawn::Look(const FInputActionValue& Value)
{
	const FVector2D LookAxis = Value.Get<FVector2D>();
	DoLook(LookAxis.X, LookAxis.Y);
}

void ASpaceDrifterPawn::OnTestBounce(const FInputActionValue& Value)
{
	ApplyDeformImpulse(FVector(0.0f, 0.0f, -TestBounceImpulse));
}

void ASpaceDrifterPawn::OnExtendTendrilStarted(const FInputActionValue& Value)
{
	bIsExtendInputHeld = true;
	SetTendrilExtending(true);
}

void ASpaceDrifterPawn::OnExtendTendrilEnded(const FInputActionValue& Value)
{
	bIsExtendInputHeld = false;
	if (ActiveTendrilAbility)
	{
		ActiveTendrilAbility->OnInputEnded();
	}
}

void ASpaceDrifterPawn::OnToggleTendrilStyle(const FInputActionValue& Value)
{
	ToggleTendrilVisualStyle();
}

void ASpaceDrifterPawn::OnSelectTendrilV1()
{
	if (const ASpaceDrifterPlayerController* PC = Cast<ASpaceDrifterPlayerController>(GetController()))
	{
		if (PC->IsInTitleScreen() || PC->IsPauseMenuOpen()) { return; }
	}
	SetTendrilVisualStyle(ETendrilVisualStyle::MyceliumWeb);
}

void ASpaceDrifterPawn::OnSelectTendrilV2()
{
	if (const ASpaceDrifterPlayerController* PC = Cast<ASpaceDrifterPlayerController>(GetController()))
	{
		if (PC->IsInTitleScreen() || PC->IsPauseMenuOpen()) { return; }
	}
	SetTendrilVisualStyle(ETendrilVisualStyle::GossamerSpores);
}

void ASpaceDrifterPawn::OnSelectTendrilV3()
{
	if (const ASpaceDrifterPlayerController* PC = Cast<ASpaceDrifterPlayerController>(GetController()))
	{
		if (PC->IsInTitleScreen() || PC->IsPauseMenuOpen()) { return; }
	}
	SetTendrilVisualStyle(ETendrilVisualStyle::SporeJet);
}

void ASpaceDrifterPawn::DoLook(float Yaw, float Pitch)
{
	AController* PC = GetController();
	if (!PC || PC->IsLookInputIgnored())
	{
		return;
	}
	if (const ASpaceDrifterPlayerController* DrifterPC = Cast<ASpaceDrifterPlayerController>(GetController()))
	{
		if (DrifterPC->IsInTitleScreen() || DrifterPC->IsPauseMenuOpen() || IsRunEnded())
		{
			return;
		}
	}

	const float ScaledYawRad = FMath::DegreesToRadians(Yaw * OrbitLookSensitivity);
	const float ScaledPitchRad = FMath::DegreesToRadians(Pitch * OrbitLookSensitivity);

	const FQuat YawDelta(FVector::UpVector, ScaledYawRad);
	const FQuat PitchDelta(FVector::RightVector, ScaledPitchRad);
	TargetCameraOrbitQuat = (TargetCameraOrbitQuat * YawDelta * PitchDelta).GetNormalized();

	if (OrbitRotationLagSpeed <= 0.0f && CameraPivot)
	{
		SmoothedCameraOrbitQuat = TargetCameraOrbitQuat;
		CameraPivot->SetRelativeRotation(SmoothedCameraOrbitQuat);
	}
}

void ASpaceDrifterPawn::UpdateCameraOrbit(float DeltaTime)
{
	if (!CameraPivot)
	{
		return;
	}

	const ASpaceDrifterPlayerController* DrifterPC = Cast<ASpaceDrifterPlayerController>(GetController());
	const bool bShouldAutoOrbit = (DrifterPC && DrifterPC->IsInTitleScreen()) || IsRunEnded();
	if (bShouldAutoOrbit && TitleCameraOrbitSpeedDegPerSec > KINDA_SMALL_NUMBER)
	{
		const float AutoYawRad = FMath::DegreesToRadians(TitleCameraOrbitSpeedDegPerSec * DeltaTime);
		const FQuat AutoYawDelta(FVector::UpVector, AutoYawRad);
		TargetCameraOrbitQuat = (TargetCameraOrbitQuat * AutoYawDelta).GetNormalized();
	}

	if (OrbitRotationLagSpeed > 0.0f)
	{
		SmoothedCameraOrbitQuat = FMath::QInterpTo(SmoothedCameraOrbitQuat, TargetCameraOrbitQuat, DeltaTime, OrbitRotationLagSpeed).GetNormalized();
	}
	else
	{
		SmoothedCameraOrbitQuat = TargetCameraOrbitQuat;
	}

	CameraPivot->SetRelativeRotation(SmoothedCameraOrbitQuat);

	if (CameraBoom)
	{
		const FVector CurrentVel = GetTendrilBodyVelocity();
		const float SpeedAlpha = FMath::Clamp(CurrentVel.Size() / FMath::Max(SpeedCameraReferenceSpeed, 200.0f), 0.0f, 1.0f);
		const float BodyScale = GetTendrilBodyScale();
		const float CameraSizeScale = FMath::Pow(BodyScale, FMath::Max(CameraArmBodyScaleExponent, 0.0f));
		const float SocketOffsetScale = FMath::Pow(BodyScale, FMath::Max(CameraSocketOffsetBodyScaleExponent, 0.0f));
		const float ScaledBaseArm = BaseCameraArmLength * CameraSizeScale;
		const float DesiredArmLen = bEnableSpeedDynamicCameraArm
			? (ScaledBaseArm + FMath::Max(SpeedCameraMaxExtensionCm, 0.0f) * CameraSizeScale * SpeedAlpha)
			: ScaledBaseArm;

		CameraBoom->TargetArmLength = FMath::FInterpTo(
			CameraBoom->TargetArmLength,
			DesiredArmLen,
			DeltaTime,
			FMath::Max(SpeedCameraInterpSpeed, 0.5f)
		);
		CameraBoom->SocketOffset = BaseCameraSocketOffset * SocketOffsetScale;

		CameraBoom->CameraLagSpeed = FMath::Lerp(
			FMath::Max(BaseCameraLagSpeed, 1.0f),
			FMath::Max(MaxSpeedCameraLagSpeed, BaseCameraLagSpeed),
			SpeedAlpha
		);
		CameraBoom->CameraLagMaxDistance = FMath::Max(MaxCameraLagDistanceCm, 0.0f) * CameraSizeScale;
	}
}

void ASpaceDrifterPawn::DoZoom(float ZoomDelta)
{
	BaseCameraArmLength = FMath::Clamp(BaseCameraArmLength + ZoomDelta, MinCameraDistance, MaxCameraDistance);
	if (CameraBoom && !bEnableSpeedDynamicCameraArm)
	{
		const float CameraSizeScale = FMath::Pow(GetTendrilBodyScale(), FMath::Max(CameraArmBodyScaleExponent, 0.0f));
		CameraBoom->TargetArmLength = BaseCameraArmLength * CameraSizeScale;
	}
}

// ============================================================================
// ISpaceTendrilHostInterface 구현
// ============================================================================

FVector ASpaceDrifterPawn::GetTendrilVisualOrigin() const
{
	return DrifterMesh ? DrifterMesh->GetComponentLocation() : GetActorLocation();
}

float ASpaceDrifterPawn::GetTendrilBodyRadius() const
{
	return CollisionSphere ? CollisionSphere->GetScaledSphereRadius() : CurrentBodyRadiusCm;
}

float ASpaceDrifterPawn::GetTendrilBodyScale() const
{
	return FMath::Max(CurrentBodyRadiusCm / FMath::Max(InitialBodyRadiusCm, 10.0f), 0.1f);
}

float ASpaceDrifterPawn::GetTendrilMinGameOverBiomass() const
{
	const float Ratio = FMath::Clamp(MinGameOverBodyRadiusCm / FMath::Max(InitialBodyRadiusCm, 10.0f), 0.05f, 0.99f);
	const float InvExp = 1.0f / FMath::Clamp(BiomassToRadiusExponent, 0.2f, 0.8f);
	return FMath::Max(InitialBiomass, 10.0f) * FMath::Pow(Ratio, InvExp);
}

bool ASpaceDrifterPawn::CanSpendTendrilBiomass(float Amount, bool bAllowGameOver) const
{
	if (IsRunEnded())
	{
		return false;
	}
	if (Amount <= 0.0f)
	{
		return true;
	}
	if (bAllowGameOver)
	{
		return CurrentBiomass > GetTendrilMinGameOverBiomass();
	}
	return (CurrentBiomass - Amount) > GetTendrilMinGameOverBiomass();
}

bool ASpaceDrifterPawn::SpendTendrilBiomass(float Amount, bool bTriggerGameOverOnDeplete)
{
	if (bIsGameOver)
	{
		return false;
	}

	const float MinBiomass = GetTendrilMinGameOverBiomass();
	CurrentBiomass = FMath::Max(CurrentBiomass - FMath::Max(Amount, 0.0f), MinBiomass);
	UpdateBodyScaleFromBiomass();

	if (CurrentBiomass <= MinBiomass + KINDA_SMALL_NUMBER)
	{
		if (bTriggerGameOverOnDeplete)
		{
			TriggerGameOver();
		}
		return false;
	}
	return true;
}

void ASpaceDrifterPawn::RefundTendrilBiomass(float Amount)
{
	if (bIsGameOver || Amount <= 0.0f)
	{
		return;
	}

	CurrentBiomass += Amount;
	UpdateBodyScaleFromBiomass();
}

void ASpaceDrifterPawn::PostTendrilStatusBanner(const FString& Message, const FLinearColor& Color, float DurationSeconds)
{
	if (const ASpaceDrifterPlayerController* PC = Cast<ASpaceDrifterPlayerController>(GetController()))
	{
		if (USpaceDrifterHUDWidget* HUD = PC->GetHUDWidget())
		{
			HUD->SetStatusBannerMessage(Message, Color, DurationSeconds);
		}
	}
}

FVector ASpaceDrifterPawn::GetTendrilBodyVelocity() const
{
	return (CollisionSphere && CollisionSphere->IsSimulatingPhysics())
		? CollisionSphere->GetPhysicsLinearVelocity()
		: GetVelocity();
}

void ASpaceDrifterPawn::GetTendrilCameraBasis(
	FVector& OutCamLocation,
	FVector& OutCamForward,
	FVector& OutCamRight,
	FVector& OutCamUp) const
{
	if (FollowCamera)
	{
		OutCamLocation = FollowCamera->GetComponentLocation();
		OutCamForward = FollowCamera->GetForwardVector().GetSafeNormal();
		OutCamRight = FollowCamera->GetRightVector();
		OutCamUp = FollowCamera->GetUpVector();
	}
	else
	{
		OutCamLocation = GetActorLocation();
		OutCamForward = GetActorForwardVector().GetSafeNormal();
		OutCamRight = GetActorRightVector();
		OutCamUp = GetActorUpVector();
	}
}

UPrimitiveComponent* ASpaceDrifterPawn::GetTendrilPhysicsComponent() const
{
	return CollisionSphere.Get();
}

UMaterialInterface* ASpaceDrifterPawn::GetTendrilBodyMaterial() const
{
	if (DrifterMID)
	{
		return DrifterMID.Get();
	}
	return DrifterMesh ? DrifterMesh->GetMaterial(0) : nullptr;
}

UFXSystemComponent* ASpaceDrifterPawn::GetTendrilFXComponent(ETendrilVisualStyle Style) const
{
	switch (Style)
	{
	case ETendrilVisualStyle::MyceliumWeb:
		return MyceliumWebFX.Get();
	case ETendrilVisualStyle::GossamerSpores:
		return GossamerSporesFX.Get();
	case ETendrilVisualStyle::SporeJet:
		return SporeJetFX.Get();
	default:
		return nullptr;
	}
}

AAsteroidFieldManager* ASpaceDrifterPawn::GetActiveAsteroidFieldManager() const
{
	if (CachedFieldManager.IsValid())
	{
		return CachedFieldManager.Get();
	}

	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AAsteroidFieldManager> It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				CachedFieldManager = *It;
				return *It;
			}
		}
	}
	return nullptr;
}

void ASpaceDrifterPawn::ApplyHostDeformImpulse(const FVector& WorldImpulse, float StrengthScale)
{
	ApplyDeformImpulse(WorldImpulse, StrengthScale);
}

// ============================================================================
// 촉수 능력(Ability) 관리 및 라우팅
// ============================================================================

void ASpaceDrifterPawn::InitializeTendrilAbilities()
{
	if (!LongTendrilAbility)
	{
		LongTendrilAbility = NewObject<ULongTendrilAbility>(this, TEXT("LongTendrilAbility"));
	}
	if (!NetTendrilAbility)
	{
		NetTendrilAbility = NewObject<UNetTendrilAbility>(this, TEXT("NetTendrilAbility"));
	}
	if (!JetTendrilAbility)
	{
		JetTendrilAbility = NewObject<UJetTendrilAbility>(this, TEXT("JetTendrilAbility"));
	}

	SyncAbilitiesConfig();

	TScriptInterface<ISpaceTendrilHostInterface> HostIfc(this);
	LongTendrilAbility->InitializeAbility(HostIfc);
	NetTendrilAbility->InitializeAbility(HostIfc);
	JetTendrilAbility->InitializeAbility(HostIfc);

	if (!IsTendrilStyleUnlocked(TendrilVisualStyle))
	{
		TendrilVisualStyle = ETendrilVisualStyle::MyceliumWeb;
	}
	ActiveTendrilAbility = ResolveAbilityForStyle(TendrilVisualStyle);
}

void ASpaceDrifterPawn::SyncAbilitiesConfig()
{
	if (LongTendrilAbility)
	{
		LongTendrilAbility->SyncCommonParameters(TendrilExtendSpeed, TendrilRetractSpeed, TendrilRootInset, TendrilBodyStretch, TendrilRetractImpulse);
		LongTendrilAbility->MaxLengthCm = LongTendrilMaxLength;
		LongTendrilAbility->TrunkWidthCm = LongTendrilTrunkWidth;
		LongTendrilAbility->TipWidthRatio = LongTendrilTipWidthRatio;
		LongTendrilAbility->BranchFrequency = LongTendrilBranchFrequency;
		LongTendrilAbility->BranchMinTrunkCm = LongTendrilBranchMinTrunkCm;
		LongTendrilAbility->BranchLengthCm = LongTendrilBranchLength;
		LongTendrilAbility->BranchWidthCm = LongTendrilBranchWidth;
		LongTendrilAbility->BaseToTipLengthRatio = LongTendrilBaseToTipLengthRatio;
		LongTendrilAbility->BranchStartRatio = LongTendrilBranchStartRatio;
		LongTendrilAbility->BranchSpreadScale = LongTendrilBranchSpreadScale;
		LongTendrilAbility->bLimitHoldDuration = bLimitLongTendrilHoldDuration;
		LongTendrilAbility->MaxHoldDurationSeconds = LongTendrilMaxHoldDuration;
		LongTendrilAbility->bLimitHoldWhileLatched = bLimitHoldDurationWhileLatched;
		LongTendrilAbility->AimTraceRadiusCm = TendrilAimTraceRadius;
		LongTendrilAbility->PullForce = TendrilPullForce;
		LongTendrilAbility->MaxRelativePullSpeedCmPerSec = MaxRelativePullSpeed;
		LongTendrilAbility->TorqueTransferRatio = TendrilTorqueTransferRatio;
		LongTendrilAbility->PullActivationAlpha = TendrilPullActivationAlpha;
		LongTendrilAbility->bAutoDetachOnTargetHit = bAutoDetachTendrilOnTargetHit;
		LongTendrilAbility->MaxTetherBreakMultiplier = MaxTetherBreakDistanceMultiplier;
		LongTendrilAbility->SizeScaleReachExponent = V1LengthGrowthExponent;
		LongTendrilAbility->ColonizeTriggerSurfaceDistanceCm = ColonizeTriggerSurfaceDistanceCm;
		LongTendrilAbility->ColonizeUsablePercentPerSec = ColonizeUsablePercentPerSec;
	}

	if (NetTendrilAbility)
	{
		const float BodyRadius = GetTendrilBodyRadius();
		const float EffectiveDirectRadius = BodyRadius * FMath::Max(DirectContactAbsorbRadiusMultiplier, 1.0f) + FMath::Max(DirectContactAbsorbMarginCm, 0.0f);

		NetTendrilAbility->SyncCommonParameters(TendrilExtendSpeed, NetTendrilRetractSpeed, TendrilRootInset, TendrilBodyStretch, TendrilRetractImpulse);
		NetTendrilAbility->MaxReachCm = NetTendrilMaxReach;
		NetTendrilAbility->NetRetractSpeed = NetTendrilRetractSpeed;
		NetTendrilAbility->HoldDurationSeconds = NetTendrilHoldDuration;
		NetTendrilAbility->CapturePaddingCm = NetCapturePaddingCm;
		NetTendrilAbility->DirectContactMarginCm = FMath::Max(EffectiveDirectRadius - BodyRadius, 0.0f);
		NetTendrilAbility->SizeScaleReachExponent = V2ReachGrowthExponent;
		NetTendrilAbility->SizeScaleFunnelRadiusExponent = V2FunnelRadiusGrowthExponent;
	}

	if (JetTendrilAbility)
	{
		JetTendrilAbility->SyncCommonParameters(TendrilExtendSpeed, TendrilRetractSpeed, TendrilRootInset, TendrilBodyStretch, TendrilRetractImpulse);
		JetTendrilAbility->ThrustAccelerationCmPerSec2 = JetTendrilThrustAccel;
		JetTendrilAbility->SpeedDampingReferenceCmPerSec = JetTendrilSpeedDampingRef;
		JetTendrilAbility->SpeedInverseDecayRatio = JetTendrilSpeedInverseRatio;
		JetTendrilAbility->VisualReachCm = JetTendrilVisualLength;
		JetTendrilAbility->ProjectileSpawnRateScale = JetTendrilSpawnRate;
		JetTendrilAbility->JetBiomassCostPerSecond = JetBiomassCostPerSecond;
		JetTendrilAbility->JetParticleToBodySizeRatio = JetParticleToBodySizeRatio;
	}
}

USpaceTendrilAbility* ASpaceDrifterPawn::ResolveAbilityForStyle(ETendrilVisualStyle Style) const
{
	switch (Style)
	{
	case ETendrilVisualStyle::MyceliumWeb:
		return LongTendrilAbility.Get();
	case ETendrilVisualStyle::GossamerSpores:
		return NetTendrilAbility.Get();
	case ETendrilVisualStyle::SporeJet:
		return JetTendrilAbility.Get();
	default:
		return LongTendrilAbility.Get();
	}
}

void ASpaceDrifterPawn::SetTendrilExtending(bool bExtend)
{
	if (IsRunEnded() && bExtend)
	{
		return;
	}
	if (bExtend)
	{
		if (const ASpaceDrifterPlayerController* PC = Cast<ASpaceDrifterPlayerController>(GetController()))
		{
			if (PC->IsInTitleScreen() || PC->IsPauseMenuOpen() || PC->IsSuppressingTendrilUntilSpaceRelease())
			{
				return;
			}
		}
	}

	if (!ActiveTendrilAbility)
	{
		if (!IsTendrilStyleUnlocked(TendrilVisualStyle))
		{
			TendrilVisualStyle = ETendrilVisualStyle::MyceliumWeb;
		}
		ActiveTendrilAbility = ResolveAbilityForStyle(TendrilVisualStyle);
		if (!ActiveTendrilAbility)
		{
			return;
		}
	}

	if (bExtend)
	{
		ActiveTendrilAbility->OnInputStarted();
	}
	else
	{
		ActiveTendrilAbility->BeginRetract(true);
	}
}

bool ASpaceDrifterPawn::IsTendrilStyleUnlocked(ETendrilVisualStyle Style) const
{
	switch (Style)
	{
	case ETendrilVisualStyle::GossamerSpores:
		return DefeatedEnemyCount >= FMath::Max(V2UnlockDefeatedEnemyCount, 0);
	case ETendrilVisualStyle::SporeJet:
		return DefeatedEnemyCount >= FMath::Max(V3UnlockDefeatedEnemyCount, 0);
	case ETendrilVisualStyle::MyceliumWeb:
	default:
		return true;
	}
}

int32 ASpaceDrifterPawn::GetRequiredDefeatsForStyle(ETendrilVisualStyle Style) const
{
	switch (Style)
	{
	case ETendrilVisualStyle::GossamerSpores:
		return FMath::Max(V2UnlockDefeatedEnemyCount, 0);
	case ETendrilVisualStyle::SporeJet:
		return FMath::Max(V3UnlockDefeatedEnemyCount, 0);
	case ETendrilVisualStyle::MyceliumWeb:
	default:
		return 0;
	}
}

void ASpaceDrifterPawn::SetTendrilVisualStyle(ETendrilVisualStyle NewStyle)
{
	if (IsRunEnded())
	{
		return;
	}

	if (!IsTendrilStyleUnlocked(NewStyle))
	{
		const int32 Required = GetRequiredDefeatsForStyle(NewStyle);
		if (const ASpaceDrifterPlayerController* PC = Cast<ASpaceDrifterPlayerController>(GetController()))
		{
			if (USpaceDrifterHUDWidget* HUD = PC->GetHUDWidget())
			{
				HUD->TriggerLockedAbilityWarning(NewStyle, Required, DefeatedEnemyCount);
			}
		}
		return;
	}

	if (TendrilVisualStyle == NewStyle && ActiveTendrilAbility)
	{
		return;
	}

	if (ActiveTendrilAbility)
	{
		ActiveTendrilAbility->DeactivateAbilityImmediate();
	}

	TendrilVisualStyle = NewStyle;
	SyncAbilitiesConfig();
	ActiveTendrilAbility = ResolveAbilityForStyle(TendrilVisualStyle);

	if (ActiveTendrilAbility)
	{
		if (ActiveTendrilAbility->IsHoldToMaintainAbility() && bIsExtendInputHeld)
		{
			ActiveTendrilAbility->OnInputStarted();
		}

		if (bShowOnScreenDebugMessages && GEngine)
		{
			GEngine->AddOnScreenDebugMessage(901, 2.5f, FColor(175, 200, 255), ActiveTendrilAbility->GetAbilityDisplayName());
		}
	}
}

void ASpaceDrifterPawn::ToggleTendrilVisualStyle()
{
	if (IsRunEnded())
	{
		return;
	}

	static const ETendrilVisualStyle Order[3] = {
		ETendrilVisualStyle::MyceliumWeb,
		ETendrilVisualStyle::GossamerSpores,
		ETendrilVisualStyle::SporeJet
	};

	int32 CurrentIdx = 0;
	for (int32 i = 0; i < 3; ++i)
	{
		if (Order[i] == TendrilVisualStyle)
		{
			CurrentIdx = i;
			break;
		}
	}

	for (int32 Step = 1; Step <= 2; ++Step)
	{
		const ETendrilVisualStyle Candidate = Order[(CurrentIdx + Step) % 3];
		if (IsTendrilStyleUnlocked(Candidate))
		{
			SetTendrilVisualStyle(Candidate);
			return;
		}
	}

	if (const ASpaceDrifterPlayerController* PC = Cast<ASpaceDrifterPlayerController>(GetController()))
	{
		if (USpaceDrifterHUDWidget* HUD = PC->GetHUDWidget())
		{
			HUD->TriggerLockedAbilityWarning(
				ETendrilVisualStyle::GossamerSpores,
				FMath::Max(V2UnlockDefeatedEnemyCount, 0),
				DefeatedEnemyCount
			);
		}
	}
}

float ASpaceDrifterPawn::GetInvestedColonizeBiomass() const
{
	return LongTendrilAbility ? LongTendrilAbility->GetInvestedBiomassOnTarget() : 0.0f;
}

void ASpaceDrifterPawn::ResolveTendrilComponents()
{
	TArray<UFXSystemComponent*> FXComponents;
	GetComponents<UFXSystemComponent>(FXComponents);
	for (UFXSystemComponent* FX : FXComponents)
	{
		if (!FX)
		{
			continue;
		}
		if (!MyceliumWebFX && FX->GetName().Contains(TEXT("MyceliumWeb")))
		{
			MyceliumWebFX = FX;
		}
		else if (!GossamerSporesFX && FX->GetName().Contains(TEXT("GossamerSpores")))
		{
			GossamerSporesFX = FX;
		}
		else if (!SporeJetFX && FX->GetName().Contains(TEXT("SporeJet")))
		{
			SporeJetFX = FX;
		}
	}

	auto ConfigureTendrilFXComponent = [this](UFXSystemComponent* FX)
	{
		if (!FX)
		{
			return;
		}
		// 나이아가라 시뮬레이션이 반드시 Pawn의 TG_PostPhysics Tick(User.TendrilStart/End 갱신) 및 모든 물리/트랜스폼 갱신 이후
		// 동일 프레임 최종 단계(TG_PostUpdateWork)에서 수행되도록 틱 그룹과 선행 의존성을 등록합니다.
		FX->SetTickGroup(TG_PostUpdateWork);
		FX->AddTickPrerequisiteActor(this);
		FX->DeactivateImmediate();
		FX->SetVisibility(false);
	};

	ConfigureTendrilFXComponent(MyceliumWebFX);
	ConfigureTendrilFXComponent(GossamerSporesFX);
	ConfigureTendrilFXComponent(SporeJetFX);
}

int32 ASpaceDrifterPawn::ComputeLongTendrilBranchCount() const
{
	return LongTendrilAbility ? LongTendrilAbility->ComputeCurrentBranchCount() : 0;
}

bool ASpaceDrifterPawn::HasLatchedTarget() const
{
	return GetLatchedTargetActor() != nullptr;
}

AActor* ASpaceDrifterPawn::GetLatchedTargetActor() const
{
	return ActiveTendrilAbility ? ActiveTendrilAbility->GetLatchedTargetActor() : nullptr;
}

bool ASpaceDrifterPawn::EvaluateAimReticleTarget(bool& OutbTargetDetected, float& OutDistanceMeters, bool& OutbWithinLongTendrilReach) const
{
	OutbTargetDetected = false;
	OutDistanceMeters = 0.0f;
	OutbWithinLongTendrilReach = false;

	if (bIsGameOver)
	{
		return false;
	}

	const FVector RootOrigin = GetTendrilVisualOrigin();
	if (const AActor* LatchedActor = GetLatchedTargetActor())
	{
		OutbTargetDetected = true;
		OutbWithinLongTendrilReach = true;
		OutDistanceMeters = FVector::Distance(RootOrigin, LatchedActor->GetActorLocation()) * 0.01f;
		return true;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	FVector CamLoc = RootOrigin;
	FVector CamForward = GetActorForwardVector();
	FVector UnusedRight = FVector::RightVector;
	FVector UnusedUp = FVector::UpVector;
	GetTendrilCameraBasis(CamLoc, CamForward, UnusedRight, UnusedUp);

	const float V1ReachCm = LongTendrilAbility ? LongTendrilAbility->GetMaxEffectiveReachCm() : (LongTendrilMaxLength * GetTendrilBodyScale());
	const float RawDetectDistCm = V1ReachCm * FMath::Max(ReticleDetectRangeMultiplier, 0.1f);
	const float MaxMeasureDistCm = (ReticleMaxMeasureDistanceCm > KINDA_SMALL_NUMBER)
		? FMath::Min(RawDetectDistCm, ReticleMaxMeasureDistanceCm)
		: RawDetectDistCm;
	const float TraceDistCm = FMath::Max(MaxMeasureDistCm, V1ReachCm);

	const float PlayerProjDist = FMath::Max(FVector::DotProduct(RootOrigin - CamLoc, CamForward), 0.0f);
	const FVector TraceStart = CamLoc + CamForward * PlayerProjDist;
	const FVector TraceEnd = TraceStart + CamForward * TraceDistCm;

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SpaceDrifterReticle), false, this);
	bool bHit = false;
	const float EffectiveTraceRadius = (ReticleTraceRadiusOverrideCm > KINDA_SMALL_NUMBER) ? ReticleTraceRadiusOverrideCm : TendrilAimTraceRadius;
	if (EffectiveTraceRadius > KINDA_SMALL_NUMBER)
	{
		const FCollisionShape SphereShape = FCollisionShape::MakeSphere(EffectiveTraceRadius);
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

	if (bHit && !Hit.bStartPenetrating && Hit.GetActor())
	{
		const float HitDistFromStartCm = FVector::Distance(TraceStart, Hit.ImpactPoint);
		OutbTargetDetected = true;
		OutbWithinLongTendrilReach = (HitDistFromStartCm <= V1ReachCm);
		OutDistanceMeters = FVector::Distance(RootOrigin, Hit.ImpactPoint) * 0.01f;
		return true;
	}

	return false;
}

bool ASpaceDrifterPawn::CanFireTendril() const
{
	return ActiveTendrilAbility ? ActiveTendrilAbility->CanActivateAbility() : false;
}

float ASpaceDrifterPawn::GetTendrilExtendAlpha() const
{
	return ActiveTendrilAbility ? ActiveTendrilAbility->GetExtendAlpha() : 0.0f;
}

#if WITH_EDITOR
void ASpaceDrifterPawn::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	SyncAbilitiesConfig();

	if (GetWorld() && GetWorld()->IsGameWorld())
	{
		ApplyChaosPhysicsSettings();
	}

	if (MyceliumWebFX && LongTendrilAbility)
	{
		LongTendrilAbility->PushLongTendrilParametersToFX(MyceliumWebFX);
	}
}
#endif

void ASpaceDrifterPawn::SweepDirectContactMinerals()
{
	if (!bEnableDirectMineralContactAbsorb)
	{
		return;
	}

	AAsteroidFieldManager* FieldMgr = GetActiveAsteroidFieldManager();
	if (!FieldMgr)
	{
		return;
	}

	TArray<ASpaceMineral*> AvailableMinerals;
	FieldMgr->GatherAvailableMinerals(AvailableMinerals);
	if (AvailableMinerals.IsEmpty())
	{
		return;
	}

	const FVector BodyPos = GetActorLocation();
	const float EffectiveDirectRadius = GetTendrilBodyRadius() * FMath::Max(DirectContactAbsorbRadiusMultiplier, 1.0f) + FMath::Max(DirectContactAbsorbMarginCm, 0.0f);
	const float DirectContactRadiusSq = FMath::Square(EffectiveDirectRadius);

	for (ASpaceMineral* Mineral : AvailableMinerals)
	{
		if (Mineral && FVector::DistSquared(Mineral->GetActorLocation(), BodyPos) <= DirectContactRadiusSq)
		{
			Mineral->BeginAbsorption(this);
		}
	}
}

void ASpaceDrifterPawn::NotifyMineralAbsorbed(ASpaceMineral* AbsorbedMineral, const FVector& ImpactWorldLocation)
{
	if (bIsGameOver)
	{
		return;
	}

	++ConsumedMineralCount;
	const float OverrideGain = AbsorbedMineral ? AbsorbedMineral->GetBiomassRewardOverride() : -1.0f;
	const float Gain = (OverrideGain > 0.0f) ? OverrideGain : FMath::Max(MineralBiomassGain, 0.0f);
	CurrentBiomass += Gain;
	UpdateBodyScaleFromBiomass();

	const FVector MyLoc = GetActorLocation();
	FVector BounceDir = (ImpactWorldLocation - MyLoc).GetSafeNormal();
	if (BounceDir.IsNearlyZero())
	{
		BounceDir = ActiveTendrilAbility ? ActiveTendrilAbility->GetLastTendrilDir() : GetActorForwardVector();
	}

	if (MineralAbsorbSquishImpulse > KINDA_SMALL_NUMBER)
	{
		ApplyDeformImpulse(BounceDir, MineralAbsorbSquishImpulse);
	}

	OnMineralConsumed.Broadcast(ConsumedMineralCount);

	PostTendrilStatusBanner(
		FString::Printf(TEXT("미네랄 흡수! (+%.0f 생체량, 총 %d개)"), Gain, ConsumedMineralCount),
		FLinearColor(0.42f, 0.98f, 0.85f, 1.0f),
		1.5f
	);
}

void ASpaceDrifterPawn::ApplyChaosPhysicsSettings()
{
	if (!CollisionSphere)
	{
		return;
	}

	CollisionSphere->SetEnableGravity(false);
	CollisionSphere->SetNotifyRigidBodyCollision(true);
	CollisionSphere->SetLinearDamping(FMath::Max(PlayerLinearDamping, 0.0f));
	CollisionSphere->SetMassOverrideInKg(NAME_None, FMath::Max(PlayerMassKg, 1.0f), true);

	CollisionSphere->BodyInstance.bLockXRotation = true;
	CollisionSphere->BodyInstance.bLockYRotation = true;
	CollisionSphere->BodyInstance.bLockZRotation = true;
	CollisionSphere->BodyInstance.SetDOFLock(EDOFMode::SixDOF);

	if (!RuntimePhysicalMaterial)
	{
		RuntimePhysicalMaterial = NewObject<UPhysicalMaterial>(this);
	}
	if (RuntimePhysicalMaterial)
	{
		RuntimePhysicalMaterial->Restitution = FMath::Clamp(PlayerRestitution, 0.0f, 1.0f);
		RuntimePhysicalMaterial->Friction = FMath::Max(PlayerFriction, 0.0f);
		RuntimePhysicalMaterial->StaticFriction = RuntimePhysicalMaterial->Friction;
		RuntimePhysicalMaterial->bOverrideRestitutionCombineMode = true;
		RuntimePhysicalMaterial->RestitutionCombineMode = EFrictionCombineMode::Average;
		RuntimePhysicalMaterial->SleepLinearVelocityThreshold = 0.0f;
		RuntimePhysicalMaterial->SleepAngularVelocityThreshold = 0.0f;
		CollisionSphere->SetPhysMaterialOverride(RuntimePhysicalMaterial);
	}

	const bool bIsGameWorld = GetWorld() && GetWorld()->IsGameWorld();
	CollisionSphere->SetSimulatePhysics(bEnableChaosPhysics && bIsGameWorld);
}

void ASpaceDrifterPawn::OnCollisionSphereHit(
	UPrimitiveComponent* HitComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	FVector NormalImpulse,
	const FHitResult& Hit)
{
	const float EffectiveMass = FMath::Max(
		(CollisionSphere && CollisionSphere->IsSimulatingPhysics()) ? CollisionSphere->GetMass() : PlayerMassKg,
		1.0f
	);
	const float DeltaSpeed = NormalImpulse.Size() / EffectiveMass;

	if (DeltaSpeed >= MinCollisionSquishDeltaSpeed)
	{
		const float ImpulseStrength = FMath::Clamp(DeltaSpeed * CollisionSquishScale, 0.5f, FMath::Max(MaxCollisionSquishImpulse, 0.5f));
		const FVector ImpactDir = !Hit.ImpactNormal.IsNearlyZero() ? FVector(Hit.ImpactNormal) : NormalImpulse.GetSafeNormal();
		if (!ImpactDir.IsNearlyZero())
		{
			ApplyDeformImpulse(ImpactDir, ImpulseStrength);
			bSuppressAccelDeformThisTick = true;
		}
	}

	if (ActiveTendrilAbility)
	{
		ActiveTendrilAbility->OnHostCollisionHit(OtherActor, OtherComp, NormalImpulse, Hit);
	}
}

void ASpaceDrifterPawn::ApplyDeformImpulse(const FVector& WorldImpulse, float StrengthScale)
{
	const float Magnitude = WorldImpulse.Size() * StrengthScale;
	if (Magnitude <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const FVector Direction = WorldImpulse.GetSafeNormal();
	if (FMath::Abs(DeformValue) < 0.05f && FMath::Abs(DeformVelocity) < 0.5f)
	{
		DeformAxis = Direction;
	}
	else
	{
		const FVector AlignedDir = (FVector::DotProduct(DeformAxis, Direction) < 0.0f) ? -Direction : Direction;
		DeformAxis = FMath::Lerp(DeformAxis, AlignedDir, 0.65f).GetSafeNormal();
	}

	DeformVelocity -= Magnitude;
}

void ASpaceDrifterPawn::UpdateElasticity(float DeltaTime)
{
	const float SimDt = FMath::Min(DeltaTime, 0.05f);
	if (SimDt <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const FVector CurrentLocation = GetActorLocation();
	const bool bUsePhysicsVelocity = (bEnableChaosPhysics && CollisionSphere && CollisionSphere->IsSimulatingPhysics());
	const FVector CurrentVelocity = bUsePhysicsVelocity
		? CollisionSphere->GetPhysicsLinearVelocity()
		: (CurrentLocation - PreviousActorLocation) / SimDt;
	float TargetDeform = 0.0f;

	if (bSuppressAccelDeformThisTick)
	{
		bSuppressAccelDeformThisTick = false;
	}
	else if (bHasVelocityHistory)
	{
		const FVector Acceleration = (CurrentVelocity - PreviousWorldVelocity) / SimDt;
		const float AccelMag = Acceleration.Size();
		if (AccelMag > AccelerationDeadzone)
		{
			const FVector AccelDir = Acceleration / AccelMag;
			const FVector AlignedDir = (FVector::DotProduct(DeformAxis, AccelDir) < 0.0f) ? -AccelDir : AccelDir;
			DeformAxis = FMath::VInterpTo(DeformAxis, AlignedDir, SimDt, 12.0f).GetSafeNormal();
			TargetDeform = FMath::Clamp((AccelMag - AccelerationDeadzone) * AccelerationDeformScale, 0.0f, MaxDeformAmount);
		}
	}

	PreviousActorLocation = CurrentLocation;
	PreviousWorldVelocity = CurrentVelocity;
	bHasVelocityHistory = true;

	if (ActiveTendrilAbility)
	{
		FVector StretchDir = FVector::ForwardVector;
		const float TendrilDeform = ActiveTendrilAbility->EvaluateHostBodyStretch(ElapsedFloatTime, StretchDir);
		if (TendrilDeform > TargetDeform)
		{
			const FVector AlignedDir = (FVector::DotProduct(DeformAxis, StretchDir) < 0.0f) ? -StretchDir : StretchDir;
			DeformAxis = FMath::VInterpTo(DeformAxis, AlignedDir, SimDt, 14.0f).GetSafeNormal();
			TargetDeform = FMath::Clamp(TendrilDeform, 0.0f, MaxDeformAmount);
		}
	}

	const float SpringForce = SpringStiffness * (TargetDeform - DeformValue);
	const float DampingForce = -SpringDamping * DeformVelocity;
	DeformVelocity += (SpringForce + DampingForce) * SimDt;

	constexpr float MinDeform = -0.45f;
	const float RawNextDeform = DeformValue + DeformVelocity * SimDt;
	if (RawNextDeform <= MinDeform)
	{
		DeformValue = MinDeform;
		if (DeformVelocity < 0.0f)
		{
			DeformVelocity = -DeformVelocity * 0.65f;
		}
	}
	else if (RawNextDeform >= MaxDeformAmount)
	{
		DeformValue = MaxDeformAmount;
		if (DeformVelocity > 0.0f)
		{
			DeformVelocity = 0.0f;
		}
	}
	else
	{
		DeformValue = RawNextDeform;
	}
}

void ASpaceDrifterPawn::PushDeformToMaterial()
{
	if (!DrifterMID && DrifterMesh && DrifterMesh->GetMaterial(0))
	{
		DrifterMID = DrifterMesh->CreateAndSetMaterialInstanceDynamic(0);
	}
	if (!DrifterMID)
	{
		return;
	}

	float FinalDeform = DeformValue;
	FVector FinalAxis = DeformAxis;

	if (bEnableIdleBreathing)
	{
		const float IdleFade = 1.0f - FMath::Clamp(FMath::Abs(DeformValue) * 4.5f, 0.0f, 1.0f);
		const float BreathWave = FMath::Sin(ElapsedFloatTime * BreathFrequency * UE_TWO_PI) * BreathAmplitude * IdleFade;
		FinalDeform = FMath::Clamp(FinalDeform + BreathWave, -0.45f, MaxDeformAmount);
	}

	FinalAxis = FinalAxis.GetSafeNormal();
	if (FinalAxis.IsNearlyZero())
	{
		FinalAxis = FVector::UpVector;
	}

	DrifterMID->SetVectorParameterValue(DeformAxisParamName, FLinearColor(FinalAxis.X, FinalAxis.Y, FinalAxis.Z, 0.0f));
	DrifterMID->SetScalarParameterValue(DeformAmountParamName, FinalDeform);
}

void ASpaceDrifterPawn::UpdateBodyScaleFromBiomass()
{
	const float BaseBiomass = FMath::Max(InitialBiomass, 10.0f);
	const float BaseRadius = FMath::Max(InitialBodyRadiusCm, 10.0f);
	const float MinRadius = FMath::Clamp(MinGameOverBodyRadiusCm, 5.0f, BaseRadius - 1.0f);
	const float MaxRadius = FMath::Max(MaxBodyRadiusCm, BaseRadius);

	const float RadiusExp = FMath::Clamp(BiomassToRadiusExponent, 0.2f, 0.8f);
	const float InvRadiusExp = 1.0f / RadiusExp;
	const float MaxBiomass = BaseBiomass * FMath::Pow(MaxRadius / BaseRadius, InvRadiusExp);
	CurrentBiomass = FMath::Clamp(CurrentBiomass, GetTendrilMinGameOverBiomass(), MaxBiomass);

	const float BiomassRatio = FMath::Max(CurrentBiomass / BaseBiomass, 0.001f);
	CurrentBodyRadiusCm = FMath::Clamp(BaseRadius * FMath::Pow(BiomassRatio, RadiusExp), MinRadius, MaxRadius);
	MaxReachedBodyRadiusCm = FMath::Max(MaxReachedBodyRadiusCm, CurrentBodyRadiusCm);

	const float ScaleFactor = CurrentBodyRadiusCm / BaseRadius;
	const float PhysicalVolumeRatio = FMath::Pow(ScaleFactor, 3.0f);

	if (CollisionSphere)
	{
		CollisionSphere->SetSphereRadius(CurrentBodyRadiusCm, true);
		CollisionSphere->SetMassOverrideInKg(NAME_None, FMath::Max(PlayerMassKg * PhysicalVolumeRatio, 30.0f), true);
	}
	if (DrifterMesh)
	{
		DrifterMesh->SetRelativeScale3D(InitialMeshRelativeScale * ScaleFactor);
	}
}

void ASpaceDrifterPawn::NotifyEnemyDefeatedByPlayer()
{
	if (IsRunEnded())
	{
		return;
	}

	const bool bWasV2Unlocked = IsTendrilStyleUnlocked(ETendrilVisualStyle::GossamerSpores);
	const bool bWasV3Unlocked = IsTendrilStyleUnlocked(ETendrilVisualStyle::SporeJet);

	++DefeatedEnemyCount;

	const bool bUnlockedV2Now = !bWasV2Unlocked && IsTendrilStyleUnlocked(ETendrilVisualStyle::GossamerSpores);
	const bool bUnlockedV3Now = !bWasV3Unlocked && IsTendrilStyleUnlocked(ETendrilVisualStyle::SporeJet);

	if (DefeatedEnemyCount >= FMath::Max(TargetVictoryEnemyCount, 1))
	{
		EndRun(ESpaceRunState::Victory);
		return;
	}

	if (bUnlockedV2Now || bUnlockedV3Now)
	{
		if (const ASpaceDrifterPlayerController* PC = Cast<ASpaceDrifterPlayerController>(GetController()))
		{
			if (USpaceDrifterHUDWidget* HUD = PC->GetHUDWidget())
			{
				if (bUnlockedV3Now)
				{
					HUD->TriggerAbilityUnlockEffect(ETendrilVisualStyle::SporeJet);
				}
				else if (bUnlockedV2Now)
				{
					HUD->TriggerAbilityUnlockEffect(ETendrilVisualStyle::GossamerSpores);
				}
			}
		}
	}
}

void ASpaceDrifterPawn::EndRun(ESpaceRunState NewState)
{
	if (IsRunEnded() || NewState == ESpaceRunState::Playing)
	{
		return;
	}

	RunState = NewState;
	bIsGameOver = (NewState == ESpaceRunState::GameOver);
	bIsExtendInputHeld = false;

	if (ActiveTendrilAbility)
	{
		ActiveTendrilAbility->DeactivateAbilityImmediate();
	}

	if (NewState == ESpaceRunState::GameOver)
	{
		if (CollisionSphere)
		{
			CollisionSphere->SetPhysicsLinearVelocity(FVector::ZeroVector);
			CollisionSphere->SetSimulatePhysics(false);
			CollisionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		if (DrifterMesh)
		{
			DrifterMesh->SetVisibility(false, true);
		}
	}

	// 리더보드에 이번 판 점수(적 처치 횟수 + 최대 반지름) 영구 등록
	FString PlayerName = TEXT("익명 곰팡이");
	if (ASpaceDrifterPlayerController* PC = Cast<ASpaceDrifterPlayerController>(GetController()))
	{
		PlayerName = PC->GetCurrentPlayerName();
	}

	if (USpaceDrifterLeaderboardSave* SaveObj = USpaceDrifterLeaderboardSave::LoadOrCreate())
	{
		FSpaceLeaderboardEntry Entry;
		Entry.PlayerName = PlayerName;
		Entry.DefeatedEnemies = DefeatedEnemyCount;
		Entry.MaxRadiusMeters = MaxReachedBodyRadiusCm * 0.01f;
		Entry.AbsorbedMinerals = ConsumedMineralCount;
		Entry.bClearedGoal = (NewState == ESpaceRunState::Victory);
		Entry.ClearTimeSeconds = ElapsedSurvivalSeconds;
		Entry.bSurvivedFullTime = Entry.bClearedGoal;
		SaveObj->AddScoreAndSave(Entry);
	}

	if (ASpaceDrifterPlayerController* PC = Cast<ASpaceDrifterPlayerController>(GetController()))
	{
		PC->NotifyRunEnded();
	}

	if (NewState == ESpaceRunState::GameOver)
	{
		OnPlayerGameOver.Broadcast();

		if (bAutoRestartOnGameOver)
		{
			if (UWorld* World = GetWorld())
			{
				World->GetTimerManager().ClearTimer(GameOverRestartTimerHandle);
				World->GetTimerManager().SetTimer(
					GameOverRestartTimerHandle,
					this,
					&ASpaceDrifterPawn::OnGameOverRestartTimerExpired,
					FMath::Max(GameOverRestartDelaySeconds, 0.2f),
					false
				);
			}
		}
	}
}

void ASpaceDrifterPawn::RestartCurrentLevel()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GameOverRestartTimerHandle);
		UGameplayStatics::OpenLevel(this, FName(*World->GetName()), false);
	}
}

void ASpaceDrifterPawn::TriggerGameOver()
{
	EndRun(ESpaceRunState::GameOver);
}

void ASpaceDrifterPawn::OnGameOverRestartTimerExpired()
{
	RestartCurrentLevel();
}
