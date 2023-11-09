./build.sh release --init --make \
&& rm -rf ./competition \
; mkdir -p ./competition/bin \
&& cp ./build_release/src/observer/observer ./competition/bin/observer