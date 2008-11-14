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

#include "ccm-config.h"

#define CCM_CONFIG_PREFIX "/apps/cairo-compmgr"

#define CCM_CONFIG_ERROR_QUARK g_quark_from_string("CCMConfigError")

enum
{
	CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

G_DEFINE_TYPE (CCMConfig, ccm_config, G_TYPE_OBJECT);

struct _CCMConfigPrivate
{
	gchar* key;
	guint id_notify;
};

#define CCM_CONFIG_GET_PRIVATE(o)  \
   ((CCMConfigPrivate*)G_TYPE_INSTANCE_GET_PRIVATE ((o), CCM_TYPE_CONFIG, CCMConfigClass))

static void
ccm_config_init (CCMConfig *self)
{
	self->priv = CCM_CONFIG_GET_PRIVATE(self);
	self->priv->key = NULL;
	self->priv->id_notify = 0;
}

static void
ccm_config_finalize (GObject *object)
{
	CCMConfig* self = CCM_CONFIG(object);
	
	if (self->priv->key) g_free(self->priv->key);
	if (self->priv->id_notify) 
		gconf_client_notify_remove(CCM_CONFIG_GET_CLASS(self)->client,
								   self->priv->id_notify);
		
	G_OBJECT_CLASS (ccm_config_parent_class)->finalize (object);
}

static void
ccm_config_class_init (CCMConfigClass *klass)
{
	GObjectClass* object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (CCMConfigPrivate));

	klass->client = gconf_client_get_default ();
	gconf_client_add_dir (klass->client, CCM_CONFIG_PREFIX,
						  GCONF_CLIENT_PRELOAD_NONE, NULL);
	
	signals[CHANGED] = g_signal_new ("changed",
									 G_OBJECT_CLASS_TYPE (object_class),
									 G_SIGNAL_RUN_LAST, 0, NULL, NULL,
									 g_cclosure_marshal_VOID__VOID,
									 G_TYPE_NONE, 0, G_TYPE_NONE);
	
	object_class->finalize = ccm_config_finalize;
}

static void
ccm_config_on_change(GConfClient *client, guint cnxn_id, GConfEntry * entry, 
		  CCMConfig * self)
{
	g_return_if_fail(self != NULL);
	
	g_signal_emit(self, signals[CHANGED], 0, NULL);
}

static gboolean
ccm_config_copy_entry(CCMConfig* self, gchar* src)
{
	g_return_val_if_fail(self != NULL, FALSE);
	g_return_val_if_fail(src != NULL, FALSE);
	
	GConfEntry* entry;
	gboolean ret = FALSE;
	
	entry = gconf_client_get_entry(CCM_CONFIG_GET_CLASS(self)->client,
								   src, NULL, TRUE, NULL);
	if (entry && 
		gconf_engine_associate_schema (CCM_CONFIG_GET_CLASS(self)->client->engine,
									   self->priv->key,
									   gconf_entry_get_schema_name (entry),
									   NULL))
	{
		gconf_client_set (CCM_CONFIG_GET_CLASS(self)->client, 
						  self->priv->key, entry->value, NULL);
		ret = TRUE;
	}
	if (entry) gconf_entry_unref(entry);
	
	return ret;
}

GQuark
ccm_config_error_quark()
{
	return CCM_CONFIG_ERROR_QUARK;
}

