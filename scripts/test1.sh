sleep 3 #per dare tempo al server di avviarsi
realpath=$(realpath ./)
folder1="${realpath}/test/folder1/"
folder2="${realpath}/test/folder2/"
backup="${realpath}/tmp/backup/"
download="${realpath}/tmp/download/"
socket="./tmp/serversock.sk"

./out/client -t200 -f${socket} -d${download} -D${backup}    \
-w${folder1} ,                                              \
-R4

#valgrind --leak-check=full --show-leak-kinds=all -s ./out/client -t200 -f${socket} -D${backup} -W${folder2}3.txt ,  \
./out/client -t200 -f${socket} -D${backup}  \
-L -W${folder2}3.txt ,                      \
-D${backup} -W${folder2}6.txt               \
-W${folder2}5.txt                           \
-a${folder2}5.txt,${folder2}1.txt ,         \
-a${folder2}5.txt,${folder2}2.txt ,         \
-a${folder2}5.txt,${folder2}3.txt ,         \
-a${folder2}5.txt,${folder2}4.txt

./out/client -t200 -f${socket} -d${download}  \
-o${folder2}5.txt                             \
-l${folder2}5.txt                             \
-r${folder2}5.txt                             \
-c${folder2}5.txt
