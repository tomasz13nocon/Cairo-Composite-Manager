/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * cairo-compmgr
 * Copyright (C) Nicolas Bruguier 2007-2010 <gandalfn@club-internet.fr>
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

#include "ccm-screen-plugin.h"

static void
ccm_screen_plugin_base_init (gpointer g_class)
{
    static gboolean initialized = FALSE;

    if (!initialized)
    {
        initialized = TRUE;
    }
}

GType
ccm_screen_plugin_get_type (void)
{
    static GType ccm_screen_plugin_type = 0;

    if (!ccm_screen_plugin_type)
    {
        const GTypeInfo ccm_screen_plugin_info = {
            sizeof (CCMScreenPluginClass),
            ccm_screen_plugin_base_init,
            NULL
        };

        ccm_screen_plugin_type =
            g_type_register_static (G_TYPE_INTERFACE, "CCMScreenPlugin",
                                    &ccm_screen_plugin_info, 0);
    }

    return ccm_screen_plugin_type;
}

CCMScreenPlugin *
_ccm_screen_plugin_get_root (CCMScreenPlugin * self)
{
    g_return_val_if_fail (CCM_IS_SCREEN_PLUGIN (self), NULL);

    CCMScreenPlugin *plugin;

    for (plugin = self; CCM_IS_PLUGIN (plugin);
         plugin = CCM_SCREEN_PLUGIN_PARENT (plugin));

    return plugin;
}

void
ccm_screen_plugin_load_options (CCMScreenPlugin * self, CCMScreen * screen)
{
    g_return_if_fail (CCM_IS_SCREEN_PLUGIN (self));
    g_return_if_fail (screen != NULL);

    CCMScreenPlugin *plugin;

    for (plugin = self; CCM_IS_PLUGIN (plugin);
         plugin = CCM_SCREEN_PLUGIN_PARENT (plugin))
    {
        if (CCM_SCREEN_PLUGIN_GET_INTERFACE (plugin)->load_options)
            break;
    }

    if (CCM_SCREEN_PLUGIN_GET_INTERFACE (plugin)->load_options)
    {
        if (!_ccm_plugin_method_locked ((GObject *) plugin,
                                        CCM_SCREEN_PLUGIN_GET_INTERFACE (plugin)->load_options))
            CCM_SCREEN_PLUGIN_GET_INTERFACE (plugin)->load_options (plugin,
                                                                    screen);
    }
}

gboolean
ccm_screen_plugin_paint (CCMScreenPlugin * self, CCMScreen * screen,
                         cairo_t * ctx)
{
    g_return_val_if_fail (CCM_IS_SCREEN_PLUGIN (self), FALSE);
    g_return_val_if_fail (screen != NULL, FALSE);
    g_return_val_if_fail (ctx != NULL, FALSE);

    CCMScreenPlugin *plugin;

    for (plugin = self; CCM_IS_PLUGIN (plugin);
         plugin = CCM_SCREEN_PLUGIN_PARENT (plugin))
    {
        if (CCM_SCREEN_PLUGIN_GET_INTERFACE (plugin)->paint)
            break;
    }

    if (CCM_SCREEN_PLUGIN_GET_INTERFACE (plugin)->paint)
    {
        if (!_ccm_plugin_method_locked ((GObject *) plugin,
                                        CCM_SCREEN_PLUGIN_GET_INTERFACE (plugin)->paint))
            return CCM_SCREEN_PLUGIN_GET_INTERFACE (plugin)->paint (plugin,
                                                                    screen,
                                                                    ctx);
    }
    return FALSE;
}

gboolean
ccm_screen_plugin_add_window (CCMScreenPlugin * self, CCMScreen * screen,
                              CCMWindow * window)
{
    g_return_val_if_fail (CCM_IS_SCREEN_PLUGIN (self), FALSE);
    g_return_val_if_fail (screen != NULL, FALSE);
    g_return_val_if_fail (window != NULL, FALSE);

    CCMScreenPlugin *plugin;

    for (plugin = self; CCM_IS_PLUGIN (plugin);
         plugin = CCM_SCREEN_PLUGIN_PARENT (plugin))
    {
        if (CCM_SCREEN_PLUGIN_GET_INTERFACE (plugin)->add_window)
            break;
    }

    if (CCM_SCREEN_PLUGIN_GET_INTERFACE (plugin)->add_window)
    {
        if (!_ccm_plugin_method_locked ((GObject *) plugin,
                                        CCM_SCREEN_PLUGIN_GET_INTERFACE (plugin)->add_window))
            return CCM_SCREEN_PLUGIN_GET_INTERFACE (plugin)->add_window (plugin,
                                                                         screen,
                                                                         window);
    }

    return FALSE;
}

