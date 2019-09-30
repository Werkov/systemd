#!/bin/bash
set -e
TEST_DESCRIPTION="test Freeze() and Thaw() dbus API and corresponding systemctl commands"
TEST_NO_NSPAWN=1

. $TEST_BASE_DIR/test-functions

test_setup() {
    create_empty_image_rootdir

    (
        setup_basic_environment
        dracut_install mktemp cut tr timeout

        # setup the testsuite service
        cat >$initdir/etc/systemd/system/testsuite.service <<EOF
[Unit]
Description=Testsuite service
Wants=dbus.service
After=dbus.service

[Service]
ExecStart=/bin/bash -x /testsuite.sh
Type=oneshot
StandardOutput=tty
StandardError=tty
NotifyAccess=all
EOF
        cp testsuite.sh $initdir/

        setup_testsuite
    ) || return 1
    setup_nspawn_root
}

do_test "$@"
