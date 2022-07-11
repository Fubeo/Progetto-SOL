sleep 3 #per dare tempo al server di avviarsi
real_path=$(realpath ./)
path_test1="${real_path}/test/test1/"
path_test2="${real_path}/test/test2/"
path_test3="${real_path}/test/test3/"
path_test30="${real_path}/test/test30/"
backup="${real_path}/tmp/backup/"
download="${real_path}/tmp/download/"

args="-f./tmp/serversock.sk"
./out/client ${args} -t100 -L -D${backup} -w${path_test1}
./out/client ${args} -t100 -L -D${backup} -w${path_test2}
./out/client ${args} -t100 -L -D${backup} -w${path_test3}
./out/client ${args} -t100 -L -D${backup} -w${path_test30}
