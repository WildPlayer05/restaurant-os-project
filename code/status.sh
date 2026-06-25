#!/bin/bash

#variables with error codes
ERR_FILE_NOT_FOUND=1
ERR_NOT_READABLE=6
ERR_NOT_VALID_VALUE=7
ERR_PROCESS_NOT_EXIST=8
file="/tmp/restaurant.pid"

#control if the file exist and is readable
[[ -e "$file" ]] || { echo "error $ERR_FILE_NOT_FOUND, file ("$file") doesn't exist"; exit $ERR_FILE_NOT_FOUND; }
[[ -r "$file" ]] || { echo "error $ERR_NOT_READABLE, file ("$file") isn't readable"; exit $ERR_NOT_READABLE; }

#acquire the pid
pid=$(cat "$file")

#control if pid is a number
if ! [[ "$pid" =~ ^[0-9]+$ ]]; then
    echo "error $ERR_NOT_VALID_VALUE, pid isn't an integer number";
    exit $ERR_NOT_VALID_VALUE;
fi

#sends a signal (SIGUSR1) to the process contained in (/tmp/restaurant.pid)
if ! kill -SIGUSR1 "$pid" 2>/dev/null; then
    echo "error $ERR_PROCESS_NOT_EXIST, process with pid $pid does not exist"
    exit $ERR_PROCESS_NOT_EXIST
fi
