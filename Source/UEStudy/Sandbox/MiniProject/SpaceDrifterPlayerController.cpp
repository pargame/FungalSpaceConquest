#include "SpaceDrifterPlayerController.h"

#include "Blueprint/UserWidget.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "SpaceDrifterHUDWidget.h"
#include "SpaceDrifterLeaderboardSave.h"
#include "SpaceDrifterPawn.h"

void ASpaceDrifterPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (IsLocalPlayerController())
	{
		if (const USpaceDrifterLeaderboardSave* SaveObj = USpaceDrifterLeaderboardSave::LoadOrCreate())
		{
			CurrentPlayerName = SaveObj->LastPlayerName;
		}

		EnsureHUDCreated();

		if (bStartWithTitleScreen)
		{
			bIsInTitleScreen = true;
			bIsNameInputFocused = false;
			bSuppressTendrilUntilSpaceRelease = false;
			SetShowMouseCursor(true);
			FInputModeGameAndUI InputMode;
			InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			InputMode.SetHideCursorDuringCapture(false);
			SetInputMode(InputMode);
		}
		else
		{
			StartGameplayFromTitle();
		}
	}
}

void ASpaceDrifterPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (IsLocalPlayerController())
	{
		EnsureHUDCreated();
		if (HUDWidgetInstance)
		{
			HUDWidgetInstance->BindToPawnAndController(Cast<ASpaceDrifterPawn>(InPawn), this);
		}
	}
}

void ASpaceDrifterPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (InputComponent)
	{
		FInputKeyBinding& SpacePressBinding = InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &ASpaceDrifterPlayerController::OnTitleConfirmKeyPressed);
		SpacePressBinding.bConsumeInput = false;

		FInputKeyBinding& SpaceReleaseBinding = InputComponent->BindKey(EKeys::SpaceBar, IE_Released, this, &ASpaceDrifterPlayerController::OnTitleConfirmKeyReleased);
		SpaceReleaseBinding.bConsumeInput = false;
	}

	if (IsLocalPlayerController())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (int32 Index = 0; Index < DefaultMappingContexts.Num(); ++Index)
			{
				if (UInputMappingContext* MappingContext = DefaultMappingContexts[Index])
				{
					Subsystem->AddMappingContext(MappingContext, Index);
				}
			}
		}
	}
}

void ASpaceDrifterPlayerController::SetCurrentPlayerName(const FString& InName, bool bSaveToDisk)
{
	CurrentPlayerName = InName.TrimStartAndEnd().Left(12);
	if (bSaveToDisk)
	{
		if (USpaceDrifterLeaderboardSave* SaveObj = USpaceDrifterLeaderboardSave::LoadOrCreate())
		{
			SaveObj->SaveLastPlayerName(CurrentPlayerName);
		}
	}
}

FString ASpaceDrifterPlayerController::GetCurrentPlayerName() const
{
	const FString Trimmed = CurrentPlayerName.TrimStartAndEnd();
	return Trimmed.IsEmpty() ? TEXT("익명 곰팡이") : Trimmed;
}

void ASpaceDrifterPlayerController::NotifyRunEnded()
{
	if (const UWorld* World = GetWorld())
	{
		RunEndedWorldTime = World->GetTimeSeconds();
	}

	if (IsLocalPlayerController())
	{
		SetShowMouseCursor(true);
		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
		SetInputMode(InputMode);

		if (HUDWidgetInstance)
		{
			HUDWidgetInstance->RefreshLeaderboardDisplay();
		}
	}
}

void ASpaceDrifterPlayerController::StartGameplayFromTitle()
{
	bIsInTitleScreen = false;
	bIsNameInputFocused = false;

	if (IsLocalPlayerController())
	{
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().SetUserFocusToGameViewport(0);
		}
		SetShowMouseCursor(false);
		SetInputMode(FInputModeGameOnly());
		EnsureHUDCreated();
		if (HUDWidgetInstance)
		{
			HUDWidgetInstance->SetStatusBannerMessage(
				FString::Printf(TEXT("[%s] 포식자 10마리 타임어택 시작! 빠르게 성장해 역포식하세요!"), *GetCurrentPlayerName()),
				FLinearColor(0.38f, 0.96f, 0.80f, 1.0f),
				3.0f
			);
		}
	}
}

void ASpaceDrifterPlayerController::OnTitleConfirmKeyPressed()
{
	if (bIsInTitleScreen)
	{
		if (bIsNameInputFocused)
		{
			return;
		}
		bSuppressTendrilUntilSpaceRelease = true;
		StartGameplayFromTitle();
		return;
	}

	if (ASpaceDrifterPawn* DrifterPawn = Cast<ASpaceDrifterPawn>(GetPawn()))
	{
		if (DrifterPawn->IsRunEnded())
		{
			const UWorld* World = GetWorld();
			const double Now = World ? World->GetTimeSeconds() : 0.0;
			if (Now - RunEndedWorldTime < static_cast<double>(FMath::Max(EndRunRestartGraceSeconds, 0.0f)))
			{
				return;
			}
			DrifterPawn->RestartCurrentLevel();
		}
	}
}

void ASpaceDrifterPlayerController::OnTitleConfirmKeyReleased()
{
	bSuppressTendrilUntilSpaceRelease = false;
}

void ASpaceDrifterPlayerController::EnsureHUDCreated()
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	if (!HUDWidgetInstance)
	{
		UClass* WidgetCls = HUDWidgetClass ? HUDWidgetClass.Get() : USpaceDrifterHUDWidget::StaticClass();
		HUDWidgetInstance = CreateWidget<USpaceDrifterHUDWidget>(this, WidgetCls);
		if (HUDWidgetInstance)
		{
			HUDWidgetInstance->AddToViewport(10);
		}
	}

	if (HUDWidgetInstance)
	{
		HUDWidgetInstance->BindToPawnAndController(Cast<ASpaceDrifterPawn>(GetPawn()), this);
	}
}
