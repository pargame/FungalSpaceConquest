#include "SpaceDrifterHUDWidget.h"

#include "EnemyMoldManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Text/TextLayout.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/GameplayStatics.h"
#include "SpaceDrifterLeaderboardSave.h"
#include "SpaceDrifterPawn.h"
#include "SpaceDrifterPlayerController.h"
#include "SpaceTendrilAbility.h"
#include "Styling/CoreStyle.h"
#include "Styling/StyleDefaults.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

void USpaceDrifterHUDWidget::BindToPawnAndController(ASpaceDrifterPawn* InPawn, ASpaceDrifterPlayerController* InPC)
{
	OwnerPawn = InPawn;
	OwnerPC = InPC;
	RefreshLeaderboardDisplay();
	RefreshPauseMenuSettingsDisplay();
	RefreshHUDTexts();
	RefreshAimReticle();
}

void USpaceDrifterHUDWidget::SetStatusBannerMessage(const FString& InMessage, const FLinearColor& InColor, float DurationSeconds)
{
	CachedBannerMessage = InMessage;
	CachedBannerColor = InColor;

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	BannerExpireTime = Now + static_cast<double>(FMath::Max(DurationSeconds, 0.05f));

	if (BannerTextBlock.IsValid())
	{
		BannerTextBlock->SetText(FText::FromString(CachedBannerMessage));
		BannerTextBlock->SetColorAndOpacity(FSlateColor(CachedBannerColor));
	}
}

void USpaceDrifterHUDWidget::TriggerDamageEdgeFlash(float Intensity)
{
	DamageFlashAlpha = FMath::Max(DamageFlashAlpha, FMath::Clamp(Intensity, 0.0f, 1.0f));
}

