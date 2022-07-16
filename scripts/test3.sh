#! /bin/bash

./out/server -c./config/test3.ini &
# server pid
SERVER_PID=$!
export SERVER_PID


bash -c 'sleep 35 && kill -2 ${SERVER_PID} -c./config/test3.ini' &
echo -e "30 secondi all'invio del segnale SIGINT"
PID=()

for i in {1..10}; do
    bash -c './scripts/test3_clientoperations.sh' &
    PID+=($!)
    #sleep 0.1
done

sleep 25

for i in "${PID[@]}"; do
	kill "${i}"
	wait "${i}" 2>/dev/null
done

wait $SERVER_PID
killall -q ./out/client
