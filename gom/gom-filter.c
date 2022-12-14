/* gom-filter.c
 *
 * Copyright (C) 2011 Christian Hergert <chris@dronelabs.com>
 *
 * This file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdarg.h>

#include "gom-filter.h"
#include "gom-resource.h"

G_DEFINE_TYPE(GomFilter, gom_filter, G_TYPE_INITIALLY_UNOWNED)

struct _GomFilterPrivate
{
   GomFilterMode mode;

   gchar *sql;
   GArray *values;

   GValue value;
   GParamSpec *pspec;
   GType type;

   GQueue *subfilters;
};

enum
{
   PROP_0,
   PROP_MODE,
   PROP_SQL,
   LAST_PROP
};

static GParamSpec  *gParamSpecs[LAST_PROP];
static const gchar *gOperators[] = {
   NULL,
   NULL,
   "OR",
   "AND",
   "==",
   "!=",
   ">",
   ">=",
   "<",
   "<=",
   "LIKE",
   "GLOB",
   "IS NULL",
   "IS NOT NULL"
};

/**
 * gom_filter_new_sql:
 * @sql: (in): A UTF-8 string.
 * @values: (in) (transfer none) (element-type GValue): An array of values.
 *
 * Creates a new #GomFilter using the SQL and values provided.
 *
 * Returns: (transfer full): A new #GomFilter.
 */
GomFilter *
gom_filter_new_sql (const gchar  *sql,
                    const GArray *values)
{
   GomFilter *filter;

   g_return_val_if_fail (sql != NULL, NULL);

   filter = g_object_new (GOM_TYPE_FILTER,
                          "mode", GOM_FILTER_SQL,
                          "sql", sql,
                          NULL);

   if (values != NULL) {
      guint i;

      filter->priv->values = g_array_sized_new (FALSE, FALSE, sizeof(GValue), values->len);
      g_array_set_clear_func (filter->priv->values, (GDestroyNotify)g_value_unset);

      for (i = 0; i < values->len; i++) {
         GValue copy = { 0 };
         const GValue *src;

         src = &g_array_index (values, GValue, i);
         g_value_init (&copy, G_VALUE_TYPE (src));
         g_value_copy (src, &copy);
         g_array_append_val (filter->priv->values, copy);
      }
   }

   return filter;
}

static GomFilter *
gom_filter_new_for_param (GType          resource_type,
                          const gchar   *property_name,
                          GomFilterMode  mode,
                          const GValue  *value)
{
   GObjectClass *klass;
   GParamSpec *pspec;
   GomFilter *filter;

   g_return_val_if_fail(g_type_is_a(resource_type, GOM_TYPE_RESOURCE), NULL);
   if (mode != GOM_FILTER_IS_NULL && mode != GOM_FILTER_IS_NOT_NULL) {
     g_return_val_if_fail(value != NULL, NULL);
     g_return_val_if_fail(G_VALUE_TYPE(value), NULL);
   }

   klass = g_type_class_ref(resource_type);
   pspec = g_object_class_find_property(klass, property_name);
   g_type_class_unref(klass);

   if (!pspec) {
      g_warning("No such property %s::%s",
                g_type_name(resource_type), property_name);
      return NULL;
   }

   filter = g_object_new(GOM_TYPE_FILTER,
                         "mode", mode,
                         NULL);
   filter->priv->pspec = g_param_spec_ref(pspec);
   filter->priv->type = resource_type;

   if (value)
     {
       g_value_init(&filter->priv->value, G_VALUE_TYPE(value));
       g_value_copy(value, &filter->priv->value);
     }

   return filter;
}

static GomFilter *
gom_filter_new_for_subfilters_full (GomFilterMode  mode,
                                    GomFilter     *first,
                                    va_list        filters)
{
   GomFilter *filter, *f;

   g_return_val_if_fail(GOM_IS_FILTER(first), NULL);

   filter = g_object_new(GOM_TYPE_FILTER, "mode", mode, NULL);
   filter->priv->subfilters = g_queue_new();
   g_queue_push_tail(filter->priv->subfilters, g_object_ref(first));

   f = va_arg(filters, GomFilter*);

   while (f != NULL) {
      g_return_val_if_fail(GOM_IS_FILTER(f), NULL);
      g_queue_push_tail(filter->priv->subfilters, g_object_ref(f));

      f = va_arg(filters, GomFilter*);
   }

   return filter;
}

