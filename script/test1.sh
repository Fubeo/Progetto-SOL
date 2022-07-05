sleep 3 #per dare tempo al server di avviarsi
real_path=$(realpath ./test)
path_test1="${real_path}/test1/"

args="-f./tmp/serversock.sk -w${path_test1}"
./out/client ${args}, -c${path_test1}filenormale.txt
