# DNF Mock Server + Client

C++20·Boost.Asio 서버와 Godot 4.6 C# 클라이언트로 구성한
횡스크롤 액션 게임 서버 포트폴리오입니다.

## 데모 실행

macOS에서 CMake 의존성과 Godot 4.6 .NET이 준비되어 있다면 다음 한 줄로
인증 서버, 게임 서버, 데모 DB, Godot 클라이언트를 함께 실행할 수 있습니다.

```sh
./tools/run_demo.sh
```

스크립트는 `.demo` 아래에 개발용 계정 `demo_player`, 캐릭터 `DemoFighter`,
자체 서명 인증서를 준비합니다. 이 데이터는 서버 운영 코드에 포함되지
않습니다.
Godot 로그인 화면의 접속값과 인증서 지문은 환경변수로 자동 입력됩니다.

클라이언트에서는 다음 순서로 진행합니다.

1. 인증 서버 로그인 후 캐릭터 선택
2. 채널 입장
3. 파티 생성
4. Forest 던전 입장
5. `WASD`로 이동하고 `J`로 Ice Slash 사용
6. 적을 처치한 뒤 오른쪽 포탈로 이동하여 클리어

클라이언트를 닫으면 함께 실행한 서버 프로세스도 종료됩니다.

## 4계정 동시 접속 데모

다음 명령은 서로 다른 DB 계정 네 개를 준비하고, 일반 Godot 클라이언트 창
네 개를 2×2로 띄웁니다. 로그인 ID와 비밀번호는 직접 입력하고, 자체 서명
인증서의 SHA-256 지문만 네 창에 자동으로 입력됩니다.

```sh
./tools/run_multiplayer_demo.sh
```

사용할 계정은 다음과 같으며 비밀번호는 모두 `demo-password`입니다.

- `demo_party_1` / `PartyFighter1`
- `demo_party_2` / `PartyFighter2`
- `demo_party_3` / `PartyFighter3`
- `demo_party_4` / `PartyFighter4`

첫 번째 창에서 파티를 생성하고 표시된 Party ID를 나머지 세 창에 입력해
가입시킵니다. 파티장은 던전을 생성하고, 파티원은 `생성된 던전 참가` 버튼을
눌러 각자의 UDP 토큰으로 입장합니다.