static GomFilter *
gom_filter_new_for_subfilters_fullv (GomFilterMode   mode,
                                     GomFilter     **filter_array)
{
   GomFilter *filter, *f;
   guint i;

   filter = g_object_new(GOM_TYPE_FILTER, "mode", mode, NULL);
   filter->priv->subfilters = g_queue_new();

   i = 0;
   f = filter_array[i];

   while (f != NULL) {
      g_return_val_if_fail(GOM_IS_FILTER(f), NULL);
      g_queue_push_tail(filter->priv->subfilters, g_object_ref(f));

      i += 1;
      f = filter_array[i];
   }

   return filter;
}

GomFilter *
gom_filter_new_like (GType         resource_type,
                     const gchar  *property_name,
                     const GValue *value)
{
  return gom_filter_new_for_param(resource_type, property_name, GOM_FILTER_LIKE, value);
}

GomFilter *
gom_filter_new_glob (GType         resource_type,
                     const gchar  *property_name,
                     const GValue *value)
{
  return gom_filter_new_for_param(resource_type, property_name, GOM_FILTER_GLOB, value);
}

GomFilter *
gom_filter_new_eq (GType         resource_type,
                   const gchar  *property_name,
                   const GValue *value)
{
   return gom_filter_new_for_param(resource_type, property_name, GOM_FILTER_EQ, value);
}

GomFilter *
gom_filter_new_neq (GType         resource_type,
                    const gchar  *property_name,
                    const GValue *value)
{
   return gom_filter_new_for_param(resource_type, property_name, GOM_FILTER_NEQ, value);
}

GomFilter *
gom_filter_new_gt (GType         resource_type,
                   const gchar  *property_name,
                   const GValue *value)
{
   return gom_filter_new_for_param(resource_type, property_name, GOM_FILTER_GT, value);
}

GomFilter *
gom_filter_new_gte (GType         resource_type,
                    const gchar  *property_name,
                    const GValue *value)
{
   return gom_filter_new_for_param(resource_type, property_name, GOM_FILTER_GTE, value);
}

GomFilter *
gom_filter_new_lt (GType         resource_type,
                   const gchar  *property_name,
                   const GValue *value)
{
   return gom_filter_new_for_param(resource_type, property_name, GOM_FILTER_LT, value);
}

GomFilter *
gom_filter_new_lte (GType         resource_type,
                    const gchar  *property_name,
                    const GValue *value)
{
   return gom_filter_new_for_param(resource_type, property_name, GOM_FILTER_LTE, value);
}

GomFilter   *
gom_filter_new_is_null (GType          resource_type,
                        const gchar   *property_name)
{
   return gom_filter_new_for_param(resource_type, property_name, GOM_FILTER_IS_NULL, NULL);
}

GomFilter   *
gom_filter_new_is_not_null (GType          resource_type,
                            const gchar   *property_name)
{
   return gom_filter_new_for_param(resource_type, property_name, GOM_FILTER_IS_NOT_NULL, NULL);
}

static GomFilterMode
gom_filter_get_mode (GomFilter *filter)
{
   g_return_val_if_fail(GOM_IS_FILTER(filter), 0);
   return filter->priv->mode;
}

static void
gom_filter_set_mode (GomFilter     *filter,
                     GomFilterMode  mode)
{
   g_return_if_fail(GOM_IS_FILTER(filter));
   filter->priv->mode = mode;
}

static gchar *
get_table (GParamSpec *pspec,
           GType       type,
           GHashTable *table_map)
{
   GomResourceClass *klass;
   gchar *table;
   gchar *key;

   g_return_val_if_fail(g_type_is_a(type, GOM_TYPE_RESOURCE), NULL);

   klass = g_type_class_ref(pspec->owner_type);
   key = g_strdup_printf("%s.%s", g_type_name(type), klass->table);
   if (table_map && (table = g_hash_table_lookup(table_map, key))) {
      table = g_strdup(table);
   } else {
      table = g_strdup(klass->table);
   }
   g_free(key);
   g_type_class_unref(klass);

   return table;
}

/**
 * gom_filter_new_and: (constructor)
 * @left: (in): A #GomFilter.
 * @right: (in): A #GomFilter.
 *
 * Creates a new filter that requires that both left and right filters
 * equate to #TRUE.
 *
 * Returns: (transfer full): A #GomFilter.
 */
