#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "SpaceDrifterLeaderboardSave.generated.h"

USTRUCT(BlueprintType)
struct FSpaceLeaderboardEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leaderboard")
	FString PlayerName = TEXT("익명 곰팡이");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leaderboard")
	int32 DefeatedEnemies = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leaderboard")
	float MaxRadiusMeters = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leaderboard")
	int32 AbsorbedMinerals = 0;

	/** 포식자 목표 수(10마리) 완주 성공 여부입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leaderboard")
	bool bClearedGoal = false;

	/** 완주 시 클리어 소요 시간(초, 미완주 시 생존 시간)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leaderboard")
	float ClearTimeSeconds = 0.0f;

	// 레거시 SaveGame 호환용 필드
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leaderboard")
	bool bSurvivedFullTime = false;
};

/**
 * 곰팡이 우주 정복 대작전(Fungal Space Conquest) 로컬 리더보드 영구 저장용 SaveGame입니다.
 */
UCLASS(BlueprintType)
class UESTUDY_API USpaceDrifterLeaderboardSave : public USaveGame
{
	GENERATED_BODY()

public:
	static constexpr const TCHAR* SaveSlotName = TEXT("FungalSpaceLeaderboard");
	static constexpr int32 SaveUserIndex = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Leaderboard")
	TArray<FSpaceLeaderboardEntry> Entries;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Leaderboard")
	FString LastPlayerName;

	static USpaceDrifterLeaderboardSave* LoadOrCreate()
	{
		if (UGameplayStatics::DoesSaveGameExist(SaveSlotName, SaveUserIndex))
		{
			if (USpaceDrifterLeaderboardSave* Loaded = Cast<USpaceDrifterLeaderboardSave>(UGameplayStatics::LoadGameFromSlot(SaveSlotName, SaveUserIndex)))
			{
				Loaded->SortEntries();
				return Loaded;
			}
		}
		return Cast<USpaceDrifterLeaderboardSave>(UGameplayStatics::CreateSaveGameObject(USpaceDrifterLeaderboardSave::StaticClass()));
	}

	void SortEntries()
	{
		Entries.Sort([](const FSpaceLeaderboardEntry& A, const FSpaceLeaderboardEntry& B)
		{
			if (A.bClearedGoal != B.bClearedGoal)
			{
				return A.bClearedGoal;
			}
			if (A.bClearedGoal && B.bClearedGoal)
			{
				if (!FMath::IsNearlyEqual(A.ClearTimeSeconds, B.ClearTimeSeconds, 0.005f))
				{
					return A.ClearTimeSeconds < B.ClearTimeSeconds;
				}
			}
			if (A.DefeatedEnemies != B.DefeatedEnemies)
			{
				return A.DefeatedEnemies > B.DefeatedEnemies;
			}
			if (A.AbsorbedMinerals != B.AbsorbedMinerals)
			{
				return A.AbsorbedMinerals > B.AbsorbedMinerals;
			}
			return A.MaxRadiusMeters > B.MaxRadiusMeters;
		});
	}

	void AddScoreAndSave(const FSpaceLeaderboardEntry& NewEntry)
	{
		Entries.Add(NewEntry);
		SortEntries();
		if (Entries.Num() > 20)
		{
			Entries.SetNum(20);
		}
		LastPlayerName = NewEntry.PlayerName;
		UGameplayStatics::SaveGameToSlot(this, SaveSlotName, SaveUserIndex);
	}

	void SaveLastPlayerName(const FString& InName)
	{
		LastPlayerName = InName;
		UGameplayStatics::SaveGameToSlot(this, SaveSlotName, SaveUserIndex);
	}

	void ClearAllAndSave()
	{
		Entries.Reset();
		UGameplayStatics::SaveGameToSlot(this, SaveSlotName, SaveUserIndex);
	}
};