void
ccm_screen_plugin_remove_window (CCMScreenPlugin * self, CCMScreen * screen,
                                 CCMWindow * window)
{
    g_return_if_fail (CCM_IS_SCREEN_PLUGIN (self));
    g_return_if_fail (screen != NULL);
    g_return_if_fail (window != NULL);

    CCMScreenPlugin *plugin;

    for (plugin = self; CCM_IS_PLUGIN (plugin);
         plugin = CCM_SCREEN_PLUGIN_PARENT (plugin))
    {
        if (CCM_SCREEN_PLUGIN_GET_INTERFACE (plugin)->remove_window)
            break;
    }

    if (CCM_SCREEN_PLUGIN_GET_INTERFACE (plugin)->remove_window)
    {
        if (!_ccm_plugin_method_locked ((GObject *) plugin,
                                        CCM_SCREEN_PLUGIN_GET_INTERFACE (plugin)->remove_window))
            CCM_SCREEN_PLUGIN_GET_INTERFACE (plugin)->remove_window (plugin,
                                                                     screen,
                                                                     window);
    }
}

void
ccm_screen_plugin_damage (CCMScreenPlugin * self, CCMScreen * screen,
                          CCMRegion * area, CCMWindow * window)
{
    g_return_if_fail (CCM_IS_SCREEN_PLUGIN (self));
    g_return_if_fail (screen != NULL);
    g_return_if_fail (area != NULL);
    g_return_if_fail (window != NULL);

    CCMScreenPlugin *plugin;

    for (plugin = self; CCM_IS_PLUGIN (plugin);
         plugin = CCM_SCREEN_PLUGIN_PARENT (plugin))
    {
        if (CCM_SCREEN_PLUGIN_GET_INTERFACE (plugin)->damage)
            break;
    }

    if (CCM_SCREEN_PLUGIN_GET_INTERFACE (plugin)->damage)
    {
        if (!_ccm_plugin_method_locked ((GObject *) plugin,
                                        CCM_SCREEN_PLUGIN_GET_INTERFACE (plugin)->damage))
            CCM_SCREEN_PLUGIN_GET_INTERFACE (plugin)->damage (plugin, screen,
                                                              area, window);
    }
}

void
ccm_screen_plugin_on_cursor_move (CCMScreenPlugin * self, CCMScreen * screen,
                                  int x, int y)
{
    g_return_if_fail (CCM_IS_SCREEN_PLUGIN (self));
    g_return_if_fail (screen != NULL);

    CCMScreenPlugin *plugin;

    for (plugin = self; CCM_IS_PLUGIN (plugin);
         plugin = CCM_SCREEN_PLUGIN_PARENT (plugin))
    {
        if (CCM_SCREEN_PLUGIN_GET_INTERFACE (plugin)->on_cursor_move)
            break;
    }

    if (CCM_SCREEN_PLUGIN_GET_INTERFACE (plugin)->on_cursor_move)
    {
        if (!_ccm_plugin_method_locked ((GObject *) plugin,
                                        CCM_SCREEN_PLUGIN_GET_INTERFACE (plugin)->on_cursor_move))
            CCM_SCREEN_PLUGIN_GET_INTERFACE (plugin)->on_cursor_move (plugin,
                                                                      screen, x,
                                                                      y);
    }
}

void
ccm_screen_plugin_paint_cursor (CCMScreenPlugin * self, CCMScreen * screen,
                                cairo_t * ctx, int x, int y)
{
    g_return_if_fail (CCM_IS_SCREEN_PLUGIN (self));
    g_return_if_fail (screen != NULL);

    CCMScreenPlugin *plugin;

    for (plugin = self; CCM_IS_PLUGIN (plugin);
         plugin = CCM_SCREEN_PLUGIN_PARENT (plugin))
    {
        if (CCM_SCREEN_PLUGIN_GET_INTERFACE (plugin)->paint_cursor)
            break;
    }

    if (CCM_SCREEN_PLUGIN_GET_INTERFACE (plugin)->paint_cursor)
    {
        if (!_ccm_plugin_method_locked ((GObject *) plugin,
                                        CCM_SCREEN_PLUGIN_GET_INTERFACE (plugin)->paint_cursor))
            CCM_SCREEN_PLUGIN_GET_INTERFACE (plugin)->paint_cursor (plugin,
                                                                    screen, ctx,
                                                                    x, y);
    }
}
