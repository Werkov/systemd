#!/usr/bin/env bash
set -ex
set -o pipefail

if grep -qw cpu /sys/fs/cgroup/cgroup.controllers ; then
    # cpu controller on the unified hierarchy
    systemd-run --scope --slice=-.slice --unit=test1.scope /usr/bin/sleep infinity &
    # wait for scope startup
    systemd-run --wait --unit=test2.service true

    systemctl set-property test1.scope CPUQuota=25%
    grep -q "25000 100000" /sys/fs/cgroup/test1.scope/cpu.max

    systemctl set-property test1.scope CPUQuota=
    # accept either disabled CPU controller or no limit set
    test ! -f /sys/fs/cgroup/test1.scope/cpu.max || \
        grep -q "max 100000" /sys/fs/cgroup/test1.scope/cpu.max

    # clean up
    systemctl stop test1.scope

elif test -f /sys/fs/cgroup/cpu,cpuacct/cpu.stat ; then
    # cpu controller on the legacy hierarchy
    systemd-run --scope --slice=-.slice --unit=test1.scope /usr/bin/sleep infinity &
    pid=$!
    # wait for scope startup
    systemd-run --wait --unit=test2.service true

    systemctl set-property test1.scope CPUQuota=25%
    grep -q 25000 /sys/fs/cgroup/cpu,cpuacct/test1.scope/cpu.cfs_period_us

    # accept either no CPU hierarchy membership or no limit set
    systemctl set-property test1.scope CPUQuota=
    grep -q "cpu,cpuacct:/$" /proc/$pid/cgroup || \
        grep -q "\-1" /sys/fs/cgroup/cpu,cpuacct/test1.scope/cpu.cfs_period_us


    systemctl stop test1.scope

else
    echo "Skipping no cpu cgroup controller found" >&2
fi

echo OK >/testok
exit 0