TSharedRef<SWidget> USpaceDrifterHUDWidget::RebuildWidget()
{
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("WhiteBrush");

	const FSlateFontInfo HeroTagFont = FCoreStyle::GetDefaultFontStyle("Bold", 11);
	const FSlateFontInfo HeroTitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 34);
	const FSlateFontInfo HeroSubTitleFont = FCoreStyle::GetDefaultFontStyle("Regular", 15);
	const FSlateFontInfo SectionTitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 13);
	const FSlateFontInfo BodyFont = FCoreStyle::GetDefaultFontStyle("Regular", 12);
	const FSlateFontInfo TitleKeycapFont = FCoreStyle::GetDefaultFontStyle("Bold", 22);
	const FSlateFontInfo TitlePromptMainFont = FCoreStyle::GetDefaultFontStyle("Bold", 23);
	const FSlateFontInfo TitlePromptSubFont = FCoreStyle::GetDefaultFontStyle("Regular", 13);

	const FSlateFontInfo MicroHeaderFont = FCoreStyle::GetDefaultFontStyle("Bold", 9);
	const FSlateFontInfo BigNumFont = FCoreStyle::GetDefaultFontStyle("Bold", 24);
	const FSlateFontInfo MedNumFont = FCoreStyle::GetDefaultFontStyle("Bold", 15);
	const FSlateFontInfo HUDStatFont = FCoreStyle::GetDefaultFontStyle("Bold", 13);
	const FSlateFontInfo HUDLabelFont = FCoreStyle::GetDefaultFontStyle("Regular", 11);
	const FSlateFontInfo BannerFont = FCoreStyle::GetDefaultFontStyle("Bold", 14);
	const FSlateFontInfo ReticleFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);

	auto MakeGuideLine = [&BodyFont](const FString& KeyTag, const FString& Desc) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 12.0f, 0.0f)
			[
				SNew(SBox)
				.MinDesiredWidth(92.0f)
				[
					SNew(SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor(FLinearColor(0.10f, 0.24f, 0.26f, 0.92f))
					.HAlign(HAlign_Center)
					.Padding(FMargin(8.0f, 3.0f))
					[
						SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.42f, 0.96f, 0.80f, 1.0f)))
						.Text(FText::FromString(KeyTag))
					]
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(BodyFont)
				.ColorAndOpacity(FSlateColor(FLinearColor(0.84f, 0.90f, 0.96f, 0.95f)))
				.Text(FText::FromString(Desc))
			];
	};

	auto BuildPauseUIScaleRow = [this, WhiteBrush, &HUDLabelFont]() -> TSharedRef<SWidget>
	{
		static const TCHAR* ScaleLabels[NumUIScalePresets] = {
			TEXT("75%"), TEXT("90%"), TEXT("100%"), TEXT("115%"), TEXT("130%")
		};
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		for (int32 i = 0; i < NumUIScalePresets; ++i)
		{
			Row->AddSlot()
				.FillWidth(1.0f)
				.Padding(i == 0 ? 0.0f : 4.0f, 0.0f, i == NumUIScalePresets - 1 ? 0.0f : 4.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
					.OnClicked(FOnClicked::CreateUObject(this, &USpaceDrifterHUDWidget::OnUIScaleSelected, i))
					.ContentPadding(FMargin(0.0f))
					[
						SAssignNew(PauseUIScaleBtnBorders[i], SBorder)
						.BorderImage(WhiteBrush)
						.BorderBackgroundColor(FLinearColor(0.09f, 0.13f, 0.20f, 0.95f))
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.Padding(FMargin(6.0f, 8.0f))
						[
							SAssignNew(PauseUIScaleBtnTexts[i], STextBlock)
							.Font(HUDLabelFont)
							.ColorAndOpacity(FSlateColor(FLinearColor(0.82f, 0.90f, 0.96f, 1.0f)))
							.Text(FText::FromString(ScaleLabels[i]))
						]
					]
				];
		}
		return Row;
	};

	auto BuildPauseTexQualityRow = [this, WhiteBrush, &HUDLabelFont]() -> TSharedRef<SWidget>
	{
		static const TCHAR* QualityLabels[NumQualityLevels] = {
			TEXT("낮음"), TEXT("보통"), TEXT("높음"), TEXT("에픽")
		};
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		for (int32 i = 0; i < NumQualityLevels; ++i)
		{
			Row->AddSlot()
				.FillWidth(1.0f)
				.Padding(i == 0 ? 0.0f : 4.0f, 0.0f, i == NumQualityLevels - 1 ? 0.0f : 4.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
					.OnClicked(FOnClicked::CreateUObject(this, &USpaceDrifterHUDWidget::OnTextureQualitySelected, i))
					.ContentPadding(FMargin(0.0f))
					[
						SAssignNew(PauseTexQualityBtnBorders[i], SBorder)
						.BorderImage(WhiteBrush)
						.BorderBackgroundColor(FLinearColor(0.09f, 0.13f, 0.20f, 0.95f))
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.Padding(FMargin(8.0f, 8.0f))
						[
							SAssignNew(PauseTexQualityBtnTexts[i], STextBlock)
							.Font(HUDLabelFont)
							.ColorAndOpacity(FSlateColor(FLinearColor(0.82f, 0.90f, 0.96f, 1.0f)))
							.Text(FText::FromString(QualityLabels[i]))
						]
					]
				];
		}
		return Row;
	};

	auto BuildPauseGfxQualityRow = [this, WhiteBrush, &HUDLabelFont]() -> TSharedRef<SWidget>
	{
		static const TCHAR* QualityLabels[NumQualityLevels] = {
			TEXT("낮음"), TEXT("보통"), TEXT("높음"), TEXT("에픽")
		};
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		for (int32 i = 0; i < NumQualityLevels; ++i)
		{
			Row->AddSlot()
				.FillWidth(1.0f)
				.Padding(i == 0 ? 0.0f : 4.0f, 0.0f, i == NumQualityLevels - 1 ? 0.0f : 4.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
					.OnClicked(FOnClicked::CreateUObject(this, &USpaceDrifterHUDWidget::OnGraphicsQualitySelected, i))
					.ContentPadding(FMargin(0.0f))
					[
						SAssignNew(PauseGfxQualityBtnBorders[i], SBorder)
						.BorderImage(WhiteBrush)
						.BorderBackgroundColor(FLinearColor(0.09f, 0.13f, 0.20f, 0.95f))
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.Padding(FMargin(8.0f, 8.0f))
						[
							SAssignNew(PauseGfxQualityBtnTexts[i], STextBlock)
							.Font(HUDLabelFont)
							.ColorAndOpacity(FSlateColor(FLinearColor(0.82f, 0.90f, 0.96f, 1.0f)))
							.Text(FText::FromString(QualityLabels[i]))
						]
					]
				];
		}
		return Row;
	};

	return SAssignNew(RootDPIScaler, SDPIScaler)
		.DPIScale(TAttribute<float>::Create(TAttribute<float>::FGetter::CreateUObject(this, &USpaceDrifterHUDWidget::GetCurrentUIScale)))
		[
			SNew(SOverlay)

		// =========================================================================
		// 레이어 1: 타이틀 시작 화면 (라이브 3D 우주 배경 위 반투명 글래스 카드)
		// =========================================================================
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SNew(SBorder)
			.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateUObject(this, &USpaceDrifterHUDWidget::GetTitleOverlayVisibility)))
			.BorderImage(WhiteBrush)
			.BorderBackgroundColor(FLinearColor(0.01f, 0.02f, 0.05f, 0.42f))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)

				// 좌측 메인 타이틀 & 이름 입력 & SPACE 시작 카드
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 18.0f, 0.0f)
				[
				SNew(SBox)
				.WidthOverride(620.0f)
				[
					SNew(SBorder)
					.BorderImage(WhiteBrush)
					.BorderBackgroundColor(FLinearColor(0.03f, 0.05f, 0.09f, 0.88f))
					.Padding(FMargin(32.0f, 26.0f))
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 0.0f, 0.0f, 16.0f)
						[
							SNew(SBox)
							.HeightOverride(3.0f)
							[
								SNew(SBorder)
								.BorderImage(WhiteBrush)
								.BorderBackgroundColor(FLinearColor(0.24f, 0.94f, 0.74f, 0.92f))
							]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						.Padding(0.0f, 0.0f, 0.0f, 6.0f)
						[
							SNew(STextBlock)
							.Font(HeroTagFont)
							.ColorAndOpacity(FSlateColor(FLinearColor(0.36f, 0.92f, 0.76f, 1.0f)))
							.Text(FText::FromString(TEXT("UE 5.9 ZERO-G 10-HUNT TIME ATTACK")))
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						.Padding(0.0f, 0.0f, 0.0f, 4.0f)
						[
							SNew(STextBlock)
							.Font(HeroTitleFont)
							.ColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.98f, 1.0f, 1.0f)))
							.Text(FText::FromString(TEXT("곰팡이 우주 정복 대작전")))
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						.Padding(0.0f, 0.0f, 0.0f, 18.0f)
						[
							SNew(STextBlock)
							.Font(HeroSubTitleFont)
							.ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.78f, 0.92f, 0.90f)))
							.Text(FText::FromString(TEXT("FUNGAL SPACE CONQUEST  ·  성장해서 포식자 10마리를 최단 시간 안에 모두 먹어치워라!")))
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 0.0f, 0.0f, 16.0f)
						[
							SNew(SBorder)
							.BorderImage(WhiteBrush)
							.BorderBackgroundColor(FLinearColor(0.06f, 0.09f, 0.15f, 0.85f))
							.Padding(FMargin(18.0f, 14.0f))
							[
								SNew(SVerticalBox)

								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0.0f, 0.0f, 0.0f, 8.0f)
								[
									SNew(STextBlock)
									.Font(SectionTitleFont)
									.ColorAndOpacity(FSlateColor(FLinearColor(0.86f, 0.94f, 1.0f, 1.0f)))
									.Text(FText::FromString(TEXT("조작법 및 촉수 능력 안내")))
								]

								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0.0f, 3.0f)
								[
									MakeGuideLine(TEXT("마우스 이동"), TEXT("카메라 회전 및 360° 무중력 조준"))
								]

								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0.0f, 3.0f)
								[
									MakeGuideLine(TEXT("스페이스바"), TEXT("선택한 촉수 발사 (길게 누르면 당기기·전염 유지)"))
								]

								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0.0f, 3.0f)
								[
									MakeGuideLine(TEXT("1번 키"), TEXT("V1 긴 촉수 — 소행성·적 곰팡이 그래플 & 전염 → 배양 폭발"))
								]

								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0.0f, 3.0f)
								[
									MakeGuideLine(TEXT("2번 키"), TEXT("V2 그물 촉수 — 미네랄 대량 포획 · 포식자 둔화 & 축소 (2처치 해금)"))
								]

								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0.0f, 3.0f)
								[
									MakeGuideLine(TEXT("3번 키"), TEXT("V3 긴급 탈출 — 단발 초고속 분사 탈출 · 가용 8% 소모 (5처치 해금)"))
								]

								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0.0f, 3.0f)
								[
									MakeGuideLine(TEXT("ESC 키"), TEXT("일시정지 및 설정 (UI 크기 · 그래픽 품질 · 재시작/종료)"))
								]
							]
						]

						// 플레이어 이름 입력창 (공백 불가 · 스페이스바 즉시 시작)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 0.0f, 0.0f, 16.0f)
						[
							SAssignNew(NameInputFrameBorder, SBorder)
							.BorderImage(WhiteBrush)
							.BorderBackgroundColor(FLinearColor(0.05f, 0.10f, 0.16f, 0.92f))
							.Padding(FMargin(16.0f, 12.0f))
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0.0f, 0.0f, 0.0f, 6.0f)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().FillWidth(1.0f)
									[
										SNew(STextBlock)
										.Font(MicroHeaderFont)
										.ColorAndOpacity(FSlateColor(FLinearColor(0.45f, 0.95f, 0.80f, 1.0f)))
										.Text(FText::FromString(TEXT("PLAYER NAME (리더보드 등록명)")))
									]
									+ SHorizontalBox::Slot().AutoWidth()
									[
										SAssignNew(NameConfirmHintTextBlock, STextBlock)
										.Font(MicroHeaderFont)
										.ColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.85f, 0.95f, 0.85f)))
										.Text(FText::FromString(TEXT("이름 입력 후 스페이스바로 바로 시작!")))
									]
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								[
									SAssignNew(NameInputTextBox, SEditableTextBox)
									.Font(HUDStatFont)
									.Justification(ETextJustify::Center)
									.HintText(FText::FromString(TEXT("플레이어 이름 입력 (필수)")))
									.OnTextChanged(FOnTextChanged::CreateUObject(this, &USpaceDrifterHUDWidget::OnNameTextChanged))
									.OnTextCommitted(FOnTextCommitted::CreateUObject(this, &USpaceDrifterHUDWidget::OnNameTextCommitted))
								]
							]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						.Padding(0.0f, 4.0f, 0.0f, 0.0f)
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Center)
							.Padding(0.0f, 0.0f, 0.0f, 10.0f)
							[
								SAssignNew(TitleKeycapOuterBorder, SBorder)
								.BorderImage(WhiteBrush)
								.BorderBackgroundColor(FLinearColor(0.24f, 0.96f, 0.76f, 0.95f))
								.Padding(FMargin(2.5f))
								[
									SAssignNew(TitleKeycapInnerBorder, SBorder)
									.BorderImage(WhiteBrush)
									.BorderBackgroundColor(FLinearColor(0.06f, 0.14f, 0.17f, 0.96f))
									.Padding(FMargin(36.0f, 10.0f))
									[
										SAssignNew(TitleKeycapTextBlock, STextBlock)
										.Font(TitleKeycapFont)
										.ColorAndOpacity(FSlateColor(FLinearColor(0.42f, 1.0f, 0.84f, 1.0f)))
										.Text(FText::FromString(TEXT("SPACE")))
									]
								]
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Center)
							.Padding(0.0f, 0.0f, 0.0f, 4.0f)
							[
								SAssignNew(TitlePromptMainTextBlock, STextBlock)
								.Font(TitlePromptMainFont)
								.ColorAndOpacity(FSlateColor(FLinearColor(0.96f, 1.0f, 0.98f, 1.0f)))
								.Text(FText::FromString(TEXT("눌러서 우주에 곰팡이 퍼뜨리기!")))
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Center)
							[
								SAssignNew(TitlePromptSubTextBlock, STextBlock)
								.Font(TitlePromptSubFont)
								.ColorAndOpacity(FSlateColor(FLinearColor(0.56f, 0.82f, 0.78f, 0.88f)))
								.Text(FText::FromString(TEXT("스페이스바 한 방이면 온 우주가 내 포자밭")))
							]
						]

						// 타이틀 하단 클릭 버튼 (옵션 / 게임 종료)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 16.0f, 0.0f, 0.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.Padding(0.0f, 0.0f, 6.0f, 0.0f)
							[
								SNew(SButton)
								.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
								.OnClicked(FOnClicked::CreateUObject(this, &USpaceDrifterHUDWidget::OnTitleOptionsClicked))
								.ContentPadding(FMargin(0.0f))
								[
									SNew(SBorder)
									.BorderImage(WhiteBrush)
									.BorderBackgroundColor(FLinearColor(0.11f, 0.22f, 0.34f, 0.94f))
									.HAlign(HAlign_Center)
									.Padding(FMargin(14.0f, 10.0f))
									[
										SNew(STextBlock)
										.Font(HUDStatFont)
										.ColorAndOpacity(FSlateColor(FLinearColor(0.84f, 0.94f, 1.0f, 1.0f)))
										.Text(FText::FromString(TEXT("옵션 (설정)")))
									]
								]
							]
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.Padding(6.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SButton)
								.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
								.OnClicked(FOnClicked::CreateUObject(this, &USpaceDrifterHUDWidget::OnPauseQuitClicked))
								.ContentPadding(FMargin(0.0f))
								[
									SNew(SBorder)
									.BorderImage(WhiteBrush)
									.BorderBackgroundColor(FLinearColor(0.36f, 0.10f, 0.14f, 0.94f))
									.HAlign(HAlign_Center)
									.Padding(FMargin(14.0f, 10.0f))
									[
										SNew(STextBlock)
										.Font(HUDStatFont)
										.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.78f, 0.80f, 1.0f)))
										.Text(FText::FromString(TEXT("게임 종료")))
									]
								]
							]
						]
					]
				]
				]

				// 우측 TOP 5 리더보드 & 초기화 카드
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(310.0f)
					[
						SNew(SBorder)
						.BorderImage(WhiteBrush)
						.BorderBackgroundColor(FLinearColor(0.03f, 0.05f, 0.09f, 0.88f))
						.Padding(FMargin(22.0f, 22.0f))
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, 0.0f, 0.0f, 12.0f)
							[
								SNew(SBox).HeightOverride(3.0f)
								[
									SNew(SBorder)
									.BorderImage(WhiteBrush)
									.BorderBackgroundColor(FLinearColor(1.0f, 0.76f, 0.24f, 0.92f))
								]
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, 0.0f, 0.0f, 4.0f)
							[
								SNew(STextBlock)
								.Font(SectionTitleFont)
								.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.86f, 0.42f, 1.0f)))
								.Text(FText::FromString(TEXT("TOP 5 리더보드")))
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, 0.0f, 0.0f, 12.0f)
							[
								SNew(STextBlock)
								.Font(MicroHeaderFont)
								.ColorAndOpacity(FSlateColor(FLinearColor(0.62f, 0.74f, 0.86f, 0.90f)))
								.Text(FText::FromString(TEXT("10마리 완주 최단 시간순 (미완주는 처치 수 순)")))
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, 0.0f, 0.0f, 16.0f)
							[
								SAssignNew(TitleLeaderboardListBox, SVerticalBox)
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Right)
							[
								SNew(SButton)
								.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
								.OnClicked(FOnClicked::CreateUObject(this, &USpaceDrifterHUDWidget::OnResetLeaderboardClicked))
								.ContentPadding(FMargin(0.0f))
								[
									SNew(SBorder)
									.BorderImage(WhiteBrush)
									.BorderBackgroundColor(FLinearColor(0.32f, 0.09f, 0.12f, 0.92f))
									.Padding(FMargin(14.0f, 7.0f))
									[
										SNew(STextBlock)
										.Font(HUDLabelFont)
										.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.65f, 0.68f, 1.0f)))
										.Text(FText::FromString(TEXT("리더보드 초기화")))
									]
								]
							]
						]
					]
				]
			]
		]

		// =========================================================================
		// 레이어 2: 인게임 HUD (다중 구역 배치 + 정중앙 촉수 표적 점)
		// =========================================================================
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SNew(SOverlay)
			.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateUObject(this, &USpaceDrifterHUDWidget::GetInGameHUDVisibility)))

			// ---------------------------------------------------------------------
			// [구역 A: 화면 정중앙] 촉수 조준 표적 점 (Center Aim Reticle)
			// ---------------------------------------------------------------------
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)

				// 크로스헤어 도트 + 4방향 가이드 틱
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(42.0f)
					.HeightOverride(42.0f)
					[
						SNew(SOverlay)

						// 상단 틱
						+ SOverlay::Slot()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Top)
						[
							SNew(SBox).WidthOverride(2.0f).HeightOverride(7.0f)
							[
								SAssignNew(ReticleTickBorders[0], SBorder)
								.BorderImage(WhiteBrush)
								.BorderBackgroundColor(FLinearColor(0.75f, 0.90f, 1.0f, 0.45f))
							]
						]

						// 하단 틱
						+ SOverlay::Slot()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Bottom)
						[
							SNew(SBox).WidthOverride(2.0f).HeightOverride(7.0f)
							[
								SAssignNew(ReticleTickBorders[1], SBorder)
								.BorderImage(WhiteBrush)
								.BorderBackgroundColor(FLinearColor(0.75f, 0.90f, 1.0f, 0.45f))
							]
						]

						// 좌측 틱
						+ SOverlay::Slot()
						.HAlign(HAlign_Left)
						.VAlign(VAlign_Center)
						[
							SNew(SBox).WidthOverride(7.0f).HeightOverride(2.0f)
							[
								SAssignNew(ReticleTickBorders[2], SBorder)
								.BorderImage(WhiteBrush)
								.BorderBackgroundColor(FLinearColor(0.75f, 0.90f, 1.0f, 0.45f))
							]
						]

						// 우측 틱
						+ SOverlay::Slot()
						.HAlign(HAlign_Right)
						.VAlign(VAlign_Center)
						[
							SNew(SBox).WidthOverride(7.0f).HeightOverride(2.0f)
							[
								SAssignNew(ReticleTickBorders[3], SBorder)
								.BorderImage(WhiteBrush)
								.BorderBackgroundColor(FLinearColor(0.75f, 0.90f, 1.0f, 0.45f))
							]
						]

						// 중앙 표적 점
						+ SOverlay::Slot()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SAssignNew(ReticleDotBox, SBox)
							.WidthOverride(6.0f)
							.HeightOverride(6.0f)
							[
								SAssignNew(ReticleDotBorder, SBorder)
								.BorderImage(WhiteBrush)
								.BorderBackgroundColor(FLinearColor(0.90f, 0.98f, 1.0f, 0.85f))
							]
						]
					]
				]

				// 표적 포착 거리 표시 (사거리 내 소행성 조준 시 점등)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.Padding(0.0f, 6.0f, 0.0f, 0.0f)
				[
					SNew(SBorder)
					.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateUObject(this, &USpaceDrifterHUDWidget::GetReticleTargetVisibility)))
					.BorderImage(WhiteBrush)
					.BorderBackgroundColor(FLinearColor(0.03f, 0.08f, 0.12f, 0.72f))
					.Padding(FMargin(8.0f, 2.0f))
					[
						SAssignNew(ReticleRangeTextBlock, STextBlock)
						.Font(ReticleFont)
						.ColorAndOpacity(FSlateColor(FLinearColor(0.30f, 1.0f, 0.78f, 1.0f)))
						.Text(FText::FromString(TEXT("0.0m TARGET")))
					]
				]
			]

			// ---------------------------------------------------------------------
			// [구역 B: 좌하단] 생체량 & 성장 코어 대시보드 카드
			// ---------------------------------------------------------------------
			+ SOverlay::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Bottom)
			.Padding(28.0f)
			[
				SNew(SBox)
				.WidthOverride(310.0f)
				[
					SNew(SBorder)
					.BorderImage(WhiteBrush)
					.BorderBackgroundColor(FLinearColor(0.03f, 0.05f, 0.09f, 0.82f))
					.Padding(FMargin(0.0f))
					[
						SNew(SVerticalBox)

						// 상단 액센트 라인
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBox).HeightOverride(3.0f)
							[
								SNew(SBorder)
								.BorderImage(WhiteBrush)
								.BorderBackgroundColor(FLinearColor(0.24f, 0.94f, 0.74f, 0.92f))
							]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(18.0f, 14.0f)
						[
							SNew(SVerticalBox)

							// 헤더 라인
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								.FillWidth(1.0f)
								.VAlign(VAlign_Center)
								[
									SNew(STextBlock)
									.Font(MicroHeaderFont)
									.ColorAndOpacity(FSlateColor(FLinearColor(0.40f, 0.94f, 0.78f, 0.95f)))
									.Text(FText::FromString(TEXT("BIOMASS CORE")))
								]
								+ SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
								[
									SAssignNew(BiomassMinTextBlock, STextBlock)
									.Font(MicroHeaderFont)
									.ColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.50f, 0.50f, 0.92f)))
									.Text(FText::FromString(TEXT("MIN 7.7")))
								]
							]

							// 대형 생체량 상대(%) + 보조 절대값
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, 2.0f, 0.0f, 6.0f)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Bottom)
								[
									SAssignNew(BiomassValueTextBlock, STextBlock)
									.Font(BigNumFont)
									.ColorAndOpacity(FSlateColor(FLinearColor(0.96f, 0.99f, 0.98f, 1.0f)))
									.Text(FText::FromString(TEXT("100%")))
								]
								+ SHorizontalBox::Slot()
								.FillWidth(1.0f)
								.HAlign(HAlign_Right)
								.VAlign(VAlign_Bottom)
								.Padding(8.0f, 0.0f, 0.0f, 3.0f)
								[
									SAssignNew(BiomassAbsTextBlock, STextBlock)
									.Font(MicroHeaderFont)
									.ColorAndOpacity(FSlateColor(FLinearColor(0.62f, 0.76f, 0.86f, 0.88f)))
									.Text(FText::FromString(TEXT("BIO 100 · MIN 8")))
								]
							]

							// 생체량 이중 게이지 바 (민트: 남은 가용 % / 앰버: 흡수 투자 중 %)
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, 0.0f, 0.0f, 12.0f)
							[
								SNew(SBox)
								.HeightOverride(10.0f)
								[
									SNew(SOverlay)
									+ SOverlay::Slot()
									[
										SAssignNew(BiomassInvestedBar, SProgressBar)
										.Percent(1.0f)
										.FillColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.68f, 0.16f, 0.96f)))
									]
									+ SOverlay::Slot()
									[
										SAssignNew(BiomassProgressBar, SProgressBar)
										.BackgroundImage(FStyleDefaults::GetNoBrush())
										.Percent(1.0f)
										.FillColorAndOpacity(FSlateColor(FLinearColor(0.24f, 0.94f, 0.74f, 0.96f)))
									]
								]
							]

							// 2열 스탯 박스: 반지름 | 최대 반지름 기록
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SHorizontalBox)

								// 좌측 셀: 현재 반지름
								+ SHorizontalBox::Slot()
								.FillWidth(1.0f)
								.Padding(0.0f, 0.0f, 6.0f, 0.0f)
								[
									SNew(SBorder)
									.BorderImage(WhiteBrush)
									.BorderBackgroundColor(FLinearColor(0.07f, 0.11f, 0.17f, 0.90f))
									.Padding(FMargin(10.0f, 8.0f))
									[
										SNew(SVerticalBox)
										+ SVerticalBox::Slot().AutoHeight()
										[
											SNew(STextBlock)
											.Font(MicroHeaderFont)
											.ColorAndOpacity(FSlateColor(FLinearColor(0.58f, 0.72f, 0.86f, 0.92f)))
											.Text(FText::FromString(TEXT("BODY RADIUS")))
										]
										+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 0.0f)
										[
											SAssignNew(RadiusValueTextBlock, STextBlock)
											.Font(MedNumFont)
											.ColorAndOpacity(FSlateColor(FLinearColor(0.70f, 0.92f, 1.0f, 1.0f)))
											.Text(FText::FromString(TEXT("0.75 m")))
										]
										+ SVerticalBox::Slot().AutoHeight()
										[
											SAssignNew(RadiusScaleTextBlock, STextBlock)
											.Font(MicroHeaderFont)
											.ColorAndOpacity(FSlateColor(FLinearColor(0.45f, 0.88f, 0.78f, 0.95f)))
											.Text(FText::FromString(TEXT("75 cm · x1.00")))
										]
									]
								]

								// 우측 셀: 최대 도달 반지름
								+ SHorizontalBox::Slot()
								.FillWidth(1.0f)
								.Padding(6.0f, 0.0f, 0.0f, 0.0f)
								[
									SNew(SBorder)
									.BorderImage(WhiteBrush)
									.BorderBackgroundColor(FLinearColor(0.07f, 0.11f, 0.17f, 0.90f))
									.Padding(FMargin(10.0f, 8.0f))
									[
										SNew(SVerticalBox)
										+ SVerticalBox::Slot().AutoHeight()
										[
											SNew(STextBlock)
											.Font(MicroHeaderFont)
											.ColorAndOpacity(FSlateColor(FLinearColor(0.58f, 0.72f, 0.86f, 0.92f)))
											.Text(FText::FromString(TEXT("PEAK SIZE")))
										]
										+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 0.0f)
										[
											SAssignNew(MaxRadiusValueTextBlock, STextBlock)
											.Font(MedNumFont)
											.ColorAndOpacity(FSlateColor(FLinearColor(0.88f, 0.94f, 1.0f, 1.0f)))
											.Text(FText::FromString(TEXT("0.75 m")))
										]
										+ SVerticalBox::Slot().AutoHeight()
										[
											SNew(STextBlock)
											.Font(MicroHeaderFont)
											.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.65f, 0.78f, 0.85f)))
											.Text(FText::FromString(TEXT("MAX RECORD")))
										]
									]
								]
							]
						]
					]
				]
			]

			// ---------------------------------------------------------------------
			// [구역 C: 우상단] 비행 속도계 + 미네랄 수확 카운터 (2개 분리 카드)
			// ---------------------------------------------------------------------
			+ SOverlay::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Top)
			.Padding(28.0f)
			[
				SNew(SHorizontalBox)

				// 카드 1: 비행 속도계 + 유효 사거리
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 12.0f, 0.0f)
				[
					SNew(SBox)
					.WidthOverride(196.0f)
					[
						SNew(SBorder)
						.BorderImage(WhiteBrush)
						.BorderBackgroundColor(FLinearColor(0.03f, 0.05f, 0.09f, 0.82f))
						.Padding(FMargin(16.0f, 12.0f))
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock)
								.Font(MicroHeaderFont)
								.ColorAndOpacity(FSlateColor(FLinearColor(0.58f, 0.78f, 0.98f, 0.95f)))
								.Text(FText::FromString(TEXT("VELOCITY")))
							]

							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 4.0f)
							[
								SAssignNew(SpeedValueTextBlock, STextBlock)
								.Font(BigNumFont)
								.ColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.98f, 1.0f, 1.0f)))
								.Text(FText::FromString(TEXT("0.0 m/s")))
							]

							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
							[
								SNew(SBox).HeightOverride(5.0f)
								[
									SAssignNew(SpeedProgressBar, SProgressBar)
									.Percent(0.0f)
									.FillColorAndOpacity(FSlateColor(FLinearColor(0.45f, 0.78f, 1.0f, 0.95f)))
								]
							]

							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f)
								[
									SAssignNew(SpeedSubTextBlock, STextBlock)
									.Font(MicroHeaderFont)
									.ColorAndOpacity(FSlateColor(FLinearColor(0.62f, 0.72f, 0.84f, 0.90f)))
									.Text(FText::FromString(TEXT("0 cm/s")))
								]
								+ SHorizontalBox::Slot().AutoWidth()
								[
									SAssignNew(ReachRangeTextBlock, STextBlock)
									.Font(MicroHeaderFont)
									.ColorAndOpacity(FSlateColor(FLinearColor(0.42f, 0.94f, 0.80f, 0.95f)))
									.Text(FText::FromString(TEXT("REACH 18.0m")))
								]
							]
						]
					]
				]

				// 카드 2: 미네랄 수확 카운터
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox)
					.WidthOverride(140.0f)
					[
						SNew(SBorder)
						.BorderImage(WhiteBrush)
						.BorderBackgroundColor(FLinearColor(0.03f, 0.05f, 0.09f, 0.82f))
						.Padding(FMargin(16.0f, 12.0f))
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock)
								.Font(MicroHeaderFont)
								.ColorAndOpacity(FSlateColor(FLinearColor(0.45f, 0.95f, 0.85f, 0.95f)))
								.Text(FText::FromString(TEXT("MINERALS")))
							]

							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 2.0f)
							[
								SAssignNew(MineralValueTextBlock, STextBlock)
								.Font(BigNumFont)
								.ColorAndOpacity(FSlateColor(FLinearColor(0.95f, 1.0f, 0.96f, 1.0f)))
								.Text(FText::FromString(TEXT("0")))
							]

							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock)
								.Font(MicroHeaderFont)
								.ColorAndOpacity(FSlateColor(FLinearColor(0.58f, 0.72f, 0.80f, 0.90f)))
								.Text(FText::FromString(TEXT("ABSORBED CRYSTALS")))
							]
						]
					]
				]
			]

			// ---------------------------------------------------------------------
			// [구역 D: 상단 중앙] 전술 이벤트 알림 배너
			// ---------------------------------------------------------------------
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Top)
			.Padding(0.0f, 28.0f, 0.0f, 0.0f)
			[
				SAssignNew(BannerContainerBorder, SBorder)
				.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateUObject(this, &USpaceDrifterHUDWidget::GetBannerVisibility)))
				.BorderImage(WhiteBrush)
				.BorderBackgroundColor(FLinearColor(0.03f, 0.06f, 0.11f, 0.86f))
				.Padding(FMargin(24.0f, 10.0f))
				[
					SAssignNew(BannerTextBlock, STextBlock)
					.Font(BannerFont)
					.ColorAndOpacity(FSlateColor(FLinearColor(0.45f, 0.96f, 0.80f, 1.0f)))
					.Text(FText::GetEmpty())
				]
			]

			// [피격 비네트] 화면 4면 붉은 테두리 경고 효과 (외곽 진한 빨강 + 내곽 옅은 빨강)
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Top)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox).HeightOverride(18.0f)
					[
						SAssignNew(DamageEdgeOuterBorders[0], SBorder)
						.Visibility(EVisibility::Collapsed)
						.BorderImage(WhiteBrush)
						.BorderBackgroundColor(FLinearColor(0.95f, 0.06f, 0.12f, 0.0f))
					]
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox).HeightOverride(34.0f)
					[
						SAssignNew(DamageEdgeInnerBorders[0], SBorder)
						.Visibility(EVisibility::Collapsed)
						.BorderImage(WhiteBrush)
						.BorderBackgroundColor(FLinearColor(0.95f, 0.06f, 0.12f, 0.0f))
					]
				]
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Bottom)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox).HeightOverride(34.0f)
					[
						SAssignNew(DamageEdgeInnerBorders[1], SBorder)
						.Visibility(EVisibility::Collapsed)
						.BorderImage(WhiteBrush)
						.BorderBackgroundColor(FLinearColor(0.95f, 0.06f, 0.12f, 0.0f))
					]
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox).HeightOverride(18.0f)
					[
						SAssignNew(DamageEdgeOuterBorders[1], SBorder)
						.Visibility(EVisibility::Collapsed)
						.BorderImage(WhiteBrush)
						.BorderBackgroundColor(FLinearColor(0.95f, 0.06f, 0.12f, 0.0f))
					]
				]
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Fill)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox).WidthOverride(18.0f)
					[
						SAssignNew(DamageEdgeOuterBorders[2], SBorder)
						.Visibility(EVisibility::Collapsed)
						.BorderImage(WhiteBrush)
						.BorderBackgroundColor(FLinearColor(0.95f, 0.06f, 0.12f, 0.0f))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox).WidthOverride(34.0f)
					[
						SAssignNew(DamageEdgeInnerBorders[2], SBorder)
						.Visibility(EVisibility::Collapsed)
						.BorderImage(WhiteBrush)
						.BorderBackgroundColor(FLinearColor(0.95f, 0.06f, 0.12f, 0.0f))
					]
				]
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Fill)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox).WidthOverride(34.0f)
					[
						SAssignNew(DamageEdgeInnerBorders[3], SBorder)
						.Visibility(EVisibility::Collapsed)
						.BorderImage(WhiteBrush)
						.BorderBackgroundColor(FLinearColor(0.95f, 0.06f, 0.12f, 0.0f))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox).WidthOverride(18.0f)
					[
						SAssignNew(DamageEdgeOuterBorders[3], SBorder)
						.Visibility(EVisibility::Collapsed)
						.BorderImage(WhiteBrush)
						.BorderBackgroundColor(FLinearColor(0.95f, 0.06f, 0.12f, 0.0f))
					]
				]
			]

			// [구역 G: 좌상단] 5분 타임어택 & 점수 카드
			+ SOverlay::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Top)
			.Padding(28.0f)
			[
				SNew(SBox)
				.WidthOverride(228.0f)
				[
					SNew(SBorder)
					.BorderImage(WhiteBrush)
					.BorderBackgroundColor(FLinearColor(0.03f, 0.05f, 0.09f, 0.82f))
					.Padding(FMargin(16.0f, 12.0f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f)
							[
								SNew(STextBlock)
								.Font(MicroHeaderFont)
								.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.82f, 0.36f, 0.95f)))
								.Text(FText::FromString(TEXT("HUNT TIME")))
							]
							+ SHorizontalBox::Slot().AutoWidth()
							[
								SAssignNew(MatchPlayerNameTextBlock, STextBlock)
								.Font(MicroHeaderFont)
								.ColorAndOpacity(FSlateColor(FLinearColor(0.62f, 0.80f, 0.94f, 0.92f)))
								.Text(FText::FromString(TEXT("익명 곰팡이")))
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 6.0f)
						[
							SAssignNew(MatchTimerTextBlock, STextBlock)
							.Font(BigNumFont)
							.ColorAndOpacity(FSlateColor(FLinearColor(0.96f, 0.99f, 1.0f, 1.0f)))
							.Text(FText::FromString(TEXT("05:00")))
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SAssignNew(MatchScoreTextBlock, STextBlock)
							.Font(HUDStatFont)
							.ColorAndOpacity(FSlateColor(FLinearColor(0.42f, 0.98f, 0.80f, 1.0f)))
							.Text(FText::FromString(TEXT("SCORE  0  (적 처치)")))
						]
					]
				]
			]

			// [구역 F] 활성 포식자별 개별 마커 오버레이 (머리 위 / 화면 가장자리)
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				SAssignNew(EnemyMarkersOverlay, SOverlay)
			]

			// [구역 H: 화면 중앙 상단] 신규 촉수 해금 대형 팝업 카드
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 0.0f, 220.0f)
			[
				SAssignNew(UnlockPopupOuterBorder, SBorder)
				.Visibility(EVisibility::Collapsed)
				.BorderImage(WhiteBrush)
				.BorderBackgroundColor(FLinearColor(0.20f, 0.94f, 1.0f, 0.95f))
				.Padding(FMargin(3.0f))
				[
					SAssignNew(UnlockPopupInnerBorder, SBorder)
					.BorderImage(WhiteBrush)
					.BorderBackgroundColor(FLinearColor(0.03f, 0.07f, 0.13f, 0.94f))
					.Padding(FMargin(34.0f, 18.0f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						[
							SAssignNew(UnlockPopupTagTextBlock, STextBlock)
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.32f, 0.96f, 1.0f, 1.0f)))
							.Text(FText::FromString(TEXT("★  NEW ABILITY UNLOCKED  ★")))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						.Padding(0.0f, 6.0f, 0.0f, 4.0f)
						[
							SAssignNew(UnlockPopupTitleTextBlock, STextBlock)
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 24))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.98f, 1.0f, 1.0f, 1.0f)))
							.Text(FText::FromString(TEXT("2번 키 · V2 그물 촉수 해금!")))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						[
							SAssignNew(UnlockPopupDescTextBlock, STextBlock)
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.76f, 0.92f, 0.98f, 0.95f)))
							.Text(FText::FromString(TEXT("광역 포식자 둔화 · 크기 -15% 영구 축소  |  2번 키로 전환")))
						]
					]
				]
			]

			// [구역 I: 화면 중앙 상단] 잠긴 촉수 능력 경고 배너 (강렬한 붉은/앰버 발광 프레임 + 스케일 팝 + 셰이크)
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 0.0f, 150.0f)
			[
				SAssignNew(LockedWarningOuterBorder, SBorder)
				.Visibility(EVisibility::Collapsed)
				.BorderImage(WhiteBrush)
				.BorderBackgroundColor(FLinearColor(1.0f, 0.24f, 0.16f, 0.98f))
				.Padding(FMargin(3.5f))
				[
					SAssignNew(LockedWarningInnerBorder, SBorder)
					.BorderImage(WhiteBrush)
					.BorderBackgroundColor(FLinearColor(0.14f, 0.02f, 0.04f, 0.96f))
					.Padding(FMargin(36.0f, 18.0f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						[
							SAssignNew(LockedWarningBadgeTextBlock, STextBlock)
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
							.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.72f, 0.22f, 1.0f)))
							.Text(FText::FromString(TEXT("⚠  LOCKED ABILITY  ⚠")))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						.Padding(0.0f, 6.0f, 0.0f, 4.0f)
						[
							SAssignNew(LockedWarningTitleTextBlock, STextBlock)
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 22))
							.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.94f, 0.92f, 1.0f)))
							.Text(FText::FromString(TEXT("🔒 아직 잠긴 능력입니다!")))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						[
							SAssignNew(LockedWarningDetailTextBlock, STextBlock)
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
							.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.78f, 0.36f, 1.0f)))
							.Text(FText::FromString(TEXT("포식자 2마리 처치 시 해금 (현재 0 / 2)")))
						]
					]
				]
			]

			// ---------------------------------------------------------------------
			// [구역 E: 하단 중앙] 3종 촉수 전술 슬롯 독
			// ---------------------------------------------------------------------
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Bottom)
			.Padding(0.0f, 0.0f, 0.0f, 28.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(6.0f, 0.0f)
				[
					BuildTendrilSlotBox(TEXT("1"), TEXT("V1 긴 촉수"), TEXT("그래플 · 전염"), 0)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(6.0f, 0.0f)
				[
					BuildTendrilSlotBox(TEXT("2"), TEXT("V2 그물 촉수"), TEXT("광역 둔화 · 축소"), 1)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(6.0f, 0.0f)
				[
					BuildTendrilSlotBox(TEXT("3"), TEXT("V3 긴급 탈출"), TEXT("단발 고속 대시"), 2)
				]
			]
		]

		// 레이어 3: 판 종료 오버레이 (5분 완주 / 게임오버 공용 + TOP 5 리더보드)
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SNew(SBorder)
			.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateUObject(this, &USpaceDrifterHUDWidget::GetGameOverOverlayVisibility)))
			.BorderImage(WhiteBrush)
			.BorderBackgroundColor(FLinearColor(0.02f, 0.03f, 0.06f, 0.68f))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(560.0f)
				[
					SNew(SBorder)
					.BorderImage(WhiteBrush)
					.BorderBackgroundColor(FLinearColor(0.04f, 0.06f, 0.11f, 0.94f))
					.Padding(FMargin(36.0f, 28.0f))
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						.Padding(0.0f, 0.0f, 0.0f, 6.0f)
						[
							SAssignNew(EndTitleTextBlock, STextBlock)
							.Font(HeroTitleFont)
							.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.34f, 0.38f, 1.0f)))
							.Text(FText::FromString(TEXT("GAME OVER")))
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						.Padding(0.0f, 0.0f, 0.0f, 12.0f)
						[
							SAssignNew(EndSubTitleTextBlock, STextBlock)
							.Font(HeroSubTitleFont)
							.ColorAndOpacity(FSlateColor(FLinearColor(0.88f, 0.92f, 0.98f, 0.95f)))
							.Text(FText::FromString(TEXT("점수가 리더보드에 등록되었습니다!")))
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						.Padding(0.0f, 0.0f, 0.0f, 16.0f)
						[
							SAssignNew(GameOverStatsTextBlock, STextBlock)
							.Font(HUDStatFont)
							.ColorAndOpacity(FSlateColor(FLinearColor(0.45f, 0.98f, 0.80f, 1.0f)))
							.Text(FText::FromString(TEXT("처치 점수: 0  |  최대 반지름: 0.75 m  |  흡수 미네랄: 0 개")))
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 0.0f, 0.0f, 14.0f)
						[
							SNew(SBorder)
							.BorderImage(WhiteBrush)
							.BorderBackgroundColor(FLinearColor(0.06f, 0.09f, 0.15f, 0.90f))
							.Padding(FMargin(18.0f, 14.0f))
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
									[
										SNew(STextBlock)
										.Font(SectionTitleFont)
										.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.86f, 0.42f, 1.0f)))
										.Text(FText::FromString(TEXT("TOP 5 리더보드")))
									]
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
									[
										SNew(SButton)
										.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
										.OnClicked(FOnClicked::CreateUObject(this, &USpaceDrifterHUDWidget::OnResetLeaderboardClicked))
										.ContentPadding(FMargin(0.0f))
										[
											SNew(SBorder)
											.BorderImage(WhiteBrush)
											.BorderBackgroundColor(FLinearColor(0.32f, 0.09f, 0.12f, 0.92f))
											.Padding(FMargin(10.0f, 4.0f))
											[
												SNew(STextBlock)
												.Font(MicroHeaderFont)
												.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.65f, 0.68f, 1.0f)))
												.Text(FText::FromString(TEXT("초기화")))
											]
										]
									]
								]
								+ SVerticalBox::Slot().AutoHeight()
								[
									SAssignNew(EndLeaderboardListBox, SVerticalBox)
								]
							]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						[
							SNew(STextBlock)
							.Font(TitlePromptMainFont)
							.ColorAndOpacity(FSlateColor(FLinearColor(0.45f, 1.0f, 0.82f, 1.0f)))
							.Text(FText::FromString(TEXT("[ SPACE ]  타이틀로 돌아가기 / 다음 도전")))
						]
					]
				]
			]
		]

		// =========================================================================
		// 레이어 4: ESC 일시정지 & 환경 설정 오버레이
		// =========================================================================
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SNew(SBorder)
			.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateUObject(this, &USpaceDrifterHUDWidget::GetPauseMenuOverlayVisibility)))
			.BorderImage(WhiteBrush)
			.BorderBackgroundColor(FLinearColor(0.01f, 0.02f, 0.05f, 0.72f))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(540.0f)
				[
					SNew(SBorder)
					.BorderImage(WhiteBrush)
					.BorderBackgroundColor(FLinearColor(0.03f, 0.05f, 0.10f, 0.96f))
					.Padding(FMargin(32.0f, 26.0f))
					[
						SNew(SVerticalBox)

						// 상단 액센트 라인
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 0.0f, 0.0f, 16.0f)
						[
							SNew(SBox).HeightOverride(3.0f)
							[
								SNew(SBorder)
								.BorderImage(WhiteBrush)
								.BorderBackgroundColor(FLinearColor(0.24f, 0.94f, 0.74f, 0.95f))
							]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						[
							SNew(STextBlock)
							.Font(HeroTagFont)
							.ColorAndOpacity(FSlateColor(FLinearColor(0.36f, 0.94f, 0.78f, 1.0f)))
							.Text(FText::FromString(TEXT("PAUSED  ·  SYSTEM SETTINGS")))
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						.Padding(0.0f, 4.0f, 0.0f, 4.0f)
						[
							SNew(STextBlock)
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 26))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.96f, 0.99f, 1.0f, 1.0f)))
							.Text(FText::FromString(TEXT("일시정지 및 설정")))
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						.Padding(0.0f, 0.0f, 0.0f, 18.0f)
						[
							SNew(STextBlock)
							.Font(HUDLabelFont)
							.ColorAndOpacity(FSlateColor(FLinearColor(0.62f, 0.74f, 0.88f, 0.90f)))
							.Text(FText::FromString(TEXT("ESC 키를 다시 누르면 즉시 게임으로 복귀합니다")))
						]

						// 설정 카드
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 0.0f, 0.0f, 18.0f)
						[
							SNew(SBorder)
							.BorderImage(WhiteBrush)
							.BorderBackgroundColor(FLinearColor(0.06f, 0.09f, 0.15f, 0.92f))
							.Padding(FMargin(18.0f, 16.0f))
							[
								SNew(SVerticalBox)

								// 1) UI 크기 (UI Scale)
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0.0f, 0.0f, 0.0f, 6.0f)
								[
									SNew(STextBlock)
									.Font(SectionTitleFont)
									.ColorAndOpacity(FSlateColor(FLinearColor(0.86f, 0.94f, 1.0f, 1.0f)))
									.Text(FText::FromString(TEXT("UI 크기 (UI Scale)")))
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0.0f, 0.0f, 0.0f, 14.0f)
								[
									BuildPauseUIScaleRow()
								]

								// 2) 텍스처 품질 (Texture Quality)
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0.0f, 0.0f, 0.0f, 6.0f)
								[
									SNew(STextBlock)
									.Font(SectionTitleFont)
									.ColorAndOpacity(FSlateColor(FLinearColor(0.86f, 0.94f, 1.0f, 1.0f)))
									.Text(FText::FromString(TEXT("텍스처 품질 (Texture Quality)")))
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0.0f, 0.0f, 0.0f, 14.0f)
								[
									BuildPauseTexQualityRow()
								]

								// 3) 전체 그래픽 품질 (Graphics Quality)
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0.0f, 0.0f, 0.0f, 6.0f)
								[
									SNew(STextBlock)
									.Font(SectionTitleFont)
									.ColorAndOpacity(FSlateColor(FLinearColor(0.86f, 0.94f, 1.0f, 1.0f)))
									.Text(FText::FromString(TEXT("전체 그래픽 품질 (Graphics Quality)")))
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0.0f, 0.0f, 0.0f, 10.0f)
								[
									BuildPauseGfxQualityRow()
								]

								+ SVerticalBox::Slot()
								.AutoHeight()
								.HAlign(HAlign_Center)
								[
									SAssignNew(PauseCurrentSettingsSummaryTextBlock, STextBlock)
									.Font(MicroHeaderFont)
									.ColorAndOpacity(FSlateColor(FLinearColor(0.56f, 0.78f, 0.86f, 0.92f)))
									.Text(FText::FromString(TEXT("현재 적용: UI 100% · 텍스처 높음 · 전체 높음")))
								]
							]
						]

						// 하단 액션 버튼 (계속하기 / 다시 시작 / 게임 나가기)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 0.0f, 0.0f, 8.0f)
						[
							SNew(SButton)
							.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
							.OnClicked(FOnClicked::CreateUObject(this, &USpaceDrifterHUDWidget::OnPauseResumeClicked))
							.ContentPadding(FMargin(0.0f))
							[
								SNew(SBorder)
								.BorderImage(WhiteBrush)
								.BorderBackgroundColor(FLinearColor(0.10f, 0.38f, 0.34f, 0.96f))
								.HAlign(HAlign_Center)
								.Padding(FMargin(16.0f, 12.0f))
								[
									SNew(STextBlock)
									.Font(HUDStatFont)
									.ColorAndOpacity(FSlateColor(FLinearColor(0.88f, 1.0f, 0.96f, 1.0f)))
									.Text(FText::FromString(TEXT("계속하기  (ESC)")))
								]
							]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.Padding(0.0f, 0.0f, 6.0f, 0.0f)
							[
								SNew(SButton)
								.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
								.OnClicked(FOnClicked::CreateUObject(this, &USpaceDrifterHUDWidget::OnPauseRestartClicked))
								.ContentPadding(FMargin(0.0f))
								[
									SNew(SBorder)
									.BorderImage(WhiteBrush)
									.BorderBackgroundColor(FLinearColor(0.14f, 0.20f, 0.30f, 0.95f))
									.HAlign(HAlign_Center)
									.Padding(FMargin(14.0f, 11.0f))
									[
										SNew(STextBlock)
										.Font(HUDStatFont)
										.ColorAndOpacity(FSlateColor(FLinearColor(0.88f, 0.94f, 1.0f, 1.0f)))
										.Text(FText::FromString(TEXT("다시 시작")))
									]
								]
							]
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.Padding(6.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SButton)
								.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
								.OnClicked(FOnClicked::CreateUObject(this, &USpaceDrifterHUDWidget::OnPauseQuitClicked))
								.ContentPadding(FMargin(0.0f))
								[
									SNew(SBorder)
									.BorderImage(WhiteBrush)
									.BorderBackgroundColor(FLinearColor(0.36f, 0.10f, 0.14f, 0.95f))
									.HAlign(HAlign_Center)
									.Padding(FMargin(14.0f, 11.0f))
									[
										SNew(STextBlock)
										.Font(HUDStatFont)
										.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.78f, 0.80f, 1.0f)))
										.Text(FText::FromString(TEXT("게임 나가기")))
									]
								]
							]
						]
					]
				]
			]
		]
	];
}

