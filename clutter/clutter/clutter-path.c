/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Matthew Allum  <mallum@openedhand.com>
 *
 * Copyright (C) 2008 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * ClutterPath:
 * 
 * An object describing a path with straight lines and bezier curves.
 *
 * A #ClutterPath contains a description of a path consisting of
 * straight lines and bezier curves.
 *
 * The path consists of a series of nodes. Each node is one of the
 * following four types:
 *
 *  - %CLUTTER_PATH_MOVE_TO, changes the position of the path to the
 *  given pair of coordinates. This is usually used as the first node
 *  of a path to mark the start position. If it is used in the middle
 *  of a path then the path will be disjoint and the actor will appear
 *  to jump to the new position when animated.
 *  - %CLUTTER_PATH_LINE_TO, creates a straight line from the previous
 *  point to the given point.
 *  - %CLUTTER_PATH_CURVE_TO, creates a bezier curve. The end of the
 *  last node is used as the first control point and the three
 *  subsequent coordinates given in the node as used as the other three.
 *  - %CLUTTER_PATH_CLOSE, creates a straight line from the last node to
 *  the last %CLUTTER_PATH_MOVE_TO node. This can be used to close a
 *  path so that it will appear as a loop when animated.
 *
 * The first three types have the corresponding relative versions
 * %CLUTTER_PATH_REL_MOVE_TO, %CLUTTER_PATH_REL_LINE_TO and
 * %CLUTTER_PATH_REL_CURVE_TO. These are exactly the same except the
 * coordinates are given relative to the previous node instead of as
 * direct screen positions.
 *
 * You can build a path using the node adding functions such as
 * [method@Path.add_line_to]. Alternatively the path can be described
 * in a string using a subset of the SVG path syntax. See
 * [method@Path.add_string] for details.
 */

#include "clutter-build-config.h"

#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <glib-object.h>

#include "clutter-path.h"
#include "clutter-types.h"
#include "clutter-bezier.h"
#include "clutter-private.h"

#define CLUTTER_PATH_NODE_TYPE_IS_VALID(t) \
  ((((t) & ~CLUTTER_PATH_RELATIVE) >= CLUTTER_PATH_MOVE_TO      \
    && ((t) & ~CLUTTER_PATH_RELATIVE) <= CLUTTER_PATH_CURVE_TO) \
   || (t) == CLUTTER_PATH_CLOSE)

enum
{
  PROP_0,

  PROP_DESCRIPTION,
  PROP_LENGTH,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

typedef struct _ClutterPathNodeFull ClutterPathNodeFull;

struct _ClutterPathNodeFull
{
  ClutterPathNode k;

  ClutterBezier *bezier;

  guint length;
};

struct _ClutterPathPrivate
{
  GSList *nodes, *nodes_tail;
  gboolean nodes_dirty;

