#!/bin/bash
#
# Copyright (C) 2025 The pgexporter community
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this list
# of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice, this
# list of conditions and the following disclaimer in the documentation and/or other
# materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its contributors may
# be used to endorse or promote products derived from this software without specific
# prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
# THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
# OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
# TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
set -eo pipefail

# Variables
IMAGE_NAME="pgexporter-test-postgresql17-rocky9"
CONTAINER_NAME="pgexporter-test-postgresql17"

SCRIPT_DIR="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
PROJECT_DIRECTORY=$(realpath "$SCRIPT_DIR/..")
EXECUTABLE_DIRECTORY=$PROJECT_DIRECTORY/build/src
TEST_DIRECTORY=$PROJECT_DIRECTORY/build/test
TEST_PG17_DIRECTORY=$PROJECT_DIRECTORY/test/postgresql/src/postgresql17

PGEXPORTER_ROOT_DIR="/tmp/pgexporter-test"
BASE_DIR="$PGEXPORTER_ROOT_DIR/base"
COVERAGE_DIR="$PGEXPORTER_ROOT_DIR/coverage"
LOG_DIR="$PGEXPORTER_ROOT_DIR/log"
PG_LOG_DIR="$PGEXPORTER_ROOT_DIR/pg_log"

# BASE DIR holds all the run time data
CONFIGURATION_DIRECTORY=$BASE_DIR/conf
PGCONF_DIRECTORY=$BASE_DIR/pg_conf

PG_DATABASE=postgres
PG_USER_NAME=pgexporter
PG_USER_PASSWORD=pgexporter
USER=$(whoami)
MODE="dev"
PORT=6432

# Detect container engine: Docker or Podman (prefer Podman to match Makefile)
if command -v podman &> /dev/null; then
  CONTAINER_ENGINE="podman"
elif command -v docker &> /dev/null; then
  CONTAINER_ENGINE="sudo docker"
else
  echo "Neither Docker nor Podman is installed. Please install one to proceed."
  exit 1
fi

if [ -n "$PGEXPORTER_TEST_PORT" ]; then
    PORT=$PGEXPORTER_TEST_PORT
fi
echo "Container port is set to: $PORT"

