\newpage

## Test

**Dependencies**

To install all the required dependencies, simply run `<PATH_TO_PGEXPORTER>/pgexporter/test/check.sh setup`. You need to install docker or podman
separately. The script currently only works on Linux system (we recommend Fedora 39+).

**Running Tests**

To run the tests, simply run `<PATH_TO_PGEXPORTER>/pgexporter/test/check.sh`. The script will build a PostgreSQL 17 image the first time you run it,
and start a docker/podman container using the image (so make sure you at least have one of them installed and have the corresponding container engine started).
The containerized postgres server will have a `pgexporter` user with the `pg_monitor` role granted, and the `pg_stat_statements` extension enabled.

The script then starts pgexporter and runs tests in your local environment. The tests are run locally so that you may leverage stdout to debug and
the testing environment won't run into weird container environment issues, and so that we can reuse the installed dependencies and cmake cache to speed up development
and debugging.

All the configuration, logs, coverage reports and data will be in `/tmp/pgexporter-test/`, and a cleanup will run whether
the script exits normally or not. pgexporter will be force shutdown if it doesn't terminate normally.
So don't worry about your local setup being tampered. The container will be stopped and removed when the script exits or is terminated.

**Running Specific Test Cases or Modules**

To run one particular test case or module, use `<PATH_TO_PGEXPORTER>/test/check.sh -t <test_case_name>` or `<PATH_TO_PGEXPORTER>/test/check.sh -m <module_name>`. Alternatively, if the test environment is already set up by a previous run of `check.sh`, you can run the test binary directly: `<PATH_TO_PGEXPORTER>/build/test/pgexporter-test -t <test_case_name>` or `<PATH_TO_PGEXPORTER>/build/test/pgexporter-test -m <module_name>` (with the same environment variables set).

It is recommended that you **ALWAYS** run tests before raising PR.

**MCTF Framework Overview**

MCTF (Minimal C Test Framework) is pgexporter's custom test framework designed for simplicity and ease of use.

**What MCTF Can Do:**

- **Automatic test registration** – Tests are automatically registered via the `MCTF_TEST` macro
- **Module organization** – Module names are automatically extracted from file names (e.g. `test_cli.c` → module `cli`)
- **Flexible assertions** – Assert macros with optional printf-style error messages
- **Test filtering** – Run tests by name pattern (`-t`) or by module (`-m`)
- **Test skipping** – Skip tests conditionally using `MCTF_SKIP()` when prerequisites aren't met
- **Lifecycle hooks** – Automatic per-test and per-module setup/teardown via `MCTF_TEST_SETUP`, `MCTF_TEST_TEARDOWN`, `MCTF_MODULE_SETUP`, `MCTF_MODULE_TEARDOWN`
- **Config snapshot/restore** – `pgexporter_test_config_save()` / `pgexporter_test_config_restore()` to isolate shared-memory config changes between tests
- **Cleanup pattern** – Structured cleanup using goto labels for resource management
- **Error tracking** – Automatic error tracking with line numbers and custom error messages
- **Multiple assertion types** – Various assertion macros (`MCTF_ASSERT`, `MCTF_ASSERT_PTR_NONNULL`, `MCTF_ASSERT_INT_EQ`, `MCTF_ASSERT_STR_EQ`, etc.)

**What MCTF Cannot Do (Limitations):**

- **No parameterized tests** – Tests cannot be parameterized (each variation needs a separate test function)
- **No parallel or async execution** – Tests run sequentially and synchronously
- **No built-in timeouts** – No framework-level test timeouts (rely on OS-level signals or manual timeouts)
- **No test organization beyond modules** – No test suites, groups, tags, or metadata beyond module names extracted from filenames

**Add Testcases**

