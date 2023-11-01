./build.sh release --init --make \
&& ./tools/deploy/obd.sh prepare -p /tmp/obtest \
; ./tools/deploy/obd.sh deploy -c ./tools/deploy/single.yaml