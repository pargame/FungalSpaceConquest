#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "GameFramework/Actor.h"
#include "SpaceColonizableTarget.h"
#include "SpaceAsteroid.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPhysicalMaterial;
class AAsteroidFieldManager;

/** 소행성의 균류 전염(식민지화) 상태입니다. */
UENUM(BlueprintType)
enum class EAsteroidColonizationState : uint8
{
	Uncolonized UMETA(DisplayName = "Uncolonized (미감염)"),
	Colonizing UMETA(DisplayName = "Colonizing (표면 전이 중)"),
	ColonizedIncubating UMETA(DisplayName = "ColonizedIncubating (피부 덮임 · 배양 중)")
};

/**
 * 단일 랜덤 시드로부터 질량(1:1 부피 연동), 3D 노이즈·크레이터 형상, 표면 재질,
 * Chaos 물리 충돌/반발까지 결정론적으로 동적 생성되는 우주 소행성 액터입니다.
 */
UCLASS(Blueprintable)
class UESTUDY_API ASpaceAsteroid : public AActor, public ISpaceColonizableTarget
{
	GENERATED_BODY()

public:
	ASpaceAsteroid();

	/** 소행성의 질량·부피·3D 형상·크레이터·표면 색조와 질감을 1:1로 결정하는 고유 시드값입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Seed", meta = (ClampMin = "0", UIMin = "0", UIMax = "99999"))
	int32 Seed = 1042;

	/** 에디터 디테일 패널에서 시드나 형상 수치를 조절할 때마다 실시간으로 메시와 재질을 다시 생성할지 여부입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Seed")
	bool bAutoRebuildOnEdit = true;

	/** 시드 기반 분포에서 뽑힐 수 있는 소행성 최소 질량(kg)입니다. 부피는 밀도에 반비례해 1:1로 자동 결정됩니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|MassAndVolume", meta = (ClampMin = "1.0", UIMin = "5.0", UIMax = "5000.0"))
	float MinMassKg = 25.0f;

	/** 시드 기반 분포에서 뽑힐 수 있는 소행성 최대 질량(kg)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|MassAndVolume", meta = (ClampMin = "1.0", UIMin = "50.0", UIMax = "1000000.0"))
	float MaxMassKg = 420000.0f;

	/** 반경·질량 난수 편향 지수입니다(SizeDistributionCurve에 키가 없을 때 폴백으로 사용). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|MassAndVolume", meta = (ClampMin = "0.2", ClampMax = "6.0", UIMin = "0.5", UIMax = "5.0"))
	float MassDistributionExponent = 1.85f;

	/**
	 * 언리얼 내장 커브(FRichCurve / 외부 UCurveFloat 에셋 호환) 기반 소행성 반경 스케일 분포입니다.
	 * X축(0~1 난수) -> Y축(0=최소 반경 ~ 1=최대 반경)을 에디터 그래프에서 마우스로 직접 조절할 수 있습니다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|MassAndVolume")
	FRuntimeFloatCurve SizeDistributionCurve;

	/** 소행성 암석 평균 밀도(kg/m³)입니다. 목표 부피(m³) = 생성 질량 / 밀도로 계산되어 외형 크기가 결정됩니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|MassAndVolume", meta = (ClampMin = "10.0", ClampMax = "5000.0", UIMin = "50.0", UIMax = "1000.0"))
	float RockDensityKgPerM3 = 260.0f;

	/** 시드가 정하는 질량 대신 아래 수동 질량 값을 직접 강제할지 여부입니다(테스트용). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|MassAndVolume")
	bool bOverrideSeedMass = false;

	/** 수동 강제 질량(kg)입니다. 활성화 시 이 질량에 정확히 맞는 부피로 소행성이 스케일됩니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|MassAndVolume", meta = (ClampMin = "1.0", EditCondition = "bOverrideSeedMass"))
	float ManualMassKg = 250.0f;

	/** 렌더링용 아이코스피어 분할 단계(1~4)입니다. 기본 4단계는 2,562정점·5,120삼각형으로 근접 시에도 크레이터와 암반 실루엣이 선명합니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Shape", meta = (ClampMin = "1", ClampMax = "4"))
	int32 VisualSubdivisionLevel = 4;

	/** Chaos 물리 Convex 콜리전 생성에 사용할 분할 단계(1~3)입니다. 낮을수록 물리 쿠킹과 충돌 연산이 가볍습니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Shape", meta = (ClampMin = "1", ClampMax = "3"))
	int32 CollisionSubdivisionLevel = 2;

	/** 3D FBM 노이즈에 의해 암석 표면이 울퉁불퉁하게 변형되는 강도 비율입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Shape", meta = (ClampMin = "0.0", ClampMax = "0.55"))
	float MacroDeformStrength = 0.28f;

	/** 깎인 암석 모서리와 능선(Ridge)이 도드라지는 비율입니다. 값이 클수록 각진 운석 느낌이 강해집니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Shape", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RidgeSharpness = 0.45f;

	/** 시드별로 소행성 축 비율이 길쭉하거나 납작하게 찌그러지는 최대 편차 비율입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Shape", meta = (ClampMin = "0.0", ClampMax = "0.45"))
	float MaxAnisotropy = 0.26f;

	/** 소행성 표면에 파이는 최소 운석 충돌 구덩이(크레이터) 개수입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Shape", meta = (ClampMin = "0", ClampMax = "16"))
	int32 MinCraterCount = 3;

	/** 소행성 표면에 파이는 최대 운석 충돌 구덩이(크레이터) 개수입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Shape", meta = (ClampMin = "0", ClampMax = "16"))
	int32 MaxCraterCount = 7;

	/** 크레이터가 안쪽으로 파여 들어가는 깊이 강도입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Shape", meta = (ClampMin = "0.0", ClampMax = "0.45"))
	float CraterDepthScale = 0.22f;

	/** 소행성 표면에 적용할 베이스 절차적 머티리얼입니다(비워 두면 M_SpaceAsteroid 자동 연결). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Visual")
	TObjectPtr<UMaterialInterface> AsteroidMaterial;

	/** 우주 별빛을 받아 외곽 실루엣이 은은하게 살아나는 프레넬 림 강도 기본값입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Visual", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BaseRimIntensity = 0.09f;

	/** 3D 노멀 노이즈로 암석 표면의 거친 미세 요철을 살리는 강도 기본값입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Visual", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BaseBumpStrength = 0.34f;

	/** 풀에서 깨어나거나 스폰될 때 원거리 스케일 인으로 서서히 나타나는 페이드인 시간(초)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Visual", meta = (ClampMin = "0.05", ClampMax = "5.0"))
	float SpawnFadeDuration = 1.0f;

	/** 카메라 기준 이 거리 안쪽에서는 100% 실제 크기로 선명하게 표시됩니다(cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Visual", meta = (ClampMin = "500.0", ClampMax = "30000.0"))
	float DistanceFadeStartCm = 8800.0f;

	/** 카메라 기준 이 거리에 가까워질수록 원근 스케일 인으로 수렴해 원거리 팝인을 숨깁니다(cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Visual", meta = (ClampMin = "800.0", ClampMax = "40000.0"))
	float DistanceFadeEndCm = 11000.0f;

	/** 런타임(PIE)에서 Chaos 강체 물리 시뮬레이션을 활성화할지 여부입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Physics")
	bool bSimulatePhysics = true;

	/** 물리 머티리얼 자산을 따로 쓰는 대신 아래 탄성·마찰 수치를 런타임 물리 재질에 직접 반영할지 여부입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Physics")
	bool bOverridePhysicsMaterialValues = true;

	/** 소행성 충돌 시 튕겨 나가는 탄성 반발 계수(Restitution, 0=무반발 ~ 1=완전 탄성)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Physics", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bOverridePhysicsMaterialValues"))
	float Restitution = 0.78f;

	/** 소행성 표면 마찰 계수(Friction)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Physics", meta = (ClampMin = "0.0", ClampMax = "2.0", EditCondition = "bOverridePhysicsMaterialValues"))
	float Friction = 0.35f;

	/** 외부 물리 머티리얼 에셋을 직접 지정하고 싶을 때 연결합니다(bOverridePhysicsMaterialValues 해제 시 적용). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Physics", meta = (EditCondition = "!bOverridePhysicsMaterialValues"))
	TObjectPtr<UPhysicalMaterial> OverridePhysicalMaterial;

	/** 무중력 우주 공간에서의 선형 속도 감쇠(Linear Damping)입니다. 낮을수록 관성을 오래 유지합니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Physics", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float LinearDamping = 0.005f;

	/** 무중력 우주 공간에서의 회전 각속도 감쇠(Angular Damping)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Physics", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float AngularDamping = 0.01f;

	/** BeginPlay 시 시드 기반으로 초기 유영 속도와 느린 자전 각속도를 부여할지 여부입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Physics")
	bool bApplySeededInitialDrift = true;

	/** 초기 선형 이동 속도 최소·최대 범위(cm/s)입니다. 거의 정지한 암석부터 고속 폭주 운석까지 넓은 혼돈 범위를 가집니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Physics", meta = (EditCondition = "bApplySeededInitialDrift"))
	FVector2D InitialLinearSpeedRange = FVector2D(15.0f, 2400.0f);

	/** 초기 선형 속도 분산도입니다(1.0 = 균등, 클수록 아주 느리거나 아주 빠른 양극단 비율 증가, 1 미만 = 중간 속도 집중). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Physics", meta = (ClampMin = "0.05", ClampMax = "8.0", UIMin = "0.2", UIMax = "5.0", EditCondition = "bApplySeededInitialDrift"))
	float LinearSpeedDistributionExponent = 1.65f;

	/** SpeedDistributionCurve에 키가 없을 때 대신 사용하는 회전 각속력 분산도입니다(1.0 = 균등, 클수록 아주 느리거나 아주 빠른 양극단 비율 증가). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Physics", meta = (ClampMin = "0.05", ClampMax = "8.0", UIMin = "0.2", UIMax = "5.0", EditCondition = "bApplySeededInitialDrift"))
	float AngularSpeedDistributionExponent = 1.50f;

	/**
	 * 언리얼 내장 커브(FRichCurve / 외부 UCurveFloat 호환) 기반 초기 선형 속도 분포입니다.
	 * X축(0~1 난수) -> Y축(0=최소 속도 ~ 1=최대 속도)으로 정지 암석·중속 유영·고속 폭주 운석 비율을 제어합니다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Physics", meta = (EditCondition = "bApplySeededInitialDrift"))
	FRuntimeFloatCurve SpeedDistributionCurve;

	/** 초기 자전 각속도 최소·최대 범위(deg/s)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Physics", meta = (EditCondition = "bApplySeededInitialDrift"))
	FVector2D InitialAngularSpeedDegRange = FVector2D(2.0f, 140.0f);

	/** 현재 시드와 밀도로부터 계산된 최종 질량(kg)입니다. Chaos 강체 질량과 정확히 일치합니다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SpaceAsteroid|GeneratedStats")
	float GeneratedMassKg = 0.0f;

	/** 질량/밀도로부터 1:1로 정규화된 실제 폐곡면 메시 부피(m³)입니다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SpaceAsteroid|GeneratedStats")
	float GeneratedVolumeM3 = 0.0f;

	/** 정규화된 부피에 대응하는 등가 평균 반지름(cm)입니다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SpaceAsteroid|GeneratedStats")
	float GeneratedRadiusCm = 0.0f;

	/** 생성된 시각 메시 정점 수입니다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SpaceAsteroid|GeneratedStats")
	int32 GeneratedVertexCount = 0;

	/** 생성된 시각 메시 삼각형 수입니다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SpaceAsteroid|GeneratedStats")
	int32 GeneratedTriangleCount = 0;

	/** 현재 시드에서 생성된 크레이터 개수입니다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SpaceAsteroid|GeneratedStats")
	int32 GeneratedCraterCount = 0;

	/** 시드로부터 결정된 암석 광물 계열(Carbonaceous Basalt / Stony Silicate / Cosmic Chondrite)입니다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SpaceAsteroid|GeneratedStats")
	FString GeneratedArchetypeLabel;

	/** 현재 시드 및 파라미터 설정을 바탕으로 메시·콜리전·표면 재질·물리 질량을 즉시 다시 생성합니다. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "SpaceAsteroid|Seed")
	void RebuildAsteroidFromSeed();

	/** 새로운 랜덤 시드를 뽑고 즉시 소행성을 재생성합니다. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "SpaceAsteroid|Seed")
	void RandomizeSeed();

	/** 외부 매니저(풀링 등)에서 특정 시드를 주입해 소행성을 초기화할 때 호출합니다. */
	UFUNCTION(BlueprintCallable, Category = "SpaceAsteroid|Seed")
	void InitializeWithSeed(int32 NewSeed);

