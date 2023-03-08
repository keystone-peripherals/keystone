#!/bin/bash

taskset 0x7 ./profile-runner profile-receiver eyrie-rt &
sleep 10
taskset 0x8 ./profile-runner profile-sender eyrie-rt