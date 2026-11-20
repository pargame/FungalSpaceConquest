// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpaceMineral.generated.h"

class AAsteroidFieldManager;
class ASpaceAsteroid;
class ASpaceDrifterPawn;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProceduralMeshComponent;

/** 우주 미네랄 광석의 독립적 상태 머신 (휴면 / 소행성 부착 / 부유 / 흡착 / 흡수). */
UENUM(BlueprintType)
enum class ESpaceMineralState : uint8
{
	Dormant UMETA(DisplayName = "Dormant (풀 대기)"),
	AttachedToAsteroid UMETA(DisplayName = "Attached To Asteroid (소행성 표면)"),
	FreeFloating UMETA(DisplayName = "Free Floating (우주 부유)"),
	Adsorbed UMETA(DisplayName = "Adsorbed (촉수 흡착)"),
	Absorbing UMETA(DisplayName = "Absorbing (본체 흡수 진행 중)")
};

/**
 * 곰팡이 부유체가 그물 촉수(흡착 -> 흡수) 또는 직접 접촉(즉시 흡수)으로 섭취할 수 있는
 * 결정형 우주 미네랄 광석 액터입니다.
 */
UCLASS(Blueprintable)
class UESTUDY_API ASpaceMineral : public AActor
{
	GENERATED_BODY()

public:
	ASpaceMineral();

	/** 흡수(Absorbing) 상태 시작 후 곰팡이 중심으로 빨려 들어가며 소멸·섭취가 완료되기까지의 연출 시간(초)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMineral|Absorption", meta = (ClampMin = "0.04", ClampMax = "2.0"))
	float AbsorbDurationSeconds = 0.18f;

	/** 풀 프리웜 단계에서 결정 클러스터 메시와 재질을 1회 사전 베이킹한 뒤 Dormant 대기 상태로 전환합니다. */
	void BakePooledArchetype(int32 ArchetypeSeed);

	/** 소행성 표면에 돋아난 광석 상태로 활성화합니다. (0ms 스위치 ON) */
	void ActivateOnAsteroidSurface(
		AAsteroidFieldManager* InManager,
		ASpaceAsteroid* HostAsteroid,
		uint32 InMineralHash,
		const FVector& LocalSurfacePoint,
		const FVector& LocalSurfaceNormal,
		float ClusterScale
	);

	/** 소행성 사이 공간에 떠다니는 부유 광석 상태로 활성화합니다. (0ms 스위치 ON) */
	void ActivateFreeFloating(
		AAsteroidFieldManager* InManager,
		uint32 InMineralHash,
		const FVector& WorldLocation,
		const FRotator& WorldRotation,
		float ClusterScale,
		const FVector& InSpinRatesDeg,
		const FVector& InInitialDriftVelocity = FVector::ZeroVector
	);

	/** 풀 대기로 복귀시킵니다. */
	void EnterDormantPool();

	/**
	 * [1단계: 흡착(Adsorption)] 그물 촉수 등 외부 매개체에 미네랄을 달라붙게 하고 표면 프레넬 형광을 켭니다.
	 * 플레이어 본체와 닿기 전까지는 흡수되지 않고 흡착 상태를 유지합니다.
	 */
	bool BeginAdsorption(
		ASpaceDrifterPawn* CapturerPawn,
		float InNetAnchorT,
		const FVector2D& InRadialDir2D,
		float InRadialFraction
	);

	/**
	 * [2단계: 흡수(Absorption)] 플레이어 본체에 닿았을 때 호출되어 본체 중심부로 빨려 들어가며 섭취를 완료합니다.
	 * 이미 흡착(Adsorbed)된 미네랄은 물론, 아직 흡착되지 않은 일반 미네랄도 본체 직접 접촉 시 즉시 흡수 모드로 전환합니다.
	 */
	bool BeginAbsorption(ASpaceDrifterPawn* CapturerPawn);

	/** 흡착(Adsorbed) 상태에서 매 프레임 그물 실시간 좌표로 동기화합니다. 플레이어 본체 접촉 시 자동으로 흡수(Absorbing)로 전이하고 true를 반환합니다. */
	bool SyncAdsorbedPosition(
		float DeltaTime,
		const FVector& NetWorldPos,
		const FVector& PawnBodyPos,
		float AbsorbContactRadius,
		bool bIsRetracting,
		float NetEasedReach
	);

