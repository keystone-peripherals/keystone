#!/bin/bash

sysctl -w vm.max_map_count=6553000
taskset 0x7 ./vta-msg-pass-runner mp-receiver eyrie-rt 192 &
sleep 5
taskset 0x8 ./vta-msg-pass-runner mp-sender eyrie-rt 128