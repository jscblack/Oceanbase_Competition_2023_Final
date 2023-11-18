for file in `find $HOME/.obd/plugins/ -name "analyze.sql"`; do echo "" > $file; done \
&& python3 deploy_local.py --clean --cluster-home-path ./competition \
&& python3 deploy_local.py --cluster-home-path ./competition \
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
&& python3 deploy_local.py --clean --cluster-home-path ./competition \
&& echo "test_done"

# below for quick test
for file in `find $HOME/.obd/plugins/ -name "analyze.sql"`; do echo "" > $file; done \
&& python3 deploy_local.py --clean --cluster-home-path ./competition \
&& python3 deploy_local.py --cluster-home-path ./competition \
&& mysql -S competition/run/sql.sock -uroot@test -e "SET GLOBAL secure_file_priv = '/'" \
&& mysql -S competition/run/sql.sock -uroot@test -e "SET GLOBAL ob_query_timeout = 60000000" \
&& obd test tpch obcluster --tenant=test --database=test --remote-tbl-dir=/data/obcluster/tbl --scale-factor=1 --optimization=0 \
&& obd cluster restart obcluster \
&& obd test sysbench obcluster --time=20 --threads=8 --tables=4 --table-size=8989 --database=test --tenant=test --script-name=oltp_point_select \
&& python3 deploy_local.py --clean --cluster-home-path ./competition \
&& echo "test_done"