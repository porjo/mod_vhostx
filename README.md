mod_vhostx
==========

An Apache module for configuring virtualhosts from LDAP backend.

Features
--------
* Apache processes run as the user/group defined in LDAP. Any scripts run via mod_php also run as the user/group
* Apache processes chrooted to path set from LDAP (optional)
* PHP options can be set dynamically from LDAP at runtime (no Apache restart)
* VirtualHosts can be added/removed/modified at runtime (no Apache restart)
* minimal Apache config 

Requirements
------------
* Apache httpd >= 2.2 + devel libraries, mod_ldap
* OpenLDAP server
* Apache mpm-itk (optional)
* mod_php + PHP devel libraries (optional)

To compile the module from source requires the GNU build tools: `make` `gcc`. 
The Apache `apxs` utility is also needed: on Redhat distros this can be found in the `httpd-devel` package

Installation
------------
Install any of the optional components as required: `mod_php`, `mpm-itk` etc

* Download and unpack the `mod_vhostx` zip file from Github
* As root user, run `make` followed by `make install` (some modification of `Makefile` may be required depending on your environment).
* Update Apache `httpd.conf` configuration as described below
* Restart Apache


Configuration
-------------
The following configuration directives can be used inside `<Virtualhost>` `</Virtualhost>` context

Key | Values | Default | Description
:--- | :---: | :---: | :---
`EnableVhx`     | `on`,`off` | `off`   | Enable virtualhost lookups
`vhx_PathPrefix`| e.g _/home/web_     | - | Prepend this path to DocumentRoot
`vhx_NotFoundRedirect` | e.g. [http://t.co/404.html](http://t.co/404.html) | - | If a host is not found in the database redirect to this URL
`vhx_WWWMode` | `on`,`off` | `off` | If a host is not found, prepend 'www.' to the hostname and try again
`vhx_PHPOpenBasedir` | `on`,`off` | `off` | Set PHP `open_basedir` to include DocumentRoot path
`vhx_PHPOptFromDb` | `on`,`off` | `off` | Set PHP `ini` values from database. A list of valid options can be found [here](http://www.php.net/manual/en/ini.list.php)
`vhx_PHPDisplayErrors` | `on`,`off` | `off` | Output PHP errors to browser.
`vhx_PHPOpenBasedirCommon` | e.g. _/tmp_ | - | Append this path to `open_basedir`. Common to all virtualhosts
`vhx_ITKEnable` | `on`, `off` | `off` | Enable mpm-itk support 
`vhx_ChrootEnable` | `on`, `off` | `off` | Enable chroot of Apache process using path from database (requires mpm-itk)
`vhx_LDAPBindDN` |  | - | Bind DN for LDAP user
`vhx_LDAPBindPassword` |  | - | Bind Password for LDAP user
`vhx_LDAPDereferenceAliases` | `never`, `always`, `searching`, `finding` | `always` | 
`vhx_LDAPUrl` |  | - | LDAP Server connection string. Uses standard URL syntax

An example Apache config is as follows:

```apache
# This needs to load after mod_php otherwise we get 'undefined symbol: zend_alter_ini_entry' 
LoadModule vhostx_module    modules/mod_vhostx.so

<VirtualHost *:80>
 <IfModule vhostx_module>
   EnableVhx On
   vhx_WWWMode On
   vhx_LDAPUrl "ldap://127.0.0.1/ou=Vhosts,ou=Web,dc=foobar???(&(apacheVhostEnabled=yes)(objectClass=apacheConfig))"
   vhx_LDAPBindDN "cn=admin,dc=foobar"
   vhx_LDAPBindPassword "xxxxyyyy"
   vhx_PHPOptFromDb On
   vhx_ITKEnable On
 </IfModule>
</VirtualHost>
```


 
LDAP
----
See [LDAP](https://github.com/porjo/mod_vhostx/blob/master/LDAP.md) documentation

PHP
----
See [PHP](https://github.com/porjo/mod_vhostx/blob/master/PHP.md) documentation

Logs
----
See [Logs](https://github.com/porjo/mod_vhostx/blob/master/LOGS.md) documentation

Troubleshooting
---------------
To see additional debug in `error.log`, compile with the `VH_DEBUG` flag (see Makefile), and set Apache's log level to `debug`.

Credits
--------
The code is a fork of Xavier Beaudouin's [mod_vhs](http://openvisp.fr/doku/doku.php?id=mod_vhs:intro). mod_vhs provides more options than I required, so I've stripped out a lot of unneeded code in an effort keep it clean and maintainable.

`vhx_PHPOptFromDb` code from Cosmomill's mod_vhs [fork](https://bitbucket.org/cosmomill/mod_vhs) and Igor Popov's [mod_myvhost](http://code.google.com/p/mod-myvhost/)
