#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SpaceDrifterGameMode.generated.h"

/**
 * Default GameMode for the MiniProject persistent space level.
 */
UCLASS(Blueprintable)
class UESTUDY_API ASpaceDrifterGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASpaceDrifterGameMode();
};
