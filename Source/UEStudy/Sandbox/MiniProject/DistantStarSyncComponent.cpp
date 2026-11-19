// Copyright Epic Games, Inc. All Rights Reserved.

#include "DistantStarSyncComponent.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#if WITH_EDITOR
#include "UObject/UObjectGlobals.h"
#endif

namespace
{
	const TCHAR* DefaultSpaceMPCPath = TEXT("/Game/Sandbox/MiniProject/Materials/MPC_SpaceEnvironment.MPC_SpaceEnvironment");
	const FName ParamStarDirection(TEXT("StarDirection"));
	const FName ParamStarCoreColor(TEXT("StarCoreColor"));
	const FName ParamStarCoronaColor(TEXT("StarCoronaColor"));
	const FName ParamStarAngularRadius(TEXT("StarAngularRadius"));
	const FName ParamStarCoreIntensity(TEXT("StarCoreIntensity"));
	const FName ParamStarCoronaIntensity(TEXT("StarCoronaIntensity"));
	const FName ParamStarCoronaScale(TEXT("StarCoronaScale"));
	const FName ParamStarGlareIntensity(TEXT("StarGlareIntensity"));
	const FName ParamStarGlareRayLength(TEXT("StarGlareRayLength"));
	const FName ParamStarGlareRaySharpness(TEXT("StarGlareRaySharpness"));
	const FName ParamStarDiagonalGlareWeight(TEXT("StarDiagonalGlareWeight"));
	const FName ParamStarAmbientGlowIntensity(TEXT("StarAmbientGlowIntensity"));
}

UDistantStarSyncComponent::UDistantStarSyncComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;

	SpaceEnvironmentMPC = TSoftObjectPtr<UMaterialParameterCollection>(FSoftObjectPath(DefaultSpaceMPCPath));
}

void UDistantStarSyncComponent::OnRegister()
{
	Super::OnRegister();

	if (AActor* Owner = GetOwner())
	{
		if (USceneComponent* RootComp = Owner->GetRootComponent())
		{
			if (TransformUpdatedHandle.IsValid())
			{
				RootComp->TransformUpdated.Remove(TransformUpdatedHandle);
				TransformUpdatedHandle.Reset();
			}
			TransformUpdatedHandle = RootComp->TransformUpdated.AddUObject(
				this,
				&UDistantStarSyncComponent::HandleOwnerTransformUpdated
			);
		}
	}

#if WITH_EDITOR
	if (ObjectPropertyChangedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ObjectPropertyChangedHandle);
		ObjectPropertyChangedHandle.Reset();
	}
	ObjectPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject(
		this,
		&UDistantStarSyncComponent::HandleObjectPropertyChanged
	);
#endif

	bVisualParamsDirty = true;
	SyncStarParameters();
}

void UDistantStarSyncComponent::OnUnregister()
{
#if WITH_EDITOR
	if (ObjectPropertyChangedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ObjectPropertyChangedHandle);
		ObjectPropertyChangedHandle.Reset();
	}
#endif

	if (AActor* Owner = GetOwner())
	{
		if (USceneComponent* RootComp = Owner->GetRootComponent())
		{
			if (TransformUpdatedHandle.IsValid())
			{
				RootComp->TransformUpdated.Remove(TransformUpdatedHandle);
				TransformUpdatedHandle.Reset();
			}
		}
	}

	Super::OnUnregister();
}

void UDistantStarSyncComponent::BeginPlay()
{
	Super::BeginPlay();

	if (AActor* SkyDome = ResolveSkyDomeActor())
	{
		if (USceneComponent* SkyRoot = SkyDome->GetRootComponent())
		{
			if (SkyRoot->Mobility != EComponentMobility::Movable)
			{
				SkyRoot->SetMobility(EComponentMobility::Movable);
			}
		}
	}

	bVisualParamsDirty = true;
	SyncStarParameters();
	UpdateSkyDomePosition();
}

void UDistantStarSyncComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	SyncStarParameters();
	UpdateSkyDomePosition();
}

#if WITH_EDITOR
void UDistantStarSyncComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	LastSyncedStarDir = FVector::ZeroVector;
	LastSyncedLightColor = FLinearColor(-1.0f, -1.0f, -1.0f, 1.0f);
	bVisualParamsDirty = true;
	SyncStarParameters();
}
#endif

void UDistantStarSyncComponent::HandleOwnerTransformUpdated(
	USceneComponent* UpdatedComponent,
	EUpdateTransformFlags UpdateTransformFlags,
	ETeleportType Teleport)
{
	SyncStarParameters();
}

#if WITH_EDITOR
void UDistantStarSyncComponent::HandleObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Object)
	{
		return;
	}

	if (Object == this || Object == Owner || Object == Owner->FindComponentByClass<UDirectionalLightComponent>())
	{
		LastSyncedLightColor = FLinearColor(-1.0f, -1.0f, -1.0f, 1.0f);
		bVisualParamsDirty = true;
		SyncStarParameters();
	}
}
#endif

UMaterialParameterCollection* UDistantStarSyncComponent::ResolveSpaceMPC()
{
	if (IsValid(CachedMPC))
	{
		return CachedMPC;
	}

	if (!SpaceEnvironmentMPC.IsNull())
	{
		CachedMPC = SpaceEnvironmentMPC.LoadSynchronous();
	}
	return CachedMPC;
}

AActor* UDistantStarSyncComponent::ResolveSkyDomeActor()
{
	if (AActor* OverrideActor = SkyDomeActorOverride.Get())
	{
		CachedSkyDomeActor = OverrideActor;
		return OverrideActor;
	}

	if (AActor* Existing = CachedSkyDomeActor.Get())
	{
		return Existing;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Candidate = *It;
		if (!IsValid(Candidate))
		{
			continue;
		}

		// 라벨/머티리얼 이름으로 기존 우주 스카이 돔 탐색
#if WITH_EDITOR
		const bool bMatchesLabel = Candidate->GetActorLabel().Contains(TEXT("SpaceSky"), ESearchCase::IgnoreCase);
#else
		const bool bMatchesLabel = Candidate->GetName().Contains(TEXT("SpaceSky"), ESearchCase::IgnoreCase);
#endif
		if (bMatchesLabel)
		{
			CachedSkyDomeActor = Candidate;
			return Candidate;
		}

		if (const UStaticMeshComponent* MeshComp = Candidate->FindComponentByClass<UStaticMeshComponent>())
		{
			if (const UMaterialInterface* Mat = MeshComp->GetMaterial(0))
			{
				if (Mat->GetName().Contains(TEXT("M_SpaceSky"), ESearchCase::IgnoreCase))
				{
					CachedSkyDomeActor = Candidate;
					return Candidate;
				}
			}
		}
	}

	return nullptr;
}

