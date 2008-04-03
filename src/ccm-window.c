/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * cairo-compmgr
 * Copyright (C) Nicolas Bruguier 2007 <gandalfn@club-internet.fr>
 * 
 * cairo-compmgr is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * cairo-compmgr is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with cairo-compmgr.  If not, write to:
 * 	The Free Software Foundation, Inc.,
 * 	51 Franklin Street, Fifth Floor
 * 	Boston, MA  02110-1301, USA.
 */

#include <math.h>
#include <string.h>
#include <cairo.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xregion.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>

#include <gdk/gdkx.h>
#include <gdk/gdk.h>

#include "ccm-window.h"
#include "ccm-window-backend.h"
#include "ccm-window-plugin.h"
#include "ccm-display.h"
#include "ccm-screen.h"
#include "ccm-pixmap.h"
#include "ccm-pixmap-backend.h"
#include "async-getprop.h"

#define MWM_HINTS_DECORATIONS (1L << 1)

typedef struct {
    unsigned long flags;
    unsigned long functions;
    unsigned long decorations;
} MotifWmHints;


static void 		ccm_window_iface_init		(CCMWindowPluginClass* iface);
static CCMRegion* 	ccm_window_query_geometry	(CCMDrawable* drawable);
static void 		ccm_window_move				(CCMDrawable* drawable, 
												 int x, int y);
static void 		ccm_window_resize			(CCMDrawable* drawable, 
												 int width, int height);

static CCMRegion* 	impl_ccm_window_query_geometry(CCMWindowPlugin* plugin,
												   CCMWindow* self);
static gboolean 	impl_ccm_window_paint		  (CCMWindowPlugin* plugin, 
												   CCMWindow* self, 
												   cairo_t* context,        
												   cairo_surface_t* surface,
												   gboolean y_invert);
static void 		impl_ccm_window_map			  (CCMWindowPlugin* plugin, 
												   CCMWindow* self);
static void 		impl_ccm_window_unmap		  (CCMWindowPlugin* plugin, 
												   CCMWindow* self);
static void 		impl_ccm_window_query_opacity (CCMWindowPlugin* plugin, 
												   CCMWindow* self);
static void			impl_ccm_window_set_opaque	  (CCMWindowPlugin* plugin, 
												   CCMWindow* self);
static void			impl_ccm_window_move		  (CCMWindowPlugin* plugin, 
												   CCMWindow* self, 
												   int x, int y);
static void			impl_ccm_window_resize		  (CCMWindowPlugin* plugin, 
												   CCMWindow* self, 
												   int width, int height);
static void			ccm_window_get_property_async (CCMWindow* self, 
												   Atom property_atom, 
												   Atom req_type, long length);
static void			ccm_window_on_get_property_async(CCMWindow* self, 
													 AgGetPropertyTask* task,
													 CCMDisplay* display);

enum
{
	PROPERTY_CHANGED,
	STATE_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

G_DEFINE_TYPE_EXTENDED (CCMWindow, ccm_window, CCM_TYPE_DRAWABLE, 0,
						G_IMPLEMENT_INTERFACE(CCM_TYPE_WINDOW_PLUGIN,
											  ccm_window_iface_init))

struct _CCMWindowPrivate
{
	CCMWindowType		hint_type;
	gchar*				name;
	gchar*				class_name;
	
	Window				child;
	Window				transient_for;
	Window				group_leader;
	
	gboolean			is_shaped;
	gboolean			is_shaded;
	gboolean			is_fullscreen;
	gboolean			is_decorated;
	gboolean			is_modal;
	gboolean			skip_taskbar;
	gboolean			skip_pager;
	gboolean			keep_above;
	gboolean			keep_below;
	gboolean			has_format;
	cairo_format_t		format;
	gboolean			override_redirect;
	XWindowAttributes   attribs;
	
	gboolean			unmap_pending;

	CCMPixmap*			pixmap;
	