GomFilter *
gom_filter_new_and (GomFilter *left,
                    GomFilter *right)
{
   GomFilter *filter_array[3] = { left, right, NULL };

   return gom_filter_new_for_subfilters_fullv(GOM_FILTER_AND, filter_array);
}

/**
 * gom_filter_new_and_full: (constructor)
 * @first: (in): A #GomFilter.
 * @...: (in): A %NULL-terminated list of #GomFilter.
 *
 * Creates a new filter that requires that all filters passed as arguments
 * equate to #TRUE.
 *
 * Returns: (transfer full): A #GomFilter.
 */
GomFilter *
gom_filter_new_and_full (GomFilter *first,
                         ...)
{
   GomFilter *filter;
   va_list args;

   va_start(args, first);
   filter = gom_filter_new_for_subfilters_full(GOM_FILTER_AND, first, args);
   va_end(args);

   return filter;
}

/**
 * gom_filter_new_and_fullv: (constructor)
 * @filter_array: (in): A %NULL-terminated array of #GomFilter.
 *
 * Creates a new filter that requires that all filters passed as arguments
 * equate to #TRUE.
 *
 * Returns: (transfer full): A #GomFilter.
 */
GomFilter *
gom_filter_new_and_fullv (GomFilter **filter_array)
{
   return gom_filter_new_for_subfilters_fullv(GOM_FILTER_AND, filter_array);
}

/**
 * gom_filter_new_or: (constructor)
 * @left: (in): A #GomFilter.
 * @right: (in): A #GomFilter.
 *
 * Creates a new filter that requires either the left or right filters
 * equate to #TRUE.
 *
 * Returns: (transfer full): A #GomFilter.
 */
GomFilter *
gom_filter_new_or (GomFilter *left,
                   GomFilter *right)
{
   GomFilter *filter_array[3] = { left, right, NULL };

   return gom_filter_new_for_subfilters_fullv(GOM_FILTER_OR, filter_array);
}

/**
 * gom_filter_new_or_full: (constructor)
 * @first: (in): A #GomFilter.
 * @...: (in): A %NULL-terminated list of #GomFilter.
 *
 * Creates a new filter that requires either of the filters passed as
 * arguments equate to #TRUE.
 *
 * Returns: (transfer full): A #GomFilter.
 */
GomFilter *
gom_filter_new_or_full (GomFilter *first,
                        ...)
{
   GomFilter *filter;
   va_list args;

   va_start(args, first);
   filter = gom_filter_new_for_subfilters_full(GOM_FILTER_OR, first, args);
   va_end(args);

   return filter;
}

/**
 * gom_filter_new_or_fullv: (constructor)
 * @filter_array: (in): A %NULL-terminated array of #GomFilter.
 *
 * Creates a new filter that requires either of the filters passed as
 * arguments equate to #TRUE.
 *
 * Returns: (transfer full): A #GomFilter.
 */
GomFilter *
gom_filter_new_or_fullv (GomFilter **filter_array)
{
   return gom_filter_new_for_subfilters_fullv(GOM_FILTER_OR, filter_array);
}

gchar *
gom_filter_get_sql (GomFilter  *filter,
                    GHashTable *table_map)
{
   GomFilterPrivate *priv;
   gchar *table;
   GomFilter *f;
   gchar **sqls;
   gint i, len;
   gchar *sep, *s, *ret;

   g_return_val_if_fail(GOM_IS_FILTER(filter), NULL);

   priv = filter->priv;

   switch (priv->mode) {
   case GOM_FILTER_SQL:
      return g_strdup(priv->sql);
   case GOM_FILTER_EQ:
   case GOM_FILTER_NEQ:
   case GOM_FILTER_GT:
   case GOM_FILTER_GTE:
   case GOM_FILTER_LT:
   case GOM_FILTER_LTE:
   case GOM_FILTER_LIKE:
   case GOM_FILTER_GLOB:
      table = get_table(priv->pspec, priv->type, table_map);
      ret = g_strdup_printf("'%s'.'%s' %s ?",
                            table,
                            priv->pspec->name,
                            gOperators[priv->mode]);
      g_free(table);
      return ret;
   case GOM_FILTER_IS_NULL:
   case GOM_FILTER_IS_NOT_NULL:
      table = get_table(priv->pspec, priv->type, table_map);
      ret = g_strdup_printf("'%s'.'%s' %s",
                            table,
                            priv->pspec->name,
                            gOperators[priv->mode]);
      g_free(table);
      return ret;
   case GOM_FILTER_AND:
   case GOM_FILTER_OR:
      len = g_queue_get_length(priv->subfilters);
      sqls = g_new(gchar *, 1 + len);

      for (i = 0; i < len; i++) {
         f = g_queue_peek_nth(priv->subfilters, i);
         s = gom_filter_get_sql(f, table_map);

         if ((f->priv->mode == GOM_FILTER_AND) || (f->priv->mode == GOM_FILTER_OR)) {
            gchar *tmp = g_strdup_printf("(%s)", s);
            g_free(s);
            s = tmp;
         }

         sqls[i] = s;
      }
      sqls[i] = NULL;

      sep = g_strdup_printf(" %s ", gOperators[priv->mode]);
      ret = g_strjoinv(sep, sqls);

      g_free(sep);
      g_strfreev(sqls);

      return ret;
   default:
      break;
   }

   g_assert_not_reached();

   return NULL;
}

