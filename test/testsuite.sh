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

set -e

OS=$(uname)

THIS_FILE=$(realpath "$0")
FILE_OWNER=$(ls -l "$THIS_FILE" | awk '{print $3}')
USER=$(whoami)
WAIT_TIMEOUT=5

PORT=5432
PGPASSWORD="password"

PROJECT_DIRECTORY=$(pwd)
EXECUTABLE_DIRECTORY=$(pwd)/src
TEST_DIRECTORY=$(pwd)/test

LOG_DIRECTORY=$(pwd)/log
PGCTL_LOG_FILE=$LOG_DIRECTORY/logfile
PGEXPORTER_LOG_FILE=$LOG_DIRECTORY/pgexporter.log

POSTGRES_OPERATION_DIR=$(pwd)/pgexporter-postgresql
DATA_DIRECTORY=$POSTGRES_OPERATION_DIR/data

PGEXPORTER_OPERATION_DIR=$(pwd)/pgexporter-testsuite
CONFIGURATION_DIRECTORY=$PGEXPORTER_OPERATION_DIR/conf

PSQL_USER=$USER
if [ "$OS" = "FreeBSD" ]; then
  PSQL_USER=postgres
fi

########################### UTILS ############################
is_port_in_use() {
   local port=$1
   if [[ "$OS" == "Linux" ]]; then
      ss -tuln | grep $port >/dev/null 2>&1
   elif [[ "$OS" == "Darwin" ]]; then
      lsof -i:$port >/dev/null 2>&1
   elif [[ "$OS" == "FreeBSD" ]]; then
      sockstat -4 -l | grep $port >/dev/null 2>&1
   fi
   return $?
}

next_available_port() {
   local port=$1
   while true; do
      is_port_in_use $port
      if [ $? -ne 0 ]; then
         echo "$port"
         return 0
      else
         port=$((port + 1))
      fi
   done
}

wait_for_server_ready() {
   local start_time=$SECONDS
   while true; do
      pg_isready -h localhost -p $PORT
      if [ $? -eq 0 ]; then
         echo "postgres is ready for accepting responses"
         return 0
      fi
      if [ $(($SECONDS - $start_time)) -gt $WAIT_TIMEOUT ]; then
         echo "waiting for server timed out"
         return 1
      fi

      # Avoid busy-waiting
      sleep 1
   done
}

function sed_i() {
   if [[ "$OS" == "Darwin" || "$OS" == "FreeBSD" ]]; then
      sed -i '' -E "$@"
   else
      sed -i -E "$@"
   fi
}

##############################################################

############### CHECK POSTGRES DEPENDENCIES ##################
check_inidb() {
   if which initdb >/dev/null 2>&1; then
      echo "check initdb in path ... ok"
      return 0
   else
      echo "check initdb in path ... not present"
      return 1
   fi
}

check_pg_ctl() {
   if which pg_ctl >/dev/null 2>&1; then
      echo "check pg_ctl in path ... ok"
      return 0
   else
      echo "check pg_ctl in path ... not ok"
      return 1
   fi
}

stop_pgctl(){
   echo "Attempting to stop PostgreSQL..."
   set +e  # Allow failures here since server might already be stopped
   if [[ "$OS" == "FreeBSD" ]]; then
      su - postgres -c "$PGCTL_PATH -D $DATA_DIRECTORY -l $PGCTL_LOG_FILE stop"
      stop_result=$?
   else
      pg_ctl -D $DATA_DIRECTORY -l $PGCTL_LOG_FILE stop
      stop_result=$?
   fi
   
   if [ $stop_result -ne 0 ]; then
      echo "PostgreSQL stop returned code $stop_result (may have been already stopped)"
   else
      echo "PostgreSQL stopped successfully"
   fi
   set -e
}

run_as_postgres() {
  if [[ "$OS" == "FreeBSD" ]]; then
    su - postgres -c "$*"
  else
    eval "$@"
  fi
}

check_psql() {
   if which psql >/dev/null 2>&1; then
      echo "check psql in path ... ok"
      return 0
   else
      echo "check psql in path ... not present"
      return 1
   fi
}

