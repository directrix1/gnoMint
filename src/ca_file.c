//  gnoMint: a graphical interface for managing a certification authority
//  Copyright (C) 2006,2007 David Marín Carreño <davefx@gmail.com>
//
//  This file is part of gnoMint.
//
//  gnoMint is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 3 of the License, or   
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of 
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the  
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

#include <glib.h>
#include <glib/gstdio.h>
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

#include "tls.h"
#include "ca_file.h"
#include "pkey_manage.h"

#include <libintl.h>
#define _(x) gettext(x)
#define N_(x) (x) gettext_noop(x)

extern gchar * gnomint_current_opened_file;
extern gchar * gnomint_temp_created_file;

sqlite3 * ca_db = NULL;


#define CURRENT_GNOMINT_DB_VERSION 5


int __ca_file_get_single_row_cb (void *pArg, int argc, char **argv, char **columnNames)
{
	gchar ***result = (gchar ***) pArg;
	int i;

	if (! result)
		return 1;

	(*result) = g_new0 (gchar *, argc+1);
	for (i = 0; i<argc; i++) {
		(*result)[i] = g_strdup (argv[i]);
	}

	return 0;
}

gchar ** ca_file_get_single_row (const gchar *query, ...)
{
	gchar **result = NULL;
	gchar *sql = NULL;
	gchar * error;
	va_list list;	

	va_start (list, query);
	sql = sqlite3_vmprintf (query, list);
	va_end (list);

	g_assert (ca_db);

	sqlite3_exec (ca_db, sql, __ca_file_get_single_row_cb, &result, &error);
	
	sqlite3_free (sql);

        if (error)
                fprintf (stderr, "%s\n", error);

	return result;
}


gchar * ca_file_create (CaCreationData *creation_data, 
                        gchar *pem_ca_private_key,
                        gchar *pem_ca_certificate)
{
	gchar *sql = NULL;
	gchar *error = NULL;

	gchar *filename = NULL;

	gchar **row;
	gint64 rootca_rowid;
	guint64 rootca_id;

	TlsCert *tls_cert = tls_parse_cert_pem (pem_ca_certificate);

	filename = strdup (tmpnam(NULL));

	if (sqlite3_open (filename, &ca_db))
		return g_strdup_printf(_("Error opening filename '%s'"), filename) ;

	if (gnomint_temp_created_file)
		ca_file_delete_tmp_file();
	gnomint_temp_created_file = filename;

	if (sqlite3_exec (ca_db, "BEGIN TRANSACTION;", NULL, NULL, &error))
		return error;

	if (sqlite3_exec (ca_db,
                          "CREATE TABLE ca_properties (id INTEGER PRIMARY KEY, name TEXT UNIQUE, value TEXT);",
                          NULL, NULL, &error)) {
		return error;
	}
	if (sqlite3_exec (ca_db,
                          "CREATE TABLE certificates (id INTEGER PRIMARY KEY, is_ca BOOLEAN, serial INT, subject TEXT, activation TIMESTAMP, expiration TIMESTAMP, revocation TIMESTAMP, pem TEXT, private_key_in_db BOOLEAN, private_key TEXT, dn TEXT, parent_dn TEXT);",
                          NULL, NULL, &error)) {
		return error;
	}
	if (sqlite3_exec (ca_db,
                          "CREATE TABLE cert_requests (id INTEGER PRIMARY KEY, subject TEXT, pem TEXT, private_key_in_db BOOLEAN, private_key TEXT, dn TEXT UNIQUE);",
                          NULL, NULL, &error)) {
		return error;
	}

	if (sqlite3_exec (ca_db,
                          "CREATE TABLE ca_policies (id INTEGER PRIMARY KEY, ca_id INTEGER, name TEXT, value TEXT, UNIQUE (ca_id, name));",
                          NULL, NULL, &error)) {
		return error;
	}

	if (sqlite3_exec (ca_db,
                          "CREATE TABLE ca_crl (id INTEGER PRIMARY KEY, ca_id INTEGER, crl_version INTEGER, "
                          "date TIMESTAMP, UNIQUE (ca_id, crl_version));",
                          NULL, NULL, &error)) {
                fprintf (stderr, "%s\n", error);
		return error;
	}

	
	sql = sqlite3_mprintf ("INSERT INTO ca_properties VALUES (NULL, 'ca_db_version', %d);", CURRENT_GNOMINT_DB_VERSION);
	if (sqlite3_exec (ca_db, sql, NULL, NULL, &error))
		return error;
	sqlite3_free (sql);

	sql = sqlite3_mprintf ("INSERT INTO ca_properties VALUES (NULL, 'ca_root_certificate_pem', '%q');", pem_ca_certificate);
	if (sqlite3_exec (ca_db, sql, NULL, NULL, &error))
		return error;
	sqlite3_free (sql);

	sql = sqlite3_mprintf ("INSERT INTO certificates VALUES (NULL, 1, 1, '%q', '%ld', '%ld', NULL, '%q', 1, '%q','%q','%q');", 
			       creation_data->cn,
			       creation_data->activation,
			       creation_data->expiration,
			       pem_ca_certificate,
			       pem_ca_private_key,
			       tls_cert->dn,
			       tls_cert->i_dn );

	if (sqlite3_exec (ca_db, sql, NULL, NULL, &error))
		return error;
	sqlite3_free (sql);

	rootca_rowid = sqlite3_last_insert_rowid (ca_db);
	row = ca_file_get_single_row ("SELECT id FROM certificates WHERE ROWID=%llu ;",
				      rootca_rowid);
	rootca_id = atoll (row[0]);
	g_strfreev (row);

	sql = sqlite3_mprintf ("INSERT INTO ca_properties VALUES (NULL, 'ca_root_last_assigned_serial', 1);");
	if (sqlite3_exec (ca_db, sql, NULL, NULL, &error))
		return error;
	sqlite3_free (sql);

	if (creation_data->is_pwd_protected) {
		gchar *hashed_pwd = pkey_manage_encrypt_password (creation_data->password);

		sql = sqlite3_mprintf ("INSERT INTO ca_properties VALUES (NULL, 'ca_db_is_password_protected', 1);");

		if (sqlite3_exec (ca_db, sql, NULL, NULL, &error))
			return error;
		sqlite3_free (sql);

		sql = sqlite3_mprintf ("INSERT INTO ca_properties VALUES (NULL, 'ca_db_hashed_password', '%q');",
				       hashed_pwd);

		if (sqlite3_exec (ca_db, sql, NULL, NULL, &error))
			return error;
		sqlite3_free (sql);

		g_free (hashed_pwd);				       
	} else  {

		sql = sqlite3_mprintf ("INSERT INTO ca_properties VALUES (NULL, 'ca_db_is_password_protected', '0');");

		if (sqlite3_exec (ca_db, sql, NULL, NULL, &error))
			return error;
		sqlite3_free (sql);

		sql = sqlite3_mprintf ("INSERT INTO ca_properties VALUES (NULL, 'ca_db_hashed_password', '');");

		if (sqlite3_exec (ca_db, sql, NULL, NULL, &error))
			return error;
		sqlite3_free (sql);
	}

	ca_file_policy_set (rootca_id, "MONTHS_TO_EXPIRE", 60);
	ca_file_policy_set (rootca_id, "HOURS_BETWEEN_CRL_UPDATES", 24);
	ca_file_policy_set (rootca_id, "DIGITAL_SIGNATURE", 1);
	ca_file_policy_set (rootca_id, "KEY_ENCIPHERMENT", 1);
	ca_file_policy_set (rootca_id, "KEY_AGREEMENT", 1);
	ca_file_policy_set (rootca_id, "DATA_ENCIPHERMENT", 1);
	ca_file_policy_set (rootca_id, "TLS_WEB_SERVER", 1);
	ca_file_policy_set (rootca_id, "TLS_WEB_CLIENT", 1);
	ca_file_policy_set (rootca_id, "EMAIL_PROTECTION", 1);

	if (sqlite3_exec (ca_db, "COMMIT;", NULL, NULL, &error))
		return error;

	sqlite3_close (ca_db);
	ca_db = NULL;

	tls_cert_free (tls_cert);
	tls_cert = NULL;
	
	return NULL;

}

