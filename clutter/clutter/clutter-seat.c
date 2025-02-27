/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2019 Red Hat Inc.
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
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "clutter-build-config.h"

#include "clutter-input-device-tool.h"
#include "clutter-input-pointer-a11y-private.h"
#include "clutter-marshal.h"
#include "clutter-mutter.h"
#include "clutter-private.h"
#include "clutter-seat.h"
#include "clutter-seat-private.h"
#include "clutter-settings-private.h"
#include "clutter-virtual-input-device.h"

enum
{
  DEVICE_ADDED,
  DEVICE_REMOVED,
  KBD_A11Y_MASK_CHANGED,
  KBD_A11Y_FLAGS_CHANGED,
  PTR_A11Y_DWELL_CLICK_TYPE_CHANGED,
  PTR_A11Y_TIMEOUT_STARTED,
  PTR_A11Y_TIMEOUT_STOPPED,
  IS_UNFOCUS_INHIBITED_CHANGED,
  N_SIGNALS,
};

static guint signals[N_SIGNALS] = { 0 };

enum
{
  PROP_0,
  PROP_TOUCH_MODE,
  N_PROPS
};

static GParamSpec *props[N_PROPS];

typedef struct _ClutterSeatPrivate ClutterSeatPrivate;

struct _ClutterSeatPrivate
{
  unsigned int inhibit_unfocus_count;

