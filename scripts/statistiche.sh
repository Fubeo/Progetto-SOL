#!/bin/bash

set -o pipefail

if [ $# -eq 0 ];
  then
    if [ -d logs ];
      then
        LOG_FILE=logs/$(ls logs | grep -E '*' | tail -n1) || { echo "No log file could be found inside logs/ directory. Aborting script."; exit 1; }
    else # log
        echo "No log file could be found. Aborting script."
        exit 1
    fi
else
    LOG_FILE=$1
fi

# unsetting pipefail
set +o pipefail

COLOR_BOLD_CYAN="\033[1;36m"
COLOR_CYAN="\x1B[36m"
COLOR_BLUE="\u001b[34m"
COLOR_STANDARD="\033[0m"

# =================================== READ =====================================
echo -e "\n${COLOR_BOLD_CYAN}=== READ STATS ===========================================================${COLOR_STANDARD}\n"

n_reads=$(grep " read " -c $LOG_FILE)
tot_bytes_read=$(grep " read " $LOG_FILE | grep -Eo "\[[0-9]*\]" | grep -Eo "[0-9]*" | { sum=0; while read num; do ((sum+=num)); done; echo $sum; } )

echo -e  "${COLOR_BLUE}Number of successful read operations:${COLOR_STANDARD} ${n_reads}"
if [ ${n_reads} -gt 0 ]; then
    mean_bytes_read=$(echo "scale=4; ${tot_bytes_read} / ${n_reads}" | bc -l)
fi

echo -e "${COLOR_BLUE}Total number of bytes read by clients:${COLOR_STANDARD} $(( tot_bytes_read + tot_bytes_readNFiles))"
if (( n_reads + n_readNFiles > 0 )); then
    mean_bytes_gen_read=$(echo "scale=4; ($tot_bytes_read) / ($n_reads)" | bc -l )
    echo -e "${COLOR_BLUE}Average number of bytes read by clients:${COLOR_STANDARD} ${mean_bytes_gen_read}"
fi


# ================================== WRITE =====================================
echo -e "\n\n${COLOR_BOLD_CYAN}=== WRITE STATS ==========================================================${COLOR_STANDARD}\n"

n_write=$(grep " written " -c $LOG_FILE)
tot_written_bytes=$(grep " written " $LOG_FILE | grep -Eo "\[[0-9]*\]" | grep -Eo "[0-9]*" | { sum=0; while read num; do ((sum+=num)); done; echo $sum; } )
n_appends=$(grep " appended " -c $LOG_FILE)
tot_appended_bytes=$(grep " appended " $LOG_FILE | grep -Eo "\[[0-9]*\]" | grep -Eo "[0-9]*" | { sum=0; while read num; do ((sum+=num)); done; echo $sum; } )

echo -e  "${COLOR_BLUE}Number of successful write operations:${COLOR_STANDARD} ${n_write}"
if [ ${n_write} -gt 0 ]; then
  mean_bytes_written=$(echo "scale=4; ${tot_written_bytes} / ${n_write}" | bc -l)
fi


echo -e "${COLOR_BLUE}Total number of bytes written:${COLOR_STANDARD} $(( tot_written_bytes + tot_appended_bytes))"
if (( n_appends + n_write > 0 )); then
    average_written_bytes=$(echo "scale=4; ($tot_appended_bytes + $tot_written_bytes) / ($n_appends + $n_write)" | bc -l )
    echo -e  "${COLOR_BLUE}Average number of bytes written:${COLOR_STANDARD} ${average_written_bytes}"
fi

echo ""

echo -e "${COLOR_BLUE}Number of successful append operations:${COLOR_STANDARD} ${n_appends}"
if [[ ${n_appends} -gt 0 ]]; then
  average_appended_bytes=$(echo "scale=4; ${tot_appended_bytes} / ${n_appends}" | bc -l)
  echo -e "${COLOR_BLUE}Average number of bytes appended:${COLOR_STANDARD} ${average_appended_bytes}"
fi


# ================================ VARIOUS STATS ===============================

echo -e "\n\n${COLOR_BOLD_CYAN}=== VARIOUS STATS ========================================================${COLOR_STANDARD}\n"

n_create=$(grep " created file" -c $LOG_FILE)
n_createlocks=$(grep " created with lock " -c $LOG_FILE)
n_openlocks=$(grep " opened with lock " -c $LOG_FILE)
n_locks=$(grep " locked " -c $LOG_FILE)
n_unlocks=$(grep " unlocked " -c $LOG_FILE)
n_close=$(grep " closed " -c $LOG_FILE)
max_size=$(grep "Max stored " ${LOG_FILE} | grep -Eo "[0-9]+" | { max=0; while read num; do if (( max<num )); then ((max=num)); fi; done; echo $max; } )
max_files=$(grep "Max number " ${LOG_FILE} | grep -Eo "[0-9]+" | { max=0; while read num; do if (( max<num )); then ((max=num)); fi; done; echo $max; } )
max_conn=$(grep "Max connected " ${LOG_FILE} | grep -Eo "[0-9]+" | { max=0; while read num; do if (( max<num )); then ((max=num)); fi; done; echo $max; } )
n_eject=$(grep " ejected " -c $LOG_FILE)

echo -e  "${COLOR_BLUE}Number of successful create operations:${COLOR_STANDARD} $n_create"
echo -e  "${COLOR_BLUE}Number of successful create-lock operations:${COLOR_STANDARD} $n_createlocks"
echo -e  "${COLOR_BLUE}Number of successful open-lock operations:${COLOR_STANDARD} $n_openlocks"
echo -e  "${COLOR_BLUE}Number of successful lock operations:${COLOR_STANDARD} $n_locks"
echo -e  "${COLOR_BLUE}Number of successful unlock operations:${COLOR_STANDARD} $n_unlocks"
echo -e  "${COLOR_BLUE}Number of successful close operations:${COLOR_STANDARD} $n_close"
echo -e  "${COLOR_BLUE}Number of successful cache replacements:${COLOR_STANDARD} $n_eject"


# getting number of requests satisfied by each thread
echo -e "\n${COLOR_BLUE}Number of requests managed by each worker:${COLOR_STANDARD}"

s=$(echo $(grep -Eo "Worker [0-9]* received" ${LOG_FILE} | grep -Eo "[0-9]* " | sort -u | sed s/' '/"\n"/g))
tids=()
i=0
for str in $(echo $s)
do
  tids+=($str)
  let "i++"
done

n_tids=$(grep -Eo "Worker [0-9]* received" ${LOG_FILE} | grep -Eo "[0-9]+" | sort -u | grep -c '')
for ((i=0; i<$n_tids; i++)); do
  n_req=$(grep -c "Worker ${tids[$i]} received" ${LOG_FILE})
  echo -e "${COLOR_CYAN}Worker ${COLOR_STANDARD}${tids[$i]}${COLOR_CYAN} managed ${COLOR_STANDARD}$n_req${COLOR_CYAN} requests${COLOR_STANDARD}"
done

echo ""

echo -e  "${COLOR_BLUE}Max stored data size (Mbytes):${COLOR_STANDARD} $max_size"
echo -e  "${COLOR_BLUE}Max number of stored files:${COLOR_STANDARD} $max_files"
echo -e  "${COLOR_BLUE}Max number of simultaneously connected clients:${COLOR_STANDARD} $max_conn"
