/*
 * some wrapper stuff to simplify translating Motif apps via gettext
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <Xm/Xm.h>
#include <Xm/Label.h>
#include <Xm/RowColumn.h>
#include <Xm/SelectioB.h>
#include <Xm/MessageB.h>
#include <Xm/PushB.h>
#include <Xm/ToggleB.h>
#include <Xm/CascadeB.h>

#include "RegEdit.h"
#include "motif-gettext.h"

/*----------------------------------------------------------------------*/

static void split_kbd(char *label, XmString *str, KeySym *sym)
{
    char h[2],*l,*a;

    //fprintf(stderr,"%s\n",label);
    if (NULL == (a = strchr(label,'&'))) {
	*sym = 0;
	*str = XmStringGenerate(label, NULL, XmMULTIBYTE_TEXT, NULL);
	return;
    }

    l = malloc(strlen(label));
    sprintf(l,"%.*s%s",(int)(a-label),label,a+1);
    *str = XmStringGenerate(l, NULL, XmMULTIBYTE_TEXT, NULL);
    h[0] = a[1];
    h[1] = 0;
    *sym = XStringToKeysym(h);
    free(l);
    return;
}

static Widget xm_button(WidgetClass class, Widget parent, char *name, char *label)
{
    Widget w;
    XmString xmlabel;
    KeySym sym;
    Arg args[4];
    int nargs = 0;

    split_kbd(label,&xmlabel,&sym);
    XtSetArg(args[nargs], XmNlabelString, xmlabel), nargs++;
    if (sym)
	XtSetArg(args[nargs], XmNmnemonic, sym), nargs++;
    w = XtCreateManagedWidget(name, class, parent, args, nargs);
    XmStringFree(xmlabel);
    return w;
}

/*----------------------------------------------------------------------*/

Widget xm_label(Widget parent, char *name, char *label)
{
    Widget w;
    XmString xmlabel;

    xmlabel = XmStringGenerate(label, NULL, XmMULTIBYTE_TEXT, NULL);
    w = XtVaCreateManagedWidget(name, xmLabelWidgetClass, parent,
				XmNlabelString, xmlabel,
				NULL);
    XmStringFree(xmlabel);
    return w;
}

Widget xm_pushbutton(Widget parent, char *name, char *label)
{
    return xm_button(xmPushButtonWidgetClass, parent, name, label);
}

Widget xm_togglebutton(Widget parent, char *name, char *label)
{
    return xm_button(xmToggleButtonWidgetClass, parent, name, label);
}

Widget xm_submenu(Widget parent, Widget *button, char *name, char *label)
{
    Widget push,menu;
    XmString xmlabel;
    KeySym sym;
    Arg args[4];
    int nargs = 0;
    
    split_kbd(label,&xmlabel,&sym);
    menu = XmCreatePulldownMenu(parent,name,NULL,0);
    XtSetArg(args[nargs], XmNsubMenuId, menu), nargs++;
    XtSetArg(args[nargs], XmNlabelString, xmlabel), nargs++;
    if (sym)
	XtSetArg(args[nargs], XmNmnemonic, sym), nargs++;
    push = XtCreateManagedWidget(name,xmCascadeButtonWidgetClass,parent,
				 args,nargs);
    if (0 == strcmp(name,"help"))
	XtVaSetValues(parent,XmNmenuHelpWidget,push,NULL);
    if (button)
	*button = push;
    return menu;
}

Widget xm_optionmenu(Widget parent, Widget menu, char *name, char *label)
{
    Widget w;
    XmString xmlabel;
    Arg args[4];
    int nargs = 0;

    xmlabel = XmStringGenerate(label, NULL, XmMULTIBYTE_TEXT, NULL);
    XtSetArg(args[nargs], XmNsubMenuId, menu), nargs++;
    XtSetArg(args[nargs], XmNlabelString, xmlabel), nargs++;
    w = XmCreateOptionMenu(parent, name, args, nargs);
    XtManageChild(w);
    XmStringFree(xmlabel);
    return w;
}

/*----------------------------------------------------------------------*/

static void handle_button(Widget button, char *label)
{
    XmString xmstr;

    if (label) {
	xmstr = XmStringGenerate(label, NULL, XmMULTIBYTE_TEXT, NULL);
	XtVaSetValues(button,XmNlabelString,xmstr,NULL);
	XmStringFree(xmstr);
	XtManageChild(button);
    } else {
	XtUnmanageChild(button);
    }
}

void xm_init_dialog(Widget dlg, char *ok, char *apply, char *cancel, char *help)
{
    Widget widget;

    XmdRegisterEditres(XtParent(dlg));

    widget = XmSelectionBoxGetChild(dlg,XmDIALOG_OK_BUTTON);
    if (NULL != widget)
	handle_button(widget,ok);
    widget = XmSelectionBoxGetChild(dlg,XmDIALOG_APPLY_BUTTON);
    if (NULL != widget)
	handle_button(widget,apply);
    widget = XmSelectionBoxGetChild(dlg,XmDIALOG_CANCEL_BUTTON);
    if (NULL != widget)
	handle_button(widget,cancel);
    widget = XmSelectionBoxGetChild(dlg,XmDIALOG_HELP_BUTTON);
    if (NULL != widget)
	handle_button(widget,help);
}

void xm_init_msgbox(Widget dlg, char *title, char *msg)
{
    XmString xmmsg;
    Widget button;

    button = XmMessageBoxGetChild(dlg,XmDIALOG_HELP_BUTTON);
    XtUnmanageChild(button);
    button = XmMessageBoxGetChild(dlg,XmDIALOG_CANCEL_BUTTON);
    XtUnmanageChild(button);

    xmmsg = XmStringGenerate(msg, NULL, XmMULTIBYTE_TEXT, NULL);
    XtVaSetValues(XtParent(dlg),XtNtitle,title,NULL);
    XtVaSetValues(dlg,XmNmessageString,xmmsg,NULL);
    XmStringFree(xmmsg);
}