check_postgres_version() {
   version=$(psql --version | awk '{print $3}' | sed -E 's/^([0-9]+(\.[0-9]+)?).*/\1/')
   major_version=$(echo "$version" | cut -d'.' -f1)
   required_major_version=$1
   if [ "$major_version" -ge "$required_major_version" ]; then
      echo "check postgresql version: $version ... ok"
      return 0
   else
      echo "check postgresql version: $version ... not ok"
      return 1
   fi
}

check_system_requirements() {
   echo -e "\e[34mCheck System Requirements \e[0m"
   echo "check system os ... $OS"
   check_inidb
   if [ $? -ne 0 ]; then
      exit 1
   fi
   check_pg_ctl
   if [ $? -ne 0 ]; then
      exit 1
   fi
   check_psql
   if [ $? -ne 0 ]; then
      exit 1
   fi
   check_postgres_version 17
   if [ $? -ne 0 ]; then
      exit 1
   fi
   echo ""
}

initialize_log_files() {
   echo -e "\e[34mInitialize Test logfiles \e[0m"
   mkdir -p $LOG_DIRECTORY
   echo "create log directory ... $LOG_DIRECTORY"
   touch $PGEXPORTER_LOG_FILE
   echo "create log file ... $PGEXPORTER_LOG_FILE"
   touch $PGCTL_LOG_FILE
   echo "create log file ... $PGCTL_LOG_FILE"
   echo ""
}
##############################################################

##################### POSTGRES OPERATIONS ####################
create_cluster() {
   local port=$1
   echo -e "\e[34mInitializing Cluster \e[0m"

   if [ "$OS" = "FreeBSD" ]; then
    mkdir -p "$POSTGRES_OPERATION_DIR"
    mkdir -p "$DATA_DIRECTORY"
    mkdir -p $CONFIGURATION_DIRECTORY
    if ! pw user show postgres >/dev/null 2>&1; then
        pw groupadd -n postgres -g 770
        pw useradd -n postgres -u 770 -g postgres -d /var/db/postgres -s /bin/sh
    fi
    chown postgres:postgres $PGCTL_LOG_FILE
    chown -R postgres:postgres "$DATA_DIRECTORY"
    chown -R postgres:postgres $CONFIGURATION_DIRECTORY

   fi

   echo $DATA_DIRECTORY
   
   INITDB_PATH=$(command -v initdb)

   if [ -z "$INITDB_PATH" ]; then
      echo "Error: initdb not found!" >&2
      exit 1
   fi
   run_as_postgres "$INITDB_PATH -k -D $DATA_DIRECTORY"
   echo "initdb exit code: $?"
   echo "initialize database ... ok"
   set +e
   echo "setting postgresql.conf"

   error_out=$(sed_i "s/^#[[:space:]]*password_encryption[[:space:]]*=[[:space:]]*(md5|scram-sha-256)/password_encryption = scram-sha-256/" "$DATA_DIRECTORY/postgresql.conf" 2>&1)

   if [ $? -ne 0 ]; then
      echo "setting password_encryption ... $error_out"
      clean
      exit 1
   else
      echo "setting password_encryption ... scram-sha-256"
   fi
   error_out=$(sed_i "s|#unix_socket_directories = '/var/run/postgresql'|unix_socket_directories = '/tmp'|" $DATA_DIRECTORY/postgresql.conf 2>&1)
   if [ $? -ne 0 ]; then
      echo "setting unix_socket_directories ... $error_out"
      clean
      exit 1
   else
      echo "setting unix_socket_directories ... '/tmp'"
   fi
   error_out=$(sed_i "s/#port = 5432/port = $port/" $DATA_DIRECTORY/postgresql.conf 2>&1)
   if [ $? -ne 0 ]; then
      echo "setting port ... $error_out"
      clean
      exit 1
   else
      echo "setting port ... $port"
   fi
   error_out=$(sed_i "s/#wal_level = replica/wal_level = replica/" $DATA_DIRECTORY/postgresql.conf 2>&1)
   if [ $? -ne 0 ]; then
      echo "setting wal_level ... $error_out"
      clean
      exit 1
   else
      echo "setting wal_level ... replica"
   fi

   LOG_ABS_PATH=$(realpath "$LOG_DIRECTORY")
   sed_i "s/^#*logging_collector.*/logging_collector = on/" "$DATA_DIRECTORY/postgresql.conf"
   sed_i "s/^#*log_destination.*/log_destination = 'stderr'/" "$DATA_DIRECTORY/postgresql.conf"
   sed_i "s|^#*log_directory.*|log_directory = '$LOG_ABS_PATH'|" "$DATA_DIRECTORY/postgresql.conf"
   sed_i "s/^#*log_filename.*/log_filename = 'logfile'/" "$DATA_DIRECTORY/postgresql.conf"

   # If any of the above settings are missing, append them
   grep -q "^logging_collector" "$DATA_DIRECTORY/postgresql.conf" || echo "logging_collector = on" >> "$DATA_DIRECTORY/postgresql.conf"
   grep -q "^log_destination" "$DATA_DIRECTORY/postgresql.conf" || echo "log_destination = 'stderr'" >> "$DATA_DIRECTORY/postgresql.conf"
   grep -q "^log_directory" "$DATA_DIRECTORY/postgresql.conf" || echo "log_directory = '$LOG_ABS_PATH'" >> "$DATA_DIRECTORY/postgresql.conf"
   grep -q "^log_filename" "$DATA_DIRECTORY/postgresql.conf" || echo "log_filename = 'logfile'" >> "$DATA_DIRECTORY/postgresql.conf"

   set -e
   echo ""
}

