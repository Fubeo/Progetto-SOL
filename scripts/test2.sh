sleep 3
realpath=$(realpath ./)
folder1="${realpath}/test/folder1/"
folder2="${realpath}/test/folder2/"
backup="${realpath}/tmp/backup/"
download="${realpath}/tmp/download/"
socket="./tmp/serversock.sk"

./out/client -t200 -f${socket}  -D${backup}     \
-w${folder1} ,                                  \
-d${download} -R                                