	CCMWindowPlugin*	plugin;
	double				opacity;
};

#define CCM_WINDOW_GET_PRIVATE(o)  \
   ((CCMWindowPrivate*)G_TYPE_INSTANCE_GET_PRIVATE ((o), CCM_TYPE_WINDOW, CCMWindowClass))

static void
ccm_window_init (CCMWindow *self)
{
	self->is_input_only = FALSE;
	self->is_viewable = FALSE;
	self->opaque = NULL;
	self->priv = CCM_WINDOW_GET_PRIVATE(self);
	self->priv->hint_type = CCM_WINDOW_TYPE_NORMAL;
	self->priv->name = NULL;
	self->priv->class_name = NULL;
	self->priv->child = None;
	self->priv->transient_for = None;
	self->priv->group_leader = None;
	self->priv->is_shaped = FALSE;
	self->priv->is_shaded = FALSE;
	self->priv->is_fullscreen = FALSE;
	self->priv->is_decorated = TRUE;
	self->priv->is_modal = FALSE;
	self->priv->skip_taskbar = FALSE;
	self->priv->skip_pager = FALSE;
	self->priv->keep_above = FALSE;
	self->priv->keep_below = FALSE;
	self->priv->has_format = FALSE;
	self->priv->format = CAIRO_FORMAT_ARGB32;
	self->priv->override_redirect = FALSE;
	self->priv->unmap_pending = FALSE;
	self->priv->opacity = 1.0f;
	self->priv->pixmap = NULL;
	self->priv->plugin = NULL;
}

static void
ccm_window_finalize (GObject *object)
{
	CCMWindow* self = CCM_WINDOW(object);
	CCMDisplay* display = ccm_drawable_get_display (CCM_DRAWABLE(self));
	
	if (self->opaque) ccm_region_destroy(self->opaque);
	if (self->priv->pixmap) g_object_unref(self->priv->pixmap);
	if (self->priv->name) g_free(self->priv->name);
	if (self->priv->plugin) g_object_unref(self->priv->plugin);
	g_signal_handlers_disconnect_by_func(display, ccm_window_on_get_property_async, self);
	G_OBJECT_CLASS (ccm_window_parent_class)->finalize (object);
}

static void
ccm_window_class_init (CCMWindowClass *klass)
{
	GObjectClass* object_class = G_OBJECT_CLASS (klass);
	
	g_type_class_add_private (klass, sizeof (CCMWindowPrivate));
	
	klass->opacity_atom = None;
	
	CCM_DRAWABLE_CLASS(klass)->query_geometry = ccm_window_query_geometry;
	CCM_DRAWABLE_CLASS(klass)->move = ccm_window_move;
	CCM_DRAWABLE_CLASS(klass)->resize = ccm_window_resize;
	
	signals[PROPERTY_CHANGED] = g_signal_new ("property-changed",
									 G_OBJECT_CLASS_TYPE (object_class),
									 G_SIGNAL_RUN_LAST, 0, NULL, NULL,
									 g_cclosure_marshal_VOID__VOID,
									 G_TYPE_NONE, 0);
	
	signals[STATE_CHANGED] = g_signal_new ("state-changed",
									 G_OBJECT_CLASS_TYPE (object_class),
									 G_SIGNAL_RUN_LAST, 0, NULL, NULL,
									 g_cclosure_marshal_VOID__VOID,
									 G_TYPE_NONE, 0);
	
	object_class->finalize = ccm_window_finalize;
}

static void
ccm_window_iface_init(CCMWindowPluginClass* iface)
{
	iface->load_options 	= NULL;
	iface->query_geometry 	= impl_ccm_window_query_geometry;
	iface->paint 			= impl_ccm_window_paint;
	iface->map  			= impl_ccm_window_map;
	iface->unmap  			= impl_ccm_window_unmap;
	iface->query_opacity  	= impl_ccm_window_query_opacity;
	iface->set_opaque	  	= impl_ccm_window_set_opaque;
	iface->move				= impl_ccm_window_move;
	iface->resize			= impl_ccm_window_resize;
}

static void
create_atoms(CCMWindow* self)
{
	g_return_if_fail(self != NULL);
	
	CCMWindowClass* klass = CCM_WINDOW_GET_CLASS(self);
	if (!klass->opacity_atom)
	{
		CCMDisplay* display = ccm_drawable_get_display(CCM_DRAWABLE(self));
		
		klass->none_atom  = XInternAtom (CCM_DISPLAY_XDISPLAY(display),
										 "NONE", False);
		
		klass->utf8_string_atom  = XInternAtom (CCM_DISPLAY_XDISPLAY(display),
										        "UTF8_STRING", 
											    False);
		klass->name_atom         = XInternAtom (CCM_DISPLAY_XDISPLAY(display),
										        "_NET_WM_NAME", 
											    False);
		klass->visible_name_atom = XInternAtom (CCM_DISPLAY_XDISPLAY(display),
										        "_NET_WM_VISIBLE_NAME", 
											    False);
		klass->opacity_atom      = XInternAtom (CCM_DISPLAY_XDISPLAY(display),
										        "_NET_WM_WINDOW_OPACITY", 
											    False);
		klass->type_atom         = XInternAtom (CCM_DISPLAY_XDISPLAY(display),
									            "_NET_WM_WINDOW_TYPE", 
												False);
		klass->type_normal_atom  = XInternAtom (CCM_DISPLAY_XDISPLAY(display),
											    "_NET_WM_WINDOW_TYPE_NORMAL", 
												False);
		klass->type_desktop_atom = XInternAtom (CCM_DISPLAY_XDISPLAY(display),
							 				    "_NET_WM_WINDOW_TYPE_DESKTOP", 
											    False);
    	klass->type_dock_atom    = XInternAtom (CCM_DISPLAY_XDISPLAY(display),
											    "_NET_WM_WINDOW_TYPE_DOCK", 
											    False);
    	klass->type_toolbar_atom = XInternAtom (CCM_DISPLAY_XDISPLAY(display),
											    "_NET_WM_WINDOW_TYPE_TOOLBAR",
												False);
    	klass->type_menu_atom    = XInternAtom (CCM_DISPLAY_XDISPLAY(display),
											    "_NET_WM_WINDOW_TYPE_MENU", 
												False);
        klass->type_util_atom    = XInternAtom (CCM_DISPLAY_XDISPLAY(display),
											    "_NET_WM_WINDOW_TYPE_UTILITY",
												False);
    	klass->type_splash_atom  = XInternAtom (CCM_DISPLAY_XDISPLAY(display),
											    "_NET_WM_WINDOW_TYPE_SPLASH", 
												False);
    	klass->type_dialog_atom  = XInternAtom (CCM_DISPLAY_XDISPLAY(display),
											    "_NET_WM_WINDOW_TYPE_DIALOG", 
												False);
        klass->type_dropdown_menu_atom = XInternAtom (
											CCM_DISPLAY_XDISPLAY(display),
											"_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", 
											False);
		klass->type_popup_menu_atom    = XInternAtom (
											CCM_DISPLAY_XDISPLAY(display),
											"_NET_WM_WINDOW_TYPE_POPUP_MENU", 
											False);
    	klass->type_tooltip_atom       = XInternAtom (
											CCM_DISPLAY_XDISPLAY(display),
											"_NET_WM_WINDOW_TYPE_TOOLTIP", 
											False);
    	klass->type_notification_atom  = XInternAtom (
											CCM_DISPLAY_XDISPLAY(display),
											"_NET_WM_WINDOW_TYPE_NOTIFICATION", 
											False);
    	klass->type_combo_atom         = XInternAtom (
											CCM_DISPLAY_XDISPLAY(display),
											"_NET_WM_WINDOW_TYPE_COMBO", 
											False);
    	klass->type_dnd_atom           = XInternAtom (
											CCM_DISPLAY_XDISPLAY(display),
											"_NET_WM_WINDOW_TYPE_DND", 
											False);
    	klass->state_atom              = XInternAtom (
											CCM_DISPLAY_XDISPLAY(display),
											"_NET_WM_STATE", 
											False);
		klass->state_shade_atom        = XInternAtom (
											CCM_DISPLAY_XDISPLAY(display),
											"_NET_WM_STATE_SHADED", 
											False);
		klass->state_fullscreen_atom   = XInternAtom (
											CCM_DISPLAY_XDISPLAY(display),
											"_NET_WM_STATE_FULLSCREEN", 
											False);
		klass->state_above_atom        = XInternAtom (
											CCM_DISPLAY_XDISPLAY(display),
											"_NET_WM_STATE_ABOVE", 
											False);
		klass->state_below_atom        = XInternAtom (
											CCM_DISPLAY_XDISPLAY(display),
											"_NET_WM_STATE_BELOW", 
											False);
		klass->state_skip_taskbar      = XInternAtom (
											CCM_DISPLAY_XDISPLAY(display),
											"_NET_WM_STATE_SKIP_TASKBAR", 
											False);
		klass->state_skip_pager        = XInternAtom (
											CCM_DISPLAY_XDISPLAY(display),
											"_NET_WM_STATE_SKIP_PAGER", 
											False);
		klass->state_is_modal          = XInternAtom (
											CCM_DISPLAY_XDISPLAY(display),
											"_NET_WM_STATE_MODAL", 
											False);
		klass->mwm_hints_atom          = XInternAtom (
											CCM_DISPLAY_XDISPLAY(display),
											"_MOTIF_WM_HINTS", 
											False);
		klass->frame_extends_atom      = XInternAtom (
											CCM_DISPLAY_XDISPLAY(display),
											"_NET_FRAME_EXTENTS", 
											False);
		klass->transient_for_atom      = XInternAtom (
											CCM_DISPLAY_XDISPLAY(display),
											"WM_TRANSIENT_FOR", 
											False);
	}
}

static guint32 *
ccm_window_get_property(CCMWindow* self, Atom property_atom, int req_format, 
						Atom req_type, guint *n_items)
{
	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(property_atom != None, NULL);
	
    CCMDisplay* display = ccm_drawable_get_display(CCM_DRAWABLE(self));
    int ret;
    Atom type;
    int format;
    gulong n_items_internal;
    guchar *property = NULL;
    gulong bytes_after;
    guint32 *result;
    
    ret = XGetWindowProperty (CCM_DISPLAY_XDISPLAY(display), 
							  CCM_WINDOW_XWINDOW(self), property_atom, 
							  0, G_MAXLONG, False,
							  req_type, &type, &format,
							  &n_items_internal, &bytes_after,
							  &property);
    
    if (ret != Success)
    {
		if (property) XFree(property);
		return NULL;
    }
	result = g_memdup (property, n_items_internal * (format / 8));
    XFree(property);
	
    if (n_items) *n_items = n_items_internal;
    
    return result;
}

static void
ccm_window_on_get_property_async(CCMWindow* self, AgGetPropertyTask* task,
								 CCMDisplay* display)
{
	g_return_if_fail(display != NULL);
	g_return_if_fail(task != NULL);
	g_return_if_fail(self != NULL);
	
    Atom type;
    int format;
    gulong n_items_internal;
    gulong bytes_after;
    gchar *result;
    Atom property = ag_task_get_property (task);
	
	if (ag_task_get_window (task) != CCM_WINDOW_XWINDOW(self))
		return;
	
	if (ag_task_get_reply (task, &type, &format, &n_items_internal, 
						   &bytes_after, &result) == Success 
		&& type != None)
	{
		if (property == CCM_WINDOW_GET_CLASS(self)->type_atom)
		{
			CCMWindowType old = self->priv->hint_type;
			Atom atom;
			memcpy (&atom, result, sizeof (Atom));
				
			if (atom == CCM_WINDOW_GET_CLASS(self)->type_normal_atom)
				self->priv->hint_type = CCM_WINDOW_TYPE_NORMAL;
			else if (atom == CCM_WINDOW_GET_CLASS(self)->type_menu_atom)
				self->priv->hint_type = CCM_WINDOW_TYPE_MENU;
			else if (atom == CCM_WINDOW_GET_CLASS(self)->type_desktop_atom)
				self->priv->hint_type = CCM_WINDOW_TYPE_DESKTOP;
			else if (atom == CCM_WINDOW_GET_CLASS(self)->type_dock_atom)
				self->priv->hint_type = CCM_WINDOW_TYPE_DOCK;
			else if (atom == CCM_WINDOW_GET_CLASS(self)->type_toolbar_atom)
				self->priv->hint_type = CCM_WINDOW_TYPE_TOOLBAR;
			else if (atom == CCM_WINDOW_GET_CLASS(self)->type_util_atom)
				self->priv->hint_type = CCM_WINDOW_TYPE_UTILITY;
			else if (atom == CCM_WINDOW_GET_CLASS(self)->type_splash_atom)
				self->priv->hint_type = CCM_WINDOW_TYPE_SPLASH;
			else if (atom == CCM_WINDOW_GET_CLASS(self)->type_dialog_atom)
				self->priv->hint_type = CCM_WINDOW_TYPE_DIALOG;
			else if (atom == CCM_WINDOW_GET_CLASS(self)->type_dropdown_menu_atom)
				self->priv->hint_type = CCM_WINDOW_TYPE_DROPDOWN_MENU;
			else if (atom == CCM_WINDOW_GET_CLASS(self)->type_popup_menu_atom)
				self->priv->hint_type = CCM_WINDOW_TYPE_POPUP_MENU;
			else if (atom == CCM_WINDOW_GET_CLASS(self)->type_tooltip_atom)
				self->priv->hint_type = CCM_WINDOW_TYPE_TOOLTIP;
			else if (atom == CCM_WINDOW_GET_CLASS(self)->type_notification_atom)
				self->priv->hint_type = CCM_WINDOW_TYPE_NOTIFICATION;
			else if (atom == CCM_WINDOW_GET_CLASS(self)->type_combo_atom)
				self->priv->hint_type = CCM_WINDOW_TYPE_COMBO;
			else if (atom == CCM_WINDOW_GET_CLASS(self)->type_dnd_atom)
				self->priv->hint_type = CCM_WINDOW_TYPE_DND;
			
			if (old != self->priv->hint_type)
				g_signal_emit(self, signals[PROPERTY_CHANGED], 0);
			
			ccm_window_get_property_async (self, 
							CCM_WINDOW_GET_CLASS(self)->transient_for_atom,
							XA_WINDOW, G_MAXLONG);
			ccm_window_query_wm_hints (self);
		}
		if (property == CCM_WINDOW_GET_CLASS(self)->transient_for_atom)
		{
			gboolean updated = FALSE;
				
			if (result)
			{
				Window old = self->priv->transient_for;
				
				memcpy (&self->priv->transient_for, result, sizeof (Window));
				updated = old != self->priv->transient_for;
				
				if (self->priv->transient_for != None && 
					self->priv->hint_type == CCM_WINDOW_TYPE_NORMAL)
				{
					self->priv->hint_type = CCM_WINDOW_TYPE_DIALOG;
					updated = TRUE;
				}
			}
			if (updated) g_signal_emit(self, signals[PROPERTY_CHANGED], 0);
		}
		if (property == CCM_WINDOW_GET_CLASS(self)->mwm_hints_atom)
		{
#define MAX_ITEMS sizeof (MotifWmHints)/sizeof (gulong)
			
			if (result)
			{
				MotifWmHints* hints;
				int real_size, max_size;
				gboolean old = self->priv->is_decorated;
				
				hints = ag_Xmalloc (sizeof (MotifWmHints));
				real_size = n_items_internal * sizeof (gulong);
  				max_size = MAX_ITEMS * sizeof (gulong);
  				memcpy (hints, result, MIN (real_size, max_size));
			
				if (hints->flags & MWM_HINTS_DECORATIONS)
					self->priv->is_decorated = hints->decorations != 0;
				if (old != self->priv->is_decorated)
					g_signal_emit(self, signals[PROPERTY_CHANGED], 0);
				XFree(hints);
			}
		}
		if (property == CCM_WINDOW_GET_CLASS(self)->state_atom)
		{
			if (result) 
			{
				Atom *atom = (Atom *) result;
				gulong cpt;
			
				for (cpt = 0; cpt < n_items_internal; cpt++)
				{
					ccm_window_set_state (self, atom[cpt]);
				}
			}
		}
		if (result) XFree(result);
	}		
}
								 
static void
ccm_window_get_property_async(CCMWindow* self, Atom property_atom, 
							  Atom req_type, long length)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(property_atom != None);
	
