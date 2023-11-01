./build.sh debug --init --make \
&& ./tools/deploy/obd.sh prepare_debug -p /tmp/obtest \
; ./tools/deploy/obd.sh deploy -c ./tools/deploy/single.yaml