	/** 스폰 직후 초기 시드 빌드를 건너뛰어 풀 대기 슬롯의 중복 생성을 방지합니다(SpawnActorDeferred 용). */
	void SetDeferInitialBuild(bool bDefer);

	/** 풀링 매니저가 범위 이탈 시 메시·콜리전·속도를 유지한 채 소행성을 휴면(Dormant) 상태로 전환합니다. */
	void EnterDormantPool();

	/** 풀링 매니저가 범위 재진입 시 기존 메시·위치·속도를 그대로 유지한 채 소행성을 즉시 재가동합니다. */
	void WakeFromDormantPool(bool bImmediateFade = false);

	/** 근거리(Chaos 물리 활성) ↔ 원거리(Kinematic 등속 유영) 물리 계층을 전환합니다. */
	void SetPhysicsSimulationTier(bool bEnableChaosPhysics);

	/** 원거리 Kinematic 계층일 때 Chaos 솔버 없이 등속 이동·자전을 가볍게 적분합니다. */
	void StepKinematicDrift(float DeltaTime);

	/** 필드 매니저에서 지정한 이동·회전 속력 범위 및 분산도를 소행성에 덮어씁니다. */
	void ApplyFieldSpeedOverrides(
		const FVector2D& InLinearSpeedRange,
		float InLinearSpeedDispersion,
		const FVector2D& InAngularSpeedDegRange,
		float InAngularSpeedDispersion
	);