CCMConfig*
ccm_config_new (int screen, gchar* extension, gchar* key)
{
	g_return_val_if_fail(key != NULL, NULL);
	
	CCMConfig* self = g_object_new(CCM_TYPE_CONFIG, NULL);
	
	if (screen >= 0)
	{
		if (extension)
		{
			GConfEntry* entry;
		
			self->priv->key = g_strdup_printf("%s/screen_%i/%s/%s", 
											  CCM_CONFIG_PREFIX, 
											  screen, extension, key);
			
			entry = gconf_client_get_entry(CCM_CONFIG_GET_CLASS(self)->client,
								           self->priv->key, NULL, TRUE, NULL);
			if (!entry || !entry->value)
			{
				gchar * default_config = g_strdup_printf("%s/default/%s/%s", 
														 CCM_CONFIG_PREFIX, 
														 extension, key);
				if (!ccm_config_copy_entry(self, default_config))
				{
					g_free(default_config);
					g_object_unref(self);
					return NULL;
				}
				g_free(default_config);
			}
			if (entry) gconf_entry_unref(entry);
		}
		else
		{
			GConfEntry* entry;
			self->priv->key = g_strdup_printf("%s/screen_%i/general/%s", 
											  CCM_CONFIG_PREFIX, screen, key);
			entry = gconf_client_get_entry(CCM_CONFIG_GET_CLASS(self)->client,
								           self->priv->key, NULL, TRUE, NULL);
			if (!entry || !entry->value)
			{
				gchar * default_config = g_strdup_printf("%s/default/screen/%s", 
														 CCM_CONFIG_PREFIX, key);
				if (ccm_config_copy_entry(self, default_config))
				{
					g_free(default_config);
					g_object_unref(self);
					return NULL;
				}
				g_free(default_config);
			}
			if (entry) gconf_entry_unref(entry);
		}
	}
	else if (screen == -1)
		self->priv->key = g_strdup_printf("%s/general/%s", 
										  CCM_CONFIG_PREFIX, key);
	else
		self->priv->key = g_strdup(key);
	
	self->priv->id_notify = gconf_client_notify_add(
									CCM_CONFIG_GET_CLASS(self)->client,
									self->priv->key, 
									(GConfClientNotifyFunc)ccm_config_on_change, 
									self, NULL, NULL);

	return self;
}

gboolean
ccm_config_get_boolean(CCMConfig* self, GError** error)
{
	if (self == NULL)
	{
		if (error)
			*error = g_error_new(CCM_CONFIG_ERROR_QUARK, 
								 CCM_CONFIG_ERROR_IS_NULL,
								 "Invalid object");
		
		return FALSE;
	}
	
	return gconf_client_get_bool(CCM_CONFIG_GET_CLASS(self)->client, 
								 self->priv->key, error);
}
	
void
ccm_config_set_boolean(CCMConfig* self, gboolean value, GError** error)
{
	if (self == NULL)
	{
		if (error)
			*error = g_error_new(CCM_CONFIG_ERROR_QUARK, 
								 CCM_CONFIG_ERROR_IS_NULL,
								 "Invalid object");
	}
	
	gconf_client_set_bool(CCM_CONFIG_GET_CLASS(self)->client, self->priv->key, 
						  value, error);
}
	
gint
ccm_config_get_integer(CCMConfig* self, GError** error)
{
	if (self == NULL)
	{
		if (error)
			*error = g_error_new(CCM_CONFIG_ERROR_QUARK, 
								 CCM_CONFIG_ERROR_IS_NULL,
								 "Invalid object");
		return 0;
	}
	
	return gconf_client_get_int(CCM_CONFIG_GET_CLASS(self)->client, 
								self->priv->key, error);
}
	
void
ccm_config_set_integer(CCMConfig* self, gint value, GError** error)
{
	if (self == NULL)
	{
		if (error)
			*error = g_error_new(CCM_CONFIG_ERROR_QUARK, 
								 CCM_CONFIG_ERROR_IS_NULL,
								 "Invalid object");
	}
	
	gconf_client_set_int(CCM_CONFIG_GET_CLASS(self)->client, 
						 self->priv->key, value, error);
}
	
gfloat
ccm_config_get_float(CCMConfig* self, GError** error)
{
	if (self == NULL)
	{
		if (error)
			*error = g_error_new(CCM_CONFIG_ERROR_QUARK, 
								 CCM_CONFIG_ERROR_IS_NULL,
								 "Invalid object");
		return 0.0f;
	}
	
	return gconf_client_get_float(CCM_CONFIG_GET_CLASS(self)->client, 
								  self->priv->key, error);
}
	
