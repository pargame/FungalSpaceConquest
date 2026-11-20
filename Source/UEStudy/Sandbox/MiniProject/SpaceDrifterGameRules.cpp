#include "SpaceDrifterGameRules.h"

#include "SpaceDrifterLeaderboardSave.h"

namespace SpaceDrifterRules
{
	static float GetMetricValue(const FSpaceRunStats& Stats, ESpaceClearMetric Metric)
	{
		switch (Metric)
		{
		case ESpaceClearMetric::DefeatedEnemies:     return static_cast<float>(Stats.DefeatedEnemies);
		case ESpaceClearMetric::AbsorbedMinerals:    return static_cast<float>(Stats.AbsorbedMinerals);
		case ESpaceClearMetric::MaxRadiusMeters:     return Stats.MaxRadiusMeters;
		case ESpaceClearMetric::CurrentRadiusMeters: return Stats.CurrentRadiusMeters;
		case ESpaceClearMetric::ElapsedSeconds:      return Stats.ElapsedSeconds;
		default:                                     return 0.0f;
		}
	}

	static bool Compare(float Value, ESpaceCompareOp Op, float Threshold)
	{
		switch (Op)
		{
		case ESpaceCompareOp::GreaterOrEqual: return Value >= Threshold - KINDA_SMALL_NUMBER;
		case ESpaceCompareOp::Greater:        return Value > Threshold;
		case ESpaceCompareOp::LessOrEqual:    return Value <= Threshold + KINDA_SMALL_NUMBER;
		case ESpaceCompareOp::Less:           return Value < Threshold;
		case ESpaceCompareOp::Equal:          return FMath::IsNearlyEqual(Value, Threshold, 0.001f);
		default:                              return false;
		}
	}

	static const TCHAR* MetricLabel(ESpaceClearMetric Metric)
	{
		switch (Metric)
		{
		case ESpaceClearMetric::DefeatedEnemies:     return TEXT("포식자 처치");
		case ESpaceClearMetric::AbsorbedMinerals:    return TEXT("미네랄");
		case ESpaceClearMetric::MaxRadiusMeters:     return TEXT("최대 반지름");
		case ESpaceClearMetric::CurrentRadiusMeters: return TEXT("현재 반지름");
		case ESpaceClearMetric::ElapsedSeconds:      return TEXT("시간");
		default:                                     return TEXT("?");
		}
	}

	static const TCHAR* OpLabel(ESpaceCompareOp Op)
	{
		switch (Op)
		{
		case ESpaceCompareOp::GreaterOrEqual: return TEXT("이상");
		case ESpaceCompareOp::Greater:        return TEXT("초과");
		case ESpaceCompareOp::LessOrEqual:    return TEXT("이하");
		case ESpaceCompareOp::Less:           return TEXT("미만");
		case ESpaceCompareOp::Equal:          return TEXT("");
		default:                              return TEXT("");
		}
	}

	static FString FormatThreshold(ESpaceClearMetric Metric, float Threshold)
	{
		switch (Metric)
		{
		case ESpaceClearMetric::DefeatedEnemies:     return FString::Printf(TEXT("%d마리"), FMath::RoundToInt(Threshold));
		case ESpaceClearMetric::AbsorbedMinerals:    return FString::Printf(TEXT("%d개"), FMath::RoundToInt(Threshold));
		case ESpaceClearMetric::MaxRadiusMeters:
		case ESpaceClearMetric::CurrentRadiusMeters: return FString::Printf(TEXT("%.2fm"), Threshold);
		case ESpaceClearMetric::ElapsedSeconds:      return FString::Printf(TEXT("%.0f초"), Threshold);
		default:                                     return FString::SanitizeFloat(Threshold);
		}
	}

	static bool EvaluateGroup(const FSpaceClearConditionGroup& Group, const FSpaceRunStats& Stats, bool& bOutHasAnyCondition)
	{
		bOutHasAnyCondition = false;
		const bool bAll = (Group.InnerLogic == ESpaceConditionLogic::All);
		bool bResult = bAll;
		for (const FSpaceClearCondition& Cond : Group.Conditions)
		{
			if (!Cond.bEnabled)
			{
				continue;
			}
			bOutHasAnyCondition = true;
			const bool bPass = Compare(GetMetricValue(Stats, Cond.Metric), Cond.Op, Cond.Threshold);
			bResult = bAll ? (bResult && bPass) : (bResult || bPass);
		}
		return bOutHasAnyCondition && bResult;
	}
}

