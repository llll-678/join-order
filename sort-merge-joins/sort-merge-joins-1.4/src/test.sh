string_list=("m-pass" "m-way" "mpsm")

# Workload A
./configure --enable-key8B && make -j96
for name in "${string_list[@]}"; do
    echo $name
    ./src/sortmergejoins --r-size=16777216 --s-size=268435456 -a $name
done

# Workload B
./configure && make -j96
for name in "${string_list[@]}"; do
    echo $name
    ./src/sortmergejoins -a $name
done