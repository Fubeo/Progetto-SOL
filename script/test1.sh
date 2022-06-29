sleep 5

k=0

./out/client
PID[k]=$!
((k++))

for((i=0;i<k;++i)); do
    wait ${PID[i]}
done

sleep 1