initialize_hba_configuration() {
   echo -e "\e[34mCreate HBA Configuration \e[0m"
   echo "
    local   all              all                                     trust
    local   replication      all                                     trust
    host    postgres         pgexporter      127.0.0.1/32            scram-sha-256
    host    postgres         pgexporter      ::1/128                 scram-sha-256
    " >$DATA_DIRECTORY/pg_hba.conf
   echo "initialize hba configuration at $DATA_DIRECTORY/pg_hba.conf ... ok"
   echo ""
}

initialize_cluster() {
   echo -e "\e[34mInitializing Cluster \e[0m"
   set +e
   PGCTL_PATH=$(command -v pg_ctl)
   if [ -z "$PGCTL_PATH" ]; then
      echo "Error: pg_ctl not found!" >&2
      exit 1
   fi
   run_as_postgres "$PGCTL_PATH -D $DATA_DIRECTORY -l $PGCTL_LOG_FILE start"
   if [ $? -ne 0 ]; then
      echo "PostgreSQL failed to start. Printing log:"
      cat $PGCTL_LOG_FILE
      clean
      exit 1
   fi
   pg_isready -h localhost -p $PORT
   if [ $? -eq 0 ]; then
      echo "postgres server is accepting requests ... ok"
   else
      echo "postgres server is not accepting response ... not ok"
      clean
      exit 1
   fi
   err_out=$(psql -h /tmp -p $PORT -U $PSQL_USER -d postgres -c "CREATE USER pgexporter WITH PASSWORD '$PGPASSWORD';" 2>&1)
   if [ $? -ne 0 ]; then
      echo "create user pgexporter ... $err_out"
      stop_pgctl
      clean
      exit 1
   else
      echo "create user pgexporter ... ok"
   fi
   err_out=$(psql -h /tmp -p $PORT -U $PSQL_USER -d postgres -c "CREATE EXTENSION IF NOT EXISTS pg_stat_statements;" 2>&1)
   if [ $? -ne 0 ]; then
      echo "create extension pg_stat_statements ... $err_out"
      echo "Warning: pg_stat_statements extension not available"
   else
      echo "create extension pg_stat_statements ... ok"
   fi
   err_out=$(psql -h /tmp -p $PORT -U $PSQL_USER -d postgres -c "GRANT pg_monitor TO pgexporter;" 2>&1)
   if [ $? -ne 0 ]; then
      echo "grant pg_monitor to pgexporter ... $err_out"
      stop_pgctl
      clean
      exit 1
   else
      echo "grant pg_monitor to pgexporter ... ok"
   fi
   set -e
   stop_pgctl
   echo ""
}

