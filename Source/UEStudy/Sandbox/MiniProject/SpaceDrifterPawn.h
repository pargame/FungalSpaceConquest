#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "SpaceTendrilHostInterface.h"
#include "SpaceDrifterPawn.generated.h"

class USphereComponent;
class USceneComponent;
class UStaticMeshComponent;
class USpringArmComponent;
class UCameraComponent;
class UFXSystemComponent;
class UInputAction;
class UMaterialInstanceDynamic;
class UPhysicalMaterial;
class UPrimitiveComponent;
class ASpaceMineral;
class AAsteroidFieldManager;
class USpaceTendrilAbility;
class ULongTendrilAbility;
class UNetTendrilAbility;
class UJetTendrilAbility;
struct FInputActionValue;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSpaceMineralConsumedSignature, int32, NewTotalMineralCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSpaceDrifterGameOverSignature);

UENUM(BlueprintType)
enum class ESpaceRunState : uint8
{
	Playing,
	Victory,
	GameOver
};

/**
 * 3인칭 쿼터니언 궤도 카메라, 외력/반동 기반 부피 보존 물풍선 탄성, 그리고
 * ISpaceTendrilHostInterface 기반 다형성 촉수 능력(USpaceTendrilAbility 3종)을 갖춘 무중력 부유체 폰입니다.
 */
UCLASS(Blueprintable)
class UESTUDY_API ASpaceDrifterPawn : public APawn, public ISpaceTendrilHostInterface
{
	GENERATED_BODY()

public:
	ASpaceDrifterPawn();

	/** 게임패드 등 일반 시점 회전 입력 액션(Axis2D)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	/** 마우스 이동 기반 시점 회전 입력 액션(Axis2D)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MouseLookAction;

	/** 물풍선 탄성 출렁임을 즉시 테스트하는 입력 액션입니다(Start 시 하향 충격 적용). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> TestBounceAction;

	/** 촉수를 뻗는 입력 액션입니다(예: 스페이스바). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> ExtendTendrilAction;

	/** 플레이 중 촉수 능력(V1 긴 촉수 ↔ V2 그물 촉수 ↔ V3 이동용 촉수)을 순환 전환하는 입력 액션입니다(예: Tab). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> ToggleTendrilStyleAction;

	/** 현재 선택된 촉수 능력 스타일입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Tendril")
	ETendrilVisualStyle TendrilVisualStyle = ETendrilVisualStyle::MyceliumWeb;

	/** V1: 1번 긴 촉수 능력 인스턴스입니다(EditInlineNew). */
	UPROPERTY(VisibleAnywhere, Instanced, BlueprintReadOnly, Category = "SpaceDrifter|Abilities")
	TObjectPtr<ULongTendrilAbility> LongTendrilAbility;

	/** V2: 2번 그물 촉수 능력 인스턴스입니다(EditInlineNew). */
	UPROPERTY(VisibleAnywhere, Instanced, BlueprintReadOnly, Category = "SpaceDrifter|Abilities")
	TObjectPtr<UNetTendrilAbility> NetTendrilAbility;

	/** V3: 3번 이동용 분사 촉수 능력 인스턴스입니다(EditInlineNew). */
	UPROPERTY(VisibleAnywhere, Instanced, BlueprintReadOnly, Category = "SpaceDrifter|Abilities")
	TObjectPtr<UJetTendrilAbility> JetTendrilAbility;

