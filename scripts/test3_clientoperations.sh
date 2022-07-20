#!/bin/bash
realpath=$(realpath ./)
folder1="${realpath}/test/folder1/"
folder2="${realpath}/test/folder2/"
backup="${realpath}/tmp/backup/"
download="${realpath}/tmp/download/"
socket="tmp/serversock.sk"

while true
do
./out/client -f${socket} -d${download}    \
-r${folder2}A.txt                         \
-W${folder2}A.txt                         \
-o${folder2}A.txt,${folder2}B.txt         \
-W${folder2}B.txt                         \
-w${folder1}cibo                          \
-a${folder2}A.txt,${folder2}A.txt         \
-a${folder2}B.txt,${folder2}B.txt         \
-C${folder1}cibo/cibo_1.png               \
-R
done
