#!/bin/bash

sysctl -w vm.max_map_count=6553000
taskset 0x7 ./vta-export-runner export-driver eyrie-rt 192 &
echo "Waiting 10 sec..."
sleep 10
taskset 0x8 ./vta-export-runner export-logic eyrie-rt 192