	FORCEINLINE bool IsPhysicsTierActive() const { return bPhysicsTierActive; }

	/** 풀 프리웜 단계에서 고유 아키타입 시드로 메시·콜리전·재질을 1회 사전 베이킹한 뒤 즉시 Dormant 대기 상태로 전환합니다. */
	void BakePooledArchetype(int32 ArchetypeSeed);

	/** 퇴거·재할당된 풀 소행성을 새 좌표로 이동시킨 뒤 활성화합니다. bReusePrebakedGeometry가 true이고 이미 구워진 메시가 있으면 재빌드 없이 스위치만 켭니다(0ms). */
	void ActivatePooledWithSeed(int32 NewSeed, const FTransform& NewWorldTransform, bool bReusePrebakedGeometry = false, bool bImmediateFade = false);

	/** 스트리밍 매니저의 활성화 반경에 맞춰 디더 거리 페이드 구간(cm)을 동기화합니다. */
	void SetDistanceFadeRange(float InFadeStartCm, float InFadeEndCm);

	FORCEINLINE bool HasBuiltGeometry() const { return bHasBuiltOnce; }

	FORCEINLINE bool IsManagedDormant() const { return bManagedDormant; }

	/** 프리베이크된 로컬 기준 반지름(cm, RuntimeSizeScale 미반영)입니다. */
	FORCEINLINE float GetBaseGeneratedRadiusCm() const { return GeneratedRadiusCm; }

