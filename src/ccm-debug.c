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

#include <unistd.h>
#include <glib.h>

#include "ccm-debug.h"
#include "ccm-drawable.h"
#include "ccm-window.h"
#include "ccm-display.h"

static GTimer* timer = NULL;

static void
ccm_print_log(const char* format, ...)
{
	va_list args;
	gchar* formatted;

	if (!timer) timer = g_timer_new ();
	
	va_start (args, format);
	formatted = g_strdup_vprintf (format, args);
	va_end (args);
	
	g_print("%f: %s", g_timer_elapsed (timer, NULL), formatted);
	g_free (formatted);
}
		 
void
ccm_log (const char *format, ...)
{
	va_list args;
	gchar* formatted;

	va_start (args, format);
	formatted = g_strdup_vprintf (format, args);
	va_end (args);
	
	ccm_print_log("%s\n", formatted);
	g_free(formatted);
} 

void
ccm_log_window (CCMWindow* window, const char *format, ...)
{
	va_list args;
	gchar* formatted;

	va_start (args, format);
	formatted = g_strdup_vprintf (format, args);
	va_end (args);
	
	ccm_print_log("%s: 0x%lx\n", formatted, CCM_WINDOW_XWINDOW(window));
	g_free(formatted);
} 

void
ccm_log_atom (CCMDisplay* display, Atom atom, const char *format, ...)
{
	va_list args;
	gchar* formatted;

	va_start (args, format);
	formatted = g_strdup_vprintf (format, args);
	va_end (args);
	
	ccm_print_log("%s: %s\n", formatted, 
				  XGetAtomName (CCM_DISPLAY_XDISPLAY(display), atom));
	g_free(formatted);
} 

void
ccm_log_region (CCMDrawable* drawable, const char *format, ...)
{
	va_list args;
	gchar* formatted;
	CCMRegion* damaged,* geometry;
	va_start (args, format);
	formatted = g_strdup_vprintf (format, args);
	va_end (args);
	cairo_rectangle_t* rects;
	gint cpt, nb_rects;
	
	ccm_print_log("%s: 0x%lx\n", formatted, ccm_drawable_get_xid(drawable));
	geometry = ccm_drawable_get_geometry (drawable);
	if (geometry)
	{
		g_print("-> geometry : \n");
		ccm_region_get_rectangles (geometry, &rects, &nb_rects);
	
		for (cpt = 0; cpt < nb_rects; cpt++)
			g_print("--> %i, %i, %i, %i\n", (int)rects[cpt].x, (int)rects[cpt].y,
					(int)rects[cpt].width, (int)rects[cpt].height);
		g_free(rects);
	}	
	
	damaged = _ccm_drawable_get_damaged (drawable);
	if (damaged)
	{
		g_print("-> damaged : \n");
		ccm_region_get_rectangles (damaged, &rects, &nb_rects);
		
		for (cpt = 0; cpt < nb_rects; cpt++)
			g_print("--> %i, %i, %i, %i\n", (int)rects[cpt].x, (int)rects[cpt].y,
					(int)rects[cpt].width, (int)rects[cpt].height);
		g_free(rects);
	}
	
	g_free(formatted);
} 
