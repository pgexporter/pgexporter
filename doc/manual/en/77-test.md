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

To run one particular test case or suite (unfortunately check doesn't support running one single test at the moment),
run `CK_RUN_CASE=<test_case_name> <PATH_TO_PGEXPORTER>/pgexporter/test/check.sh` or
`CK_RUN_SUITE=<test_suite_name> <PATH_TO_PGEXPORTER>/pgexporter/test/check.sh`. Alternatively, you can first export the environment variables
and then run the script:
```
export CK_RUN_CASE=<test_case_name>
<PATH_TO_PGEXPORTER>/pgexporter/test/check.sh
```

The environment variables will be automatically unset when the test is finished or aborted.

It is recommended that you **ALWAYS** run tests before raising PR.

**Add Testcases**

To add an additional testcase, go to [testcases](https://github.com/pgexporter/pgexporter/tree/main/test/testcases) directory inside the `pgexporter` project.

Create a `.c` file that contains the test suite and define the suite inside `/test/include/tssuite.h`. Add the above created suite to the test runner in [runner.c](https://github.com/pgexporter/pgexporter/tree/main/test/runner.c)

**Test Directory**

After running the tests, you will find:

* **pgexporter log:** `/tmp/pgexporter-test/log/`
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

| Name                | Default | Value           | Description                                          |
|---------------------|---------|-----------------|------------------------------------------------------|
| CK_RUN_CASE         |         | test case name  | Run one single test case                             |
| CK_RUN_SUITE        |         | test suite name | Run one single test suite                            |
| PGEXPORTER_TEST_PORT| 6432    | port number     | The port name pgexporter use to connect to the db    |