clean_logs() {
   if [ -d $LOG_DIRECTORY ]; then
      rm -r $LOG_DIRECTORY
      echo "remove log directory $LOG_DIRECTORY ... ok"
   else
      echo "$LOG_DIRECTORY not present ... ok"
   fi
}

clean() {
   echo -e "\e[34mClean Test Resources \e[0m"
   if [ -d $POSTGRES_OPERATION_DIR ]; then
      rm -r $POSTGRES_OPERATION_DIR
      echo "remove postgres operations directory $POSTGRES_OPERATION_DIR ... ok"
   else
      echo "$POSTGRES_OPERATION_DIR not present ... ok"
   fi

   if [ -d $PGEXPORTER_OPERATION_DIR ]; then
      rm -r $PGEXPORTER_OPERATION_DIR
      echo "remove pgexporter operations directory $PGEXPORTER_OPERATION_DIR ... ok"
   else
      echo "$PGEXPORTER_OPERATION_DIR not present ... ok"
   fi
}

##############################################################

#################### PGEXPORTER OPERATIONS #####################
pgexporter_initialize_configuration() {
   echo -e "\e[34mInitialize pgexporter configuration files \e[0m"
   mkdir -p $CONFIGURATION_DIRECTORY
   echo "create configuration directory $CONFIGURATION_DIRECTORY ... ok"
   touch $CONFIGURATION_DIRECTORY/pgexporter.conf $CONFIGURATION_DIRECTORY/pgexporter_users.conf
   echo "create pgexporter.conf and pgexporter_users.conf inside $CONFIGURATION_DIRECTORY ... ok"
   cat <<EOF >$CONFIGURATION_DIRECTORY/pgexporter.conf
[pgexporter]
host = localhost
metrics = 5002
bridge = 5003
bridge_endpoints = localhost:5002

log_type = file
log_level = debug5
log_path = $PGEXPORTER_LOG_FILE

unix_socket_dir = /tmp/

[primary]
host = localhost
port = $PORT
user = pgexporter
EOF
   echo "add test configuration to pgexporter.conf ... ok"
   if [[ "$OS" == "FreeBSD" ]]; then
    chown -R postgres:postgres $CONFIGURATION_DIRECTORY
    chown -R postgres:postgres $PGEXPORTER_LOG_FILE
   fi
   
   echo "=== DEBUG: Creating master key ==="
   run_as_postgres "$EXECUTABLE_DIRECTORY/pgexporter-admin master-key -P $PGPASSWORD" || true
   master_key_result=$?
   echo "Master key creation result: $master_key_result"
   
   echo "=== DEBUG: Adding user to config ==="
   run_as_postgres "$EXECUTABLE_DIRECTORY/pgexporter-admin -f $CONFIGURATION_DIRECTORY/pgexporter_users.conf -U pgexporter -P $PGPASSWORD user add"
   user_add_result=$?
   echo "User add result: $user_add_result"
   
   echo "=== DEBUG: Verifying users config file ==="
   if [[ -f "$CONFIGURATION_DIRECTORY/pgexporter_users.conf" ]]; then
      echo "Users config file exists"
      run_as_postgres "ls -la $CONFIGURATION_DIRECTORY/pgexporter_users.conf"
      file_size=$(run_as_postgres "wc -c < $CONFIGURATION_DIRECTORY/pgexporter_users.conf")
      echo "Users config file size: $file_size bytes"
      if [[ $file_size -eq 0 ]]; then
         echo "file exists but is empty"
         exit 1
      fi
   else
      echo "ERROR: Users config file missing!"
      echo "Expected: $CONFIGURATION_DIRECTORY/pgexporter_users.conf"
      run_as_postgres "ls -la $CONFIGURATION_DIRECTORY/"
      exit 1
   fi
   
   echo "add user pgexporter to pgexporter_users.conf file ... ok"
   echo ""
}

