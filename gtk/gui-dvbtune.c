#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/dvb/frontend.h>

#include <X11/Xlib.h>

#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "list.h"
#include "grab-ng.h"
#include "devs.h"
#include "parseconfig.h"
#include "tv-config.h"
#include "dvb-tuning.h"
#include "dvb.h"
#include "gui.h"

/* ---------------------------------------------------------------------------- */
/* static info                                                                  */

struct omenu {
    char *label;
    char *value;
    int  automagic;
};

static struct omenu m_inv[] = {
    { noop("off"),  "0", 0 },
    { noop("on"),   "1", 0 },
    { noop("auto"), "2", 1 },
};
static struct omenu m_bw[] = {
    { "8 MHz",       "8",  0 },
    { "7 MHz",       "7",  0 },
    { "6 MHz",       "6",  0 },
    { noop("auto"),  "0",  1 },
};
static struct omenu m_rate[] = {
    { noop("none"),  "0",   0 },
    { "1-2",         "12",  0 },
    { "2-3",         "23",  0 },
    { "3-4",         "34",  0 },
    { "4-5",         "45",  0 },
    { "5-6",         "56",  0 },
    { "6-7",         "67",  0 },
    { "7-8",         "78",  0 },
    { "8-9",         "89",  0 },
    { noop("auto"),  "999", 1 },
};
static struct omenu m_mo[] = {
    { "QPSK",           "0",   0 },
    { "QAM 16",         "16",  0 },
    { "QAM 32",         "32",  0 },
    { "QAM 64",         "64",  0 },
    { "QAM 128",        "128", 0 },
    { "QAM 256",        "256", 0 },
    { "VSB 8",          "8",   0 },
    { "VSB 16",         "1",   0 },
    { noop("auto"),     "999", 1 },
};
static struct omenu m_tr[] = {
    { "2K",         "2", 0 },
    { "8K",         "8", 0 },
    { noop("auto"), "0", 1 },
};
static struct omenu m_po[] = {
    { noop("horizontal"), "H", 0 },
    { noop("vertical"),   "V", 0 },
};
static struct omenu m_gu[] = {
    { "1/32",       "32", 0 },
    { "1/16",       "16", 0 },
    { "1/8",        "8",  0 },
    { "1/4",        "4",  0 },
    { noop("auto"), "0",  1 },
};
static struct omenu m_hi[] = {
    { noop("none"), "0",   0 },
    { "1",          "1",   0 },
    { "2",          "2",   0 },
    { "4",          "4",   0 },
    { noop("auto"), "999", 1 },
};
static struct omenu m_sat[] = {
    { noop("have only one ;)"),  "",   0 },
    { noop("mini A"),            "MA", 0 },
    { noop("mini B"),            "MB", 0 },
    { noop("DiSEqC pos 0"),      "D0", 0 },
    { noop("DiSEqC pos 1"),      "D1", 0 },
    { noop("DiSEqC pos 2"),      "D2", 0 },
    { noop("DiSEqC pos 3"),      "D3", 0 },
};

static GtkWidget *add_omenu(GtkBox *box, char *text,
			    struct omenu *items, int len,
			    int automagic)
{
    GtkWidget *item, *menu, *omenu;
    GtkBox *hbox;
    int i, a = -1;

    menu = gtk_menu_new();
    for (i = 0; i < len; i++) {
	if (items[i].automagic) {
	    if (automagic)
		a = i;
	    else
		continue;
	}
	item = gtk_menu_item_new_with_label(gettext(items[i].label));
	gtk_menu_append(menu,item);
    }
    if (-1 != a)
	gtk_menu_set_active(GTK_MENU(menu),a);

    hbox = gtk_add_hbox_with_label(box, text);
    omenu = gtk_option_menu_new();
    gtk_option_menu_set_menu(GTK_OPTION_MENU(omenu),menu);
    gtk_box_pack_start(hbox, omenu, TRUE, TRUE, 0);

    return omenu;
}

/* ---------------------------------------------------------------------------- */

GtkWidget *satellite_dialog;
GtkWidget *dvbtune_dialog;
char *dvbtune_lnb;
char *dvbtune_sat;

