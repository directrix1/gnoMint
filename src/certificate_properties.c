//  gnoMint: a graphical interface for managing a certification authority
//  Copyright (C) 2006 David Marín Carreño <davefx@gmail.com>
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or   
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

#include "tls.h"


#define _(x) gettext(x)
#define N_(x) (x) gettext_noop(x)




GladeXML * certificate_properties_window_xml = NULL;

void __certificate_properties_populate (const char *certificate_pem);

void certificate_properties_display(const char *certificate_pem, gboolean privkey_in_db)
{
	gchar     * xml_file = NULL;
	GtkWidget * widget = NULL;

	xml_file = g_build_filename (PACKAGE_DATA_DIR, "gnomint", "gnomint.glade", NULL );
	 
	// Workaround for libglade
	volatile GType foo = GTK_TYPE_FILE_CHOOSER_WIDGET, tst;
	tst = foo;
	certificate_properties_window_xml = glade_xml_new (xml_file, "certificate_properties_dialog", NULL);
	
	g_free (xml_file);
	
	glade_xml_signal_autoconnect (certificate_properties_window_xml); 	
	
	__certificate_properties_populate (certificate_pem);
       
	widget = glade_xml_get_widget (certificate_properties_window_xml, "certificate_properties_dialog");
	gtk_widget_show (widget);
}


void __certificate_properties_populate (const char *certificate_pem)
{
	GtkWidget *widget = NULL;
	struct tm tim;
	TlsCert * cert = NULL;
	gchar model_time_str[100];

	cert = tls_parse_cert_pem (certificate_pem);

	widget = glade_xml_get_widget (certificate_properties_window_xml, "certActivationDateLabel");
	gmtime_r (&cert->activation_time, &tim);
	strftime (model_time_str, 100, _("%m/%d/%Y %R GMT"), &tim);	
	gtk_label_set_text (GTK_LABEL(widget), model_time_str);

	widget = glade_xml_get_widget (certificate_properties_window_xml, "certExpirationDateLabel");
	gmtime_r (&cert->expiration_time, &tim);
	strftime (model_time_str, 100, _("%m/%d/%Y %R GMT"), &tim);	
	gtk_label_set_text (GTK_LABEL(widget), model_time_str);

	widget = glade_xml_get_widget (certificate_properties_window_xml, "certSNLabel");	
	snprintf (model_time_str, 100, "%llu", cert->serial_number);
	gtk_label_set_text (GTK_LABEL(widget), model_time_str);

	widget = glade_xml_get_widget (certificate_properties_window_xml, "certSubjectCNLabel");	
	gtk_label_set_text (GTK_LABEL(widget), cert->cn);

	widget = glade_xml_get_widget (certificate_properties_window_xml, "certSubjectOLabel");	
	gtk_label_set_text (GTK_LABEL(widget), cert->o);

	widget = glade_xml_get_widget (certificate_properties_window_xml, "certSubjectOULabel");	
	gtk_label_set_text (GTK_LABEL(widget), cert->ou);

	widget = glade_xml_get_widget (certificate_properties_window_xml, "certIssuerCNLabel");	
	gtk_label_set_text (GTK_LABEL(widget), cert->i_cn);

	widget = glade_xml_get_widget (certificate_properties_window_xml, "certIssuerOLabel");	
	gtk_label_set_text (GTK_LABEL(widget), cert->i_o);

	widget = glade_xml_get_widget (certificate_properties_window_xml, "certIssuerOULabel");	
	gtk_label_set_text (GTK_LABEL(widget), cert->i_ou);

	widget = glade_xml_get_widget (certificate_properties_window_xml, "sha1Label");	
	gtk_label_set_text (GTK_LABEL(widget), cert->sha1);

	widget = glade_xml_get_widget (certificate_properties_window_xml, "md5Label");	
	gtk_label_set_text (GTK_LABEL(widget), cert->md5);


	if (g_list_length (cert->uses)) {
		GValue * valtrue = g_new0 (GValue, 1);
		int i;
		
		g_value_init (valtrue, G_TYPE_BOOLEAN);
		g_value_set_boolean (valtrue, TRUE);

		widget = glade_xml_get_widget (certificate_properties_window_xml, "certPropSeparator");
		gtk_widget_show (widget);
		
		widget = glade_xml_get_widget (certificate_properties_window_xml, "vboxCertCapabilities");
		
		for (i = g_list_length(cert->uses) - 1; i >= 0; i--) {
			GtkLabel *label = NULL;
			label = GTK_LABEL(gtk_label_new ((gchar *) g_list_nth_data (cert->uses, i)));
			gtk_misc_set_alignment (GTK_MISC(label), 0.0, 0.5);
			gtk_box_pack_end (GTK_BOX(widget), GTK_WIDGET(label), 0, 0, 0);
		}
		gtk_widget_show_all (widget);
		
		g_free (valtrue);
	}



	tls_cert_free (cert);
}

void certificate_properties_close_clicked (const char *certificate_pem)
{
	GtkWidget *widget = glade_xml_get_widget (certificate_properties_window_xml, "certificate_properties_dialog");
	gtk_widget_destroy (widget);
}