TSharedRef<SWidget> USpaceDrifterHUDWidget::BuildTendrilSlotBox(
	const FString& KeyLabel,
	const FString& NameLabel,
	const FString& SubDescLabel,
	int32 SlotIndex)
{
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("WhiteBrush");

	return SNew(SBox)
		.WidthOverride(172.0f)
		[
			SAssignNew(SlotBorders[SlotIndex], SBorder)
			.BorderImage(WhiteBrush)
			.BorderBackgroundColor(FLinearColor(0.04f, 0.06f, 0.11f, 0.82f))
			.Padding(FMargin(0.0f))
			[
				SNew(SVerticalBox)

				// 상단 활성 강조 바
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox).HeightOverride(3.0f)
					[
						SAssignNew(SlotAccentBorders[SlotIndex], SBorder)
						.BorderImage(WhiteBrush)
						.BorderBackgroundColor(FLinearColor(0.18f, 0.25f, 0.34f, 0.60f))
					]
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(14.0f, 9.0f)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 6.0f, 0.0f)
						[
							SAssignNew(SlotKeyTexts[SlotIndex], STextBlock)
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.45f, 0.96f, 0.80f, 1.0f)))
							.Text(FText::FromString(FString::Printf(TEXT("%s번 키"), *KeyLabel)))
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						[
							SAssignNew(SlotTitleTexts[SlotIndex], STextBlock)
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.82f, 0.88f, 0.95f, 0.95f)))
							.Text(FText::FromString(NameLabel))
						]
					]

					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 0.0f)
					[
						SAssignNew(SlotDescTexts[SlotIndex], STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.58f, 0.70f, 0.82f, 0.88f)))
						.Text(FText::FromString(SubDescLabel))
					]
				]
			]
		];
}

void USpaceDrifterHUDWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	RootDPIScaler.Reset();

	NameInputFrameBorder.Reset();
	NameInputTextBox.Reset();
	NameConfirmHintTextBlock.Reset();
	TitleLeaderboardListBox.Reset();
	TitleKeycapOuterBorder.Reset();
	TitleKeycapInnerBorder.Reset();
	TitleKeycapTextBlock.Reset();
	TitlePromptMainTextBlock.Reset();
	TitlePromptSubTextBlock.Reset();

	MatchTimerTextBlock.Reset();
	MatchScoreTextBlock.Reset();
	MatchPlayerNameTextBlock.Reset();

	BiomassValueTextBlock.Reset();
	BiomassAbsTextBlock.Reset();
	BiomassMinTextBlock.Reset();
	BiomassInvestedBar.Reset();
	BiomassProgressBar.Reset();
	RadiusValueTextBlock.Reset();
	RadiusScaleTextBlock.Reset();
	MaxRadiusValueTextBlock.Reset();
	SpeedValueTextBlock.Reset();
	SpeedSubTextBlock.Reset();
	SpeedProgressBar.Reset();
	ReachRangeTextBlock.Reset();
	MineralValueTextBlock.Reset();
	BannerContainerBorder.Reset();
	BannerTextBlock.Reset();
	UnlockPopupOuterBorder.Reset();
	UnlockPopupInnerBorder.Reset();
	UnlockPopupTagTextBlock.Reset();
	UnlockPopupTitleTextBlock.Reset();
	UnlockPopupDescTextBlock.Reset();
	LockedWarningOuterBorder.Reset();
	LockedWarningInnerBorder.Reset();
	LockedWarningBadgeTextBlock.Reset();
	LockedWarningTitleTextBlock.Reset();
	LockedWarningDetailTextBlock.Reset();
	PauseCurrentSettingsSummaryTextBlock.Reset();
	for (int32 i = 0; i < NumUIScalePresets; ++i)
	{
		PauseUIScaleBtnBorders[i].Reset();
		PauseUIScaleBtnTexts[i].Reset();
	}
	for (int32 i = 0; i < NumQualityLevels; ++i)
	{
		PauseTexQualityBtnBorders[i].Reset();
		PauseTexQualityBtnTexts[i].Reset();
		PauseGfxQualityBtnBorders[i].Reset();
		PauseGfxQualityBtnTexts[i].Reset();
	}
	ReticleDotBox.Reset();
	ReticleDotBorder.Reset();
	ReticleRangeTextBlock.Reset();
	EnemyMarkersOverlay.Reset();
	for (int32 i = 0; i < MaxEnemyMarkers; ++i)
	{
		EnemyIndicatorBorders[i].Reset();
		EnemyIndicatorTextBlocks[i].Reset();
	}
	EndTitleTextBlock.Reset();
	EndSubTitleTextBlock.Reset();
	GameOverStatsTextBlock.Reset();
	EndLeaderboardListBox.Reset();

	for (int32 i = 0; i < 4; ++i)
	{
		ReticleTickBorders[i].Reset();
		DamageEdgeOuterBorders[i].Reset();
		DamageEdgeInnerBorders[i].Reset();
	}

	for (int32 i = 0; i < 3; ++i)
	{
		SlotAccentBorders[i].Reset();
		SlotBorders[i].Reset();
		SlotKeyTexts[i].Reset();
		SlotTitleTexts[i].Reset();
		SlotDescTexts[i].Reset();
	}
}

void USpaceDrifterHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (ASpaceDrifterPlayerController* PC = OwnerPC.Get())
	{
		if (bPendingInitialNameFocus && PC->IsInTitleScreen() && NameInputTextBox.IsValid() && FSlateApplication::IsInitialized())
		{
			bPendingInitialNameFocus = false;
			FSlateApplication::Get().SetKeyboardFocus(NameInputTextBox.ToSharedRef(), EFocusCause::SetDirectly);
			PC->SetNameInputFocused(true);
		}
		else
		{
			const bool bFocused = NameInputTextBox.IsValid() && NameInputTextBox->HasKeyboardFocus();
			PC->SetNameInputFocused(bFocused);

			if (PC->IsInTitleScreen())
			{
				// Cocoa IME setMarkedText 블록 밖(NativeTick)에서 안전하게 SetText 및 타이틀 종료 처리
				if (bPendingSanitizeNameInput)
				{
					bPendingSanitizeNameInput = false;
					if (NameInputTextBox.IsValid())
					{
						const FString Sanitized = GetSanitizedNameInputText();
						if (NameInputTextBox->GetText().ToString() != Sanitized)
						{
							NameInputTextBox->SetText(FText::FromString(Sanitized));
						}
					}
				}

				if (bPendingTitleStartFromSpace)
				{
					bPendingTitleStartFromSpace = false;
					const FString Sanitized = GetSanitizedNameInputText();
					if (Sanitized.IsEmpty())
					{
						TriggerEmptyNameWarning();
					}
					else
					{
						PC->StartGameplayFromTitle();
					}
				}
			}
			else
			{
				bPendingSanitizeNameInput = false;
				bPendingTitleStartFromSpace = false;
			}
		}
	}
	RefreshTitlePulse(InDeltaTime);
	RefreshUnlockEffects(InDeltaTime);
	RefreshLockedAbilityWarning(InDeltaTime);
	RefreshDamageEdgeFlash(InDeltaTime);
	RefreshHUDTexts();
	RefreshAimReticle();
	RefreshEnemyIndicator(MyGeometry);
}

