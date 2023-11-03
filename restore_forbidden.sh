#!/bin/bash
# should be executed before commiting changes to the repository
file_list=("cmake/Env.cmake")

for file in "${file_list[@]}"; do
    url="https://gh.api.99988866.xyz/https://raw.githubusercontent.com/oceanbase/oceanbase/2023-oceanbase-competition/${file}"
    output_file="./${file}"
    curl -o "$output_file" "$url"
done