#!/bin/bash
TRACE_ID="$1"
LOG_PATH="$2"
MERGE_LOG_PATH=${LOG_PATH}/${TRACE_ID}.log
touch ${MERGE_LOG_PATH} \
    &&
grep ${TRACE_ID} ${LOG_PATH}/observer* ${LOG_PATH}/election* ${LOG_PATH}/rootservice* \
    | sed 's/:/ /' | awk '{tmp=$1; $1=$2; $2=$3; $3=$4; $4=tmp; print $0}' \
    | sort > ${MERGE_LOG_PATH}