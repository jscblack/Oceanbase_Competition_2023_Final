./build.sh release --init --make \
&& mkdir -p ./competition/bin \
&& rm -rf ./competition/* \
&& cp ./build_release/src/observer/observer ./competition/bin/observer \
&& python3 deploy.py --cluster-home-path ./competition