    CCMDisplay* display = ccm_drawable_get_display(CCM_DRAWABLE(self));
	ag_task_create (CCM_DISPLAY_XDISPLAY(display), CCM_WINDOW_XWINDOW(self),
					property_atom, 0, length, False, req_type);
}

static void
ccm_window_get_child_property_async(CCMWindow* self, Atom property_atom, 
									Atom req_type, long length)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(property_atom != None);
	
	if (self->priv->child == None) return;
	
    CCMDisplay* display = ccm_drawable_get_display(CCM_DRAWABLE(self));
	ag_task_create (CCM_DISPLAY_XDISPLAY(display), self->priv->child,
					property_atom, 0, length, False, req_type);
}

static guint32 *
ccm_window_get_child_property(CCMWindow* self, Atom property_atom, int req_format, 
						       Atom req_type, guint *n_items)
{
	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(property_atom != None, NULL);

    CCMDisplay* display = ccm_drawable_get_display(CCM_DRAWABLE(self));
    int ret;
    Atom type;
    int format;
    gulong n_items_internal;
    guchar *property = NULL;
    gulong bytes_after;
    guint32 *result;
    
	if (!self->priv->child) return NULL;
	
    ret = XGetWindowProperty (CCM_DISPLAY_XDISPLAY(display), 
							  self->priv->child, property_atom, 
							  0, G_MAXLONG, False,
							  req_type, &type, &format,
							  &n_items_internal, &bytes_after,
							  &property);
    
    if (ret != Success)
    {
		if (property) XFree(property);
		return NULL;
    }
        
    result = g_memdup (property, n_items_internal * (format / 8));
    XFree(property);
	
    if (n_items) *n_items = n_items_internal;
    
    return result;
}

static gchar*
ccm_window_get_utf8_property (CCMWindow* self, Atom atom)
{
	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(atom != None, NULL);
  
	gchar *val;
	guint32* data = NULL;
	guint n_items;
	
	data = ccm_window_get_property(self, atom, 8, 
								   CCM_WINDOW_GET_CLASS(self)->utf8_string_atom, 
								   &n_items);
  
	if (!data) return NULL;
	
	if (!g_utf8_validate ((gchar*)data, n_items, NULL))
    {
		g_free (data);
		return NULL;
    }
  
  	val = g_strndup ((gchar*)data, n_items);
  
  	g_free (data);
  
  	return val;
}