test_bridge_endpoint_with_curl() {
   echo -e "\e[34mTesting Bridge Endpoint with curl \e[0m"
   
   echo "=== Bridge Endpoint curl Test on $OS ==="
   echo "Testing direct connectivity to bridge endpoint using curl"
   
   # Test if bridge port is listening
   echo "Checking if bridge port 5003 is listening..."
   if [[ "$OS" == "Linux" ]]; then
      ss -tuln | grep :5003 || echo "Port 5003 not found in ss output"
   elif [[ "$OS" == "Darwin" ]]; then
      lsof -i :5003 || echo "Port 5003 not found in lsof output"
      netstat -an | grep :5003 || echo "Port 5003 not found in netstat output"
   elif [[ "$OS" == "FreeBSD" ]]; then
      sockstat -l | grep :5003 || echo "Port 5003 not found in sockstat output"
      netstat -an | grep :5003 || echo "Port 5003 not found in netstat output"
   fi
   
   # Test curl connectivity to bridge endpoint
   echo "Testing curl to bridge endpoint..."
   set +e  # Allow curl to fail without stopping the script
   
   # Create a temporary file for curl output
   CURL_OUTPUT_FILE="$LOG_DIRECTORY/bridge_curl_output.txt"
   
   # Check if timeout command exists (not available on macOS by default)
   if command -v timeout >/dev/null 2>&1; then
      timeout 15 curl -v --connect-timeout 10 --max-time 30 http://localhost:5003/metrics > "$CURL_OUTPUT_FILE" 2>&1
      CURL_EXIT_CODE=$?
   else
      # macOS and other systems without timeout - use curl's built-in timeouts
      curl -v --connect-timeout 10 --max-time 30 http://localhost:5003/metrics > "$CURL_OUTPUT_FILE" 2>&1
      CURL_EXIT_CODE=$?
   fi
   
   echo "Curl exit code: $CURL_EXIT_CODE"
   echo "=== Curl output ==="
   cat "$CURL_OUTPUT_FILE"
   echo "=== End curl output ==="
   
   # Check response content
   if [ $CURL_EXIT_CODE -eq 0 ]; then
      echo "SUCCESS: curl connected to bridge endpoint on $OS"
      echo "Response size: $(wc -c < "$CURL_OUTPUT_FILE") bytes"
      echo "Response lines: $(wc -l < "$CURL_OUTPUT_FILE") lines"
      
      # Check for expected content
      if grep -q "pgexporter_state" "$CURL_OUTPUT_FILE"; then
         echo "SUCCESS: Found expected pgexporter_state metric in bridge response"
      else
         echo "WARNING: pgexporter_state metric not found in bridge response"
         echo "First 500 chars of bridge response:"
         head -c 500 "$CURL_OUTPUT_FILE"
      fi
      
      # Check for endpoint-specific metrics (bridge format)
      if grep -q "endpoint=" "$CURL_OUTPUT_FILE"; then
         echo "SUCCESS: Found endpoint labels in bridge response (expected bridge format)"
      else
         echo "WARNING: No endpoint labels found - may not be bridge format"
      fi
   else
      echo "ERROR: curl failed to connect to bridge endpoint on $OS (exit code: $CURL_EXIT_CODE)"
      
      # Test if main metrics endpoint works as fallback
      echo "Testing if main metrics endpoint works..."
      MAIN_CURL_OUTPUT_FILE="$LOG_DIRECTORY/main_curl_output.txt"
      
      # Use same timeout handling for main endpoint
      if command -v timeout >/dev/null 2>&1; then
         timeout 10 curl -v --connect-timeout 5 --max-time 15 http://localhost:5002/metrics > "$MAIN_CURL_OUTPUT_FILE" 2>&1
         MAIN_CURL_EXIT_CODE=$?
      else
         curl -v --connect-timeout 5 --max-time 15 http://localhost:5002/metrics > "$MAIN_CURL_OUTPUT_FILE" 2>&1
         MAIN_CURL_EXIT_CODE=$?
      fi
      echo "Main endpoint curl exit code: $MAIN_CURL_EXIT_CODE"
      
      if [ $MAIN_CURL_EXIT_CODE -eq 0 ]; then
         echo "SUCCESS: Main metrics endpoint works"
         echo "Main response size: $(wc -c < "$MAIN_CURL_OUTPUT_FILE") bytes"
         if grep -q "pgexporter_state" "$MAIN_CURL_OUTPUT_FILE"; then
            echo "SUCCESS: Found pgexporter_state in main endpoint"
         fi
      else
         echo "ERROR: Main metrics endpoint also failed"
         echo "Main endpoint curl output:"
         cat "$MAIN_CURL_OUTPUT_FILE" 2>/dev/null || echo "No main response file"
      fi
   fi
   
   # Show process status
   echo "=== Process status ==="
   ps aux | grep pgexporter | grep -v grep || echo "No pgexporter processes found"
   
   # Show system info
   echo "=== System info ==="
   echo "OS: $OS"
   uname -a
   if [[ "$OS" == "FreeBSD" ]]; then
      freebsd-version 2>/dev/null || echo "FreeBSD version not available"
   fi
   
   set -e  # Re-enable exit on error
   echo ""
}