	/** 런타임 스케일(RuntimeSizeScale)이 반영된 실제 월드 반지름(cm)입니다. */
	FORCEINLINE float GetGeneratedRadiusCm() const { return GeneratedRadiusCm * RuntimeSizeScale; }

	/** 런타임 스케일(RuntimeSizeScale^3)이 반영된 실제 물리 질량(kg)입니다. */
	FORCEINLINE float GetEffectiveMassKg() const { return FMath::Max(GeneratedMassKg * FMath::Pow(RuntimeSizeScale, 3.0f), 1.0f); }

	FORCEINLINE float GetRuntimeSizeScale() const { return RuntimeSizeScale; }

	FORCEINLINE UProceduralMeshComponent* GetAsteroidMesh() const { return AsteroidMesh; }

	/** 생성된 소행성 메시 표면에서 시드 기반으로 로컬 부착 좌표와 외향 법선을 1회 샘플링합니다. */
	bool SampleSurfaceAnchor(int32 SampleSeed, FVector& OutLocalPoint, FVector& OutLocalNormal) const;

	// ============================================================================
	// 균류 전염(식민지화) & 배양 폭발 파라미터/API
	// ============================================================================

	/** 기준 반지름(ColonizeReferenceRadiusCm) 소행성을 완전히 뒤덮는 데 필요한 기본 생체량입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Colonization", meta = (ClampMin = "1.0"))
	float ColonizeRequiredBiomassBase = 14.0f;

	/** 전염 비용 계산의 기준이 되는 소행성 반지름(cm)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Colonization", meta = (ClampMin = "10.0"))
	float ColonizeReferenceRadiusCm = 100.0f;

	/** 소행성 반지름 비율에 적용할 지수입니다(2.0 = 표면적 비례). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Colonization", meta = (ClampMin = "0.5", ClampMax = "4.0"))
	float ColonizeCostRadiusExponent = 2.0f;

	/** 완전히 피부로 뒤덮인 뒤 미네랄 폭발이 일어나기까지의 배양 시간(초)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Colonization", meta = (ClampMin = "0.2", ClampMax = "30.0"))
	float ColonizeIncubationSeconds = 1.2f;

	/** 전염 중 소행성 맥(Vein) 및 림 발광에 블렌딩할 균류 피부 색조입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceAsteroid|Colonization")
	FLinearColor ColonizedSkinTint = FLinearColor(0.24f, 0.94f, 0.74f, 1.0f);

	/** 이 소행성을 완전히 뒤덮는 데 필요한 총 생체량을 반환합니다. */
	UFUNCTION(BlueprintPure, Category = "SpaceAsteroid|Colonization")
	virtual float GetRequiredColonizeBiomass() const override;

