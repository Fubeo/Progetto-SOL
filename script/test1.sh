sleep 5

k=0

./out/client Fabio
PID[k]=$!
((k++))
./out/client Giada
PID[k]=$!
((k++))
./out/client Matteo
PID[k]=$!
((k++))
./out/client Giovanna
PID[k]=$!
((k++))
./out/client Guido
PID[k]=$!
((k++))

for((i=0;i<k;++i)); do
    wait ${PID[i]}
done

sleep 1