	/** 긴 촉수(V1)용 나이아가라 컴포넌트 참조입니다. 비워 두면 에셋/컴포넌트에서 자동으로 찾아 연결합니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Tendril")
	TObjectPtr<UFXSystemComponent> MyceliumWebFX;

	/** 그물 촉수(V2)용 나이아가라 컴포넌트 참조입니다. 비워 두면 에셋/컴포넌트에서 자동으로 찾아 연결합니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Tendril")
	TObjectPtr<UFXSystemComponent> GossamerSporesFX;

	/** 이동용 촉수(V3)용 나이아가라 컴포넌트 참조입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Tendril")
	TObjectPtr<UFXSystemComponent> SporeJetFX;

	/** 촉수가 뻗어 나가는 속도 배율(초당 정규화 알파 증가량, 1/s)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Tendril", meta = (ClampMin = "0.2"))
	float TendrilExtendSpeed = 2.8f;

	/** 촉수가 몸 안으로 되감겨 들어오는 속도 배율(1/s)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Tendril", meta = (ClampMin = "0.2"))
	float TendrilRetractSpeed = 5.2f;

	/** 구체 중심에서 조준 방향으로 촉수 시작점을 안쪽/앞쪽으로 배치하는 거리(cm)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Tendril", meta = (ClampMin = "0.0"))
	float TendrilRootInset = 2.0f;

	/** 촉수가 뻗는 동안 본체 구체가 조준 방향으로 길게 늘어나는 최대 변형 비율입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Tendril", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float TendrilBodyStretch = 0.34f;

	/** 뻗어 있던 촉수를 회수할 때 본체에 가해지는 반동 찌그러짐(스퀴시) 충격 세기입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Tendril", meta = (ClampMin = "0.0"))
	float TendrilRetractImpulse = 7.2f;

	/** 이동용 촉수(V3)를 누르는 동안 조준 반대 방향으로 가해지는 기본 추진 가속도(cm/s²)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|JetTendril", meta = (ClampMin = "0.0", ClampMax = "10000.0"))
	float JetTendrilThrustAccel = 1400.0f;

	/** 이동용 촉수(V3) 가속도가 현재 속력에 반비례(`1 / (1 + Ratio * (Speed / RefSpeed))`)로 줄어드는 기준 속력(cm/s)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|JetTendril", meta = (ClampMin = "100.0", ClampMax = "10000.0"))
	float JetTendrilSpeedDampingRef = 900.0f;

	/** 이동용 촉수(V3) 속력 반비례 감쇠 비율입니다. 0이면 일정 가속도, 클수록 고속 주행 시 가속도 증가가 억제됩니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|JetTendril", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float JetTendrilSpeedInverseRatio = 1.2f;

	/** 이동용 촉수(V3) 분사 이펙트의 기준 길이(cm)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|JetTendril", meta = (ClampMin = "120.0", ClampMax = "10000.0"))
	float JetTendrilVisualLength = 720.0f;

	/** 이동용 촉수(V3) 분사 시 초당 본체 발사체 생성 스케일입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|JetTendril", meta = (ClampMin = "0.0", ClampMax = "500.0"))
	float JetTendrilSpawnRate = 160.0f;

	/** 이동용 촉수(V3) 분사 시 초당 영구 소모되는 생체량(Biomass/s)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|JetTendril", meta = (ClampMin = "0.0", ClampMax = "200.0"))
	float JetBiomassCostPerSecond = 8.0f;

	/** 내 본체 크기(BodyScale) 대비 V3 분사 입자 크기 비율(`내 크기 : 분사 크기`)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|JetTendril", meta = (ClampMin = "0.05", ClampMax = "3.0"))
	float JetParticleToBodySizeRatio = 0.45f;

	/** 긴 촉수(V1) 본(가운데) 가지가 최대로 뻗는 거리(cm)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|LongTendril", meta = (ClampMin = "120.0", ClampMax = "50000.0"))
	float LongTendrilMaxLength = 1800.0f;

	/** 긴 촉수(V1) 본(가운데) 가지의 뿌리 쪽 시작 굵기(cm)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|LongTendril", meta = (ClampMin = "0.2", ClampMax = "100.0"))
	float LongTendrilTrunkWidth = 11.5f;

	/** 긴 촉수(V1) 본 가지와 곁가지 리본 자체의 뿌리 대비 끝단 굵기 테이퍼 비율입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|LongTendril", meta = (ClampMin = "0.0", ClampMax = "3.0", UIMin = "0.0", UIMax = "3.0"))
	float LongTendrilTipWidthRatio = 0.08f;

	/** 긴 촉수(V1) 본 가지 1m(100cm)당 돋아나는 곁가지 빈도(개/m)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|LongTendril", meta = (ClampMin = "0.0", ClampMax = "25.0"))
	float LongTendrilBranchFrequency = 0.70f;

	/** 긴 촉수(V1)가 이 길이(cm) 이하로 짧게 뻗을 때는 곁가지가 생성되지 않습니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|LongTendril", meta = (ClampMin = "100.0", ClampMax = "5000.0"))
	float LongTendrilBranchMinTrunkCm = 420.0f;

	/** 긴 촉수(V1) 곁가지가 끝단 부근에서 최대로 자라나는 기준 길이(cm)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|LongTendril", meta = (ClampMin = "1.0", ClampMax = "5000.0"))
	float LongTendrilBranchLength = 180.0f;

	/** 긴 촉수(V1) 곁가지의 시작 굵기(cm)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|LongTendril", meta = (ClampMin = "0.1", ClampMax = "50.0"))
	float LongTendrilBranchWidth = 4.8f;

	/** 긴 촉수(V1) 끝단 대비 뿌리쪽 곁가지 길이·굵기·퍼짐 비율입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|LongTendril", meta = (ClampMin = "0.0", ClampMax = "4.0", UIMin = "0.0", UIMax = "4.0"))
	float LongTendrilBaseToTipLengthRatio = 0.35f;

	/** 긴 촉수(V1) 본 가지 전체 길이 중 곁가지가 처음 시작되는 위치 비율(0.02~0.90)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|LongTendril", meta = (ClampMin = "0.02", ClampMax = "0.90"))
	float LongTendrilBranchStartRatio = 0.22f;

	/** 긴 촉수(V1) 곁가지들이 옆으로 퍼지는 각도/폭 배율입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|LongTendril", meta = (ClampMin = "0.05", ClampMax = "4.0"))
	float LongTendrilBranchSpreadScale = 1.0f;

	/** 긴 촉수(V1) 최대 길이 도달 후 유지 시간 제한 사용 여부입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|LongTendril")
	bool bLimitLongTendrilHoldDuration = true;

	/** 긴 촉수(V1) 최대 길이 상태로 유지되는 최대 시간(초)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|LongTendril", meta = (ClampMin = "0.0", ClampMax = "60.0", EditCondition = "bLimitLongTendrilHoldDuration"))
	float LongTendrilMaxHoldDuration = 1.5f;

	/** 긴 촉수가 소행성에 꽂혀 있는 동안에도 최대 유지 시간 제한을 적용할지 여부입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|LongTendril", meta = (EditCondition = "bLimitLongTendrilHoldDuration"))
	bool bLimitHoldDurationWhileLatched = false;

	/** 긴 촉수 조준 시 화면 중앙 카메라 레이 주변을 포착하는 스피어 트레이스 반경(cm)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|LongTendril", meta = (ClampMin = "0.0", ClampMax = "80.0"))
	float TendrilAimTraceRadius = 14.0f;

	/** 긴 촉수가 소행성에 꽂혔을 때 양쪽에 가해지는 뉴턴 작용·반작용 인력 세기(Chaos Force)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|LongTendril", meta = (ClampMin = "0.0", UIMin = "10000.0", UIMax = "1000000.0"))
	float TendrilPullForce = 240000.0f;

	/** 촉수로 당길 때 소행성과의 상대 접근 속력 상한(cm/s)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|LongTendril", meta = (ClampMin = "100.0", ClampMax = "5000.0"))
	float MaxRelativePullSpeed = 1100.0f;

	/** 촉수가 꽂힌 표면 지점의 편심 회전 토크를 소행성에 전달하는 비율(0~1)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|LongTendril", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TendrilTorqueTransferRatio = 0.25f;

	/** 촉수가 목표 소행성 표면까지 뻗어나가 인력이 발동되기 시작하는 진행도(0~1) 임계값입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|LongTendril", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float TendrilPullActivationAlpha = 0.85f;

	/** 촉수로 끌어당기는 중 매달린 소행성과 충돌했을 때 즉시 촉수를 해제할지 여부입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|LongTendril")
	bool bAutoDetachTendrilOnTargetHit = false;

	/** 본체 크기 배율 증가에 따라 V1 촉수 최대 길이가 커지는 지수입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|LongTendril", meta = (ClampMin = "0.0", ClampMax = "2.5"))
	float V1LengthGrowthExponent = 1.0f;

	/** V1 락온 중 플레이어와 소행성 표면 사이 거리가 이 값(cm) 이하일 때 전염(몸 뒤덮기)이 시작됩니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|LongTendril", meta = (ClampMin = "10.0", ClampMax = "1000.0"))
	float ColonizeTriggerSurfaceDistanceCm = 140.0f;

	/** V1 전염 중 초당 소행성 표면으로 전이되는 생체량 비율(가용 생체량 대비 %/s)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|LongTendril", meta = (ClampMin = "5.0", ClampMax = "500.0"))
	float ColonizeUsablePercentPerSec = 50.0f;

	/** 소행성에 꽂힌 상태에서 최대 사거리 대비 몇 배까지 벌어지면 촉수가 끊어질지 결정하는 배율입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|LongTendril", meta = (ClampMin = "1.0", ClampMax = "4.0"))
	float MaxTetherBreakDistanceMultiplier = 1.45f;

	/** 그물 촉수(V2)가 한 번 발사되었을 때 최대로 펼쳐지는 거리(cm)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|NetTendril", meta = (ClampMin = "120.0", ClampMax = "50000.0"))
	float NetTendrilMaxReach = 920.0f;

	/** 그물 촉수(V2)가 회수될 때 되감기는 정규화 속도(1/s)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|NetTendril", meta = (ClampMin = "0.5", ClampMax = "15.0"))
	float NetTendrilRetractSpeed = 2.6f;

	/** 그물 촉수(V2) 단발 발사 후 최대 전개 상태로 유지되는 시간(초)입니다(FTimerManager 기반). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|NetTendril", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float NetTendrilHoldDuration = 0.55f;

	/** 나이아가라 그물 깔때기 반경 바깥쪽으로 광석 자체 크기를 보정해 주는 포획 여유 반경(cm)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|NetTendril", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float NetCapturePaddingCm = 24.0f;

	/** 본체 크기 배율 증가에 따라 V2 그물 사거리가 커지는 지수입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|NetTendril", meta = (ClampMin = "0.0", ClampMax = "2.5"))
	float V2ReachGrowthExponent = 1.0f;

	/** 본체 크기 배율 증가에 따라 V2 그물 깔때기 반경이 커지는 지수입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|NetTendril", meta = (ClampMin = "0.0", ClampMax = "2.5"))
	float V2FunnelRadiusGrowthExponent = 1.0f;

	/** 미네랄 광석을 몸 안으로 빨아들여 흡수 완료했을 때 본체에 가해지는 스퀴시 반동 세기입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|NetTendril", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float MineralAbsorbSquishImpulse = 5.4f;

	/** 곰팡이 본체가 미네랄과 직접 맞닿았을 때 즉시 흡수(Absorbing)할지 여부입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Minerals")
	bool bEnableDirectMineralContactAbsorb = true;

	/** 현재 본체 반경(GetTendrilBodyRadius)에 곱해지는 직접 접촉 흡수 반경 배율입니다(예: 1.45 = 본체 반경의 1.45배). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Minerals", meta = (ClampMin = "1.0", ClampMax = "4.0", EditCondition = "bEnableDirectMineralContactAbsorb"))
	float DirectContactAbsorbRadiusMultiplier = 1.45f;

	/** 배율이 적용된 본체 흡수 반경 바깥쪽으로 추가되는 고정 여유 거리(cm)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Minerals", meta = (ClampMin = "0.0", ClampMax = "500.0", EditCondition = "bEnableDirectMineralContactAbsorb"))
	float DirectContactAbsorbMarginCm = 55.0f;

	/** 현재 세션 동안 흡수한 누적 미네랄 개수입니다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SpaceDrifter|Minerals")
	int32 ConsumedMineralCount = 0;

	/** 미네랄을 1개 흡수 완료할 때마다 호출되는 델리게이트 이벤트입니다. */
	UPROPERTY(BlueprintAssignable, Category = "SpaceDrifter|Minerals")
	FOnSpaceMineralConsumedSignature OnMineralConsumed;