USpaceDrifterGameRulesSettings::USpaceDrifterGameRulesSettings()
{
	// 기본 클리어 조건: 포식자 6마리 처치 AND 미네랄 200개 이상
	FSpaceClearConditionGroup DefaultGroup;
	DefaultGroup.InnerLogic = ESpaceConditionLogic::All;

	FSpaceClearCondition KillCond;
	KillCond.Metric = ESpaceClearMetric::DefeatedEnemies;
	KillCond.Op = ESpaceCompareOp::GreaterOrEqual;
	KillCond.Threshold = 6.0f;
	DefaultGroup.Conditions.Add(KillCond);

	FSpaceClearCondition MineralCond;
	MineralCond.Metric = ESpaceClearMetric::AbsorbedMinerals;
	MineralCond.Op = ESpaceCompareOp::GreaterOrEqual;
	MineralCond.Threshold = 200.0f;
	DefaultGroup.Conditions.Add(MineralCond);

	ClearConditionGroups.Add(DefaultGroup);

	// 기본 리더보드 정렬: 클리어 우선 → 클리어 시간 짧은 순 → 처치 많은 순 → 미네랄 많은 순 → 반지름 큰 순
	auto AddKey = [this](ESpaceLeaderboardSortField Field, bool bDesc)
	{
		FSpaceLeaderboardSortKey Key;
		Key.Field = Field;
		Key.bDescending = bDesc;
		LeaderboardSortKeys.Add(Key);
	};
	AddKey(ESpaceLeaderboardSortField::Cleared, true);
	AddKey(ESpaceLeaderboardSortField::ClearTime, false);
	AddKey(ESpaceLeaderboardSortField::DefeatedEnemies, true);
	AddKey(ESpaceLeaderboardSortField::AbsorbedMinerals, true);
	AddKey(ESpaceLeaderboardSortField::MaxRadius, true);
}

bool USpaceDrifterGameRulesSettings::IsClearSatisfied(const FSpaceRunStats& Stats) const
{
	const bool bAll = (GroupLogic == ESpaceConditionLogic::All);
	bool bResult = bAll;
	bool bAnyGroup = false;

	for (const FSpaceClearConditionGroup& Group : ClearConditionGroups)
	{
		if (!Group.bEnabled)
		{
			continue;
		}
		bool bGroupHasCondition = false;
		const bool bPass = SpaceDrifterRules::EvaluateGroup(Group, Stats, bGroupHasCondition);
		if (!bGroupHasCondition)
		{
			continue; // 비어 있는 그룹은 무시
		}
		bAnyGroup = true;
		bResult = bAll ? (bResult && bPass) : (bResult || bPass);
	}
	return bAnyGroup && bResult;
}

bool USpaceDrifterGameRulesSettings::FindGoalThreshold(ESpaceClearMetric Metric, float& OutThreshold) const
{
	for (const FSpaceClearConditionGroup& Group : ClearConditionGroups)
	{
		if (!Group.bEnabled)
		{
			continue;
		}
		for (const FSpaceClearCondition& Cond : Group.Conditions)
		{
			if (Cond.bEnabled && Cond.Metric == Metric
				&& (Cond.Op == ESpaceCompareOp::GreaterOrEqual || Cond.Op == ESpaceCompareOp::Greater || Cond.Op == ESpaceCompareOp::Equal))
			{
				OutThreshold = Cond.Threshold;
				return true;
			}
		}
	}
	return false;
}

FString USpaceDrifterGameRulesSettings::DescribeClearConditions() const
{
	TArray<FString> GroupTexts;
	for (const FSpaceClearConditionGroup& Group : ClearConditionGroups)
	{
		if (!Group.bEnabled)
		{
			continue;
		}
		TArray<FString> CondTexts;
		for (const FSpaceClearCondition& Cond : Group.Conditions)
		{
			if (!Cond.bEnabled)
			{
				continue;
			}
			FString Text = FString::Printf(TEXT("%s %s"),
				SpaceDrifterRules::MetricLabel(Cond.Metric),
				*SpaceDrifterRules::FormatThreshold(Cond.Metric, Cond.Threshold));
			const TCHAR* OpText = SpaceDrifterRules::OpLabel(Cond.Op);
			if (*OpText)
			{
				Text += TEXT(" ");
				Text += OpText;
			}
			CondTexts.Add(MoveTemp(Text));
		}
		if (CondTexts.IsEmpty())
		{
			continue;
		}
		const TCHAR* Sep = (Group.InnerLogic == ESpaceConditionLogic::All) ? TEXT(" + ") : TEXT(" 또는 ");
		FString Joined = FString::Join(CondTexts, Sep);
		GroupTexts.Add(CondTexts.Num() > 1 ? FString::Printf(TEXT("(%s)"), *Joined) : Joined);
	}

	if (GroupTexts.IsEmpty())
	{
		return TEXT("클리어 조건 없음");
	}
	if (GroupTexts.Num() == 1)
	{
		FString Only = GroupTexts[0];
		Only.RemoveFromStart(TEXT("("));
		Only.RemoveFromEnd(TEXT(")"));
		return Only;
	}
	const TCHAR* Sep = (GroupLogic == ESpaceConditionLogic::All) ? TEXT(" + ") : TEXT(" 또는 ");
	return FString::Join(GroupTexts, Sep);
}

