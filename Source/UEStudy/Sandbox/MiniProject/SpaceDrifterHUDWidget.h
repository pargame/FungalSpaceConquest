#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SpaceTendrilHostInterface.h"
#include "SpaceDrifterHUDWidget.generated.h"

class ASpaceDrifterPawn;
class ASpaceDrifterPlayerController;
class AEnemyMoldManager;
class SBorder;
class SBox;
class SEditableTextBox;
class SProgressBar;
class STextBlock;
class SVerticalBox;
class SWidget;

/**
 * [Fungal Space Conquest 통합 게임 UI]
 * 좌하단(생체량·반지름 코어), 우상단(속도·미네랄 수확), 하단 중앙(3종 촉수 슬롯 독),
 * 상단 중앙(상태 알림 배너), 화면 정중앙(촉수 사거리 연동 조준 표적 점)을 구역별로 배치한 게임 HUD입니다.
 */
UCLASS(BlueprintType, Blueprintable)
class UESTUDY_API USpaceDrifterHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 플레이어 폰 및 컨트롤러 참조를 UI에 연결합니다. */
	void BindToPawnAndController(ASpaceDrifterPawn* InPawn, ASpaceDrifterPlayerController* InPC);

	/** 인게임 상단 배너에 일시적인 상태 알림(전염 진행도, 폭발, 크기 부족 등)을 출력합니다. */
	void SetStatusBannerMessage(const FString& InMessage, const FLinearColor& InColor, float DurationSeconds = 1.8f);

	/** 로컬 SaveGame에서 최신 TOP 5 리더보드를 다시 읽어 타이틀/종료 화면을 갱신합니다. */
	void RefreshLeaderboardDisplay();

	/** 포식자 피격/흡수 시 화면 4면 테두리에 붉은 경고 플래시 효과를 발동합니다. */
	void TriggerDamageEdgeFlash(float Intensity = 1.0f);

	/** V2/V3 신규 촉수 해금 시 중앙 대형 팝업·4면 테두리 플래시·하단 슬롯 해금 글로우 연출을 발동합니다. */
	void TriggerAbilityUnlockEffect(ETendrilVisualStyle UnlockedStyle);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	EVisibility GetTitleOverlayVisibility() const;
	EVisibility GetInGameHUDVisibility() const;
	EVisibility GetGameOverOverlayVisibility() const;
	EVisibility GetBannerVisibility() const;
	EVisibility GetReticleTargetVisibility() const;

	void OnNameTextChanged(const FText& NewText);
	void OnNameTextCommitted(const FText& NewText, ETextCommit::Type CommitType);
	FReply OnResetLeaderboardClicked();
	void PopulateLeaderboardBox(const TSharedPtr<SVerticalBox>& TargetBox);

	void RefreshTitlePulse(float InDeltaTime);
	void RefreshDamageEdgeFlash(float InDeltaTime);
	void RefreshUnlockEffects(float InDeltaTime);
	void RefreshHUDTexts();
	void RefreshAimReticle();
	void RefreshEnemyIndicator(const FGeometry& MyGeometry);
	void UpdateTendrilSlotHighlight(ETendrilVisualStyle ActiveStyle);
	TSharedRef<SWidget> BuildTendrilSlotBox(const FString& KeyLabel, const FString& NameLabel, const FString& SubDescLabel, int32 SlotIndex);

	UPROPERTY(Transient)
	TWeakObjectPtr<ASpaceDrifterPawn> OwnerPawn;

	UPROPERTY(Transient)
	TWeakObjectPtr<ASpaceDrifterPlayerController> OwnerPC;

	// 타이틀 시작 프롬프트 & 이름 입력 & 리더보드
	TSharedPtr<SEditableTextBox> NameInputTextBox;
	TSharedPtr<STextBlock> NameConfirmHintTextBlock;
	TSharedPtr<SVerticalBox> TitleLeaderboardListBox;
	TSharedPtr<SBorder> TitleKeycapOuterBorder;
	TSharedPtr<SBorder> TitleKeycapInnerBorder;
	TSharedPtr<STextBlock> TitleKeycapTextBlock;
	TSharedPtr<STextBlock> TitlePromptMainTextBlock;
	TSharedPtr<STextBlock> TitlePromptSubTextBlock;

	// 좌상단: 5분 타이머 & 점수 카드
	TSharedPtr<STextBlock> MatchTimerTextBlock;
	TSharedPtr<STextBlock> MatchScoreTextBlock;
	TSharedPtr<STextBlock> MatchPlayerNameTextBlock;

	// 좌하단: 생체량 & 성장 코어 카드
	TSharedPtr<STextBlock> BiomassValueTextBlock;
	TSharedPtr<STextBlock> BiomassAbsTextBlock;
	TSharedPtr<STextBlock> BiomassMinTextBlock;
	TSharedPtr<SProgressBar> BiomassInvestedBar;
	TSharedPtr<SProgressBar> BiomassProgressBar;
	TSharedPtr<STextBlock> RadiusValueTextBlock;
	TSharedPtr<STextBlock> RadiusScaleTextBlock;
	TSharedPtr<STextBlock> MaxRadiusValueTextBlock;

	// 우상단: 속도계 & 미네랄 수확 카드
	TSharedPtr<STextBlock> SpeedValueTextBlock;
	TSharedPtr<STextBlock> SpeedSubTextBlock;
	TSharedPtr<SProgressBar> SpeedProgressBar;
	TSharedPtr<STextBlock> ReachRangeTextBlock;
	TSharedPtr<STextBlock> MineralValueTextBlock;

	// 상단 중앙: 상태 알림 배너
	TSharedPtr<SBorder> BannerContainerBorder;
	TSharedPtr<STextBlock> BannerTextBlock;

	// 정중앙: 촉수 조준 표적 점 (Reticle)
	TSharedPtr<SBox> ReticleDotBox;
	TSharedPtr<SBorder> ReticleDotBorder;
	TSharedPtr<SBorder> ReticleTickBorders[4];
	TSharedPtr<STextBlock> ReticleRangeTextBlock;

	// 적군 곰팡이 개별 마커 풀 (포식자마다 머리 위 / 화면 가장자리에 사냥가능·포식자 표시)
	static constexpr int32 MaxEnemyMarkers = 8;
	TSharedPtr<SOverlay> EnemyMarkersOverlay;
	TSharedPtr<SBorder> EnemyIndicatorBorders[MaxEnemyMarkers];
	TSharedPtr<STextBlock> EnemyIndicatorTextBlocks[MaxEnemyMarkers];
	TWeakObjectPtr<AEnemyMoldManager> CachedEnemyManager;

	// 피격 시 화면 4면 붉은 테두리 비네트 (상/하/좌/우 외곽+내곽 2중 레이어, 해금 플래시 공용)
	TSharedPtr<SBorder> DamageEdgeOuterBorders[4];
	TSharedPtr<SBorder> DamageEdgeInnerBorders[4];

	// 신규 촉수 해금 대형 중앙 팝업 카드
	TSharedPtr<SBorder> UnlockPopupOuterBorder;
	TSharedPtr<SBorder> UnlockPopupInnerBorder;
	TSharedPtr<STextBlock> UnlockPopupTagTextBlock;
	TSharedPtr<STextBlock> UnlockPopupTitleTextBlock;
	TSharedPtr<STextBlock> UnlockPopupDescTextBlock;

	// 하단 중앙: 촉수 슬롯 독
	TSharedPtr<SBorder> SlotAccentBorders[3];
	TSharedPtr<SBorder> SlotBorders[3];
	TSharedPtr<STextBlock> SlotKeyTexts[3];
	TSharedPtr<STextBlock> SlotTitleTexts[3];
	TSharedPtr<STextBlock> SlotDescTexts[3];

	// 종료(5분 완주 / 게임오버) 오버레이
	TSharedPtr<STextBlock> EndTitleTextBlock;
	TSharedPtr<STextBlock> EndSubTitleTextBlock;
	TSharedPtr<STextBlock> GameOverStatsTextBlock;
	TSharedPtr<SVerticalBox> EndLeaderboardListBox;

	FString CachedBannerMessage;
	FLinearColor CachedBannerColor = FLinearColor(0.45f, 0.96f, 0.80f, 1.0f);
	double BannerExpireTime = -1.0;
	float TitlePulseTime = 0.0f;
	float DamageFlashAlpha = 0.0f;
	float UnlockEdgeFlashAlpha = 0.0f;
	FLinearColor UnlockEdgeFlashColor = FLinearColor(0.20f, 0.94f, 1.0f, 1.0f);
	float UnlockPopupRemainingSeconds = 0.0f;
	float UnlockPopupTotalSeconds = 3.2f;
	FLinearColor UnlockPopupAccentColor = FLinearColor(0.20f, 0.94f, 1.0f, 1.0f);
	float SlotUnlockGlowTimers[3] = { 0.0f, 0.0f, 0.0f };
	bool bPendingInitialNameFocus = true;
	bool bReticleTargetInRange = false;
};
