# Fungal Space Conquest (UE 5.9 Mini-Project)

무중력 우주 공간에서 균류 세포로 성장하며 **규칙 기반 클리어 조건(기본: 포식자 6마리 AND 미네랄 200개)**을 달성하는 Unreal Engine 5.9 C++ 미니게임입니다.

## 주요 특징
- **규칙 기반 클리어 조건 & 리더보드**: `Config=Game` 설정에서 AND/OR 그룹 조건을 편집할 수 있으며, 클리어 기록은 최대 20개 저장·5단계 키 정렬 후 HUD에 TOP 5 표시
- **3종 다형성 촉수 능력 (`USpaceTendrilAbility`)**:
  - `[1]` **V1 긴 촉수**: 소행성/사냥 가능 포식자 락온 · 길이 비례 인력 · 표면 전염 및 폭발 흡수
  - `[2]` **V2 그물 촉수** *(포식자 2마리 처치 시 해금)*: 미네랄 대량 포획 · 광역 포식자 영구 둔화(속도 35%, 재적중 시 ×0.85, 하한 20%) 및 크기 -15% 영구 축소
  - `[3]` **V3 긴급 탈출** *(포식자 3마리 처치 시 해금)*: 단발 고속 임펄스 탈출
- **0-Tick 오브젝트 풀링 & AI**:
  - 소행성 2-Tier 물리(활성 680개 중 반경 3500cm 내 최대 48개 Chaos 강체) 및 플레이어 크기 변화에 따른 즉시 동적 스케일링
  - 포식자 공격 티켓(최대 2마리 돌진 + 나머지 포위 링 선회) 및 최대 도달 크기 기준 스폰

## Windows (UE 5.9) 실행 방법
1. 저장소 클론:
   ```bash
   git clone https://github.com/pargame/FungalSpaceConquest.git
   ```
2. `UEStudy.uproject` 우클릭 → **Generate Visual Studio project files** (또는 바로 더블클릭 후 C++ 모듈 빌드 승인)
3. Unreal Editor 5.9가 열리면 기본 시작 맵(`/Game/Sandbox/MiniProject/Maps/Lvl_Main`)에서 **Play (PIE)** 실행

> 블루프린트/레벨 에셋의 `/Script/UEStudy` C++ 모듈 참조 호환을 위해 프로젝트 파일 및 모듈명은 `UEStudy`를 유지합니다.
> 상세 아키텍처는 [Docs/ARCHITECTURE.md](Docs/ARCHITECTURE.md)를 참고하세요.

## 라이선스

이 저장소의 코드와 직접 제작한 에셋은 [MIT License](LICENSE)를 따릅니다. Unreal Engine 엔진과 Epic Games가 제공한 콘텐츠는 이 라이선스에 포함되지 않으며 [Unreal Engine EULA](https://www.unrealengine.com/eula)를 따릅니다.