void USpaceDrifterGameRulesSettings::BuildObjectiveLines(const FSpaceRunStats& Stats, TArray<FSpaceObjectiveLine>& OutLines) const
{
	using namespace SpaceDrifterRules;
	OutLines.Reset();

	auto LogicText = [](ESpaceConditionLogic Logic)
	{
		return Logic == ESpaceConditionLogic::All ? TEXT("모두 달성 (AND)") : TEXT("하나만 달성 (OR)");
	};

	auto FormatValue = [](ESpaceClearMetric Metric, float Value) -> FString
	{
		switch (Metric)
		{
		case ESpaceClearMetric::DefeatedEnemies:
		case ESpaceClearMetric::AbsorbedMinerals:    return FString::Printf(TEXT("%d"), FMath::FloorToInt(Value));
		case ESpaceClearMetric::MaxRadiusMeters:
		case ESpaceClearMetric::CurrentRadiusMeters: return FString::Printf(TEXT("%.2f"), Value);
		case ESpaceClearMetric::ElapsedSeconds:      return FString::Printf(TEXT("%.0f"), Value);
		default:                                     return FString::SanitizeFloat(Value);
		}
	};

	// 활성 그룹 수집
	TArray<const FSpaceClearConditionGroup*> ActiveGroups;
	for (const FSpaceClearConditionGroup& Group : ClearConditionGroups)
	{
		if (!Group.bEnabled)
		{
			continue;
		}
		for (const FSpaceClearCondition& Cond : Group.Conditions)
		{
			if (Cond.bEnabled)
			{
				ActiveGroups.Add(&Group);
				break;
			}
		}
	}

	if (ActiveGroups.IsEmpty())
	{
		FSpaceObjectiveLine Line;
		Line.bIsHeader = true;
		Line.Label = TEXT("클리어 조건 없음");
		OutLines.Add(Line);
		return;
	}

	const bool bMultiGroup = ActiveGroups.Num() > 1;
	if (bMultiGroup)
	{
		FSpaceObjectiveLine Top;
		Top.bIsHeader = true;
		Top.Label = FString::Printf(TEXT("그룹 중 %s"), LogicText(GroupLogic));
		Top.bMet = IsClearSatisfied(Stats);
		OutLines.Add(Top);
	}

	for (int32 GroupIdx = 0; GroupIdx < ActiveGroups.Num(); ++GroupIdx)
	{
		const FSpaceClearConditionGroup& Group = *ActiveGroups[GroupIdx];
		bool bDummy = false;

		int32 EnabledCount = 0;
		for (const FSpaceClearCondition& Cond : Group.Conditions)
		{
			EnabledCount += Cond.bEnabled ? 1 : 0;
		}

		if (bMultiGroup || EnabledCount > 1)
		{
			FSpaceObjectiveLine Header;
			Header.bIsHeader = true;
			Header.bMet = EvaluateGroup(Group, Stats, bDummy);
			Header.Label = bMultiGroup
				? (EnabledCount > 1
					? FString::Printf(TEXT("그룹 %d · %s"), GroupIdx + 1, LogicText(Group.InnerLogic))
					: FString::Printf(TEXT("그룹 %d"), GroupIdx + 1))
				: FString(LogicText(Group.InnerLogic));
			OutLines.Add(Header);
		}

		for (const FSpaceClearCondition& Cond : Group.Conditions)
		{
			if (!Cond.bEnabled)
			{
				continue;
			}
			const float Value = GetMetricValue(Stats, Cond.Metric);
			FSpaceObjectiveLine Line;
			Line.bMet = Compare(Value, Cond.Op, Cond.Threshold);
			Line.Label = MetricLabel(Cond.Metric);
			// "3 / 6마리" 처럼 '이상'은 생략해 좁은 카드에서도 잘리지 않게 함
			const TCHAR* OpText = (Cond.Op == ESpaceCompareOp::GreaterOrEqual) ? TEXT("") : OpLabel(Cond.Op);
			Line.Progress = FString::Printf(TEXT("%s / %s%s%s"),
				*FormatValue(Cond.Metric, Value),
				*FormatThreshold(Cond.Metric, Cond.Threshold),
				*OpText ? TEXT(" ") : TEXT(""),
				OpText);
			OutLines.Add(MoveTemp(Line));
		}
	}
}

