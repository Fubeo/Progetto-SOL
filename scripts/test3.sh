#!/bin/bash

COLOR_CYAN="\x1B[36m"
COLOR_GREEN="\u001b[32m"
COLOR_STANDARD="\033[0m"

# avvio server
./out/server -c./config/test3.ini &
echo -e "\n${COLOR_GREEN}Server running${COLOR_STANDARD}\n"


# server pid
SERVER_PID=$!
export SERVER_PID

sleep 1

bash -c 'sleep 32 && kill -2 ${SERVER_PID}' &

PID=()
for i in {1..10}; do
    bash -c './scripts/test3_clientoperations.sh' &
    PID+=($!)
    sleep 0.1
done

echo -e "${COLOR_CYAN}Executing clients...\n${COLOR_STANDARD}"

for i in {0..5}; do
    let "sleft = 30 - $i*5"
    echo "$sleft seconds left"
    sleep 5
done

for i in "${PID[@]}"; do
	kill "${i}"
	wait "${i}" 2>/dev/null
done

echo ""

wait $SERVER_PID