void
ccm_config_set_float(CCMConfig* self, gfloat value, GError** error)
{
	if (self == NULL)
	{
		if (error)
			*error = g_error_new(CCM_CONFIG_ERROR_QUARK, 
								 CCM_CONFIG_ERROR_IS_NULL,
								 "Invalid object");
	}
	
	gconf_client_set_float(CCM_CONFIG_GET_CLASS(self)->client, 
						   self->priv->key, value, error);
}
	
gchar *
ccm_config_get_string(CCMConfig* self, GError** error)
{
	if (self == NULL)
	{
		if (error)
			*error = g_error_new(CCM_CONFIG_ERROR_QUARK, 
								 CCM_CONFIG_ERROR_IS_NULL,
								 "Invalid object");
		return NULL;
	}
		
	return gconf_client_get_string(CCM_CONFIG_GET_CLASS(self)->client,
								   self->priv->key, error);
}
	
void
ccm_config_set_string(CCMConfig* self, gchar * value, GError** error)
{
	if (self == NULL)
	{
		if (error)
			*error = g_error_new(CCM_CONFIG_ERROR_QUARK, 
								 CCM_CONFIG_ERROR_IS_NULL,
								 "Invalid object");
	}
		
	if (value)
		gconf_client_set_string(CCM_CONFIG_GET_CLASS(self)->client, 
								self->priv->key, value, error);
}
	
GSList*
ccm_config_get_string_list(CCMConfig* self, GError** error)
{
	if (self == NULL)
	{
		if (error)
			*error = g_error_new(CCM_CONFIG_ERROR_QUARK, 
								 CCM_CONFIG_ERROR_IS_NULL,
								 "Invalid object");
		return NULL;
	}
		
	return gconf_client_get_list(CCM_CONFIG_GET_CLASS(self)->client, 
								 self->priv->key, GCONF_VALUE_STRING, error);
}
	
void
ccm_config_set_string_list(CCMConfig* self, GSList * value, GError** error)
{
	if (self == NULL)
	{
		if (error)
			*error = g_error_new(CCM_CONFIG_ERROR_QUARK, 
								 CCM_CONFIG_ERROR_IS_NULL,
								 "Invalid object");
	}
		
	gconf_client_set_list(CCM_CONFIG_GET_CLASS(self)->client, 
						  self->priv->key, GCONF_VALUE_STRING, 
						  (GSList *)value, error);
}

GSList*
ccm_config_get_integer_list(CCMConfig* self, GError** error)
{
	if (self == NULL)
	{
		if (error)
			*error = g_error_new(CCM_CONFIG_ERROR_QUARK, 
								 CCM_CONFIG_ERROR_IS_NULL,
								 "Invalid object");
		return NULL;
	}
		
	return gconf_client_get_list(CCM_CONFIG_GET_CLASS(self)->client, 
								 self->priv->key, GCONF_VALUE_INT, error);
}
	
void
ccm_config_set_integer_list(CCMConfig* self, GSList * value, GError** error)
{
	if (self == NULL)
	{
		if (error)
			*error = g_error_new(CCM_CONFIG_ERROR_QUARK, 
								 CCM_CONFIG_ERROR_IS_NULL,
								 "Invalid object");
	}
		
	gconf_client_set_list(CCM_CONFIG_GET_CLASS(self)->client, 
						  self->priv->key, GCONF_VALUE_INT, 
						  (GSList *)value, error);
}

GdkColor*
ccm_config_get_color(CCMConfig* self, GError** error)
{
	if (self == NULL)
	{
		if (error)
			*error = g_error_new(CCM_CONFIG_ERROR_QUARK, 
								 CCM_CONFIG_ERROR_IS_NULL,
								 "Invalid object");
		return NULL;
	}
	
	gchar* value = ccm_config_get_string (self, error);
	GdkColor* color = NULL;
	
	if (value && value[0] == '#')
	{
	    gint c[3];

		if (sscanf(value, "#%2x%2x%2x", &c[0], &c[1], &c[2]) == 3)
		{
			color = g_new0(GdkColor, 1);

			color->red = c[0] << 8 | c[0];
	        color->green = c[1] << 8 | c[1];
	        color->blue = c[2] << 8 | c[2];
		}
	}
	if (value) g_free(value);
	
	return color;
}