execute_testcases() {
   echo -e "\e[34mExecute Testcases \e[0m"
   set +e
   run_as_postgres "pg_ctl -D $DATA_DIRECTORY -l $PGCTL_LOG_FILE start"
   pg_isready -h localhost -p $PORT
   if [ $? -eq 0 ]; then
      echo "postgres server accepting requests ... ok"
   else
      echo "postgres server is not accepting response ... not ok"
      stop_pgctl
      clean
      exit 1
   fi
   echo "starting pgexporter server in daemon mode"
   
   echo "=== DEBUG: Final config verification before starting pgexporter ==="
   run_as_postgres "ls -la $CONFIGURATION_DIRECTORY/"
   echo "Contents of pgexporter.conf:"
   run_as_postgres "cat $CONFIGURATION_DIRECTORY/pgexporter.conf"
   echo "Users config file status:"
   if [[ -f "$CONFIGURATION_DIRECTORY/pgexporter_users.conf" ]]; then
      run_as_postgres "ls -la $CONFIGURATION_DIRECTORY/pgexporter_users.conf"
      run_as_postgres "wc -c $CONFIGURATION_DIRECTORY/pgexporter_users.conf"
   else
      echo "ERROR: Users config file missing at execution time!"
      exit 1
   fi
   
   run_as_postgres "$EXECUTABLE_DIRECTORY/pgexporter -c $CONFIGURATION_DIRECTORY/pgexporter.conf -u $CONFIGURATION_DIRECTORY/pgexporter_users.conf -d"
   
   # Wait a moment for pgexporter to start
   sleep 3
   
   # Test bridge endpoint with curl before running unit tests
   test_bridge_endpoint_with_curl
   
   ### RUN TESTCASES ###
   run_as_postgres $TEST_DIRECTORY/pgexporter_test $PROJECT_DIRECTORY
   if [ $? -ne 0 ]; then
      # Kill pgexporter if tests failed
      pkill -f pgexporter || true
      stop_pgctl
      clean
      exit 1
   fi
   
   pkill -f pgexporter || true
   echo "pgexporter server stopped ... ok"
   stop_pgctl
   set -e
   echo ""
}

##############################################################

run_tests() {
   # Check if the user is pgexporter
   if [ "$FILE_OWNER" == "$USER" ]; then
      ## Postgres operations
      check_system_requirements

      initialize_log_files

      PORT=$(next_available_port $PORT)
      create_cluster $PORT

      initialize_hba_configuration
      initialize_cluster

      ## pgexporter operations
      pgexporter_initialize_configuration
      execute_testcases
      # clean cluster
      clean
   else
      echo "user should be $FILE_OWNER"
      exit 1
   fi
}

usage() {
   echo "Usage: $0 [sub-command]"
   echo "Subcommand:"
   echo " clean           clean up test suite environment"
   exit 1
}

if [ $# -gt 1 ]; then
   usage # More than one argument, show usage and exit
elif [ $# -eq 1 ]; then
   if [ "$1" == "clean" ]; then
      # If the parameter is 'clean', run clean_function
      clean
      clean_logs
   else
      echo "Invalid parameter: $1"
      usage # If an invalid parameter is provided, show usage and exit
   fi
else
   # If no arguments are provided, run function_without_param
   run_tests
fi