static GtkWidget *lnb_c;
static GtkWidget *lnb_e;
static GtkWidget *sat;

static GtkWidget *frame;
static GtkWidget *frequency;
static GtkWidget *symbol_rate;
static GtkWidget *inversion;
static GtkWidget *bandwidth;
static GtkWidget *rate_high;
static GtkWidget *rate_low;
static GtkWidget *modulation;
static GtkWidget *transmission;
static GtkWidget *polarization;
static GtkWidget *guard;
static GtkWidget *hierarchy;

static void add_frequency(GtkBox *box)
{
    GtkBox *hbox;

    hbox = gtk_add_hbox_with_label(box, _("frequency"));
    frequency = gtk_entry_new();
    gtk_box_pack_start(hbox, frequency, TRUE, TRUE, 0);
}

static void add_symbol_rate(GtkBox *box)
{
    GtkBox *hbox;

    hbox = gtk_add_hbox_with_label(box, _("symbol rate"));
    symbol_rate = gtk_entry_new();
    gtk_box_pack_start(hbox, symbol_rate, TRUE, TRUE, 0);
}

static void add_inversion(GtkBox *box, int caps)
{
    inversion = add_omenu(box, _("inversion"), m_inv, DIMOF(m_inv),
			  caps & FE_CAN_INVERSION_AUTO);
}

static void add_bandwidth(GtkBox *box, int caps)
{
    bandwidth = add_omenu(box, _("bandwidth"), m_bw, DIMOF(m_bw),
			  caps & FE_CAN_BANDWIDTH_AUTO);
}

static void add_code_rate(GtkBox *box, int caps)
{
    rate_high = add_omenu(box, _("code rate high"), m_rate, DIMOF(m_rate),
			  caps & FE_CAN_FEC_AUTO);
    rate_low  = add_omenu(box, _("code rate low"),  m_rate, DIMOF(m_rate),
			  caps & FE_CAN_FEC_AUTO);
}

static void add_modulation(GtkBox *box, int caps)
{
    modulation = add_omenu(box, _("modulation"), m_mo, DIMOF(m_mo),
			   caps & FE_CAN_QAM_AUTO);
}

static void add_transmission(GtkBox *box, int caps)
{
    transmission = add_omenu(box, _("transmission"), m_tr, DIMOF(m_tr),
			     caps & FE_CAN_TRANSMISSION_MODE_AUTO);
}

static void add_polarization(GtkBox *box)
{
    polarization = add_omenu(box, _("polarization"), m_po, DIMOF(m_po), 0);
}

static void add_guard(GtkBox *box, int caps)
{
    guard = add_omenu(box, _("guard interval"), m_gu, DIMOF(m_gu),
		      caps & FE_CAN_GUARD_INTERVAL_AUTO);
}

static void add_hierarchy(GtkBox *box, int caps)
{
    hierarchy = add_omenu(box, _("hierarchy"), m_hi, DIMOF(m_hi),
			  caps & FE_CAN_HIERARCHY_AUTO);
}

static void add_lnb(GtkBox *box)
{
    GtkBox *hbox;
    GList *groups;

    hbox   = gtk_add_hbox_with_label(box, _("LNB (lo,hi,sw)"));
    lnb_c  = gtk_combo_new();
    lnb_e  = GTK_COMBO(lnb_c)->entry;
    gtk_box_pack_start(hbox, lnb_c, TRUE, TRUE, 0);

    groups = NULL;
    groups = g_list_append(groups, "9750,10600,11700");
    groups = g_list_append(groups, "11200");
    groups = g_list_append(groups, "10000");
    groups = g_list_append(groups, "9750");
    groups = g_list_append(groups, "5150");
    gtk_combo_set_popdown_strings(GTK_COMBO(lnb_c), groups);
    g_list_free(groups);
}

static void add_sat(GtkBox *box)
{
    sat = add_omenu(box, _("Satellite"), m_sat, DIMOF(m_sat), 0);
}

