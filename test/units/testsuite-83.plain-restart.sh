#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
set -eux
set -o pipefail

DUMMY_SERVICE=/tmp/dummy-service.sh

cat >"$DUMMY_SERVICE" <<EOF
#!/usr/bin/env bash

state=2
function soft_reset() {
    echo "Handling reset request..."
    state=1
    echo "Passivated"
}
trap soft_reset SIGHUP

function full_stop() {
    echo "Handling stop request..."
    state=0
    echo "Stopped"
}
trap full_stop SIGTERM

# half seconds
lameduck=8
echo "Running main loop..."
while [ "\$state" -gt 0 -a "\$lameduck" -gt 0 ] ; do
    sleep .5
    if [ "\$state" -eq 1 ] ; then
       lameduck=\$((\$lameduck - 1))
    fi
done
echo "Exiting \$state:\$lameduck"
EOF

chmod u+x "$DUMMY_SERVICE"

cat >/run/systemd/system/testservice-83-foo#.service <<EOF
[Service]
ExecStart=$DUMMY_SERVICE
ExecStop=-/usr/bin/kill -SIGTERM \$MAINPID
ExecRestartPre=/usr/bin/kill -SIGHUP \$MAINPID
ExecRestartPre=/usr/bin/sleep 1
RuntimePassiveMaxSec=infinity
EOF

systemctl daemon-reload

### test simple ZEDR

systemctl start testservice-83-foo.service
# check one gen exists
active_inst="$(systemctl show -p Following --value testservice-83-foo.service)"
[[ "$active_inst" =~ testservice-83-foo#[^.]*.service ]]
systemctl is-active "$active_inst"

systemctl restart testservice-83-foo.service
# check two gens exist
active_inst2="$(systemctl show -p Following --value testservice-83-foo.service)"
[[ "$active_inst" != "$active_inst2" ]]
systemctl is-active "$active_inst"
systemctl is-active "$active_inst2"

sleep 5
# check one gen exists
! systemctl is-active "$active_inst"
systemctl is-active "$active_inst2"

systemctl stop testservice-83-foo.service
sleep 1
# check no gen exists
! systemctl is-active "$active_inst"
! systemctl is-active "$active_inst2"


# TODO add other tests for ZEDR failure modes/interactions
