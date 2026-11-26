# FungalSpaceConquest 프로젝트 가이드

범용 언리얼 규칙(툴체인 `ue-on`, Hot Reload·재시작 절차, MCP, 파라미터 외부화)은 전역 `unreal-engine` 스킬을 따른다. 이 문서는 이 프로젝트에만 해당하는 사실과 불변 조건을 담는다. 객체 모델링·설계 원칙·확장 체크리스트는 [ARCHITECTURE.md](./ARCHITECTURE.md)를 먼저 읽는다.

## 아키텍처 불변 조건

- **모듈·프로젝트명 `UEStudy` 유지**: `UEStudy.uproject`, `Source/UEStudy/`, `Config/DefaultEngine.ini`의 `/Script/UEStudy` 리다이렉트는 UEStudy 워크스페이스에서 분리될 때의 이름 그대로다. BP·레벨 에셋이 `/Script/UEStudy` 모듈을 참조하므로 리네임하지 않는다.
- **코드 위치**: 게임 코드는 `Source/UEStudy/Sandbox/MiniProject/`, 시작 맵은 `/Game/Sandbox/MiniProject/Maps/Lvl_Main`이다.