static void
gom_filter_set_sql (GomFilter   *filter,
                    const gchar *sql)
{
   g_return_if_fail(GOM_IS_FILTER(filter));
   filter->priv->sql = g_strdup(sql);
}

static void
join_value_array (GArray *dst,
                  GArray *src)
{
   g_return_if_fail(dst);
   g_return_if_fail(src);

   g_array_append_vals(dst, src->data, src->len);
   g_array_set_clear_func(src, NULL);
   g_array_unref(src);
}

/**
 * gom_filter_get_values:
 * @filter: (in): A #GomFilter.
 *
 * Fetches the list of values that should be applied in order when building
 * the #GomCommand.
 *
 * Returns: (transfer full) (element-type GValue): An array of values for the SQL.
 */
GArray *
gom_filter_get_values (GomFilter *filter)
{
   GomFilterPrivate *priv;
   GomFilter *f;
   GArray *tmp;
   GArray *va;
   gint i, len;

   g_return_val_if_fail(GOM_IS_FILTER(filter), NULL);

   priv = filter->priv;

   switch (priv->mode) {
   case GOM_FILTER_SQL:
      if (priv->values) {
         return g_array_ref(priv->values);
      }
      va = g_array_new(FALSE, FALSE, sizeof(GValue));
      g_array_set_clear_func(va, (GDestroyNotify) g_value_unset);
      return va;
   case GOM_FILTER_EQ:
   case GOM_FILTER_NEQ:
   case GOM_FILTER_GT:
   case GOM_FILTER_GTE:
   case GOM_FILTER_LT:
   case GOM_FILTER_LTE:
   case GOM_FILTER_LIKE:
   case GOM_FILTER_GLOB: {
      GValue v = { 0 };
      g_value_init(&v, G_VALUE_TYPE(&priv->value));
      g_value_copy(&priv->value, &v);
      va = g_array_sized_new(FALSE, FALSE, sizeof(GValue), 1);
      g_array_set_clear_func(va, (GDestroyNotify) g_value_unset);
      g_array_append_val(va, v);
      return va;
   }
   case GOM_FILTER_IS_NULL:
   case GOM_FILTER_IS_NOT_NULL:
      va = g_array_new(FALSE, FALSE, sizeof(GValue));
      g_array_set_clear_func(va, (GDestroyNotify) g_value_unset);
      return va;
   case GOM_FILTER_AND:
   case GOM_FILTER_OR:
      len = g_queue_get_length(priv->subfilters);
      va = g_array_new(FALSE, FALSE, sizeof(GValue));

      for (i = 0; i < len; i++) {
         f = g_queue_peek_nth(priv->subfilters, i);
         tmp = gom_filter_get_values(f);
         join_value_array(va, tmp);
      }

      return va;
   default:
      break;
   }

   g_assert_not_reached();
   return NULL;
}

/**
 * gom_filter_finalize:
 * @object: (in): A #GomFilter.
 *
 * Finalizer for a #GomFilter instance.  Frees any resources held by
 * the instance.
 */
static void
gom_filter_finalize (GObject *object)
{
   GomFilterPrivate *priv = GOM_FILTER(object)->priv;

   g_free(priv->sql);

   if (priv->pspec) {
      g_param_spec_unref(priv->pspec);
   }

   if (G_VALUE_TYPE(&priv->value)) {
      g_value_unset(&priv->value);
   }

   g_clear_pointer (&priv->values, g_array_unref);

   if (priv->subfilters != NULL)
      g_queue_free_full(priv->subfilters, g_object_unref);

   G_OBJECT_CLASS(gom_filter_parent_class)->finalize(object);
}