	/** 그물 스타일 전환/해제 등으로 그물 구동이 중단된 경우 남은 흡착 광석을 프레넬이 켜진 채 본체로 끌어와 흡수로 이어지게 합니다. */
	void SwitchToFallbackDirectPull();

	FORCEINLINE float GetNetAnchorT() const { return NetAnchorT; }
	FORCEINLINE FVector2D GetNetRadialDir2D() const { return NetRadialDir2D; }
	FORCEINLINE float GetNetRadialFraction() const { return NetRadialFraction; }

	UFUNCTION(BlueprintPure, Category = "SpaceMineral")
	FORCEINLINE ESpaceMineralState GetMineralState() const { return MineralState; }

	FORCEINLINE bool IsAvailableForAdsorption() const
	{
		return (MineralState == ESpaceMineralState::AttachedToAsteroid || MineralState == ESpaceMineralState::FreeFloating) && !IsHidden();
	}
	FORCEINLINE bool IsAdsorbed() const { return MineralState == ESpaceMineralState::Adsorbed; }
	FORCEINLINE bool IsAbsorbing() const { return MineralState == ESpaceMineralState::Absorbing; }
	/** 풀 매니저가 청크 언로드/재활용 시 플레이어가 흡착 또는 흡수 중인 미네랄을 건드리지 않도록 보호하는 헬퍼입니다. */
	FORCEINLINE bool IsBeingAbsorbed() const
	{
		return MineralState == ESpaceMineralState::Adsorbed || MineralState == ESpaceMineralState::Absorbing;
	}
	FORCEINLINE ASpaceAsteroid* GetHostAsteroid() const { return HostAsteroidPtr.Get(); }
	FORCEINLINE uint32 GetMineralHash() const { return ActiveMineralHash; }

	/** 폭발 미네랄 등 특수 광석이 기본 MineralBiomassGain 대신 지급할 생체량 보상(0 이하이면 폰 기본값 사용)입니다. */
	void SetBiomassRewardOverride(float InBiomassReward) { BiomassRewardOverride = InBiomassReward; }
	FORCEINLINE float GetBiomassRewardOverride() const { return BiomassRewardOverride; }

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UProceduralMeshComponent> MineralMesh;

	/** 미네랄 크리스탈 메시에 적용할 머티리얼입니다(비워 두면 기본 머티리얼 자동 적용). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMineral|Visual")
	TObjectPtr<UMaterialInterface> MineralMaterial;

private:
	void BuildCrystalClusterGeometry(int32 ArchetypeSeed);
	void AppendCrystalSpire(
		const FVector& BaseOffset,
		const FQuat& Orientation,
		float HeightCm,
		float BaseRadiusCm,
		int32 Sides,
		TArray<FVector>& OutVerts,
		TArray<int32>& OutTris,
		TArray<FVector>& OutNormals,
		TArray<FVector2D>& OutUVs,
		TArray<FLinearColor>& OutColors
	) const;
	void FinishAbsorption();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MineralMID;

	TWeakObjectPtr<AAsteroidFieldManager> OwnerManager;
	TWeakObjectPtr<ASpaceAsteroid> HostAsteroidPtr;
	TWeakObjectPtr<ASpaceDrifterPawn> CapturerPtr;

	ESpaceMineralState MineralState = ESpaceMineralState::Dormant;
	uint32 ActiveMineralHash = 0;
	float BiomassRewardOverride = -1.0f;
	bool bHasBakedGeometry = false;

	FVector BaseScale3D = FVector::OneVector;
	FVector FloatingSpinDeg = FVector(6.0f, 12.0f, 9.0f);
	FVector FloatingDriftVelocity = FVector::ZeroVector;
	FVector AdsorbStartWorldPos = FVector::ZeroVector;
	FVector AbsorbStartWorldPos = FVector::ZeroVector;
	FVector AbsorbStartScale3D = FVector::OneVector;
	FVector PullVelocity = FVector::ZeroVector;
	FVector2D NetRadialDir2D = FVector2D(1.0f, 0.0f);
	float NetAnchorT = 0.85f;
	float NetRadialFraction = 0.0f;
	float NetBindBlendAlpha = 0.0f;
	float AdsorbElapsed = 0.0f;
	float AbsorbElapsed = 0.0f;
	bool bFallbackDirectPull = false;
};
