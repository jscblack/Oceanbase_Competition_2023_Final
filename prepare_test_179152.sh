./build_179152.sh release -DOB_USE_CCACHE=ON --init --make \
&& rm -rf ./competition \
; mkdir -p ./competition/bin \
&& cp ./build_release/src/observer/observer ./competition/bin/observer