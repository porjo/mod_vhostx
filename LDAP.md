LDAP
====
It is assumed you will know how to setup an LDAP server and import a new schema. 

Schema
------
Using the [supplied schema](https://github.com/porjo/mod_vhostx/blob/master/mod_vhostx.schema), a typical virtualhost entry might look like this:

```
dn: cn=u1234,ou=Vhosts,ou=Web,dc=foobar
objectClass: vhostx
objectClass: posixAccount
cn: u1234
homeDirectory: /home/web/u1234/
uid: u1234
uidNumber: 1000
gidNumber: 1000
apacheServerName: example.com
apacheServerAlias: example1.com
apacheServerAlias: example2.com
apacheVhostEnabled: yes
apacheDocumentRoot: /htdocs
apacheChrootDir: /home/web/u1234
phpOptions: session.save_path=/php; display_errors=1
```
The `posixAccount` object class is optional but in practice will always be used.

Additionally, you could add a `userPassword` attribute and use LDAP for authenticating your FTP logins.