static void tune_response(GtkDialog *dialog,
			  gint arg1,
			  gpointer user_data)
{
    char *d = "tmp";
    char *s = "dvbtune";
    char *val;
    int n;

    if (arg1 == GTK_RESPONSE_OK || arg1 == GTK_RESPONSE_APPLY) {
	if (frequency) {
	    val = (char*)gtk_entry_get_text(GTK_ENTRY(frequency));
	    cfg_set_str(d, s, "frequency", val);
	}
	if (symbol_rate) {
	    val = (char*)gtk_entry_get_text(GTK_ENTRY(symbol_rate));
	    cfg_set_str(d, s, "symbol_rate", val);
	}
	if (inversion) {
	    n = gtk_option_menu_get_history(GTK_OPTION_MENU(inversion));
	    cfg_set_str(d, s, "inversion", m_inv[n].value);
	}
	if (bandwidth) {
	    n = gtk_option_menu_get_history(GTK_OPTION_MENU(bandwidth));
	    cfg_set_str(d, s, "bandwidth", m_bw[n].value);
	}
	if (rate_high) {
	    n = gtk_option_menu_get_history(GTK_OPTION_MENU(rate_high));
	    cfg_set_str(d, s, "code_rate_high", m_rate[n].value);
	    n = gtk_option_menu_get_history(GTK_OPTION_MENU(rate_low));
	    cfg_set_str(d, s, "code_rate_low", m_rate[n].value);
	}
	if (modulation) {
	    n = gtk_option_menu_get_history(GTK_OPTION_MENU(modulation));
	    cfg_set_str(d, s, "modulation", m_mo[n].value);
	}
	if (transmission) {
	    n = gtk_option_menu_get_history(GTK_OPTION_MENU(transmission));
	    cfg_set_str(d, s, "transmission", m_tr[n].value);
	}
	if (polarization) {
	    n = gtk_option_menu_get_history(GTK_OPTION_MENU(polarization));
	    cfg_set_str(d, s, "polarization", m_po[n].value);
	}
	if (guard) {
	    n = gtk_option_menu_get_history(GTK_OPTION_MENU(guard));
	    cfg_set_str(d, s, "guard_interval", m_gu[n].value);
	}
	if (hierarchy) {
	    n = gtk_option_menu_get_history(GTK_OPTION_MENU(hierarchy));
	    cfg_set_str(d, s, "hierarchy", m_hi[n].value);
	}
	if (dvbtune_lnb)
	    cfg_set_str(d, s, "lnb", dvbtune_lnb);
	if (dvbtune_sat)
	    cfg_set_str(d, s, "sat", dvbtune_sat);
#if 0
	write_config_file("tmp");
#endif
	dvb_frontend_tune(devs.dvb, d, s);
	cfg_del_section(d, s);
	dvbmon_refresh(devs.dvbmon);
    }

    if (arg1 != GTK_RESPONSE_APPLY)
	gtk_widget_hide(GTK_WIDGET(dialog));
}