cleanup() {
   echo "Clean up"
   set +e
   echo "Shutdown pgexporter"
   if [[ -f "/tmp/pgexporter.localhost.pid" ]]; then
     $EXECUTABLE_DIRECTORY/pgexporter-cli -c $CONFIGURATION_DIRECTORY/pgexporter.conf shutdown
     sleep 5
     if [[ -f "/tmp/pgexporter.localhost.pid" ]]; then
       echo "Force stop pgexporter"
       kill -9 $(pgrep pgexporter)
       rm -f "/tmp/pgexporter.localhost.pid"
     fi
   fi

   echo "Clean Test Resources"
   if [[ -d $PGEXPORTER_ROOT_DIR ]]; then
      if [[ -d $BASE_DIR ]]; then
        rm -Rf "$BASE_DIR"
      fi

      if ls "$COVERAGE_DIR"/*-*.profraw >/dev/null 2>&1; then
       echo "Generating coverage report, expect error when the binary is not covered at all"
       llvm-profdata merge -sparse $COVERAGE_DIR/*-*.profraw -o $COVERAGE_DIR/coverage.profdata

       if [[ -f "$EXECUTABLE_DIRECTORY/libpgexporter.so" ]]; then
           BIN_PATH="$EXECUTABLE_DIRECTORY"
       elif [[ -f "$EXECUTABLE_DIRECTORY/pgexporter/libpgexporter.so" ]]; then
           BIN_PATH="$EXECUTABLE_DIRECTORY/pgexporter"
       else
           echo "ERROR: Could not find libpgexporter.so in $EXECUTABLE_DIRECTORY or subdirectory."
           ls -R "$EXECUTABLE_DIRECTORY" # Debug: list files to see where they are
           exit 1
       fi

       echo "Generating $COVERAGE_DIR/coverage-report-libpgexporter.txt"
       llvm-cov report "$BIN_PATH/libpgexporter.so" \
         --instr-profile=$COVERAGE_DIR/coverage.profdata \
         --format=text > $COVERAGE_DIR/coverage-report-libpgexporter.txt 2>&1
       echo "Generating $COVERAGE_DIR/coverage-report-pgexporter.txt"
       llvm-cov report "$BIN_PATH/pgexporter" \
         --instr-profile=$COVERAGE_DIR/coverage.profdata \
         --format=text > $COVERAGE_DIR/coverage-report-pgexporter.txt 2>&1
      echo "Generating $COVERAGE_DIR/coverage-report-pgexporter-cli.txt"
       llvm-cov report "$BIN_PATH/pgexporter-cli" \
         --instr-profile=$COVERAGE_DIR/coverage.profdata \
         --format=text > $COVERAGE_DIR/coverage-report-pgexporter-cli.txt 2>&1
      echo "Generating $COVERAGE_DIR/coverage-report-pgexporter-admin.txt"
       llvm-cov report "$BIN_PATH/pgexporter-admin" \
         --instr-profile=$COVERAGE_DIR/coverage.profdata \
         --format=text > $COVERAGE_DIR/coverage-report-pgexporter-admin.txt 2>&1

      echo "Generating $COVERAGE_DIR/coverage-libpgexporter.txt"
      llvm-cov show $BIN_PATH/pgexporter/libpgexporter.so \
        --instr-profile=$COVERAGE_DIR/coverage.profdata \
        --format=text > $COVERAGE_DIR/coverage-libpgexporter.txt
      
      echo "Generating $COVERAGE_DIR/coverage-pgexporter.txt"
      llvm-cov show $BIN_PATH/pgexporter/pgexporter \
        --instr-profile=$COVERAGE_DIR/coverage.profdata \
        --format=text > $COVERAGE_DIR/coverage-pgexporter.txt
      
      echo "Generating $COVERAGE_DIR/coverage-pgexporter-cli.txt"
      llvm-cov show $BIN_PATH/pgexporter/pgexporter-cli \
        --instr-profile=$COVERAGE_DIR/coverage.profdata \
        --format=text > $COVERAGE_DIR/coverage-pgexporter-cli.txt
      
      echo "Generating $COVERAGE_DIR/coverage-pgexporter-admin.txt"
      llvm-cov show $BIN_PATH/pgexporter/pgexporter-admin \
        --instr-profile=$COVERAGE_DIR/coverage.profdata \
        --format=text > $COVERAGE_DIR/coverage-pgexporter-admin.txt

       echo "Coverage --> $COVERAGE_DIR"
     fi
     echo "Logs --> $LOG_DIR, $PG_LOG_DIR"
   else
     echo "$PGEXPORTER_ROOT_DIR not present ... ok"
   fi

   if [[ $MODE != "ci" ]]; then
     echo "Removing postgres 17 container"
     remove_postgresql_container
   fi

   echo "Unsetting environment variables"
   unset_pgexporter_test_variables

   set -e
}

build_postgresql_image() {
  echo "Building the PostgreSQL 17 image $IMAGE_NAME"
  CUR_DIR=$(pwd)
  cd $TEST_PG17_DIRECTORY
  set +e
  make clean
  set -e
  make build
  cd $CUR_DIR
}

cleanup_postgresql_image() {
  set +e
  echo "Cleanup of the PostgreSQL 17 image $IMAGE_NAME"
  CUR_DIR=$(pwd)
  cd $TEST_PG17_DIRECTORY
  make clean
  cd $CUR_DIR
  set -e
}

start_postgresql_container() {
  # Use the correct image name (Podman adds localhost/ prefix)
  local ACTUAL_IMAGE=$IMAGE_NAME
  if ! $CONTAINER_ENGINE image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
    if $CONTAINER_ENGINE image inspect "localhost/$IMAGE_NAME" >/dev/null 2>&1; then
      ACTUAL_IMAGE="localhost/$IMAGE_NAME"
    fi
  fi

  $CONTAINER_ENGINE run -p $PORT:5432 -v "$PG_LOG_DIR:/pglog:z" -v "$PGCONF_DIRECTORY:/conf:z"\
  --name $CONTAINER_NAME -d \
  -e PG_DATABASE=$PG_DATABASE \
  -e PG_USER_NAME=$PG_USER_NAME \
  -e PG_USER_PASSWORD=$PG_USER_PASSWORD \
  -e PG_LOG_LEVEL=debug5 \
  $ACTUAL_IMAGE

  echo "Checking PostgreSQL 17 container readiness"
  sleep 3
  if $CONTAINER_ENGINE exec $CONTAINER_NAME /usr/pgsql-17/bin/pg_isready -h localhost -p 5432 >/dev/null 2>&1; then
    echo "PostgreSQL 17 is ready!"
  else
    echo "Wait for 10 seconds and retry"
    sleep 10
    if $CONTAINER_ENGINE exec $CONTAINER_NAME /usr/pgsql-17/bin/pg_isready -h localhost -p 5432 >/dev/null 2>&1; then
      echo "PostgreSQL 17 is ready!"
    else
      echo "Printing container logs..."
      $CONTAINER_ENGINE logs $CONTAINER_NAME
      echo ""
      echo "PostgreSQL 17 is not ready, exiting"
      cleanup_postgresql_image
      exit 1
    fi
  fi
}

remove_postgresql_container() {
  $CONTAINER_ENGINE stop $CONTAINER_NAME 2>/dev/null || true
  $CONTAINER_ENGINE rm -f $CONTAINER_NAME 2>/dev/null || true
}

pgexporter_initialize_configuration() {
   touch $CONFIGURATION_DIRECTORY/pgexporter.conf $CONFIGURATION_DIRECTORY/pgexporter_users.conf
   echo "Creating pgexporter.conf and pgexporter_users.conf inside $CONFIGURATION_DIRECTORY ... ok"
   cat <<EOF >$CONFIGURATION_DIRECTORY/pgexporter.conf
[pgexporter]
host = localhost
metrics = 5002
bridge = 5003
bridge_endpoints = localhost:5002

log_type = file
log_level = debug5
log_path = $LOG_DIR/pgexporter.log

unix_socket_dir = /tmp/

[primary]
host = localhost
port = $PORT
user = $PG_USER_NAME
EOF
   echo "Add test configuration to pgexporter.conf ... ok"
   if [[ ! -e $HOME/.pgexporter/master.key ]]; then
     $EXECUTABLE_DIRECTORY/pgexporter-admin master-key -P $PG_USER_PASSWORD
   fi
   $EXECUTABLE_DIRECTORY/pgexporter-admin -f $CONFIGURATION_DIRECTORY/pgexporter_users.conf -U $PG_USER_NAME -P $PG_USER_PASSWORD user add
   echo "Add user $PG_USER_NAME to pgexporter_users.conf file ... ok"
   echo ""
}

export_pgexporter_test_variables() {
  echo "export PGEXPORTER_TEST_BASE_DIR=$BASE_DIR"
  export PGEXPORTER_TEST_BASE_DIR=$BASE_DIR

  echo "export PGEXPORTER_TEST_CONF=$CONFIGURATION_DIRECTORY/pgexporter.conf"
  export PGEXPORTER_TEST_CONF=$CONFIGURATION_DIRECTORY/pgexporter.conf

  echo "export PGEXPORTER_TEST_USER_CONF=$CONFIGURATION_DIRECTORY/pgexporter_users.conf"
  export PGEXPORTER_TEST_USER_CONF=$CONFIGURATION_DIRECTORY/pgexporter_users.conf
}

unset_pgexporter_test_variables() {
  unset PGEXPORTER_TEST_BASE_DIR
  unset PGEXPORTER_TEST_CONF
  unset PGEXPORTER_TEST_USER_CONF
  unset LLVM_PROFILE_FILE
  unset CK_RUN_CASE
  unset CK_RUN_SUITE
  unset CC
}

execute_testcases() {
   echo "Execute Testcases"
   set +e
   echo "Starting pgexporter server in daemon mode"
   $EXECUTABLE_DIRECTORY/pgexporter -c $CONFIGURATION_DIRECTORY/pgexporter.conf -u $CONFIGURATION_DIRECTORY/pgexporter_users.conf -d
   echo "Wait for pgexporter to be ready"
   sleep 10
   $EXECUTABLE_DIRECTORY/pgexporter-cli -c $CONFIGURATION_DIRECTORY/pgexporter.conf status
   if [[ $? -eq 0 ]]; then
      echo "pgexporter server started ... ok"
   else
      echo "pgexporter server not started ... not ok"
      exit 1
   fi

   echo "Start running tests"
   # Run tests from the project root
   cd "$PROJECT_DIRECTORY"
   $TEST_DIRECTORY/pgexporter-test
   if [[ $? -ne 0 ]]; then
      exit 1
   fi
   set -e
}

usage() {
   echo "Usage: $0 [sub-command]"
   echo "Subcommand:"
   echo " clean       Clean up test suite environment and remove PostgreSQL image"
   echo " setup       Install dependencies and build PostgreSQL image"
   exit 1
}

run_tests() {
  echo "Preparing the pgexporter directory"
  export LLVM_PROFILE_FILE="$COVERAGE_DIR/coverage-%p.profraw"
  rm -Rf "$PGEXPORTER_ROOT_DIR"
  mkdir -p "$PGEXPORTER_ROOT_DIR"
  mkdir -p "$LOG_DIR" "$PG_LOG_DIR" "$COVERAGE_DIR" "$BASE_DIR"
  mkdir -p "$CONFIGURATION_DIRECTORY" "$PGCONF_DIRECTORY"
  cp -R $TEST_PG17_DIRECTORY/conf/* $PGCONF_DIRECTORY/
  chmod -R 777 $PG_LOG_DIR
  chmod -R 777 $PGCONF_DIRECTORY

  echo "Building PostgreSQL 17 image if necessary"
  # Check with and without localhost/ prefix for Podman compatibility
  if $CONTAINER_ENGINE image inspect "$IMAGE_NAME" >/dev/null 2>&1 || \
     $CONTAINER_ENGINE image inspect "localhost/$IMAGE_NAME" >/dev/null 2>&1; then
    echo "Image $IMAGE_NAME exists, skip building"
  else
    echo "Image $IMAGE_NAME does not exist, building now..."
    build_postgresql_image
    if $CONTAINER_ENGINE image inspect "$IMAGE_NAME" >/dev/null 2>&1 || \
       $CONTAINER_ENGINE image inspect "localhost/$IMAGE_NAME" >/dev/null 2>&1; then
      echo "Image built successfully"
    else
      echo "ERROR: Failed to build PostgreSQL image"
      exit 1
    fi
  fi

  if [[ ! -f "$EXECUTABLE_DIRECTORY/pgexporter" ]] || [[ ! -f "$TEST_DIRECTORY/pgexporter-test" ]]; then
    echo "Building pgexporter"
    mkdir -p "$PROJECT_DIRECTORY/build"
    cd "$PROJECT_DIRECTORY/build"
    export CC=$(which clang)
    cmake -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug -Dcheck=ON ..
    make -j$(nproc)
    cd ..
  else
    echo "pgexporter already built, skipping build step"
  fi

  echo "Start PostgreSQL 17 container"
  start_postgresql_container

  echo "Initialize pgexporter"
  pgexporter_initialize_configuration

  export_pgexporter_test_variables

  execute_testcases
}

if [[ $# -gt 1 ]]; then
   usage # More than one argument, show usage and exit
elif [[ $# -eq 1 ]]; then
   if [[ "$1" == "setup" ]]; then
      build_postgresql_image
      sudo dnf install -y \
        clang \
        clang-analyzer \
        cmake \
        make \
        libev libev-devel \
        openssl openssl-devel \
        systemd systemd-devel \
        check check-devel check-static \
        llvm
   elif [[ "$1" == "clean" ]]; then
      rm -Rf $COVERAGE_DIR
      cleanup
      cleanup_postgresql_image
      rm -Rf $PGEXPORTER_ROOT_DIR
   elif [[ "$1" == "ci" ]]; then
      MODE="ci"
      PORT=5432
      trap cleanup EXIT
      run_tests
   else
      echo "Invalid parameter: $1"
      usage # If an invalid parameter is provided, show usage and exit
   fi
else
   # If no arguments are provided, run function_without_param
   trap cleanup EXIT SIGINT
   run_tests
fi
