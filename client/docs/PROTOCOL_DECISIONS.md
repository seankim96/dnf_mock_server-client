# 클라이언트 연동 과정의 프로토콜 설계 결정

Godot 클라이언트를 C++ 서버에 연결하면서 발견한 프로토콜 공백과 해결 방식을
기록한다. 이 문서는 미구현 목록이 아니라, 현재 구현에 이르게 된 결정 이력이다.

## 1. 파티 조작은 명시적인 TCP 요청으로 제공한다

- 문제: 서버 내부 `PartyManager`만으로는 원격 클라이언트가 파티를 생성하거나
  가입할 수 없었다.
- 선택: `CreateParty`, `JoinParty`, `LeaveParty`, `PartySnapshot` 요청·응답을
  TCP FlatBuffers 프로토콜에 추가했다.
- 대안: 채널 입장 시 항상 1인 파티를 자동 생성하는 방식은 빠른 시연에는
  단순하지만 실제 파티 흐름을 보여주지 못해 채택하지 않았다.
- 결과: 클라이언트가 파티 ID, 파티장 Session ID, 멤버 Session ID 목록을
  서버 상태 기준으로 조회할 수 있다.

구현 위치: `schemas/TcpMessage.fbs`, `PartyProtocol`, `PacketDispatcher`

## 2. 네트워크 Session ID를 로그인 응답으로 전달한다

- 문제: UDP Hello에는 `session_id`, `dungeon_id`, 개인 토큰이 필요하지만
  클라이언트가 자신의 Session ID를 알 수 없었다.
- 선택: 게임 서버 `LoginResponse`에 현재 연결의 Session ID를 포함한다.
- 대안: 던전 입장 응답에만 넣으면 UDP에는 충분하지만, 로비의 파티 상태에서도
  동일 식별자가 필요하므로 로그인 시 한 번 전달하는 편이 일관적이다.
- 결과: 성공 응답은 0이 아닌 Session ID를 전달하고, 클라이언트는 이를 UDP
  인증과 파티 표시 용도로만 사용한다.

Session ID는 연결 수명을 나타내며 영속적인 Account ID 또는 Player ID와
구분한다. 재접속 기능은 Session ID가 아닌 영속 ID를 기준으로 설계해야 한다.

구현 위치: `LoginProtocol`, `SessionAuthState`, `Main.cs`

## 3. UDP 인증 결과를 명시적인 Ack로 응답한다

- 문제: 첫 snapshot을 성공 신호로 사용하면 인증 성공 후 파티원을 기다리는
  상태와 토큰 오류, 잘못된 던전, 방화벽 문제를 구분할 수 없었다.
- 선택: `UdpHelloAck`에 결과 코드와 서버 tick을 담아 즉시 응답한다.
- 대안: 첫 snapshot을 암묵적인 성공 신호로 사용하는 방식은 오류 진단이
  어려워 채택하지 않았다.
- 결과: 클라이언트는 UDP 인증 성공 여부를 명확히 확인한 뒤 던전 상태로
  전환한다. 실패 응답은 세부 내부 정보를 과도하게 노출하지 않는 제한된
  결과 코드만 제공한다.

구현 위치: `schemas/DungeonMessage.fbs`, `DungeonProtocol`,
`DungeonUdpSession`, `DungeonUdpService.cs`

## 4. 던전 목록은 TCP 카탈로그로 조회한다

- 문제: 클라이언트가 템플릿 ID `1001`과 표시 정보를 하드코딩해야 했다.
- 선택: `DungeonCatalogRequest/Response`를 추가하고 템플릿 ID, 표시 이름,
  권장 인원, 입장 가능 여부를 전달한다.
- 대안: 실행 파일에 동일 카탈로그를 복제하면 네트워크 요청은 줄지만 서버와
  클라이언트 데이터가 쉽게 어긋난다.
- 결과: 입장 가능 여부의 최종 판단은 서버가 유지하면서 클라이언트가 서버
  카탈로그를 기준으로 던전 UI를 구성한다.

구현 위치: `DungeonCatalog`, `DungeonCatalogProtocol`, `PacketDispatcher`

## 5. 정적 룸 데이터와 동적 snapshot을 분리한다

- 문제: 매 snapshot에 룸 크기, 장애물, 포탈 같은 데이터를 넣으면 30Hz UDP
  대역폭을 낭비하고, 전달하지 않으면 클라이언트 하드코딩이 필요했다.
- 선택: 던전 입장 시 `DungeonStaticData`를 TCP로 한 번 전달하고, UDP
  `DungeonSnapshot`에는 계속 변하는 상태만 둔다.
- 대안: 모든 정보를 UDP snapshot에 반복 전송하는 방식은 단순하지만 패킷
  크기와 전송량이 불필요하게 증가한다.
- 결과: 룸 크기, 포탈, 장애물, 적 표시용 템플릿은 신뢰성 있는 TCP로 받고,
  플레이어와 적의 실시간 상태는 UDP로 갱신한다.

구현 위치: `DungeonStaticDataProtocol`, `PacketDispatcher`,
`DungeonWorldView.cs`

## 6. 지속 상태와 일회성 전투 결과를 구분한다

- 문제: 좌표만으로는 MP, HP, 쿨타임, 스킬 단계와 피격 결과를 표현할 수
  없었다.
- 선택: HP·MP·스킬 `Startup/Active/Recovery`와 적 HP처럼 현재 상태를
  snapshot에 포함한다. 공격 입력 승인 여부는 sequence 기반 결과와 서버의
  권위형 상태 변화로 검증한다.
- 대안: 클라이언트가 MP나 데미지를 계산하는 방식은 조작에 취약하므로
  채택하지 않았다.
- 결과: 서버가 스킬 카탈로그, 사거리, 히트박스, MP, 쿨타임과 데미지를
  판정하고 클라이언트는 결과를 표시한다.

피격 숫자나 사망 연출처럼 반드시 한 번만 보여야 하는 표현 이벤트는 향후
별도의 식별 가능한 combat event 채널로 확장한다. 현재 snapshot은 누락되어도
다음 상태로 복구할 수 있는 지속 상태 전달에 집중한다.

구현 위치: `DungeonCombatProcessor`, `DungeonPlayerState`,
`DungeonProtocol`, `DungeonWorldView.cs`