  guint total_length;
};

/* Character tests that don't pay attention to the locale */
#define clutter_path_isspace(ch) memchr (" \f\n\r\t\v", (ch), 6)
#define clutter_path_isdigit(ch) ((ch) >= '0' && (ch) <= '9')

static ClutterPathNodeFull *clutter_path_node_full_new (void);
static void clutter_path_node_full_free (ClutterPathNodeFull *node);

static void clutter_path_finalize (GObject *object);

static void clutter_value_transform_path_string (const GValue *src,
                                                 GValue       *dest);
static void clutter_value_transform_string_path (const GValue *src,
                                                 GValue       *dest);

G_DEFINE_BOXED_TYPE (ClutterPathNode, clutter_path_node,
                     clutter_path_node_copy,
                     clutter_path_node_free);

G_DEFINE_TYPE_WITH_CODE (ClutterPath,
                         clutter_path,
                         G_TYPE_INITIALLY_UNOWNED,
                         G_ADD_PRIVATE (ClutterPath)
                         CLUTTER_REGISTER_VALUE_TRANSFORM_TO (G_TYPE_STRING, clutter_value_transform_path_string)
                         CLUTTER_REGISTER_VALUE_TRANSFORM_FROM (G_TYPE_STRING, clutter_value_transform_string_path));

static void
clutter_path_get_property (GObject      *gobject,
                           guint         prop_id,
                           GValue       *value,
                           GParamSpec   *pspec)
{
  ClutterPath *path = CLUTTER_PATH (gobject);

  switch (prop_id)
    {
    case PROP_DESCRIPTION:
      g_value_take_string (value, clutter_path_get_description (path));
      break;
    case PROP_LENGTH:
      g_value_set_uint (value, clutter_path_get_length (path));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_path_set_property (GObject      *gobject,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  ClutterPath *path = CLUTTER_PATH (gobject);

  switch (prop_id)
    {
    case PROP_DESCRIPTION:
      if (!clutter_path_set_description (path, g_value_get_string (value)))
        g_warning ("Invalid path description");
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_path_class_init (ClutterPathClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GParamSpec *pspec;

  gobject_class->get_property = clutter_path_get_property;
  gobject_class->set_property = clutter_path_set_property;
  gobject_class->finalize = clutter_path_finalize;

  pspec = g_param_spec_string ("description",
                               "Description",
                               "SVG-style description of the path",
                               "",
                               CLUTTER_PARAM_READWRITE);
  obj_props[PROP_DESCRIPTION] = pspec;
  g_object_class_install_property (gobject_class, PROP_DESCRIPTION, pspec);

  pspec = g_param_spec_uint ("length",
                             "Length",
                             "An approximation of the total length "
                             "of the path.",
                             0, G_MAXUINT, 0,
                             CLUTTER_PARAM_READABLE);
  obj_props[PROP_LENGTH] = pspec;
  g_object_class_install_property (gobject_class, PROP_LENGTH, pspec);
}

static void
clutter_path_init (ClutterPath *self)
{
  self->priv = clutter_path_get_instance_private (self);
}

static void
clutter_value_transform_path_string (const GValue *src,
                                     GValue       *dest)
{
  if (src->data[0].v_pointer != NULL)
    {
      gchar *string = clutter_path_get_description (src->data[0].v_pointer);

      g_value_take_string (dest, string);
    }
}

static void
clutter_value_transform_string_path (const GValue *src,
                                     GValue       *dest)
{
  const char *str;

  str = g_value_get_string (src);
  if (str != NULL)
    {
      ClutterPath *new_path = clutter_path_new_with_description (str);
      g_value_take_object (dest, new_path);
    }
}

static void
clutter_path_finalize (GObject *object)
{
  ClutterPath *self = (ClutterPath *) object;

  clutter_path_clear (self);

  G_OBJECT_CLASS (clutter_path_parent_class)->finalize (object);
}

/**
 * clutter_path_new:
 *
 * Creates a new #ClutterPath instance with no nodes.
 *
 * Return value: the newly created #ClutterPath
 */
ClutterPath *
clutter_path_new (void)
{
  ClutterPath *self = g_object_new (CLUTTER_TYPE_PATH, NULL);

  return self;
}

/**
 * clutter_path_new_with_description:
 * @desc: a string describing the path
 *
 * Creates a new #ClutterPath instance with the nodes described in
 * @desc. See [method@Path.add_string] for details of the format of
 * the string.
 *
 * Return value: the newly created #ClutterPath
 */
ClutterPath *
clutter_path_new_with_description (const gchar *desc)
{
  return g_object_new (CLUTTER_TYPE_PATH,
                       "description", desc,
                       NULL);
}

/**
 * clutter_path_clear:
 * @path: a #ClutterPath
 *
 * Removes all nodes from the path.
 */
void
clutter_path_clear (ClutterPath *path)
{
  ClutterPathPrivate *priv = path->priv;

  g_slist_free_full (priv->nodes, (GDestroyNotify) clutter_path_node_full_free);

  priv->nodes = priv->nodes_tail = NULL;
  priv->nodes_dirty = TRUE;
}

/* Takes ownership of the node */
static void
clutter_path_add_node_full (ClutterPath         *path,
                            ClutterPathNodeFull *node)
{
  ClutterPathPrivate *priv = path->priv;
  GSList *new_node;

  new_node = g_slist_prepend (NULL, node);

  if (priv->nodes_tail == NULL)
    priv->nodes = new_node;
  else
    priv->nodes_tail->next = new_node;

  priv->nodes_tail = new_node;

  priv->nodes_dirty = TRUE;
}

/* Helper function to make the rest of the add_* functions shorter */
static void
clutter_path_add_node_helper (ClutterPath         *path,
                              ClutterPathNodeType  type,
                              int                  num_coords,
                              ...)
{
  ClutterPathNodeFull *node;
  int i;
  va_list ap;

  node = clutter_path_node_full_new ();

  node->k.type = type;

  va_start (ap, num_coords);

  for (i = 0; i < num_coords; i++)
    {
      node->k.points[i].x = va_arg (ap, gint);
      node->k.points[i].y = va_arg (ap, gint);
    }

  va_end (ap);

  clutter_path_add_node_full (path, node);
}

/**
 * clutter_path_add_move_to:
 * @path: a #ClutterPath
 * @x: the x coordinate
 * @y: the y coordinate
 *
 * Adds a %CLUTTER_PATH_MOVE_TO type node to the path. This is usually
 * used as the first node in a path. It can also be used in the middle
 * of the path to cause the actor to jump to the new coordinate.
 */
void
clutter_path_add_move_to (ClutterPath *path,
                          gint         x,
                          gint         y)
{
  g_return_if_fail (CLUTTER_IS_PATH (path));

  clutter_path_add_node_helper (path, CLUTTER_PATH_MOVE_TO, 1, x, y);
}

/**
 * clutter_path_add_rel_move_to:
 * @path: a #ClutterPath
 * @x: the x coordinate
 * @y: the y coordinate
 *
 * Same as [method@Path.add_move_to] except the coordinates are
 * relative to the previous node.
 */
void
clutter_path_add_rel_move_to (ClutterPath *path,
                              gint         x,
                              gint         y)
{
  g_return_if_fail (CLUTTER_IS_PATH (path));

  clutter_path_add_node_helper (path, CLUTTER_PATH_REL_MOVE_TO, 1, x, y);
}

/**
 * clutter_path_add_line_to:
 * @path: a #ClutterPath
 * @x: the x coordinate
 * @y: the y coordinate
 *
 * Adds a %CLUTTER_PATH_LINE_TO type node to the path. This causes the
 * actor to move to the new coordinates in a straight line.
 */
void
clutter_path_add_line_to (ClutterPath *path,
                          gint         x,
                          gint         y)
{
  g_return_if_fail (CLUTTER_IS_PATH (path));

  clutter_path_add_node_helper (path, CLUTTER_PATH_LINE_TO, 1, x, y);
}

/**
 * clutter_path_add_rel_line_to:
 * @path: a #ClutterPath
 * @x: the x coordinate
 * @y: the y coordinate
 *
 * Same as [method@Path.add_line_to] except the coordinates are
 * relative to the previous node.
 */
void
clutter_path_add_rel_line_to (ClutterPath *path,
                              gint         x,
                              gint         y)
{
  g_return_if_fail (CLUTTER_IS_PATH (path));

  clutter_path_add_node_helper (path, CLUTTER_PATH_REL_LINE_TO, 1, x, y);
}

/**
 * clutter_path_add_curve_to:
 * @path: a #ClutterPath
 * @x_1: the x coordinate of the first control point
 * @y_1: the y coordinate of the first control point
 * @x_2: the x coordinate of the second control point
 * @y_2: the y coordinate of the second control point
 * @x_3: the x coordinate of the third control point
 * @y_3: the y coordinate of the third control point
 *
 * Adds a %CLUTTER_PATH_CURVE_TO type node to the path. This causes
 * the actor to follow a bezier from the last node to (@x_3, @y_3) using
 * (@x_1, @y_1) and (@x_2,@y_2) as control points.
 */
void
clutter_path_add_curve_to (ClutterPath *path,
                           gint         x_1,
                           gint         y_1,
                           gint         x_2,
                           gint         y_2,
                           gint         x_3,
                           gint         y_3)
{
  g_return_if_fail (CLUTTER_IS_PATH (path));

  clutter_path_add_node_helper (path, CLUTTER_PATH_CURVE_TO, 3,
                                x_1, y_1,
                                x_2, y_2,
                                x_3, y_3);
}

/**
 * clutter_path_add_rel_curve_to:
 * @path: a #ClutterPath
 * @x_1: the x coordinate of the first control point
 * @y_1: the y coordinate of the first control point
 * @x_2: the x coordinate of the second control point
 * @y_2: the y coordinate of the second control point
 * @x_3: the x coordinate of the third control point
 * @y_3: the y coordinate of the third control point
 *
 * Same as [method@Path.add_curve_to] except the coordinates are
 * relative to the previous node.
 */
void
clutter_path_add_rel_curve_to (ClutterPath *path,
                               gint         x_1,
                               gint         y_1,
                               gint         x_2,
                               gint         y_2,
                               gint         x_3,
                               gint         y_3)
{
  g_return_if_fail (CLUTTER_IS_PATH (path));

  clutter_path_add_node_helper (path, CLUTTER_PATH_REL_CURVE_TO, 3,
                                x_1, y_1,
                                x_2, y_2,
                                x_3, y_3);
}

/**
 * clutter_path_add_close:
 * @path: a #ClutterPath
 *
 * Adds a %CLUTTER_PATH_CLOSE type node to the path. This creates a
 * straight line from the last node to the last %CLUTTER_PATH_MOVE_TO
 * type node.
 */
void
clutter_path_add_close (ClutterPath *path)
{
  g_return_if_fail (CLUTTER_IS_PATH (path));

  clutter_path_add_node_helper (path, CLUTTER_PATH_CLOSE, 0);
}

static gboolean
clutter_path_parse_number (const gchar **pin,
                           gboolean      allow_comma,
                           gint         *ret)
{
  gint val = 0;
  gboolean negative = FALSE;
  gint digit_count = 0;
  const gchar *p = *pin;

  /* Skip leading spaces */
  while (clutter_path_isspace (*p))
    p++;

  /* Optional comma */
  if (allow_comma && *p == ',')
    {
      p++;
      while (clutter_path_isspace (*p))
        p++;
    }

  /* Optional sign */
  if (*p == '+')
    p++;
  else if (*p == '-')
    {
      negative = TRUE;
      p++;
    }

  /* Some digits */
  while (clutter_path_isdigit (*p))
    {
      val = val * 10 + *p - '0';
      digit_count++;
      p++;
    }

  /* We need at least one digit */
  if (digit_count < 1)
    return FALSE;

  /* Optional fractional part which we ignore */
  if (*p == '.')
    {
      p++;
      digit_count = 0;
      while (clutter_path_isdigit (*p))
        {
          digit_count++;
          p++;
        }
      /* If there is a fractional part then it also needs at least one
         digit */
      if (digit_count < 1)
        return FALSE;
    }

  *pin = p;
  *ret = negative ? -val : val;

  return TRUE;
}

static gboolean
clutter_path_parse_description (const gchar  *p,
                                GSList      **ret)
{
  ClutterPathNodeFull *node;
  GSList *nodes = NULL;

  if (p == NULL || *p == '\0')
    return FALSE;

  while (TRUE)
    {
      /* Skip leading whitespace */
      while (clutter_path_isspace (*p))
        p++;

      /* It is not an error to end now */
      if (*p == '\0')
        break;

      switch (*p)
        {
        case 'M':
        case 'm':
        case 'L':
        case 'l':
          node = clutter_path_node_full_new ();
          nodes = g_slist_prepend (nodes, node);

          node->k.type = (*p == 'M' ? CLUTTER_PATH_MOVE_TO :
                          *p == 'm' ? CLUTTER_PATH_REL_MOVE_TO :
                          *p == 'L' ? CLUTTER_PATH_LINE_TO :
                          CLUTTER_PATH_REL_LINE_TO);
          p++;

          if (!clutter_path_parse_number (&p, FALSE, &node->k.points[0].x) ||
              !clutter_path_parse_number (&p, TRUE, &node->k.points[0].y))
            goto fail;
          break;

        case 'C':
        case 'c':
          node = clutter_path_node_full_new ();
          nodes = g_slist_prepend (nodes, node);

          node->k.type = (*p == 'C' ? CLUTTER_PATH_CURVE_TO :
                          CLUTTER_PATH_REL_CURVE_TO);
          p++;

          if (!clutter_path_parse_number (&p, FALSE, &node->k.points[0].x) ||
              !clutter_path_parse_number (&p, TRUE, &node->k.points[0].y) ||
              !clutter_path_parse_number (&p, TRUE, &node->k.points[1].x) ||
              !clutter_path_parse_number (&p, TRUE, &node->k.points[1].y) ||
              !clutter_path_parse_number (&p, TRUE, &node->k.points[2].x) ||
              !clutter_path_parse_number (&p, TRUE, &node->k.points[2].y))
            goto fail;
          break;

        case 'Z':
        case 'z':
          node = clutter_path_node_full_new ();
          nodes = g_slist_prepend (nodes, node);
          p++;

          node->k.type = CLUTTER_PATH_CLOSE;
          break;

        default:
          goto fail;
        }
    }

  *ret = g_slist_reverse (nodes);
  return TRUE;

 fail:
  g_slist_free_full (nodes, (GDestroyNotify) clutter_path_node_full_free);
  return FALSE;
}

/* Takes ownership of the node list */
static void
clutter_path_add_nodes (ClutterPath *path,
                        GSList      *nodes)
{
  ClutterPathPrivate *priv = path->priv;

  if (priv->nodes_tail == NULL)
    priv->nodes = nodes;
  else
    priv->nodes_tail->next = nodes;

  while (nodes)
    {
      priv->nodes_tail = nodes;
      nodes = nodes->next;
    }

  priv->nodes_dirty = TRUE;
}

/**
 * clutter_path_add_string:
 * @path: a #ClutterPath
 * @str: a string describing the new nodes
 *
 * Adds new nodes to the end of the path as described in @str. The
 * format is a subset of the SVG path format. Each node is represented
 * by a letter and is followed by zero, one or three pairs of
 * coordinates. The coordinates can be separated by spaces or a
 * comma. The types are:
 *
 *  - `M`: Adds a %CLUTTER_PATH_MOVE_TO node. Takes one pair of coordinates.
 *  - `L`: Adds a %CLUTTER_PATH_LINE_TO node. Takes one pair of coordinates.
 *  - `C`: Adds a %CLUTTER_PATH_CURVE_TO node. Takes three pairs of coordinates.
 *  - `z`: Adds a %CLUTTER_PATH_CLOSE node. No coordinates are needed.
 *
 * The M, L and C commands can also be specified in lower case which
 * means the coordinates are relative to the previous node.
 *
 * For example, to move an actor in a 100 by 100 pixel square centered
 * on the point 300,300 you could use the following path:
 *
 * ```
 *   M 250,350 l 0 -100 L 350,250 l 0 100 z
 * ```
 *
 * If the path description isn't valid %FALSE will be returned and no
 * nodes will be added.
 *
 * Return value: %TRUE is the path description was valid or %FALSE
 * otherwise.
 */
gboolean
clutter_path_add_string (ClutterPath *path,
                         const gchar *str)
{
  GSList *nodes;

  g_return_val_if_fail (CLUTTER_IS_PATH (path), FALSE);
  g_return_val_if_fail (str != NULL, FALSE);

  if (clutter_path_parse_description (str, &nodes))
    {
      clutter_path_add_nodes (path, nodes);

      return TRUE;
    }
  else
    return FALSE;
}

/**
 * clutter_path_add_node:
 * @path: a #ClutterPath
 * @node: a #ClutterPathNode
 *
 * Adds @node to the end of the path.
 */
void
clutter_path_add_node (ClutterPath           *path,
                       const ClutterPathNode *node)
{
  ClutterPathNodeFull *node_full;

  g_return_if_fail (CLUTTER_IS_PATH (path));
  g_return_if_fail (node != NULL);
  g_return_if_fail (CLUTTER_PATH_NODE_TYPE_IS_VALID (node->type));

  node_full = clutter_path_node_full_new ();
  node_full->k = *node;

  clutter_path_add_node_full (path, node_full);
}

/**
 * clutter_path_get_n_nodes:
 * @path: a #ClutterPath
 *
 * Retrieves the number of nodes in the path.
 *
 * Return value: the number of nodes.
 */
guint
clutter_path_get_n_nodes (ClutterPath *path)
{
  ClutterPathPrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_PATH (path), 0);

  priv = path->priv;

  return g_slist_length (priv->nodes);
}

/**
 * clutter_path_get_node:
 * @path: a #ClutterPath
 * @index_: the node number to retrieve
 * @node: (out): a location to store a copy of the node
 *
 * Retrieves the node of the path indexed by @index.
 */
void
clutter_path_get_node (ClutterPath     *path,
                       guint            index_,
                       ClutterPathNode *node)
{
  ClutterPathNodeFull *node_full;
  ClutterPathPrivate *priv;

  g_return_if_fail (CLUTTER_IS_PATH (path));

  priv = path->priv;

  node_full = g_slist_nth_data (priv->nodes, index_);

  g_return_if_fail (node_full != NULL);

  *node = node_full->k;
}

/**
 * clutter_path_get_nodes:
 * @path: a #ClutterPath
 *
 * Returns a #GSList of [struct@PathNode]s.
 * 
 * The list should be freed with g_slist_free(). The nodes are owned 
 * by the path and should not be freed. Altering the path may cause
 * the nodes in the list to become invalid so you should copy them
 * if you want to keep the list.
 *
 * Return value: (transfer container) (element-type Clutter.PathNode): a
 *   list of nodes in the path.
 */
GSList *
clutter_path_get_nodes (ClutterPath *path)
{
  ClutterPathPrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_PATH (path), NULL);

  priv = path->priv;

  return g_slist_copy (priv->nodes);
}

/**
 * clutter_path_foreach:
 * @path: a #ClutterPath
 * @callback: (scope call): the function to call with each node
 * @user_data: user data to pass to the function
 *
 * Calls a function for each node of the path.
 */
void
clutter_path_foreach (ClutterPath         *path,
                      ClutterPathCallback  callback,
                      gpointer             user_data)
{
  ClutterPathPrivate *priv;

  g_return_if_fail (CLUTTER_IS_PATH (path));

  priv = path->priv;

  g_slist_foreach (priv->nodes, (GFunc) callback, user_data);
}

/**
 * clutter_path_insert_node:
 * @path: a #ClutterPath
 * @index_: offset of where to insert the node
 * @node: the node to insert
 *
 * Inserts @node into the path before the node at the given offset. If
 * @index_ is negative it will append the node to the end of the path.
 */
void
clutter_path_insert_node (ClutterPath           *path,
                          gint                   index_,
                          const ClutterPathNode *node)
{
  ClutterPathPrivate *priv;
  ClutterPathNodeFull *node_full;

  g_return_if_fail (CLUTTER_IS_PATH (path));
  g_return_if_fail (node != NULL);
  g_return_if_fail (CLUTTER_PATH_NODE_TYPE_IS_VALID (node->type));

  priv = path->priv;

  node_full = clutter_path_node_full_new ();
  node_full->k = *node;

  priv->nodes = g_slist_insert (priv->nodes, node_full, index_);

  if (priv->nodes_tail == NULL)
    priv->nodes_tail = priv->nodes;
  else if (priv->nodes_tail->next)
    priv->nodes_tail = priv->nodes_tail->next;

  priv->nodes_dirty = TRUE;
}

/**
 * clutter_path_remove_node:
 * @path: a #ClutterPath
 * @index_: index of the node to remove
 *
 * Removes the node at the given offset from the path.
 */
void
clutter_path_remove_node (ClutterPath *path,
                          guint        index_)
{
  ClutterPathPrivate *priv;
  GSList *node, *prev = NULL;

  g_return_if_fail (CLUTTER_IS_PATH (path));

  priv = path->priv;

  for (node = priv->nodes; node && index_--; node = node->next)
    prev = node;

  if (node)
    {
      clutter_path_node_full_free (node->data);

      if (prev)
        prev->next = node->next;
      else
        priv->nodes = node->next;

      if (node == priv->nodes_tail)
        priv->nodes_tail = prev;

      g_slist_free_1 (node);

      priv->nodes_dirty = TRUE;
    }
}

/**
 * clutter_path_replace_node:
 * @path: a #ClutterPath
 * @index_: index to the existing node
 * @node: the replacement node
 *
 * Replaces the node at offset @index_ with @node.
 */
void
clutter_path_replace_node (ClutterPath           *path,
                           guint                  index_,
                           const ClutterPathNode *node)
{
  ClutterPathPrivate *priv;
  ClutterPathNodeFull *node_full;

  g_return_if_fail (CLUTTER_IS_PATH (path));
  g_return_if_fail (node != NULL);
  g_return_if_fail (CLUTTER_PATH_NODE_TYPE_IS_VALID (node->type));

  priv = path->priv;

  if ((node_full = g_slist_nth_data (priv->nodes, index_)))
    {
      node_full->k = *node;

      priv->nodes_dirty = TRUE;
    }
}

/**
 * clutter_path_set_description:
 * @path: a #ClutterPath
 * @str: a string describing the path
 *
 * Replaces all of the nodes in the path with nodes described by
 * @str. See [method@Path.add_string] for details of the format.
 *
 * If the string is invalid then %FALSE is returned and the path is
 * unaltered.
 *
 * Return value: %TRUE is the path was valid, %FALSE otherwise.
 */
gboolean
clutter_path_set_description (ClutterPath *path,
                              const gchar *str)
{
  GSList *nodes;

  g_return_val_if_fail (CLUTTER_IS_PATH (path), FALSE);
  g_return_val_if_fail (str != NULL, FALSE);

  if (clutter_path_parse_description (str, &nodes))
    {
      clutter_path_clear (path);
      clutter_path_add_nodes (path, nodes);

      return TRUE;
    }
  else
    return FALSE;
}

/**
 * clutter_path_get_description:
 * @path: a #ClutterPath
 *
 * Returns a newly allocated string describing the path in the same
 * format as used by [method@Path.add_string].
 *
 * Return value: a string description of the path. Free with g_free().
 */
gchar *
clutter_path_get_description (ClutterPath *path)
{
  ClutterPathPrivate *priv;
  GString *str;
  GSList *l;

  g_return_val_if_fail (CLUTTER_IS_PATH (path), NULL);

  priv = path->priv;

  str = g_string_new ("");

  for (l = priv->nodes; l; l = l->next)
    {
      ClutterPathNodeFull *node = l->data;
      gchar letter = '?';
      gint params = 0;
      gint i;

      switch (node->k.type)
        {
        case CLUTTER_PATH_MOVE_TO:
          letter = 'M';
          params = 1;
          break;

        case CLUTTER_PATH_REL_MOVE_TO:
          letter = 'm';
          params = 1;
          break;

        case CLUTTER_PATH_LINE_TO:
          letter = 'L';
          params = 1;
          break;

        case CLUTTER_PATH_REL_LINE_TO:
          letter = 'l';
          params = 1;
          break;

        case CLUTTER_PATH_CURVE_TO:
          letter = 'C';
          params = 3;
          break;

        case CLUTTER_PATH_REL_CURVE_TO:
          letter = 'c';
          params = 3;
          break;

        case CLUTTER_PATH_CLOSE:
          letter = 'z';
          params = 0;
          break;
        }

      if (str->len > 0)
        g_string_append_c (str, ' ');
      g_string_append_c (str, letter);

      for (i = 0; i < params; i++)
        g_string_append_printf (str, " %i %i",
                                node->k.points[i].x,
                                node->k.points[i].y);
    }

  return g_string_free (str, FALSE);
}

static guint
clutter_path_node_distance (const ClutterKnot *start,
                            const ClutterKnot *end)
{
  gint64 x_d, y_d;
  float t;

  g_return_val_if_fail (start != NULL, 0);
  g_return_val_if_fail (end != NULL, 0);

  if (clutter_knot_equal (start, end))
    return 0;

  x_d = end->x - start->x;
  y_d = end->y - start->y;

  t = floorf (sqrtf ((x_d * x_d) + (y_d * y_d)));

  return (guint) t;
}

static void
clutter_path_ensure_node_data (ClutterPath *path)
{
  ClutterPathPrivate *priv = path->priv;

  /* Recalculate the nodes data if has changed */
  if (priv->nodes_dirty)
    {
      GSList *l;
      ClutterKnot last_position = { 0, 0 };
      ClutterKnot loop_start = { 0, 0 };
      ClutterKnot points[3];

      priv->total_length = 0;

      for (l = priv->nodes; l; l = l->next)
        {
          ClutterPathNodeFull *node = l->data;
          gboolean relative = (node->k.type & CLUTTER_PATH_RELATIVE) != 0;

          switch (node->k.type & ~CLUTTER_PATH_RELATIVE)
            {
            case CLUTTER_PATH_MOVE_TO:
              node->length = 0;

              /* Store the actual position in point[1] */
              if (relative)
                {
                  node->k.points[1].x = last_position.x + node->k.points[0].x;
                  node->k.points[1].y = last_position.y + node->k.points[0].y;
                }
              else
                node->k.points[1] = node->k.points[0];

              last_position = node->k.points[1];
              loop_start = node->k.points[1];
              break;

            case CLUTTER_PATH_LINE_TO:
              /* Use point[1] as the start point and point[2] as the end
                 point */
              node->k.points[1] = last_position;

              if (relative)
                {
                  node->k.points[2].x = (node->k.points[1].x
                                         + node->k.points[0].x);
                  node->k.points[2].y = (node->k.points[1].y
                                         + node->k.points[0].y);
                }
              else
                node->k.points[2] = node->k.points[0];

              last_position = node->k.points[2];

              node->length = clutter_path_node_distance (node->k.points + 1,
                                                         node->k.points + 2);
              break;

            case CLUTTER_PATH_CURVE_TO:
              /* Convert to a bezier curve */
              if (node->bezier == NULL)
                node->bezier = _clutter_bezier_new ();

              if (relative)
                {
                  int i;

                  for (i = 0; i < 3; i++)
                    {
                      points[i].x = last_position.x + node->k.points[i].x;
                      points[i].y = last_position.y + node->k.points[i].y;
                    }
                }
              else
                memcpy (points, node->k.points, sizeof (ClutterKnot) * 3);

              _clutter_bezier_init (node->bezier,
                                    last_position.x, last_position.y,
                                    points[0].x, points[0].y,
                                    points[1].x, points[1].y,
                                    points[2].x, points[2].y);

              last_position = points[2];

              node->length = _clutter_bezier_get_length (node->bezier);

              break;

            case CLUTTER_PATH_CLOSE:
              /* Convert to a line to from last_point to loop_start */
              node->k.points[1] = last_position;
              node->k.points[2] = loop_start;
              last_position = node->k.points[2];

              node->length = clutter_path_node_distance (node->k.points + 1,
                                                         node->k.points + 2);
              break;
            }

          priv->total_length += node->length;
        }

      priv->nodes_dirty = FALSE;
    }
}

/**
 * clutter_path_get_position:
 * @path: a #ClutterPath
 * @progress: a position along the path as a fraction of its length
 * @position: (out): location to store the position
 *
 * The value in @progress represents a position along the path where
 * 0.0 is the beginning and 1.0 is the end of the path. An
 * interpolated position is then stored in @position.
 *
 * Return value: index of the node used to calculate the position.
 */
guint
clutter_path_get_position (ClutterPath *path,
                           gdouble progress,
                           ClutterKnot *position)
{
  ClutterPathPrivate *priv;
  GSList *l;
  guint point_distance, length = 0, node_num = 0;
  ClutterPathNodeFull *node;

  g_return_val_if_fail (CLUTTER_IS_PATH (path), 0);
  g_return_val_if_fail (progress >= 0.0 && progress <= 1.0, 0);

  priv = path->priv;

  clutter_path_ensure_node_data (path);

  /* Special case if the path is empty, just return 0,0 for want of
     something better */
  if (priv->nodes == NULL)
    {
      memset (position, 0, sizeof (ClutterKnot));
      return 0;
    }

  /* Convert the progress to a length along the path */
  point_distance = progress * priv->total_length;

  /* Find the node that covers this point */
  for (l = priv->nodes;
       l->next && point_distance >= (((ClutterPathNodeFull *) l->data)->length
                                     + length);
       l = l->next)
    {
      length += ((ClutterPathNodeFull *) l->data)->length;
      node_num++;
    }

  node = l->data;

  /* Convert the point distance to a distance along the node */
  point_distance -= length;
  if (point_distance > node->length)
    point_distance = node->length;

  switch (node->k.type & ~CLUTTER_PATH_RELATIVE)
    {
    case CLUTTER_PATH_MOVE_TO:
      *position = node->k.points[1];
      break;

    case CLUTTER_PATH_LINE_TO:
    case CLUTTER_PATH_CLOSE:
      if (node->length == 0)
        *position = node->k.points[1];
      else
        {
          position->x = (node->k.points[1].x
                         + ((node->k.points[2].x - node->k.points[1].x)
                            * (gint) point_distance / (gint) node->length));
          position->y = (node->k.points[1].y
                         + ((node->k.points[2].y - node->k.points[1].y)
                            * (gint) point_distance / (gint) node->length));
        }
      break;

    case CLUTTER_PATH_CURVE_TO:
      if (node->length == 0)
        *position = node->k.points[2];
      else
        {
          _clutter_bezier_advance (node->bezier,
                                   point_distance * CLUTTER_BEZIER_MAX_LENGTH
                                   / node->length,
                                   position);
        }
      break;
    }

  return node_num;
}

/**
 * clutter_path_get_length:
 * @path: a #ClutterPath
 *
 * Retrieves an approximation of the total length of the path.
 *
 * Return value: the length of the path.
 */
guint
clutter_path_get_length (ClutterPath *path)
{
  g_return_val_if_fail (CLUTTER_IS_PATH (path), 0);

  clutter_path_ensure_node_data (path);

  return path->priv->total_length;
}

static ClutterPathNodeFull *
clutter_path_node_full_new (void)
{
  return g_new0 (ClutterPathNodeFull, 1);
}

static void
clutter_path_node_full_free (ClutterPathNodeFull *node)
{
  if (node->bezier)
    _clutter_bezier_free (node->bezier);

  g_free (node);
}

/**
 * clutter_path_node_copy:
 * @node: a #ClutterPathNode
 *
 * Makes an allocated copy of a node.
 *
 * Return value: the copied node.
 */
ClutterPathNode *
clutter_path_node_copy (const ClutterPathNode *node)
{
  return g_memdup2 (node, sizeof (ClutterPathNode));
}

/**
 * clutter_path_node_free:
 * @node: a #ClutterPathNode
 *
 * Frees the memory of an allocated node.
 */
void
clutter_path_node_free (ClutterPathNode *node)
{
  if (G_LIKELY (node))
    g_free (node);
}

/**
 * clutter_path_node_equal:
 * @node_a: First node
 * @node_b: Second node
 *
 * Compares two nodes and checks if they are the same type with the
 * same coordinates.
 *
 * Return value: %TRUE if the nodes are the same.
 */
gboolean
clutter_path_node_equal (const ClutterPathNode *node_a,
                         const ClutterPathNode *node_b)
{
  guint n_points, i;

  g_return_val_if_fail (node_a != NULL, FALSE);
  g_return_val_if_fail (node_b != NULL, FALSE);

  if (node_a->type != node_b->type)
    return FALSE;

  switch (node_a->type & ~CLUTTER_PATH_RELATIVE)
    {
    case CLUTTER_PATH_MOVE_TO: n_points = 1; break;
    case CLUTTER_PATH_LINE_TO: n_points = 1; break;
    case CLUTTER_PATH_CURVE_TO: n_points = 3; break;
    case CLUTTER_PATH_CLOSE: n_points = 0; break;
    default: return FALSE;
    }

  for (i = 0; i < n_points; i++)
    if (node_a->points[i].x != node_b->points[i].x
        || node_a->points[i].y != node_b->points[i].y)
      return FALSE;

  return TRUE;
}

G_DEFINE_BOXED_TYPE (ClutterKnot, clutter_knot,
                     clutter_knot_copy,
                     clutter_knot_free);

/**
 * clutter_knot_copy:
 * @knot: a #ClutterKnot
 *
 * Makes an allocated copy of a knot.
 *
 * Return value: the copied knot.
 */
ClutterKnot *
clutter_knot_copy (const ClutterKnot *knot)
{
  if (G_UNLIKELY (knot == NULL))
    return NULL;

  return g_memdup2 (knot, sizeof (ClutterKnot));
}

/**
 * clutter_knot_free:
 * @knot: a #ClutterKnot
 *
 * Frees the memory of an allocated knot.
 */
void
clutter_knot_free (ClutterKnot *knot)
{
  if (G_LIKELY (knot != NULL))
    g_free (knot);
}

/**
 * clutter_knot_equal:
 * @knot_a: First knot
 * @knot_b: Second knot
 *
 * Compares to knot and checks if the point to the same location.
 *
 * Return value: %TRUE if the knots point to the same location.
 */
gboolean
clutter_knot_equal (const ClutterKnot *knot_a,
                    const ClutterKnot *knot_b)
{
  g_return_val_if_fail (knot_a != NULL, FALSE);
  g_return_val_if_fail (knot_b != NULL, FALSE);

  if (knot_a == knot_b)
    return TRUE;

  return knot_a->x == knot_b->x && knot_a->y == knot_b->y;
}