/**
 * gom_filter_get_property:
 * @object: (in): A #GObject.
 * @prop_id: (in): The property identifier.
 * @value: (out): The given property.
 * @pspec: (in): A #ParamSpec.
 *
 * Get a given #GObject property.
 */
static void
gom_filter_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
   GomFilter *filter = GOM_FILTER(object);

   switch (prop_id) {
   case PROP_MODE:
      g_value_set_enum(value, gom_filter_get_mode(filter));
      break;
   default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
   }
}

/**
 * gom_filter_set_property:
 * @object: (in): A #GObject.
 * @prop_id: (in): The property identifier.
 * @value: (in): The given property.
 * @pspec: (in): A #ParamSpec.
 *
 * Set a given #GObject property.
 */
static void
gom_filter_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
   GomFilter *filter = GOM_FILTER(object);

   switch (prop_id) {
   case PROP_MODE:
      gom_filter_set_mode(filter, g_value_get_enum(value));
      break;
   case PROP_SQL:
      gom_filter_set_sql(filter, g_value_get_string(value));
      break;
   default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
   }
}

/**
 * gom_filter_class_init:
 * @klass: (in): A #GomFilterClass.
 *
 * Initializes the #GomFilterClass and prepares the vtable.
 */
static void
gom_filter_class_init (GomFilterClass *klass)
{
   GObjectClass *object_class;

   object_class = G_OBJECT_CLASS(klass);
   object_class->finalize = gom_filter_finalize;
   object_class->get_property = gom_filter_get_property;
   object_class->set_property = gom_filter_set_property;
   g_type_class_add_private(object_class, sizeof(GomFilterPrivate));

   gParamSpecs[PROP_MODE] =
      g_param_spec_enum("mode",
                        "Mode",
                        "The mode of the filter.",
                        GOM_TYPE_FILTER_MODE,
                        GOM_FILTER_SQL,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
   g_object_class_install_property(object_class, PROP_MODE,
                                   gParamSpecs[PROP_MODE]);

   gParamSpecs[PROP_SQL] =
      g_param_spec_string("sql",
                          "SQL",
                          "The SQL for the filter.",
                          NULL,
                          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);
   g_object_class_install_property(object_class, PROP_SQL,
                                   gParamSpecs[PROP_SQL]);
}

/**
 * gom_filter_init:
 * @filter: (in): A #GomFilter.
 *
 * Initializes the newly created #GomFilter instance.
 */
static void
gom_filter_init (GomFilter *filter)
{
   filter->priv =
      G_TYPE_INSTANCE_GET_PRIVATE(filter,
                                  GOM_TYPE_FILTER,
                                  GomFilterPrivate);
   filter->priv->mode = GOM_FILTER_SQL;
}

GType
gom_filter_mode_get_type (void)
{
   static GType g_type = 0;
   static gsize initialized = FALSE;
   static const GEnumValue values[] = {
      { GOM_FILTER_SQL, "GOM_FILTER_SQL", "SQL" },
      { GOM_FILTER_OR,  "GOM_FILTER_OR",  "OR" },
      { GOM_FILTER_AND, "GOM_FILTER_AND", "AND" },
      { GOM_FILTER_EQ,  "GOM_FILTER_EQ",  "EQ" },
      { GOM_FILTER_NEQ, "GOM_FILTER_NEQ", "NEQ" },
      { GOM_FILTER_GT,  "GOM_FILTER_GT",  "GT" },
      { GOM_FILTER_GTE, "GOM_FILTER_GTE", "GTE" },
      { GOM_FILTER_LT,  "GOM_FILTER_LT",  "LT" },
      { GOM_FILTER_LTE, "GOM_FILTER_LTE", "LTE" },
      { GOM_FILTER_LIKE, "GOM_FILTER_LIKE", "LIKE" },
      { GOM_FILTER_GLOB, "GOM_FILTER_GLOB", "GLOB" },
      { GOM_FILTER_IS_NULL, "GOM_FILTER_IS_NULL", "IS_NULL" },
      { GOM_FILTER_IS_NOT_NULL, "GOM_FILTER_IS_NOT_NULL", "IS_NOT_NULL" },
      { 0 }
   };

   if (g_once_init_enter(&initialized)) {
      g_type = g_enum_register_static("GomFilterMode", values);
      g_once_init_leave(&initialized, TRUE);
   }

   return g_type;
}
