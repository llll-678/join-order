string_list=("RJ" "PRO" "PRH" "PRHO" "NPO" "NPO_st")

# Workload A
# ./configure --enable-key8B && make -j96
echo "Workload A"
for name in "${string_list[@]}"; do
    echo $name
    ./src/mchashjoins --r-size=16777216 --s-size=268435456 -a $name
done

# # Workload B
# ./configure && make -j96
# echo "Workload B"
# for name in "${string_list[@]}"; do
#     echo $name
#     ./src/mchashjoins -a $name
# done