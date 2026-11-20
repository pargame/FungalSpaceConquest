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
		// 매 시작/재시작마다 이전 이름을 불러오지 않고 항상 빈칸으로 초기화
		CurrentPlayerName.Empty();

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
					const bool bIsHighPriority = !HighPriorityMappingContext.IsNull()
						&& FSoftObjectPath(MappingContext) == HighPriorityMappingContext.ToSoftObjectPath();
					const int32 Priority = bIsHighPriority ? 100 : Index;
					Subsystem->AddMappingContext(MappingContext, Priority);
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
	(void)bSaveToDisk;
}

bool ASpaceDrifterPlayerController::HasValidPlayerName() const
{
	return !CurrentPlayerName.TrimStartAndEnd().IsEmpty();
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

void ASpaceDrifterPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	// 매 프레임 실제 키 상태를 검사해 스페이스바가 떼어져 있으면 차단 플래그를 즉시 해제
	if (bSuppressTendrilUntilSpaceRelease && !IsInputKeyDown(EKeys::SpaceBar))
	{
		bSuppressTendrilUntilSpaceRelease = false;
	}
}

bool ASpaceDrifterPlayerController::IsSuppressingTendrilUntilSpaceRelease() const
{
	if (!bSuppressTendrilUntilSpaceRelease)
	{
		return false;
	}
	if (!IsInputKeyDown(EKeys::SpaceBar))
	{
		const_cast<ASpaceDrifterPlayerController*>(this)->bSuppressTendrilUntilSpaceRelease = false;
		return false;
	}
	return true;
}

void ASpaceDrifterPlayerController::StartGameplayFromTitle()
{
	EnsureHUDCreated();
	if (HUDWidgetInstance)
	{
		const FString LiveInputName = HUDWidgetInstance->GetSanitizedNameInputText();
		if (!LiveInputName.IsEmpty())
		{
			SetCurrentPlayerName(LiveInputName, false);
		}
	}

	if (bStartWithTitleScreen && !HasValidPlayerName())
	{
		if (HUDWidgetInstance)
		{
			HUDWidgetInstance->TriggerEmptyNameWarning();
		}
		return;
	}

	bIsInTitleScreen = false;
	bIsNameInputFocused = false;
	bIsPauseMenuOpen = false;
	bSuppressTendrilUntilSpaceRelease = false;

	if (IsLocalPlayerController())
	{
		// SEditableTextBox 및 Windows 한글 IME 포커스를 완전히 해제한 뒤 게임 뷰포트로 전환
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::SetDirectly);
		}
		FlushPressedKeys();
		SetShowMouseCursor(false);
		SetInputMode(FInputModeGameOnly());
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().SetAllUserFocusToGameViewport(EFocusCause::SetDirectly);
		}

		// SEditableTextBox::OnTextChanged 콜백이 완전히 끝난 다음 틱에도 포커스·입력 모드를 한 번 더 확정
		if (UWorld* World = GetWorld())
		{
			TWeakObjectPtr<ASpaceDrifterPlayerController> WeakThis(this);
			World->GetTimerManager().SetTimerForNextTick([WeakThis]()
			{
				if (ASpaceDrifterPlayerController* StrongThis = WeakThis.Get())
				{
					if (!StrongThis->bIsInTitleScreen && !StrongThis->bIsPauseMenuOpen && FSlateApplication::IsInitialized())
					{
						FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::SetDirectly);
						StrongThis->SetShowMouseCursor(false);
						StrongThis->SetInputMode(FInputModeGameOnly());
						FSlateApplication::Get().SetAllUserFocusToGameViewport(EFocusCause::SetDirectly);
					}
				}
			});
		}

		if (HUDWidgetInstance)
		{
			HUDWidgetInstance->SetStatusBannerMessage(
				FString::Printf(TEXT("[%s] 타임어택 시작! 목표: %s"), *GetCurrentPlayerName(),
					*USpaceDrifterGameRulesSettings::Get()->DescribeClearConditions()),
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
			return;
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