	// ============================================================================
	// 본체 성장 · 생체량(Biomass) · 게임오버 파라미터
	// ============================================================================

	/** 시작 시 플레이어 본체의 기준 생체량입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|GrowthAndBiomass", meta = (ClampMin = "10.0"))
	float InitialBiomass = 100.0f;

	/** 시작 시(BodyScale = 1.0) 플레이어 본체 구체 반지름(cm)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|GrowthAndBiomass", meta = (ClampMin = "10.0"))
	float InitialBodyRadiusCm = 75.0f;

	/** 이 반지름(cm) 이하로 줄어들면 게임오버가 발동합니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|GrowthAndBiomass", meta = (ClampMin = "5.0"))
	float MinGameOverBodyRadiusCm = 32.0f;

	/** 성장으로 도달 가능한 최대 본체 반지름(cm) 상한입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|GrowthAndBiomass", meta = (ClampMin = "50.0"))
	float MaxBodyRadiusCm = 400.0f;

	/** 생체량 비율에서 본체 반지름을 계산하는 성장 지수입니다(0.333=엄밀한 부피 비례, 0.40=체감 성장 강화). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|GrowthAndBiomass", meta = (ClampMin = "0.2", ClampMax = "0.8"))
	float BiomassToRadiusExponent = 0.40f;

	/** 미네랄 1개를 흡수할 때마다 증가하는 생체량입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|GrowthAndBiomass", meta = (ClampMin = "0.5"))
	float MineralBiomassGain = 18.0f;

	/** 게임오버 시 일정 시간 후 현재 레벨을 자동 재시작할지 여부입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|GrowthAndBiomass")
	bool bAutoRestartOnGameOver = false;

	/** 게임오버 후 자동 재시작까지 대기 시간(초)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|GrowthAndBiomass", meta = (ClampMin = "0.2", EditCondition = "bAutoRestartOnGameOver"))
	float GameOverRestartDelaySeconds = 2.5f;

	/** 현재 판의 진행 상태입니다(Playing / GameOver). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SpaceDrifter|Scenario")
	ESpaceRunState RunState = ESpaceRunState::Playing;

	/** 인게임 진입 후 누적 생존 시간(초)입니다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SpaceDrifter|Scenario")
	float ElapsedSurvivalSeconds = 0.0f;

	/** 이번 판에서 역포식으로 처치한 적군 곰팡이 수입니다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SpaceDrifter|Scenario")
	int32 DefeatedEnemyCount = 0;

	/** 클리어(승리) 조건이 되는 목표 포식자 처치 수입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Scenario", meta = (ClampMin = "1", ClampMax = "100"))
	int32 TargetVictoryEnemyCount = 10;

	/** V2 그물 촉수를 해금하기 위해 필요한 적 포식자 처치 수입니다(0이면 시작부터 해금). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Unlock", meta = (ClampMin = "0", ClampMax = "50"))
	int32 V2UnlockDefeatedEnemyCount = 2;

	/** V3 긴급 탈출 분사를 해금하기 위해 필요한 적 포식자 처치 수입니다(0이면 시작부터 해금). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Unlock", meta = (ClampMin = "0", ClampMax = "100"))
	int32 V3UnlockDefeatedEnemyCount = 5;

	/** 지정한 촉수 능력이 현재 해금 상태인지 반환합니다. */
	UFUNCTION(BlueprintPure, Category = "SpaceDrifter|Unlock")
	bool IsTendrilStyleUnlocked(ETendrilVisualStyle Style) const;

