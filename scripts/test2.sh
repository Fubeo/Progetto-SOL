#!/bin/bash

sleep 2

realpath=$(realpath ./)
folder1="${realpath}/test/folder1/"
folder2="${realpath}/test/folder2/"
backup="${realpath}/tmp/backup/"
download="${realpath}/tmp/download/"
socket="tmp/serversock.sk"

k=0

./out/client -t100 -f${socket} -p -D${backup} -w${folder1}cani , -w${folder2}
PID[k]=$!
((k++))

./out/client -t100 -f${socket} -p -D${backup} -L -w${folder1}molti_file
PID[k]=$!
((k++))

./out/client -t100 -f${socket} -p -D${backup} -w${folder1}vari , -w${folder1}cibo
PID[k]=$!
((k++))

./out/client -t100 -f${socket} -p -L -D${backup} -d${download}    \
-W${folder1}cibo/cibo_3.png ,                                     \
-a${folder1}cibo/cibo_3.png,${folder2}1.txt ,                     \
-a${folder1}cibo/cibo_3.png,${folder2}2.txt ,                     \
-a${folder1}cibo/cibo_3.png,${folder2}3.txt ,                     \
-a${folder1}cibo/cibo_3.png,${folder2}4.txt ,                     \
-r${folder1}cibo/cibo_3.png                                       \
-c${folder1}cibo/cibo_3.png
PID[k]=$!
((k++))

for((i=0;i<k;++i)); do
    wait ${PID[i]}
done
