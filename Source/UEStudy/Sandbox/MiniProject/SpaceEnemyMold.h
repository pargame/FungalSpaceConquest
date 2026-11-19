#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpaceColonizableTarget.h"
#include "SpaceEnemyMold.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPhysicalMaterial;
class AAsteroidFieldManager;
class AEnemyMoldManager;

UENUM(BlueprintType)
enum class EEnemyMoldColonizationState : uint8
{
	Uncolonized,
	Colonizing,
	ColonizedIncubating
};

/**
 * 플레이어를 추적하는 적군 곰팡이 액터입니다(0-Tick).
 * 조향/접촉 흡수/재배치는 AEnemyMoldManager가 일괄 처리하며,
 * 플레이어가 더 커졌을 때 V1 긴 촉수로 역전염·폭발시킬 수 있도록 ISpaceColonizableTarget을 구현합니다.
 */
UCLASS(Blueprintable)
class UESTUDY_API ASpaceEnemyMold : public AActor, public ISpaceColonizableTarget
{
	GENERATED_BODY()

public:
	ASpaceEnemyMold();

	/** 적 곰팡이의 기본 외형 메쉬입니다(비워 두면 SM_DustPuffSphere 자동 로드). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|Visual")
	TObjectPtr<UStaticMesh> MoldStaticMesh;

	/** 적 곰팡이 기본 머티리얼입니다(비워 두면 M_SpaceMoteOrb 또는 MI_DustPuff 자동 로드). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|Visual")
	TObjectPtr<UMaterialInterface> MoldBaseMaterial;

	/** 적 곰팡이 고유 진홍/보라 포자 색조입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|Visual")
	FLinearColor EnemyTintColor = FLinearColor(0.95f, 0.14f, 0.28f, 1.0f);

	/** 전염 완료 후 폭발까지 배양 시간(초)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|Colonization", meta = (ClampMin = "0.2", ClampMax = "10.0"))
	float ColonizeIncubationSeconds = 1.0f;

	/** 적 곰팡이 전염에 필요한 생체량 배율(적 현재 생체량 대비)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|Colonization", meta = (ClampMin = "0.2", ClampMax = "2.0"))
	float ColonizeCostMultiplier = 0.85f;

	/** 적 폭발 시 드롭할 생체량 보상 배율(적 현재 생체량 대비)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|Colonization", meta = (ClampMin = "1.0", ClampMax = "5.0"))
	float DefeatRewardBiomassMultiplier = 1.55f;

	/** 적 곰팡이 물리 질량 기준값(반지름 75cm 기준 kg)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|Physics", meta = (ClampMin = "10.0"))
	float BaseMassKg = 360.0f;

	/** 무중력 선형 감쇠입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyMold|Physics", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float LinearDamping = 0.35f;

	/** 풀에서 꺼내 지정 위치·크기·생체량으로 활성화합니다. */
	void ActivateAt(AEnemyMoldManager* InManager, const FVector& WorldLocation, float InBiomass, float InRadiusCm);

	/** 충돌·물리·렌더링을 끄고 풀 휴면 상태로 전환합니다. */
	void EnterDormant();

	/** V2 그물 촉수에 적중했을 때 지정 시간 동안 이동속도를 감속합니다. */
	void ApplyWebSlow(float DurationSeconds, float SpeedMultiplier);

	/** V2 광역 그물 적중 시 적의 반지름과 생체량을 영구 축소합니다(초기 반지름 대비 MinRadiusRatio 하한). */
	bool ApplyWebShrink(float ShrinkPercent, float MinRadiusRatio);

	/** 플레이어가 사냥(V1 전염) 가능한 상태인지 머티리얼 시각 표식을 갱신합니다. */
	void SetHuntableVisual(bool bInHuntable);

	/** 카메라가 포식자 메시(WPO 가시 포함) 내부로 침투했을 때 화면 가림/어지럼을 막기 위해 시각 메시만 임시 숨김 처리합니다. */
	void SetCameraProximityHidden(bool bShouldHide);

	FORCEINLINE bool IsWebSlowed() const { return bIsWebSlowed; }
	FORCEINLINE float GetMoveSpeedMultiplier() const { return bIsWebSlowed ? WebSlowSpeedMultiplier : 1.0f; }

	FORCEINLINE bool IsDormant() const { return bIsDormant; }
	FORCEINLINE float GetCurrentBiomass() const { return CurrentBiomass; }
	FORCEINLINE float GetCurrentRadiusCm() const { return CurrentRadiusCm; }
	FORCEINLINE USphereComponent* GetCollisionSphere() const { return CollisionSphere; }
	FORCEINLINE EEnemyMoldColonizationState GetColonizationState() const { return ColonizationState; }

	// ISpaceColonizableTarget 구현
	virtual bool CanBeColonized() const override;
	virtual float GetRequiredColonizeBiomass() const override;
	virtual float GetColonizeRadiusCm() const override { return CurrentRadiusCm; }
	virtual bool IsColonizingInProgress() const override { return ColonizationState == EEnemyMoldColonizationState::Colonizing; }
	virtual void SetColonizationProgressVisual(float Progress) override;
	virtual void CompleteColonization(UMaterialInterface* HostSkinMaterial, AAsteroidFieldManager* FieldMgr) override;
	virtual void ResetColonizationState() override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> CollisionSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> MoldMesh;

private:
	void EnsureVisualAssets();
	void ApplyBodySize(float InRadiusCm);
	void OnColonizationIncubationExpired();
	void OnWebSlowTimerExpired();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> EnemyMID;

	UPROPERTY(Transient)
	TObjectPtr<UPhysicalMaterial> RuntimePhysicalMaterial;

	TWeakObjectPtr<AEnemyMoldManager> OwnerManager;
	TWeakObjectPtr<AAsteroidFieldManager> OwnerFieldManager;

	FTimerHandle IncubationTimerHandle;
	FTimerHandle WebSlowTimerHandle;
	float CurrentBiomass = 120.0f;
	float CurrentRadiusCm = 90.0f;
	float InitialSpawnRadiusCm = 90.0f;
	float ColonizationProgress = 0.0f;
	float WebSlowSpeedMultiplier = 1.0f;
	bool bIsWebSlowed = false;
	bool bIsHuntableVisual = false;
	bool bIsDormant = true;
	EEnemyMoldColonizationState ColonizationState = EEnemyMoldColonizationState::Uncolonized;
};