static gchar*
ccm_window_get_child_utf8_property (CCMWindow* self, Atom atom)
{
	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(atom != None, NULL);
  
	gchar *val;
	guint32* data = NULL;
	guint n_items;
		
	data = ccm_window_get_child_property(self, atom, 8, 
								   CCM_WINDOW_GET_CLASS(self)->utf8_string_atom, 
								   &n_items);
  
	if (!data) return NULL;
	
	if (!g_utf8_validate ((gchar*)data, n_items, NULL))
    {
		XFree (data);
		return NULL;
    }
  
  	val = g_strndup ((gchar*)data, n_items);
  
  	g_free (data);
  
  	return val;
}

static gchar*
text_property_to_utf8 (const XTextProperty* prop)
{
	gchar **list;
	gint count;
	gchar *retval;
  
  	list = NULL;

  	count = gdk_text_property_to_utf8_list (gdk_x11_xatom_to_atom (prop->encoding),
											prop->format,
											prop->value,
											prop->nitems,
											&list);

  	if (count == 0)
		retval = NULL;
	else
    {
      	retval = list[0];
      	list[0] = g_strdup (""); /* something to free */
    }

  	g_strfreev (list);

  	return retval;
}

static gchar*
ccm_window_get_text_property (CCMWindow* self, Atom atom)
{
	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(atom != None, NULL);
  
	CCMDisplay* display = ccm_drawable_get_display (CCM_DRAWABLE(self));
	XTextProperty text;
	gchar* retval = NULL;
  
  	text.nitems = 0;
  	if (XGetTextProperty(CCM_DISPLAY_XDISPLAY(display), 
						 CCM_WINDOW_XWINDOW(self), &text, atom))
    {
      	retval = text_property_to_utf8 (&text);

      	if (text.value) XFree (text.value);
    }
	
	return retval;
}

static gchar*
ccm_window_get_child_text_property (CCMWindow* self, Atom atom)
{
	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(atom != None, NULL);
  
	CCMDisplay* display = ccm_drawable_get_display (CCM_DRAWABLE(self));
	XTextProperty text;
	gchar* retval = NULL;
  
  	text.nitems = 0;
  	if (XGetTextProperty(CCM_DISPLAY_XDISPLAY(display), 
						 self->priv->child, &text, atom))
    {
      	retval = text_property_to_utf8 (&text);

      	if (text.value) XFree (text.value);
    }
	
	return retval;
}

static void
ccm_window_query_child(CCMWindow* self)
{
	g_return_if_fail(self != NULL);
	
	CCMDisplay* display = ccm_drawable_get_display (CCM_DRAWABLE(self));
	Window* windows = NULL, w, p;
	guint n_windows;
		
	if (XQueryTree(CCM_DISPLAY_XDISPLAY(display), CCM_WINDOW_XWINDOW(self), 
				   &w, &p, &windows, &n_windows) && n_windows > 0)
	{
		self->priv->child = windows[n_windows - 1];
		XSelectInput (CCM_DISPLAY_XDISPLAY(display), 
					  self->priv->child, 
					  PropertyChangeMask | 
					  SubstructureNotifyMask);
	}
	if (windows) XFree(windows);
}

static CCMRegion*
impl_ccm_window_query_geometry(CCMWindowPlugin* plugin, CCMWindow* self)
{
	g_return_val_if_fail(self != NULL, NULL);
	
	CCMRegion* geometry = NULL;
	CCMDisplay* display = ccm_drawable_get_display(CCM_DRAWABLE(self));
	int bx, by, cs, cx, cy, o; /* dummies */
	unsigned int bw, bh, cw, ch; /* dummies */
	cairo_rectangle_t area;
	
	_ccm_display_trap_error (display);
	if (!XGetWindowAttributes (CCM_DISPLAY_XDISPLAY(display), 
							   CCM_WINDOW_XWINDOW(self), &self->priv->attribs))
		return NULL;
	
	if (_ccm_display_pop_error (display))
		return NULL;
	
	if (XShapeQueryExtents (CCM_DISPLAY_XDISPLAY(display), 
							CCM_WINDOW_XWINDOW(self), 
							&self->priv->is_shaped, &bx, &by, &bw, &bh, 
							&cs, &cx, &cy, &cw, &ch) && 
							self->priv->is_shaped)
	{
    	gint cpt, nb;
		XRectangle* shapes;
	
		if ((shapes = XShapeGetRectangles (CCM_DISPLAY_XDISPLAY(display), 
										   CCM_WINDOW_XWINDOW(self), 
										   ShapeBounding, &nb, &o)))
		{
			geometry = ccm_region_new();
			for (cpt = 0; cpt < nb; cpt++)
				ccm_region_union_with_xrect(geometry, &shapes[cpt]);
			ccm_region_offset(geometry, 
							  self->priv->attribs.x - self->priv->attribs.border_width, 
							  self->priv->attribs.y - self->priv->attribs.border_width);
		}
		else
			self->priv->is_shaped = FALSE;
		XFree(shapes);
	}
	
	if (!self->priv->is_shaped)
	{
		area.x = self->priv->attribs.x - self->priv->attribs.border_width;
		area.y = self->priv->attribs.y - self->priv->attribs.border_width;
		area.width = self->priv->attribs.width + self->priv->attribs.border_width * 2;
		area.height = self->priv->attribs.height + self->priv->attribs.border_width * 2;
		geometry = ccm_region_rectangle(&area);
	}
	
	ccm_window_set_alpha(self);
	if (geometry &&
		ccm_window_get_format(self) != CAIRO_FORMAT_ARGB32 && 
		self->priv->opacity == 1.0f)
	{
		self->opaque = ccm_region_copy(geometry);
	}
		
	if (self->priv->pixmap)
	{
		g_object_unref(self->priv->pixmap);
		self->priv->pixmap = NULL;
	}
	
	return geometry;
}

static CCMRegion*
ccm_window_query_geometry(CCMDrawable* drawable)
{
	g_return_val_if_fail(drawable != NULL, NULL);

	CCMWindow* self = CCM_WINDOW(drawable);
	CCMRegion* geometry = NULL;
	
	geometry = ccm_window_plugin_query_geometry(self->priv->plugin, self);
	
	return geometry;
}

static void
ccm_window_move(CCMDrawable* drawable, int x, int y)
{
	g_return_if_fail(drawable != NULL);
	
	CCMWindow* self = CCM_WINDOW(drawable);

	ccm_window_plugin_move(self->priv->plugin, self, x , y);
}

static void
impl_ccm_window_move(CCMWindowPlugin* plugin, CCMWindow* self, int x, int y)
{
	cairo_rectangle_t geometry;
	
	if (ccm_drawable_get_geometry_clipbox(CCM_DRAWABLE(self), &geometry) &&
		(x != (int)geometry.x || y != (int)geometry.y))
	{
		CCMRegion* old_geometry = ccm_region_rectangle (&geometry);
		CCMPixmap* pixmap = ccm_window_get_pixmap(self);
		CCMWindowType type = ccm_window_get_hint_type (self);
		
		if (pixmap && !ccm_drawable_is_damaged (CCM_DRAWABLE(pixmap)) &&
			CCM_IS_PIXMAP_BUFFERED(pixmap) &&
			ccm_window_is_managed (self) && ccm_window_is_decorated (self) &&
			(type == CCM_WINDOW_TYPE_NORMAL || type == CCM_WINDOW_TYPE_DIALOG))
			g_object_set(pixmap, "buffered", TRUE, NULL);
		else if (CCM_IS_PIXMAP_BUFFERED(pixmap))
			g_object_set(pixmap, "buffered", FALSE, NULL);
			
		CCM_DRAWABLE_CLASS(ccm_window_parent_class)->move(CCM_DRAWABLE(self), 
														  x, y);
		if (self->opaque)
			ccm_region_offset(self->opaque, x - geometry.x, y - geometry.y);
		if ((self->is_viewable || self->priv->unmap_pending) &&
			ccm_drawable_get_geometry_clipbox(CCM_DRAWABLE(self), &geometry))
		{
			ccm_region_union_with_rect (old_geometry, &geometry);
			ccm_drawable_damage_region (CCM_DRAWABLE(self), old_geometry);
		}
		ccm_region_destroy (old_geometry);
	}
}

