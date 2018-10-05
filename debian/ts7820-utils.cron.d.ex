#
# Regular cron jobs for the ts7820-utils package
#
0 4	* * *	root	[ -x /usr/bin/ts7820-utils_maintenance ] && /usr/bin/ts7820-utils_maintenance