	/** 지정한 촉수 능력 해금에 필요한 포식자 처치 수를 반환합니다. */
	UFUNCTION(BlueprintPure, Category = "SpaceDrifter|Unlock")
	int32 GetRequiredDefeatsForStyle(ETendrilVisualStyle Style) const;

	/** 현재 보유 생체량(Biomass)입니다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SpaceDrifter|GrowthAndBiomass")
	float CurrentBiomass = 100.0f;

	/** 현재 본체 구체 반지름(cm)입니다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SpaceDrifter|GrowthAndBiomass")
	float CurrentBodyRadiusCm = 75.0f;

	/** 이번 플레이에서 도달한 최대 본체 반지름(cm) 기록입니다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SpaceDrifter|GrowthAndBiomass")
	float MaxReachedBodyRadiusCm = 75.0f;

	/** 현재 게임오버 상태인지 여부입니다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SpaceDrifter|GrowthAndBiomass")
	bool bIsGameOver = false;

	/** 현재 판이 종료되었는지 반환합니다. */
	UFUNCTION(BlueprintPure, Category = "SpaceDrifter|Scenario")
	bool IsRunEnded() const { return RunState != ESpaceRunState::Playing; }

	/** 적군 곰팡이를 역포식으로 처치했을 때 처치 수를 올립니다. */
	void NotifyEnemyDefeatedByPlayer();

