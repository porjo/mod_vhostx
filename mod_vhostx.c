/*
 * ====================================================================
 * The Apache Software License, Version 1.1
 *
 * Copyright (c) 2000 The Apache Software Foundation.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. The end-user documentation included with the redistribution, if any, must
 * include the following acknowledgment: "This product includes software
 * developed by the Apache Software Foundation (http://www.apache.org/)."
 * Alternately, this acknowledgment may appear in the software itself, if and
 * wherever such third-party acknowledgments normally appear.
 *
 * 4. The names "Apache" and "Apache Software Foundation" must not be used to
 * endorse or promote products derived from this software without prior
 * written permission. For written permission, please contact
 * apache@apache.org.
 *
 * 5. Products derived from this software may not be called "Apache", nor may
 * "Apache" appear in their name, without prior written permission of the
 * Apache Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE APACHE SOFTWARE FOUNDATION OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * ====================================================================
 */
/* 
 * Based on mod_vhs by Xavier Beaudouin <kiwi (at) oav (dot) net>
 *
 * Additional code from:
 * - mod_vhs (fork) by Rene Kanzler <rk (at) cosmomill (dot) de>
 * - mod_myvhost by Igor Popov
 */


#include "mod_vhostx.h"

static char * trimwhitespace(char *str);
static int getldaphome(request_rec *r, vhx_config_rec *vhr, const char *hostname, vhx_request_t *reqc);

// Why does this need 9 elements? Any less and we get a segfault!
char * ldap_attributes[] = { "apacheServerName", "apacheDocumentRoot", "apacheServerAdmin","phpOptions","apacheChrootDir","homeDirectory","uidNumber","gidNumber","blah" };

static APR_OPTIONAL_FN_TYPE(uldap_connection_close)  *util_ldap_connection_close;
static APR_OPTIONAL_FN_TYPE(uldap_connection_find)   *util_ldap_connection_find;
static APR_OPTIONAL_FN_TYPE(uldap_cache_comparedn)   *util_ldap_cache_comparedn;
static APR_OPTIONAL_FN_TYPE(uldap_cache_compare)     *util_ldap_cache_compare;
static APR_OPTIONAL_FN_TYPE(uldap_cache_checkuserid) *util_ldap_cache_checkuserid;
static APR_OPTIONAL_FN_TYPE(uldap_cache_getuserdn)   *util_ldap_cache_getuserdn;
static APR_OPTIONAL_FN_TYPE(uldap_ssl_supported)     *util_ldap_ssl_supported;

static void ImportULDAPOptFn(void)
{
	util_ldap_connection_close  = APR_RETRIEVE_OPTIONAL_FN(uldap_connection_close);
	util_ldap_connection_find   = APR_RETRIEVE_OPTIONAL_FN(uldap_connection_find);
	util_ldap_cache_comparedn   = APR_RETRIEVE_OPTIONAL_FN(uldap_cache_comparedn);
	util_ldap_cache_compare     = APR_RETRIEVE_OPTIONAL_FN(uldap_cache_compare);
	util_ldap_cache_checkuserid = APR_RETRIEVE_OPTIONAL_FN(uldap_cache_checkuserid);
	util_ldap_cache_getuserdn   = APR_RETRIEVE_OPTIONAL_FN(uldap_cache_getuserdn);
	util_ldap_ssl_supported     = APR_RETRIEVE_OPTIONAL_FN(uldap_ssl_supported);
}


/*
 * Let's start coding
 */
module AP_MODULE_DECLARE_DATA vhostx_module;


/*
 * Apache per server config structure
 */
static void * vhx_create_server_config(apr_pool_t * p, server_rec * s) {
	vhx_config_rec *vhr = (vhx_config_rec *) apr_pcalloc(p, sizeof(vhx_config_rec));

	/*
	 * Pre default the module is not enabled
	 */
	vhr->enable 		= 0;

	vhr->ldap_binddn	= NULL;
	vhr->ldap_bindpw	= NULL;
	vhr->ldap_have_url	= 0;
	vhr->ldap_have_deref	= 0;
	vhr->ldap_deref		= always;
	vhr->ldap_set_filter	= 0;

#ifdef HAVE_MPM_ITK_SUPPORT
	vhr->itk_enable 	= 0;
	vhr->chroot_enable 	= 0;
#endif /* HAVE_MPM_ITK_SUPPORT */

	return (void *)vhr;
}

/*
 * Apache merge per server config structures
 */
static void * vhx_merge_server_config(apr_pool_t * p, void *parentv, void *childv) {
	vhx_config_rec *parent	= (vhx_config_rec *) parentv;
	vhx_config_rec *child	= (vhx_config_rec *) childv;
	vhx_config_rec *conf	= (vhx_config_rec *) apr_pcalloc(p, sizeof(vhx_config_rec));

	conf->enable 		= (child->enable ? child->enable : parent->enable);
	conf->path_prefix 	= (child->path_prefix ? child->path_prefix : parent->path_prefix);
	conf->default_host 	= (child->default_host ? child->default_host : parent->default_host);
	conf->lamer_mode 	= (child->lamer_mode ? child->lamer_mode : parent->lamer_mode);
	conf->log_notfound 	= (child->log_notfound ? child->log_notfound : parent->log_notfound);

#ifdef HAVE_MOD_PHP_SUPPORT
	conf->open_basedir 	= (child->open_basedir ? child->open_basedir : parent->open_basedir);
	conf->display_errors 	= (child->display_errors ? child->display_errors : parent->display_errors);
	conf->append_basedir 	= (child->append_basedir ? child->append_basedir : parent->append_basedir);
	conf->openbdir_path 	= (child->openbdir_path ? child->openbdir_path : parent->openbdir_path);
	conf->phpopt_fromdb 	= (child->phpopt_fromdb ? child->phpopt_fromdb : parent->phpopt_fromdb);
#endif /* HAVE_MOD_PHP_SUPPORT */

#ifdef HAVE_MPM_ITK_SUPPORT
	conf->itk_enable = (child->itk_enable ? child->itk_enable : parent->itk_enable);
	conf->chroot_enable = (child->chroot_enable ? child->chroot_enable : parent->chroot_enable);
#endif /* HAVE_MPM_ITK_SUPPORT */

	if (child->ldap_have_url) {
		conf->ldap_have_url   = child->ldap_have_url;
		conf->ldap_url        = child->ldap_url;
		conf->ldap_host       = child->ldap_host;
		conf->ldap_port       = child->ldap_port;
		conf->ldap_basedn     = child->ldap_basedn;
		conf->ldap_scope      = child->ldap_scope;
		conf->ldap_filter     = child->ldap_filter;
		conf->ldap_secure     = child->ldap_secure;
		conf->ldap_set_filter = child->ldap_set_filter;
	} else {
		conf->ldap_have_url   = parent->ldap_have_url;
		conf->ldap_url        = parent->ldap_url;
		conf->ldap_host       = parent->ldap_host;
		conf->ldap_port       = parent->ldap_port;
		conf->ldap_basedn     = parent->ldap_basedn;
		conf->ldap_scope      = parent->ldap_scope;
		conf->ldap_filter     = parent->ldap_filter;
		conf->ldap_secure     = parent->ldap_secure;
		conf->ldap_set_filter = parent->ldap_set_filter;
	}
	if (child->ldap_have_deref) {
		conf->ldap_have_deref = child->ldap_have_deref;
		conf->ldap_deref      = child->ldap_deref;
	} else {
		conf->ldap_have_deref = parent->ldap_have_deref;
		conf->ldap_deref      = parent->ldap_deref;
	}

	conf->ldap_binddn = (child->ldap_binddn ? child->ldap_binddn : parent->ldap_binddn);
	conf->ldap_bindpw = (child->ldap_bindpw ? child->ldap_bindpw : parent->ldap_bindpw);

	return conf;
}


