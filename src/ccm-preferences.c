/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * cairo-compmgr
 * Copyright (C) Nicolas Bruguier 2009 <nicolas.bruguier@supersonicimagine.fr>
 * 
 * cairo-compmgr is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * cairo-compmgr is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <X11/Xatom.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "ccm-cairo-utils.h"
#include "ccm-preferences.h"
#include "ccm-preferences-page.h"

G_DEFINE_TYPE (CCMPreferences, ccm_preferences, G_TYPE_OBJECT);

#define CCM_PREFERENCES_GET_PRIVATE(o)  \
   ((CCMPreferencesPrivate*)G_TYPE_INSTANCE_GET_PRIVATE ((o), CCM_TYPE_PREFERENCES, \
														 CCMPreferencesClass))

struct _CCMPreferencesPrivate
{
	int					  nb_screens;
	
	int					  width;
	int					  height;
	
	GtkBuilder*			  builder;
	CCMPreferencesPage**  screen_pages;
	GtkWidget**			  screen_titles;
};

static void
ccm_preferences_init (CCMPreferences *self)
{
	self->priv = CCM_PREFERENCES_GET_PRIVATE(self);

	self->priv->nb_screens = 0;
	self->priv->width = 0;
	self->priv->height = 0;
	self->priv->builder = NULL;
	self->priv->screen_titles = NULL;
	self->priv->screen_pages = NULL;
}

static void
ccm_preferences_finalize (GObject *object)
{
	CCMPreferences *self = CCM_PREFERENCES(object);
	gint cpt;
	
	for (cpt = 0; cpt < self->priv->nb_screens; ++cpt)
	{
		g_object_unref(self->priv->screen_titles[cpt]);
		g_object_unref(self->priv->screen_pages[cpt]);
	}
	g_free (self->priv->screen_titles);
	g_free (self->priv->screen_pages);
	
	if (self->priv->builder) g_object_unref(self->priv->builder);
	
	G_OBJECT_CLASS (ccm_preferences_parent_class)->finalize (object);
}

static void
ccm_preferences_class_init (CCMPreferencesClass *klass)
{
	GObjectClass* object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (CCMPreferencesPrivate));
	
	object_class->finalize = ccm_preferences_finalize;
}

static gboolean
ccm_preferences_on_screen_change(CCMPreferences* self, GdkEventButton* event,
								 GtkWidget* widget)
{
	int screen_num = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), 
													   "screen_num"));
	GtkNotebook* notebook;
	
	notebook = GTK_NOTEBOOK(gtk_builder_get_object(self->priv->builder, 
												   "notebook"));
	gtk_notebook_set_current_page(notebook, screen_num);
	
	return TRUE;
}

