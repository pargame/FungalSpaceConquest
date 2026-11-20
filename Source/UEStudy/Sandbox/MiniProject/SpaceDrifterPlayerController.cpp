#include "SpaceDrifterPlayerController.h"

#include "Blueprint/UserWidget.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Kismet/KismetSystemLibrary.h"
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

		FInputKeyBinding& EscapeBinding = InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ASpaceDrifterPlayerController::OnPauseMenuKeyPressed);
		EscapeBinding.bExecuteWhenPaused = true;
		EscapeBinding.bConsumeInput = true;
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
	FString Sanitized = InName;
	Sanitized.ReplaceInline(TEXT(" "), TEXT(""));
	Sanitized.ReplaceInline(TEXT("\t"), TEXT(""));
	CurrentPlayerName = Sanitized.TrimStartAndEnd().Left(12);
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
	if (bIsPauseMenuOpen)
	{
		bIsPauseMenuOpen = false;
		SetPause(false);
	}

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

void ASpaceDrifterPlayerController::TogglePauseMenu()
{
	SetPauseMenuOpen(!bIsPauseMenuOpen);
}

void ASpaceDrifterPlayerController::SetPauseMenuOpen(bool bOpen)
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	if (bOpen)
	{
		if (const ASpaceDrifterPawn* DrifterPawn = Cast<ASpaceDrifterPawn>(GetPawn()))
		{
			if (DrifterPawn->IsRunEnded())
			{
				return;
			}
		}
	}

	bIsPauseMenuOpen = bOpen;

	// 타이틀 화면에서는 라이브 3D 배경을 유지하고, 인게임 플레이 중일 때만 월드 일시정지
	if (!bIsInTitleScreen)
	{
		SetPause(bOpen);
	}

	if (bOpen)
	{
		SetShowMouseCursor(true);
		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
		SetInputMode(InputMode);

		if (HUDWidgetInstance)
		{
			HUDWidgetInstance->RefreshPauseMenuSettingsDisplay();
		}
	}
	else
	{
		if (bIsInTitleScreen)
		{
			SetShowMouseCursor(true);
			FInputModeGameAndUI InputMode;
			InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			InputMode.SetHideCursorDuringCapture(false);
			SetInputMode(InputMode);
		}
		else
		{
			if (FSlateApplication::IsInitialized())
			{
				FSlateApplication::Get().SetUserFocusToGameViewport(0);
			}
			SetShowMouseCursor(false);
			SetInputMode(FInputModeGameOnly());
		}
	}
}

void ASpaceDrifterPlayerController::RestartFromPauseMenu()
{
	bIsPauseMenuOpen = false;
	SetPause(false);

	if (ASpaceDrifterPawn* DrifterPawn = Cast<ASpaceDrifterPawn>(GetPawn()))
	{
		DrifterPawn->RestartCurrentLevel();
	}
}

void ASpaceDrifterPlayerController::QuitGameFromPauseMenu()
{
	bIsPauseMenuOpen = false;
	SetPause(false);
	UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
}

void ASpaceDrifterPlayerController::StartGameplayFromTitle()
{
	bIsInTitleScreen = false;
	bIsNameInputFocused = false;
	bIsPauseMenuOpen = false;
	bSuppressTendrilUntilSpaceRelease = true;

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
	if (bIsPauseMenuOpen)
	{
		return;
	}

	if (bIsInTitleScreen)
	{
		SetCurrentPlayerName(CurrentPlayerName, true);
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

void ASpaceDrifterPlayerController::OnPauseMenuKeyPressed()
{
	if (bIsInTitleScreen)
	{
		return;
	}
	if (const ASpaceDrifterPawn* DrifterPawn = Cast<ASpaceDrifterPawn>(GetPawn()))
	{
		if (DrifterPawn->IsRunEnded())
		{
			return;
		}
	}
	TogglePauseMenu();
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
