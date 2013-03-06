/* 
 * Based on mod_vhs by Xavier Beaudouin <kiwi (at) oav (dot) net>
 *
 * Additional code from:
 * - mod_vhs (fork) by Rene Kanzler <rk (at) cosmomill (dot) de>
 * - mod_myvhost by Igor Popov
 */

/*
 * Version of mod_vhostx
 */
#define VH_NAME		"mod_vhostx"
#define VH_VERSION	"1.0"


/* We need this to be able to set the docroot (ap_document_root) */
#define CORE_PRIVATE

#define APR_WANT_STRFUNC
#include "apr_want.h"

#include "apr.h"
#include "apr_strings.h"
#include "apr_lib.h"
#include "apr_uri.h"
#include "apr_thread_mutex.h"
#if APR_MAJOR_VERSION > 0
#include "apr_regexp.h"
#endif

#include "ap_config.h"
#include "httpd.h"
#include "http_config.h"
#include "http_core.h"
#include "http_log.h"
#include "http_main.h"
#include "http_protocol.h"
#include "http_request.h"
#include "util_script.h"
#include "util_ldap.h"
#include "apr_ldap.h"
#include "apr_strings.h"
#include "apr_reslist.h"
#include "mpm_common.h"

#include "ap_config_auto.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <regex.h>

#ifdef HAVE_MOD_PHP_SUPPORT
#  include <zend.h>
#  include <zend_qsort.h>
#  include <zend_API.h>
#  include <zend_ini.h>
#  include <zend_alloc.h>
#  include <zend_operators.h>
#endif

#ifdef HAVE_MPM_ITK_SUPPORT
typedef struct {
	uid_t 	uid;
	gid_t	gid;
	char	*username;
	int	nice_value;
} itk_conf;
#endif /* HAVE_MPM_ITK_SUPPORT  */

/*
 * Configuration structure
 */
typedef struct {
	unsigned short int	enable;			/* Enable the module */
	char           		*path_prefix;		/* Prefix to add to path returned by database/ldap */
	char           		*default_host;		/* Default host to redirect to */

	unsigned short int 	lamer_mode;		/* Lamer friendly mode */
	unsigned short int 	log_notfound;		/* Log request for vhost/path is not found */

#ifdef HAVE_MOD_PHP_SUPPORT
	char           		*openbdir_path;		/* PHP open_basedir default path */
	unsigned short int 	open_basedir;		/* PHP open_basedir */
	unsigned short int 	append_basedir;		/* PHP append current directory to open_basedir */
	unsigned short int 	display_errors;		/* PHP display_error */
	unsigned short int 	phpopt_fromdb;		/* Get PHP options from database/ldap */
#endif /* HAVE_MOD_PHP_SUPPORT */

#ifdef HAVE_MPM_ITK_SUPPORT
        unsigned short int	itk_enable;			/* MPM-ITK support */
        unsigned short int	chroot_enable;			/* chroot support */
	uid_t			itk_defuid;
	gid_t			itk_defgid;
	char			*itk_defusername;
#endif /* HAVE_MPM_ITK_SUPPORT */

	char				*ldap_url;		/* String representation of LDAP URL */
	char				*ldap_host;		/* Name of the ldap server or space separated list */
	int				ldap_port;		/* Port of the LDAP server */
	char				*ldap_basedn;		/* Base DN */
	int				ldap_scope;		/* Scope of search */
	int				ldap_set_filter;	/* Set custom filter */
	char				*ldap_filter;		/* LDAP Filter */
	deref_options			ldap_deref;		/* How to handle alias dereferening */
	char				*ldap_binddn;		/* DN to bind to server (can be NULL) */
	char				*ldap_bindpw;		/* Password to bind to server (can be NULL) */
	int				ldap_have_deref;	/* Set if we have found an Deref option */
	int 				ldap_have_url;		/* Set if we have found an LDAP url */
	int				ldap_secure;		/* True if SSL connections are requested */

} vhx_config_rec;

typedef struct vhx_request_t {
    char *dn;				/* The saved dn from a successful search */
    char *name;				/* ServerName or host accessed uppon request */
    char *admin;			/* ServerAdmin or email for admin */
    char *docroot;			/* DocumentRoot */
    char *homedirectory;		/* home directory */
    char *phpoptions;			/* PHP Options */
    char *uid;				/* Suexec Uid */
    char *gid;				/* Suexec Gid */
    char *chroot_dir;			/* chroot directory */
    int vhost_found;			/* set to 1 if the struct is field with vhost information, 0 if not, -1 if the vhost does not exist  */
} vhx_request_t;

typedef struct mod_vhx_conn_cfg_t {
    char *last_vhost;			/* Last vhost requested */
    int last_vhost_status;			/* set to 1 if the struct is field with vhost information, 0 if not, -1 if the vhost does not exist  */
} vhx_conn_cfg_t;

#ifdef VH_DEBUG
#  define VH_AP_LOG_ERROR ap_log_error
#else
#  define VH_AP_LOG_ERROR my_ap_log_error
static void my_ap_log_error(void *none, ...)
{
  return;
}
#endif

#ifdef VH_DEBUG
#  define VH_AP_LOG_RERROR ap_log_rerror
#else
#  define VH_AP_LOG_RERROR my_ap_log_rerror
static void my_ap_log_rerror(void *none, ...)
{
  return;
}
#endif

#define VH_VHOST_INFOS_FOUND 1
#define VH_VHOST_INFOS_NOT_FOUND -1
#define VH_VHOST_INFOS_NOT_YET_REQUESTED 0
