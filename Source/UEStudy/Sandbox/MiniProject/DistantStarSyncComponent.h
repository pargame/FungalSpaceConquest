// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DistantStarSyncComponent.generated.h"

class AActor;
class UDirectionalLightComponent;
class UMaterialParameterCollection;
class USceneComponent;

/**
 * DirectionalLight_Starlight의 방향·색상·빛줄기(Glare) 외형 파라미터를
 * 우주 배경 머티리얼 파라미터 컬렉션(MPC_SpaceEnvironment)과 동기화하고,
 * 런타임에는 스카이 돔(SM_SpaceSky)을 카메라 위치 중심으로 유지해 항성을 물리적으로 닿을 수 없는 무한 거리에 고정합니다.
 */
UCLASS(ClassGroup = (SpaceEnvironment), meta = (BlueprintSpawnableComponent))
class UESTUDY_API UDistantStarSyncComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDistantStarSyncComponent();

	/** 머티리얼 파라미터 컬렉션과 스카이 돔 위치를 즉시 수동 동기화합니다. */
	UFUNCTION(BlueprintCallable, Category = "Space Star")
	void SyncStarParameters();

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	/** 에디터/런타임에서 소유 액터(DirectionalLight) 회전·이동 시 즉시 반응하는 델리게이트 핸들러입니다. */
	void HandleOwnerTransformUpdated(USceneComponent* UpdatedComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport);

#if WITH_EDITOR
	/** 에디터에서 DirectionalLight 색상·온도나 컴포넌트 수치 변경 시 즉시 항성 셰이더를 갱신합니다. */
	void HandleObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent);
#endif

	/** 스카이 돔 액터(SM_SpaceSky)를 찾거나 캐시에서 반환합니다. */
	AActor* ResolveSkyDomeActor();

	/** MPC_SpaceEnvironment 에셋을 로드해 반환합니다. */
	UMaterialParameterCollection* ResolveSpaceMPC();

	/** 런타임 카메라 기준 스카이 돔 위치를 갱신합니다. */
	void UpdateSkyDomePosition();

	/** 소유 액터(DirectionalLight)의 회전을 기준으로 MPC의 StarDirection을 동기화합니다. */
	UPROPERTY(EditAnywhere, Category = "Space Star|Sync")
	bool bSyncDirectionFromLight = true;

	/** 소유 액터(DirectionalLight)의 색상을 기준으로 항성 코어·코로나 색상을 자동 조화시킵니다. */
	UPROPERTY(EditAnywhere, Category = "Space Star|Sync")
	bool bSyncColorFromLight = true;

	/** 아래 외형·빛줄기 파라미터들을 MPC_SpaceEnvironment에 실시간 반영합니다. */
	UPROPERTY(EditAnywhere, Category = "Space Star|Sync")
	bool bSyncVisualParamsToMPC = true;

	/** 항성 중심 광구(원반)의 겉보기 각반지름(라디안)입니다. 클수록 하늘에서 태양이 크게 보입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Space Star|Visual", meta = (AllowPrivateAccess = "true", ClampMin = "0.002", ClampMax = "0.15", UIMin = "0.004", UIMax = "0.05"))
	float StarAngularRadius = 0.012f;

	/** 항성 중심 광구의 HDR 발광 세기입니다. 블룸 강도와 중심 백열감에 영향을 줍니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Space Star|Visual", meta = (AllowPrivateAccess = "true", ClampMin = "1.0", ClampMax = "300.0", UIMin = "5.0", UIMax = "120.0"))
	float StarCoreIntensity = 65.0f;

	/** 항성 주변을 감싸는 채층 및 코로나 헤일로의 발광 세기입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Space Star|Visual", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "30.0", UIMin = "0.5", UIMax = "12.0"))
	float StarCoronaIntensity = 3.6f;

	/** 코로나 헤일로가 퍼져나가는 거리 배율(항성 반지름 배수)입니다. 클수록 주변 광륜이 넓어집니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Space Star|Visual", meta = (AllowPrivateAccess = "true", ClampMin = "0.5", ClampMax = "12.0", UIMin = "1.0", UIMax = "6.0"))
	float StarCoronaScale = 2.4f;

	/** 항성에서 뻗어나가는 광학 회절 빛줄기(글레어)의 전체 밝기 강도입니다. 0으로 두면 빛줄기를 끕니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Space Star|Glare Rays", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "15.0", UIMin = "0.0", UIMax = "5.0"))
	float StarGlareIntensity = 1.65f;

	/** 항성에서 뻗어나가는 빛줄기의 길이(항성 반지름 배수)입니다. 클수록 빛줄기가 화면 멀리까지 길게 뻗습니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Space Star|Glare Rays", meta = (AllowPrivateAccess = "true", ClampMin = "0.5", ClampMax = "40.0", UIMin = "1.5", UIMax = "20.0"))
	float StarGlareRayLength = 6.5f;

	/** 빛줄기의 날카로움(굵기의 역수 지수)입니다. 값이 작을수록 빛줄기가 굵고 부드러워지며, 클수록 바늘처럼 가늘어집니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Space Star|Glare Rays", meta = (AllowPrivateAccess = "true", ClampMin = "4.0", ClampMax = "300.0", UIMin = "12.0", UIMax = "160.0"))
	float StarGlareRaySharpness = 56.0f;

	/** 45도 대각선 보조 빛줄기의 상대 세기입니다. 0이면 수직·수평 4갈래 십자 빛줄기만 출력됩니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Space Star|Glare Rays", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "1.0"))
	float StarDiagonalGlareWeight = 0.25f;

	/** 항성 쪽 반구 전체에 은은하게 퍼지는 심우주 산란광 세기입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Space Star|Visual", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "0.5", UIMin = "0.0", UIMax = "0.1"))
	float StarAmbientGlowIntensity = 0.016f;

	/** 런타임에서 스카이 돔을 항상 활성 카메라 위치로 이동시켜 경계 이탈을 막습니다. */
	UPROPERTY(EditAnywhere, Category = "Space Star|Sky Dome")
	bool bKeepSkyDomeCenteredOnCamera = true;

	/** 항성 방향/색상/빛줄기 파라미터를 공유할 MaterialParameterCollection 에셋입니다. */
	UPROPERTY(EditAnywhere, Category = "Space Star|Assets")
	TSoftObjectPtr<UMaterialParameterCollection> SpaceEnvironmentMPC;

	/** 카메라를 따라다닐 우주 배경 스카이 돔 액터(미지정 시 SM_SpaceSky 자동 탐색)입니다. */
	UPROPERTY(EditAnywhere, Category = "Space Star|Assets")
	TSoftObjectPtr<AActor> SkyDomeActorOverride;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> CachedMPC = nullptr;

	TWeakObjectPtr<AActor> CachedSkyDomeActor;
	FDelegateHandle TransformUpdatedHandle;
#if WITH_EDITOR
	FDelegateHandle ObjectPropertyChangedHandle;
#endif

	FVector LastSyncedStarDir = FVector::ZeroVector;
	FLinearColor LastSyncedLightColor = FLinearColor(-1.0f, -1.0f, -1.0f, 1.0f);
	bool bVisualParamsDirty = true;
};