  /* Pointer a11y */
  ClutterPointerA11ySettings pointer_a11y_settings;
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (ClutterSeat, clutter_seat, G_TYPE_OBJECT)

static void
clutter_seat_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  switch (prop_id)
    {
    case PROP_TOUCH_MODE:
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
clutter_seat_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  switch (prop_id)
    {
    case PROP_TOUCH_MODE:
      g_value_set_boolean (value, FALSE);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
clutter_seat_constructed (GObject *object)
{
  ClutterSettings *settings = clutter_settings_get_default ();

  G_OBJECT_CLASS (clutter_seat_parent_class)->constructed (object);
  clutter_settings_ensure_pointer_a11y_settings (settings,
                                                 CLUTTER_SEAT (object));
}

static void
clutter_seat_class_init (ClutterSeatClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = clutter_seat_set_property;
  object_class->get_property = clutter_seat_get_property;
  object_class->constructed = clutter_seat_constructed;

  signals[DEVICE_ADDED] =
    g_signal_new (I_("device-added"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  CLUTTER_TYPE_INPUT_DEVICE);

  signals[DEVICE_REMOVED] =
    g_signal_new (I_("device-removed"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  CLUTTER_TYPE_INPUT_DEVICE);

  /**
   * ClutterSeat::kbd-a11y-mods-state-changed:
   * @seat: the #ClutterSeat that emitted the signal
   * @latched_mask: the latched modifier mask from stickykeys
   * @locked_mask:  the locked modifier mask from stickykeys
   *
   * The signal is emitted each time either the
   * latched modifiers mask or locked modifiers mask are changed as the
   * result of keyboard accessibilty's sticky keys operations.
   */
  signals[KBD_A11Y_MASK_CHANGED] =
    g_signal_new (I_("kbd-a11y-mods-state-changed"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  _clutter_marshal_VOID__UINT_UINT,
                  G_TYPE_NONE, 2,
                  G_TYPE_UINT,
                  G_TYPE_UINT);
  g_signal_set_va_marshaller (signals[KBD_A11Y_MASK_CHANGED],
                              G_TYPE_FROM_CLASS (object_class),
                              _clutter_marshal_VOID__UINT_UINTv);

  /**
   * ClutterSeat::kbd-a11y-flags-changed:
   * @seat: the #ClutterSeat that emitted the signal
   * @settings_flags: the new ClutterKeyboardA11yFlags configuration
   * @changed_mask: the ClutterKeyboardA11yFlags changed
   *
   * The signal is emitted each time the ClutterKeyboardA11yFlags
   * configuration is changed as the result of keyboard accessibility operations.
   */
  signals[KBD_A11Y_FLAGS_CHANGED] =
    g_signal_new (I_("kbd-a11y-flags-changed"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  _clutter_marshal_VOID__UINT_UINT,
                  G_TYPE_NONE, 2,
                  G_TYPE_UINT,
                  G_TYPE_UINT);
  g_signal_set_va_marshaller (signals[KBD_A11Y_FLAGS_CHANGED],
                              G_TYPE_FROM_CLASS (object_class),
                              _clutter_marshal_VOID__UINT_UINTv);

  /**
   * ClutterSeat::ptr-a11y-dwell-click-type-changed:
   * @seat: the #ClutterSeat that emitted the signal
   * @click_type: the new #ClutterPointerA11yDwellClickType mode
   *
   * The signal is emitted each time the ClutterPointerA11yDwellClickType
   * mode is changed as the result of pointer accessibility operations.
   */
  signals[PTR_A11Y_DWELL_CLICK_TYPE_CHANGED] =
    g_signal_new (I_("ptr-a11y-dwell-click-type-changed"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  CLUTTER_TYPE_POINTER_A11Y_DWELL_CLICK_TYPE);

  /**
   * ClutterSeat::ptr-a11y-timeout-started:
   * @seat: the #ClutterSeat that emitted the signal
   * @device: the core pointer #ClutterInputDevice
   * @timeout_type: the type of timeout #ClutterPointerA11yTimeoutType
   * @delay: the delay in ms before secondary-click is triggered.
   *
   * The signal is emitted when a pointer accessibility timeout delay is started,
   * so that upper layers can notify the user with some visual feedback.
   */
  signals[PTR_A11Y_TIMEOUT_STARTED] =
    g_signal_new (I_("ptr-a11y-timeout-started"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  _clutter_marshal_VOID__OBJECT_FLAGS_UINT,
                  G_TYPE_NONE, 3,
                  CLUTTER_TYPE_INPUT_DEVICE,
                  CLUTTER_TYPE_POINTER_A11Y_TIMEOUT_TYPE,
                  G_TYPE_UINT);
  g_signal_set_va_marshaller (signals[PTR_A11Y_TIMEOUT_STARTED],
                              G_TYPE_FROM_CLASS (object_class),
                              _clutter_marshal_VOID__OBJECT_FLAGS_UINTv);

  /**
   * ClutterSeat::ptr-a11y-timeout-stopped:
   * @seat: the #ClutterSeat that emitted the signal
   * @device: the core pointer #ClutterInputDevice
   * @timeout_type: the type of timeout #ClutterPointerA11yTimeoutType
   * @clicked: %TRUE if the timeout finished and triggered a click
   *
   * The signal is emitted when a running pointer accessibility timeout
   * delay is stopped, either because it's triggered at the end of
   * the delay or cancelled, so that upper layers can notify the user
   * with some visual feedback.
   */
  signals[PTR_A11Y_TIMEOUT_STOPPED] =
    g_signal_new (I_("ptr-a11y-timeout-stopped"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  _clutter_marshal_VOID__OBJECT_FLAGS_BOOLEAN,
                  G_TYPE_NONE, 3,
                  CLUTTER_TYPE_INPUT_DEVICE,
                  CLUTTER_TYPE_POINTER_A11Y_TIMEOUT_TYPE,
                  G_TYPE_BOOLEAN);
  g_signal_set_va_marshaller (signals[PTR_A11Y_TIMEOUT_STOPPED],
                              G_TYPE_FROM_CLASS (object_class),
                              _clutter_marshal_VOID__OBJECT_FLAGS_BOOLEANv);

  /**
   * ClutterSeat::is-unfocus-inhibited-changed:
   * @seat: the #ClutterSeat that emitted the signal
   *
   * The signal is emitted when the property to inhibit the unsetting
   * of the focus-surface of the #ClutterSeat changed.
   *  
   * To get the current state of this property, use [method@Seat.is_unfocus_inhibited].
   */
  signals[IS_UNFOCUS_INHIBITED_CHANGED] =
    g_signal_new (I_("is-unfocus-inhibited-changed"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  /**
   * ClutterSeat:touch-mode:
   *
   * The current touch-mode of the #ClutterSeat, it is set to %TRUE if the
   * requirements documented in [method@Seat.get_touch_mode] are fulfilled.
   **/
  props[PROP_TOUCH_MODE] =
    g_param_spec_boolean ("touch-mode",
                          P_("Touch mode"),
                          P_("Touch mode"),
                          FALSE,
                          CLUTTER_PARAM_READABLE);

  g_object_class_install_properties (object_class, N_PROPS, props);
}

static void
clutter_seat_init (ClutterSeat *seat)
{
}

/**
 * clutter_seat_get_pointer:
 * @seat: a #ClutterSeat
 *
 * Returns the logical pointer
 *
 * Returns: (transfer none): the logical pointer
 **/
ClutterInputDevice *
clutter_seat_get_pointer (ClutterSeat *seat)
{
  g_return_val_if_fail (CLUTTER_IS_SEAT (seat), NULL);

  return CLUTTER_SEAT_GET_CLASS (seat)->get_pointer (seat);
}

/**
 * clutter_seat_get_keyboard:
 * @seat: a #ClutterSeat
 *
 * Returns the logical keyboard
 *
 * Returns: (transfer none): the logical keyboard
 **/
ClutterInputDevice *
clutter_seat_get_keyboard (ClutterSeat *seat)
{
  g_return_val_if_fail (CLUTTER_IS_SEAT (seat), NULL);

  return CLUTTER_SEAT_GET_CLASS (seat)->get_keyboard (seat);
}

/**
 * clutter_seat_peek_devices: (skip)
 **/
const GList *
clutter_seat_peek_devices (ClutterSeat *seat)
{
  g_return_val_if_fail (CLUTTER_IS_SEAT (seat), NULL);

  return CLUTTER_SEAT_GET_CLASS (seat)->peek_devices (seat);
}

/**
 * clutter_seat_list_devices:
 * @seat: a #ClutterSeat
 *
 * Returns the list of HW devices
 *
 * Returns: (transfer container) (element-type Clutter.InputDevice): A list
 *   of #ClutterInputDevice. The elements of the returned list are owned by
 *   Clutter and may not be freed, the returned list should be freed using
 *   g_list_free() when done.
 **/
GList *
clutter_seat_list_devices (ClutterSeat *seat)
{
  g_return_val_if_fail (CLUTTER_IS_SEAT (seat), NULL);

  return g_list_copy ((GList *)clutter_seat_peek_devices (seat));
}

void
clutter_seat_bell_notify (ClutterSeat *seat)
{
  CLUTTER_SEAT_GET_CLASS (seat)->bell_notify (seat);
}

/**
 * clutter_seat_get_keymap:
 * @seat: a #ClutterSeat
 *
 * Returns the seat keymap
 *
 * Returns: (transfer none): the seat keymap
 **/
ClutterKeymap *
clutter_seat_get_keymap (ClutterSeat *seat)
{
  return CLUTTER_SEAT_GET_CLASS (seat)->get_keymap (seat);
}

void
clutter_seat_ensure_a11y_state (ClutterSeat *seat)
{
  ClutterInputDevice *core_pointer;

  core_pointer = clutter_seat_get_pointer (seat);

  if (core_pointer)
    {
      if (_clutter_is_input_pointer_a11y_enabled (core_pointer))
        _clutter_input_pointer_a11y_add_device (core_pointer);
    }
}

static gboolean
are_pointer_a11y_settings_equal (ClutterPointerA11ySettings *a,
                                 ClutterPointerA11ySettings *b)
{
  return (memcmp (a, b, sizeof (ClutterPointerA11ySettings)) == 0);
}

static void
clutter_seat_enable_pointer_a11y (ClutterSeat *seat)
{
  ClutterInputDevice *core_pointer;

  core_pointer = clutter_seat_get_pointer (seat);

  _clutter_input_pointer_a11y_add_device (core_pointer);
}

static void
clutter_seat_disable_pointer_a11y (ClutterSeat *seat)
{
  ClutterInputDevice *core_pointer;

  core_pointer = clutter_seat_get_pointer (seat);

  _clutter_input_pointer_a11y_remove_device (core_pointer);
}

/**
 * clutter_seat_set_pointer_a11y_settings:
 * @seat: a #ClutterSeat
 * @settings: a pointer to a #ClutterPointerA11ySettings
 *
 * Sets the pointer accessibility settings
 **/
void
clutter_seat_set_pointer_a11y_settings (ClutterSeat                *seat,
                                        ClutterPointerA11ySettings *settings)
{
  ClutterSeatPrivate *priv = clutter_seat_get_instance_private (seat);

  g_return_if_fail (CLUTTER_IS_SEAT (seat));

  if (are_pointer_a11y_settings_equal (&priv->pointer_a11y_settings, settings))
    return;

  if (priv->pointer_a11y_settings.controls == 0 && settings->controls != 0)
    clutter_seat_enable_pointer_a11y (seat);
  else if (priv->pointer_a11y_settings.controls != 0 && settings->controls == 0)
    clutter_seat_disable_pointer_a11y (seat);

  priv->pointer_a11y_settings = *settings;
}

/**
 * clutter_seat_get_pointer_a11y_settings:
 * @seat: a #ClutterSeat
 * @settings: a pointer to a #ClutterPointerA11ySettings
 *
 * Gets the current pointer accessibility settings
 **/
void
clutter_seat_get_pointer_a11y_settings (ClutterSeat                *seat,
                                        ClutterPointerA11ySettings *settings)
{
  ClutterSeatPrivate *priv = clutter_seat_get_instance_private (seat);

  g_return_if_fail (CLUTTER_IS_SEAT (seat));

  *settings = priv->pointer_a11y_settings;
}

/**
 * clutter_seat_set_pointer_a11y_dwell_click_type:
 * @seat: a #ClutterSeat
 * @click_type: type of click as #ClutterPointerA11yDwellClickType
 *
 * Sets the dwell click type
 **/
void
clutter_seat_set_pointer_a11y_dwell_click_type (ClutterSeat                      *seat,
                                                ClutterPointerA11yDwellClickType  click_type)
{
  ClutterSeatPrivate *priv = clutter_seat_get_instance_private (seat);

  g_return_if_fail (CLUTTER_IS_SEAT (seat));

  priv->pointer_a11y_settings.dwell_click_type = click_type;
}

/**
 * clutter_seat_inhibit_unfocus:
 * @seat: a #ClutterSeat
 *
 * Inhibits unsetting of the pointer focus-surface for the #ClutterSeat @seat,
 * this allows to keep using the pointer even when it's hidden.
 *
 * This property is refcounted, so [method@Seat.uninhibit_unfocus] must be
 * called the exact same number of times as [method@Seat.inhibit_unfocus]
 * was called before.
 **/
void
clutter_seat_inhibit_unfocus (ClutterSeat *seat)
{
  ClutterSeatPrivate *priv;

  g_return_if_fail (CLUTTER_IS_SEAT (seat));

  priv = clutter_seat_get_instance_private (seat);

  priv->inhibit_unfocus_count++;

  if (priv->inhibit_unfocus_count == 1)
    g_signal_emit (G_OBJECT (seat), signals[IS_UNFOCUS_INHIBITED_CHANGED], 0);
}

/**
 * clutter_seat_uninhibit_unfocus:
 * @seat: a #ClutterSeat
 *
 * Disables the inhibiting of unsetting of the pointer focus-surface
 * previously enabled by calling [method@Seat.inhibit_unfocus].
 *
 * This property is refcounted, so [method@Seat.uninhibit_unfocus] must be
 * called the exact same number of times as [method@Seat.inhibit_unfocus]
 * was called before.
 **/
void
clutter_seat_uninhibit_unfocus (ClutterSeat *seat)
{
  ClutterSeatPrivate *priv;

  g_return_if_fail (CLUTTER_IS_SEAT (seat));

  priv = clutter_seat_get_instance_private (seat);

  if (priv->inhibit_unfocus_count == 0)
    {
      g_warning ("Called clutter_seat_uninhibit_unfocus without inhibiting before");
      return;
    }

  priv->inhibit_unfocus_count--;

  if (priv->inhibit_unfocus_count == 0)
    g_signal_emit (G_OBJECT (seat), signals[IS_UNFOCUS_INHIBITED_CHANGED], 0);
}

/**
 * clutter_seat_is_unfocus_inhibited:
 * @seat: a #ClutterSeat
 *
 * Gets whether unsetting of the pointer focus-surface is inhibited
 * for the #ClutterSeat @seat.
 *
 * Returns: %TRUE if unsetting is inhibited, %FALSE otherwise
 **/
gboolean
clutter_seat_is_unfocus_inhibited (ClutterSeat *seat)
{
  ClutterSeatPrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_SEAT (seat), FALSE);

  priv = clutter_seat_get_instance_private (seat);

  return priv->inhibit_unfocus_count > 0;
}

/**
 * clutter_seat_create_virtual_device:
 * @seat: a #ClutterSeat
 * @device_type: the type of the virtual device
 *
 * Creates a virtual input device.
 *
 * Returns: (transfer full): a newly created virtual device
 **/
ClutterVirtualInputDevice *
clutter_seat_create_virtual_device (ClutterSeat            *seat,
                                    ClutterInputDeviceType  device_type)
{
  ClutterSeatClass *seat_class;

  g_return_val_if_fail (CLUTTER_IS_SEAT (seat), NULL);

  seat_class = CLUTTER_SEAT_GET_CLASS (seat);
  return seat_class->create_virtual_device (seat, device_type);
}

/**
 * clutter_seat_get_supported_virtual_device_types: (skip)
 **/
ClutterVirtualDeviceType
clutter_seat_get_supported_virtual_device_types (ClutterSeat *seat)
{
  ClutterSeatClass *seat_class;

  g_return_val_if_fail (CLUTTER_IS_SEAT (seat),
                        CLUTTER_VIRTUAL_DEVICE_TYPE_NONE);

  seat_class = CLUTTER_SEAT_GET_CLASS (seat);
  return seat_class->get_supported_virtual_device_types (seat);
}

gboolean
clutter_seat_handle_event_post (ClutterSeat        *seat,
                                const ClutterEvent *event)
{
  ClutterSeatClass *seat_class;
  ClutterInputDevice *device;

  g_return_val_if_fail (CLUTTER_IS_SEAT (seat), FALSE);
  g_return_val_if_fail (event, FALSE);

  seat_class = CLUTTER_SEAT_GET_CLASS (seat);

  if (seat_class->handle_event_post)
    seat_class->handle_event_post (seat, event);

  device = clutter_event_get_source_device (event);
  g_assert_true (CLUTTER_IS_INPUT_DEVICE (device));

  switch (event->type)
    {
      case CLUTTER_DEVICE_ADDED:
        g_signal_emit (seat, signals[DEVICE_ADDED], 0, device);
        break;
      case CLUTTER_DEVICE_REMOVED:
        g_signal_emit (seat, signals[DEVICE_REMOVED], 0, device);
        g_object_run_dispose (G_OBJECT (device));
        break;
      default:
        break;
    }

  return TRUE;
}

void
clutter_seat_warp_pointer (ClutterSeat *seat,
                           int          x,
                           int          y)
{
  g_return_if_fail (CLUTTER_IS_SEAT (seat));

  CLUTTER_SEAT_GET_CLASS (seat)->warp_pointer (seat, x, y);
}

void
clutter_seat_init_pointer_position (ClutterSeat *seat,
                                    float        x,
                                    float        y)
{
  g_return_if_fail (CLUTTER_IS_SEAT (seat));

  CLUTTER_SEAT_GET_CLASS (seat)->init_pointer_position (seat, x, y);
}

/**
 * clutter_seat_get_touch_mode:
 * @seat: a #ClutterSeat
 *
 * Gets the current touch-mode state of the #ClutterSeat @seat.
 * The [property@Seat:touch-mode] property is set to %TRUE if the following
 * requirements are fulfilled:
 *
 *  - A touchscreen is available
 *  - A tablet mode switch, if present, is enabled
 *
 * Returns: %TRUE if the device is a tablet that doesn't have an external
 *   keyboard attached, %FALSE otherwise.
 **/
gboolean
clutter_seat_get_touch_mode (ClutterSeat *seat)
{
  gboolean touch_mode;

  g_return_val_if_fail (CLUTTER_IS_SEAT (seat), FALSE);

  g_object_get (G_OBJECT (seat), "touch-mode", &touch_mode, NULL);

  return touch_mode;
}

/**
 * clutter_seat_has_touchscreen: (skip)
 **/
gboolean
clutter_seat_has_touchscreen (ClutterSeat *seat)
{
  gboolean has_touchscreen = FALSE;
  const GList *devices, *l;

  g_return_val_if_fail (CLUTTER_IS_SEAT (seat), FALSE);

  devices = clutter_seat_peek_devices (seat);
  for (l = devices; l; l = l->next)
    {
      ClutterInputDevice *device = l->data;

      if (clutter_input_device_get_device_mode (device) != CLUTTER_INPUT_MODE_LOGICAL &&
          clutter_input_device_get_device_type (device) == CLUTTER_TOUCHSCREEN_DEVICE)
        {
          has_touchscreen = TRUE;
          break;
        }
    }

  return has_touchscreen;
}

/**
 * clutter_seat_query_state:
 * @seat: a #ClutterSeat
 * @device: a #ClutterInputDevice
 * @sequence: (nullable): a #ClutterEventSequence
 * @coords: (out caller-allocates) (optional): the coordinates of the pointer
 * @modifiers: (out) (optional): the current #ClutterModifierType of the pointer
 *
 * Returns: %TRUE if @device (or the specific @sequence) is on the stage, %FALSE
 *   otherwise.
 **/
gboolean
clutter_seat_query_state (ClutterSeat          *seat,
                          ClutterInputDevice   *device,
                          ClutterEventSequence *sequence,
                          graphene_point_t     *coords,
                          ClutterModifierType  *modifiers)
{
  g_return_val_if_fail (CLUTTER_IS_SEAT (seat), FALSE);
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), FALSE);

  return CLUTTER_SEAT_GET_CLASS (seat)->query_state (seat,
                                                     device,
                                                     sequence,
                                                     coords,
                                                     modifiers);
}

void
clutter_seat_destroy (ClutterSeat *seat)
{
  g_object_run_dispose (G_OBJECT (seat));
  g_object_unref (seat);
}

ClutterGrabState
clutter_seat_grab (ClutterSeat *seat,
                   uint32_t     time)
{
  ClutterSeatClass *seat_class;

  seat_class = CLUTTER_SEAT_GET_CLASS (seat);
  if (seat_class->grab)
    return seat_class->grab (seat, time);
  else
    return CLUTTER_GRAB_STATE_ALL;
}

void
clutter_seat_ungrab (ClutterSeat *seat,
                     uint32_t     time)
{
  ClutterSeatClass *seat_class;

  seat_class = CLUTTER_SEAT_GET_CLASS (seat);
  if (seat_class->ungrab)
    return seat_class->ungrab (seat, time);
}
