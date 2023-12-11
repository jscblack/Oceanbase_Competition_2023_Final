# full test [sysbech-tpcc-tpch-restart-sysbench-tpcc-tpch]
for file in `find $HOME/.obd/plugins/ -name "analyze.sql"`; do echo "" > $file; done \
&& python3 deploy.py --clean --cluster-home-path ./competition \
&& python3 deploy.py --cluster-home-path ./competition \
&& obd test sysbench obcluster --time=20 --threads=8 --tables=4 --table-size=8989 --database=test --tenant=test --script-name=oltp_point_select \
&& obd test tpcc obcluster --tenant=test --database=test --terminals=10 --run-mins=1 --warehouses=1 --optimization=0 \
&& mysql -S competition/run/sql.sock -uroot@test -e "SET GLOBAL secure_file_priv = '/'" \
&& mysql -S competition/run/sql.sock -uroot@test -e "SET GLOBAL ob_query_timeout = 60000000" \
&& obd test tpch obcluster --tenant=test --database=test --remote-tbl-dir=/data/obcluster/tbl --scale-factor=1 --optimization=0 \
&& obd cluster restart obcluster \
&& obd test sysbench obcluster --time=20 --threads=8 --tables=4 --table-size=8989 --database=test --tenant=test --script-name=oltp_point_select \
&& obd test tpcc obcluster --tenant=test --database=test --terminals=10 --run-mins=1 --warehouses=1 --optimization=0 \
&& mysql -S competition/run/sql.sock -uroot@test -e "SET GLOBAL secure_file_priv = '/'" \
&& mysql -S competition/run/sql.sock -uroot@test -e "SET GLOBAL ob_query_timeout = 60000000" \
&& obd test tpch obcluster --tenant=test --database=test --remote-tbl-dir=/data/obcluster/tbl --scale-factor=1 --optimization=0 \
&& python3 deploy.py --clean --cluster-home-path ./competition \
&& echo "test_done"

# quick test (focus on restart behavior) [tpch-restart-sysbench]
for file in `find $HOME/.obd/plugins/ -name "analyze.sql"`; do echo "" > $file; done \
&& python3 deploy.py --clean --cluster-home-path ./competition \
&& python3 deploy.py --cluster-home-path ./competition \
&& mysql -S competition/run/sql.sock -uroot@test -e "SET GLOBAL secure_file_priv = '/'" \
&& mysql -S competition/run/sql.sock -uroot@test -e "SET GLOBAL ob_query_timeout = 60000000" \
&& obd test tpch obcluster --tenant=test --database=test --remote-tbl-dir=/data/obcluster/tbl --scale-factor=1 --optimization=0 \
&& obd cluster restart obcluster \
&& obd test sysbench obcluster --time=20 --threads=8 --tables=4 --table-size=8989 --database=test --tenant=test --script-name=oltp_point_select \
&& echo "test_done"

# quick test (focus on restart behavior, but more faster) [sysbench-restart-sysbench]
for file in `find $HOME/.obd/plugins/ -name "analyze.sql"`; do echo "" > $file; done \
&& python3 deploy.py --clean --cluster-home-path ./competition \
&& python3 deploy.py --cluster-home-path ./competition \
&& obd test sysbench obcluster --time=20 --threads=8 --tables=4 --table-size=8989 --database=test --tenant=test --script-name=oltp_point_select \
&& obd cluster restart obcluster \
&& obd test sysbench obcluster --time=20 --threads=8 --tables=4 --table-size=8989 --database=test --tenant=test --script-name=oltp_point_select \
&& python3 deploy.py --clean --cluster-home-path ./competition \
&& echo "test_done"

# quick test (only check restart) [restart-sysbench]
for file in `find $HOME/.obd/plugins/ -name "analyze.sql"`; do echo "" > $file; done \
&& python3 deploy.py --clean --cluster-home-path ./competition \
&& python3 deploy.py --cluster-home-path ./competition \
&& obd cluster restart obcluster \
&& obd test sysbench obcluster --time=20 --threads=8 --tables=4 --table-size=8989 --database=test --tenant=test --script-name=oltp_point_select \
&& python3 deploy.py --clean --cluster-home-path ./competition \
&& echo "test_done"

# quick test (only check restart bug) [restart-connect]
python3 deploy.py --clean --cluster-home-path ./competition \
&& python3 deploy.py --cluster-home-path ./competition \
&& obd cluster restart obcluster \
&& obclient -h127.0.0.1 -P2881 -uroot@test  -A -e "create database if not exists test;"

# for kill observer
pidof ./competition/bin/observer | xargs kill -9