	/** 현재 전염(식민지화)을 시작하거나 진행할 수 있는 상태인지 반환합니다. */
	UFUNCTION(BlueprintPure, Category = "SpaceAsteroid|Colonization")
	virtual bool CanBeColonized() const override { return ColonizationState != EAsteroidColonizationState::ColonizedIncubating && !bManagedDormant && !IsHidden(); }

	virtual float GetColonizeRadiusCm() const override { return GetGeneratedRadiusCm(); }
	virtual bool IsColonizingInProgress() const override { return ColonizationState == EAsteroidColonizationState::Colonizing; }

	UFUNCTION(BlueprintPure, Category = "SpaceAsteroid|Colonization")
	EAsteroidColonizationState GetColonizationState() const { return ColonizationState; }

	UFUNCTION(BlueprintPure, Category = "SpaceAsteroid|Colonization")
	float GetColonizationProgress() const { return ColonizationProgress; }

	/** 전염 진행도(0~1)에 따라 표면 재질 연출을 갱신합니다. */
	virtual void SetColonizationProgressVisual(float Progress) override;

	/** 소행성 전체가 피부로 뒤덮였을 때 배양 상태에 진입하고 폭발 타이머를 무장합니다. */
	virtual void CompleteColonization(UMaterialInterface* HostSkinMaterial, AAsteroidFieldManager* FieldMgr) override;

