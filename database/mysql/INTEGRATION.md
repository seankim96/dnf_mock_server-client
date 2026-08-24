# Optional MySQL integration profile

SQLite remains the default database. The MySQL repository is only compiled by
the `mysql` and `mysql-integration` CMake presets.

The optional target requires Boost 1.92 or newer. This is the minimum supported
and verified API profile for the implementation's Boost.MySQL `connection_pool`,
`with_params`, and Asio coroutine timeout signatures. The default SQLite build
keeps the existing Boost 1.82 minimum.

The pool is capped at 64 connections, and its configured connection-acquire and
query timeouts must be positive. `SavePlayer` is transactional and retries only
MySQL deadlock and lock-wait-timeout errors, at most three attempts.

Create a dedicated disposable test database and apply the schema. Let the
`mysql` client prompt for the password so it is not stored in shell history:

```sh
mysql --host=127.0.0.1 --user=dnf_test --password dnf_test \
  < database/mysql/schema.sql
```

The integration test reads its connection settings at runtime:

```sh
export DNF_TEST_MYSQL_HOST=127.0.0.1
export DNF_TEST_MYSQL_PORT=3306
export DNF_TEST_MYSQL_USER=dnf_test
export DNF_TEST_MYSQL_PASSWORD='set-outside-the-repository'
export DNF_TEST_MYSQL_DATABASE=dnf_test
export DNF_TEST_MYSQL_TLS=required

cmake --preset mysql-integration
cmake --build --preset mysql-integration
ctest --preset mysql-integration
```

`DNF_TEST_MYSQL_TLS` accepts `disabled`, `preferred`, or `required`. Use
`disabled` only for an isolated local database. When the required user or
database variables are absent, CTest reports the integration test as skipped.