/*
 * Set the fields inside the conf struct
 */
static const char * set_field(cmd_parms * parms, void *mconfig, const char *arg) {
	long pos = (long) parms->info;

	vhx_config_rec *vhr = (vhx_config_rec *) ap_get_module_config(parms->server->module_config, &vhostx_module);

	switch (pos) {
		case 1:
			vhr->path_prefix = apr_pstrdup(parms->pool, arg);
			break;
		case 2:
			vhr->default_host = apr_pstrdup(parms->pool, arg);
			break;
#ifdef HAVE_MOD_PHP_SUPPORT
		case 3:
			vhr->openbdir_path = apr_pstrdup(parms->pool, arg);
			break;
#endif /* HAVE_MOD_PHP_SUPPORT */

		case 4:
			vhr->ldap_binddn = apr_pstrdup(parms->pool, arg);
			break;

		case 5:
			vhr->ldap_bindpw = apr_pstrdup(parms->pool, arg);
			break;

		case 6:
			if (apr_strnatcasecmp(arg, "never") == 0 || apr_strnatcasecmp(arg, "off") == 0) {
				vhr->ldap_deref = never;
				vhr->ldap_have_deref = 1;
			}
			else if (apr_strnatcasecmp(arg, "searching") == 0) {
				vhr->ldap_deref = searching;
				vhr->ldap_have_deref = 1;
			}
			else if (apr_strnatcasecmp(arg, "finding") == 0) {
				vhr->ldap_deref = finding;
				vhr->ldap_have_deref = 1;
			}
			else if (apr_strnatcasecmp(arg, "always") == 0 || apr_strnatcasecmp(arg, "on") == 0) {
				vhr->ldap_deref = always;
				vhr->ldap_have_deref = 1;
			}
			else {
				return "Unrecognized value for vhx_DAPAliasDereference directive";
			}
			break;
	}
	return NULL;
}

/*
 * LDAP Parse URL :
 * Use the ldap url parsing routines to break up the LDAP  URL into
 * host and port.
 * Is out of set_field because it is very big stuff
 */
static const char *vhx_ldap_parse_url(cmd_parms *cmd, void *dummy, const char *url) {
	int result;
	apr_ldap_url_desc_t *urld;
	apr_ldap_err_t	*ldap_result;

	vhx_config_rec *vhr = (vhx_config_rec *) ap_get_module_config(cmd->server->module_config, &vhostx_module);

	VH_AP_LOG_ERROR(APLOG_MARK, APLOG_DEBUG|APLOG_NOERRNO, 0,
			cmd->server, "ldap url parse: `%s'",
			url);

	result = apr_ldap_url_parse(cmd->pool, url, &(urld), &(ldap_result));
	if (result != APR_SUCCESS) {
		VH_AP_LOG_ERROR(APLOG_MARK, APLOG_DEBUG, 0, cmd->server, "ldap url not parsed : %s.", ldap_result->reason);
		return ldap_result->reason;
	}
	vhr->ldap_url = apr_pstrdup(cmd->pool, url);

	VH_AP_LOG_ERROR(APLOG_MARK, APLOG_DEBUG|APLOG_NOERRNO, 0,
			cmd->server, "ldap url parse: Host: %s", urld->lud_host);
	VH_AP_LOG_ERROR(APLOG_MARK, APLOG_DEBUG|APLOG_NOERRNO, 0,
			cmd->server, "ldap url parse: Port: %d", urld->lud_port);
	VH_AP_LOG_ERROR(APLOG_MARK, APLOG_DEBUG|APLOG_NOERRNO, 0,
			cmd->server, "ldap url parse: DN: %s", urld->lud_dn);
	VH_AP_LOG_ERROR(APLOG_MARK, APLOG_DEBUG|APLOG_NOERRNO, 0,
			cmd->server, "ldap url parse: attrib: %s", urld->lud_attrs? urld->lud_attrs[0] : "(null)");
	VH_AP_LOG_ERROR(APLOG_MARK, APLOG_DEBUG|APLOG_NOERRNO, 0,
			cmd->server, "ldap url parse: scope: %s",
			(urld->lud_scope == LDAP_SCOPE_SUBTREE? "subtree" :
			 urld->lud_scope == LDAP_SCOPE_BASE? "base" :
			 urld->lud_scope == LDAP_SCOPE_ONELEVEL? "onelevel" : "unknown"));
	VH_AP_LOG_ERROR(APLOG_MARK, APLOG_DEBUG|APLOG_NOERRNO, 0,
			cmd->server, "ldap url parse: filter: %s", urld->lud_filter);

	/* Set all the values, or at least some sane defaults */
	if (vhr->ldap_host) {
		char *p = apr_palloc(cmd->pool, strlen(vhr->ldap_host) + strlen(urld->lud_host) + 2);
		strcpy(p, urld->lud_host);
		strcat(p, " ");
		strcat(p, vhr->ldap_host);
		vhr->ldap_host = p;
	}
	else {
		vhr->ldap_host = urld->lud_host? apr_pstrdup(cmd->pool, urld->lud_host) : "localhost";
	}
	vhr->ldap_basedn = urld->lud_dn? apr_pstrdup(cmd->pool, urld->lud_dn) : "";

	vhr->ldap_scope = urld->lud_scope == LDAP_SCOPE_ONELEVEL ? LDAP_SCOPE_ONELEVEL : LDAP_SCOPE_SUBTREE;

	if (urld->lud_filter) {
		if (urld->lud_filter[0] == '(') {
			/*
			 * Get rid of the surrounding parens; later on when generating the
			 * filter, they'll be put back.
			 */
			vhr->ldap_filter = apr_pstrdup(cmd->pool, urld->lud_filter+1);
			vhr->ldap_filter[strlen(vhr->ldap_filter)-1] = '\0';
		}
		else {
			vhr->ldap_filter = apr_pstrdup(cmd->pool, urld->lud_filter);
		}
	}
	else {
		vhr->ldap_filter = "objectClass=apacheConfig";
	}

	/*
	 *  "ldaps" indicates secure ldap connections desired
	 */
	if (apr_strnatcasecmp(url, "ldaps") == 0)
	{
		vhr->ldap_secure = 1;
		vhr->ldap_port = urld->lud_port? urld->lud_port : LDAPS_PORT;

		VH_AP_LOG_ERROR(APLOG_MARK, APLOG_DEBUG|APLOG_NOERRNO, 0, cmd->server,
				"LDAP: using SSL connections");
	}
	else
	{
		vhr->ldap_secure = 0;
		vhr->ldap_port = urld->lud_port? urld->lud_port : LDAP_PORT;
		VH_AP_LOG_ERROR(APLOG_MARK, APLOG_DEBUG|APLOG_NOERRNO, 0, cmd->server,
				"LDAP: not using SSL connections");
	}

	vhr->ldap_have_url = 1;
	return NULL;
}

