Logging
=======

mod_vhostx populates the `VH_GECOS` environment variable with the contents of `apacheServerName` LDAP attribute.

This allows one log file per virtualhost, regardless of how many `apacheServerAlias` attributes there may be.

Then in your Apache config, include the following log config:
```
LogFormat "%{VH_GECOS}e %h %l %u %t \"%r\" %>s %b \"%{Referer}i\" \"%{User-Agent}i\"" vhostx_combined
```
The use a `CustomLog` directive inside your `<VirtualHost></>` context referencing `vhost_combined`. The log can then be piped to your favourite log handling script. Refer to [Apache documentation](http://httpd.apache.org/docs/2.2/mod/mod_log_config.html#customlog) for more information.

This can't be done for ErrorLog, because ErrorLog isn't customizable.


