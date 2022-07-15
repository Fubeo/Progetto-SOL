sleep 3
realpath=$(realpath ./)
folder1="${realpath}/test/folder1/"
folder2="${realpath}/test/folder2/"
backup="${realpath}/tmp/backup/"
download="${realpath}/tmp/download/"
socket="./tmp/serversock.sk"

./out/client -t200 -f${socket} -L        \
-W${folder2}3.txt ,                      \
-W${folder2}6.txt ,                      \
-W${folder2}5.txt ,                      \
-a${folder2}5.txt,${folder2}1.txt ,      \
-a${folder2}5.txt,${folder2}2.txt ,      \
-a${folder2}5.txt,${folder2}3.txt ,      \
-a${folder2}5.txt,${folder2}4.txt

./out/client -t200 -f${socket}  -D${backup}    \
-w${folder1} ,                                 \
-d${download} -R

./out/client -t200 -f${socket} -d${download}   \
-o${folder2}5.txt,${folder1}cani/cani_1.png ,  \
-l${folder2}5.txt,${folder1}cani/cani_1.png ,  \
-r${folder2}5.txt,${folder1}cani/cani_1.png ,  \
-c${folder2}5.txt                           ,  \
-u${folder1}cani/cani_1.png

./out/client -t200 -f${socket}                \
-L -w${folder1}cani ,                         \
-u${folder1}cani/cani_1.png ,                 \
-u${folder1}cani/cani_2.png ,                 \
-u${folder1}cani/cani_3.png ,                 \
-u${folder1}cani/cani_400.png ,                 \
-u${folder1}molti_file/file_piccolo5.txt ,    \
-d${download} -R5