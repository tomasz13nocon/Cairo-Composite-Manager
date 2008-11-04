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
#include <X11/Xlib.h>
#include <math.h>

#include "ccm-drawable.h"
#include "ccm-window.h"
#include "ccm-screen.h"
#include "ccm-display.h"
#include "ccm-timeline.h"
#include "ccm-freeze.h"
#include "ccm-config.h"
#include "ccm.h"

enum
{
	CCM_FREEZE_DELAY,
	CCM_FREEZE_DURATION,
	CCM_FREEZE_OPTION_N
};

static gchar* CCMFreezeOptions[CCM_FREEZE_OPTION_N] = {
	"delay",
	"duration"
};

static void ccm_freeze_window_iface_init(CCMWindowPluginClass* iface);
static void ccm_freeze_screen_iface_init(CCMScreenPluginClass* iface);
	
CCM_DEFINE_PLUGIN (CCMFreeze, ccm_freeze, CCM_TYPE_PLUGIN, 
				   CCM_IMPLEMENT_INTERFACE(ccm_freeze,
										   CCM_TYPE_WINDOW_PLUGIN,
										   ccm_freeze_window_iface_init);
				   CCM_IMPLEMENT_INTERFACE(ccm_freeze,
										   CCM_TYPE_SCREEN_PLUGIN,
										   ccm_freeze_screen_iface_init))

struct _CCMFreezePrivate
{	
	gboolean alive;
	gfloat opacity;
	
	CCMWindow* window;
	
	guint id_ping;
	guint32 last_ping;
	
	CCMTimeline* timeline;
	CCMConfig* options[CCM_FREEZE_OPTION_N];
};

#define CCM_FREEZE_GET_PRIVATE(o)  \
   ((CCMFreezePrivate*)G_TYPE_INSTANCE_GET_PRIVATE ((o), CCM_TYPE_FREEZE, CCMFreezeClass))

static void
ccm_freeze_init (CCMFreeze *self)
{
	gint cpt;
	
	self->priv = CCM_FREEZE_GET_PRIVATE(self);
	self->priv->alive = TRUE;
	self->priv->window = NULL;
	self->priv->id_ping = 0;
	self->priv->last_ping = 0;
	self->priv->timeline = NULL;
	for (cpt = 0; cpt < CCM_FREEZE_OPTION_N; cpt++) 
		self->priv->options[cpt] = NULL;
}

static void
ccm_freeze_finalize (GObject *object)
{
	CCMFreeze* self = CCM_FREEZE(object);
	gint cpt;
	
	for (cpt = 0; cpt < CCM_FREEZE_OPTION_N; cpt++)
		if (self->priv->options[cpt]) g_object_unref(self->priv->options[cpt]);
	self->priv->window = NULL;
	if (self->priv->id_ping) g_source_remove (self->priv->id_ping);
	self->priv->opacity = 0.0f;
	self->priv->id_ping = 0;
	self->priv->last_ping = 0;
	if (self->priv->timeline) 
		g_object_unref (self->priv->timeline);
	
	G_OBJECT_CLASS (ccm_freeze_parent_class)->finalize (object);
}

static void
ccm_freeze_class_init (CCMFreezeClass *klass)
{
	GObjectClass* object_class = G_OBJECT_CLASS (klass);
	
	klass->protocol_atom = None;
	klass->ping_atom = None;
	
	g_type_class_add_private (klass, sizeof (CCMFreezePrivate));
	
	object_class->finalize = ccm_freeze_finalize;
}

static void
ccm_freeze_on_new_frame (CCMFreeze* self, guint num_frame, 
						 CCMTimeline* timeline)
{
	if (!self->priv->alive && self->priv->opacity < 0.5)
	{
		gfloat progress = ccm_timeline_get_progress (timeline);
		
		self->priv->opacity = progress / 2.0f;
		ccm_drawable_damage (CCM_DRAWABLE(self->priv->window));
	}
}

static void
ccm_freeze_on_event(CCMFreeze* self, XEvent* event)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(event != NULL);
	
	if (!CCM_IS_FREEZE(self)) return;
	
	if (self->priv->window && event->type == ClientMessage)
	{
		Window window = None;
		
		g_object_get (G_OBJECT(self->priv->window), "child", &window, NULL);
    	
		if (!window) window = CCM_WINDOW_XWINDOW(self->priv->window);
		
		if (event->xclient.message_type == CCM_FREEZE_GET_CLASS(self)->protocol_atom &&
			event->xclient.data.l[2] == window)
		{
			self->priv->alive = TRUE;
			self->priv->last_ping = 0;
			ccm_timeline_start(self->priv->timeline);
		}
	}
}

