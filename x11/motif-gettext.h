#include <libintl.h>

#define _(string) gettext(string)

/* -------------------------------------------------------------------------- */

extern Widget xm_label(Widget parent, char *name, char *label);
extern Widget xm_pushbutton(Widget parent, char *name, char *label);
extern Widget xm_togglebutton(Widget parent, char *name, char *label);
extern Widget xm_submenu(Widget parent, Widget *button, char *name, char *label);
extern Widget xm_optionmenu(Widget parent, Widget menu, char *name, char *label);

extern void xm_init_dialog(Widget dlg, char *ok, char *apply,
			   char *cancel, char *help);
extern void xm_init_msgbox(Widget dlg, char *title, char *msg);
