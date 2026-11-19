#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SpaceColonizableTarget.generated.h"

class UMaterialInterface;
class AAsteroidFieldManager;

UINTERFACE(MinimalAPI, BlueprintType)
class USpaceColonizableTarget : public UInterface
{
	GENERATED_BODY()
};

/**
 * V1 긴 촉수로 전염(식민지화) 및 배양 폭발이 가능한 대상(소행성, 적군 곰팡이 등)의 공통 계약입니다.
 */
class UESTUDY_API ISpaceColonizableTarget
{
	GENERATED_BODY()

public:
	/** 현재 전염(식민지화)을 시작하거나 진행할 수 있는 상태인지 반환합니다. */
	virtual bool CanBeColonized() const = 0;

	/** 대상을 완전히 뒤덮는 데 필요한 총 생체량을 반환합니다. */
	virtual float GetRequiredColonizeBiomass() const = 0;

	/** 표면 거리 판정에 사용할 대상의 반지름(cm)을 반환합니다. */
	virtual float GetColonizeRadiusCm() const = 0;

	/** 현재 전염 진행 중(Colonizing) 상태인지 반환합니다(중단 시 리셋 판별용). */
	virtual bool IsColonizingInProgress() const = 0;

	/** 전염 진행도(0~1)에 따라 표면 재질 연출을 갱신합니다. */
	virtual void SetColonizationProgressVisual(float Progress) = 0;

	/** 전염 완료 시 배양 상태에 진입하고 폭발 타이머를 무장합니다. */
	virtual void CompleteColonization(UMaterialInterface* HostSkinMaterial, AAsteroidFieldManager* FieldMgr) = 0;

	/** 전염 실패/중단 시 전염 상태와 외형을 초기화합니다. */
	virtual void ResetColonizationState() = 0;
};