static void
ccm_window_resize(CCMDrawable* drawable, int width, int height)
{
	g_return_if_fail(drawable != NULL);
	
	CCMWindow* self = CCM_WINDOW(drawable);

	ccm_window_plugin_resize(self->priv->plugin, self, width, height);
}

static void
impl_ccm_window_resize(CCMWindowPlugin* plugin, CCMWindow* self, 
					   int width, int height)
{
	cairo_rectangle_t geometry;
	
	if (!ccm_drawable_get_geometry_clipbox(CCM_DRAWABLE(self), &geometry) ||
		width != (int)geometry.width || height != (int)geometry.height)
	{
		CCMRegion* old_geometry = ccm_region_rectangle (&geometry);
		
		CCM_DRAWABLE_CLASS(ccm_window_parent_class)->resize(CCM_DRAWABLE(self), 
															width, height);
		if (self->is_viewable || self->priv->unmap_pending)
		{
			ccm_drawable_damage_region (CCM_DRAWABLE(self), old_geometry);
			ccm_drawable_damage (CCM_DRAWABLE(self));
		}
		ccm_region_destroy (old_geometry);
		
		if (self->priv->pixmap)
		{
			g_object_unref(self->priv->pixmap);
			self->priv->pixmap = NULL;
		}
	}
}

static gboolean
impl_ccm_window_paint(CCMWindowPlugin* plugin, CCMWindow* self, 
					  cairo_t* context, cairo_surface_t* surface, 
					  gboolean y_invert)
{
	g_return_val_if_fail(self != NULL, FALSE);
	
	cairo_rectangle_t geometry;
	gboolean ret = FALSE;
	
	if (ccm_drawable_get_geometry_clipbox (CCM_DRAWABLE(self), &geometry))
	{
		cairo_matrix_t matrix;
		
		cairo_get_matrix (context, &matrix);
		cairo_translate (context, geometry.x / matrix.xx, geometry.y / matrix.yy);
		if (y_invert)
		{
			cairo_scale (context, 1.0, -1.0);
			cairo_translate (context, 0.0f, -self->priv->attribs.height);
		}
		cairo_set_source_surface(context, surface, 0.0f, 0.0f);
		cairo_paint_with_alpha(context, self->priv->opacity);
		ret  = TRUE;
	}		
	
	return ret;
}

static void
impl_ccm_window_map(CCMWindowPlugin* plugin, CCMWindow* self)
{
	g_return_if_fail(plugin != NULL);
	g_return_if_fail(self != NULL);
	ccm_drawable_query_geometry(CCM_DRAWABLE(self));
}

static void
impl_ccm_window_unmap(CCMWindowPlugin* plugin, CCMWindow* self)
{
	g_return_if_fail(plugin != NULL);
	g_return_if_fail(self != NULL);
	
	self->priv->unmap_pending = FALSE;
	self->priv->keep_above = FALSE;
	self->priv->keep_below = FALSE;
	if (self->priv->pixmap)
	{
		g_object_unref(self->priv->pixmap);
		self->priv->pixmap = NULL;
	}
}
	
static void
impl_ccm_window_query_opacity(CCMWindowPlugin* plugin, CCMWindow* self)
{
	guint32* data = NULL;
	guint n_items;
	
	data = ccm_window_get_property(self, 
								   CCM_WINDOW_GET_CLASS(self)->opacity_atom,
								   32, XA_CARDINAL, &n_items);
									  
	if (data) 
	{
		self->priv->opacity = (double)*data / (double)0xffffffff;
		g_free(data);
		
		if (self->priv->opacity == 1.0f && 
			ccm_window_get_format(self) != CAIRO_FORMAT_ARGB32 &&
			!self->opaque)
			ccm_window_set_opaque(self);
		else
			ccm_window_set_alpha(self);
		
		ccm_drawable_damage(CCM_DRAWABLE(self));
	}
}

static void
impl_ccm_window_set_opaque(CCMWindowPlugin* plugin, CCMWindow* self)
{
	g_return_if_fail(self != NULL);
	
	CCMRegion* geometry = ccm_drawable_get_geometry(CCM_DRAWABLE(self));
	
	ccm_window_set_alpha(self);
	if (geometry) self->opaque = ccm_region_copy(geometry);
}

CCMWindowPlugin*
_ccm_window_get_plugin(CCMWindow *self, GType type)
{
	g_return_val_if_fail(self != NULL, NULL);
	
	CCMWindowPlugin* plugin;
	
	for (plugin = self->priv->plugin; plugin != (CCMWindowPlugin*)self; plugin = CCM_WINDOW_PLUGIN_PARENT(plugin))
	{
		if (g_type_is_a(G_OBJECT_TYPE(plugin), type))
			return plugin;
	}
	
	return NULL;
}

Window
_ccm_window_get_child(CCMWindow* self)
{
	g_return_val_if_fail(self != NULL, None);
	
	return self->priv->child;
}

void
_ccm_window_set_child(CCMWindow* self, Window child)
{
	g_return_if_fail(self != NULL);
	
	self->priv->child = child;
}

CCMWindow *
ccm_window_new (CCMScreen* screen, Window xwindow)
{
	g_return_val_if_fail(screen != NULL, NULL);
	g_return_val_if_fail(xwindow != None, NULL);
	
	CCMDisplay* display = ccm_screen_get_display (screen);
	ccm_display_sync(display);
	CCMWindow* self = g_object_new(CCM_TYPE_WINDOW_BACKEND(screen), 
								   "screen", screen,
								   "drawable", xwindow,
								   NULL);
	GSList* item, *plugins = _ccm_screen_get_window_plugins(screen);
	self->priv->plugin = (CCMWindowPlugin*)self;
	
	create_atoms(self);
	
	for (item = plugins; item; item = item->next)
	{
		GType type = GPOINTER_TO_INT(item->data);
		GObject* prev = G_OBJECT(self->priv->plugin);
		
		self->priv->plugin = g_object_new(type, "parent", prev, NULL);
	}
	g_slist_free(plugins);
	
	ccm_window_plugin_load_options(self->priv->plugin, self);
	
	if (!ccm_drawable_query_geometry(CCM_DRAWABLE(self)))
	{
		g_object_unref(self);
		return NULL;
	}
	
	if (!self->is_input_only)
	{
		ccm_window_query_hint_type(self);
		ccm_window_query_state (self);
		ccm_window_query_child (self);
		
		XSelectInput (CCM_DISPLAY_XDISPLAY(ccm_screen_get_display(screen)), 
					  CCM_WINDOW_XWINDOW(self),
					  PropertyChangeMask | 
					  SubstructureNotifyMask);
	}
	
	g_signal_connect_swapped(display, "get-property-event",
							 G_CALLBACK(ccm_window_on_get_property_async), 
							 self);
							 
	return self;
}

