#!/bin/bash
realpath=$(realpath ./)
folder1="${realpath}/test/folder1/"
folder2="${realpath}/test/folder2/"
backup="${realpath}/tmp/backup/"
download="${realpath}/tmp/download/"
socket="tmp/serversock.sk"

while true
do

./out/client -ftmp/serversock.sk -d${download} -L \
-W${folder2}A.txt                 \
-W${folder2}2.txt                 \
-W${folder2}3.txt                 \
-a${folder2}A.txt,${folder2}A.txt \
-R
sleep 0.1
done