/*
 * To setting flags
 */
static const char * set_flag(cmd_parms * parms, void *mconfig, int flag) {
	long pos = (long) parms->info;
	vhx_config_rec *vhr = (vhx_config_rec *) ap_get_module_config(parms->server->module_config, &vhostx_module);

	/*	VH_AP_LOG_ERROR(APLOG_MARK, APLOG_DEBUG, 0, parms->server,
		"set_flag:Flag='%d' for server: '%s' for pos='%d' line: %d",
		flag, parms->server->defn_name, pos, parms->server->defn_line_number ); */
	switch (pos) {
		case 0:
			if (flag) {
				vhr->lamer_mode = 1;
			} else {
				vhr->lamer_mode = 0;
			}
			break;
#ifdef HAVE_MOD_PHP_SUPPORT
		case 2:
			if (flag) {
				vhr->open_basedir = 1;
			} else {
				vhr->open_basedir = 0;
			}
			break;
		case 3:
			if (flag) {
				vhr->phpopt_fromdb = 1;
			} else {
				vhr->phpopt_fromdb = 0;
			}
			break;
		case 4:
			if (flag) {
				vhr->display_errors = 1;
			} else {
				vhr->display_errors = 0;
			}
			break;
#endif				/* HAVE_MOD_PHP_SUPPORT */
		case 5:
			if (flag) {
				vhr->enable = 1;
			} else {
				vhr->enable = 0;
			}
			break;
#ifdef HAVE_MOD_PHP_SUPPORT
		case 6:
			if (flag) {
				vhr->append_basedir = 1;
			} else {
				vhr->append_basedir = 0;
			}
			break;
#endif				/* HAVE_MOD_PHP_SUPPORT */
		case 7:
			if (flag) {
				vhr->log_notfound = 1;
			} else {
				vhr->log_notfound = 0;
			}
			break;
#ifdef HAVE_MPM_ITK_SUPPORT
		case 8:
			if (flag) {
				vhr->itk_enable = 1;
			} else {
				vhr->itk_enable = 0;
			}
			break;
		case 9:
			if (flag) {
				vhr->chroot_enable = 1;
			} else {
				vhr->chroot_enable = 0;
			}
			break;
#endif /* HAVE_MPM_ITK_SUPPORT  */
	}
	return NULL;
}


static int vhx_init_handler(apr_pool_t * pconf, apr_pool_t * plog, apr_pool_t * ptemp, server_rec * s) {

	/* make sure that mod_ldap (util_ldap) is loaded */
	if (ap_find_linked_module("util_ldap.c") == NULL) {
		VH_AP_LOG_ERROR(APLOG_MARK, APLOG_ERR|APLOG_NOERRNO, 0, s,
				"%s: (vhx_init_handler) module mod_ldap missing. Mod_ldap (aka. util_ldap) "
				"must be loaded in order for vhx to function properly",VH_NAME);
		return HTTP_INTERNAL_SERVER_ERROR;
	}

	VH_AP_LOG_ERROR(APLOG_MARK, APLOG_INFO, 0, s, "Loading %s version %s", VH_NAME,VH_VERSION);

	ap_add_version_component(pconf, VH_VERSION);

#ifdef HAVE_MPM_ITK_SUPPORT
	unsigned short int itk_enable = 1;
	server_rec *sp;

	module *mpm_itk_module = ap_find_linked_module("itk.c");
	if (mpm_itk_module == NULL) {
		VH_AP_LOG_ERROR(APLOG_MARK, APLOG_ERR, 0, s, "%s: (vhx_init_handler) itk.c is not loaded",VH_NAME);
		itk_enable = 0;
	}

	for (sp = s; sp; sp = sp->next) {
		vhx_config_rec *vhr = (vhx_config_rec *) ap_get_module_config(sp->module_config, &vhostx_module);

		if (vhr->itk_enable) {
			if (!itk_enable) {
				vhr->itk_enable = 0;
			} else {
				itk_conf *cfg = (itk_conf *) ap_get_module_config(sp->module_config, mpm_itk_module);
				vhr->itk_defuid = cfg->uid;
				vhr->itk_defgid = cfg->gid;
				//vhr->itk_defusername = DEFAULT_USER;

				VH_AP_LOG_ERROR(APLOG_MARK, APLOG_DEBUG, 0, sp, "vhx_init_handler: itk uid='%d' itk gid='%d' "/*itk username='%s'*/, cfg->uid, cfg->gid/*, cfg->username */);
			}
		}       
	}
#endif /* HAVE_MPM_ITK_SUPPORT  */

	return OK;
}