gboolean
ccm_window_is_managed(CCMWindow* self)
{
	g_return_val_if_fail(self != NULL, FALSE);

	return !self->priv->override_redirect;
}

void
ccm_window_query_state(CCMWindow* self)
{
	g_return_if_fail(self != NULL);
	
	ccm_window_get_property_async(self, CCM_WINDOW_GET_CLASS(self)->state_atom,
								  XA_ATOM, G_MAXLONG);
	
	ccm_window_get_child_property_async(self, 
										CCM_WINDOW_GET_CLASS(self)->state_atom,
										XA_ATOM, G_MAXLONG);
}

void
ccm_window_set_state(CCMWindow* self, Atom state_atom)
{
	g_return_if_fail(self != NULL);
	gboolean changed = FALSE;
	
	if (state_atom == CCM_WINDOW_GET_CLASS(self)->state_shade_atom)
	{
		changed  = !self->priv->is_shaded;
		self->priv->is_shaded = TRUE;
		ccm_drawable_damage (CCM_DRAWABLE(self));
	}
	else if (state_atom == CCM_WINDOW_GET_CLASS(self)->state_fullscreen_atom)
	{
		CCMScreen* screen = ccm_drawable_get_screen (CCM_DRAWABLE(self));
	
		changed  = !self->priv->is_fullscreen;
		self->priv->is_fullscreen = TRUE;
		ccm_screen_damage (screen);
	}
	else if (state_atom == CCM_WINDOW_GET_CLASS(self)->state_is_modal)
	{
		changed  = !self->priv->is_modal;
		self->priv->is_modal = TRUE;
	}
	else if (state_atom == CCM_WINDOW_GET_CLASS(self)->state_skip_taskbar)
	{
		changed  = !self->priv->skip_taskbar;
		self->priv->skip_taskbar = TRUE;
	}
	else if (state_atom == CCM_WINDOW_GET_CLASS(self)->state_skip_pager)
	{
		changed  = !self->priv->skip_pager;
		self->priv->skip_pager = TRUE;
	}
	else if (state_atom == CCM_WINDOW_GET_CLASS(self)->state_above_atom)
	{
		changed  = !self->priv->keep_above;
		self->priv->keep_above = TRUE;
	}
	else if (state_atom == CCM_WINDOW_GET_CLASS(self)->state_below_atom)
	{
		changed  = !self->priv->keep_below;
		self->priv->keep_below = TRUE;
	}
	
	if (changed) g_signal_emit(self, signals[STATE_CHANGED], 0);
}

void
ccm_window_unset_state(CCMWindow* self, Atom state_atom)
{
	g_return_if_fail(self != NULL);
	
	gboolean changed = FALSE;
	
	if (state_atom == CCM_WINDOW_GET_CLASS(self)->state_shade_atom)
	{
		changed = self->priv->is_shaded;
		self->priv->is_shaded = FALSE;
		if (changed) ccm_drawable_damage (CCM_DRAWABLE(self));
	}
	else if (state_atom == CCM_WINDOW_GET_CLASS(self)->state_fullscreen_atom)
	{
		CCMScreen* screen = ccm_drawable_get_screen (CCM_DRAWABLE(self));
		
		changed = self->priv->is_fullscreen;
		self->priv->is_fullscreen = FALSE;
		if (changed) ccm_screen_damage (screen);
	}
	else if (state_atom == CCM_WINDOW_GET_CLASS(self)->state_is_modal)
	{
		changed = self->priv->is_modal;
		self->priv->is_modal = FALSE;
	}
	else if (state_atom == CCM_WINDOW_GET_CLASS(self)->state_skip_taskbar)
	{
		changed = self->priv->skip_taskbar;
		self->priv->skip_taskbar = FALSE;
	}
	else if (state_atom == CCM_WINDOW_GET_CLASS(self)->state_skip_pager)
	{
		changed = self->priv->skip_pager;
		self->priv->skip_pager = FALSE;
	}
	else if (state_atom == CCM_WINDOW_GET_CLASS(self)->state_above_atom)
	{
		changed = self->priv->keep_above;
		self->priv->keep_above = FALSE;
	}
	else if (state_atom == CCM_WINDOW_GET_CLASS(self)->state_below_atom)
	{
		changed = self->priv->keep_below;
		self->priv->keep_below = FALSE;
	}
	if (changed) g_signal_emit(self, signals[STATE_CHANGED], 0);
}

void
ccm_window_switch_state(CCMWindow* self, Atom state_atom)
{
	g_return_if_fail(self != NULL);
	
	gboolean changed = FALSE;
	
	if (state_atom == CCM_WINDOW_GET_CLASS(self)->state_shade_atom)
	{
		changed = TRUE;
		self->priv->is_shaded = !self->priv->is_shaded;
		ccm_drawable_damage (CCM_DRAWABLE(self));
	}
	else if (state_atom == CCM_WINDOW_GET_CLASS(self)->state_fullscreen_atom)
	{
		CCMScreen* screen = ccm_drawable_get_screen (CCM_DRAWABLE(self));
	
		changed = TRUE;
		self->priv->is_fullscreen = !self->priv->is_fullscreen;
		ccm_screen_damage (screen);
	}
	else if (state_atom == CCM_WINDOW_GET_CLASS(self)->state_is_modal)
	{
		changed = TRUE;
		self->priv->is_modal = !self->priv->is_modal;
	}
	else if (state_atom == CCM_WINDOW_GET_CLASS(self)->state_skip_taskbar)
	{
		changed = TRUE;
		self->priv->skip_taskbar = !self->priv->skip_taskbar;
	}
	else if (state_atom == CCM_WINDOW_GET_CLASS(self)->state_skip_pager)
	{
		changed = TRUE;
		self->priv->skip_pager = !self->priv->skip_pager;
	}
	else if (state_atom == CCM_WINDOW_GET_CLASS(self)->state_above_atom)
	{
		changed = TRUE;
		self->priv->keep_above = !self->priv->keep_above;
	}
	else if (state_atom == CCM_WINDOW_GET_CLASS(self)->state_below_atom)
	{
		changed = TRUE;
		self->priv->keep_below = !self->priv->keep_below;
	}
	
	if (changed) g_signal_emit(self, signals[STATE_CHANGED], 0);
}

gboolean
ccm_window_is_shaded(CCMWindow* self)
{
	g_return_val_if_fail(self != NULL, FALSE);
	
	return self->priv->is_shaded;
}

gboolean
ccm_window_is_fullscreen(CCMWindow* self)
{
	g_return_val_if_fail(self != NULL, FALSE);
	
	return self->priv->is_fullscreen;
}

gboolean
ccm_window_skip_taskbar(CCMWindow* self)
{
	g_return_val_if_fail(self != NULL, FALSE);
	
	return self->priv->skip_taskbar;
}

gboolean
ccm_window_skip_pager(CCMWindow* self)
{
	g_return_val_if_fail(self != NULL, FALSE);
	
	return self->priv->skip_pager;
}

gboolean
ccm_window_keep_above(CCMWindow* self)
{
	g_return_val_if_fail(self != NULL, FALSE);
	
	return self->priv->keep_above;
}

gboolean
ccm_window_keep_below(CCMWindow* self)
{
	g_return_val_if_fail(self != NULL, FALSE);
	
	return self->priv->keep_below;
}

CCMWindow*
ccm_window_transient_for(CCMWindow* self)
{
	g_return_val_if_fail(self != NULL, NULL);
	
	CCMScreen* screen = ccm_drawable_get_screen (CCM_DRAWABLE(self));
	CCMWindow* root = ccm_screen_get_root_window (screen);
	CCMWindow* window = NULL;
	
	if (self->priv->transient_for)
		window = ccm_screen_find_window_or_child (screen,
												  self->priv->transient_for);
	else if (self->priv->transient_for == CCM_WINDOW_XWINDOW(root))
		window = root;
	
	return window;
}