gboolean ca_file_check_and_update_version ()
{
	gchar ** result = NULL;
	gint db_version_in_file = 0;
	gchar * sql = NULL;
	gchar * error;

	result = ca_file_get_single_row ("SELECT value FROM ca_properties WHERE name = 'ca_db_version';");

	if (result && result[0] && atoi(result[0]) == CURRENT_GNOMINT_DB_VERSION) {
		g_strfreev (result);
		return TRUE;
	}

	if (!result || !result[0]) {
		db_version_in_file = 1;
		if (result)
			g_strfreev (result);
	} else {
		db_version_in_file = atoi(result[0]);
		g_strfreev (result);
	}

	switch (db_version_in_file) {
		/* Careful! This switch has not breaks, as all actions must be done for the earliest versions */
	case 1:

		if (sqlite3_exec (ca_db, "BEGIN TRANSACTION;", NULL, NULL, &error)) {
			return FALSE;
		}

		if (sqlite3_exec (ca_db,
                                  "CREATE TABLE ca_policies (id INTEGER PRIMARY KEY, ca_id INTEGER, name TEXT, value TEXT, UNIQUE (ca_id, name));",
                                  NULL, NULL, &error)) {
			return FALSE;
		}
		
		sql = sqlite3_mprintf ("INSERT INTO ca_properties VALUES (NULL, 'ca_db_version', %d);", 2);
		if (sqlite3_exec (ca_db, sql, NULL, NULL, &error)){
			return FALSE;
		}
		sqlite3_free (sql);
		
		if (sqlite3_exec (ca_db, "COMMIT;", NULL, NULL, &error)) {
			return FALSE;
		}

	case 2:
		if (sqlite3_exec (ca_db, "BEGIN TRANSACTION;", NULL, NULL, &error)){
			return FALSE;
		}

		if (sqlite3_exec (ca_db,
				  "ALTER TABLE certificates ADD dn TEXT; ALTER TABLE certificates ADD parent_dn TEXT;",
				  NULL, NULL, &error)){
			return FALSE;
		}

		{
			gchar **cert_table;
			gint rows, cols;
			gint i;
			if (sqlite3_get_table (ca_db, 
					       "SELECT id, pem FROM certificates;",
					       &cert_table,
					       &rows,
					       &cols,
					       &error)) {
				return FALSE;
			}
			for (i = 0; i < rows; i++) {
				TlsCert * tls_cert = tls_parse_cert_pem (cert_table[(i*2)+3]);			       				
				sql = sqlite3_mprintf ("UPDATE certificates SET dn='%q', parent_dn='%q' WHERE id=%s;",
						       tls_cert->dn, tls_cert->i_dn, cert_table[(i*2)+2]);

				if (sqlite3_exec (ca_db, sql, NULL, NULL, &error)){
					return FALSE;
				}

				tls_cert_free (tls_cert);
				tls_cert = NULL;
			}

			sqlite3_free_table (cert_table);

		}

		if (sqlite3_exec (ca_db,
				  "CREATE TABLE cert_requests_new (id INTEGER PRIMARY KEY, subject TEXT, pem TEXT, private_key_in_db BOOLEAN, private_key TEXT, dn TEXT UNIQUE);",
				  NULL, NULL, &error)){
			return FALSE;
		}
		
		if (sqlite3_exec (ca_db,
				  "INSERT OR REPLACE INTO cert_requests_new SELECT *, NULL FROM cert_requests;",
				  NULL, NULL, &error)){
			return FALSE;
		}
		
		if (sqlite3_exec (ca_db,
				  "DROP TABLE cert_requests;",
				  NULL, NULL, &error)){
			return FALSE;
		}

		if (sqlite3_exec (ca_db,
				  "ALTER TABLE cert_requests_new RENAME TO cert_requests;",
				  NULL, NULL, &error)){
			return FALSE;
		}

		{
			gchar **csr_table;
			gint rows, cols;
			gint i;
			if (sqlite3_get_table (ca_db, 
					       "SELECT id, pem FROM cert_requests;",
					       &csr_table,
					       &rows,
					       &cols,
					       &error)) {
				return FALSE;
			}
			for (i = 0; i < rows; i++) {
				TlsCsr * tls_csr = tls_parse_csr_pem (csr_table[(i*2)+3]);			       				
				sql = sqlite3_mprintf ("UPDATE cert_requests SET dn='%q' WHERE id=%s;",
						       tls_csr->dn, csr_table[(i*2)+2]);

				if (sqlite3_exec (ca_db, sql, NULL, NULL, &error)){
					return FALSE;
				}

				tls_csr_free (tls_csr);
				tls_csr = NULL;
			}

			sqlite3_free_table (csr_table);

		}
		
		sql = sqlite3_mprintf ("UPDATE ca_properties SET value=%d WHERE name='ca_db_version';", 3);
		if (sqlite3_exec (ca_db, sql, NULL, NULL, &error)){
			return FALSE;
		}
		sqlite3_free (sql);
		
		if (sqlite3_exec (ca_db, "COMMIT;", NULL, NULL, &error)){
			return FALSE;
		}


	case 3:
                
		if (sqlite3_exec (ca_db, "BEGIN TRANSACTION;", NULL, NULL, &error)){
			return FALSE;
		}
                
                if (sqlite3_exec (ca_db,
                                  "CREATE TABLE certificates_new (id INTEGER PRIMARY KEY, is_ca BOOLEAN, serial INT, subject TEXT, activation TIMESTAMP, expiration TIMESTAMP, revocation TIMESTAMP, pem TEXT, private_key_in_db BOOLEAN, private_key TEXT, dn TEXT, parent_dn TEXT);",
                                  NULL, NULL, &error)) {
                        return FALSE;
                }

		if (sqlite3_exec (ca_db,
				  "INSERT OR REPLACE INTO certificates_new SELECT id, is_ca, serial, subject, activation, expiration, NULL, pem, private_key_in_db, private_key, dn, parent_dn FROM certificates;",
				  NULL, NULL, &error)){
			return FALSE;
		}
		
		if (sqlite3_exec (ca_db,
				  "DROP TABLE certificates;",
				  NULL, NULL, &error)){
			return FALSE;
		}

		if (sqlite3_exec (ca_db,
				  "ALTER TABLE certificates_new RENAME TO certificates;",
				  NULL, NULL, &error)){
			return FALSE;
		}

		sql = sqlite3_mprintf ("UPDATE ca_properties SET value=%d WHERE name='ca_db_version';", 4);
		if (sqlite3_exec (ca_db, sql, NULL, NULL, &error)){
			return FALSE;
		}
		sqlite3_free (sql);
		


                if (sqlite3_exec (ca_db,
                                  "CREATE TABLE ca_crl (id INTEGER PRIMARY KEY, ca_id INTEGER, crl_version INTEGER, "
                                  "date TIMESTAMP, UNIQUE (ca_id, crl_version));",
                                  NULL, NULL, &error)) {
                        return FALSE;
                }

		if (sqlite3_exec (ca_db, "COMMIT;", NULL, NULL, &error)){
			return FALSE;
		}

	case 4:

		if (sqlite3_exec (ca_db, "BEGIN TRANSACTION;", NULL, NULL, &error)) {
			return FALSE;
		}

		sql = sqlite3_mprintf ("INSERT INTO ca_properties VALUES (NULL, 'ca_db_is_password_protected', 0);");
		if (sqlite3_exec (ca_db, sql, NULL, NULL, &error))
			return FALSE;
		sqlite3_free (sql);

		sql = sqlite3_mprintf ("INSERT INTO ca_properties VALUES (NULL, 'ca_db_hashed_password', '');");
		if (sqlite3_exec (ca_db, sql, NULL, NULL, &error))
			return FALSE;
		sqlite3_free (sql);		

		sql = sqlite3_mprintf ("UPDATE ca_properties SET value=%d WHERE name='ca_db_version';", 5);
		if (sqlite3_exec (ca_db, sql, NULL, NULL, &error)){
			return FALSE;
		}
		sqlite3_free (sql);

		if (sqlite3_exec (ca_db, "COMMIT;", NULL, NULL, &error))
			return FALSE;

        case 5:
		/* Nothing must be done, as this is the current gnoMint db version */
		break;
	}
	
	return TRUE;
}