void USpaceDrifterHUDWidget::TriggerAbilityUnlockEffect(ETendrilVisualStyle UnlockedStyle)
{
	const bool bIsV3 = (UnlockedStyle == ETendrilVisualStyle::SporeJet);
	const int32 SlotIndex = bIsV3 ? 2 : 1;

	UnlockPopupAccentColor = bIsV3
		? FLinearColor(1.0f, 0.80f, 0.22f, 1.0f)
		: FLinearColor(0.18f, 0.94f, 1.0f, 1.0f);
	UnlockEdgeFlashColor = UnlockPopupAccentColor;
	UnlockEdgeFlashAlpha = 1.0f;
	UnlockPopupTotalSeconds = 3.2f;
	UnlockPopupRemainingSeconds = UnlockPopupTotalSeconds;
	SlotUnlockGlowTimers[SlotIndex] = 2.8f;

	if (UnlockPopupTagTextBlock.IsValid())
	{
		UnlockPopupTagTextBlock->SetText(FText::FromString(
			bIsV3 ? TEXT("★  APEX ABILITY UNLOCKED  ★") : TEXT("★  NEW ABILITY UNLOCKED  ★")
		));
	}
	if (UnlockPopupTitleTextBlock.IsValid())
	{
		UnlockPopupTitleTextBlock->SetText(FText::FromString(
			bIsV3 ? TEXT("3번 키 · V3 긴급 탈출 분사 해금!") : TEXT("2번 키 · V2 그물 촉수 해금!")
		));
	}
	if (UnlockPopupDescTextBlock.IsValid())
	{
		UnlockPopupDescTextBlock->SetText(FText::FromString(
			bIsV3
				? TEXT("전방 고속 임펄스 긴급 탈출 (쿨타임 1초)  |  3번 키로 전환")
				: TEXT("광역 포식자 둔화 · 크기 -15% 영구 축소  |  2번 키로 전환")
		));
	}
}

void USpaceDrifterHUDWidget::RefreshUnlockEffects(float InDeltaTime)
{
	if (UnlockEdgeFlashAlpha > KINDA_SMALL_NUMBER)
	{
		UnlockEdgeFlashAlpha = FMath::Max(UnlockEdgeFlashAlpha - InDeltaTime * 0.85f, 0.0f);
	}

	for (int32 i = 0; i < 3; ++i)
	{
		if (SlotUnlockGlowTimers[i] > 0.0f)
		{
			SlotUnlockGlowTimers[i] = FMath::Max(SlotUnlockGlowTimers[i] - InDeltaTime, 0.0f);
		}
	}

	if (!UnlockPopupOuterBorder.IsValid())
	{
		return;
	}

	if (UnlockPopupRemainingSeconds <= KINDA_SMALL_NUMBER)
	{
		UnlockPopupRemainingSeconds = 0.0f;
		UnlockPopupOuterBorder->SetVisibility(EVisibility::Collapsed);
		return;
	}

	UnlockPopupRemainingSeconds = FMath::Max(UnlockPopupRemainingSeconds - InDeltaTime, 0.0f);
	const float Elapsed = UnlockPopupTotalSeconds - UnlockPopupRemainingSeconds;
	const float FadeIn = FMath::Clamp(Elapsed / 0.22f, 0.0f, 1.0f);
	const float FadeOut = FMath::Clamp(UnlockPopupRemainingSeconds / 0.55f, 0.0f, 1.0f);
	const float Alpha = FMath::Min(FadeIn, FadeOut);
	const float Pulse = 0.72f + 0.28f * FMath::Sin(TitlePulseTime * 9.0f);

	UnlockPopupOuterBorder->SetVisibility(EVisibility::HitTestInvisible);
	UnlockPopupOuterBorder->SetRenderOpacity(Alpha);
	UnlockPopupOuterBorder->SetBorderBackgroundColor(FLinearColor(
		UnlockPopupAccentColor.R,
		UnlockPopupAccentColor.G,
		UnlockPopupAccentColor.B,
		(0.70f + 0.30f * Pulse) * Alpha));

	if (UnlockPopupInnerBorder.IsValid())
	{
		UnlockPopupInnerBorder->SetBorderBackgroundColor(FLinearColor(0.03f, 0.07f, 0.13f, 0.94f * Alpha));
	}
	if (UnlockPopupTagTextBlock.IsValid())
	{
		UnlockPopupTagTextBlock->SetColorAndOpacity(FSlateColor(FLinearColor(
			UnlockPopupAccentColor.R,
			UnlockPopupAccentColor.G,
			UnlockPopupAccentColor.B,
			Alpha)));
	}
}

void USpaceDrifterHUDWidget::TriggerLockedAbilityWarning(ETendrilVisualStyle LockedStyle, int32 RequiredDefeats, int32 CurrentDefeats)
{
	const bool bIsV3 = (LockedStyle == ETendrilVisualStyle::SporeJet);
	const int32 SlotIndex = bIsV3 ? 2 : 1;
	const int32 ClampedRequired = FMath::Max(RequiredDefeats, 0);
	const int32 ClampedCurrent = FMath::Max(CurrentDefeats, 0);
	const int32 RemainingDefeats = FMath::Max(ClampedRequired - ClampedCurrent, 0);

	LockedWarningTotalSeconds = 2.1f;
	LockedWarningRemainingSeconds = LockedWarningTotalSeconds;
	SlotLockedFlashTimers[SlotIndex] = 0.85f;

	if (LockedWarningBadgeTextBlock.IsValid())
	{
		LockedWarningBadgeTextBlock->SetText(FText::FromString(
			bIsV3 ? TEXT("⚠  LOCKED ABILITY  ·  3번 키  ⚠") : TEXT("⚠  LOCKED ABILITY  ·  2번 키  ⚠")
		));
	}
	if (LockedWarningTitleTextBlock.IsValid())
	{
		LockedWarningTitleTextBlock->SetText(FText::FromString(
			bIsV3
				? TEXT("🔒 아직 잠긴 능력입니다! (V3 긴급 탈출)")
				: TEXT("🔒 아직 잠긴 능력입니다! (V2 그물 촉수)")
		));
	}
	if (LockedWarningDetailTextBlock.IsValid())
	{
		LockedWarningDetailTextBlock->SetText(FText::FromString(FString::Printf(
			TEXT("포식자 %d마리 처치 시 해금  (현재 %d / %d · %d마리 남음)"),
			ClampedRequired, ClampedCurrent, ClampedRequired, RemainingDefeats
		)));
	}
}

void USpaceDrifterHUDWidget::RefreshLockedAbilityWarning(float InDeltaTime)
{
	for (int32 i = 0; i < 3; ++i)
	{
		if (SlotLockedFlashTimers[i] > 0.0f)
		{
			SlotLockedFlashTimers[i] = FMath::Max(SlotLockedFlashTimers[i] - InDeltaTime, 0.0f);
		}
	}

	if (!LockedWarningOuterBorder.IsValid())
	{
		return;
	}

	if (LockedWarningRemainingSeconds <= KINDA_SMALL_NUMBER)
	{
		LockedWarningRemainingSeconds = 0.0f;
		LockedWarningOuterBorder->SetVisibility(EVisibility::Collapsed);
		LockedWarningOuterBorder->SetRenderTransform(FSlateRenderTransform());
		return;
	}

	LockedWarningRemainingSeconds = FMath::Max(LockedWarningRemainingSeconds - InDeltaTime, 0.0f);
	const float Elapsed = LockedWarningTotalSeconds - LockedWarningRemainingSeconds;

	// 빠른 등장 페이드 + 부드러운 퇴장 페이드
	const float FadeIn = FMath::Clamp(Elapsed / 0.12f, 0.0f, 1.0f);
	const float FadeOut = FMath::Clamp(LockedWarningRemainingSeconds / 0.40f, 0.0f, 1.0f);
	const float Alpha = FMath::Min(FadeIn, FadeOut);

	// 탄성 스케일 팝 (0.76 -> 1.09 오버슈트 -> 1.00 안정)
	float Scale = 1.0f;
	if (Elapsed < 0.10f)
	{
		Scale = FMath::Lerp(0.76f, 1.09f, Elapsed / 0.10f);
	}
	else if (Elapsed < 0.22f)
	{
		Scale = FMath::Lerp(1.09f, 1.00f, (Elapsed - 0.10f) / 0.12f);
	}

	// 감쇠 수평 셰이크 (잠김 거절 피드백)
	const float ShakeX = FMath::Sin(Elapsed * 46.0f) * 11.0f * FMath::Exp(-Elapsed * 7.5f);

	// 붉은색 <-> 앰버 발광 프레임 펄스
	const float GlowWave = 0.5f + 0.5f * FMath::Sin(Elapsed * 18.0f);
	const FLinearColor BorderColor(
		1.0f,
		FMath::Lerp(0.18f, 0.64f, GlowWave),
		FMath::Lerp(0.12f, 0.18f, GlowWave),
		(0.84f + 0.16f * GlowWave) * Alpha
	);

	LockedWarningOuterBorder->SetVisibility(EVisibility::HitTestInvisible);
	LockedWarningOuterBorder->SetRenderOpacity(Alpha);
	LockedWarningOuterBorder->SetRenderTransformPivot(FVector2D(0.5, 0.5));
	LockedWarningOuterBorder->SetRenderTransform(FSlateRenderTransform(Scale, FVector2f(ShakeX, 0.0f)));
	LockedWarningOuterBorder->SetBorderBackgroundColor(BorderColor);

	if (LockedWarningInnerBorder.IsValid())
	{
		LockedWarningInnerBorder->SetBorderBackgroundColor(FLinearColor(
			FMath::Lerp(0.13f, 0.22f, GlowWave),
			0.02f,
			0.04f,
			0.96f * Alpha
		));
	}
}

void USpaceDrifterHUDWidget::RefreshDamageEdgeFlash(float InDeltaTime)
{
	if (DamageFlashAlpha > KINDA_SMALL_NUMBER)
	{
		DamageFlashAlpha = FMath::Max(DamageFlashAlpha - InDeltaTime * 2.4f, 0.0f);
	}

	const bool bShowDamage = (DamageFlashAlpha > KINDA_SMALL_NUMBER);
	const bool bShowUnlock = (!bShowDamage && UnlockEdgeFlashAlpha > KINDA_SMALL_NUMBER);
	const bool bShow = bShowDamage || bShowUnlock;

	const float ActiveAlpha = bShowDamage ? DamageFlashAlpha : UnlockEdgeFlashAlpha;
	const float Pulse = bShowDamage
		? (0.80f + 0.20f * FMath::Sin(TitlePulseTime * 16.0f))
		: (0.78f + 0.22f * FMath::Sin(TitlePulseTime * 10.0f));
	const float OuterAlpha = ActiveAlpha * (bShowDamage ? 0.52f : 0.48f) * Pulse;
	const float InnerAlpha = ActiveAlpha * (bShowDamage ? 0.20f : 0.22f) * Pulse;
	const FLinearColor EdgeColor = bShowDamage
		? FLinearColor(0.98f, 0.05f, 0.12f, 1.0f)
		: UnlockEdgeFlashColor;
	const EVisibility Vis = bShow ? EVisibility::HitTestInvisible : EVisibility::Collapsed;

	for (int32 i = 0; i < 4; ++i)
	{
		if (DamageEdgeOuterBorders[i].IsValid())
		{
			DamageEdgeOuterBorders[i]->SetVisibility(Vis);
			if (bShow)
			{
				DamageEdgeOuterBorders[i]->SetBorderBackgroundColor(FLinearColor(EdgeColor.R, EdgeColor.G, EdgeColor.B, OuterAlpha));
			}
		}
		if (DamageEdgeInnerBorders[i].IsValid())
		{
			DamageEdgeInnerBorders[i]->SetVisibility(Vis);
			if (bShow)
			{
				DamageEdgeInnerBorders[i]->SetBorderBackgroundColor(FLinearColor(EdgeColor.R, EdgeColor.G, EdgeColor.B, InnerAlpha));
			}
		}
	}
}

