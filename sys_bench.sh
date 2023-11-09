#!/bin/bash

# 安装必要的软件包
# sudo apt update
# sudo apt install -y sysbench lscpu dmidecode hdparm fio

echo "收集CPU信息..."
lscpu
echo "运行CPU性能测试..."
sysbench cpu --cpu-max-prime=20000 run

echo "收集内存信息..."
dmidecode --type 17
echo "运行内存带宽测试...（thread=1）"
sysbench memory --memory-block-size=1M --memory-total-size=100G --num-threads=1 --memory-oper=write run

echo "收集磁盘信息..."
lsblk
for i in {4k,8k,16k,32k,64k,128k,256k,512k,1M,2M,4M}
do
    echo "运行磁盘性能测试（随机读写$i）..."
    fio --name=randrw$i --ioengine=libaio --iodepth=1 --rw=randrw --bs=$i --direct=1 --size=1G --numjobs=1 --runtime=60 --group_reporting
done

for i in {1M,2M,4M,8M}
do
    echo "运行磁盘性能测试（顺序读写$i）..."
    fio --name=seqrw$i --ioengine=libaio --iodepth=1 --rw=rw --bs=$i --direct=1 --size=1G --numjobs=1 --runtime=60 --group_reporting
done

echo "测试完成。"