gboolean ca_file_open (gchar *file_name)
{
	if (! g_file_test(file_name, G_FILE_TEST_EXISTS))
		return FALSE;

	if (sqlite3_open(file_name, &ca_db)) {
		g_printerr ("%s\n\n", sqlite3_errmsg(ca_db));
		return FALSE;
	} else {
		gnomint_current_opened_file = file_name;
		return ca_file_check_and_update_version ();
	}
}

void ca_file_close ()
{
	sqlite3_close (ca_db);
	ca_db = NULL;
	if (gnomint_current_opened_file) {
		g_free (gnomint_current_opened_file);
		gnomint_current_opened_file = NULL;
	}
	       
}

gboolean ca_file_save_as (gchar *new_file_name)
{
	gchar * initial_file = g_strdup(gnomint_current_opened_file);
	GMappedFile *map = NULL;

	ca_file_close ();

	if (! (map = g_mapped_file_new (initial_file, FALSE, NULL)))
		return FALSE;

	if (! g_file_set_contents (new_file_name, 
				   g_mapped_file_get_contents (map),
				   g_mapped_file_get_length (map),
				   NULL)) {
		g_mapped_file_free (map);
		return FALSE;
	}

	g_mapped_file_free (map);

	g_free (initial_file);

	return ca_file_open (new_file_name);

}