void USpaceDrifterHUDWidget::RefreshEnemyIndicator(const FGeometry& MyGeometry)
{
	if (!EnemyMarkersOverlay.IsValid())
	{
		return;
	}

	// 최초 1회 마커 슬롯 풀 생성 (0-할당 유지)
	if (!EnemyIndicatorBorders[0].IsValid())
	{
		const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("WhiteBrush");
		const FSlateFontInfo MarkerFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);
		for (int32 i = 0; i < MaxEnemyMarkers; ++i)
		{
			EnemyMarkersOverlay->AddSlot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Top)
				[
					SAssignNew(EnemyIndicatorBorders[i], SBorder)
					.Visibility(EVisibility::Collapsed)
					.BorderImage(WhiteBrush)
					.BorderBackgroundColor(FLinearColor(0.28f, 0.04f, 0.08f, 0.88f))
					.Padding(FMargin(10.0f, 5.0f))
					[
						SAssignNew(EnemyIndicatorTextBlocks[i], STextBlock)
						.Font(MarkerFont)
						.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.34f, 0.42f, 1.0f)))
						.Text(FText::GetEmpty())
					]
				];
		}
	}

	auto HideAllMarkers = [this]()
	{
		for (int32 i = 0; i < MaxEnemyMarkers; ++i)
		{
			if (EnemyIndicatorBorders[i].IsValid())
			{
				EnemyIndicatorBorders[i]->SetVisibility(EVisibility::Collapsed);
			}
		}
	};

	const ASpaceDrifterPawn* Pawn = OwnerPawn.Get();
	const ASpaceDrifterPlayerController* PC = OwnerPC.Get();
	if (!Pawn || !PC || PC->IsInTitleScreen() || Pawn->IsRunEnded())
	{
		HideAllMarkers();
		return;
	}

	if (!CachedEnemyManager.IsValid())
	{
		if (const UWorld* World = GetWorld())
		{
			CachedEnemyManager = Cast<AEnemyMoldManager>(UGameplayStatics::GetActorOfClass(World, AEnemyMoldManager::StaticClass()));
		}
	}

	const AEnemyMoldManager* EnemyMgr = CachedEnemyManager.Get();
	if (!EnemyMgr)
	{
		HideAllMarkers();
		return;
	}

	const FVector2D RawLocalSize = MyGeometry.GetLocalSize();
	const float EffectiveDPIScale = GetCurrentUIScale();
	const FVector2D LocalSize = (EffectiveDPIScale > KINDA_SMALL_NUMBER) ? (RawLocalSize / EffectiveDPIScale) : RawLocalSize;
	if (LocalSize.X <= 1.0f || LocalSize.Y <= 1.0f)
	{
		HideAllMarkers();
		return;
	}

	TArray<FEnemyThreatMarkerInfo> Markers;
	EnemyMgr->GatherAllThreatInfos(Markers);
	if (Markers.IsEmpty())
	{
		HideAllMarkers();
		return;
	}

	// 위협 포식자 우선, 동순위 내에서는 가까운 거리 순으로 정렬해 슬롯 상한 초과 시 중요 대상 보존
	Markers.Sort([](const FEnemyThreatMarkerInfo& A, const FEnemyThreatMarkerInfo& B)
	{
		if (A.bIsThreat != B.bIsThreat)
		{
			return A.bIsThreat;
		}
		return A.DistanceCm < B.DistanceCm;
	});

	FVector CamLoc, CamFwd, CamRight, CamUp;
	Pawn->GetTendrilCameraBasis(CamLoc, CamFwd, CamRight, CamUp);

	int32 ViewportX = 0;
	int32 ViewportY = 0;
	PC->GetViewportSize(ViewportX, ViewportY);
	const FVector2D ViewportSize(FMath::Max(static_cast<float>(ViewportX), 1.0f), FMath::Max(static_cast<float>(ViewportY), 1.0f));

	const float EdgePadX = 68.0f;
	const float EdgePadY = 56.0f;
	const int32 ShowCount = FMath::Min(Markers.Num(), MaxEnemyMarkers);

	for (int32 i = 0; i < MaxEnemyMarkers; ++i)
	{
		if (!EnemyIndicatorBorders[i].IsValid() || !EnemyIndicatorTextBlocks[i].IsValid())
		{
			continue;
		}
		if (i >= ShowCount)
		{
			EnemyIndicatorBorders[i]->SetVisibility(EVisibility::Collapsed);
			continue;
		}

		const FEnemyThreatMarkerInfo& Info = Markers[i];
		// 화면 내에서는 포식자 머리 위쪽에 라벨 표시
		const FVector AnchorWorld = Info.WorldLocation + CamUp * (Info.RadiusCm * 1.15f);
		const FVector ToAnchor = AnchorWorld - CamLoc;
		const bool bBehindCamera = (FVector::DotProduct(CamFwd, ToAnchor) < 0.0f);

		FVector2D ScreenPos = FVector2D::ZeroVector;
		const bool bProjected = PC->ProjectWorldLocationToScreen(AnchorWorld, ScreenPos, true);

		FVector2D NormPos(0.5f, 0.5f);
		if (bProjected && !bBehindCamera)
		{
			NormPos = FVector2D(ScreenPos.X / ViewportSize.X, ScreenPos.Y / ViewportSize.Y);
		}
		else
		{
			const FVector DirUnit = ToAnchor.GetSafeNormal();
			const float RightDot = FVector::DotProduct(CamRight, DirUnit);
			const float UpDot = FVector::DotProduct(CamUp, DirUnit);
			FVector2D Dir2D(RightDot, -UpDot);
			if (Dir2D.IsNearlyZero())
			{
				Dir2D = FVector2D(0.0f, 1.0f);
			}
			else
			{
				Dir2D.Normalize();
			}
			NormPos = FVector2D(0.5f, 0.5f) + Dir2D * 0.6f;
		}

		FVector2D WidgetPos(NormPos.X * LocalSize.X, NormPos.Y * LocalSize.Y);
		WidgetPos.X = FMath::Clamp(WidgetPos.X, EdgePadX, FMath::Max(LocalSize.X - EdgePadX, EdgePadX));
		WidgetPos.Y = FMath::Clamp(WidgetPos.Y, EdgePadY, FMath::Max(LocalSize.Y - EdgePadY, EdgePadY));

		EnemyIndicatorBorders[i]->SetVisibility(EVisibility::HitTestInvisible);
		EnemyIndicatorBorders[i]->SetRenderTransform(FSlateRenderTransform(WidgetPos - FVector2D(54.0f, 14.0f)));

		const float DistM = Info.DistanceCm * 0.01f;
		const TCHAR* SlowTag = Info.bIsWebSlowed ? TEXT(" [둔화]") : TEXT("");
		if (Info.bIsThreat)
		{
			EnemyIndicatorBorders[i]->SetBorderBackgroundColor(FLinearColor(0.32f, 0.04f, 0.08f, 0.88f));
			EnemyIndicatorTextBlocks[i]->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.34f, 0.42f, 1.0f)));
			EnemyIndicatorTextBlocks[i]->SetText(FText::FromString(FString::Printf(TEXT("▲ 포식자 %.0fm%s"), DistM, SlowTag)));
		}
		else
		{
			EnemyIndicatorBorders[i]->SetBorderBackgroundColor(FLinearColor(0.06f, 0.22f, 0.16f, 0.88f));
			EnemyIndicatorTextBlocks[i]->SetColorAndOpacity(FSlateColor(FLinearColor(0.40f, 1.0f, 0.76f, 1.0f)));
			EnemyIndicatorTextBlocks[i]->SetText(FText::FromString(FString::Printf(TEXT("◆ 사냥 가능 %.0fm%s"), DistM, SlowTag)));
		}
	}
}

void USpaceDrifterHUDWidget::TriggerEmptyNameWarning()
{
	EmptyNameWarningTimer = 2.0f;

	if (NameConfirmHintTextBlock.IsValid())
	{
		NameConfirmHintTextBlock->SetText(FText::FromString(TEXT("⚠ 이름을 입력해야 시작할 수 있습니다!")));
		NameConfirmHintTextBlock->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.36f, 0.32f, 1.0f)));
	}
	if (NameInputFrameBorder.IsValid())
	{
		NameInputFrameBorder->SetBorderBackgroundColor(FLinearColor(0.42f, 0.08f, 0.10f, 0.96f));
	}
	if (NameInputTextBox.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetKeyboardFocus(NameInputTextBox.ToSharedRef(), EFocusCause::SetDirectly);
	}
}

void USpaceDrifterHUDWidget::RefreshTitlePulse(float InDeltaTime)
{
	TitlePulseTime += InDeltaTime;
	const float Pulse = 0.5f + 0.5f * FMath::Sin(TitlePulseTime * 3.2f);

	if (EmptyNameWarningTimer > 0.0f)
	{
		EmptyNameWarningTimer = FMath::Max(EmptyNameWarningTimer - InDeltaTime, 0.0f);
		const float WarnPulse = 0.5f + 0.5f * FMath::Sin(TitlePulseTime * 18.0f);
		if (EmptyNameWarningTimer > KINDA_SMALL_NUMBER)
		{
			if (NameInputFrameBorder.IsValid())
			{
				NameInputFrameBorder->SetBorderBackgroundColor(FLinearColor(
					FMath::Lerp(0.24f, 0.48f, WarnPulse),
					FMath::Lerp(0.06f, 0.11f, WarnPulse),
					FMath::Lerp(0.08f, 0.13f, WarnPulse),
					0.96f));
			}
			if (NameConfirmHintTextBlock.IsValid())
			{
				NameConfirmHintTextBlock->SetColorAndOpacity(FSlateColor(FLinearColor(
					1.0f,
					FMath::Lerp(0.30f, 0.58f, WarnPulse),
					FMath::Lerp(0.26f, 0.42f, WarnPulse),
					1.0f)));
			}
		}
		else
		{
			if (NameInputFrameBorder.IsValid())
			{
				NameInputFrameBorder->SetBorderBackgroundColor(FLinearColor(0.05f, 0.10f, 0.16f, 0.92f));
			}
			if (NameConfirmHintTextBlock.IsValid())
			{
				NameConfirmHintTextBlock->SetText(FText::FromString(TEXT("이름 입력 후 스페이스바로 바로 시작!")));
				NameConfirmHintTextBlock->SetColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.85f, 0.95f, 0.85f)));
			}
		}
	}

	if (TitleKeycapOuterBorder.IsValid())
	{
		TitleKeycapOuterBorder->SetBorderBackgroundColor(FLinearColor(
			FMath::Lerp(0.18f, 0.42f, Pulse),
			FMath::Lerp(0.76f, 1.00f, Pulse),
			FMath::Lerp(0.62f, 0.86f, Pulse),
			FMath::Lerp(0.60f, 1.00f, Pulse)));
	}
	if (TitleKeycapInnerBorder.IsValid())
	{
		TitleKeycapInnerBorder->SetBorderBackgroundColor(FLinearColor(
			FMath::Lerp(0.04f, 0.11f, Pulse),
			FMath::Lerp(0.10f, 0.22f, Pulse),
			FMath::Lerp(0.14f, 0.24f, Pulse),
			0.96f));
	}
	if (TitleKeycapTextBlock.IsValid())
	{
		TitleKeycapTextBlock->SetColorAndOpacity(FSlateColor(FLinearColor(
			FMath::Lerp(0.35f, 0.72f, Pulse),
			FMath::Lerp(0.92f, 1.00f, Pulse),
			FMath::Lerp(0.78f, 0.94f, Pulse),
			1.0f)));
	}
	if (TitlePromptMainTextBlock.IsValid())
	{
		TitlePromptMainTextBlock->SetColorAndOpacity(FSlateColor(FLinearColor(
			0.96f,
			1.0f,
			0.98f,
			FMath::Lerp(0.78f, 1.00f, Pulse))));
	}
}

void USpaceDrifterHUDWidget::RefreshHUDTexts()
{
	const ASpaceDrifterPawn* Pawn = OwnerPawn.Get();
	if (!Pawn)
	{
		return;
	}

	const float Biomass = Pawn->CurrentBiomass;
	const float MinBiomass = Pawn->GetTendrilMinGameOverBiomass();
	const float InvestedBiomass = FMath::Max(Pawn->GetInvestedColonizeBiomass(), 0.0f);
	const float UsableBiomass = FMath::Max(Biomass - MinBiomass, 0.0f);
	const float TotalUsablePool = UsableBiomass + InvestedBiomass;
	const float RemainingAlpha = (TotalUsablePool > KINDA_SMALL_NUMBER)
		? FMath::Clamp(UsableBiomass / TotalUsablePool, 0.0f, 1.0f)
		: 0.0f;
	const float InvestedAlpha = (TotalUsablePool > KINDA_SMALL_NUMBER)
		? FMath::Clamp(InvestedBiomass / TotalUsablePool, 0.0f, 1.0f)
		: 0.0f;
	const float RemainingPercent = RemainingAlpha * 100.0f;
	const float InvestedPercent = InvestedAlpha * 100.0f;
	const bool bIsInvesting = InvestedBiomass > 0.1f;

	const float RadiusCm = Pawn->GetTendrilBodyRadius();
	const float RadiusM = RadiusCm * 0.01f;
	const float MaxRadiusM = Pawn->MaxReachedBodyRadiusCm * 0.01f;
	const float BodyScale = Pawn->GetTendrilBodyScale();
	const float SpeedCm = Pawn->GetTendrilBodyVelocity().Size();
	const float SpeedM = SpeedCm * 0.01f;
	const float SpeedAlpha = FMath::Clamp(SpeedCm / 2400.0f, 0.0f, 1.0f);

	const USpaceTendrilAbility* ActiveAbility = Pawn->GetActiveTendrilAbility();
	const float EffectiveReachM = (ActiveAbility ? ActiveAbility->GetMaxEffectiveReachCm() : Pawn->LongTendrilMaxLength * BodyScale) * 0.01f;

	if (BiomassValueTextBlock.IsValid())
	{
		BiomassValueTextBlock->SetText(FText::FromString(FString::Printf(TEXT("%.0f%%"), RemainingPercent)));
		const FLinearColor NumColor = (RemainingAlpha < 0.2f)
			? FLinearColor(1.0f, 0.42f, 0.42f, 1.0f)
			: FLinearColor(0.96f, 0.99f, 0.98f, 1.0f);
		BiomassValueTextBlock->SetColorAndOpacity(FSlateColor(NumColor));
	}
	if (BiomassAbsTextBlock.IsValid())
	{
		BiomassAbsTextBlock->SetText(FText::FromString(FString::Printf(TEXT("BIO %.0f · MIN %.0f"), Biomass, MinBiomass)));
	}
	if (BiomassMinTextBlock.IsValid())
	{
		if (bIsInvesting)
		{
			BiomassMinTextBlock->SetText(FText::FromString(FString::Printf(TEXT("흡수 투자 %.0f%%"), InvestedPercent)));
			BiomassMinTextBlock->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.72f, 0.22f, 0.98f)));
		}
		else
		{
			BiomassMinTextBlock->SetText(FText::FromString(TEXT("가용 100%")));
			BiomassMinTextBlock->SetColorAndOpacity(FSlateColor(FLinearColor(0.42f, 0.94f, 0.78f, 0.90f)));
		}
	}
	if (BiomassInvestedBar.IsValid())
	{
		BiomassInvestedBar->SetPercent((TotalUsablePool > KINDA_SMALL_NUMBER && bIsInvesting) ? 1.0f : 0.0f);
	}
	if (BiomassProgressBar.IsValid())
	{
		BiomassProgressBar->SetPercent(RemainingAlpha);
		const FLinearColor BarColor = (RemainingAlpha < 0.2f)
			? FLinearColor(1.0f, 0.36f, 0.36f, 0.96f)
			: FLinearColor(0.24f, 0.94f, 0.74f, 0.96f);
		BiomassProgressBar->SetFillColorAndOpacity(FSlateColor(BarColor));
	}

	if (RadiusValueTextBlock.IsValid())
	{
		RadiusValueTextBlock->SetText(FText::FromString(FString::Printf(TEXT("%.2f m"), RadiusM)));
	}
	if (RadiusScaleTextBlock.IsValid())
	{
		RadiusScaleTextBlock->SetText(FText::FromString(FString::Printf(TEXT("%.0f cm · x%.2f"), RadiusCm, BodyScale)));
	}
	if (MaxRadiusValueTextBlock.IsValid())
	{
		MaxRadiusValueTextBlock->SetText(FText::FromString(FString::Printf(TEXT("%.2f m"), MaxRadiusM)));
	}

	if (SpeedValueTextBlock.IsValid())
	{
		SpeedValueTextBlock->SetText(FText::FromString(FString::Printf(TEXT("%.1f m/s"), SpeedM)));
	}
	if (SpeedSubTextBlock.IsValid())
	{
		SpeedSubTextBlock->SetText(FText::FromString(FString::Printf(TEXT("%.0f cm/s"), SpeedCm)));
	}
	if (SpeedProgressBar.IsValid())
	{
		SpeedProgressBar->SetPercent(SpeedAlpha);
	}
	if (ReachRangeTextBlock.IsValid())
	{
		ReachRangeTextBlock->SetText(FText::FromString(FString::Printf(TEXT("REACH %.1fm"), EffectiveReachM)));
	}

	if (MineralValueTextBlock.IsValid())
	{
		MineralValueTextBlock->SetText(FText::FromString(FString::Printf(TEXT("%d"), Pawn->ConsumedMineralCount)));
	}

	const float ElapsedSec = FMath::Max(Pawn->ElapsedSurvivalSeconds, 0.0f);
	const int32 TotalTenths = FMath::FloorToInt(ElapsedSec * 10.0f);
	const int32 Mins = (TotalTenths / 10) / 60;
	const int32 Secs = (TotalTenths / 10) % 60;
	const int32 Tenths = TotalTenths % 10;
	const int32 TargetKills = FMath::Max(Pawn->TargetVictoryEnemyCount, 1);

	if (MatchTimerTextBlock.IsValid())
	{
		MatchTimerTextBlock->SetText(FText::FromString(FString::Printf(TEXT("%02d:%02d.%d"), Mins, Secs, Tenths)));
		MatchTimerTextBlock->SetColorAndOpacity(FSlateColor(FLinearColor(0.96f, 0.99f, 1.0f, 1.0f)));
	}
	if (MatchScoreTextBlock.IsValid())
	{
		const AEnemyMoldManager* EnemyMgr = CachedEnemyManager.Get();
		const int32 ActivePredators = EnemyMgr ? EnemyMgr->GetActiveEnemyCount() : 0;
		const int32 TargetPredators = EnemyMgr ? EnemyMgr->GetCurrentTargetActiveEnemyCount(Pawn) : 1;
		MatchScoreTextBlock->SetText(FText::FromString(FString::Printf(
			TEXT("처치 %d/%d  ·  활성 %d/%d"), Pawn->DefeatedEnemyCount, TargetKills, ActivePredators, TargetPredators)));
	}
	if (MatchPlayerNameTextBlock.IsValid())
	{
		if (const ASpaceDrifterPlayerController* PC = OwnerPC.Get())
		{
			MatchPlayerNameTextBlock->SetText(FText::FromString(PC->GetCurrentPlayerName()));
		}
	}

	if (EndTitleTextBlock.IsValid() && EndSubTitleTextBlock.IsValid())
	{
		if (Pawn->RunState == ESpaceRunState::Victory)
		{
			EndTitleTextBlock->SetText(FText::FromString(FString::Printf(TEXT("CLEAR!  %02d:%02d.%d"), Mins, Secs, Tenths)));
			EndTitleTextBlock->SetColorAndOpacity(FSlateColor(FLinearColor(0.28f, 1.0f, 0.78f, 1.0f)));
			EndSubTitleTextBlock->SetText(FText::FromString(FString::Printf(
				TEXT("포식자 %d마리 처치 완료! 걸린 시간 %.2f초가 리더보드에 등록되었습니다."), TargetKills, ElapsedSec)));
		}
		else
		{
			EndTitleTextBlock->SetText(FText::FromString(TEXT("GAME OVER")));
			EndTitleTextBlock->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.34f, 0.38f, 1.0f)));
			EndSubTitleTextBlock->SetText(FText::FromString(FString::Printf(
				TEXT("생체량이 고갈되었습니다. 처치 %d/%d마리 기록이 리더보드에 등록되었습니다."), Pawn->DefeatedEnemyCount, TargetKills)));
		}
	}

	if (GameOverStatsTextBlock.IsValid())
	{
		const FString PlayerName = OwnerPC.IsValid() ? OwnerPC->GetCurrentPlayerName() : TEXT("익명 곰팡이");
		GameOverStatsTextBlock->SetText(FText::FromString(FString::Printf(
			TEXT("[%s]  시간: %02d:%02d.%d  |  처치: %d/%d마리  |  최대: %.2f m  |  미네랄: %d개"),
			*PlayerName, Mins, Secs, Tenths, Pawn->DefeatedEnemyCount, TargetKills, MaxRadiusM, Pawn->ConsumedMineralCount)));
	}

	UpdateTendrilSlotHighlight(Pawn->TendrilVisualStyle);
}