To add an additional testcase, go to the [testcases](https://github.com/pgexporter/pgexporter/tree/main/test/testcases) directory inside the `pgexporter` project. Create a `.c` file that contains the test and use the `MCTF_TEST()` macro to define your test. Tests are automatically registered and module names are extracted from file names.

**Lifecycle Hooks**

MCTF provides four self-registering lifecycle hook macros (same `__attribute__((constructor))` pattern as `MCTF_TEST`):

| Macro | When it runs | Use case |
|---|---|---|
| `MCTF_TEST_SETUP(module)` | Before **each** test | Allocate per-test resources |
| `MCTF_TEST_TEARDOWN(module)` | After **each** test (always) | Free per-test resources |
| `MCTF_MODULE_SETUP(module)` | Once before the first test | Start a daemon / open a connection |
| `MCTF_MODULE_TEARDOWN(module)` | Once after the last test | Stop a daemon / close a connection |

`module` must match the name derived from the filename (e.g. `test_cache.c` → `cache`).

Use `MCTF_TEST_SETUP/TEARDOWN` when each test needs a clean, isolated environment (e.g. private `shmem`). Use `MCTF_MODULE_SETUP/TEARDOWN` when setup is expensive and safe to share across all tests in the module. They can be combined.

**Config snapshot/restore**

Because all tests share the same process and `shmem`, config mutations in one test leak into the next. Use `pgexporter_test_config_save()` and `pgexporter_test_config_restore()` (declared in `tscommon.h`) inside `MCTF_TEST_SETUP/TEARDOWN` to snapshot and roll back `struct configuration` around each test:

```c
MCTF_TEST_SETUP(mymodule)
{
   pgexporter_test_config_save();
   pgexporter_memory_init();
}

MCTF_TEST_TEARDOWN(mymodule)
{
   pgexporter_memory_destroy();
   pgexporter_test_config_restore();
}
```

**Example test structure:**

```c
#include <mctf.h>
#include <tsclient.h>

MCTF_TEST(test_my_feature)
{
   int result = some_function();
   MCTF_ASSERT(result == 0, cleanup, "function should return 0");

cleanup:
   MCTF_FINISH();
}
```

**MCTF_ASSERT usage:** The `MCTF_ASSERT` macro supports optional error messages with printf-style formatting.

- **Without message:** `MCTF_ASSERT(condition, cleanup);` – No error message displayed
- **With simple message:** `MCTF_ASSERT(condition, cleanup, "error message");`
- **With formatted message:** `MCTF_ASSERT(condition, cleanup, "got %d, expected 0", value);`

Format arguments (e.g. `value`) are optional and only needed when the message contains format specifiers (`%d`, `%s`, etc.). Multiple format arguments: `MCTF_ASSERT(a == b, cleanup, "expected %d but got %d", expected, actual);`

**Test Directory**

After running the tests, you will find:

* **pgexporter log:** `/tmp/pgexporter-test/log/`
* **HTML test report:** `/tmp/pgexporter-test/log/pgexporter-test-report.html` (generated after each run)
* **postgres log:** `/tmp/pgexporter-test/pg_log/`, the log level is set to debug5.
* **code coverage reports:** `/tmp/pgexporter-test/coverage/`

If you need to create a directory runtime, create it under `/tmp/pgexporter-test/base/`, which also contains the `conf/` directory with pgexporter configuration files.
Base directory will be cleaned up after tests are done. In `tscommon.h` you will find `TEST_BASE_DIR` and other global variables holding corresponding directories,
fetched from environment variables.

**Cleanup**

`<PATH_TO_PGEXPORTER>/pgexporter/test/check.sh clean` will remove the testing directory and the built image. If you are using docker, chances are it eats your
disk space secretly, in that case consider cleaning up using `docker system prune --volume`. Use with caution though as it
nukes all the docker volumes.

**Port**

By default, the container exposes port 6432 for pgexporter to connect to. This can be changed by `export PGEXPORTER_TEST_PORT=<your-port>` before running `check.sh`. Or you
may also run `PGEXPORTER_TEST_PORT=<your-port> ./check.sh`.

**Configuration**

| Name                 | Default | Value           | Description                                          |
|----------------------|---------|-----------------|------------------------------------------------------|
| PGEXPORTER_TEST_PORT | 6432    | port number     | The port pgexporter uses to connect to the database |
