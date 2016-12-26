#!/bin/sh

ulimit -n 1024000
ulimit -c unlimited

p='im_server'

case $1 in 
	start)
	./$p
	sleep 1
	ps -elf | grep $p
	;;
	stop)
    sh /pa/mykill.sh im_server 
	;;
	restart)
    sh /pa/mykill.sh im_server 
	sleep 1
	./$p
	ps -elf | grep $p
	;;
	*)
    sh /pa/mykill.sh im_server 
	sleep 1
	./$p
	sleep 1
	ps -elf | grep $p
	;;
esac