void USpaceDrifterHUDWidget::RefreshAimReticle()
{
	const ASpaceDrifterPawn* Pawn = OwnerPawn.Get();
	if (!Pawn)
	{
		bReticleTargetInRange = false;
		return;
	}

	float TargetDistM = 0.0f;
	bool bWithinV1Reach = false;
	bReticleTargetInRange = Pawn->bShowAimReticle && Pawn->EvaluateAimReticleTarget(bReticleTargetInRange, TargetDistM, bWithinV1Reach);

	const EVisibility ReticleVis = Pawn->bShowAimReticle ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
	if (ReticleDotBox.IsValid())
	{
		ReticleDotBox->SetVisibility(ReticleVis);
	}
	for (int32 i = 0; i < 4; ++i)
	{
		if (ReticleTickBorders[i].IsValid())
		{
			ReticleTickBorders[i]->SetVisibility(ReticleVis);
		}
	}

	const bool bIsLatched = Pawn->HasLatchedTarget();
	// 상태별 직관적 색상:
	// - 락온 중(LOCKED): 골드 앰버
	// - V1 긴촉수 사거리 안("이제 써!" 상태): 강렬한 네온 민트/시안 + 도트 확대
	// - 사거리 밖 원거리 측정 중: 은은한 스카이블루/화이트
	const FLinearColor DotColor = bIsLatched
		? FLinearColor(1.0f, 0.86f, 0.28f, 1.0f)
		: (bWithinV1Reach
			? FLinearColor(0.18f, 1.0f, 0.72f, 1.0f)
			: (bReticleTargetInRange
				? FLinearColor(0.70f, 0.84f, 0.98f, 0.88f)
				: FLinearColor(0.90f, 0.96f, 1.0f, 0.72f)));

	const FLinearColor TickColor = bIsLatched
		? FLinearColor(1.0f, 0.86f, 0.28f, 0.95f)
		: (bWithinV1Reach
			? FLinearColor(0.18f, 1.0f, 0.72f, 0.95f)
			: (bReticleTargetInRange
				? FLinearColor(0.70f, 0.84f, 0.98f, 0.65f)
				: FLinearColor(0.75f, 0.88f, 0.98f, 0.30f)));

	if (ReticleDotBox.IsValid())
	{
		const float DotSize = bIsLatched ? 9.0f : (bWithinV1Reach ? 8.0f : 5.0f);
		ReticleDotBox->SetWidthOverride(DotSize);
		ReticleDotBox->SetHeightOverride(DotSize);
	}
	if (ReticleDotBorder.IsValid())
	{
		ReticleDotBorder->SetBorderBackgroundColor(DotColor);
	}
	for (int32 i = 0; i < 4; ++i)
	{
		if (ReticleTickBorders[i].IsValid())
		{
			ReticleTickBorders[i]->SetBorderBackgroundColor(TickColor);
		}
	}
	if (ReticleRangeTextBlock.IsValid() && bReticleTargetInRange)
	{
		ReticleRangeTextBlock->SetColorAndOpacity(FSlateColor(DotColor));
		ReticleRangeTextBlock->SetText(FText::FromString(FString::Printf(TEXT("%.1fm"), TargetDistM)));
	}
}

void USpaceDrifterHUDWidget::UpdateTendrilSlotHighlight(ETendrilVisualStyle ActiveStyle)
{
	int32 ActiveIndex = 0;
	if (ActiveStyle == ETendrilVisualStyle::GossamerSpores)
	{
		ActiveIndex = 1;
	}
	else if (ActiveStyle == ETendrilVisualStyle::SporeJet)
	{
		ActiveIndex = 2;
	}

	const ASpaceDrifterPawn* Pawn = OwnerPawn.Get();
	static const TCHAR* DefaultSlotDescs[3] = {
		TEXT("그래플 · 전염"),
		TEXT("광역 둔화 · 축소"),
		TEXT("단발 고속 대시")
	};
	static const ETendrilVisualStyle SlotStyles[3] = {
		ETendrilVisualStyle::MyceliumWeb,
		ETendrilVisualStyle::GossamerSpores,
		ETendrilVisualStyle::SporeJet
	};

	for (int32 i = 0; i < 3; ++i)
	{
		const bool bUnlocked = !Pawn || Pawn->IsTendrilStyleUnlocked(SlotStyles[i]);
		const bool bActive = bUnlocked && (i == ActiveIndex);
		const bool bUnlockGlowing = bUnlocked && (SlotUnlockGlowTimers[i] > KINDA_SMALL_NUMBER);
		const bool bLockedFlashing = !bUnlocked && (SlotLockedFlashTimers[i] > KINDA_SMALL_NUMBER);
		const float UnlockPulse = bUnlockGlowing ? (0.55f + 0.45f * FMath::Sin(TitlePulseTime * 12.0f)) : 0.0f;
		const float LockedFlashPulse = bLockedFlashing ? (0.55f + 0.45f * FMath::Sin(TitlePulseTime * 22.0f)) : 0.0f;
		const FLinearColor UnlockColor = (i == 2)
			? FLinearColor(1.0f, 0.80f, 0.22f, 1.0f)
			: FLinearColor(0.18f, 0.94f, 1.0f, 1.0f);

		float CooldownRem = 0.0f;
		if (Pawn && bUnlocked)
		{
			if (i == 1 && Pawn->NetTendrilAbility)
			{
				CooldownRem = Pawn->NetTendrilAbility->GetCooldownRemainingSeconds();
			}
			else if (i == 2 && Pawn->JetTendrilAbility)
			{
				CooldownRem = Pawn->JetTendrilAbility->GetCooldownRemainingSeconds();
			}
		}
		const bool bCoolingDown = bUnlocked && (CooldownRem > 0.05f);

		if (SlotAccentBorders[i].IsValid())
		{
			if (!bUnlocked)
			{
				SlotAccentBorders[i]->SetBorderBackgroundColor(bLockedFlashing
					? FLinearColor(1.0f, FMath::Lerp(0.20f, 0.56f, LockedFlashPulse), 0.14f, 0.98f)
					: FLinearColor(0.14f, 0.16f, 0.22f, 0.45f));
			}
			else if (bUnlockGlowing)
			{
				SlotAccentBorders[i]->SetBorderBackgroundColor(FLinearColor(UnlockColor.R, UnlockColor.G, UnlockColor.B, 0.70f + 0.30f * UnlockPulse));
			}
			else
			{
				SlotAccentBorders[i]->SetBorderBackgroundColor(bCoolingDown
					? FLinearColor(0.95f, 0.56f, 0.18f, 0.90f)
					: (bActive
						? FLinearColor(0.24f, 0.96f, 0.76f, 1.0f)
						: FLinearColor(0.14f, 0.20f, 0.28f, 0.65f)));
			}
		}
		if (SlotBorders[i].IsValid())
		{
			if (!bUnlocked)
			{
				SlotBorders[i]->SetBorderBackgroundColor(bLockedFlashing
					? FLinearColor(0.26f * LockedFlashPulse + 0.08f, 0.03f, 0.04f, 0.92f)
					: FLinearColor(0.02f, 0.03f, 0.05f, 0.56f));
			}
			else if (bUnlockGlowing)
			{
				SlotBorders[i]->SetBorderBackgroundColor(FLinearColor(
					UnlockColor.R * 0.22f * UnlockPulse + 0.05f,
					UnlockColor.G * 0.22f * UnlockPulse + 0.09f,
					UnlockColor.B * 0.22f * UnlockPulse + 0.12f,
					0.94f));
			}
			else
			{
				SlotBorders[i]->SetBorderBackgroundColor(bActive
					? FLinearColor(0.08f, 0.26f, 0.24f, 0.92f)
					: FLinearColor(0.03f, 0.05f, 0.09f, 0.78f));
			}
		}
		if (SlotTitleTexts[i].IsValid())
		{
			if (!bUnlocked)
			{
				SlotTitleTexts[i]->SetColorAndOpacity(FSlateColor(FLinearColor(0.42f, 0.48f, 0.56f, 0.62f)));
			}
			else if (bUnlockGlowing)
			{
				SlotTitleTexts[i]->SetColorAndOpacity(FSlateColor(UnlockColor));
			}
			else
			{
				SlotTitleTexts[i]->SetColorAndOpacity(FSlateColor(bActive
					? FLinearColor(0.96f, 1.0f, 0.98f, 1.0f)
					: FLinearColor(0.65f, 0.74f, 0.84f, 0.82f)));
			}
		}
		if (SlotDescTexts[i].IsValid())
		{
			if (!bUnlocked && Pawn)
			{
				const int32 Req = Pawn->GetRequiredDefeatsForStyle(SlotStyles[i]);
				SlotDescTexts[i]->SetText(FText::FromString(FString::Printf(TEXT("LOCK · 처치 %d/%d"), Pawn->DefeatedEnemyCount, Req)));
				SlotDescTexts[i]->SetColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.52f, 0.34f, 0.85f)));
			}
			else if (bUnlockGlowing)
			{
				SlotDescTexts[i]->SetText(FText::FromString(TEXT("★ 해금 완료!")));
				SlotDescTexts[i]->SetColorAndOpacity(FSlateColor(UnlockColor));
			}
			else if (bCoolingDown)
			{
				SlotDescTexts[i]->SetText(FText::FromString(FString::Printf(TEXT("쿨타임 %.1fs"), CooldownRem)));
				SlotDescTexts[i]->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.72f, 0.28f, 1.0f)));
			}
			else
			{
				SlotDescTexts[i]->SetText(FText::FromString(DefaultSlotDescs[i]));
				SlotDescTexts[i]->SetColorAndOpacity(FSlateColor(bActive
					? FLinearColor(0.68f, 0.96f, 0.86f, 0.95f)
					: FLinearColor(0.45f, 0.56f, 0.68f, 0.75f)));
			}
		}
	}
}

