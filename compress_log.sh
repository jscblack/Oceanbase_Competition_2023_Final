#!/bin/bash

# 设置文件夹路径
folder_path="utility/perf"

# 寻找最新的svg, perf, 和 folded文件
latest_svg=$(ls -t $folder_path/*.svg | head -n 1)
latest_perf=$(ls -t $folder_path/*.perf | head -n 1)
latest_folded=$(ls -t $folder_path/*.folded | head -n 1)

# 压缩这些文件
zip "latest_$(date +%Y%m%d_%H-%M-%S).zip" -r "$latest_svg" "$latest_perf" "$latest_folded" competition/log

echo "Files compressed successfully."