static void
ccm_preferences_on_title_on_size_allocate (CCMPreferences* self,
                                           GtkAllocation* allocation, 
                                           GtkWidget* widget)
{
	GdkPixmap* pixmap;
	cairo_t* ctx;
	gint cpt;

	if (!widget->window) return;
	
    gdk_drawable_get_size(GDK_DRAWABLE(widget->window), 
                          &self->priv->width, &self->priv->height);

	pixmap = gdk_pixmap_new(NULL, self->priv->width, self->priv->height, 1);
	ctx = gdk_cairo_create(GDK_DRAWABLE(pixmap));

	cairo_set_operator(ctx, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_rgba(ctx, 0, 0, 0, 0);
    cairo_paint(ctx);
    
    cairo_set_operator(ctx, CAIRO_OPERATOR_OVER);
	cairo_set_source_rgba(ctx, 1, 1, 1, 1);

	for (cpt = 0; cpt < self->priv->nb_screens; ++cpt)
	{
		GtkWidget* title = self->priv->screen_titles[cpt];

		cairo_save(ctx);
		cairo_notebook_page_round(ctx, 0, 0, 
		                          self->priv->width, self->priv->height, 
								  title->allocation.x, 
								  title->allocation.width,
								  title->allocation.height, 6);
		cairo_fill(ctx);
		cairo_restore(ctx);
	}
	cairo_destroy(ctx);
	
    gdk_window_shape_combine_mask(widget->window, (GdkBitmap*)0, 0, 0);
    gdk_window_input_shape_combine_mask(widget->window, (GdkBitmap*)0, 0, 0);
    gdk_window_shape_combine_mask(widget->window, (GdkBitmap*)pixmap, 0, 0);
    gdk_window_input_shape_combine_mask(widget->window, (GdkBitmap*)pixmap, 0, 0);
	g_object_unref(pixmap);
}

static void
ccm_preferences_create_page(CCMPreferences* self, int screen_num)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail (screen_num >= 0);
		
	GtkBuilder* builder;
	GtkBox* screen_button_box;
	GtkLabel* screen_name;
	GtkNotebook* notebook;
	GtkWidget* page, *screen_title_event;
	gchar* str;
	
	builder = gtk_builder_new();
	gtk_builder_add_from_file(builder, UI_DIR "/ccm-preferences.ui", NULL);
	
	screen_button_box = GTK_BOX(gtk_builder_get_object(self->priv->builder, 
													   "screen_button_box"));
	if (!screen_button_box) return;
	
	notebook = GTK_NOTEBOOK(gtk_builder_get_object(self->priv->builder, 
												   "notebook"));
	if (!notebook) return;

	self->priv->screen_titles[screen_num] = 
		GTK_WIDGET(gtk_builder_get_object(builder, "screen_title_frame"));
	g_signal_connect_swapped(self->priv->screen_titles[screen_num], "size-allocate",
	                         G_CALLBACK(ccm_preferences_on_title_on_size_allocate),
	                         self);
	if (!self->priv->screen_titles[screen_num]) return;
	gtk_widget_show(self->priv->screen_titles[screen_num]);
	gtk_box_pack_start(screen_button_box, 
					   self->priv->screen_titles[screen_num], 
					   FALSE, FALSE, 0);
	
	screen_title_event = 
		GTK_WIDGET(gtk_builder_get_object(builder, "screen_title_event"));
	if (!screen_title_event) return;
	g_object_set_data_full(G_OBJECT(screen_title_event), "screen_num", 
						   GINT_TO_POINTER(screen_num), NULL);
	g_signal_connect_swapped(screen_title_event, "button-press-event", 
							 G_CALLBACK(ccm_preferences_on_screen_change), 
							 self);
	
	screen_name = GTK_LABEL(gtk_builder_get_object(builder, "screen_name"));
	if (!screen_name) return;
	str = g_strdup_printf("<span size='large'><b>Screen %i</b></span>",
						  screen_num);
	gtk_label_set_markup(screen_name, str);
	g_free(str);

	self->priv->screen_pages[screen_num] = ccm_preferences_page_new(self,
																	screen_num);
	page = ccm_preferences_page_get_widget(self->priv->screen_pages[screen_num]);
	gtk_widget_show(page);
	gtk_notebook_append_page(notebook, page, NULL);
	g_object_unref(builder);
}

static gboolean
ccm_preferences_on_expose_event (CCMPreferences* self, GdkEventExpose* event,
								 GtkWidget* widget)
{
	GtkWidget* child = GTK_WIDGET(gtk_builder_get_object(self->priv->builder,
														 "dialog-vbox"));
	GtkNotebook* notebook = GTK_NOTEBOOK(gtk_builder_get_object(
														self->priv->builder,
														"notebook"));
	int cpt, current = gtk_notebook_get_current_page(notebook);
	cairo_t* ctx = gdk_cairo_create(widget->window);
	int width, height;
	
	gtk_window_get_size(GTK_WINDOW(widget), &width, &height);
	
    gdk_cairo_region (ctx, event->region);
	cairo_clip(ctx);
	
	cairo_set_operator (ctx, CAIRO_OPERATOR_CLEAR);
	cairo_paint(ctx);
	
	cairo_set_operator (ctx, CAIRO_OPERATOR_SOURCE);
	for (cpt = 0; cpt < self->priv->nb_screens; ++cpt)
	{
		GtkWidget* title = self->priv->screen_titles[cpt];
		cairo_pattern_t* pattern = NULL;
	
		cairo_save(ctx);
		if (cpt == current)
		{
			pattern = cairo_pattern_create_linear(width/2, 0, width/2, height);
			cairo_pattern_add_color_stop_rgba(pattern, 0,
				(double)widget->style->bg[GTK_STATE_SELECTED].red / 65535.0f, 
				(double)widget->style->bg[GTK_STATE_SELECTED].green / 65535.0f,
				(double)widget->style->bg[GTK_STATE_SELECTED].blue / 65535.0f,
				0.9f);
			cairo_pattern_add_color_stop_rgba(pattern, 
				(double)(title->allocation.height - 
						 title->allocation.y) / (double)height,
				(double)widget->style->bg[GTK_STATE_NORMAL].red / 65535.0f, 
				(double)widget->style->bg[GTK_STATE_NORMAL].green / 65535.0f,
				(double)widget->style->bg[GTK_STATE_NORMAL].blue / 65535.0f,
				0.9f);
			cairo_pattern_add_color_stop_rgba(pattern, 1,
				(double)widget->style->bg[GTK_STATE_NORMAL].red / 65535.0f, 
				(double)widget->style->bg[GTK_STATE_NORMAL].green / 65535.0f,
				(double)widget->style->bg[GTK_STATE_NORMAL].blue / 65535.0f,
				0.9f);
			cairo_set_source(ctx, pattern);
		}
		else
		{
			cairo_rectangle(ctx, title->allocation.x - 6, 0,
							title->allocation.width + 12, 
							title->allocation.height);
			cairo_clip(ctx);
			cairo_set_source_rgba(ctx,
				(double)widget->style->bg[GTK_STATE_NORMAL].red / 65535.0f, 
				(double)widget->style->bg[GTK_STATE_NORMAL].green / 65535.0f,
				(double)widget->style->bg[GTK_STATE_NORMAL].blue / 65535.0f,
				0.9f);
		}
		cairo_notebook_page_round(ctx, 0, 0, width, height, 
								  title->allocation.x, 
								  title->allocation.width,
								  title->allocation.height, 6);
		cairo_fill(ctx);
		if (pattern) cairo_pattern_destroy(pattern);
		cairo_set_source_rgba(ctx, 
				(double)widget->style->bg[GTK_STATE_SELECTED].red / 65535.0f, 
				(double)widget->style->bg[GTK_STATE_SELECTED].green / 65535.0f,
				(double)widget->style->bg[GTK_STATE_SELECTED].blue / 65535.0f,
				0.9f);
		cairo_notebook_page_round(ctx, 0, 0, width, height, 
								  title->allocation.x, 
								  title->allocation.width,
								  title->allocation.height, 6);
		cairo_stroke (ctx);
		cairo_restore(ctx);
	}
	cairo_destroy(ctx);
	
	gtk_container_propagate_expose(GTK_CONTAINER(widget), child, event);
	
	return TRUE;
}