/*
 * Used for redirect subsystem when a hostname is not found
 */
static int vhx_redirect_stuff(request_rec * r, vhx_config_rec * vhr)
{
	if (vhr->default_host) {
		//TODO: sanity check required here on URL
		apr_table_setn(r->headers_out, "Location", vhr->default_host);
		VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "redirect_stuff: using a redirect to %s for %s", vhr->default_host, r->hostname);
		return HTTP_MOVED_TEMPORARILY;
	}
	/* Failsafe */
	VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "%s: (vhx_redirect_stuff) no host found (non HTTP/1.1 request, no default set) %s", VH_NAME, r->hostname);
	return DECLINED;
}

#ifdef HAVE_MPM_ITK_SUPPORT
/*
 * This function will configure MPM-ITK
 */
static int vhx_itk_post_read(request_rec *r)
{
	uid_t libhome_uid;
	gid_t libhome_gid;
	int vhost_found_by_request = DECLINED;

	vhx_config_rec *vhr = (vhx_config_rec *) ap_get_module_config(r->server->module_config, &vhostx_module);
	VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "vhx_itk_post_read: BEGIN ***,pid=%d",getpid());

	vhx_request_t *reqc;

	reqc = ap_get_module_config(r->request_config, &vhostx_module);	
	if (!reqc) {
		reqc = (vhx_request_t *)apr_pcalloc(r->pool, sizeof(vhx_request_t));
		reqc->vhost_found = VH_VHOST_INFOS_NOT_YET_REQUESTED;
		ap_set_module_config(r->request_config, &vhostx_module, reqc);
	}

	vhx_conn_cfg_t *conn_cfg =
		(vhx_conn_cfg_t*) ap_get_module_config(r->connection->conn_config,
				&vhostx_module);

	if (!conn_cfg) {
		conn_cfg = apr_pcalloc(r->connection->pool, sizeof(vhx_conn_cfg_t));
		conn_cfg->last_vhost = NULL;
		conn_cfg->last_vhost_status = VH_VHOST_INFOS_NOT_YET_REQUESTED;
		ap_set_module_config(r->connection->conn_config, &vhostx_module, conn_cfg);
	}

	if(r->hostname != NULL) {
		conn_cfg->last_vhost = apr_pstrdup(r->pool, r->hostname);
		vhost_found_by_request = getldaphome(r, vhr, r->hostname, reqc);
		if (vhost_found_by_request == OK) {
			libhome_uid = atoi(reqc->uid);
			libhome_gid = atoi(reqc->gid);
		}
		else {
			if (vhr->lamer_mode) {
				if ((strncasecmp(r->hostname, "www.",4) == 0) && (strlen(r->hostname) > 4)) {
					VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "vhx_itk_post_read: Lamer friendly mode engaged");
					char           *lhost;
					lhost = apr_pstrdup(r->pool, r->hostname + 4);
					VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "vhx_itk_post_read: Found a lamer for %s -> %s", r->hostname, lhost);
					vhost_found_by_request = getldaphome(r, vhr, lhost, reqc);
					if (vhost_found_by_request == OK) {
						libhome_uid = atoi(reqc->uid);
						libhome_gid = atoi(reqc->gid);
						VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "vhx_itk_post_read: lamer for %s -> %s has itk uid='%d' itk gid='%d'", r->hostname, lhost, libhome_uid, libhome_gid);
					} else {
						libhome_uid = vhr->itk_defuid;
						libhome_gid = vhr->itk_defgid;
						VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "vhx_itk_post_read: no lamer found for %s set default itk uid='%d' itk gid='%d'", r->hostname, libhome_uid, libhome_gid);
					}
				} else { 
					libhome_uid = vhr->itk_defuid;
					libhome_gid = vhr->itk_defgid;
				}
			} else {
				libhome_uid = vhr->itk_defuid;
				libhome_gid = vhr->itk_defgid;
			}

		}

		if (vhost_found_by_request == OK) {
			conn_cfg->last_vhost_status = VH_VHOST_INFOS_FOUND;
			/* If ITK support is not enabled, then don't process request */
			if (vhr->itk_enable) {
				module *mpm_itk_module = ap_find_linked_module("itk.c");

				if (mpm_itk_module == NULL) {
					VH_AP_LOG_RERROR(APLOG_MARK, APLOG_ERR, 0, r, "%s: (vhx_itk_post_read) itk.c is not loaded",VH_NAME);
					return HTTP_INTERNAL_SERVER_ERROR;
				}
				itk_conf *cfg = (itk_conf *) ap_get_module_config(r->per_dir_config, mpm_itk_module);

				VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "vhx_itk_post_read: itk uid='%d' itk gid='%d' itk username='%s' before change", cfg->uid, cfg->gid, cfg->username);

				char *itk_username = NULL;
				struct passwd *pw = getpwuid(libhome_uid); 

				if(pw != NULL)
				{
					cfg->uid = libhome_uid;
					cfg->gid = libhome_gid;

					/* set the username - otherwise MPM-ITK will not work */
					itk_username = apr_psprintf(r->pool, "%s", pw->pw_name); 
					cfg->username = itk_username;

					//chroot
					if(vhr->chroot_enable != 0 && reqc->chroot_dir != NULL) {
						if (ap_is_directory(r->pool, reqc->chroot_dir)) {
							if(chroot(reqc->chroot_dir)<0) {
								VH_AP_LOG_RERROR(APLOG_MARK, APLOG_WARNING, 0, r, "%s: (vhx_itk_post_read) couldn't chroot to %s", VH_NAME, reqc->chroot_dir);
								if(chdir("/")<0) {
									VH_AP_LOG_RERROR(APLOG_MARK, APLOG_WARNING, 0, r, "vhx_itk_post_read: couldn't chdir to / after chroot to '%s'",reqc->chroot_dir);
								}
							}
							else {
								VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "vhx_itk_post_read: chroot to %s",reqc->chroot_dir);
							}
						}
						else {
							VH_AP_LOG_RERROR(APLOG_MARK, APLOG_WARNING, 0, r, "%s: (vhx_itk_post_read) chroot directory '%s' does not exist. Skipping chroot", VH_NAME, reqc->chroot_dir);
						}
					}
					else {
						VH_AP_LOG_RERROR(APLOG_MARK, APLOG_WARNING, 0, r, "vhx_itk_post_read: chroot not enabled or chroot_dir empty. Skipping chroot.");
					}
				}
				VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "vhx_itk_post_read: itk uid='%d' itk gid='%d' itk username='%s' after change", cfg->uid, cfg->gid, cfg->username);
			}
		}
		else {
			VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "vhx_itk_post_read: set VH_VHOST_INFOS_NOT_FOUND");
			reqc->vhost_found = VH_VHOST_INFOS_NOT_FOUND;
			conn_cfg->last_vhost_status = VH_VHOST_INFOS_NOT_FOUND;
		}
	}
	VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "vhx_itk_post_read: END ***");
	return OK;
}
#endif /* HAVE_MPM_ITK_SUPPORT  */


