sleep 5

k=0

./out/client -ftmp/serversock.sk
PID[k]=$!
((k++))
./out/client -ftmp/serversock.sk
PID[k]=$!
((k++))
./out/client -ftmp/serversock.sk
PID[k]=$!
((k++))
./out/client -ftmp/serversock.sk
PID[k]=$!
((k++))
./out/client -ftmp/serversock.sk
PID[k]=$!
((k++))

for((i=0;i<k;++i)); do
    wait ${PID[i]}
done

sleep 1
