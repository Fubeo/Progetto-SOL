sleep 5

./out/client Fabio
PID[0]=$!
./out/client Giada
PID[1]=$!
./out/client Matteo
PID[2]=$!
./out/client Giovanna
PID[3]=$!
./out/client Guido
PID[4]=$!



for((i=0;i<5;++i)); do
    wait ${PID[i]}
done
