TRACE_ID=$1
LOG_PATH=$2
MERGE_LOG_PATH=$3
grep $(TRACE_ID) $(LOG_PATH)/observer* $(LOG_PATH)/election* $(LOG_PATH)/rootservice* \
    | sed 's/:/ /  awk '{tmp=$1; $1=$2; $2=$3; $3=$4; $4=tmp; print $0}' \
    | sort > ${MERGE LOG PATH}