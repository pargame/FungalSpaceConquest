#include "SpaceDrifterGameMode.h"

#include "SpaceDrifterPawn.h"
#include "SpaceDrifterPlayerController.h"

ASpaceDrifterGameMode::ASpaceDrifterGameMode()
{
	DefaultPawnClass = ASpaceDrifterPawn::StaticClass();
	PlayerControllerClass = ASpaceDrifterPlayerController::StaticClass();
}
