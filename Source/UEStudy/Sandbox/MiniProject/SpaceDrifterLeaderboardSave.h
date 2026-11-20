#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "SpaceDrifterLeaderboardSave.generated.h"

USTRUCT(BlueprintType)
struct FSpaceLeaderboardEntry
{
	GENERATED_BODY()

	/** 리더보드에 표시할 플레이어 이름입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leaderboard")
	FString PlayerName = TEXT("익명 곰팡이");

	/** 해당 판에서 역포식으로 처치한 포식자 수입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leaderboard")
	int32 DefeatedEnemies = 0;

	/** 해당 판에서 도달한 최대 본체 반지름(m)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leaderboard")
	float MaxRadiusMeters = 0.75f;

	/** 해당 판에서 흡수한 총 미네랄 개수입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leaderboard")
	int32 AbsorbedMinerals = 0;

	/** 포식자 목표 수(10마리) 완주 성공 여부입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leaderboard")
	bool bClearedGoal = false;

	/** 완주 시 클리어 소요 시간(초, 미완주 시 생존 시간)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leaderboard")
	float ClearTimeSeconds = 0.0f;

	/** 동명이인 시간순 구분을 위한 등록 순서 번호입니다(등록 시점 순으로 1씩 증가). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leaderboard")
	int32 RegistrationOrder = 0;

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

	/** 다음 리더보드 등록 시 부여할 단조 증가 시간순 번호입니다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Leaderboard")
	int32 NextRegistrationOrder = 1;

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
		FSpaceLeaderboardEntry EntryToAdd = NewEntry;
		int32 MaxExistingOrder = 0;
		for (const FSpaceLeaderboardEntry& Existing : Entries)
		{
			MaxExistingOrder = FMath::Max(MaxExistingOrder, Existing.RegistrationOrder);
		}
		const int32 AssignedOrder = FMath::Max(NextRegistrationOrder, MaxExistingOrder + 1);
		EntryToAdd.RegistrationOrder = AssignedOrder;
		NextRegistrationOrder = AssignedOrder + 1;

		Entries.Add(EntryToAdd);
		SortEntries();
		if (Entries.Num() > 20)
		{
			Entries.SetNum(20);
		}
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
		NextRegistrationOrder = 1;
		UGameplayStatics::SaveGameToSlot(this, SaveSlotName, SaveUserIndex);
	}
};