CCMWindow*
ccm_window_get_group_leader(CCMWindow* self)
{
	g_return_val_if_fail(self != NULL, NULL);
	
	CCMScreen* screen = ccm_drawable_get_screen (CCM_DRAWABLE(self));
	CCMWindow* window = NULL;
	
	if (self->priv->group_leader)
		window = ccm_screen_find_window_or_child (screen,
												  self->priv->group_leader);
	
	return window;
}

void
ccm_window_make_output_only(CCMWindow* self)
{
	g_return_if_fail(self != NULL);
	
	CCMDisplay* display = ccm_drawable_get_display(CCM_DRAWABLE(self));
	XserverRegion region;
	
	region = XFixesCreateRegion(CCM_DISPLAY_XDISPLAY(display), 0, 0);
	XFixesSetWindowShapeRegion (CCM_DISPLAY_XDISPLAY(display),
								CCM_WINDOW_XWINDOW(self),
								ShapeInput, 0, 0, region);
}

void
ccm_window_make_input_output(CCMWindow* self)
{
	g_return_if_fail(self != NULL);
	
	CCMDisplay* display = ccm_drawable_get_display(CCM_DRAWABLE(self));
	XserverRegion region;
	cairo_rectangle_t geometry;
	
	if (ccm_drawable_get_geometry_clipbox (CCM_DRAWABLE(self), &geometry))
	{
		XRectangle rect;
		rect.x = geometry.x;
		rect.y = geometry.y;
		rect.width = geometry.width;
		rect.width = geometry.height;
		region = XFixesCreateRegion(CCM_DISPLAY_XDISPLAY(display), 
									&rect, 1);
		XFixesSetWindowShapeRegion (CCM_DISPLAY_XDISPLAY(display),
									CCM_WINDOW_XWINDOW(self),
									ShapeInput, 0, 0, 
									region);
	}
}

void
ccm_window_redirect (CCMWindow* self)
{
    g_return_if_fail (self != NULL);
    
	XCompositeRedirectWindow (
					CCM_DISPLAY_XDISPLAY(ccm_drawable_get_display(CCM_DRAWABLE(self))), 
					CCM_WINDOW_XWINDOW(self),
					CompositeRedirectManual);
}

void
ccm_window_redirect_subwindows(CCMWindow* self)
{
    g_return_if_fail (self != NULL);
    
	XCompositeRedirectSubwindows(
					CCM_DISPLAY_XDISPLAY(ccm_drawable_get_display(CCM_DRAWABLE(self))), 
					CCM_WINDOW_XWINDOW(self),
					CompositeRedirectManual);
}

void
ccm_window_unredirect (CCMWindow* self)
{
    g_return_if_fail (self != NULL);
    
	XCompositeUnredirectWindow (
					CCM_DISPLAY_XDISPLAY(ccm_drawable_get_display(CCM_DRAWABLE(self))), 
					CCM_WINDOW_XWINDOW(self),
					CompositeRedirectManual);
}

void
ccm_window_unredirect_subwindows (CCMWindow* self)
{
    g_return_if_fail (self != NULL);
    
	XCompositeUnredirectSubwindows (
					CCM_DISPLAY_XDISPLAY(ccm_drawable_get_display(CCM_DRAWABLE(self))), 
					CCM_WINDOW_XWINDOW(self),
					CompositeRedirectManual);
}

void
on_pixmap_damaged(CCMWindow* self, CCMRegion* area)
{
	g_return_if_fail (self != NULL);
    g_return_if_fail (area != NULL);
	
	cairo_rectangle_t geometry;
	
	if (ccm_drawable_get_geometry_clipbox(CCM_DRAWABLE(self), &geometry))
	{
		ccm_region_offset(area, geometry.x, geometry.y);
		ccm_drawable_damage_region(CCM_DRAWABLE(self), area);
	}
}

CCMPixmap*
ccm_window_get_pixmap(CCMWindow* self)
{
	g_return_val_if_fail(self != NULL, NULL);
	
	if (!self->priv->pixmap)
	{
		CCMDisplay *display = ccm_drawable_get_display(CCM_DRAWABLE(self));
		
		ccm_display_grab(display);
		Pixmap xpixmap = XCompositeNameWindowPixmap(
												CCM_DISPLAY_XDISPLAY(display),
						 						CCM_WINDOW_XWINDOW(self));
		ccm_display_ungrab(display);
		if (xpixmap)
		{
			self->priv->pixmap = ccm_pixmap_new(self, xpixmap);
			g_signal_connect_swapped(self->priv->pixmap, "damaged", 
									 G_CALLBACK(on_pixmap_damaged), self);
		}
	}
	
	return self->priv->pixmap;
}

cairo_format_t
ccm_window_get_format (CCMWindow* self)
{
    g_return_val_if_fail(self != NULL, 0);
	
	CCMDisplay* display = ccm_drawable_get_display(CCM_DRAWABLE(self));
    XWindowAttributes attrs;
    
    if (self->priv->has_format) return self->priv->format;
    
    if (!XGetWindowAttributes (CCM_DISPLAY_XDISPLAY(display), 
							   CCM_WINDOW_XWINDOW(self), &attrs))
		return CAIRO_FORMAT_ARGB32;
	
    self->is_viewable = attrs.map_state == IsViewable;
	self->priv->override_redirect = attrs.override_redirect;
	
    if (attrs.class == InputOnly)
    {
		self->is_input_only = TRUE;
    }
    else if (attrs.depth == 16 &&
			 attrs.visual->red_mask == 0xf800 &&
			 attrs.visual->green_mask == 0x7e0 &&
			 attrs.visual->blue_mask == 0x1f)
    {
		self->priv->format = CAIRO_FORMAT_A8;
    }
    else if (attrs.depth == 24 &&
	     	 attrs.visual->red_mask == 0xff0000 &&
			 attrs.visual->green_mask == 0xff00 &&
			 attrs.visual->blue_mask == 0xff)
    {
		self->priv->format = CAIRO_FORMAT_RGB24;
    }
    else if (attrs.depth == 32 &&
			 attrs.visual->red_mask == 0xff0000 &&
			 attrs.visual->green_mask == 0xff00 &&
			 attrs.visual->blue_mask == 0xff)
    {
		self->priv->format = CAIRO_FORMAT_ARGB32;
    }
    else
    {
		g_warning ("Unknown visual format depth=%d, r=%#lx/g=%#lx/b=%#lx",
				   attrs.depth, attrs.visual->red_mask,
				   attrs.visual->green_mask, attrs.visual->blue_mask);
	
		self->priv->format = CAIRO_FORMAT_ARGB32;
    }
    
	self->priv->has_format = TRUE;
    
    return self->priv->format;
}

guint
ccm_window_get_depth (CCMWindow* self)
{
    g_return_val_if_fail(self != NULL, 0);
	
    switch (ccm_window_get_format(self))
    {
    	case CAIRO_FORMAT_A8:
		return 16;

    	case CAIRO_FORMAT_RGB24:
		return 24;
	
    	case CAIRO_FORMAT_ARGB32:
		return 32;

		default:
		break;
    };

    return 0;
}

gfloat
ccm_window_get_opacity (CCMWindow* self)
{
	g_return_val_if_fail(self != NULL, 1.0f);
	
	return self->priv->opacity;
}