	/** 현재 판을 종료하고 리더보드에 점수를 등록합니다. */
	void EndRun(ESpaceRunState NewState);

	/** 현재 레벨을 다시 열어 다음 도전을 시작합니다. */
	void RestartCurrentLevel();

	/** 게임오버 시 브로드캐스트되는 델리게이트입니다. */
	UPROPERTY(BlueprintAssignable, Category = "SpaceDrifter|GrowthAndBiomass")
	FOnSpaceDrifterGameOverSignature OnPlayerGameOver;

	/**
	 * 화면 좌상단 언리얼 기본 디버그 문자열(AddOnScreenDebugMessage 1001/901) 출력 여부입니다.
	 * 기본값은 숨김(false)이며 UMG HUD(USpaceDrifterHUDWidget)가 대신 표시합니다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|HUD")
	bool bShowOnScreenDebugMessages = false;

	/** 화면 정중앙 촉수 조준 표적 점(Reticle)을 표시할지 여부입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|HUD")
	bool bShowAimReticle = true;

	/** 조준 표적 점 아래에 포착된 소행성 거리(m) 텍스트를 표시할지 여부입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|HUD", meta = (EditCondition = "bShowAimReticle"))
	bool bShowReticleTargetDistance = true;

	/** 현재 촉수 유효 사거리 대비 조준 표적 점이 표적을 감지할 거리 배율입니다(1.0 = 촉수 사거리와 동일). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|HUD", meta = (ClampMin = "0.1", ClampMax = "20.0", EditCondition = "bShowAimReticle"))
	float ReticleDetectRangeMultiplier = 3.0f;

	/** 조준 표적 점이 거리를 측정해 표시할 최대 거리(cm) 상한입니다(0 이하이면 제한 없이 배율만 사용). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|HUD", meta = (ClampMin = "0.0", ClampMax = "100000.0", EditCondition = "bShowAimReticle"))
	float ReticleMaxMeasureDistanceCm = 12000.0f;

	/** 조준 표적 점 전용 스윕 반경(cm)입니다. 0 이하이면 TendrilAimTraceRadius를 그대로 사용합니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|HUD", meta = (ClampMin = "0.0", ClampMax = "200.0", EditCondition = "bShowAimReticle"))
	float ReticleTraceRadiusOverrideCm = 0.0f;

	/** 무중력 상태에서 메시가 위아래로 둥실거리며 천천히 회전하는 부유 연출을 켤지 여부입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Float")
	bool bEnableZeroGFloat = true;

	/** 무중력 부유 연출 시 시각 메시가 위아래로 흔들리는 진폭(cm)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Float", meta = (ClampMin = "0.0"))
	float FloatAmplitude = 10.0f;

	/** 무중력 부유 연출의 상하 왕복 주기 진동수(Hz)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Float", meta = (ClampMin = "0.0"))
	float FloatFrequency = 0.25f;

	/** 무중력 상태에서 시각 메시에 계속 적용되는 느린 회전 각속도(deg/s)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Float")
	FRotator IdleTumbleRate = FRotator(3.0f, 6.0f, 2.0f);

	/** 변형된 물풍선이 원래 둥근 형태로 되돌아가려는 스프링 복원 강성입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Elasticity", meta = (ClampMin = "1.0"))
	float SpringStiffness = 135.0f;

	/** 탄성 진동 감쇠 계수입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Elasticity", meta = (ClampMin = "0.1"))
	float SpringDamping = 4.8f;

	/** 감지된 액터의 월드 가속도(cm/s²)를 목표 신축 변형량으로 변환하는 배율입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Elasticity", meta = (ClampMin = "0.0"))
	float AccelerationDeformScale = 0.0016f;

	/** 자동 탄성 변형을 작동시키기 위해 필요한 최소 가속도 임계값(cm/s²)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Elasticity", meta = (ClampMin = "0.0"))
	float AccelerationDeadzone = 120.0f;

	/** 변형 축 방향으로 늘어날 수 있는 최대 양(+)의 변형 비율 상한입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Elasticity", meta = (ClampMin = "0.05", ClampMax = "0.85"))
	float MaxDeformAmount = 0.55f;

	/** 정지 상태에서도 숨 쉬듯 미세하게 부풀고 줄어드는 호흡 탄성 펄스를 켤지 여부입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Elasticity")
	bool bEnableIdleBreathing = true;

	/** 정지 상태 호흡 탄성 펄스의 신축 진폭 크기입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Elasticity", meta = (ClampMin = "0.0", ClampMax = "0.25"))
	float BreathAmplitude = 0.035f;

	/** 정지 상태 호흡 탄성 펄스의 진동수(Hz)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Elasticity", meta = (ClampMin = "0.0"))
	float BreathFrequency = 0.40f;

	/** TestBounceAction 입력 시 본체에 테스트로 가해지는 하향 스퀴시 충격 세기입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Elasticity", meta = (ClampMin = "0.0"))
	float TestBounceImpulse = 6.2f;

	/** 런타임에서 플레이어 루트 구체 콜리전의 Chaos 무중력 강체 물리 시뮬레이션을 활성화할지 여부입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Physics")
	bool bEnableChaosPhysics = true;

	/** 플레이어 곰팡이 셀의 물리 질량(kg)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Physics", meta = (ClampMin = "1.0", UIMin = "20.0", UIMax = "2000.0"))
	float PlayerMassKg = 350.0f;

	/** 무중력 공간에서 플레이어 이동 속도가 서서히 안정되는 선형 감쇠(Linear Damping)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Physics", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float PlayerLinearDamping = 0.01f;

	/** 소행성 등과 부딪혔을 때 물컹하게 튕겨 나가는 탄성 반발 계수(Restitution, 0~1)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Physics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PlayerRestitution = 0.84f;

	/** 플레이어 구체 표면 마찰 계수(Friction)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Physics", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float PlayerFriction = 0.25f;

	/** 물리 충돌 시 질량 정규화 상대 속도 변화(cm/s)를 물풍선 찌그러짐 충격량으로 변환하는 배율입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Physics", meta = (ClampMin = "0.0", ClampMax = "0.2"))
	float CollisionSquishScale = 0.012f;

	/** 미세 접촉 떨림을 무시하고 탄성 찌그러짐 연출을 발동할 최소 충돌 속도 변화 임계값(cm/s)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Physics", meta = (ClampMin = "0.0", ClampMax = "500.0"))
	float MinCollisionSquishDeltaSpeed = 35.0f;

	/** 강한 충돌에서도 구체가 과도하게 찌그러지지 않도록 제한하는 최대 충돌 스퀴시 충격 상한입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Physics", meta = (ClampMin = "0.5", ClampMax = "40.0"))
	float MaxCollisionSquishImpulse = 8.0f;

	/** 월드 공간 변형 축 벡터를 전달할 머티리얼 파라미터 이름입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Elasticity")
	FName DeformAxisParamName = TEXT("DeformAxis");

	/** 현재 스퀴시/스트레치 변형 수치를 전달할 머티리얼 스칼라 파라미터 이름입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Elasticity")
	FName DeformAmountParamName = TEXT("DeformAmount");

	/** 카메라 줌인 시 스프링암 최소 거리(cm)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Camera", meta = (ClampMin = "50.0"))
	float MinCameraDistance = 180.0f;

	/** 카메라 줌아웃 시 스프링암 최대 거리(cm)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Camera", meta = (ClampMin = "100.0"))
	float MaxCameraDistance = 1200.0f;

	/** 쿼터니언 궤도 카메라 회전 감도입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Camera", meta = (ClampMin = "0.05"))
	float OrbitLookSensitivity = 1.0f;

	/** 카메라 궤도 회전 보간 지연 속도입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Camera", meta = (ClampMin = "0.0"))
	float OrbitRotationLagSpeed = 12.0f;

	/** 이동 속력에 따라 스프링암 길이를 유동적으로 늘리고 줄여 고속 비행 시 폰이 화면 밖으로 사라지지 않게 합니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Camera")
	bool bEnableSpeedDynamicCameraArm = true;

	/** 최대 기준 속력에 도달했을 때 기본 스프링암 길이에 추가로 늘어나는 거리(cm)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Camera", meta = (ClampMin = "0.0", ClampMax = "1500.0", EditCondition = "bEnableSpeedDynamicCameraArm"))
	float SpeedCameraMaxExtensionCm = 280.0f;

	/** 스프링암 확장이 100%에 도달하는 기준 이동 속력(cm/s)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Camera", meta = (ClampMin = "200.0", ClampMax = "10000.0", EditCondition = "bEnableSpeedDynamicCameraArm"))
	float SpeedCameraReferenceSpeed = 2000.0f;

	/** 속력 변화에 맞춰 스프링암 길이가 늘어나고 줄어드는 보간 속도입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Camera", meta = (ClampMin = "0.5", ClampMax = "20.0", EditCondition = "bEnableSpeedDynamicCameraArm"))
	float SpeedCameraInterpSpeed = 4.5f;

	/** 정지 상태에서의 기본 카메라 위치 지연 속도입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Camera", meta = (ClampMin = "1.0", ClampMax = "40.0"))
	float BaseCameraLagSpeed = 6.0f;

	/** 고속 비행 시 카메라가 폰을 놓치지 않도록 올라가는 최대 위치 지연 속도입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Camera", meta = (ClampMin = "1.0", ClampMax = "60.0"))
	float MaxSpeedCameraLagSpeed = 18.0f;

	/** 카메라 위치 지연으로 폰과 벌어질 수 있는 최대 허용 거리(cm)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Camera", meta = (ClampMin = "0.0", ClampMax = "500.0"))
	float MaxCameraLagDistanceCm = 110.0f;

	/** 본체 반지름 배율(BodyScale)에 따라 스프링암 길이를 함께 늘리는 지수입니다(1.0 미만일수록 커질 때 화면상 묵직한 거대감이 살아납니다). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Camera", meta = (ClampMin = "0.0", ClampMax = "2.5"))
	float CameraArmBodyScaleExponent = 0.72f;

	/** 본체 반지름 배율(BodyScale)에 따라 카메라 수직/수평 소켓 오프셋(SocketOffset)을 함께 늘리는 지수입니다(1.0 = 크기가 커져도 크로스헤어와 본체 사이 상대 위치 비율 유지). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Camera", meta = (ClampMin = "0.0", ClampMax = "2.5"))
	float CameraSocketOffsetBodyScaleExponent = 1.0f;

	/** 타이틀 시작 화면 대기 중 카메라가 본체 주위를 천천히 자동 공전하는 각속도(deg/s)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceDrifter|Camera", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float TitleCameraOrbitSpeedDegPerSec = 10.0f;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> CollisionSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> DrifterMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> CameraPivot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCameraComponent> FollowCamera;

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	void Look(const FInputActionValue& Value);
	void OnTestBounce(const FInputActionValue& Value);
	void OnExtendTendrilStarted(const FInputActionValue& Value);
	void OnExtendTendrilEnded(const FInputActionValue& Value);
	void OnToggleTendrilStyle(const FInputActionValue& Value);
	void OnSelectTendrilV1();
	void OnSelectTendrilV2();
	void OnSelectTendrilV3();

public:
	// ~Begin ISpaceTendrilHostInterface
	virtual AActor* GetTendrilHostActor() const override { return const_cast<ASpaceDrifterPawn*>(this); }
	virtual UWorld* GetTendrilWorld() const override { return GetWorld(); }
	virtual FVector GetTendrilVisualOrigin() const override;
	virtual FVector GetTendrilBodyLocation() const override { return GetActorLocation(); }
	virtual float GetTendrilBodyRadius() const override;
	virtual float GetTendrilBodyScale() const override;
	virtual float GetTendrilBiomass() const override { return CurrentBiomass; }
	virtual float GetTendrilMinGameOverBiomass() const override;
	virtual bool CanSpendTendrilBiomass(float Amount, bool bAllowGameOver = false) const override;
	virtual bool SpendTendrilBiomass(float Amount, bool bTriggerGameOverOnDeplete = false) override;
	virtual void RefundTendrilBiomass(float Amount) override;
	virtual void PostTendrilStatusBanner(const FString& Message, const FLinearColor& Color, float DurationSeconds = 1.8f) override;
	virtual FVector GetTendrilBodyVelocity() const override;
	virtual void GetTendrilCameraBasis(
		FVector& OutCamLocation,
		FVector& OutCamForward,
		FVector& OutCamRight,
		FVector& OutCamUp
	) const override;
	virtual UPrimitiveComponent* GetTendrilPhysicsComponent() const override;
	virtual UStaticMeshComponent* GetTendrilBodyMeshComponent() const override { return DrifterMesh; }
	virtual UMaterialInterface* GetTendrilBodyMaterial() const override;
	virtual UFXSystemComponent* GetTendrilFXComponent(ETendrilVisualStyle Style) const override;
	virtual AAsteroidFieldManager* GetActiveAsteroidFieldManager() const override;
	virtual void ApplyHostDeformImpulse(const FVector& WorldImpulse, float StrengthScale = 1.0f) override;
	// ~End ISpaceTendrilHostInterface

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoLook(float Yaw, float Pitch);

	UFUNCTION(BlueprintCallable, Category = "Camera")
	virtual void DoZoom(float ZoomDelta);

	UFUNCTION(BlueprintCallable, Category = "SpaceDrifter|Tendril")
	virtual void SetTendrilExtending(bool bExtend);

	UFUNCTION(BlueprintCallable, Category = "SpaceDrifter|Tendril")
	virtual void SetTendrilVisualStyle(ETendrilVisualStyle NewStyle);

	UFUNCTION(BlueprintCallable, Category = "SpaceDrifter|Tendril")
	virtual void ToggleTendrilVisualStyle();

	UFUNCTION(BlueprintPure, Category = "SpaceDrifter|LongTendril")
	int32 ComputeLongTendrilBranchCount() const;

	UFUNCTION(BlueprintPure, Category = "SpaceDrifter|LongTendril")
	bool HasLatchedTarget() const;

	UFUNCTION(BlueprintPure, Category = "SpaceDrifter|LongTendril")
	AActor* GetLatchedTargetActor() const;

	UFUNCTION(BlueprintPure, Category = "SpaceDrifter|Tendril")
	bool CanFireTendril() const;

	UFUNCTION(BlueprintPure, Category = "SpaceDrifter|Tendril")
	float GetTendrilExtendAlpha() const;

	UFUNCTION(BlueprintPure, Category = "SpaceDrifter|Abilities")
	USpaceTendrilAbility* GetActiveTendrilAbility() const { return ActiveTendrilAbility.Get(); }

	/** 현재 V1 긴 촉수로 소행성 전염(흡수)에 투자 중인 생체량을 반환합니다. */
	UFUNCTION(BlueprintPure, Category = "SpaceDrifter|GrowthAndBiomass")
	float GetInvestedColonizeBiomass() const;