FString USpaceDrifterGameRulesSettings::DescribeLeaderboardSort() const
{
	TArray<FString> Parts;
	for (const FSpaceLeaderboardSortKey& Key : LeaderboardSortKeys)
	{
		const TCHAR* Arrow = Key.bDescending ? TEXT("↓") : TEXT("↑");
		switch (Key.Field)
		{
		case ESpaceLeaderboardSortField::Cleared:           Parts.Add(Key.bDescending ? TEXT("클리어 우선") : TEXT("미클리어 우선")); break;
		case ESpaceLeaderboardSortField::ClearTime:         Parts.Add(FString::Printf(TEXT("클리어 시간%s"), Arrow)); break;
		case ESpaceLeaderboardSortField::ElapsedTime:       Parts.Add(FString::Printf(TEXT("플레이 시간%s"), Arrow)); break;
		case ESpaceLeaderboardSortField::DefeatedEnemies:   Parts.Add(FString::Printf(TEXT("처치%s"), Arrow)); break;
		case ESpaceLeaderboardSortField::AbsorbedMinerals:  Parts.Add(FString::Printf(TEXT("미네랄%s"), Arrow)); break;
		case ESpaceLeaderboardSortField::MaxRadius:         Parts.Add(FString::Printf(TEXT("반지름%s"), Arrow)); break;
		case ESpaceLeaderboardSortField::RegistrationOrder: Parts.Add(Key.bDescending ? TEXT("최신순") : TEXT("오래된순")); break;
		default: break;
		}
	}
	return Parts.IsEmpty() ? FString(TEXT("등록순")) : FString::Join(Parts, TEXT(" → "));
}

bool USpaceDrifterGameRulesSettings::LeaderboardLess(const FSpaceLeaderboardEntry& A, const FSpaceLeaderboardEntry& B) const
{
	for (const FSpaceLeaderboardSortKey& Key : LeaderboardSortKeys)
	{
		double VA = 0.0;
		double VB = 0.0;
		double Tolerance = 0.0;

		switch (Key.Field)
		{
		case ESpaceLeaderboardSortField::Cleared:
			VA = A.bClearedGoal ? 1.0 : 0.0;
			VB = B.bClearedGoal ? 1.0 : 0.0;
			break;
		case ESpaceLeaderboardSortField::ClearTime:
			if (!(A.bClearedGoal && B.bClearedGoal))
			{
				continue; // 클리어 기록끼리만 시간 비교
			}
			VA = A.ClearTimeSeconds;
			VB = B.ClearTimeSeconds;
			Tolerance = 0.005;
			break;
		case ESpaceLeaderboardSortField::ElapsedTime:
			VA = A.ClearTimeSeconds;
			VB = B.ClearTimeSeconds;
			Tolerance = 0.005;
			break;
		case ESpaceLeaderboardSortField::DefeatedEnemies:
			VA = A.DefeatedEnemies;
			VB = B.DefeatedEnemies;
			break;
		case ESpaceLeaderboardSortField::AbsorbedMinerals:
			VA = A.AbsorbedMinerals;
			VB = B.AbsorbedMinerals;
			break;
		case ESpaceLeaderboardSortField::MaxRadius:
			VA = A.MaxRadiusMeters;
			VB = B.MaxRadiusMeters;
			Tolerance = 0.0005;
			break;
		case ESpaceLeaderboardSortField::RegistrationOrder:
			VA = A.RegistrationOrder;
			VB = B.RegistrationOrder;
			break;
		default:
			continue;
		}

		if (FMath::Abs(VA - VB) <= Tolerance)
		{
			continue;
		}
		return Key.bDescending ? (VA > VB) : (VA < VB);
	}
	// 모든 키가 같으면 먼저 등록된 기록이 위
	return A.RegistrationOrder < B.RegistrationOrder;
}
