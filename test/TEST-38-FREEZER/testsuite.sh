#!/bin/bash

set -ex
set -o pipefail

systemd-analyze log-level debug
systemd-analyze log-target console

unit=foo.service
unit_file=/etc/systemd/system/${unit}

setup_test_service() {
    cat > ${unit_file} <<EOF
[Service]
ExecStart=/bin/sleep 3600
EOF

    systemctl daemon-reload
    systemctl start "${unit}"
}

get_suffix() {
    local unit="$1"

    if [ "$unit" != "${unit%.service}" ]; then
        echo "service"
    elif [ "$unit" != "${unit%.scope}" ]; then
        echo "scope"
    elif  [ "$unit" != "${unit%.slice}" ]; then
        echo "slice"
    else
        echo "error: unexpected unit suffix" >&2
        exit 1
    fi
}

freeze_via_dbus() {
    local suffix=
    suffix=$(get_suffix "$1")

    local name="${1%.$suffix}"
    local object_path="/org/freedesktop/systemd1/unit/${name}_2e${suffix}"
    busctl call \
           org.freedesktop.systemd1 \
           "${object_path}" \
           org.freedesktop.systemd1.Unit \
           Freeze
}

thaw_via_dbus() {
    local suffix=
    suffix=$(get_suffix "$1")

    local name="${1%.$suffix}"
    local object_path="/org/freedesktop/systemd1/unit/${name}_2e${suffix}"

    busctl call \
           org.freedesktop.systemd1 \
           "${object_path}" \
           org.freedesktop.systemd1.Unit \
           Thaw
}

check_freezer_state() {
    local suffix=
    suffix=$(get_suffix "$1")

    local name="${1%.$suffix}"
    local object_path="/org/freedesktop/systemd1/unit/${name}_2e${suffix}"

    state=$(busctl get-property \
                   org.freedesktop.systemd1 \
                   "${object_path}" \
                   org.freedesktop.systemd1.Unit \
                   FreezerState | cut -d " " -f2 | tr -d '"')

    [ "$state" = "$2" ] || {
        echo "error: unexpected freezer state, expected: $2, actual: $state" >&2
        exit 1
    }
}

check_cgroup_state() {
    grep -q "frozen $2" /sys/fs/cgroup/system.slice/"$1"/cgroup.events
}

cleanup() {
    set +e
    systemctl stop "${unit}" >/dev/null 2>&1
    rm -f "${unit_file}"
    systemctl daemon-reload
}

test_dbus_api() {
    echo "Test that DBus API works:"
    echo -n "  - Freeze(): "
    freeze_via_dbus "${unit}"
    check_freezer_state "${unit}" "frozen"
    check_cgroup_state "$unit" 1
    echo "[ OK ]"

    echo -n "  - Thaw(): "
    thaw_via_dbus "${unit}"
    check_freezer_state "${unit}" "running"
    check_cgroup_state "$unit" 0
    echo "[ OK ]"

    echo
}

test_jobs() {
    local pid_before=
    local pid_after=

    echo "Test that it is possible to apply jobs on frozen units:"

    freeze_via_dbus "${unit}"
    check_freezer_state "${unit}" "frozen"

    echo -n "  - restart: "
    pid_before=$(systemctl show -p MainPID "${unit}" | cut -d'=' -f2)
    systemctl restart "${unit}"
    pid_after=$(systemctl show -p MainPID "${unit}" | cut -d'=' -f2)
    [ "$pid_before" != "$pid_after" ] && echo "[ OK ]"

    freeze_via_dbus "${unit}"
    check_freezer_state "${unit}" "frozen"

    echo -n "  - stop: "
    timeout 5s systemctl stop "${unit}"
    echo "[ OK ]"

    echo
}

test_systemctl() {
    echo "Test that systemctl verbs work:"

    systemctl start "$unit"

    echo -n "  - freeze: "
    systemctl freeze "$unit"
    check_freezer_state "${unit}" "frozen"
    check_cgroup_state "$unit" 1
    echo "[ OK ]"

    echo -n "  - thaw: "
    systemctl thaw "$unit"
    check_freezer_state "${unit}" "running"
    check_cgroup_state "$unit" 0
    echo "[ OK ]"

    echo
}

test_slice() {
    local slice="bar.slice"
    local unit="baz.service"

    systemd-run --unit "$unit" --slice "$slice" sleep 3600 >/dev/null 2>&1

    echo "Test freezing the slice (with service inside):"

    echo -n "  - freeze: "
    systemctl freeze "$slice"
    check_freezer_state "${unit}" "frozen"
    check_freezer_state "${slice}" "frozen"
    grep -q "frozen 1" /sys/fs/cgroup/"${slice}"/"${unit}"/cgroup.events
    grep -q "frozen 1" /sys/fs/cgroup/"${slice}"/"${unit}"/cgroup.events
    echo "[ OK ]"

    echo -n "  - thaw: "
    systemctl thaw "$slice"
    check_freezer_state "${unit}" "running"
    check_freezer_state "${slice}" "running"
    grep -q "frozen 0" /sys/fs/cgroup/"${slice}"/"${unit}"/cgroup.events
    grep -q "frozen 0" /sys/fs/cgroup/"${slice}"/"${unit}"/cgroup.events
    echo "[ OK ]"

    systemctl stop "$unit"
    systemctl stop "$slice"

    echo
}

test -e /sys/fs/cgroup/system.slice/cgroup.freeze && {
    trap cleanup EXIT

    setup_test_service
    test_dbus_api
    test_jobs
    test_systemctl
    test_slice
}

echo OK > /testok
exit 0
