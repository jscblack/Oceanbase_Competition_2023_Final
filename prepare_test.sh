./build.sh release --init --DOB_USE_CCACHE=ON --make \
&& rm -rf ./competition \
; mkdir -p ./competition/bin \
&& cp ./build_release/src/observer/observer ./competition/bin/observer