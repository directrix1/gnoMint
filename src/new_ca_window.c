//  gnoMint: a graphical interface for managing a certification authority
//  Copyright (C) 2006-2009 David Marín Carreño <davefx@gmail.com>
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

#include <glade/glade.h>
#include <glib-object.h>
#include <gtk/gtk.h>
#include <libintl.h>
#include <stdlib.h>
#include <string.h>

#include "ca_policy.h"
#include "new_ca_window.h"
#include "creation_process_window.h"
#include "ca_file.h"
#include "tls.h"
#include "pkey_manage.h"
#include "country_table.h"

#include <glib/gi18n.h>

GladeXML * new_ca_window_xml = NULL;


void new_ca_window_display()
{
	gchar     * xml_file = NULL;
	// Workaround for libglade
	volatile GType foo = GTK_TYPE_FILE_CHOOSER_WIDGET, tst;

	xml_file = g_build_filename (PACKAGE_DATA_DIR, "gnomint", "gnomint.glade", NULL );
	 
	tst = foo;
	new_ca_window_xml = glade_xml_new (xml_file, "new_ca_window", NULL);
	
	g_free (xml_file);
	
	glade_xml_signal_autoconnect (new_ca_window_xml); 	
	
	country_table_populate_combobox(GTK_COMBO_BOX(glade_xml_get_widget(new_ca_window_xml, "country_combobox")));

}



// TAB Manage

void new_ca_tab_activate (int tab_number)
{
	GtkNotebook *notebook = GTK_NOTEBOOK(glade_xml_get_widget (new_ca_window_xml, "new_ca_notebook"));
	
	gtk_notebook_set_current_page (notebook, tab_number);

}

void on_cn_entry_changed (GtkEditable *editable,
			 gpointer user_data) 
{
	GtkButton *button = GTK_BUTTON(glade_xml_get_widget (new_ca_window_xml, "new_ca_next1"));

	if (strlen (gtk_entry_get_text (GTK_ENTRY(editable)))) 
		gtk_widget_set_sensitive (GTK_WIDGET(button), TRUE);
	else
		gtk_widget_set_sensitive (GTK_WIDGET(button), FALSE);
		
}

void on_new_ca_next1_clicked (GtkButton *widget,
			      gpointer user_data) 
{
	new_ca_tab_activate (1);
}

void on_new_ca_previous2_clicked (GtkButton *widget,
				  gpointer user_data) 
{
	new_ca_tab_activate (0);
}

void on_new_ca_next2_clicked (GtkButton *widget,
			      gpointer user_data) 
{
	new_ca_tab_activate (2);
}

void on_new_ca_previous3_clicked (GtkButton *widget,
				  gpointer user_data) 
{
	new_ca_tab_activate (1);
}

void on_new_ca_cancel_clicked (GtkButton *widget,
			       gpointer user_data) 
{
	
	GtkWindow *window = GTK_WINDOW(glade_xml_get_widget (new_ca_window_xml, "new_ca_window"));

	gtk_object_destroy(GTK_OBJECT(window));
	
}


void on_new_ca_pwd_entry_changed (GtkEntry *entry,
				       gpointer user_data)
{
	const gchar *text1;
	const gchar *text2;
	
	GtkEntry *pwd_entry_1 = GTK_ENTRY(glade_xml_get_widget (new_ca_window_xml, "new_ca_pwd_entry_1"));
	GtkEntry *pwd_entry_2 = GTK_ENTRY(glade_xml_get_widget (new_ca_window_xml, "new_ca_pwd_entry_2"));
	GtkButton *commit_button = GTK_BUTTON(glade_xml_get_widget (new_ca_window_xml, "new_ca_commit"));

	text1 = gtk_entry_get_text (pwd_entry_1);
	text2 = gtk_entry_get_text (pwd_entry_2);

	if (strlen(text1) && strlen(text2) && ! strcmp(text1, text2)) {
		gtk_widget_set_sensitive (GTK_WIDGET(commit_button), TRUE);		
	} else {
		gtk_widget_set_sensitive (GTK_WIDGET(commit_button), FALSE);		
	}

}


void on_new_ca_pwd_protect_radiobutton_toggled (GtkRadioButton *radiobutton, 
						     gpointer user_data)
{
	GtkRadioButton *yes = GTK_RADIO_BUTTON(glade_xml_get_widget (new_ca_window_xml, 
								     "new_ca_pwd_protect_yes_radiobutton"));
	GtkLabel *pwd_label_1 = GTK_LABEL(glade_xml_get_widget (new_ca_window_xml, "new_ca_pwd_label_1"));
	GtkLabel *pwd_label_2 = GTK_LABEL(glade_xml_get_widget (new_ca_window_xml, "new_ca_pwd_label_2"));
	GtkEntry *pwd_entry_1 = GTK_ENTRY(glade_xml_get_widget (new_ca_window_xml, "new_ca_pwd_entry_1"));
	GtkEntry *pwd_entry_2 = GTK_ENTRY(glade_xml_get_widget (new_ca_window_xml, "new_ca_pwd_entry_2"));
	GtkButton *commit_button = GTK_BUTTON(glade_xml_get_widget (new_ca_window_xml, "new_ca_commit"));

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(yes))) {
		gtk_widget_set_sensitive (GTK_WIDGET(pwd_label_1), TRUE);
		gtk_widget_set_sensitive (GTK_WIDGET(pwd_label_2), TRUE);
		gtk_widget_set_sensitive (GTK_WIDGET(pwd_entry_1), TRUE);
		gtk_widget_set_sensitive (GTK_WIDGET(pwd_entry_2), TRUE);
		on_new_ca_pwd_entry_changed (pwd_entry_1, NULL);
	} else {
		gtk_widget_set_sensitive (GTK_WIDGET(pwd_label_1), FALSE);
		gtk_widget_set_sensitive (GTK_WIDGET(pwd_label_2), FALSE);
		gtk_widget_set_sensitive (GTK_WIDGET(pwd_entry_1), FALSE);
		gtk_widget_set_sensitive (GTK_WIDGET(pwd_entry_2), FALSE);
		gtk_widget_set_sensitive (GTK_WIDGET(commit_button), TRUE);		
	}

}


