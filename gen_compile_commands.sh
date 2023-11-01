CURRENT_DIR=/root/workspace/oceanbase
TARGET_FILE=compile_commands.json
./build.sh debug --init -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
mv build_debug/$TARGET_FILE $CURRENT_DIR