#ifdef HAVE_MOD_PHP_SUPPORT
/*
 * This function will configure on the fly the php like php.ini will do
 */
static void vhx_php_config(request_rec * r, vhx_config_rec * vhr, char *path, char *phpoptstr)
{
	/*
	 * Some Basic PHP stuff, thank to Igor Popov module
	 */
	apr_table_set(r->subprocess_env, "PHP_DOCUMENT_ROOT", path);
	//zend_alter_ini_entry("doc_root", sizeof("doc_root"), path, strlen(path), 4, 1);
	zend_alter_ini_entry("doc_root", sizeof("doc_root"), path, strlen(path), ZEND_INI_SYSTEM, ZEND_INI_STAGE_ACTIVATE);

	/*
	 * vhx_PHPopt_fromdb
	 */
	if (vhr->phpopt_fromdb) {
		VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "vhx_php_config: PHP from DB engaged");
		char           *retval;
		char           *state;
		char           *myphpoptions;

		myphpoptions = apr_pstrdup(r->pool, phpoptstr);
		if(myphpoptions != NULL) {
			VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "vhx_php_config: DB => %s", myphpoptions);

			if ((ap_strchr(myphpoptions, '=') != NULL)) {
				/* Getting values for PHP there so we can proceed */

				retval = apr_strtok(myphpoptions, ";", &state);
				while (retval != NULL) {
					char           *key = NULL;
					char           *val = NULL;
					char           *strtokstate = NULL;

					key = apr_strtok(retval, "=", &strtokstate);
					val = apr_strtok(NULL, "=", &strtokstate);

					if(key != NULL && val != NULL) {
						key = trimwhitespace(key);
						val = trimwhitespace(val);
						VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "vhx_php_config: Zend PHP option => %s => %s", key, val);
						//zend_alter_ini_entry(key, strlen(key) + 1, val, strlen(val), 4, 16);
						zend_alter_ini_entry(key, strlen(key) + 1, val, strlen(val), ZEND_INI_SYSTEM, ZEND_INI_STAGE_RUNTIME);

						if(apr_strnatcasecmp(key, "display_errors") == 0) {
							vhr->display_errors = atoi(val);
						}

					} else if( key != NULL && val == NULL) {
						VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "vhx_php_config: PHP option key '%s' had empty value, skipping...", key);
					}

					retval = apr_strtok(NULL, ";", &state);
				}
			}
		}
		else {
			VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "vhx_php_config: no PHP options found in DB");
		}
	}

	/*
	 * vhx_PHPopen_baserdir    \ vhx_append_open_basedir |  support
	 * vhx_open_basedir_path   /
	 */
	if (vhr->open_basedir) {
		if (vhr->append_basedir && vhr->openbdir_path) {
			/*
			 * There is a default open_basedir path and
			 * configuration allow appending them
			 */
			char *obasedir_path;

			if (vhr->path_prefix) {
				obasedir_path = apr_pstrcat(r->pool, vhr->openbdir_path, ":", vhr->path_prefix, path, NULL);
			} else {
				obasedir_path = apr_pstrcat(r->pool, vhr->openbdir_path, ":", path, NULL);
			}
			//zend_alter_ini_entry("open_basedir", 13, obasedir_path, strlen(obasedir_path), 4, 16);
			zend_alter_ini_entry("open_basedir", 13, obasedir_path, strlen(obasedir_path), ZEND_INI_SYSTEM, ZEND_INI_STAGE_RUNTIME);
			VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "vhx_php_config: PHP open_basedir set to %s (appending mode)", obasedir_path);
		} else {
			//zend_alter_ini_entry("open_basedir", 13, path, strlen(path), 4, 16);
			zend_alter_ini_entry("open_basedir", 13, path, strlen(path), ZEND_INI_SYSTEM, ZEND_INI_STAGE_RUNTIME);
			VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "vhx_php_config: PHP open_basedir set to %s", path);
		}
	} else {
		VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "vhx_php_config: PHP open_basedir inactive defaulting to php.ini values");
	}

	/*
	 * vhx_PHPdisplay_errors support
	 */
	if (vhr->display_errors) {
		VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "vhx_php_config: PHP display_errors engaged");
		//zend_alter_ini_entry("display_errors", 10, "1", 1, 4, 16);
		zend_alter_ini_entry("display_errors", 10, "1", 1, ZEND_INI_SYSTEM, ZEND_INI_STAGE_RUNTIME);
	} else {
		VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "vhx_php_config: PHP display_errors inactive defaulting to php.ini values");
	}

}
#endif				/* HAVE_MOD_PHP_SUPPORT */

/*
 *  Get the stuff from LDAP
 */
static int getldaphome(request_rec *r, vhx_config_rec *vhr, const char *hostname, vhx_request_t *reqc)
{
	/* LDAP associated variable and stuff */
	const char 		**vals = NULL;
	char 			*filtbuf = NULL;
	int 			result = 0;
	const char 		*dn = NULL;
	util_ldap_connection_t 	*ldc = NULL;
	int 			failures = 0;

	VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "getldaphome(): BEGIN ***,pid=%d",getpid());


	/* Check for illegal characters in hostname that would mess up the LDAP query */
	ap_regex_t *cpat = ap_pregcomp(r->pool, "\\[\\/\\*\\(\\)&\\]", REG_EXTENDED|REG_ICASE); 
	if(cpat != NULL) {
		if (ap_regexec(cpat, hostname, 0, NULL, 0) == 0 ) {
			VH_AP_LOG_RERROR(APLOG_MARK, APLOG_WARNING, 0, r, "%s: (getldaphome) bad characters in hostname %s", VH_NAME, hostname);
			return DECLINED;
		}
	}