void on_new_ca_commit_clicked (GtkButton *widg,
			       gpointer user_data) 
{
	TlsCreationData *ca_creation_data = NULL;

	GtkWidget *widget = NULL;
	GtkWindow *window = NULL;
	gint active = -1;
	gchar *text = NULL;
	GtkTreeModel *tree_model = NULL;
	GtkTreeIter tree_iter;
	
	time_t tmp;
	struct tm * expiration_time;

	ca_creation_data = g_new0 (TlsCreationData, 1);
	widget = glade_xml_get_widget (new_ca_window_xml, "country_combobox");
	active = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));

	if (active < 0) {
			ca_creation_data->country = NULL;
	} else {
		tree_model = gtk_combo_box_get_model (GTK_COMBO_BOX(widget));
		gtk_combo_box_get_active_iter (GTK_COMBO_BOX(widget), &tree_iter);
		gtk_tree_model_get (tree_model, &tree_iter, 1, &text, -1);

		ca_creation_data->country = g_strdup (text);
		g_free (text);
	}
		
	widget = glade_xml_get_widget (new_ca_window_xml, "st_entry");
	text = (gchar *) gtk_entry_get_text (GTK_ENTRY(widget));
	if (strlen (text))
		ca_creation_data->state = g_strdup (text);
	else
		ca_creation_data->state = NULL;

	widget = glade_xml_get_widget (new_ca_window_xml, "city_entry");
	text = (gchar *) gtk_entry_get_text (GTK_ENTRY(widget));
	if (strlen (text))
		ca_creation_data->city = g_strdup (text);
	else
		ca_creation_data->city = NULL;

	widget = glade_xml_get_widget (new_ca_window_xml, "o_entry");
	text = (gchar *) gtk_entry_get_text (GTK_ENTRY(widget));
	if (strlen (text))
		ca_creation_data->org = g_strdup (text);
	else
		ca_creation_data->org = NULL;

	widget = glade_xml_get_widget (new_ca_window_xml, "ou_entry");
	text = (gchar *) gtk_entry_get_text (GTK_ENTRY(widget));
	if (strlen (text))
		ca_creation_data->ou = g_strdup (text);
	else
		ca_creation_data->ou = NULL;

	widget = glade_xml_get_widget (new_ca_window_xml, "cn_entry");
	text = (gchar *) gtk_entry_get_text (GTK_ENTRY(widget));
	if (strlen (text))
		ca_creation_data->cn = g_strdup (text);
	else
		ca_creation_data->cn = NULL;

	widget = glade_xml_get_widget (new_ca_window_xml, "dsa_radiobutton");
	active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(widget));
	ca_creation_data->key_type = active;

	widget = glade_xml_get_widget (new_ca_window_xml, "keylength_spinbutton");
	active = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON(widget));
	ca_creation_data->key_bitlength = active;

	widget = glade_xml_get_widget (new_ca_window_xml, "months_before_expiration_spinbutton");
	active = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON(widget));
	ca_creation_data->key_months_before_expiration = active;

	tmp = time (NULL);	
	ca_creation_data->activation = tmp;
	
	expiration_time = g_new (struct tm,1);
	localtime_r (&tmp, expiration_time);      
	expiration_time->tm_mon = expiration_time->tm_mon + ca_creation_data->key_months_before_expiration;
	expiration_time->tm_year = expiration_time->tm_year + (expiration_time->tm_mon / 12);
	expiration_time->tm_mon = expiration_time->tm_mon % 12;	
	ca_creation_data->expiration = mktime(expiration_time);
	g_free (expiration_time);


	/* widget = glade_xml_get_widget (new_ca_window_xml, "new_ca_pwd_protect_yes_radiobutton"); */
	/* active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(widget)); */
	/* ca_creation_data->is_pwd_protected = active; */

	/* if (active) { */
	/* 	widget = glade_xml_get_widget (new_ca_window_xml, "new_ca_pwd_entry_1"); */
	/* 	text = (gchar *) gtk_entry_get_text (GTK_ENTRY(widget)); */
	/* 	if (strlen (text)) */
	/* 		ca_creation_data->password = g_strdup (text); */
	/* 	else */
	/* 		ca_creation_data->password = NULL; */
	/* } */


	if (ca_file_is_password_protected()) {
		ca_creation_data->password = pkey_manage_ask_password();

                if (! ca_creation_data->password) {
                        /* The user hasn't provided a valid password */
                        return;
                }

        }

	window = GTK_WINDOW(glade_xml_get_widget (new_ca_window_xml, "new_ca_window"));
	gtk_object_destroy(GTK_OBJECT(window));

	creation_process_window_ca_display (ca_creation_data);
	




}