static gboolean
ccm_freeze_ping(CCMFreeze* self)
{
	if (self->priv->window && ccm_window_is_viewable (self->priv->window))
	{
		CCMWindowType type = ccm_window_get_hint_type (self->priv->window);
		
		if (type != CCM_WINDOW_TYPE_NORMAL && type != CCM_WINDOW_TYPE_UNKNOWN)
		{
			self->priv->alive = TRUE;
			ccm_timeline_stop(self->priv->timeline);
			return FALSE;
		}
		
		XEvent event;
		CCMDisplay* display = 
			ccm_drawable_get_display (CCM_DRAWABLE(self->priv->window));
		Window window = None;
		
		g_object_get (G_OBJECT(self->priv->window), "child", &window, NULL);
		
		if (!window) window = CCM_WINDOW_XWINDOW(self->priv->window);
		
		if (self->priv->last_ping)
		{
			self->priv->alive = FALSE;
			ccm_timeline_start(self->priv->timeline);
		}
		else
		{
			self->priv->alive = TRUE;
			ccm_timeline_stop(self->priv->timeline);
			if (self->priv->opacity > 0)
				ccm_drawable_damage (CCM_DRAWABLE(self->priv->window));
			self->priv->opacity = 0.0f;
		}
		
		self->priv->last_ping = 1;
		
		ccm_drawable_damage (CCM_DRAWABLE(self->priv->window));
		event.type = ClientMessage;
		event.xclient.window = window;
		event.xclient.message_type = CCM_FREEZE_GET_CLASS(self)->protocol_atom;
		event.xclient.format = 32;
		event.xclient.data.l[0] = CCM_FREEZE_GET_CLASS(self)->ping_atom;
		event.xclient.data.l[1] = self->priv->last_ping;
		event.xclient.data.l[2] = window;
		event.xclient.data.l[3] = 0;
		event.xclient.data.l[4] = 0;
		XSendEvent (CCM_DISPLAY_XDISPLAY(display), window, False, 
					NoEventMask, &event);
	}
	
	
	return TRUE;
}

static void
ccm_freeze_load_options(CCMWindowPlugin* plugin, CCMWindow* window)
{
	CCMFreeze* self = CCM_FREEZE(plugin);
	CCMScreen* screen = ccm_drawable_get_screen(CCM_DRAWABLE(window));
	CCMDisplay* display = ccm_drawable_get_display (CCM_DRAWABLE(window));
	gint cpt;
	gfloat duration;
	
	for (cpt = 0; cpt < CCM_FREEZE_OPTION_N; cpt++)
	{
		self->priv->options[cpt] = ccm_config_new(CCM_SCREEN_NUMBER(screen), 
												  "freeze", 
												  CCMFreezeOptions[cpt]);
	}
	ccm_window_plugin_load_options(CCM_WINDOW_PLUGIN_PARENT(plugin), window);
	
	self->priv->window = window;
	g_signal_connect_swapped(G_OBJECT(display), "event", 
							 G_CALLBACK(ccm_freeze_on_event), self);
	
	duration = ccm_config_get_float(self->priv->options[CCM_FREEZE_DURATION]);
	self->priv->timeline = ccm_timeline_new((int)(60.0 * duration), 60);
		
	g_signal_connect_swapped(self->priv->timeline, "new-frame", 
							 G_CALLBACK(ccm_freeze_on_new_frame), self);
	
	if (CCM_FREEZE_GET_CLASS(self)->protocol_atom == None)
		CCM_FREEZE_GET_CLASS(self)->protocol_atom = 
			XInternAtom (CCM_DISPLAY_XDISPLAY(display), "WM_PROTOCOLS", 0);
	if (CCM_FREEZE_GET_CLASS(self)->ping_atom == None)
		CCM_FREEZE_GET_CLASS(self)->ping_atom = 
			XInternAtom (CCM_DISPLAY_XDISPLAY(display), "_NET_WM_PING", 0);
}

static gboolean 
ccm_freeze_paint(CCMWindowPlugin* plugin, CCMWindow* window, 
				 cairo_t* context, cairo_surface_t* surface,
				 gboolean y_invert)
{
	CCMFreeze* self = CCM_FREEZE(plugin);
	gboolean ret;
	
	ret = ccm_window_plugin_paint(CCM_WINDOW_PLUGIN_PARENT(plugin), window, 
								  context, surface, y_invert);
	
	if (ccm_window_is_viewable (window) && !self->priv->alive)
	{
		cairo_set_source_rgba(context, 0, 0, 0, self->priv->opacity);
		cairo_paint(context);
	}
	
	return ret;
}