start_over:

	if (vhr->ldap_host) {
		VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "getldaphome(): util_ldap_connection_find(r,%s,%d,%s,%s,%d,%d);",vhr->ldap_host, vhr->ldap_port, vhr->ldap_binddn, vhr->ldap_bindpw, vhr->ldap_deref, vhr->ldap_secure);
		ldc = util_ldap_connection_find(r, vhr->ldap_host, vhr->ldap_port, vhr->ldap_binddn,
				vhr->ldap_bindpw, vhr->ldap_deref, vhr->ldap_secure);
	} else {
		return DECLINED;
	}

	/*
	if (vhr->ldap_set_filter) {
		apr_snprintf(filtbuf, FILTER_LENGTH, "%s=%s", vhr->ldap_filter, hostname);
	} else {
		apr_snprintf(filtbuf, FILTER_LENGTH, "(&(%s)(|(apacheServerName=%s)(apacheServerAlias=%s)))", vhr->ldap_filter, hostname, hostname);
	}*/

	filtbuf = apr_psprintf(r->pool, "(&(%s)(|(apacheServerName=%s)(apacheServerAlias=%s)))", vhr->ldap_filter, hostname, hostname);

	VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "getldaphome(): filtbuf = %s",filtbuf);
	result = util_ldap_cache_getuserdn(r, ldc, vhr->ldap_url, vhr->ldap_basedn, vhr->ldap_scope, ldap_attributes, filtbuf, &dn, &vals);
	util_ldap_connection_close(ldc);

	// sanity check - if server is down, retry it up to 5 times 
	if (result == LDAP_SERVER_DOWN) {
		if (failures++ <= 5) {
			apr_sleep(1000000);
			goto start_over;
		}
	}
	if ((result == LDAP_NO_SUCH_OBJECT)) {
		VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r,
				"getldaphome(): virtual host %s not found",
				hostname);
		return DECLINED;
	}

	/* handle bind failure */
	if (result != LDAP_SUCCESS) {
		VH_AP_LOG_RERROR(APLOG_MARK, APLOG_ERR, 0, r,
				"%s: (getldaphome) translate failed; virtual host %s; URI %s LDAP Error: [%s]", 
				VH_NAME, hostname, r->uri, ldap_err2string(result));
		return DECLINED;
	}

	int i = 0;
	while (ldap_attributes[i]) {
		if (apr_strnatcasecmp (ldap_attributes[i], "apacheServerName") == 0) {
			reqc->name = apr_pstrdup (r->pool, vals[i]);
		}
		else if (apr_strnatcasecmp (ldap_attributes[i], "apacheServerAdmin") == 0) {
			reqc->admin = apr_pstrdup (r->pool, vals[i]);
		}
		else if (apr_strnatcasecmp (ldap_attributes[i], "apacheDocumentRoot") == 0) {
			reqc->docroot = apr_pstrdup (r->pool, vals[i]);
		}
		else if (apr_strnatcasecmp (ldap_attributes[i], "homeDirectory") == 0) {
			reqc->homedirectory = apr_pstrdup (r->pool, vals[i]);
		}
		else if (apr_strnatcasecmp (ldap_attributes[i], "phpOptions") == 0) {
			reqc->phpoptions = apr_pstrdup (r->pool, vals[i]);
		}
		else if (apr_strnatcasecmp (ldap_attributes[i], "uidNumber") == 0) {
			reqc->uid = apr_pstrdup(r->pool, vals[i]);
		}
		else if (apr_strnatcasecmp (ldap_attributes[i], "gidNumber") == 0) {
			reqc->gid = apr_pstrdup(r->pool, vals[i]);
		}
		else if (apr_strnatcasecmp (ldap_attributes[i], "apacheChrootDir") == 0) {
			reqc->chroot_dir = apr_pstrdup(r->pool, vals[i]);
		}
		i++;
	}

	reqc->vhost_found = VH_VHOST_INFOS_FOUND;

	VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "getldaphome(): END ***");

	/* If we don't have a document root then we can't honour the request */
	if (reqc->docroot == NULL) {
		VH_AP_LOG_RERROR(APLOG_MARK, APLOG_ERR|APLOG_NOERRNO, 0, r, "%s: (getldaphome) no document root found for %s", VH_NAME, hostname);
		return DECLINED;
	}

	/* We have a document root so, this is ok */
	return OK;
}

/*
 * Send the right path to the end user upon a request.
 */
