#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SpaceDrifterPlayerController.generated.h"

class UInputMappingContext;
class USpaceDrifterHUDWidget;

/**
 * PlayerController for SpaceDrifterPawn that applies Enhanced Input mapping contexts
 * and manages the Title Screen / In-Game HUD lifecycle.
 */
UCLASS(Blueprintable)
class UESTUDY_API ASpaceDrifterPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	/** 화면에 표시할 통합 게임 UI 위젯 클래스입니다(비워 두면 C++ 기본 USpaceDrifterHUDWidget 사용). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
	TSubclassOf<USpaceDrifterHUDWidget> HUDWidgetClass;

	/** 게임 시작 시 라이브 3D 우주 배경 위에 타이틀 시작 화면을 먼저 띄울지 여부입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
	bool bStartWithTitleScreen = true;

	/** 판 종료(승리/게임오버) 직후 SPACE 재시작 입력을 무시하는 유예 시간(초)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float EndRunRestartGraceSeconds = 2.0f;

	/** 현재 타이틀 시작 화면 대기 상태인지 반환합니다. */
	UFUNCTION(BlueprintPure, Category = "UI")
	bool IsInTitleScreen() const { return bIsInTitleScreen; }

	/** 타이틀 시작용 스페이스바를 떼기 전까지 촉수 발사 관통을 차단 중인지 반환합니다. */
	UFUNCTION(BlueprintPure, Category = "UI")
	bool IsSuppressingTendrilUntilSpaceRelease() const { return bSuppressTendrilUntilSpaceRelease; }

	/** 타이틀 시작 화면을 닫고 게임 입력 모드로 전환합니다. */
	UFUNCTION(BlueprintCallable, Category = "UI")
	void StartGameplayFromTitle();

	/** 타이틀에서 입력된 플레이어 이름을 갱신합니다(bSaveToDisk=true일 때만 SaveGame에 기록). */
	void SetCurrentPlayerName(const FString& InName, bool bSaveToDisk = false);

	/** 현재 플레이어 이름을 반환합니다(비어 있으면 '익명 곰팡이'). */
	FString GetCurrentPlayerName() const;

	/** 이름 입력창 포커스 여부를 갱신합니다(입력 중 SPACE 시작 차단용). */
	void SetNameInputFocused(bool bInFocused) { bIsNameInputFocused = bInFocused; }
	bool IsNameInputFocused() const { return bIsNameInputFocused; }

	/** 판 종료 시 UI 조작(초기화 버튼·SPACE 재시작) 모드로 전환합니다. */
	void NotifyRunEnded();

	/** 현재 ESC 일시정지 메뉴가 열려 있는지 반환합니다. */
	UFUNCTION(BlueprintPure, Category = "UI")
	bool IsPauseMenuOpen() const { return bIsPauseMenuOpen; }

	/** 인게임 플레이 중 ESC 일시정지 메뉴를 토글합니다. */
	UFUNCTION(BlueprintCallable, Category = "UI")
	void TogglePauseMenu();

	/** 일시정지 메뉴를 명시적으로 열거나 닫습니다. */
	UFUNCTION(BlueprintCallable, Category = "UI")
	void SetPauseMenuOpen(bool bOpen);

	/** 일시정지 메뉴에서 현재 레벨을 다시 시작합니다. */
	UFUNCTION(BlueprintCallable, Category = "UI")
	void RestartFromPauseMenu();

	/** 일시정지 메뉴에서 게임을 종료합니다. */
	UFUNCTION(BlueprintCallable, Category = "UI")
	void QuitGameFromPauseMenu();

	/** 현재 생성된 HUD 위젯 인스턴스를 반환합니다. */
	UFUNCTION(BlueprintPure, Category = "UI")
	USpaceDrifterHUDWidget* GetHUDWidget() const { return HUDWidgetInstance.Get(); }

protected:
	/** 로컬 플레이어에게 등록할 Enhanced Input 매핑 컨텍스트(IMC) 목록입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input|Input Mappings")
	TArray<TObjectPtr<UInputMappingContext>> DefaultMappingContexts;

	virtual void BeginPlay() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void SetupInputComponent() override;

private:
	void OnTitleConfirmKeyPressed();
	void OnTitleConfirmKeyReleased();
	void OnPauseMenuKeyPressed();
	void EnsureHUDCreated();

	UPROPERTY(Transient)
	TObjectPtr<USpaceDrifterHUDWidget> HUDWidgetInstance;

	FString CurrentPlayerName;
	bool bIsInTitleScreen = false;
	bool bIsNameInputFocused = false;
	bool bSuppressTendrilUntilSpaceRelease = false;
	bool bIsPauseMenuOpen = false;
	double RunEndedWorldTime = -100.0;
};