gboolean ca_file_rename_tmp_file (gchar *new_file_name)
{
	GMappedFile *map = NULL;

	if (! gnomint_temp_created_file) 
		return FALSE;

	if (! (map = g_mapped_file_new (gnomint_temp_created_file, FALSE, NULL)))
		return FALSE;
	
	if (! g_file_set_contents (new_file_name, 
				   g_mapped_file_get_contents (map),
				   g_mapped_file_get_length (map),
				   NULL)) {
		g_mapped_file_free (map);
		return FALSE;
	}

	g_unlink ((const gchar *) gnomint_temp_created_file);
	g_free (gnomint_temp_created_file);
	gnomint_temp_created_file = NULL;
	gnomint_current_opened_file = new_file_name;

	return TRUE;
}


gboolean ca_file_delete_tmp_file ()
{
	gint result=0;

	if (! gnomint_temp_created_file) 
		return FALSE;
	
	result = g_remove ((const gchar *) gnomint_temp_created_file);

	g_free (gnomint_temp_created_file);
	gnomint_temp_created_file = NULL;

	return (! result);
}

guint64 ca_file_get_last_serial ()
{
	gchar **serialstr = NULL;
	guint64 serial;

	serialstr = ca_file_get_single_row ("SELECT value FROM ca_properties WHERE name='ca_root_last_assigned_serial';");
	serial = atoll (serialstr[0]);
	g_strfreev (serialstr);

	return serial;
}

gchar * ca_file_insert_cert (CertCreationData *creation_data, 
			     gchar *pem_private_key,
			     gchar *pem_certificate)
{
	gchar *sql = NULL;
	gchar *error = NULL;
	gchar **serialstr = NULL;
	guint64 serial;

	TlsCert *tlscert = tls_parse_cert_pem (pem_certificate);

	if (sqlite3_exec (ca_db, "BEGIN TRANSACTION;", NULL, NULL, &error))
		return error;

	serialstr = ca_file_get_single_row ("SELECT value FROM ca_properties WHERE name='ca_root_last_assigned_serial';");
	serial = atoll (serialstr[0]) + 1;
	g_strfreev (serialstr);

	if (pem_private_key)
		sql = sqlite3_mprintf ("INSERT INTO certificates VALUES (NULL, 0, %lld, '%q', '%ld', '%ld', NULL, '%q', 1, '%q', '%q', '%q');", 
				       serial,
				       tlscert->cn,
				       creation_data->activation,
				       creation_data->expiration,
				       pem_certificate,
				       pem_private_key,
				       tlscert->dn,
				       tlscert->i_dn);
	else
		sql = sqlite3_mprintf ("INSERT INTO certificates VALUES (NULL, 0, %lld, '%q', '%ld', '%ld', NULL, '%q', 0, NULL, '%q', '%q');", 
				       serial,
				       tlscert->cn,
				       creation_data->activation,
				       creation_data->expiration,
				       pem_certificate,
				       tlscert->dn,
				       tlscert->i_dn);

	tls_cert_free (tlscert);
	tlscert = NULL;


	if (sqlite3_exec (ca_db, sql, NULL, NULL, &error)) {
		sqlite3_exec (ca_db, "ROLLBACK;", NULL, NULL, NULL);
		sqlite3_free (sql);
		return error;
	}

	sqlite3_free (sql);
	
	sql = sqlite3_mprintf ("UPDATE ca_properties SET value='%lld' WHERE name='ca_root_last_assigned_serial';", 
			       serial);
	if (sqlite3_exec (ca_db, sql, NULL, NULL, &error)) {
		sqlite3_exec (ca_db, "ROLLBACK;", NULL, NULL, NULL);
		sqlite3_free (sql);
		return error;
	}

	sqlite3_free (sql);
	

	if (sqlite3_exec (ca_db, "COMMIT;", NULL, NULL, &error))
		return error;

	return NULL;

}

