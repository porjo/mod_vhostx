Here is some hint to get logs per db-vhost given from 
Micha Dalecki and updated by me :)

1. home.conf (libhome)

Add a line to add gecos field with database stuff:
(Here is LDAP configuration, adapt it to your setup)

[mod_vhs]
where wwwDomain
user wwwDomain
home homeDirectory
uid uidNumber
gid gidNumber
gecos wwwDomain
passwd "*"
shell /dev/null

2. In httpd.conf:
LogFormat "%{VH_GECOS}e %h %l %u %t \"%r\" %>s %b \"%{Referer}i\" \"%{User-Agent}i\"" vdbh_combined
Which is exatly the same as:
LogFormat "%h %l %u %t \"%r\" %>s %b \"%{Referer}i\" \"%{User-Agent}i\"" combined
except %{VH_GECOS}e at the beginning.
Don't use %V because it is the content of Host: HTTP header so it cannot be trusted.
Also you must be aware that ${VH_HOST}e is directly extracted from Host: header with some more modifications than %V, but it should NOT be trusted.

So for 'common':
LogFormat "%h %l %u %t \"%r\" %>s %b" common
LogFormat "%{VH_GECOS}e %h %l %u %t \"%r\" %>s %b" vdbh_common

and so other...

3. Make an executable script, in my case:
root@mip ~# cat /usr/sbin/mod_vdbh_log
#!/bin/bash
LOG_PATH=$1
while read name line; do
    echo $line >> $LOG_PATH/${name}_access_log;
done
root@mip ~#

3. Inside <VirtualHost></> define CustomLog like this:
CustomLog "|/usr/sbin/mod_vdbh_log /var/log/httpd/vdbh" vdbh_combined

Restart apache. At this moment you can enjoy separate logs for every database vhost.
Remember to make /var/log/httpd/vdbh directory, and give apache write permission to it.

This can't be done for ErrorLog, because ErrorLog isn't customizable.