	/** HUD 조준 표적 점(Reticle)을 위해 현재 조준선 내 소행성 포착 여부, 거리(m), 그리고 V1 긴촉수 실제 도달 가능 여부를 평가합니다. */
	bool EvaluateAimReticleTarget(bool& OutbTargetDetected, float& OutDistanceMeters, bool& OutbWithinLongTendrilReach) const;

	UFUNCTION(BlueprintCallable, Category = "SpaceDrifter|Elasticity")
	virtual void ApplyDeformImpulse(const FVector& WorldImpulse, float StrengthScale = 1.0f);

	void NotifyMineralAbsorbed(ASpaceMineral* AbsorbedMineral, const FVector& ImpactWorldLocation);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	FORCEINLINE USphereComponent* GetCollisionSphere() const { return CollisionSphere; }
	FORCEINLINE UStaticMeshComponent* GetDrifterMesh() const { return DrifterMesh; }
	FORCEINLINE USceneComponent* GetCameraPivot() const { return CameraPivot; }
	FORCEINLINE USpringArmComponent* GetCameraBoom() const { return CameraBoom; }
	FORCEINLINE UCameraComponent* GetFollowCamera() const { return FollowCamera; }

private:
	UFUNCTION()
	void OnCollisionSphereHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	void InitializeTendrilAbilities();
	void SyncAbilitiesConfig();
	USpaceTendrilAbility* ResolveAbilityForStyle(ETendrilVisualStyle Style) const;
	void ApplyChaosPhysicsSettings();
	void UpdateCameraOrbit(float DeltaTime);
	void ResolveTendrilComponents();
	void SweepDirectContactMinerals();
	void UpdateElasticity(float DeltaTime);
	void PushDeformToMaterial();
	void UpdateBodyScaleFromBiomass();
	void TriggerGameOver();
	void OnGameOverRestartTimerExpired();

