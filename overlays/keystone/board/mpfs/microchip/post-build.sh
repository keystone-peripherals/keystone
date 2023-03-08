#!/bin/sh

# Add usb network interface

cat <<EOF >> $TARGET_DIR/etc/network/interfaces
auto usb0
iface usb0 inet dhcp
  pre-up /etc/network/nfs_check
EOF

# Enable root ssh

sed -i $TARGET_DIR/etc/ssh/sshd_config \
	-e 's/#PermitRootLogin prohibit-password/PermitRootLogin yes/g'