void create_dvbtune(GtkWindow *parent)
{
    GtkBox *box,*vbox;
    int32_t caps;

    dvbtune_dialog =
	gtk_dialog_new_with_buttons(_("Tune DVB frontend"), parent, 0,
				    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				    GTK_STOCK_APPLY,  GTK_RESPONSE_APPLY,
				    GTK_STOCK_OK,     GTK_RESPONSE_OK,
				    NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dvbtune_dialog),
				    GTK_RESPONSE_OK);
    g_signal_connect(dvbtune_dialog, "delete-event",
		     G_CALLBACK(gtk_widget_hide_on_delete), NULL);

    vbox = GTK_BOX(GTK_DIALOG(dvbtune_dialog)->vbox);
    frame = gtk_frame_new(NULL);
    gtk_container_set_border_width(GTK_CONTAINER(frame), SPACING);
    gtk_box_pack_start(vbox, frame, TRUE, TRUE, 0);
    box = GTK_BOX(gtk_vbox_new(TRUE, SPACING));
    gtk_container_set_border_width(GTK_CONTAINER(box), SPACING);
    gtk_container_add(GTK_CONTAINER(frame),GTK_WIDGET(box));

    add_frequency(box);
    caps = dvb_frontend_get_caps(devs.dvb);
    switch (dvb_frontend_get_type(devs.dvb)) {
    case FE_QPSK: /* DVB-S */
	gtk_frame_set_label(GTK_FRAME(frame)," DVB-S ");
	add_symbol_rate(box);
	add_inversion(box, caps);
	add_polarization(box);
	break;
    case FE_QAM:  /* DVB-C */
	gtk_frame_set_label(GTK_FRAME(frame)," DVB-C ");
	add_symbol_rate(box);
	add_inversion(box, caps);
	add_modulation(box, caps);
	break;
    case FE_OFDM: /* DVB-T */
	gtk_frame_set_label(GTK_FRAME(frame)," DVB-T ");
	add_inversion(box, caps);
	add_bandwidth(box, caps);
	add_code_rate(box, caps);
	add_modulation(box, caps);
	add_transmission(box, caps);
	add_guard(box, caps);
	add_hierarchy(box, caps);
	break;
#ifdef FE_ATSC
    case FE_ATSC:  /* ATSC */
	gtk_frame_set_label(GTK_FRAME(frame)," ATSC ");
	add_modulation(box, caps);
	break;
#endif
    }

    g_signal_connect(dvbtune_dialog, "response",
		     G_CALLBACK(tune_response), NULL);
    gtk_widget_show_all(dvbtune_dialog);
}

/* ---------------------------------------------------------------------------- */

static void sat_response(GtkDialog *dialog,
			 gint arg1,
			 gpointer user_data)
{
    char *val;
    int n;

    if (arg1 == GTK_RESPONSE_OK) {
	val = (char*)gtk_entry_get_text(GTK_ENTRY(lnb_e));
	if (strlen(val) > 0) {
	    if (dvbtune_lnb)
		free(dvbtune_lnb);
	    dvbtune_lnb = strdup(val);
	}
	n = gtk_option_menu_get_history(GTK_OPTION_MENU(sat));
	if (strlen(m_sat[n].value) > 0) {
	    dvbtune_sat = m_sat[n].value;
	}
    }
    gtk_widget_hide(GTK_WIDGET(dialog));
}

void create_satellite(GtkWindow *parent)
{
    GtkBox *box,*vbox;

    if (dvb_frontend_get_type(devs.dvb) != FE_QPSK) {
	gtk_error_box(parent,"Error",
		      _("You don't have a DVB-S card, thus there is\n"
			"no need to setup satellite parameters ;)\n"));
	return;
    }
    
    satellite_dialog =
	gtk_dialog_new_with_buttons(_("Satellite Parameters"), parent, 0,
				    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				    GTK_STOCK_OK,     GTK_RESPONSE_OK,
				    NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(satellite_dialog),
				    GTK_RESPONSE_OK);
    g_signal_connect(satellite_dialog, "delete-event",
		     G_CALLBACK(gtk_widget_hide_on_delete), NULL);

    vbox = GTK_BOX(GTK_DIALOG(satellite_dialog)->vbox);
    frame = gtk_frame_new(NULL);
    gtk_container_set_border_width(GTK_CONTAINER(frame), SPACING);
    gtk_box_pack_start(vbox, frame, TRUE, TRUE, 0);
    box = GTK_BOX(gtk_vbox_new(TRUE, SPACING));
    gtk_container_set_border_width(GTK_CONTAINER(box), SPACING);
    gtk_container_add(GTK_CONTAINER(frame),GTK_WIDGET(box));

    add_lnb(box);
    add_sat(box);

    /* fill current values */
    if (dvbtune_lnb)
	gtk_entry_set_text(GTK_ENTRY(lnb_e),dvbtune_lnb);
    if (dvbtune_sat) {
	int i;
	for (i = 0; i < DIMOF(m_sat); i++)
	    if (0 == strcasecmp(dvbtune_sat,m_sat[i].value))
		gtk_option_menu_set_history(GTK_OPTION_MENU(sat),i);
    }

    g_signal_connect(satellite_dialog, "response",
		     G_CALLBACK(sat_response), NULL);
    gtk_widget_show_all(satellite_dialog);
}
