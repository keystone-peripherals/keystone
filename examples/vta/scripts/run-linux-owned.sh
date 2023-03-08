#!/bin/bash

sysctl -w vm.max_map_count=6553000

# This one needs a bunch of drivers
modprobe keystone-driver
modprobe u-dma-buf
modprobe vta_driver
taskset 0x8 ./vta-linux-owned-runner vta-linux-owned-eapp eyrie-rt 192