EVisibility USpaceDrifterHUDWidget::GetTitleOverlayVisibility() const
{
	const ASpaceDrifterPlayerController* PC = OwnerPC.Get();
	const ASpaceDrifterPawn* Pawn = OwnerPawn.Get();
	if (Pawn && Pawn->IsRunEnded())
	{
		return EVisibility::Collapsed;
	}
	return (PC && PC->IsInTitleScreen()) ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility USpaceDrifterHUDWidget::GetInGameHUDVisibility() const
{
	const ASpaceDrifterPlayerController* PC = OwnerPC.Get();
	const ASpaceDrifterPawn* Pawn = OwnerPawn.Get();
	if (Pawn && Pawn->IsRunEnded())
	{
		return EVisibility::Collapsed;
	}
	if (PC && PC->IsInTitleScreen())
	{
		return EVisibility::Collapsed;
	}
	return EVisibility::SelfHitTestInvisible;
}

EVisibility USpaceDrifterHUDWidget::GetGameOverOverlayVisibility() const
{
	const ASpaceDrifterPawn* Pawn = OwnerPawn.Get();
	return (Pawn && Pawn->IsRunEnded()) ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility USpaceDrifterHUDWidget::GetPauseMenuOverlayVisibility() const
{
	const ASpaceDrifterPlayerController* PC = OwnerPC.Get();
	const ASpaceDrifterPawn* Pawn = OwnerPawn.Get();
	if (!PC || !PC->IsPauseMenuOpen() || (Pawn && Pawn->IsRunEnded()))
	{
		return EVisibility::Collapsed;
	}
	return EVisibility::Visible;
}

void USpaceDrifterHUDWidget::RefreshPauseMenuSettingsDisplay()
{
	static constexpr float ScalePresets[NumUIScalePresets] = { 0.75f, 0.90f, 1.00f, 1.15f, 1.30f };
	static const TCHAR* QualityNames[NumQualityLevels] = { TEXT("낮음"), TEXT("보통"), TEXT("높음"), TEXT("에픽") };

	const FLinearColor ActiveBg(0.14f, 0.44f, 0.38f, 0.98f);
	const FLinearColor InactiveBg(0.08f, 0.12f, 0.19f, 0.94f);
	const FLinearColor ActiveText(0.94f, 1.0f, 0.98f, 1.0f);
	const FLinearColor InactiveText(0.72f, 0.80f, 0.90f, 0.90f);

	int32 ActiveScaleIdx = 2;
	float BestScaleDiff = FLT_MAX;
	for (int32 i = 0; i < NumUIScalePresets; ++i)
	{
		const float Diff = FMath::Abs(CurrentUIScale - ScalePresets[i]);
		if (Diff < BestScaleDiff)
		{
			BestScaleDiff = Diff;
			ActiveScaleIdx = i;
		}
	}

	for (int32 i = 0; i < NumUIScalePresets; ++i)
	{
		const bool bSelected = (i == ActiveScaleIdx);
		if (PauseUIScaleBtnBorders[i].IsValid())
		{
			PauseUIScaleBtnBorders[i]->SetBorderBackgroundColor(bSelected ? ActiveBg : InactiveBg);
		}
		if (PauseUIScaleBtnTexts[i].IsValid())
		{
			PauseUIScaleBtnTexts[i]->SetColorAndOpacity(FSlateColor(bSelected ? ActiveText : InactiveText));
		}
	}

	int32 TextureQuality = 2;
	int32 OverallQuality = 2;
	if (UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		TextureQuality = FMath::Clamp(Settings->GetTextureQuality(), 0, NumQualityLevels - 1);
		OverallQuality = Settings->GetOverallScalabilityLevel();
	}

	for (int32 i = 0; i < NumQualityLevels; ++i)
	{
		const bool bTexSelected = (i == TextureQuality);
		if (PauseTexQualityBtnBorders[i].IsValid())
		{
			PauseTexQualityBtnBorders[i]->SetBorderBackgroundColor(bTexSelected ? ActiveBg : InactiveBg);
		}
		if (PauseTexQualityBtnTexts[i].IsValid())
		{
			PauseTexQualityBtnTexts[i]->SetColorAndOpacity(FSlateColor(bTexSelected ? ActiveText : InactiveText));
		}

		const bool bGfxSelected = (OverallQuality >= 0 && OverallQuality < NumQualityLevels && i == OverallQuality);
		if (PauseGfxQualityBtnBorders[i].IsValid())
		{
			PauseGfxQualityBtnBorders[i]->SetBorderBackgroundColor(bGfxSelected ? ActiveBg : InactiveBg);
		}
		if (PauseGfxQualityBtnTexts[i].IsValid())
		{
			PauseGfxQualityBtnTexts[i]->SetColorAndOpacity(FSlateColor(bGfxSelected ? ActiveText : InactiveText));
		}
	}

	if (PauseCurrentSettingsSummaryTextBlock.IsValid())
	{
		const TCHAR* TexLabel = QualityNames[FMath::Clamp(TextureQuality, 0, NumQualityLevels - 1)];
		const TCHAR* GfxLabel = (OverallQuality >= 0 && OverallQuality < NumQualityLevels)
			? QualityNames[OverallQuality]
			: TEXT("사용자 지정");
		PauseCurrentSettingsSummaryTextBlock->SetText(FText::FromString(FString::Printf(
			TEXT("현재 적용: UI %d%% · 텍스처 %s · 전체 그래픽 %s"),
			FMath::RoundToInt(CurrentUIScale * 100.0f),
			TexLabel,
			GfxLabel
		)));
	}
}

FReply USpaceDrifterHUDWidget::OnUIScaleSelected(int32 PresetIndex)
{
	static constexpr float ScalePresets[NumUIScalePresets] = { 0.75f, 0.90f, 1.00f, 1.15f, 1.30f };
	const int32 ClampedIndex = FMath::Clamp(PresetIndex, 0, NumUIScalePresets - 1);
	CurrentUIScale = ScalePresets[ClampedIndex];
	RefreshPauseMenuSettingsDisplay();
	return FReply::Handled();
}

FReply USpaceDrifterHUDWidget::OnTextureQualitySelected(int32 QualityLevel)
{
	const int32 ClampedLevel = FMath::Clamp(QualityLevel, 0, NumQualityLevels - 1);
	if (UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		Settings->SetTextureQuality(ClampedLevel);
		Settings->ApplySettings(false);
	}
	RefreshPauseMenuSettingsDisplay();
	return FReply::Handled();
}

FReply USpaceDrifterHUDWidget::OnGraphicsQualitySelected(int32 QualityLevel)
{
	const int32 ClampedLevel = FMath::Clamp(QualityLevel, 0, NumQualityLevels - 1);
	if (UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		Settings->SetOverallScalabilityLevel(ClampedLevel);
		Settings->ApplySettings(false);
	}
	RefreshPauseMenuSettingsDisplay();
	return FReply::Handled();
}

FReply USpaceDrifterHUDWidget::OnTitleOptionsClicked()
{
	if (ASpaceDrifterPlayerController* PC = OwnerPC.Get())
	{
		PC->SetPauseMenuOpen(true);
	}
	return FReply::Handled();
}

FReply USpaceDrifterHUDWidget::OnPauseResumeClicked()
{
	if (ASpaceDrifterPlayerController* PC = OwnerPC.Get())
	{
		PC->SetPauseMenuOpen(false);
	}
	return FReply::Handled();
}

FReply USpaceDrifterHUDWidget::OnPauseRestartClicked()
{
	if (ASpaceDrifterPlayerController* PC = OwnerPC.Get())
	{
		PC->RestartFromPauseMenu();
	}
	return FReply::Handled();
}

FReply USpaceDrifterHUDWidget::OnPauseQuitClicked()
{
	if (ASpaceDrifterPlayerController* PC = OwnerPC.Get())
	{
		PC->QuitGameFromPauseMenu();
	}
	return FReply::Handled();
}

FString USpaceDrifterHUDWidget::GetSanitizedNameInputText() const
{
	if (!NameInputTextBox.IsValid())
	{
		return FString();
	}
	FString Sanitized = NameInputTextBox->GetText().ToString();
	Sanitized.ReplaceInline(TEXT(" "), TEXT(""));
	Sanitized.ReplaceInline(TEXT("\t"), TEXT(""));
	return Sanitized.TrimStartAndEnd().Left(12);
}

void USpaceDrifterHUDWidget::OnNameTextChanged(const FText& NewText)
{
	ASpaceDrifterPlayerController* PC = OwnerPC.Get();
	if (!PC || !PC->IsInTitleScreen())
	{
		return;
	}

	const FString Raw = NewText.ToString();
	const bool bHasSpace = Raw.Contains(TEXT(" "));

	FString Sanitized = Raw;
	Sanitized.ReplaceInline(TEXT(" "), TEXT(""));
	Sanitized.ReplaceInline(TEXT("\t"), TEXT(""));

	if (Sanitized != Raw)
	{
		bPendingSanitizeNameInput = true;
	}

	if (!Sanitized.IsEmpty() && EmptyNameWarningTimer > 0.0f)
	{
		EmptyNameWarningTimer = 0.0f;
		if (NameInputFrameBorder.IsValid())
		{
			NameInputFrameBorder->SetBorderBackgroundColor(FLinearColor(0.05f, 0.10f, 0.16f, 0.92f));
		}
		if (NameConfirmHintTextBlock.IsValid())
		{
			NameConfirmHintTextBlock->SetText(FText::FromString(TEXT("이름 입력 후 스페이스바로 바로 시작!")));
			NameConfirmHintTextBlock->SetColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.85f, 0.95f, 0.85f)));
		}
	}

	PC->SetCurrentPlayerName(Sanitized, false);
	if (bHasSpace)
	{
		bPendingTitleStartFromSpace = true;
	}
}

void USpaceDrifterHUDWidget::OnNameTextCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
	ASpaceDrifterPlayerController* PC = OwnerPC.Get();
	// 이미 게임이 시작된 뒤 포커스 이탈(OnUserMovedFocus)로 호출되는 커밋이 플레이어 이름을 덮어쓰지 않도록 차단
	if (!PC || !PC->IsInTitleScreen())
	{
		return;
	}

	FString Sanitized = NewText.ToString();
	Sanitized.ReplaceInline(TEXT(" "), TEXT(""));
	Sanitized.ReplaceInline(TEXT("\t"), TEXT(""));
	Sanitized = Sanitized.TrimStartAndEnd().Left(12);

	if (!Sanitized.IsEmpty())
	{
		PC->SetCurrentPlayerName(Sanitized, false);
	}
}

FReply USpaceDrifterHUDWidget::OnResetLeaderboardClicked()
{
	if (USpaceDrifterLeaderboardSave* SaveObj = USpaceDrifterLeaderboardSave::LoadOrCreate())
	{
		SaveObj->ClearAllAndSave();
	}
	RefreshLeaderboardDisplay();
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetUserFocusToGameViewport(0);
	}
	return FReply::Handled();
}

void USpaceDrifterHUDWidget::RefreshLeaderboardDisplay()
{
	PopulateLeaderboardBox(TitleLeaderboardListBox);
	PopulateLeaderboardBox(EndLeaderboardListBox);
}

void USpaceDrifterHUDWidget::PopulateLeaderboardBox(const TSharedPtr<SVerticalBox>& TargetBox)
{
	if (!TargetBox.IsValid())
	{
		return;
	}

	TargetBox->ClearChildren();

	const USpaceDrifterLeaderboardSave* SaveObj = USpaceDrifterLeaderboardSave::LoadOrCreate();
	if (!SaveObj || SaveObj->Entries.IsEmpty())
	{
		TargetBox->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 8.0f)
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.58f, 0.68f, 0.80f, 0.85f)))
				.Text(FText::FromString(TEXT("아직 등록된 기록이 없습니다.")))
			];
		return;
	}

	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("WhiteBrush");

	// 동명이인 시간순 넘버링 산출 (등록 순서 RegistrationOrder 오름차순 기준: 1차, 2차, 3차...)
	TMap<FString, TArray<int32>> NameToEntryIndices;
	for (int32 i = 0; i < SaveObj->Entries.Num(); ++i)
	{
		NameToEntryIndices.FindOrAdd(SaveObj->Entries[i].PlayerName).Add(i);
	}
	TArray<int32> DuplicateChronoNumber;
	DuplicateChronoNumber.Init(0, SaveObj->Entries.Num());
	for (TPair<FString, TArray<int32>>& Pair : NameToEntryIndices)
	{
		if (Pair.Value.Num() <= 1)
		{
			continue;
		}
		Pair.Value.Sort([SaveObj](int32 IndexA, int32 IndexB)
		{
			const int32 OrderA = SaveObj->Entries[IndexA].RegistrationOrder;
			const int32 OrderB = SaveObj->Entries[IndexB].RegistrationOrder;
			if (OrderA != OrderB)
			{
				return OrderA < OrderB;
			}
			return IndexA < IndexB;
		});
		for (int32 Seq = 0; Seq < Pair.Value.Num(); ++Seq)
		{
			DuplicateChronoNumber[Pair.Value[Seq]] = Seq + 1;
		}
	}

	const int32 ShowCount = FMath::Min(SaveObj->Entries.Num(), 5);
	for (int32 Idx = 0; Idx < ShowCount; ++Idx)
	{
		const FSpaceLeaderboardEntry& Entry = SaveObj->Entries[Idx];
		const int32 ChronoNum = DuplicateChronoNumber.IsValidIndex(Idx) ? DuplicateChronoNumber[Idx] : 0;
		const FLinearColor RankColor = (Idx == 0)
			? FLinearColor(1.0f, 0.84f, 0.28f, 1.0f)
			: (Idx == 1
				? FLinearColor(0.82f, 0.88f, 0.96f, 1.0f)
				: (Idx == 2
					? FLinearColor(0.92f, 0.62f, 0.36f, 1.0f)
					: FLinearColor(0.65f, 0.76f, 0.88f, 0.92f)));

		TSharedRef<SHorizontalBox> NameRow = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.94f, 0.98f, 1.0f, 1.0f)))
				.Text(FText::FromString(Entry.PlayerName))
			];

		// 동명이인일 때만 이름과 명확히 분리된 보조 배지(등록 시간순 번호)를 우측에 표시
		if (ChronoNum > 0)
		{
			NameRow->AddSlot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SBorder)
					.BorderImage(WhiteBrush)
					.BorderBackgroundColor(FLinearColor(0.12f, 0.26f, 0.38f, 0.92f))
					.Padding(FMargin(5.0f, 1.0f))
					[
						SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.58f, 0.88f, 1.0f, 0.96f)))
						.Text(FText::FromString(FString::Printf(TEXT("%d차"), ChronoNum)))
					]
				];
		}

		TargetBox->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 3.0f)
			[
				SNew(SBorder)
				.BorderImage(WhiteBrush)
				.BorderBackgroundColor(FLinearColor(0.07f, 0.11f, 0.17f, 0.85f))
				.Padding(FMargin(10.0f, 6.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
						.ColorAndOpacity(FSlateColor(RankColor))
						.Text(FText::FromString(FString::Printf(TEXT("#%d"), Idx + 1)))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						NameRow
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(6.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
						.ColorAndOpacity(FSlateColor(Entry.bClearedGoal
							? FLinearColor(0.32f, 1.0f, 0.80f, 1.0f)
							: FLinearColor(0.72f, 0.78f, 0.86f, 0.88f)))
						.Text(FText::FromString([&Entry]()
						{
							if (Entry.bClearedGoal)
							{
								const int32 TenthsTotal = FMath::FloorToInt(FMath::Max(Entry.ClearTimeSeconds, 0.0f) * 10.0f);
								const int32 EM = (TenthsTotal / 10) / 60;
								const int32 ES = (TenthsTotal / 10) % 60;
								const int32 ET = TenthsTotal % 10;
								return FString::Printf(TEXT("CLEAR %02d:%02d.%d"), EM, ES, ET);
							}
							return FString::Printf(TEXT("처치 %d마리 (미네랄 %d)"), Entry.DefeatedEnemies, Entry.AbsorbedMinerals);
						}()))
					]
				]
			];
	}
}

EVisibility USpaceDrifterHUDWidget::GetBannerVisibility() const
{
	if (CachedBannerMessage.IsEmpty())
	{
		return EVisibility::Collapsed;
	}
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	return (Now <= BannerExpireTime) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
}

EVisibility USpaceDrifterHUDWidget::GetReticleTargetVisibility() const
{
	const ASpaceDrifterPawn* Pawn = OwnerPawn.Get();
	if (!Pawn || !Pawn->bShowAimReticle || !Pawn->bShowReticleTargetDistance)
	{
		return EVisibility::Collapsed;
	}
	return bReticleTargetInRange ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
}
