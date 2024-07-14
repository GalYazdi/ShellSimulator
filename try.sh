#!/bin/bash
sleep 100&
sleep 120 &
sleep 140     &      
echo 1 2 3 2> e 
echo "1 2 3" 2> e
echo "1" "2" "3" 2> e
sleep 3 && echo "1" "2" "3" "4"
sleep 2 && echo "1" "2" "3" "4" "5"
echo 1 || echo 1 2 3 4 5
jobs
ls&
jobs
sleep 3&
jobs