gchar * ca_file_insert_csr (CaCreationData *creation_data, 
			    gchar *pem_csr_private_key,
			    gchar *pem_csr)
{
	gchar *sql = NULL;
	gchar *error = NULL;

	TlsCsr * tlscsr = tls_parse_csr_pem (pem_csr);

	if (sqlite3_exec (ca_db, "BEGIN TRANSACTION;", NULL, NULL, &error))
		return error;

	if (pem_csr_private_key)
		sql = sqlite3_mprintf ("INSERT INTO cert_requests VALUES (NULL, '%q', '%q', 1, '%q','%q');", 
				       creation_data->cn,
				       pem_csr,
				       pem_csr_private_key,
				       tlscsr->dn);
	else
		sql = sqlite3_mprintf ("INSERT INTO cert_requests VALUES (NULL, '%q', '%q', 0, NULL, '%q');", 
				       creation_data->cn,
				       pem_csr,
				       tlscsr->dn);

	tls_csr_free (tlscsr);
	tlscsr = NULL;

	if (sqlite3_exec (ca_db, sql, NULL, NULL, &error)) {
		sqlite3_exec (ca_db, "ROLLBACK;", NULL, NULL, NULL);
		
		return error;
	}
	sqlite3_free (sql);
	
	if (sqlite3_exec (ca_db, "COMMIT;", NULL, NULL, &error))
		return error;

	return NULL;

}


gchar * ca_file_remove_csr (gint id)
{
	gchar *sql = NULL;
	gchar *error = NULL;

	if (sqlite3_exec (ca_db, "BEGIN TRANSACTION;", NULL, NULL, &error))
		return error;

	sql = sqlite3_mprintf ("DELETE FROM cert_requests WHERE id = %d ;", 
			       id);
	if (sqlite3_exec (ca_db, sql, NULL, NULL, &error))
		return error;
	sqlite3_free (sql);
	
	if (sqlite3_exec (ca_db, "COMMIT;", NULL, NULL, &error))
		return error;

	return NULL;

}

gchar * ca_file_revoke_crt (gint id)
{
	gchar *sql = NULL;
	gchar *error = NULL;

	if (sqlite3_exec (ca_db, "BEGIN TRANSACTION;", NULL, NULL, &error))
		return error;

        fprintf (stderr, "%ld\n", time(NULL));

	sql = sqlite3_mprintf ("UPDATE certificates SET revocation=%ld WHERE id = %d ;", 
			       time(NULL),
                               id);
	if (sqlite3_exec (ca_db, sql, NULL, NULL, &error))
		return error;
	sqlite3_free (sql);
	
	if (sqlite3_exec (ca_db, "COMMIT;", NULL, NULL, &error))
		return error;

	return NULL;

}


int __ca_file_get_revoked_certs_add_certificate (void *pArg, int argc, char **argv, char **columnNames)
{
        GList **p_list = (GList **) pArg;
        GList *list = (* p_list);

        // Pem of the revoked certificate
        list = g_list_prepend (list, g_strdup(argv[0]));

        // Revocation time
        list = g_list_prepend (list, g_strdup(argv[1]));

        *p_list =  list;

        return 0;
}

GList * ca_file_get_revoked_certs ()
{
        GList * list = NULL;
        gchar * error_str = NULL;
        
        sqlite3_exec (ca_db,
                      "SELECT pem,revocation FROM certificates "
                      "WHERE revocation IS NOT NULL "
                      "AND expiration > strftime('%s','now') ORDER BY id",
                      __ca_file_get_revoked_certs_add_certificate, &list, &error_str);
        
        if (error_str) {
                fprintf (stderr, "%s\n", error_str);
                return NULL;
        }
        list = g_list_reverse (list);

        return list;

}

