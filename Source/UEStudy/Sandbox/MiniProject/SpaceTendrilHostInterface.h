// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SpaceTendrilHostInterface.generated.h"

class UWorld;
class UPrimitiveComponent;
class UStaticMeshComponent;
class UMaterialInterface;
class UFXSystemComponent;
class AAsteroidFieldManager;
class ASpaceMineral;

/** 카메라 조준 방향으로 발동하는 촉수 능력(Ability)의 종류입니다. */
UENUM(BlueprintType)
enum class ETendrilVisualStyle : uint8
{
	MyceliumWeb UMETA(DisplayName = "긴 촉수 (Long Tendril)"),
	GossamerSpores UMETA(DisplayName = "그물 촉수 (Net Tendril)"),
	SporeJet UMETA(DisplayName = "이동용 촉수 (Jet Tendril)")
};

UINTERFACE(MinimalAPI, BlueprintType)
class USpaceTendrilHostInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 촉수 능력(USpaceTendrilAbility 서브클래스)이 소유 액터(플레이어 폰 또는 외계 생명체)와
 * 강결합 없이 상호작용하기 위한 언리얼 표준 호스트 인터페이스입니다.
 */
class UESTUDY_API ISpaceTendrilHostInterface
{
	GENERATED_BODY()

public:
	/** 호스트 액터 포인터를 반환합니다. */
	virtual AActor* GetTendrilHostActor() const = 0;

	/** 호스트가 속한 월드를 반환합니다. */
	virtual UWorld* GetTendrilWorld() const = 0;

	/** 촉수가 뻗어 나오는 시각 메시(DrifterMesh)의 월드 중심 좌표를 반환합니다. */
	virtual FVector GetTendrilVisualOrigin() const = 0;

	/** 호스트 본체의 물리 중심 좌표를 반환합니다. */
	virtual FVector GetTendrilBodyLocation() const = 0;

	/** 호스트 본체 구체 콜리전의 반지름(cm)을 반환합니다. */
	virtual float GetTendrilBodyRadius() const = 0;

	/** 초기 기준 반지름 대비 현재 본체 크기 배율(1.0 = 초기 크기)을 반환합니다. */
	virtual float GetTendrilBodyScale() const = 0;

	/** 호스트의 현재 생체량(Biomass)을 반환합니다. */
	virtual float GetTendrilBiomass() const = 0;

	/** 게임오버가 발동되는 최소 생체량 임계치를 반환합니다. */
	virtual float GetTendrilMinGameOverBiomass() const = 0;

	/** 지정한 양의 생체량을(게임오버 임계치 위에서) 소모할 수 있는지 검사합니다. */
	virtual bool CanSpendTendrilBiomass(float Amount, bool bAllowGameOver = false) const = 0;

	/**
	 * 생체량을 차감합니다.
	 * @param bTriggerGameOverOnDeplete true이면 최소 임계치 도달 시 게임오버를 발동합니다.
	 * @return 차감 후에도 생존 상태이면 true, 게임오버에 도달했으면 false.
	 */
	virtual bool SpendTendrilBiomass(float Amount, bool bTriggerGameOverOnDeplete = false) = 0;

	/** 전염 실패 또는 도중 취소 시 투자했던 생체량을 호스트에게 되돌립니다. */
	virtual void RefundTendrilBiomass(float Amount) = 0;

	/** 호스트의 게임 HUD 배너(있을 경우)에 상태 알림 메시지를 전달합니다. */
	virtual void PostTendrilStatusBanner(const FString& Message, const FLinearColor& Color, float DurationSeconds = 1.8f) {}

	/** 호스트 본체의 현재 월드 선속도(cm/s)를 반환합니다. */
	virtual FVector GetTendrilBodyVelocity() const = 0;

	/** 호스트 카메라의 현재 조준 레이(위치 및 정규 직교 기저 벡터)를 반환합니다. */
	virtual void GetTendrilCameraBasis(
		FVector& OutCamLocation,
		FVector& OutCamForward,
		FVector& OutCamRight,
		FVector& OutCamUp
	) const = 0;

	/** 호스트의 Chaos 강체 물리 루트 컴포넌트를 반환합니다. */
	virtual UPrimitiveComponent* GetTendrilPhysicsComponent() const = 0;

	/** 호스트의 시각 본체 StaticMeshComponent(발사체 메시/머티리얼 복제용)를 반환합니다. */
	virtual UStaticMeshComponent* GetTendrilBodyMeshComponent() const = 0;

	/** 호스트 본체에 적용된 동적 머티리얼(DrifterMID)을 반환합니다. */
	virtual UMaterialInterface* GetTendrilBodyMaterial() const = 0;

	/** 지정된 촉수 스타일에 매핑된 나이아가라 컴포넌트를 반환합니다. */
	virtual UFXSystemComponent* GetTendrilFXComponent(ETendrilVisualStyle Style) const = 0;

	/** 레벨 내 활성 소행성·미네랄 풀 매니저를 반환합니다(TActorIterator 전수 순회 방지용). */
	virtual AAsteroidFieldManager* GetActiveAsteroidFieldManager() const = 0;

	/** 호스트 본체에 탄성 스퀴시/스트레치 변형 충격을 가합니다. */
	virtual void ApplyHostDeformImpulse(const FVector& WorldImpulse, float StrengthScale = 1.0f) = 0;
};
