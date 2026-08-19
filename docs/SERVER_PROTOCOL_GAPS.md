# 서버 프로토콜 보완 사항

Godot 클라이언트를 실제 C++ 서버에 연결하면서 확인한 보완 사항이다.
클라이언트 임시 구현으로 숨기지 않고 서버 기능을 추가할 때 확인하기 위해 기록한다.

## 1. 파티를 생성하거나 가입할 TCP 요청이 없음

서버 내부에는 `PartyManager::CreateParty()`와 `JoinParty()`가 구현되어 있지만
클라이언트가 호출할 수 있는 패킷과 `PacketDispatcher` 핸들러가 없다.

현재 실제 TCP 세션은 파티에 들어갈 방법이 없으므로 `EnterDungeonRequest`를 보내면
항상 `EnterDungeonResult::NotInParty`가 반환된다.

최소 보완안:

- 1인 시연을 우선한다면 채널 입장 성공 시 1인 파티를 자동 생성한다.
- 정식 흐름을 만든다면 CreateParty, JoinParty, LeaveParty 요청/응답을 추가한다.
- 파티 ID, 파티장, 멤버 Session ID 목록을 내려주는 PartySnapshot 응답을 추가한다.

## 2. 클라이언트가 자신의 Session ID를 받을 수 없음

`SessionManager`는 접속 시 `SessionId`를 만들지만 TCP 응답으로 전달하지 않는다.
반면 UDP의 `UdpHello`에는 `session_id`, `dungeon_id`, `token`이 모두 필요하다.

따라서 던전 생성과 UDP 포트·토큰 할당에 성공하더라도 클라이언트는 올바른
`UdpHello`를 만들 수 없다. 현재 Godot 클라이언트에는 이 문제를 드러내기 위해
Session ID 임시 입력란을 두었다.

권장 보완안:

- 로그인 성공 응답에 현재 세션의 `sessionId`를 포함한다.
- 또는 EnterDungeonResponse와 DungeonConnectionInfoResponse에 `sessionId`를 포함한다.
- 장기적으로는 로그인 후 발급되는 명시적인 Player/Character ID와 네트워크 Session ID를
  구분해서 관리한다.

## 3. UDP 인증 성공 응답이 없음

서버는 유효한 `UdpHello`를 받으면 endpoint를 내부에 등록하지만 클라이언트에
인증 결과를 보내지 않는다. 클라이언트는 첫 `DungeonSnapshot`을 받을 때까지
다음 상태를 구분할 수 없다.

- 인증 성공 후 다른 파티원을 기다리는 중
- 잘못된 Session ID 또는 token
- 잘못된 dungeon ID
- UDP 방화벽 또는 endpoint 문제

권장 보완안:

- UdpWelcome 또는 UdpHelloAck 메시지를 추가한다.
- 결과 코드와 서버 tick을 포함하고, 인증 실패도 제한된 형태로 응답한다.

## 4. 던전 카탈로그 조회 요청이 없음

서버에는 던전 템플릿 `1001: Forest`가 있지만 클라이언트가 카탈로그를 조회할
TCP 요청이 없다. 현재 클라이언트는 템플릿 ID 1001을 기본값으로 사용한다.

권장 보완안:

- DungeonCatalogRequest/Response를 추가한다.
- 응답에는 template ID, 표시 이름, 권장 인원과 입장 가능 여부를 포함한다.

## 5. 스냅샷에 룸 정적 정보가 없음

DungeonSnapshot은 플레이어와 적의 room ID 및 좌표를 보내지만 룸 크기,
장애물, 포탈 위치는 보내지 않는다. 클라이언트는 현재 서버 카탈로그에 있는
룸 1의 1200x500, 룸 2의 1500x600 크기를 임시로 알고 있어야 한다.

권장 보완안:

- 던전 입장 시 DungeonStaticData를 한 번 전송한다.
- 정적 데이터에는 룸 크기, 포탈, 장애물, 적 종류와 표시용 템플릿 정보를 넣는다.
- 30Hz DungeonSnapshot에는 계속 변하는 상태만 유지한다.

## 6. 전투 UI에 필요한 결과 데이터가 부족함

서버 내부에는 MP, 쿨타임, 스킬 단계와 적 HP가 있지만 현재 UDP 스냅샷에서
클라이언트가 받는 플레이어 정보는 room ID와 position뿐이다.

추후 전투 시연에 필요한 데이터:

- 플레이어 HP/MP와 주요 상태이상
- 스킬 쿨타임 또는 사용 가능 여부
- 공격 승인/거부 결과
- 피격, 데미지, 사망과 같은 일회성 전투 이벤트
- 스킬 Startup/Active/Recovery 상태

지속 상태는 스냅샷에, 한 번만 보여야 하는 타격·데미지는 별도 이벤트 메시지에
두는 편이 적합하다.

## 구현 우선순위

1. Session ID를 클라이언트에 전달
2. 1인 파티 자동 생성 또는 파티 TCP 프로토콜 추가
3. UdpHelloAck 추가
4. 던전 카탈로그와 정적 룸 데이터 추가
5. 전투 상태와 이벤트 메시지 추가

앞의 두 항목이 해결되어야 현재 Godot 클라이언트가 실제 서버를 통해
TCP 로비에서 UDP 던전까지 끊김 없이 진입할 수 있다.