static int vhx_translate_name(request_rec * r)
{
	vhx_config_rec     	*vhr  = (vhx_config_rec *)     ap_get_module_config(r->server->module_config, &vhostx_module);
	core_server_config 	*conf = (core_server_config *) ap_get_module_config(r->server->module_config, &core_module);

	vhx_request_t *reqc;
	int vhost_found_by_request = DECLINED;

	VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "vhx_translate_name: BEGIN ***,pid=%d",getpid());
	VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG|APLOG_NOERRNO, 0, r, "vhx_translate_name: request URI '%s'", r->unparsed_uri);

	/* If VHS is not enabled, then don't process request */
	if (!vhr->enable) {
		VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "vhx_translate_name: VHS Disabled ");
		return DECLINED;
	}

	if(r->hostname == NULL) {
		VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "vhx_translate_name: no 'Host:' header found");
		return vhx_redirect_stuff(r, vhr);
	}

	vhx_conn_cfg_t *conn_cfg =
		(vhx_conn_cfg_t*) ap_get_module_config(r->connection->conn_config,
				&vhostx_module);
	if (conn_cfg != NULL) {
		if(conn_cfg->last_vhost != NULL) {
			if ((apr_strnatcasecmp(r->hostname, conn_cfg->last_vhost) == 0) && (conn_cfg->last_vhost_status == VH_VHOST_INFOS_NOT_FOUND)) {
				VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "vhx_translate_name: VH_VHOST_INFOS_NOT_FOUND already set");
				return DECLINED;
			}
		}
	}

	reqc = ap_get_module_config(r->request_config, &vhostx_module);	
	if (!reqc) {
		reqc = (vhx_request_t *)apr_pcalloc(r->pool, sizeof(vhx_request_t));
		reqc->vhost_found = VH_VHOST_INFOS_NOT_YET_REQUESTED;
		VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "vhx_translate_name: set VH_VHOST_INFOS_NOT_YET_REQUESTED");
		ap_set_module_config(r->request_config, &vhostx_module, reqc);
	}
	/* If we don't have LDAP Url module is disabled */
	if (!vhr->ldap_have_url) {
		VH_AP_LOG_RERROR(APLOG_MARK, APLOG_ERR, 0, r, "vhx_translate_name: VHS Disabled - No LDAP URL ");
		return DECLINED;
	}
	VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "vhx_translate_name: VHS Enabled.");

	/* Avoid handling request that don't start with '/' */
	if (r->uri[0] != '/' && r->uri[0] != '\0') {
		VH_AP_LOG_RERROR(APLOG_MARK, APLOG_WARNING, 0, r, "%s: (vhx_translate_name) declined URI '%s' no leading `/'", VH_NAME, r->uri);
		return DECLINED;
	}
	//if ((ptr = ap_strchr(host, ':'))) {
	//	*ptr = '\0';
	//}

	if (reqc->vhost_found == VH_VHOST_INFOS_NOT_YET_REQUESTED) {
		VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "vhx_translate_name: looking for %s", r->hostname);
		/*
		 * Trying to get vhost information
		 */
		vhost_found_by_request = getldaphome(r, vhr, (char *) r->hostname, reqc);

		if (vhost_found_by_request != OK) { 
			/*
			 * The vhost has not been found
			 * Trying to get lamer mode or not
			 */
			if (vhr->lamer_mode) {
				if ((strncasecmp(r->hostname, "www.",4) == 0) && (strlen(r->hostname) > 4)) {
					VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "vhx_translate_name: Lamer friendly mode engaged");
					char           *lhost;
					lhost = apr_pstrdup(r->pool, r->hostname + 4);
					VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "vhx_translate_name: Found a lamer for %s -> %s", r->hostname, lhost);

					vhost_found_by_request = getldaphome(r, vhr, lhost, reqc);
					if (vhost_found_by_request != OK) {
						if (vhr->log_notfound) {
							VH_AP_LOG_RERROR(APLOG_MARK, APLOG_NOTICE, 0, r, "%s: (vhx_translate_name) no host found in database for %s or %s", VH_NAME, r->hostname, lhost);
						}
						return vhx_redirect_stuff(r, vhr);
					}
				}
			} else {
				if (vhr->log_notfound) {
					VH_AP_LOG_RERROR(APLOG_MARK, APLOG_NOTICE, 0, r, "%s: (vhx_translate_name) no host found in database for %s (WWW mode not enabled)", VH_NAME, r->hostname);
				}
				return vhx_redirect_stuff(r, vhr);
			}
		}
	}
	else {
		VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "vhx_translate_name: Request to backend has already been done (vhx_itk_post_read()) !");
		if (reqc->vhost_found == VH_VHOST_INFOS_NOT_FOUND)
			vhost_found_by_request = DECLINED; /* the request has already be done and vhost was not found */
		else
			vhost_found_by_request = OK; /* the request has already be done and vhost was found */
	}

	if (vhost_found_by_request == OK)
		VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "vhx_translate_name: path found in database for %s is %s", r->hostname, reqc->docroot);
	else {
		if (vhr->log_notfound) {
			VH_AP_LOG_RERROR(APLOG_MARK, APLOG_NOTICE|APLOG_NOERRNO, 0, r, "%s: (vhx_translate_name) no path found found in database for %s", VH_NAME, r->hostname);
		}
		return vhx_redirect_stuff(r, vhr);
	}

	// Set VH_GECOS env variable - used for logging
	apr_table_set(r->subprocess_env, "VH_GECOS", reqc->name ? reqc->name : "");

	/* Do we have handle vhr_Path_Prefix here ? */
	if (vhr->path_prefix) {
		apr_table_set(r->subprocess_env, "VH_PATH", apr_pstrcat(r->pool, vhr->path_prefix, reqc->docroot, NULL));
		apr_table_set(r->subprocess_env, "SERVER_ROOT", apr_pstrcat(r->pool, vhr->path_prefix, reqc->docroot, NULL));
	} else {
		apr_table_set(r->subprocess_env, "VH_PATH", reqc->docroot);
		apr_table_set(r->subprocess_env, "SERVER_ROOT", reqc->docroot);
	}

	if (reqc->admin) {
		r->server->server_admin = apr_pstrcat(r->pool, reqc->admin, NULL);
	} else {
		r->server->server_admin = apr_pstrcat(r->pool, "webmaster@", r->hostname, NULL);
	}
	r->server->server_hostname = apr_pstrcat(r->connection->pool, r->hostname, NULL);
	r->parsed_uri.path = apr_pstrcat(r->pool, vhr->path_prefix ? vhr->path_prefix : "", reqc->docroot, r->parsed_uri.path, NULL);
	r->parsed_uri.hostname = r->server->server_hostname;
	r->parsed_uri.hostinfo = r->server->server_hostname;

	/* document_root */
	if (vhr->path_prefix) {
		conf->ap_document_root = apr_pstrcat(r->pool, vhr->path_prefix, reqc->docroot, NULL);
	} else {
		conf->ap_document_root = apr_pstrcat(r->pool, reqc->docroot, NULL);
	}

	/* if directory exist */
	if (!ap_is_directory(r->pool, reqc->docroot)) {
		VH_AP_LOG_RERROR(APLOG_MARK, APLOG_ERR, 0, r,
				"%s: (vhx_translate_name) homedir '%s' is not dir at all", VH_NAME, reqc->docroot);
		return DECLINED;
	}
	r->filename = apr_psprintf(r->pool, "%s%s%s", vhr->path_prefix ? vhr->path_prefix : "", reqc->docroot, r->uri);

	/* Avoid getting // in filename */
	ap_no2slash(r->filename);

	VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "vhx_translate_name: translated http[s]://%s%s to file %s", r->hostname, r->uri, r->filename);

#ifdef HAVE_MOD_PHP_SUPPORT
	if(reqc->homedirectory != NULL)
		vhx_php_config(r, vhr, reqc->homedirectory, reqc->phpoptions);
	else
		vhx_php_config(r, vhr, reqc->docroot, reqc->phpoptions);
