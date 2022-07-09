sleep 3 #per dare tempo al server di avviarsi
real_path=$(realpath ./)
path_test1="${real_path}/test/test1/"
path_test2="${real_path}/test/test2/"
path_test3="${real_path}/test/test3/"

serversock="-f./tmp/serversock.sk"
./out/client ${serversock} -l${path_test1}progettosol-20_21.pdf &
./out/client ${serversock} -w${path_test2}  &
./out/client ${serversock} -w${path_test1}
./out/client ${serversock} -w${path_test3}
./out/client ${serversock} -l${path_test1}progettosol-20_21.pdf , -d${real_path}/tmp/download -R3