	UPROPERTY(Transient)
	TObjectPtr<USpaceTendrilAbility> ActiveTendrilAbility;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DrifterMID;

	UPROPERTY(Transient)
	TObjectPtr<UPhysicalMaterial> RuntimePhysicalMaterial;

	mutable TWeakObjectPtr<AAsteroidFieldManager> CachedFieldManager;

	FQuat TargetCameraOrbitQuat = FQuat::Identity;
	FQuat SmoothedCameraOrbitQuat = FQuat::Identity;
	float BaseCameraArmLength = 390.0f;
	FVector BaseCameraSocketOffset = FVector(0.0f, 0.0f, 78.0f);

	FVector InitialMeshRelativeLocation = FVector::ZeroVector;
	FVector InitialMeshRelativeScale = FVector::OneVector;
	FTimerHandle GameOverRestartTimerHandle;
	FVector DeformAxis = FVector::UpVector;
	float DeformValue = 0.0f;
	float DeformVelocity = 0.0f;

	bool bIsExtendInputHeld = false;
	FVector PreviousActorLocation = FVector::ZeroVector;
	FVector PreviousWorldVelocity = FVector::ZeroVector;
	bool bHasVelocityHistory = false;
	bool bSuppressAccelDeformThisTick = false;
	float ElapsedFloatTime = 0.0f;
};