static void
ccm_preferences_on_realize (CCMPreferences* self, GtkWidget* widget)
{
	GdkAtom atom_enable = gdk_atom_intern_static_string("_CCM_SHADOW_ENABLED");
	gulong enable = 1;
	GdkPixmap* pixmap;
	cairo_t* ctx;
	gint cpt;
	
    gdk_property_change(widget->window, atom_enable, 
                        gdk_x11_xatom_to_atom(XA_CARDINAL), 32, 
                        GDK_PROP_MODE_REPLACE, (guchar *)&enable, 1);

	gtk_window_get_size(GTK_WINDOW(widget), 
	                    &self->priv->width, &self->priv->height);

	pixmap = gdk_pixmap_new(NULL, self->priv->width, self->priv->height, 1);
	ctx = gdk_cairo_create(GDK_DRAWABLE(pixmap));

	cairo_set_operator(ctx, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_rgba(ctx, 0, 0, 0, 0);
    cairo_paint(ctx);
    
    cairo_set_operator(ctx, CAIRO_OPERATOR_OVER);
	cairo_set_source_rgba(ctx, 1, 1, 1, 1);
	
    for (cpt = 0; cpt < self->priv->nb_screens; ++cpt)
	{
		GtkWidget* title = self->priv->screen_titles[cpt];
		
		cairo_save(ctx);
		cairo_notebook_page_round(ctx, 0, 0, 
		                          self->priv->width, self->priv->height, 
								  title->allocation.x, 
								  title->allocation.width,
								  title->allocation.height, 6);
		cairo_fill(ctx);
		cairo_restore(ctx);
	}
	cairo_destroy(ctx);
	
    gdk_window_shape_combine_mask(widget->window, (GdkBitmap*)0, 0, 0);
    gdk_window_input_shape_combine_mask(widget->window, (GdkBitmap*)0, 0, 0);
    gdk_window_shape_combine_mask(widget->window, (GdkBitmap*)pixmap, 0, 0);
    gdk_window_input_shape_combine_mask(widget->window, (GdkBitmap*)pixmap, 0, 0);
	g_object_unref(pixmap);
}

static gboolean
ccm_preferences_on_configure_event (CCMPreferences* self, 
                                    GdkEventConfigure* event, 
                                    GtkWidget* widget)
{
	if ((self->priv->width != event->width) || 
		(self->priv->height != event->height))
	{
		GdkPixmap* pixmap;
		cairo_t* ctx;
		gint cpt;
		
		self->priv->width = event->width;
		self->priv->height = event->height;
		
		pixmap = gdk_pixmap_new(NULL, self->priv->width, self->priv->height, 1);
		ctx = gdk_cairo_create(GDK_DRAWABLE(pixmap));

		cairo_set_operator(ctx, CAIRO_OPERATOR_SOURCE);
		cairo_set_source_rgba(ctx, 0, 0, 0, 0);
		cairo_paint(ctx);
		
		cairo_set_operator(ctx, CAIRO_OPERATOR_OVER);
		cairo_set_source_rgba(ctx, 1, 1, 1, 1);
	
		for (cpt = 0; cpt < self->priv->nb_screens; ++cpt)
		{
			GtkWidget* title = self->priv->screen_titles[cpt];
		
			cairo_save(ctx);
			cairo_notebook_page_round(ctx, 0, 0, 
				                      self->priv->width, self->priv->height, 
									  title->allocation.x, 
									  title->allocation.width,
									  title->allocation.height, 6);
			cairo_fill(ctx);
			cairo_restore(ctx);
		}
		cairo_destroy(ctx);
	
		gdk_window_shape_combine_mask(widget->window, (GdkBitmap*)0, 0, 0);
		gdk_window_input_shape_combine_mask(widget->window, (GdkBitmap*)0, 
		                                    0, 0);
		gdk_window_shape_combine_mask(widget->window, (GdkBitmap*)pixmap, 0, 0);
		gdk_window_input_shape_combine_mask(widget->window, (GdkBitmap*)pixmap, 
		                                    0, 0);
		g_object_unref(pixmap);
	}

	return FALSE;
}

static void
ccm_preferences_on_response(CCMPreferences* self, gint response, 
                            GtkWidget* widget)
{
	if (response != GTK_RESPONSE_DELETE_EVENT)
		ccm_preferences_hide(self);
}

CCMPreferences*
ccm_preferences_new (void)
{
	CCMPreferences* self = g_object_new(CCM_TYPE_PREFERENCES, NULL);
	GdkDisplay* display = gdk_display_get_default();
	GdkColormap* colormap;
	GdkScreen* screen = gdk_screen_get_default();
	GtkWidget* shell;
	gint cpt;

	self->priv->builder = gtk_builder_new();
	if (!self->priv->builder)
	{
		g_object_unref(self);
		return NULL;
	}
	if (!gtk_builder_add_from_file(self->priv->builder, 
								   UI_DIR "/ccm-preferences.ui", NULL))
	{
		g_object_unref(self);
		return NULL;
	}
	
	self->priv->nb_screens = gdk_display_get_n_screens(display);
	
	self->priv->screen_pages = g_new0(CCMPreferencesPage*, 
									  self->priv->nb_screens);
	self->priv->screen_titles = g_new0(GtkWidget*,
									   self->priv->nb_screens);
	
	for (cpt = 0; cpt < self->priv->nb_screens; ++cpt)
		ccm_preferences_create_page(self, cpt);

	shell = GTK_WIDGET(gtk_builder_get_object(self->priv->builder, "shell"));
	colormap = gdk_screen_get_rgba_colormap (screen);
	gtk_widget_set_colormap (shell, colormap);
	gtk_window_set_keep_above(GTK_WINDOW(shell), TRUE);
	gtk_window_set_focus_on_map(GTK_WINDOW(shell), TRUE);
	
	g_signal_connect_swapped(shell, "delete-event", 
							 G_CALLBACK(gtk_true), self);
	g_signal_connect_swapped(shell, "response", 
							 G_CALLBACK(ccm_preferences_on_response), self);
	g_signal_connect_swapped(shell, "realize", 
							 G_CALLBACK(ccm_preferences_on_realize), self);
	g_signal_connect_swapped(shell, "expose-event", 
							 G_CALLBACK(ccm_preferences_on_expose_event), self);
	g_signal_connect_swapped(shell, "configure-event", 
							 G_CALLBACK(ccm_preferences_on_configure_event), 
	                         self);
	
	return self;
}

void
ccm_preferences_show(CCMPreferences* self)
{
	g_return_if_fail(self != NULL);
	
	gint cpt;
	GtkWidget* widget = GTK_WIDGET(gtk_builder_get_object(self->priv->builder,
														  "shell"));
	
	gtk_widget_show(widget);
	for (cpt = 0; cpt < self->priv->nb_screens; cpt++)
	{
		ccm_preferences_page_set_current_section(self->priv->screen_pages[cpt],
		                                         CCM_PREFERENCES_PAGE_SECTION_GENERAL);
	}
}

void
ccm_preferences_hide(CCMPreferences* self)
{
	g_return_if_fail(self != NULL);
	
	GtkWidget* widget = GTK_WIDGET(gtk_builder_get_object(self->priv->builder,
														  "shell"));
	
	gtk_widget_hide(widget);
}