gint ca_file_begin_new_crl_transaction (gint ca_id, time_t timestamp)
{
        gchar * sql;
        gchar **last_crl;
        gint next_crl_version;
        gchar *error;

        last_crl = ca_file_get_single_row ("SELECT crl_version FROM ca_crl WHERE ca_id=%u", ca_id);
        if (! last_crl)
                next_crl_version = 1;
        else {
                next_crl_version = atoi (last_crl[0]) + 1;
                g_strfreev (last_crl);
        }
        
	if (sqlite3_exec (ca_db, "BEGIN TRANSACTION;", NULL, NULL, &error))
		return 0;

        sql = sqlite3_mprintf ("INSERT INTO ca_crl VALUES (NULL, %u, %u, %u);",
                               ca_id, next_crl_version, timestamp);

	if (sqlite3_exec (ca_db, sql, NULL, NULL, &error)){
                sqlite3_free (sql);
                fprintf (stderr, "%s\n", error);
		return 0;        
        }

        sqlite3_free (sql);
        
        return next_crl_version;

}

void ca_file_commit_new_crl_transaction ()
{
        gchar *error;

        sqlite3_exec (ca_db, "COMMIT;", NULL, NULL, &error);

}

void ca_file_rollback_new_crl_transaction ()
{
        gchar *error;

	sqlite3_exec (ca_db, "ROLLBACK;", NULL, NULL, &error);
}

gboolean ca_file_is_password_protected()
{
	gchar **result; 
	gboolean res;

	if (! ca_db)
		return FALSE;
	
	result = ca_file_get_single_row ("SELECT value FROM ca_properties WHERE name='ca_db_is_password_protected';");
	res = ((result != NULL) && (strcmp(result[0], "0")));
	
	if (result)
		g_strfreev (result);
	
	return res;
}

gboolean ca_file_check_password (const gchar *password)
{
	gchar **result;
	gboolean res;

	if (! ca_file_is_password_protected())
		return FALSE;

	result = ca_file_get_single_row ("SELECT value FROM ca_properties WHERE name='ca_db_hashed_password';");
	if (! result)
		return FALSE;	

	res = pkey_manage_check_password (password, result[0]);

	g_strfreev (result);

	return res;
}

typedef	struct {
	const gchar *old_password;
	const gchar *new_password;
	const gchar *table;
} CaFilePwdChange;

int  __ca_file_password_unprotect_cb (void *pArg, int argc, char **argv, char **columnNames)
{
	CaFilePwdChange * pwd_change = (CaFilePwdChange *) pArg;
	const gchar *table = pwd_change->table;
	const gchar *pwd = pwd_change->old_password;
	gchar *error;
	gchar *sql;
	gchar *new_pkey;
	
	if (atoi(argv[1]) == 0)
		return 0;

	new_pkey = pkey_manage_uncrypt_w_pwd (argv[2], argv[3], pwd);

        sql = sqlite3_mprintf ("UPDATE %q SET private_key='%q' WHERE id='%q';",
                               table, new_pkey, argv[0]);

	if (sqlite3_exec (ca_db, sql, NULL, NULL, &error)) {
		fprintf (stderr, "Error while executing: %s. %s", sql, error);
		sqlite3_exec (ca_db, "ROLLBACK;", NULL, NULL, &error);	
		sqlite3_free (sql);
		g_free (new_pkey);
		return 1;
	}

	sqlite3_free(sql);

	g_free (new_pkey);

	return 0;
}

gboolean ca_file_password_unprotect(const gchar *old_password)
{
        gchar *error;
	CaFilePwdChange pwd_change;

	if (! ca_file_is_password_protected ())
		return FALSE;

	if (! ca_file_check_password (old_password))
		return FALSE;

	sqlite3_exec (ca_db, "BEGIN TRANSACTION;", NULL, NULL, &error);	
	
	pwd_change.old_password = old_password;

	pwd_change.table = "certificates";
	if (sqlite3_exec (ca_db, "SELECT id, private_key_in_db, private_key, dn FROM certificates",
			  __ca_file_password_unprotect_cb, &pwd_change, &error)) {
		sqlite3_exec (ca_db, "ROLLBACK;", NULL, NULL, &error);	
		return FALSE;
	}

	pwd_change.table = "cert_requests";
	if (sqlite3_exec (ca_db, "SELECT id, private_key_in_db, private_key, dn FROM cert_requests",
			  __ca_file_password_unprotect_cb, &pwd_change, &error)) {
		sqlite3_exec (ca_db, "ROLLBACK;", NULL, NULL, &error);	
		return FALSE;
	}

	if (sqlite3_exec (ca_db, "UPDATE ca_properties SET value='0' WHERE name='ca_db_is_password_protected';", 
			  NULL, NULL, &error)) {
		sqlite3_exec (ca_db, "ROLLBACK;", NULL, NULL, &error);	
		return FALSE;
	}	

	sqlite3_exec (ca_db, "COMMIT;", NULL, NULL, &error);	


	return TRUE;
}

int  __ca_file_password_protect_cb (void *pArg, int argc, char **argv, char **columnNames)
{
	CaFilePwdChange * pwd_change = (CaFilePwdChange *) pArg;
	const gchar *table = pwd_change->table;
	const gchar *pwd = pwd_change->new_password;
	gchar *error;
	gchar *sql;
	gchar *new_pkey;
	
	if (atoi(argv[1]) == 0)
		return 0;

	new_pkey = pkey_manage_crypt_w_pwd (argv[2], argv[3], pwd);

        sql = sqlite3_mprintf ("UPDATE %q SET private_key='%q' WHERE id='%q';",
                               table, new_pkey, argv[0]);

	if (sqlite3_exec (ca_db, sql, NULL, NULL, &error)) {
		fprintf (stderr, "Error while executing: %s. %s", sql, error);
		sqlite3_exec (ca_db, "ROLLBACK;", NULL, NULL, &error);	
		sqlite3_free (sql);
		g_free (new_pkey);
		return 1;
	}

	sqlite3_free(sql);

	g_free (new_pkey);

	return 0;
}

