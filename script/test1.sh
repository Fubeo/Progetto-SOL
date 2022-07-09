sleep 3 #per dare tempo al server di avviarsi
real_path=$(realpath ./test)
path_test1="${real_path}/test1/"
path_test2="${real_path}/test2/"
path_test3="${real_path}/test3/"

args="-f./tmp/serversock.sk"
./out/client ${args} -w${path_test1}
./out/client ${args} -w${path_test2}