void
ccm_window_set_opacity (CCMWindow* self, gfloat opacity)
{
	g_return_if_fail(self != NULL);
	
	self->priv->opacity = opacity;
	if (self->priv->opacity < 1.0f)
		ccm_window_set_alpha(self);
	else if (self->priv->format != CAIRO_FORMAT_ARGB32)
		ccm_window_set_opaque(self);
}

gboolean
ccm_window_paint (CCMWindow* self, cairo_t* context, gboolean buffered)
{
	g_return_val_if_fail(self != NULL, FALSE);
	g_return_val_if_fail(context != NULL, FALSE);
	
	gboolean ret = FALSE;
	
	cairo_path_t* damaged;
			
	if (!self->is_viewable && !self->priv->unmap_pending)
		return ret;
	
	cairo_save(context);
	
	damaged = ccm_drawable_get_damage_path(CCM_DRAWABLE(self), context);
	
	if (damaged)
	{
		CCMPixmap* pixmap = ccm_window_get_pixmap(self);
		cairo_clip(context);
	
		if (pixmap)
		{
			cairo_surface_t* surface;
			gboolean y_invert;
			
			g_object_get (pixmap, "y_invert", &y_invert, NULL);
			
			if (CCM_IS_PIXMAP_BUFFERED(pixmap) && !buffered)
				g_object_set(pixmap, "buffered", FALSE, NULL);
			
			surface = ccm_drawable_get_surface(CCM_DRAWABLE(pixmap));
				
			if (surface)
			{
				ret = ccm_window_plugin_paint(self->priv->plugin, self, 
											  context, surface, y_invert);
				cairo_surface_destroy(surface);
			}
			
			if (CCM_IS_PIXMAP_BUFFERED(pixmap))
				g_object_set(pixmap, "buffered", FALSE, NULL);
		}
		cairo_reset_clip (context);
		cairo_path_destroy(damaged);
	}
	cairo_restore(context);
	
	if (ret) ccm_drawable_repair(CCM_DRAWABLE(self));
	
	return ret;
}

void
ccm_window_map(CCMWindow* self)
{
	g_return_if_fail(self != NULL);
	
	if (!self->is_viewable)
	{
		self->is_viewable = TRUE;
	
		ccm_window_query_hint_type (self);
		ccm_window_query_mwm_hints (self);
		ccm_window_plugin_map(self->priv->plugin, self);
	}
	else
		ccm_drawable_damage (CCM_DRAWABLE(self));
}

void
ccm_window_unmap(CCMWindow* self)
{
	g_return_if_fail(self != NULL);
	
	if (self->is_viewable)
	{
		self->is_viewable = FALSE;
		self->priv->unmap_pending = TRUE;
		
		ccm_window_plugin_unmap(self->priv->plugin, self);
	}
	else
		ccm_drawable_damage (CCM_DRAWABLE(self));
}

void 
ccm_window_query_opacity(CCMWindow* self)
{
	g_return_if_fail(self != NULL);
	
	ccm_window_plugin_query_opacity (self->priv->plugin, self);
}

void 
ccm_window_query_mwm_hints(CCMWindow* self)
{
	g_return_if_fail(self != NULL);
	
	ccm_window_get_property_async(self, 
								  CCM_WINDOW_GET_CLASS(self)->mwm_hints_atom,
								  AnyPropertyType, 20L);
}

void
ccm_window_query_hint_type(CCMWindow* self)
{
	g_return_if_fail(self != NULL);
	
	ccm_window_get_property_async (self, CCM_WINDOW_GET_CLASS(self)->type_atom,
								   XA_ATOM, G_MAXLONG);
}

void
ccm_window_query_wm_hints(CCMWindow* self)
{
	g_return_if_fail(self != NULL);
	
	XWMHints *hints;
	CCMDisplay* display = ccm_drawable_get_display (CCM_DRAWABLE(self));
	
	hints = XGetWMHints (CCM_DISPLAY_XDISPLAY(display), 
						 CCM_WINDOW_XWINDOW(self));
	if (hints)
    {
		if (hints->flags & WindowGroupHint)
			self->priv->group_leader = hints->window_group;
		XFree (hints);
	}
}

CCMWindowType
ccm_window_get_hint_type(CCMWindow* self)
{
	g_return_val_if_fail(self != NULL, CCM_WINDOW_TYPE_NORMAL);
	
	return self->priv->hint_type;
}

const gchar*
ccm_window_get_name(CCMWindow* self)
{
	g_return_val_if_fail(self != NULL, NULL);
	
	if (self->priv->name == NULL)
	{
		self->priv->name = ccm_window_get_utf8_property (self, 
								CCM_WINDOW_GET_CLASS(self)->visible_name_atom);
		
		if (!self->priv->name)
			self->priv->name = ccm_window_get_utf8_property (self, 
										CCM_WINDOW_GET_CLASS(self)->name_atom);
		
		if (!self->priv->name)
			self->priv->name = ccm_window_get_text_property (self, XA_WM_NAME);
	}
	
	if (self->priv->name == NULL)
	{
		self->priv->name = ccm_window_get_child_utf8_property (self, 
								CCM_WINDOW_GET_CLASS(self)->visible_name_atom);
		
		if (!self->priv->name)
			self->priv->name = ccm_window_get_child_utf8_property (self, 
										CCM_WINDOW_GET_CLASS(self)->name_atom);
		
		if (!self->priv->name)
			self->priv->name = ccm_window_get_child_text_property (self, XA_WM_NAME);
	}
	return self->priv->name;
}

void
ccm_window_add_alpha_region(CCMWindow* self, CCMRegion* region)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(region != NULL);
	
	if (self->opaque)
	{
		ccm_region_subtract(self->opaque, region);
		if (ccm_region_empty(self->opaque))
		{
			ccm_region_destroy(self->opaque);
			self->opaque = NULL;
		}
	}
}

void
ccm_window_set_alpha(CCMWindow* self)
{
	g_return_if_fail(self != NULL);
	
	if (self->opaque)
	{
		ccm_region_destroy(self->opaque);
		self->opaque = NULL;
	}
}

void
ccm_window_set_opaque_region(CCMWindow* self, CCMRegion* region)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(region != NULL);
	
	if (!ccm_region_empty(region))
	{
		ccm_window_set_alpha(self);
		self->opaque = ccm_region_copy(region);
	}
}

void
ccm_window_set_opaque(CCMWindow* self)
{
	g_return_if_fail(self != NULL);
	
	ccm_window_plugin_set_opaque (self->priv->plugin, self);
}

gboolean
ccm_window_is_decorated(CCMWindow* self)
{
	g_return_val_if_fail(self != NULL, TRUE);
	
	return self->priv->is_decorated;
}

gboolean
ccm_window_get_frame_extends(CCMWindow* self, int* left_frame, int* right_frame, 
							 int* top_frame, int* bottom_frame)
{
	guint32* data = NULL;
	guint n_items;
	gboolean ret = FALSE;
	
	if (self->priv->child)
		data = ccm_window_get_child_property(self, 
								CCM_WINDOW_GET_CLASS(self)->frame_extends_atom,
								32, XA_CARDINAL, &n_items);
	else
		data = ccm_window_get_property(self, 
								CCM_WINDOW_GET_CLASS(self)->frame_extends_atom,
								32, XA_CARDINAL, &n_items);
	if (data)
	{
		guint32* extends = (guint32*)data;
		
		if (n_items == 4)
		{
			*left_frame   = (int)extends[0];
      		*right_frame  = (int)extends[1];
      		*top_frame    = (int)extends[2];
      		*bottom_frame = (int)extends[3];
			ret = TRUE;
		}
		g_free(data);
	}
	
	return ret;
}