gboolean ca_file_password_protect(const gchar *new_password)
{
        gchar *error;
	gchar *sql;
	gchar *hashed_pwd;
	CaFilePwdChange pwd_change;

	if (ca_file_is_password_protected ())
		return FALSE;

	sqlite3_exec (ca_db, "BEGIN TRANSACTION;", NULL, NULL, &error);	
	
	pwd_change.new_password = new_password;

	if (sqlite3_exec (ca_db, "UPDATE ca_properties SET value='1' WHERE name='ca_db_is_password_protected';", 
			  NULL, NULL, &error)) {
		sqlite3_exec (ca_db, "ROLLBACK;", NULL, NULL, &error);	
		return FALSE;
	}	

	hashed_pwd = pkey_manage_encrypt_password (new_password);
	sql = sqlite3_mprintf ("UPDATE ca_properties SET value='%q' WHERE name='ca_db_hashed_password';",
			       hashed_pwd);
	g_free (hashed_pwd);

	if (sqlite3_exec (ca_db, sql, NULL, NULL, &error)) {
		sqlite3_free (sql);
		sqlite3_exec (ca_db, "ROLLBACK;", NULL, NULL, &error);	
		return FALSE;
	}

	pwd_change.table = "certificates";
	if (sqlite3_exec (ca_db, "SELECT id, private_key_in_db, private_key, dn FROM certificates",
			  __ca_file_password_protect_cb, &pwd_change, &error)){
		sqlite3_exec (ca_db, "ROLLBACK;", NULL, NULL, &error);	
		return FALSE;
	}

	pwd_change.table = "cert_requests";
	if (sqlite3_exec (ca_db, "SELECT id, private_key_in_db, private_key, dn FROM cert_requests",
			  __ca_file_password_protect_cb, &pwd_change, &error)) {
		sqlite3_exec (ca_db, "ROLLBACK;", NULL, NULL, &error);	
		return FALSE;
	}

	sqlite3_free (sql);

	sqlite3_exec (ca_db, "COMMIT;", NULL, NULL, &error);	


	return TRUE;
}

int  __ca_file_password_change_cb (void *pArg, int argc, char **argv, char **columnNames)
{
	CaFilePwdChange * pwd_change = (CaFilePwdChange *) pArg;
	const gchar *table = pwd_change->table;
	const gchar *old_pwd = pwd_change->old_password;
	const gchar *new_pwd = pwd_change->new_password;
	gchar *error;
	gchar *sql;
	gchar *clear_pkey;
	gchar *new_pkey;
	
	if (atoi(argv[1]) == 0)
		return 0;

	clear_pkey = pkey_manage_uncrypt_w_pwd (argv[2], argv[3], old_pwd);

	new_pkey = pkey_manage_crypt_w_pwd (clear_pkey, argv[3], new_pwd);

	g_free (clear_pkey);

        sql = sqlite3_mprintf ("UPDATE %q SET private_key='%q' WHERE id='%q';",
                               table, new_pkey, argv[0]);

	if (sqlite3_exec (ca_db, sql, NULL, NULL, &error)) {
		fprintf (stderr, "Error while executing: %s. %s", sql, error);
		sqlite3_exec (ca_db, "ROLLBACK;", NULL, NULL, &error);	
		sqlite3_free (sql);
		g_free (new_pkey);
		return 1;
	}

	sqlite3_free(sql);

	g_free (new_pkey);

	return 0;
}

gboolean ca_file_password_change(const gchar *old_password, const gchar *new_password)
{
        gchar *error;
	gchar *sql;
	gchar *hashed_pwd;
	CaFilePwdChange pwd_change;

	if (!ca_file_is_password_protected ())
		return FALSE;

	if (! ca_file_check_password (old_password))
		return FALSE;

	sqlite3_exec (ca_db, "BEGIN TRANSACTION;", NULL, NULL, &error);	
	
	pwd_change.new_password = new_password;
	pwd_change.old_password = old_password;

	pwd_change.table = "certificates";
	if (sqlite3_exec (ca_db, "SELECT id, private_key_in_db, private_key, dn FROM certificates",
			  __ca_file_password_change_cb, &pwd_change, &error)) {
		sqlite3_exec (ca_db, "ROLLBACK;", NULL, NULL, &error);	
		return FALSE;
	}

	pwd_change.table = "cert_requests";
	if (sqlite3_exec (ca_db, "SELECT id, private_key_in_db, private_key, dn FROM cert_requests",
			  __ca_file_password_change_cb, &pwd_change, &error)) {
		sqlite3_exec (ca_db, "ROLLBACK;", NULL, NULL, &error);	
		return FALSE;
	}

	hashed_pwd = pkey_manage_encrypt_password (new_password);
	sql = sqlite3_mprintf ("UPDATE ca_properties SET value='%q' WHERE name='ca_db_hashed_password';",
			       hashed_pwd);
	if (sqlite3_exec (ca_db, sql, NULL, NULL, &error)) {
		sqlite3_free (sql);
		g_free (hashed_pwd);
		sqlite3_exec (ca_db, "ROLLBACK;", NULL, NULL, &error);	
		return FALSE;
	}

	sqlite3_free (sql);
	g_free (hashed_pwd);

	sqlite3_exec (ca_db, "COMMIT;", NULL, NULL, &error);	


	return TRUE;
}


