# Benchmark Guide

이 문서는 재현 가능한 부하 검증 절차와 결과 기록 형식을 정의합니다. 아직 실행하지
않은 처리량이나 지연 수치를 예시 값으로 채우지 않습니다.

## 프로필

`lobby` 프로필은 각 가상 계정이 실제 네트워크 경로를 다음 순서로 통과합니다.

```text
TLS 연결 → 계정 인증 → 캐릭터 목록/선택 → 일회성 티켓 발급
→ 게임 TCP 연결/로그인 → 채널 목록/입장
```

로비 입장 후에는 지정한 시간 동안 TCP 연결을 유지하면서 1초 간격으로 채널 목록을
조회합니다. `concurrency`는 준비 작업 수가 아니라 이 유지 구간을 포함해 동시에 살아
있는 최대 세션 수입니다.

`dungeon` 프로필은 정확히 4계정씩 다음 경로를 추가로 수행합니다.

```text
파티 생성/가입 → 파티장의 던전 생성 → 파티원별 접속 정보 조회
→ 정적 데이터 조회 → 개인 token을 사용한 UDP Hello/Ack → heartbeat 유지
```

던전 프로필의 `accounts`는 4의 배수여야 하고 `concurrency`는 4 이상이어야 합니다.
완전한 4인 파티만 실행하므로 동시성 값이 4의 배수가 아니면 가장 가까운 작은 배수로
실제 동시성이 제한됩니다. 이 프로필은 전투 자동 플레이나 던전 클리어 처리량을
측정하지 않습니다.

## 실행

다음 명령은 전용 `.demo/loadtest` SQLite DB에 계정과 캐릭터를 미리 생성하고, 자체
서명 개발 인증서의 지문을 내부에서 계산한 뒤 인증·게임 서버와 부하 클라이언트를
실행합니다. 비밀번호와 인증서 지문은 출력하지 않습니다.

```sh
./tools/run_load_test.sh \
  --profile lobby \
  --accounts 100 \
  --concurrency 20 \
  --duration 10
```

4인 UDP 던전 입장 검증:

```sh
./tools/run_load_test.sh \
  --profile dungeon \
  --accounts 40 \
  --concurrency 20 \
  --duration 10
```

기본 개발 비밀번호 대신 환경변수를 사용할 수 있습니다.

```sh
DNF_LOADTEST_PASSWORD='local-only-password' \
  ./tools/run_load_test.sh --accounts 100 --concurrency 20 --duration 10
```

한 번 생성한 전용 DB의 계정 접두사나 비밀번호를 바꾸려면 `.demo/loadtest`를 지우고
다시 생성해야 합니다. 스크립트는 다른 DB 경로를 받지 않으며, 중단된 provisioning
표시가 있으면 자동으로 덮어쓰거나 중복 계정을 만들지 않고 중지합니다.

클라이언트만 별도로 실행할 때 사용할 수 있는 설정은 다음과 같습니다.

| CLI | 환경변수 | 범위/기본값 |
| --- | --- | --- |
| `--profile` | `DNF_LOADTEST_PROFILE` | `lobby` 또는 `dungeon` / `lobby` |
| `--accounts` | `DNF_LOADTEST_ACCOUNTS` | 1~1000 / 100 |
| `--concurrency` | `DNF_LOADTEST_CONCURRENCY` | 1~1000 / 20 |
| `--duration` | `DNF_LOADTEST_DURATION_SECONDS` | 세션별 유지 초 1~3600 / 10 |
| `--timeout` | `DNF_LOADTEST_SESSION_TIMEOUT_SECONDS` | 접속 단계 제한 초 1~300 / 30 |
| `--auth-host` | `DNF_AUTH_HOST` | `localhost` |
| `--auth-port` | `DNF_AUTH_PORT` | 7443 |
| `--account-prefix` | `DNF_LOADTEST_ACCOUNT_PREFIX` | `load_user_` |

`DNF_LOADTEST_PASSWORD`와 `DNF_AUTH_CERT_FINGERPRINT`는 프로세스 목록에 노출될 수 있는
CLI 인자로 받지 않고 환경변수로만 전달합니다. 결과 로그에는 계정 ID, 비밀번호,
인증서 지문이나 인증 티켓을 기록하지 않습니다.

## 측정 방식

- 성공은 선택한 프로필의 마지막 단계까지 완료하고 유지 구간도 오류 없이 끝난
  세션입니다.
- 각 단계와 `EndToEnd` 지연은 `Stopwatch`로 측정하며 유지 시간은 지연 분포에서
  제외합니다.
- p50, p95, p99는 정렬된 표본의 nearest-rank 값입니다.
- throughput은 유지 시간을 포함한 전체 벽시계 시간 동안 완료된 세션 수입니다.
- 실패는 계정 정보나 오류 메시지 원문 없이 `stage/exception type`으로 집계합니다.
- 기본 서버에는 100명 정원의 채널이 3개 있으므로 동시 성공 로비 세션의 상한은
  300명입니다. 그 이상을 요청했을 때의 `ChannelJoin` 실패는 측정 대상입니다.

## 실행 환경 기록

| 항목 | 값 |
| --- | --- |
| 측정 날짜 | — |
| Git commit | — |
| OS / kernel | — |
| CPU / core 수 | — |
| RAM | — |
| compiler / build type | — |
| .NET SDK | — |
| 서버와 부하 생성기 배치 | — |
| 프로필 | — |
| 계정 수 / 동시성 / 유지 시간 | — |

## 결과

아래 표는 실제 측정 후 콘솔 요약을 옮겨 적습니다.

| 지표 | 결과 |
| --- | --- |
| 시도 / 성공 / 실패 | — |
| peak sessions | — |
| wall time | — |
| completed sessions/s | — |
| EndToEnd p50 / p95 / p99 | — |
| TLS connect p50 / p95 / p99 | — |
| Auth login p50 / p95 / p99 | — |
| Game login p50 / p95 / p99 | — |
| Channel join p50 / p95 / p99 | — |
| UDP Hello p50 / p95 / p99 (`dungeon`) | — |
| 주요 실패 단계 | — |

## 해석 시 주의점

- 기본 실행은 서버와 부하 생성기가 같은 머신을 사용하므로 네트워크 RTT와 실제 운영
  환경의 병목을 대표하지 않습니다.
- SQLite 단일 connection과 mutex, 제한된 채널 정원, 인증 scrypt 비용이 함께
  측정됩니다. 특정 구성 요소의 단독 microbenchmark가 아닙니다.
- 이 도구는 packet loss, delay, duplicate, reorder를 주입하지 않습니다. 네트워크
  결함 시험은 별도 단계로 기록해야 합니다.
- 서버 CPU, RSS, DB queue depth, tick p95/p99와 snapshot bandwidth는 현재 클라이언트
  요약에 포함되지 않습니다. 해당 서버 metric을 추가하기 전에는 클라이언트 지연만으로
  병목 원인을 단정하면 안 됩니다.