#endif /* HAVE_MOD_PHP_SUPPORT */

	VH_AP_LOG_RERROR(APLOG_MARK, APLOG_DEBUG, 0, r, "vhx_translate_name: END ***, pid=%d",getpid());
	return OK;
}

/*
 * Stuff for registering the module
 */
static const command_rec vhx_commands[] = {
	AP_INIT_FLAG( "EnableVhx", set_flag, (void *)5, RSRC_CONF, "Enable VHS module"),

	//AP_INIT_TAKE1("vhx_Path_Prefix", set_field, (void *)1, RSRC_CONF, "Set path prefix."),
	AP_INIT_TAKE1("vhx_PathPrefix", set_field, (void *)1, RSRC_CONF, "Set path prefix."),
	//AP_INIT_TAKE1("vhx_Default_Host", set_field, (void *)2, RSRC_CONF, "Set default host if HTTP/1.1 is not used."),
	AP_INIT_TAKE1("vhx_NotFoundRedirect", set_field, (void *)2, RSRC_CONF, "Redirect to this URL if no host found"),
	//AP_INIT_FLAG( "vhx_Lamer", set_flag, (void *)0, RSRC_CONF, "Enable Lamer Friendly mode"),
	AP_INIT_FLAG( "vhx_WWWMode", set_flag, (void *)0, RSRC_CONF, "Enable WWW mode (where host not found, try again with www. prepended)"),
	//AP_INIT_FLAG( "vhx_LogNotFound", set_flag, (void *)7, RSRC_CONF, "Log on error log when host or path is not found."),

#ifdef HAVE_MOD_PHP_SUPPORT
	//AP_INIT_FLAG( "vhx_PHPopen_basedir", set_flag, (void *)2, RSRC_CONF, "Set PHP open_basedir to path"),
	AP_INIT_FLAG( "vhx_PHPOpenBasedir", set_flag, (void *)2, RSRC_CONF, "Enable setting of PHP open_basedir"),
	//AP_INIT_FLAG( "vhx_PHPopt_fromdb", set_flag, (void *)3, RSRC_CONF, "Gets PHP options from db/libhome"),
	AP_INIT_FLAG( "vhx_PHPOptFromDb", set_flag, (void *)3, RSRC_CONF, "Gets PHP options from db/libhome"),
	//AP_INIT_FLAG( "vhx_PHPdisplay_errors", set_flag, (void *)4, RSRC_CONF, "Enable PHP display_errors"),
	AP_INIT_FLAG( "vhx_PHPDisplayErrors", set_flag, (void *)4, RSRC_CONF, "Enable PHP display_errors"),
	//AP_INIT_FLAG( "vhx_append_open_basedir", set_flag, (void *)6, RSRC_CONF, "Append homedir path to PHP open_basedir to vhx_open_basedir_path."),
	AP_INIT_TAKE1("vhx_PHPOpenBasedirCommon", set_field, (void *)3, RSRC_CONF, "Common PHP open_basedir path."),
#endif /* HAVE_MOD_PHP_SUPPORT */

#ifdef HAVE_MPM_ITK_SUPPORT
	AP_INIT_FLAG("vhx_ITKEnable", set_flag, (void *)8, RSRC_CONF, "Enable MPM-ITK support"),
	AP_INIT_FLAG("vhx_ChrootEnable", set_flag, (void *)9, RSRC_CONF, "Enable chroot support"),
#endif /* HAVE_MPM_ITK_SUPPORT */

	AP_INIT_TAKE1( "vhx_LDAPBindDN",set_field, (void *)4, RSRC_CONF,
			"DN to use to bind to LDAP server. If not provided, will do an anonymous bind."),
	AP_INIT_TAKE1( "vhx_LDAPBindPassword",set_field, (void *)5, RSRC_CONF,
			"Password to use to bind LDAP server. If not provider, will do an anonymous bind."),
	AP_INIT_TAKE1( "vhx_LDAPDereferenceAliases",set_field, (void *)6, RSRC_CONF,
			"Determines how aliases are handled during a search. Can be one of the"
			"values \"never\", \"searching\", \"finding\", or \"always\"."
			"Defaults to always."),

	//AP_INIT_FLAG(  "vhx_LDAPUseFilter", set_flag, (void *)9, RSRC_CONF, "Use filter given in LDAPUrl"),

	AP_INIT_TAKE1( "vhx_LDAPUrl",vhx_ldap_parse_url, NULL, RSRC_CONF,
			"URL to define LDAP connection in form ldap://host[:port]/basedn[?attrib[?scope[?filter]]]."),

	{NULL}
};

static void register_hooks(apr_pool_t * p) {
	/*
	// Modules that have to be loaded before vhx 
	static const char *const aszPre[] =
	{"mod_userdir.c", "mod_php5.c", NULL};
	// Modules that have to be loaded after vhx 
	static const char *const aszSucc[] =
	{"mod_php.c", NULL};
	*/

#ifdef HAVE_MPM_ITK_SUPPORT
	static const char * const aszSuc_itk[]= {"itk.c",NULL };
	ap_hook_post_read_request(vhx_itk_post_read, NULL, aszSuc_itk, APR_HOOK_REALLY_FIRST);
#endif /* HAVE_MPM_ITK_SUPPORT */

	ap_hook_post_config(vhx_init_handler, NULL, NULL, APR_HOOK_MIDDLE);
	//ap_hook_translate_name(vhx_translate_name, aszPre, aszSucc, APR_HOOK_FIRST);
	ap_hook_translate_name(vhx_translate_name, NULL, NULL, APR_HOOK_FIRST);

	ap_hook_optional_fn_retrieve(ImportULDAPOptFn,NULL,NULL,APR_HOOK_MIDDLE);
}

// Trim whitespace from beginning or end of string
static char * trimwhitespace(char *str) {
	char *end;

	// Trim leading space
	while(isspace(*str)) str++;

	if(*str == 0)  // All spaces?
		return str;

	// Trim trailing space
	end = str + strlen(str) - 1;
	while(end > str && isspace(*end)) end--;

	// Write new null terminator
	*(end+1) = 0;

	return str;
}

AP_DECLARE_DATA module vhostx_module = {
	STANDARD20_MODULE_STUFF,
	NULL,
	NULL,
	vhx_create_server_config,		/* create per-server config structure */
	vhx_merge_server_config,		/* merge per-server config structures */
	vhx_commands,				/* command apr_table_t */
	register_hooks				/* register hooks */
};
