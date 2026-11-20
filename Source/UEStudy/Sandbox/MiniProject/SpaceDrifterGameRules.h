#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SpaceDrifterGameRules.generated.h"

struct FSpaceLeaderboardEntry;

/** 클리어 조건에서 비교할 플레이 지표입니다. */
UENUM(BlueprintType)
enum class ESpaceClearMetric : uint8
{
	DefeatedEnemies   UMETA(DisplayName = "포식자 처치 수"),
	AbsorbedMinerals  UMETA(DisplayName = "흡수 미네랄 수"),
	MaxRadiusMeters   UMETA(DisplayName = "최대 반지름 (m)"),
	CurrentRadiusMeters UMETA(DisplayName = "현재 반지름 (m)"),
	ElapsedSeconds    UMETA(DisplayName = "경과 시간 (초)")
};

/** 지표와 임계값을 비교하는 연산자입니다. */
UENUM(BlueprintType)
enum class ESpaceCompareOp : uint8
{
	GreaterOrEqual UMETA(DisplayName = ">= (이상)"),
	Greater        UMETA(DisplayName = ">  (초과)"),
	LessOrEqual    UMETA(DisplayName = "<= (이하)"),
	Less           UMETA(DisplayName = "<  (미만)"),
	Equal          UMETA(DisplayName = "== (같음)")
};

/** 여러 조건(또는 그룹)을 묶는 논리 연산입니다. */
UENUM(BlueprintType)
enum class ESpaceConditionLogic : uint8
{
	All UMETA(DisplayName = "AND (모두 만족)"),
	Any UMETA(DisplayName = "OR (하나라도 만족)")
};

/** 단일 클리어 조건: [지표] [연산자] [임계값] */
USTRUCT(BlueprintType)
struct FSpaceClearCondition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clear")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clear", meta = (EditCondition = "bEnabled"))
	ESpaceClearMetric Metric = ESpaceClearMetric::DefeatedEnemies;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clear", meta = (EditCondition = "bEnabled"))
	ESpaceCompareOp Op = ESpaceCompareOp::GreaterOrEqual;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clear", meta = (EditCondition = "bEnabled"))
	float Threshold = 1.0f;
};

/** 조건 묶음. 그룹 내부는 InnerLogic으로, 그룹끼리는 설정의 GroupLogic으로 결합됩니다. 예) (A AND B) OR (C) */
USTRUCT(BlueprintType)
struct FSpaceClearConditionGroup
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clear")
	bool bEnabled = true;

	/** 그룹 내부 조건들을 묶는 방식입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clear", meta = (EditCondition = "bEnabled"))
	ESpaceConditionLogic InnerLogic = ESpaceConditionLogic::All;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clear", meta = (EditCondition = "bEnabled", TitleProperty = "Metric"))
	TArray<FSpaceClearCondition> Conditions;
};

/** 리더보드 정렬에 사용할 필드입니다. */
UENUM(BlueprintType)
enum class ESpaceLeaderboardSortField : uint8
{
	Cleared            UMETA(DisplayName = "클리어 여부"),
	ClearTime          UMETA(DisplayName = "클리어 시간 (클리어 기록끼리만 비교)"),
	ElapsedTime        UMETA(DisplayName = "플레이 시간 (전체 비교)"),
	DefeatedEnemies    UMETA(DisplayName = "포식자 처치 수"),
	AbsorbedMinerals   UMETA(DisplayName = "흡수 미네랄 수"),
	MaxRadius          UMETA(DisplayName = "최대 반지름"),
	RegistrationOrder  UMETA(DisplayName = "등록 순서")
};

/** 리더보드 정렬 키 1개. 배열 앞쪽 키가 우선이며, 같으면 다음 키로 넘어갑니다. */
USTRUCT(BlueprintType)
struct FSpaceLeaderboardSortKey
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leaderboard")
	ESpaceLeaderboardSortField Field = ESpaceLeaderboardSortField::DefeatedEnemies;

	/** true = 큰 값이 위(내림차순), false = 작은 값이 위(오름차순). 클리어 여부는 true면 클리어 기록이 위입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leaderboard")
	bool bDescending = true;
};

/** 클리어 판정에 쓰이는 현재 판의 지표 스냅샷입니다. */
struct FSpaceRunStats
{
	int32 DefeatedEnemies = 0;
	int32 AbsorbedMinerals = 0;
	float MaxRadiusMeters = 0.0f;
	float CurrentRadiusMeters = 0.0f;
	float ElapsedSeconds = 0.0f;
};

/** HUD 목표 패널에 표시할 한 줄입니다. */
struct FSpaceObjectiveLine
{
	/** true면 그룹/논리 머리줄("모두 달성" 등), false면 조건 줄 */
	bool bIsHeader = false;
	bool bMet = false;
	FString Label;
	FString Progress;
};

/**
 * Project Settings > Game > Fungal Space Rules 에서 편집하는 게임 규칙입니다.
 * 값은 Config/DefaultGame.ini 에 저장됩니다.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Fungal Space Rules"))
class UESTUDY_API USpaceDrifterGameRulesSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	USpaceDrifterGameRulesSettings();

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	// ---------------------------------------------------------------- 클리어 조건

	/** 그룹끼리 묶는 방식입니다. 그룹이 1개면 의미 없음. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "01. Clear Conditions")
	ESpaceConditionLogic GroupLogic = ESpaceConditionLogic::Any;

	/** 클리어 조건 그룹 목록. 활성 조건이 하나도 없으면 클리어되지 않습니다. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "01. Clear Conditions")
	TArray<FSpaceClearConditionGroup> ClearConditionGroups;

	// ---------------------------------------------------------------- 리더보드 정렬

	/** 위에서부터 우선 적용되는 정렬 키입니다. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "02. Leaderboard", meta = (TitleProperty = "Field"))
	TArray<FSpaceLeaderboardSortKey> LeaderboardSortKeys;

	/** 저장해 둘 최대 기록 수입니다. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "02. Leaderboard", meta = (ClampMin = "1", ClampMax = "200"))
	int32 MaxLeaderboardEntries = 20;

	// ---------------------------------------------------------------- 헬퍼

	static const USpaceDrifterGameRulesSettings* Get() { return GetDefault<USpaceDrifterGameRulesSettings>(); }

	/** 현재 지표가 클리어 조건을 만족하는지 판정합니다. */
	bool IsClearSatisfied(const FSpaceRunStats& Stats) const;

	/** 해당 지표에 대한 첫 번째 ">=" 류 목표값을 찾습니다(HUD의 "x / 목표" 표시용). 없으면 false. */
	bool FindGoalThreshold(ESpaceClearMetric Metric, float& OutThreshold) const;

	/** "포식자 처치 >= 6 AND 흡수 미네랄 >= 200" 같은 사람이 읽는 조건 설명입니다. */
	FString DescribeClearConditions() const;

	/** HUD 목표 패널용 줄 목록을 만듭니다(머리줄 + 조건별 진행도·달성 여부). */
	void BuildObjectiveLines(const FSpaceRunStats& Stats, TArray<FSpaceObjectiveLine>& OutLines) const;

	/** "정렬: 클리어 → 클리어 시간↑ → 처치↓" 같은 정렬 설명입니다. */
	FString DescribeLeaderboardSort() const;

	/** 리더보드 정렬 비교 함수(A가 B보다 위면 true). */
	bool LeaderboardLess(const FSpaceLeaderboardEntry& A, const FSpaceLeaderboardEntry& B) const;
};
