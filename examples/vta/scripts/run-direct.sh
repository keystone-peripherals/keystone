#!/bin/bash

modprobe keystone-driver
sysctl -w vm.max_map_count=6553000
taskset 0x8 ./vta-direct-runner eapp eyrie-rt 128