void UDistantStarSyncComponent::SyncStarParameters()
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	UMaterialParameterCollection* MPC = ResolveSpaceMPC();
	if (!World || !Owner || !MPC)
	{
		return;
	}

	if (bSyncDirectionFromLight)
	{
		// DirectionalLight의 빛 진행 방향(+Forward)의 반대(-Forward)가 항성이 떠 있는 방향
		const FVector TowardStarDir = (-Owner->GetActorForwardVector()).GetSafeNormal();
		if (!TowardStarDir.IsNearlyZero() && !TowardStarDir.Equals(LastSyncedStarDir, 1e-4f))
		{
			UKismetMaterialLibrary::SetVectorParameterValue(
				World,
				MPC,
				ParamStarDirection,
				FLinearColor(TowardStarDir.X, TowardStarDir.Y, TowardStarDir.Z, 0.0f)
			);
			LastSyncedStarDir = TowardStarDir;
		}
	}

	if (bSyncColorFromLight)
	{
		if (const UDirectionalLightComponent* LightComp = Owner->FindComponentByClass<UDirectionalLightComponent>())
		{
			FLinearColor LightLinear = LightComp->GetLightColor();
			if (LightComp->bUseTemperature)
			{
				LightLinear *= LightComp->GetColorTemperature();
			}
			if (!LightLinear.Equals(LastSyncedLightColor, 1e-4f))
			{
				const float MaxChan = FMath::Max3(LightLinear.R, LightLinear.G, LightLinear.B);
				const FLinearColor NormLight = (MaxChan > KINDA_SMALL_NUMBER) ? (LightLinear / MaxChan) : FLinearColor::White;

				// 중심 광구는 백열감(화이트-코어), 코로나는 항성 고유 색감을 더 진하게 유지
				const FLinearColor CoreColor(
					FMath::Lerp(1.0f, NormLight.R, 0.35f),
					FMath::Lerp(1.0f, NormLight.G, 0.35f),
					FMath::Lerp(1.0f, NormLight.B, 0.35f),
					1.0f
				);
				const FLinearColor CoronaColor(
					NormLight.R * 1.0f,
					NormLight.G * 0.76f,
					NormLight.B * 0.48f,
					1.0f
				);

				UKismetMaterialLibrary::SetVectorParameterValue(World, MPC, ParamStarCoreColor, CoreColor);
				UKismetMaterialLibrary::SetVectorParameterValue(World, MPC, ParamStarCoronaColor, CoronaColor);
				LastSyncedLightColor = LightLinear;
			}
		}
	}

	if (bSyncVisualParamsToMPC && bVisualParamsDirty)
	{
		UKismetMaterialLibrary::SetScalarParameterValue(World, MPC, ParamStarAngularRadius, StarAngularRadius);
		UKismetMaterialLibrary::SetScalarParameterValue(World, MPC, ParamStarCoreIntensity, StarCoreIntensity);
		UKismetMaterialLibrary::SetScalarParameterValue(World, MPC, ParamStarCoronaIntensity, StarCoronaIntensity);
		UKismetMaterialLibrary::SetScalarParameterValue(World, MPC, ParamStarCoronaScale, StarCoronaScale);
		UKismetMaterialLibrary::SetScalarParameterValue(World, MPC, ParamStarGlareIntensity, StarGlareIntensity);
		UKismetMaterialLibrary::SetScalarParameterValue(World, MPC, ParamStarGlareRayLength, StarGlareRayLength);
		UKismetMaterialLibrary::SetScalarParameterValue(World, MPC, ParamStarGlareRaySharpness, StarGlareRaySharpness);
		UKismetMaterialLibrary::SetScalarParameterValue(World, MPC, ParamStarDiagonalGlareWeight, StarDiagonalGlareWeight);
		UKismetMaterialLibrary::SetScalarParameterValue(World, MPC, ParamStarAmbientGlowIntensity, StarAmbientGlowIntensity);
		bVisualParamsDirty = false;
	}
}

void UDistantStarSyncComponent::UpdateSkyDomePosition()
{
	if (!bKeepSkyDomeCenteredOnCamera)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	AActor* SkyDome = ResolveSkyDomeActor();
	if (!SkyDome)
	{
		return;
	}

	FVector ViewLocation = FVector::ZeroVector;
	bool bHasViewLocation = false;

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
	{
		FRotator UnusedRot;
		PC->GetPlayerViewPoint(ViewLocation, UnusedRot);
		bHasViewLocation = true;
	}
	else if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0))
	{
		ViewLocation = PlayerPawn->GetActorLocation();
		bHasViewLocation = true;
	}
	else if (World->ViewLocationsRenderedLastFrame.Num() > 0)
	{
		ViewLocation = World->ViewLocationsRenderedLastFrame[0];
		bHasViewLocation = true;
	}

	if (bHasViewLocation && !SkyDome->GetActorLocation().Equals(ViewLocation, 1.0f))
	{
		SkyDome->SetActorLocation(ViewLocation, false, nullptr, ETeleportType::TeleportPhysics);
	}
}
