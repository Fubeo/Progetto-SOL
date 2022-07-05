real_path=$(realpath ./test)
path_file1="${real_path}/test1/filenormale.txt"

./out/client -ftmp/serversock.sk -wtest/test1/        &
./out/client -ftmp/serversock.sk -Wtest/test2/client  &
./out/client -ftmp/serversock.sk -wtest/test3/
#sleep 1
#./out/client -ftmp/serversock.sk -dtest/download -r${path_file1}