gboolean ca_file_foreach_crt (CaFileCallbackFunc func, gboolean view_revoked, gpointer userdata)
{
	gchar *error_str;

	if (view_revoked) {
		sqlite3_exec (ca_db, 
			      "SELECT id, is_ca, serial, subject, activation, expiration, revocation, private_key_in_db, pem FROM certificates ORDER BY id", 
			      func, userdata, &error_str);
	} else {
		sqlite3_exec (ca_db, 
			      "SELECT id, is_ca, serial, subject, activation, expiration, revocation, private_key_in_db, "
			      "pem FROM certificates WHERE revocation IS NULL ORDER BY id", 
			      func, userdata, &error_str);
	}

	return  (! error_str);
}

gboolean ca_file_foreach_csr (CaFileCallbackFunc func, gpointer userdata)
{
	gchar *error_str;

	sqlite3_exec 
		(ca_db, 
		 "SELECT id, subject, private_key_in_db, pem FROM cert_requests ORDER BY id",
		 func, userdata, &error_str);

	return  (! error_str);
	
}

gboolean ca_file_foreach_policy (CaFileCallbackFunc func, guint64 ca_id, gpointer userdata)
{
	gchar *error_str;
	gchar * query = g_strdup_printf ("SELECT ca_id, name, value FROM ca_policies WHERE ca_id=%"
					 G_GUINT64_FORMAT ";", ca_id);

	sqlite3_exec (ca_db, query, func, userdata, &error_str);

	sqlite3_free (query);

	return  (! error_str);
	
}

guint64 ca_file_get_cert_serial_from_id (guint64 db_id)
{
	gchar ** aux;
	guint64 res;

	aux = ca_file_get_single_row ("SELECT dn FROM certificates WHERE id=%" G_GUINT64_FORMAT ";", db_id);

	res = atoll (aux[0]);

	g_strfreev (aux);

	return res;
}

gchar * ca_file_get_field_from_id (CaFileElementType type, guint64 db_id, const gchar *field)
{
	gchar ** aux;
	gchar * res;

	if (type == CA_FILE_ELEMENT_TYPE_CERT) {
		aux = ca_file_get_single_row ("SELECT %s FROM certificates WHERE id=%" G_GUINT64_FORMAT ";", field, db_id);
	} else {
		aux = ca_file_get_single_row ("SELECT %s FROM cert_requests WHERE id=%" G_GUINT64_FORMAT ";", field, db_id);
	}
	
	if (! aux)
		return NULL;

	if (! aux[0]) {
		g_strfreev (aux);
		return NULL;
	}

	res = g_strdup (aux[0]);

	g_strfreev (aux);
	
	return res;

}

gchar * ca_file_get_dn_from_id (CaFileElementType type, guint64 db_id)
{
	return ca_file_get_field_from_id (type, db_id, "dn");
}

gchar * ca_file_get_public_pem_from_id (CaFileElementType type, guint64 db_id)
{
	return ca_file_get_field_from_id (type, db_id, "pem");
}

gboolean ca_file_get_pkey_in_db_from_id (CaFileElementType type, guint64 db_id)
{
	gboolean res;
	gchar *aux;

	aux = ca_file_get_field_from_id (type, db_id, "private_key_in_db");
	res = atoi(aux);
	g_free (aux);
	return res;
}

gchar * ca_file_get_pkey_pem_from_id (CaFileElementType type, guint64 db_id)
{
	return ca_file_get_field_from_id (type, db_id, "private_key");
}

guint ca_file_policy_get (guint64 ca_id, gchar *property_name)
{
	gchar **row = ca_file_get_single_row ("SELECT value FROM ca_policies WHERE name='%s' AND ca_id=%llu ;", 
					      property_name, ca_id);

	guint res;

	if (!row)
		return 0;

	res = atoi(row[0]);

	g_strfreev (row);

	return res;
}


void ca_file_policy_set (guint64 ca_id, gchar *property_name, guint value)
{
	gchar **aux;

	aux = ca_file_get_single_row ("SELECT id, ca_id, name, value FROM ca_policies WHERE name='%s' AND ca_id=%llu ;", 
				      property_name, ca_id);

	if (! aux) {
		aux = ca_file_get_single_row ("INSERT INTO ca_policies(ca_id, name, value) VALUES (%llu, '%s', %d);",
					      ca_id, property_name, value);
		g_strfreev (aux);
	} else {
		g_strfreev (aux);
		aux = ca_file_get_single_row ("UPDATE ca_policies SET value=%d WHERE ca_id=%llu AND name='%s';",
					      value, ca_id, property_name);
		g_strfreev (aux);
	}
		
}