	/** 전염 실패/중단 또는 풀 반납 시 전염 상태·타이머·원래 암석 머티리얼을 초기화합니다. */
	virtual void ResetColonizationState() override;

protected:
	/** 절차적 메시 렌더링과 Chaos Convex 물리 시뮬레이션을 모두 담당하는 루트 컴포넌트입니다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UProceduralMeshComponent> AsteroidMesh;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	struct FCraterSpec
	{
		FVector Direction = FVector::UpVector;
		float AngularRadiusRad = 0.35f;
		float CosOuterRadius = -1.0f;
		float Depth = 0.14f;
		float RimHeight = 0.04f;
	};

	struct FUnitIcosphereTemplate
	{
		TArray<FVector> UnitVerts;
		TArray<int32> Triangles;
		TArray<FVector2D> SphericalUVs;
	};

	struct FShapeSeedContext
	{
		FRandomStream Stream;
		FQuat StretchRotation = FQuat::Identity;
		FVector AxisStretch = FVector::OneVector;
		FVector MacroNoiseOffset = FVector::ZeroVector;
		FVector RidgeNoiseOffset = FVector::ZeroVector;
		FVector DetailNoiseOffset = FVector::ZeroVector;
		float MacroFreq = 1.6f;
		float RidgeFreq = 2.8f;
		TArray<FCraterSpec> Craters;
	};

	uint32 ComputeBuildSignature() const;
	void BuildShapeContext(FShapeSeedContext& OutContext);
	float EvaluateDisplacementAtUnitDir(const FVector& UnitDir, const FShapeSeedContext& Context, float& OutCraterMask, float& OutRidgeMask) const;

	static void BuildUnitIcosphere(int32 SubdivisionLevel, TArray<FVector>& OutUnitVerts, TArray<int32>& OutTriangles);
	static const FUnitIcosphereTemplate& GetCachedUnitIcosphere(int32 SubdivisionLevel);
	static double ComputeSignedVolumeUnitMesh(const TArray<FVector>& Verts, const TArray<int32>& Triangles);

	void ApplySeededSurfaceMaterial(const FVector& MaterialSeedOffset);
	void ApplySpawnMaterialFade(bool bImmediateFade);
	void ApplyChaosPhysicsSettings();
	void ApplySeededInitialDrift();
	void OnColonizationIncubationExpired();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> AsteroidMID;

	UPROPERTY(Transient)
	TObjectPtr<UPhysicalMaterial> RuntimePhysicalMaterial;

	EAsteroidColonizationState ColonizationState = EAsteroidColonizationState::Uncolonized;
	float ColonizationProgress = 0.0f;
	float BaseSurfaceVeinIntensity = 0.35f;
	float BaseSurfaceRimIntensity = 0.4f;
	FLinearColor BaseSurfaceVeinColor = FLinearColor::White;
	FLinearColor BaseSurfaceRimColor = FLinearColor(0.48f, 0.64f, 0.96f, 1.0f);
	FTimerHandle ColonizationIncubationTimerHandle;
	TWeakObjectPtr<AAsteroidFieldManager> OwnerFieldManager;

	FVector SavedDormantLinearVelocity = FVector::ZeroVector;
	FVector SavedDormantAngularVelocityDeg = FVector::ZeroVector;
	FVector CachedDriftLinearVelocity = FVector::ZeroVector;
	FVector CachedDriftAngularVelocityDeg = FVector::ZeroVector;
	bool bPhysicsTierActive = false;
	bool bManagedDormant = false;
	bool bDeferInitialBuild = false;
	float RuntimeSizeScale = 1.0f;

	uint32 LastBuiltSignature = 0;
	bool bHasBuiltOnce = false;
};
