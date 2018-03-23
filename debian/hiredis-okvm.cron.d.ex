#
# Regular cron jobs for the hiredis-okvm package
#
0 4	* * *	root	[ -x /usr/bin/hiredis-okvm_maintenance ] && /usr/bin/hiredis-okvm_maintenance
