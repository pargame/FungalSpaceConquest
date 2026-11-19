# Fungal Space Conquest (UE 5.9 Mini-Project)

무중력 우주 공간에서 균류 세포로 성장하며 **포식자 곰팡이 10마리를 최단 시간 안에 역포식**하는 Unreal Engine 5.9 C++ 미니게임입니다.

## 주요 특징
- **10마리 최단 시간 처치 타임어택**: 포식자 10마리 처치 시 클리어 타임 기록 및 로컬 TOP 5 리더보드 자동 저장
- **3종 다형성 촉수 능력 (`USpaceTendrilAbility`)**:
  - `[1]` **V1 긴 촉수**: 소행성/사냥 가능 포식자 락온 · 길이 비례 인력 · 표면 전염 및 폭발 흡수
  - `[2]` **V2 그물 촉수** *(포식자 2마리 처치 시 해금)*: 미네랄 대량 포획 · 광역 포식자 2.5초 둔화 및 크기 -15% 영구 축소
  - `[3]` **V3 긴급 탈출** *(포식자 5마리 처치 시 해금)*: 단발 고속 임펄스 탈출
- **0-Tick 오브젝트 풀링 & AI**:
  - 소행성 2-Tier 물리(Chaos ↔ Kinematic) 및 플레이어 크기 비례 0ms 동적 스케일링
  - 포식자 공격 티켓(최대 2마리 돌진 + 나머지 포위 링 선회) 및 최대 도달 크기 기준 스폰

## Windows (UE 5.9) 실행 방법
1. 저장소 클론:
   ```bash
   git clone https://github.com/pargame/FungalSpaceConquest.git
   ```
2. `UEStudy.uproject` 우클릭 → **Generate Visual Studio project files** (또는 바로 더블클릭 후 C++ 모듈 빌드 승인)
3. Unreal Editor 5.9가 열리면 기본 시작 맵(`/Game/Sandbox/MiniProject/Maps/Lvl_Main`)에서 **Play (PIE)** 실행

> 블루프린트/레벨 에셋의 `/Script/UEStudy` C++ 모듈 참조 호환을 위해 프로젝트 파일 및 모듈명은 `UEStudy`를 유지합니다.
> 상세 아키텍처는 [ARCHITECTURE.md](Source/UEStudy/Sandbox/MiniProject/ARCHITECTURE.md)를 참고하세요.
