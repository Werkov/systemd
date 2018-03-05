#!/bin/sh

device=$1
[ -n "$device" ] || exit 1

device=${device%.device}
loop=${device#sys-devices-virtual-block-}
[ "$loop" = "$device" ] && exit 1

[ -b "/dev/$loop" ] || exit 1

losetup -d /dev/$loop
