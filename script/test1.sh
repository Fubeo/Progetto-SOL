sleep 3

k=0

./out/client
PID[k]=$!
((k++))
./out/client
PID[k]=$!
((k++))

for((i=0;i<k;++i)); do
    wait ${PID[i]}
done