static void
ccm_freeze_map(CCMWindowPlugin* plugin, CCMWindow* window)
{
	CCMFreeze* self = CCM_FREEZE(plugin);
	
	CCMWindowType type = ccm_window_get_hint_type (window);
	
	self->priv->window = window;
		
	if (!self->priv->id_ping && !ccm_window_is_input_only (window) && 
		ccm_window_is_viewable (window) &&
		(type == CCM_WINDOW_TYPE_NORMAL || type == CCM_WINDOW_TYPE_UNKNOWN) &&
		ccm_window_is_managed (self->priv->window))	
	{
		gint delay = 
			ccm_config_get_integer (self->priv->options[CCM_FREEZE_DELAY]);
		
		self->priv->alive = TRUE;
		self->priv->id_ping = g_timeout_add_full (G_PRIORITY_LOW, delay * 1000, 
											(GSourceFunc)ccm_freeze_ping, 
											 self, NULL);
	}
	ccm_window_plugin_map(CCM_WINDOW_PLUGIN_PARENT(plugin), window);
}

static void
ccm_freeze_unmap(CCMWindowPlugin* plugin, CCMWindow* window)
{
	CCMFreeze* self = CCM_FREEZE(plugin);
	
	self->priv->window = window;
	if (self->priv->id_ping)	
	{
		g_source_remove (self->priv->id_ping);
		self->priv->id_ping = 0;
		self->priv->alive = TRUE;
		ccm_timeline_stop(self->priv->timeline);
	}
	ccm_window_plugin_unmap(CCM_WINDOW_PLUGIN_PARENT(plugin), window);
}

static gboolean
ccm_freeze_add_window(CCMScreenPlugin* plugin, CCMScreen* screen, 
					  CCMWindow* window)
{
	CCMFreeze* self = CCM_FREEZE(_ccm_window_get_plugin (window, 
														 CCM_TYPE_FREEZE));
	CCMWindowType type = ccm_window_get_hint_type (window);
	
	self->priv->window = window;
		
	if (!self->priv->id_ping && !ccm_window_is_input_only (window) && 
		ccm_window_is_viewable (window) &&
		(type == CCM_WINDOW_TYPE_NORMAL || type == CCM_WINDOW_TYPE_UNKNOWN) &&
		ccm_window_is_managed (self->priv->window))	
	{
		gint delay = 
			ccm_config_get_integer (self->priv->options[CCM_FREEZE_DELAY]);
		
		self->priv->alive = TRUE;
		self->priv->id_ping = g_timeout_add_full (G_PRIORITY_LOW, delay * 1000, 
											 (GSourceFunc)ccm_freeze_ping, 
											 self, NULL);
	}
	return ccm_screen_plugin_add_window(CCM_SCREEN_PLUGIN_PARENT(plugin), 
										screen, window);
}

static void
ccm_freeze_remove_window(CCMScreenPlugin* plugin, CCMScreen* screen,
						 CCMWindow* window)
{
	CCMFreeze* self = CCM_FREEZE(_ccm_window_get_plugin (window, 
														 CCM_TYPE_FREEZE));
	
	self->priv->window = window;
	if (self->priv->id_ping)	
	{
		g_source_remove (self->priv->id_ping);
		self->priv->id_ping = 0;
		self->priv->alive = TRUE;
		ccm_timeline_stop(self->priv->timeline);
	}
	ccm_screen_plugin_remove_window(CCM_SCREEN_PLUGIN_PARENT(plugin), screen,
									window);
}

static void
ccm_freeze_window_iface_init(CCMWindowPluginClass* iface)
{
	iface->load_options 	 = ccm_freeze_load_options;
	iface->query_geometry 	 = NULL;
	iface->paint 			 = ccm_freeze_paint;
	iface->map				 = ccm_freeze_map;
	iface->unmap			 = ccm_freeze_unmap;
	iface->query_opacity  	 = NULL;
	iface->move				 = NULL;
	iface->resize			 = NULL;
	iface->set_opaque_region = NULL;
	iface->get_origin		 = NULL;
}

static void
ccm_freeze_screen_iface_init(CCMScreenPluginClass* iface)
{
	iface->load_options 	= NULL;
	iface->paint			= NULL;
	iface->add_window		= ccm_freeze_add_window;
	iface->remove_window	= ccm_freeze_remove_window;
	iface->damage			= NULL;
}
