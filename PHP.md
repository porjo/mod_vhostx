PHP
===

`mod_vhostx` when compiled with `-DHAVE_MOD_PHP_SUPPORT`, uses `zend_alter_ini_entry()` to change parameters on the fly. A list of parameters that can be set using this method can be found on the PHP website [here](http://www.php.net/manual/en/ini.list.php)

PHP options are enabled with the `vhx_PHPOptFromDb` Apache directive and the key/value pairs are taken from the `phpOptions` LDAP attribute. The format is :

`<parameter>=<value>;<parameter>=<value>`


Important Notes
---------------

* parameters set in the system `php.ini` take precedence over any values taken from LDAP
* options taken from LDAP take precedence over any other user-defined values (e.g. in `.htaccess`) 

Warning
-------

Users should not be able to set these values directly as that could potentially expose security holes. It would be better to have a predefined set of values that are known to work well and allow users to select from that list.

