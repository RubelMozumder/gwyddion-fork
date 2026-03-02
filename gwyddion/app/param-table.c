/*
 *  $Id: param-table.c 26317 2024-05-06 09:52:03Z yeti-dn $
 *  Copyright (C) 2021-2023 David Necas (Yeti).
 *  E-mail: yeti@gwyddion.net.
 *
 *  This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any
 *  later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *  warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along with this program; if not, write to the
 *  Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include <stdarg.h>
#include <stdlib.h>
#include "libgwyddion/gwymacros.h"
#include "libgwyddion/gwymath.h"
#include "libprocess/gwyprocesstypes.h"
#include "libprocess/gwyprocessenums.h"
#include "libprocess/stats.h"
#include "libprocess/linestats.h"
#include "gwyapptypes.h"
#include "data-browser.h"
#include "menu.h"
#include "gwymoduleutils.h"
#include "dialog.h"
#include "param-table.h"
#include "param-internal.h"

/* TODO: Missing common controls:
 * - entries with history
 *
 * We also want presets to work more or less automatically (in GwyParams), which is partially implemented by
 * GwyParamResource, but so far only one module has been converted.
 */

/*
 * TODO: Common parameter interface (to avoid long lists of cases):
 *
 * make_control(GwyParamTable *partable, guint i, GwyParams *params, const GwyParamDefItem *def);
 * finalize(GwyParamControl *control);
 * destroyed(GwyParamControl *control);
 * set_sensitive(GwyParamControl *control, gboolean sensitive);
 *
 * But this can wait for some cleanup rewrite when things stop changing.
 */

typedef struct _GwyParamTablePrivate GwyParamTablePrivate;

enum {
    GWY_PARAM_TABLE_ROWSEP    = 2,
    GWY_PARAM_TABLE_COLSEP    = 6,
    GWY_PARAM_TABLE_BIGROWSEP = 12,
};

typedef enum {
    GWY_PARAM_CONTROL_HEADER,
    GWY_PARAM_CONTROL_SEPARATOR,
    GWY_PARAM_CONTROL_CHECKBOX,
    GWY_PARAM_CONTROL_ENABLER,
    GWY_PARAM_CONTROL_SLIDER,
    GWY_PARAM_CONTROL_ENTRY,
    GWY_PARAM_CONTROL_COMBO,
    GWY_PARAM_CONTROL_IMAGE_ID_COMBO,
    GWY_PARAM_CONTROL_GRAPH_ID_COMBO,
    GWY_PARAM_CONTROL_VOLUME_ID_COMBO,
    GWY_PARAM_CONTROL_XYZ_ID_COMBO,
    GWY_PARAM_CONTROL_CURVE_MAP_ID_COMBO,
    GWY_PARAM_CONTROL_GRAPH_CURVE_COMBO,
    GWY_PARAM_CONTROL_LAWN_CURVE_COMBO,
    GWY_PARAM_CONTROL_LAWN_SEGMENT_COMBO,
    GWY_PARAM_CONTROL_UNIT_CHOOSER,
    GWY_PARAM_CONTROL_RADIO_HEADER,
    GWY_PARAM_CONTROL_RADIO,
    GWY_PARAM_CONTROL_RADIO_ITEM,
    GWY_PARAM_CONTROL_RADIO_ROW,
    GWY_PARAM_CONTROL_RADIO_BUTTONS,
    GWY_PARAM_CONTROL_CHECKBOXES,
    GWY_PARAM_CONTROL_COLOR,
    GWY_PARAM_CONTROL_MASK_COLOR,
    GWY_PARAM_CONTROL_RESULTS,
    GWY_PARAM_CONTROL_REPORT,
    GWY_PARAM_CONTROL_HOLD_SELECTION,
    GWY_PARAM_CONTROL_RANDOM_SEED,
    GWY_PARAM_CONTROL_RANGE_FROM,
    GWY_PARAM_CONTROL_RANGE_TO,
    GWY_PARAM_CONTROL_BUTTON,
    GWY_PARAM_CONTROL_MESSAGE,
    GWY_PARAM_CONTROL_INFO,
    GWY_PARAM_CONTROL_FOREIGN,
} GwyParamControlType;

enum {
    SGNL_PARAM_CHANGED,
    N_SIGNALS
};

typedef gdouble       (*GwyParamEntryParseFunc) (const gchar *text,
                                                 gchar **endptr,
                                                 gpointer user_data);
typedef const gchar*  (*GwyParamEntryFormatFunc)(gdouble value,
                                                 gpointer user_data);

typedef struct {
    gint aux_id;
    gint other_id;
} GwyParamControlAssoc;

typedef struct {
    GwyParamControlAssoc *assoc;
    guint n;
    guint nalloc;
} GwyParamControlAssocTable;

typedef struct {
    gint id;
    guint32 sensitive_bits;
    gboolean sensitive : 1;
} GwyToggleListInfo;

typedef struct {
    GwyRGBA rgba;
    const gchar *name;
} GwyPresetColor;

typedef struct {
    GtkWidget *container_child;
} GwyParamControlEnabler;

typedef struct {
    GwyEnum *modified_enum;
    GwyInventory *inventory;
    GwyEnumFilterFunc filter_func;
    gpointer filter_data;
    GDestroyNotify filter_destroy;
    gboolean is_resource;
} GwyParamControlCombo;

typedef struct {
    GtkWidget *change_button;
    gboolean changing_unit : 1;
} GwyParamControlUnitChooser;

typedef struct {
    gint value;
} GwyParamControlRadioItem;

typedef struct {
    const GwyEnum *stock_ids;
} GwyParamControlRadioButtons;

typedef struct {
    GwyDataChooserFilterFunc filter_func;
    gpointer filter_data;
    GDestroyNotify filter_destroy;
    const gchar *none;
} GwyParamControlDataChooser;

typedef struct {
    GObject *parent;
} GwyParamControlCurveChooser;

typedef struct {
    GArray *presets;
    GtkSizeGroup *sizegroup;
} GwyParamControlColor;

typedef struct {
    GwyContainer *preview_data;
    GwyContainer *data;
    gint preview_i;
    gint i;
} GwyParamControlMaskColor;

typedef struct {
    gint response;
    gint sibling_id_prev;
    gint sibling_id_next;
    /* Only the first button has these set. */
    gchar *label;    /* We can have a label before the button.  This is the button text. */
    GtkSizeGroup *sizegroup;
} GwyParamControlButton;

typedef struct {
    GwyResults *results;
    GtkWidget **value_labels;
    gchar **result_ids;
    gint nresults : 16;
    gboolean wants_to_be_filled : 1;
} GwyParamControlResults;

typedef struct {
    GwyResults *results;
    GwyCreateTextFunc format_report;
    gpointer formatter_data;
    GDestroyNotify formatter_destroy;
    gulong copy_sid;
    gulong save_sid;
} GwyParamControlReport;

typedef struct {
    GtkWidget *overwrite;
    GtkWidget *clear;
} GwyParamControlHoldSelection;

typedef struct {
    GtkAdjustment *adj;
    GtkWidget *new_button;
} GwyParamControlRandomSeed;

typedef struct {
    GtkMessageType type;
} GwyParamControlMessage;

typedef struct {
    gchar *valuestr;
    gboolean add_punctuation;
} GwyParamControlInfo;

typedef struct {
    GwyCreateWidgetFunc create_widget;
    gpointer user_data;
    GDestroyNotify destroy;
} GwyParamControlForeign;

typedef struct {
    GtkWidget *spin;
    GtkAdjustment *adj;
    /* Transformations. */
    GwyRealFunc transform_to_gui;
    GwyRealFunc transform_from_gui;
    gpointer transform_data;
    GDestroyNotify transform_destroy;
    gdouble q_value_to_gui;
    /* Alternative value. */
    GtkWidget *alt_spin;
    GtkWidget *alt_unitlabel;
    gchar *alt_unitstr;
    gdouble alt_q_to_gui;
    gdouble alt_offset_to_gui;
    GwyDataLine *calibration;
    gdouble calibration_step;
    gdouble calibration_delta;
    /* These are true parameter values. */
    gdouble minimum;
    gdouble maximum;
    gdouble step;
    gdouble page;
    /* This is for the spin button, i.e. transformed. */
    gint digits : 8;
    gint alt_digits : 8;
    GwyScaleMappingType mapping : 3;
    gboolean is_int : 1;
    gboolean is_angle : 1;
    gboolean is_percentage : 1;
    gboolean snap : 1;
    gboolean mapping_set : 1;
    gboolean steps_set : 1;
    gboolean digits_set : 1;
    gboolean snap_set : 1;
    gboolean range_set : 1;
    gboolean has_alt : 1;
} GwyParamControlSlider;

typedef struct {
    gint width : 16;
    gboolean is_numeric : 1;
    gboolean is_int : 1;
    gboolean instant_changes : 1;
    /* For real numbers. */
    GString *str;   /* Shorthand for table->priv->str. */
    GwyParamEntryParseFunc parse;
    GwyParamEntryFormatFunc format;
    gpointer transform_data;
    GDestroyNotify transform_destroy;
    GwySIValueFormat *vf;
} GwyParamControlEntry;

typedef struct {
    GString *str;   /* Shorthand for table->priv->str. */
    GwySIValueFormat *vf;
} GwyParamControlRangeFrom;

typedef struct {
    gint id;
    gint row : 8;      /* The first row where it is attached. */
    gint nrows : 8;    /* How many table rows it takes.  In split radio buttons, it is for this particular piece. */
    GwyParamControlType type : 12;
    gboolean sensitive : 1;      /* Not used by radio and checkbox list controls, see toggles_info. */
    gboolean do_not_reset : 1;   /* Some controls have it set by default. */
    GtkWidget *label;  /* Descriptive label or list header (for radio buttons), occasionally unused. */
    GtkWidget *widget; /* The main control widget, whatever it is the appropriate type.  For most radio types this
                          is simply one of the radios; even for the header we store one of the buttons here. */
    GtkWidget *unitlabel;  /* Label right to the control, usually for sliders. */
    gchar *label_text;     /* Only set if it differs from parameter definition. */
    gchar *unitstr;     /* The unit string. */
    gpointer impl;     /* Pointer to GwyParamControlFoo for specific subtypes. */
} GwyParamControl;

struct _GwyParamTablePrivate {
    GwyParams *params;
    GtkWidget *widget;
    GwyDialog *parent_dialog;
    GwyPlainTool *parent_tool;
    GwyParamControl *controls;
    GwyParamControlAssocTable enabler;
    GwyParamControlAssocTable range;
    GwyToggleListInfo *toggles_info;
    GString *str;
    GwySIValueFormat *vf;
    guint ncontrols;
    guint nalloc_control;
    guint ntoggles;
    guint nalloc_toggles;
    gint nrows;
    gint ncols;
    gint in_update;
    gboolean proceed;
    gulong destroy_sid;
};

static void                      gwy_param_table_finalize        (GObject *gobject);
static GwyParamControl*          find_first_control              (GwyParamTable *partable,
                                                                  gint id);
static gboolean                  find_def_common                 (GwyParamTable *partable,
                                                                  gint id,
                                                                  GwyParams **pparams,
                                                                  const GwyParamDefItem **def);
static GwyParamControl*          append_control                  (GwyParamTable *partable,
                                                                  GwyParamControlType type,
                                                                  gint id,
                                                                  gint nrows);
static void                      add_aux_assoc                   (GwyParamControlAssocTable *assoc_table,
                                                                  gint aux_id,
                                                                  gint other_id);
static void                      add_toggles_info                (GwyParamTable *partable,
                                                                  gint id,
                                                                  gboolean must_not_exist);
static GtkWidget*                ensure_widget                   (GwyParamTable *partable);
static void                      update_control_sensitivity      (GwyParamTable *partable,
                                                                  gint i);
static void                      header_make_control             (GwyParamTable *partable,
                                                                  guint i);
static void                      checkbox_make_control           (GwyParamTable *partable,
                                                                  guint i,
                                                                  const GwyParamDefItem *def);
static void                      enabler_make_control            (GwyParamTable *partable,
                                                                  guint i,
                                                                  guint iother);
static void                      combo_make_control              (GwyParamTable *partable,
                                                                  guint i,
                                                                  const GwyParamDefItem *def);
static void                      radio_make_control              (GwyParamTable *partable,
                                                                  guint i,
                                                                  const GwyParamDefItem *def);
static void                      radio_header_make_control       (GwyParamTable *partable,
                                                                  guint i,
                                                                  const GwyParamDefItem *def);
static void                      radio_item_make_control         (GwyParamTable *partable,
                                                                  guint i,
                                                                  const GwyParamDefItem *def);
static void                      radio_row_make_control          (GwyParamTable *partable,
                                                                  guint i,
                                                                  const GwyParamDefItem *def);
static void                      radio_buttons_make_control      (GwyParamTable *partable,
                                                                  guint i,
                                                                  const GwyParamDefItem *def);
static void                      construct_radio_widgets         (GwyParamTable *partable,
                                                                  guint i,
                                                                  const GwyParamDefItem *def);
static void                      checkboxes_make_control         (GwyParamTable *partable,
                                                                  guint i,
                                                                  const GwyParamDefItem *def);
static void                      construct_checkbox_widgets      (GwyParamTable *partable,
                                                                  guint i,
                                                                  const GwyParamDefItem *def);
static void                      data_id_make_control            (GwyParamTable *partable,
                                                                  guint i,
                                                                  const GwyParamDefItem *def);
static void                      curve_no_make_control           (GwyParamTable *partable,
                                                                  guint i,
                                                                  const GwyParamDefItem *def);
static void                      slider_make_control             (GwyParamTable *partable,
                                                                  guint i,
                                                                  const GwyParamDefItem *def);
static void                      alt_make_control                (GwyParamTable *partable,
                                                                  guint i,
                                                                  const GwyParamDefItem *def);
static void                      entry_make_control              (GwyParamTable *partable,
                                                                  guint i,
                                                                  const GwyParamDefItem *def);
static void                      range_from_make_control         (GwyParamTable *partable,
                                                                  guint i,
                                                                  const GwyParamDefItem *def);
static void                      range_to_make_control           (GwyParamTable *partable,
                                                                  guint i,
                                                                  guint ifrom);
static void                      color_make_control              (GwyParamTable *partable,
                                                                  guint i,
                                                                  const GwyParamDefItem *def);
static void                      make_color_button_preset        (GwyParamTable *partable,
                                                                  guint i,
                                                                  guint idx);
static void                      mask_color_make_control         (GwyParamTable *partable,
                                                                  guint i,
                                                                  const GwyParamDefItem *def);
static void                      report_make_control             (GwyParamTable *partable,
                                                                  guint i,
                                                                  const GwyParamDefItem *def);
static void                      hold_selection_make_control     (GwyParamTable *partable,
                                                                  guint i,
                                                                  const GwyParamDefItem *def);
static void                      unit_chooser_make_control       (GwyParamTable *partable,
                                                                  guint i,
                                                                  const GwyParamDefItem *def);
static void                      random_seed_make_control        (GwyParamTable *partable,
                                                                  guint i,
                                                                  const GwyParamDefItem *def);
static void                      button_make_control             (GwyParamTable *partable,
                                                                  guint i);
static void                      results_make_control            (GwyParamTable *partable,
                                                                  guint i);
static void                      message_make_control            (GwyParamTable *partable,
                                                                  guint i);
static void                      info_make_control               (GwyParamTable *partable,
                                                                  guint i);
static void                      foreign_make_control            (GwyParamTable *partable,
                                                                  guint i);
static void                      checkbox_toggled                (GtkToggleButton *toggle,
                                                                  GwyParamTable *partable);
static void                      enabler_toggled                 (GtkToggleButton *toggle,
                                                                  GwyParamTable *partable);
static void                      togglebutton_set_value          (GwyParamControl *control,
                                                                  GwyParamTable *partable,
                                                                  gboolean value,
                                                                  gboolean use_default_instead);
static void                      combo_changed                   (GtkComboBox *combo,
                                                                  GwyParamTable *partable);
static void                      unit_chosen                     (GtkComboBox *combo,
                                                                  GwyParamTable *partable);
static void                      unit_chooser_change             (GtkButton *button,
                                                                  GwyParamTable *partable);
static gchar*                    unit_change_dialog_run          (GtkWindow *parent,
                                                                  const gchar *unitstr);
static void                      enum_combo_set_value            (GwyParamControl *control,
                                                                  GwyParamTable *partable,
                                                                  gint value,
                                                                  gboolean use_default_instead);
static void                      unit_chooser_set_value          (GwyParamControl *control,
                                                                  GwyParamTable *partable,
                                                                  const gchar *value,
                                                                  gboolean use_default_instead);
static void                      resource_combo_set_value        (GwyParamControl *control,
                                                                  GwyParamTable *partable,
                                                                  const gchar *value,
                                                                  gboolean use_default_instead);
static void                      radio_changed                   (GtkRadioButton *radio,
                                                                  GwyParamTable *partable);
static void                      radio_set_value                 (GwyParamControl *control,
                                                                  GwyParamTable *partable,
                                                                  gint value,
                                                                  gboolean use_default_instead);
static void                      checkbox_changed                (GtkToggleButton *toggle,
                                                                  GwyParamTable *partable);
static void                      checkboxes_set_value            (GwyParamControl *control,
                                                                  GwyParamTable *partable,
                                                                  guint value,
                                                                  gboolean use_default_instead);
static void                      graph_id_changed                (GwyDataChooser *chooser,
                                                                  GwyParamTable *partable);
static void                      image_id_changed                (GwyDataChooser *chooser,
                                                                  GwyParamTable *partable);
static void                      volume_id_changed               (GwyDataChooser *chooser,
                                                                  GwyParamTable *partable);
static void                      xyz_id_changed                  (GwyDataChooser *chooser,
                                                                  GwyParamTable *partable);
static void                      curve_map_id_changed            (GwyDataChooser *chooser,
                                                                  GwyParamTable *partable);
static void                      data_id_set_value               (GwyParamControl *control,
                                                                  GwyParamTable *partable,
                                                                  GwyAppDataId value,
                                                                  gboolean use_default_instead);
static void                      graph_curve_changed             (GtkComboBox *combo,
                                                                  GwyParamTable *partable);
static void                      lawn_curve_changed              (GtkComboBox *combo,
                                                                  GwyParamTable *partable);
static void                      lawn_segment_changed            (GtkComboBox *combo,
                                                                  GwyParamTable *partable);
static void                      curve_no_set_value              (GwyParamControl *control,
                                                                  GwyParamTable *partable,
                                                                  gint value,
                                                                  gboolean use_default_instead);
static gint                      curve_no_get_ncurves            (GwyParamControl *control);
static void                      random_seed_changed             (GtkAdjustment *adj,
                                                                  GwyParamTable *partable);
static void                      random_seed_new                 (GtkButton *button,
                                                                  GwyParamTable *partable);
static void                      button_clicked                  (GtkButton *button,
                                                                  GwyParamTable *partable);
static void                      color_set_value                 (GwyParamControl *control,
                                                                  GwyParamTable *partable,
                                                                  GwyRGBA color,
                                                                  gboolean use_default_instead);
static void                      report_format_changed           (GwyResultsExport *rexport,
                                                                  GwyParamTable *partable);
static void                      report_copy                     (GwyResultsExport *rexport,
                                                                  GwyParamTable *partable);
static void                      report_save                     (GwyResultsExport *rexport,
                                                                  GwyParamTable *partable);
static void                      report_set_formatter            (GwyParamControl *control,
                                                                  GwyCreateTextFunc format_report,
                                                                  gpointer user_data,
                                                                  GDestroyNotify destroy);
static void                      report_ensure_actions           (GwyParamControl *control,
                                                                  GwyParamTable *partable);
static void                      report_set_value                (GwyParamControl *control,
                                                                  GwyParamTable *partable,
                                                                  GwyResultsReportType report_type,
                                                                  gboolean use_default_instead);
static void                      hold_selection_set_value        (GwyParamControl *control,
                                                                  GwyParamTable *partable,
                                                                  GwyHoldSelectionFlags value,
                                                                  gboolean use_default_instead);
static void                      hold_selection_changed          (GtkWidget *button,
                                                                  GwyParamTable *partable);
static void                      slider_value_changed            (GtkAdjustment *adj,
                                                                  GwyParamTable *partable);
static gint                      slider_spin_input               (GtkSpinButton *spin,
                                                                  gdouble *new_value,
                                                                  GwyParamTable *partable);
static gboolean                  slider_spin_output              (GtkSpinButton *spin,
                                                                  GwyParamTable *partable);
static void                      slider_set_value                (GwyParamControl *control,
                                                                  GwyParamTable *partable,
                                                                  gdouble value,
                                                                  gboolean use_default_instead);
static void                      slider_make_angle               (GwyParamTable *partable,
                                                                  guint i);
static void                      slider_make_percentage          (GwyParamTable *partable,
                                                                  guint i);
static void                      slider_auto_configure           (GwyParamControl *control,
                                                                  const GwyParamDefItem *def);
static void                      slider_reconfigure_adjustment   (GwyParamControl *control,
                                                                  GwyParamTable *partable);
static void                      slider_reconfigure_alt          (GwyParamControl *control,
                                                                  GwyParamTable *partable);
static void                      slider_set_transformation       (GwyParamTable *partable,
                                                                  guint i,
                                                                  GwyRealFunc value_to_gui,
                                                                  GwyRealFunc gui_to_value,
                                                                  gpointer user_data,
                                                                  GDestroyNotify destroy);
static void                      slider_set_width_chars          (GwyParamTable *partable,
                                                                  guint i);
static void                      alt_set_width_chars             (GwyParamTable *partable,
                                                                  guint i);
static void                      alt_set_from_value_format       (GwyParamTable *partable,
                                                                  gint id,
                                                                  const gchar *unitstr,
                                                                  gdouble raw_q,
                                                                  gdouble raw_offset);
static void                      entry_activated                 (GtkEntry *gtkentry,
                                                                  GwyParamTable *partable);
static void                      entry_changed                   (GtkEntry *gtkentry,
                                                                  GwyParamTable *partable);
static void                      entry_input                     (GtkEntry *gtkentry,
                                                                  GwyParamTable *partable,
                                                                  gboolean is_instant_edit);
static void                      entry_output                    (GwyParamTable *partable,
                                                                  GwyParamControl *control);
static gdouble                   entry_parse_double_vf           (const gchar *text,
                                                                  gchar **end,
                                                                  gpointer user_data);
static const gchar*              entry_format_double_vf          (gdouble value,
                                                                  gpointer user_data);
static void                      string_entry_set_value          (GwyParamControl *control,
                                                                  GwyParamTable *partable,
                                                                  const gchar *value,
                                                                  gboolean use_default_instead);
static void                      int_entry_set_value             (GwyParamControl *control,
                                                                  GwyParamTable *partable,
                                                                  gint value,
                                                                  gboolean use_default_instead);
static void                      double_entry_set_value          (GwyParamControl *control,
                                                                  GwyParamTable *partable,
                                                                  gdouble value,
                                                                  gboolean use_default_instead);
static void                      range_set_value                 (GwyParamControl *control,
                                                                  GwyParamTable *partable,
                                                                  gdouble value,
                                                                  gboolean use_default_instead);
static void                      range_input                     (GtkEntry *gtkentry,
                                                                  GwyParamTable *partable);
static void                      range_output                    (GwyParamTable *partable,
                                                                  GwyParamControl *control);
static void                      range_activated                 (GtkEntry *gtkentry,
                                                                  GwyParamTable *partable);
static GwyParamControlRangeFrom* range_get_impl                  (GwyParamControl *control,
                                                                  GwyParamTable *partable);
static void                      update_control_unit_label       (GwyParamControl *control,
                                                                  GwyParamTable *partable);
static void                      color_run_selector              (GwyColorButton *color_button,
                                                                  GwyParamTable *partable);
static void                      mask_color_run_selector         (GwyColorButton *color_button,
                                                                  GwyParamTable *partable);
static void                      message_update_type             (GwyParamControl *control);
static gboolean                  filter_graph_model              (GwyContainer *data,
                                                                  gint id,
                                                                  gpointer user_data);
static GwyParamControl*          find_button_box_end             (GwyParamTable *partable,
                                                                  GwyParamControl *control,
                                                                  gboolean forward);
static gboolean                  button_box_has_any_sensitive    (GwyParamTable *partable,
                                                                  GwyParamControl *control);
static void                      widget_dispose                  (GtkWidget *widget,
                                                                  GwyParamTable *partable);
static void                      make_control_common             (GwyParamTable *partable,
                                                                  guint i);
static void                      add_separator_as_needed         (GwyParamTable *partable,
                                                                  guint i);
static void                      attach_hbox_row                 (GwyParamTable *partable,
                                                                  gint row,
                                                                  GwyParamControl *control,
                                                                  const gchar *dest);
static GtkWidget*                add_left_padding                (GtkWidget *widget,
                                                                  gint left_pad);
static GtkWidget*                add_right_padding               (GtkWidget *widget,
                                                                  gint right_pad);
static void                      expand_table                    (GwyParamTable *partable);
static gint                      find_control_for_aux            (GwyParamTable *partable,
                                                                  const GwyParamControlAssocTable *assoc_table,
                                                                  gint id);
static gint                      find_aux_for_control            (GwyParamTable *partable,
                                                                  const GwyParamControlAssocTable *assoc_table,
                                                                  gint id);
static GwyToggleListInfo*        find_toggles_info               (GwyParamTable *partable,
                                                                  gint id);
static GwyEnum*                  modify_enum_labels              (GwyParamTable *partable,
                                                                  const GwyEnum *values,
                                                                  guint nvalues,
                                                                  gboolean end_with_colon,
                                                                  gboolean remove_underline);
static const gchar*              modify_label                    (GwyParamTable *partable,
                                                                  const gchar *label,
                                                                  gboolean end_with_colon,
                                                                  gboolean remove_underline);
static GtkWindow*                get_parent_window               (GwyParamTable *partable,
                                                                  gboolean must_be_dialog);
static const gchar*              format_double_vf_common         (gdouble value,
                                                                  GwySIValueFormat *vf,
                                                                  GString *str);
static void                      format_numerical_value          (GString *str,
                                                                  gdouble value,
                                                                  gint digits);
static guint32                   bit_mask_for_enum_value         (const GwyParamDefItem *def,
                                                                  gint value);
static void                      compute_calibration_steps       (GwyDataLine *calibration,
                                                                  gdouble *mean_step,
                                                                  gdouble *min_step);
static gdouble                   calibration_transform_to_gui    (GwyDataLine *calibration,
                                                                  gdouble v);
static gdouble                   calibration_transform_from_gui  (GwyDataLine *calibration,
                                                                  gdouble v);
static const GwyEnum*            guess_standard_stock_ids        (const GwyParamDefItem *def);
static gboolean                  control_has_no_parameter        (GwyParamControlType type);
static gboolean                  control_is_some_kind_of_radio   (GwyParamControlType type);
static gboolean                  control_is_some_kind_of_data_id (GwyParamControlType type);
static gboolean                  control_is_some_kind_of_curve_no(GwyParamControlType type);
static gboolean                  control_can_integrate_enabler   (GwyParamControlType type);
static gboolean                  control_can_integrate_unitstr   (GwyParamControlType type);
static gboolean                  control_has_hbox                (GwyParamControlType type);
static gdouble                   multiply_by_constant            (gdouble v,
                                                                  gpointer user_data);
static gdouble                   divide_by_constant              (gdouble v,
                                                                  gpointer user_data);

static guint signals[N_SIGNALS];

static GQuark param_control_quark = 0;
static GQuark radio_button_quark = 0;
const gchar *colonext = ":";

G_DEFINE_TYPE(GwyParamTable, gwy_param_table, G_TYPE_INITIALLY_UNOWNED);

static void
gwy_param_table_class_init(GwyParamTableClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    const gchar *lang;

    gobject_class->finalize = gwy_param_table_finalize;

    g_type_class_add_private(klass, sizeof(GwyParamTablePrivate));

    /**
     * GwyParamTable::param-changed:
     * @gwyparamtable: The #GwyParamTable which received the signal.
     * @arg1: Identifier of the changed parameter.
     *
     * The ::param-changed signal is emitted when a parameter changes.
     *
     * The signal is not emitted recursively.  In other words, if a signal handler modifies other parameters in
     * response to a parameter change, it is expected to complete all the dependent udpdates.
     */
    signals[SGNL_PARAM_CHANGED]
        = g_signal_new_class_handler("param-changed", G_OBJECT_CLASS_TYPE(klass),
                                     G_SIGNAL_RUN_FIRST, NULL, NULL, NULL,
                                     g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_INT);

    param_control_quark = g_quark_from_static_string("gwy-param-table-control-index");
    /* Must match gwyradiobuttons.c */
    radio_button_quark = g_quark_from_static_string("gwy-radiobuttons-key");

    lang = gwy_sgettext("current-language-code|en");
    if (gwy_stramong(lang, "fr", "fr_FR", "fr_CA", NULL))
        colonext = " :";
}

static void
gwy_param_table_init(GwyParamTable *partable)
{
    GwyParamTablePrivate *priv;

    partable->priv = priv = G_TYPE_INSTANCE_GET_PRIVATE(partable, GWY_TYPE_PARAM_TABLE, GwyParamTablePrivate);
    priv->str = g_string_new(NULL);
    priv->vf = gwy_si_unit_value_format_new(1.0, 0, "");
    priv->ncols = 3;
}

static void
gwy_param_table_finalize(GObject *gobject)
{
    GwyParamTable *partable = (GwyParamTable*)gobject;
    GwyParamTablePrivate *priv = partable->priv;
    guint k, n;

    gwy_debug("table %p finalise start", partable);
    GWY_SIGNAL_HANDLER_DISCONNECT(priv->widget, priv->destroy_sid);
    n = priv->ncontrols;
    for (k = 0; k < n; k++) {
        GwyParamControl *control = priv->controls + k;
        GwyParamControlType type = control->type;

        g_free(control->unitstr);
        g_free(control->label_text);
        if (type == GWY_PARAM_CONTROL_ENABLER)
            g_slice_free(GwyParamControlEnabler, control->impl);
        else if (type == GWY_PARAM_CONTROL_RADIO_ITEM)
            g_slice_free(GwyParamControlRadioItem, control->impl);
        else if (type == GWY_PARAM_CONTROL_RADIO_BUTTONS)
            g_slice_free(GwyParamControlRadioButtons, control->impl);
        else if (type == GWY_PARAM_CONTROL_RANDOM_SEED)
            g_slice_free(GwyParamControlRandomSeed, control->impl);
        else if (type == GWY_PARAM_CONTROL_COMBO) {
            GwyParamControlCombo *combo = (GwyParamControlCombo*)control->impl;
            GWY_OBJECT_UNREF(combo->inventory);
            if (combo->modified_enum)
                gwy_enum_freev(combo->modified_enum);
            if (combo->filter_destroy)
                combo->filter_destroy(combo->filter_data);
            g_slice_free(GwyParamControlCombo, combo);
        }
        else if (type == GWY_PARAM_CONTROL_SLIDER) {
            GwyParamControlSlider *slider = (GwyParamControlSlider*)control->impl;
            if (slider->transform_destroy)
                slider->transform_destroy(slider->transform_data);
            gwy_set_member_object(partable, NULL, GWY_TYPE_DATA_LINE, &slider->calibration, NULL);
            g_free(slider->alt_unitstr);
            g_slice_free(GwyParamControlSlider, slider);
        }
        else if (type == GWY_PARAM_CONTROL_ENTRY) {
            GwyParamControlEntry *entry = (GwyParamControlEntry*)control->impl;
            if (entry->transform_destroy)
                entry->transform_destroy(entry->transform_data);
            else if (entry->vf)
                GWY_SI_VALUE_FORMAT_FREE(entry->vf);
            g_slice_free(GwyParamControlEntry, entry);
        }
        else if (type == GWY_PARAM_CONTROL_RANGE_FROM) {
            GwyParamControlRangeFrom *range = (GwyParamControlRangeFrom*)control->impl;
            if (range->vf)
                GWY_SI_VALUE_FORMAT_FREE(range->vf);
            g_slice_free(GwyParamControlRangeFrom, range);
        }
        else if (type == GWY_PARAM_CONTROL_COLOR) {
            GwyParamControlColor *colour = (GwyParamControlColor*)control->impl;
            if (colour->presets) {
                g_array_free(colour->presets, TRUE);
                colour->presets = NULL;
            }
            g_slice_free(GwyParamControlColor, colour);
        }
        else if (type == GWY_PARAM_CONTROL_MASK_COLOR) {
            GwyParamControlMaskColor *maskcolor = (GwyParamControlMaskColor*)control->impl;
            gwy_debug("sync back mask color to %p, %d? %d", maskcolor->data, maskcolor->i, priv->proceed);
            if (priv->proceed && maskcolor->data && maskcolor->i >= 0) {
                gwy_app_sync_data_items(maskcolor->preview_data, maskcolor->data, maskcolor->preview_i, maskcolor->i,
                                        FALSE, GWY_DATA_ITEM_MASK_COLOR, 0);
            }
            gwy_set_member_object(partable, NULL, GWY_TYPE_CONTAINER, &maskcolor->preview_data, NULL);
            gwy_set_member_object(partable, NULL, GWY_TYPE_CONTAINER, &maskcolor->data, NULL);
            g_slice_free(GwyParamControlMaskColor, maskcolor);
        }
        else if (type == GWY_PARAM_CONTROL_UNIT_CHOOSER)
            g_slice_free(GwyParamControlUnitChooser, control->impl);
        else if (type == GWY_PARAM_CONTROL_BUTTON) {
            GwyParamControlButton *button = (GwyParamControlButton*)control->impl;
            g_free(button->label);
            g_slice_free(GwyParamControlButton, button);
        }
        else if (type == GWY_PARAM_CONTROL_RESULTS) {
            GwyParamControlResults *resultlist = (GwyParamControlResults*)control->impl;
            gwy_set_member_object(partable, NULL, GWY_TYPE_RESULTS, &resultlist->results, NULL);
            g_strfreev(resultlist->result_ids);
            g_slice_free(GwyParamControlResults, resultlist);
        }
        else if (type == GWY_PARAM_CONTROL_REPORT) {
            GwyParamControlReport *report = (GwyParamControlReport*)control->impl;
            gwy_set_member_object(partable, NULL, GWY_TYPE_RESULTS, &report->results, NULL);
            if (report->formatter_destroy)
                report->formatter_destroy(report->formatter_data);
            g_slice_free(GwyParamControlReport, report);
        }
        else if (type == GWY_PARAM_CONTROL_INFO) {
            GwyParamControlInfo *info = (GwyParamControlInfo*)control->impl;
            g_free(info->valuestr);
            g_slice_free(GwyParamControlInfo, info);
        }
        else if (type == GWY_PARAM_CONTROL_MESSAGE)
            g_slice_free(GwyParamControlMessage, control->impl);
        else if (type == GWY_PARAM_CONTROL_FOREIGN) {
            GwyParamControlForeign *foreign = (GwyParamControlForeign*)control->impl;
            if (foreign->destroy)
                foreign->destroy(foreign->user_data);
            g_slice_free(GwyParamControlForeign, foreign);
        }
        else if (control_is_some_kind_of_data_id(type)) {
            GwyParamControlDataChooser *datachooser = (GwyParamControlDataChooser*)control->impl;
            if (datachooser->filter_destroy)
                datachooser->filter_destroy(datachooser->filter_data);
            g_slice_free(GwyParamControlDataChooser, datachooser);
        }
        else if (control_is_some_kind_of_curve_no(type)) {
            GwyParamControlCurveChooser *curvechooser = (GwyParamControlCurveChooser*)control->impl;
            gwy_set_member_object(partable, NULL, GWY_TYPE_SERIALIZABLE, &curvechooser->parent, NULL);
            g_slice_free(GwyParamControlCurveChooser, curvechooser);
        }
    }
    g_free(priv->controls);
    g_free(priv->enabler.assoc);
    g_free(priv->range.assoc);
    g_free(priv->toggles_info);
    g_string_free(priv->str, TRUE);
    GWY_SI_VALUE_FORMAT_FREE(priv->vf);

    GWY_OBJECT_UNREF(priv->params);
    G_OBJECT_CLASS(gwy_param_table_parent_class)->finalize(gobject);
}

/**
 * gwy_param_table_new:
 * @params: A parameter value set.
 *
 * Creates a new table of parameter value controls.
 *
 * The parameter table manages a set of widgets but it is not a widget.  Obtain the widget using
 * gwy_param_table_widget().
 *
 * The created object is initially unowned.  Usually you use gwy_dialog_add_param_table() and then #GwyDialog will
 * assume ownership.  However, if you use #GwyParamTable standalone you should take ownership yourself with
 * g_object_ref_sink() and then release it with g_object_unref() when done.
 *
 * Returns: A newly created set of parameter value controls.
 *
 * Since: 2.59
 **/
GwyParamTable*
gwy_param_table_new(GwyParams *params)
{
    GwyParamTable *partable;

    g_assert(GWY_IS_PARAMS(params));
    partable = g_object_new(GWY_TYPE_PARAM_TABLE, NULL);
    partable->priv->params = g_object_ref(params);

    return partable;
}

/**
 * gwy_param_table_params:
 * @partable: A parameter table.
 *
 * Gets the parameter value set for a parameter table.
 *
 * Returns: The parameter value set.
 *
 * Since: 2.59
 **/
GwyParams*
gwy_param_table_params(GwyParamTable *partable)
{
    g_return_val_if_fail(GWY_IS_PARAM_TABLE(partable), NULL);
    return partable->priv->params;
}

/**
 * gwy_param_table_widget:
 * @partable: A parameter table.
 *
 * Gets and possibly constructs the parameter controls.
 *
 * If the widget already exists this function returns the existing widget.  Otherwise the widget is created.
 *
 * The returned widget is a table-like widget with implementation-defined type and structure.  It can be added as a
 * child to other widgets, show, hidden, made insensitive or destroyed.  However, its individual pieces must not be
 * manipulated outside the #GwyParamTable functions.
 *
 * It is more efficient to get the widget after the controls for all parameters were added with functions like
 * gwy_param_table_append_checkbox() and gwy_param_table_append_combo().  It is also safer with respect to inadvertent
 * parameter modifications as widgets are created after the setup is done.  The opposite order is possible but may not
 * work as expected in complex cases.
 *
 * Returns: Widget with all the parameter controls.  The widget is owned by @partable and you do not receive a new
 *          reference (not even a floating one).
 *
 * Since: 2.59
 **/
GtkWidget*
gwy_param_table_widget(GwyParamTable *partable)
{
    g_return_val_if_fail(GWY_IS_PARAM_TABLE(partable), NULL);
    return ensure_widget(partable);
}

/**
 * gwy_param_table_reset:
 * @partable: A parameter table.
 *
 * Resets all parameters in a parameter table to defaults.
 *
 * Foreign controls are not reset; this has to be done extra. Controls with the no-reset flag set with
 * gwy_param_table_set_no_reset() are not reset either.
 *
 * The parameters are reset in the order they were added to the table. In a #GwyDialog with multiple tables they are
 * reset in the order they were added to the dialog. In complex tables when valid values of one parameter depend on
 * the value of another this may not be sufficient for a well-defined reset. See the introductory section of
 * #GwyDialog for discussion.
 *
 * The entire update will emit at most one signal (with id equal to -1).
 *
 * Since: 2.59
 **/
void
gwy_param_table_reset(GwyParamTable *partable)
{
    GwyRGBA rgba = { 0.0, 0.0, 0.0, 0.0 };
    GwyParamTablePrivate *priv;
    guint n, k;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    gwy_debug("reset started");
    _gwy_param_table_in_update(partable, TRUE);
    priv = partable->priv;
    n = priv->ncontrols;
    for (k = 0; k < n; k++) {
        GwyParamControl *control = priv->controls + k;
        GwyParamControlType type = control->type;

        if (control->do_not_reset)
            continue;
        if (control_has_no_parameter(type))
            continue;

        if (type == GWY_PARAM_CONTROL_CHECKBOX || type == GWY_PARAM_CONTROL_ENABLER)
            togglebutton_set_value(control, partable, FALSE, TRUE);
        else if (type == GWY_PARAM_CONTROL_COMBO) {
            GwyParamControlCombo *combo = (GwyParamControlCombo*)control->impl;
            if (combo->is_resource)
                resource_combo_set_value(control, partable, NULL, TRUE);
            else
                enum_combo_set_value(control, partable, 0, TRUE);
        }
        else if (type == GWY_PARAM_CONTROL_UNIT_CHOOSER)
            unit_chooser_set_value(control, partable, NULL, TRUE);
        else if (control_is_some_kind_of_radio(type))
            radio_set_value(control, partable, 0, TRUE);
        else if (type == GWY_PARAM_CONTROL_CHECKBOXES)
            checkboxes_set_value(control, partable, 0, TRUE);
        else if (type == GWY_PARAM_CONTROL_SLIDER)
            slider_set_value(control, partable, 0.0, TRUE);
        else if (type == GWY_PARAM_CONTROL_ENTRY) {
            GwyParamControlEntry *entry = (GwyParamControlEntry*)control->impl;
            if (entry->is_numeric) {
                if (entry->is_int)
                    int_entry_set_value(control, partable, 0, TRUE);
                else
                    double_entry_set_value(control, partable, 0.0, TRUE);
            }
            else
                string_entry_set_value(control, partable, "", TRUE);
        }
        else if (control_is_some_kind_of_data_id(control->type)) {
            GwyAppDataId noid = GWY_APP_DATA_ID_NONE;
            data_id_set_value(control, partable, noid, TRUE);
        }
        else if (control_is_some_kind_of_curve_no(control->type))
            curve_no_set_value(control, partable, 0, TRUE);
        else if (type == GWY_PARAM_CONTROL_COLOR)
            color_set_value(control, partable, rgba, TRUE);
        else if (type == GWY_PARAM_CONTROL_MASK_COLOR)
            color_set_value(control, partable, rgba, TRUE);
        else if (type == GWY_PARAM_CONTROL_REPORT)
            report_set_value(control, partable, 0, TRUE);
        else if (type == GWY_PARAM_CONTROL_HOLD_SELECTION)
            hold_selection_set_value(control, partable, 0, TRUE);
        else {
            g_critical("Unhandled control type %d.", type);
        }
    }
    _gwy_param_table_in_update(partable, FALSE);
    gwy_debug("reset finished");
}

/**
 * gwy_param_table_set_from_params:
 * @partable: A parameter table.
 * @params: A parameter value set.
 *
 * Set all parameters in a parameter table to values from a parameter set.
 *
 * The parameter value set must be compatible with the table's one, including ids, names and types. It can be
 * a subset.
 *
 * Unlike gwy_param_table_reset(), this function sets also parameters with the no-reset flag. In the typical case,
 * they would not be included in @params anyway. If they are, update them to the current values beforehand.
 *
 * The parameters are set in the order they were added to the table. In the case of complex inter-parameter
 * dependencies some parameter may have to be set manually beforehand for the entire process to work as intended.
 *
 * The entire update will emit at most one signal (with id equal to -1).
 *
 * Since: 2.62
 **/
void
gwy_param_table_set_from_params(GwyParamTable *partable,
                                GwyParams *params)
{
    GwyParamTablePrivate *priv;
    GwyParamDef *srcpardef, *pardef;
    guint n, k;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    g_return_if_fail(GWY_IS_PARAMS(params));
    srcpardef = gwy_params_get_def(params);
    g_return_if_fail(GWY_IS_PARAM_DEF(srcpardef));
    priv = partable->priv;
    pardef = gwy_params_get_def(priv->params);

    gwy_debug("set_from_params started");
    _gwy_param_table_in_update(partable, TRUE);
    n = priv->ncontrols;
    for (k = 0; k < n; k++) {
        GwyParamControl *control = priv->controls + k;
        GwyParamControlType type = control->type;
        gint id = control->id;
        const GwyParamDefItem *srcdef, *def;

        if (id < 0 || control_has_no_parameter(type))
            continue;
        if (!(srcdef = _gwy_param_def_item(srcpardef, _gwy_param_def_index(srcpardef, id))))
            continue;
        def = _gwy_param_def_item(pardef, _gwy_param_def_index(pardef, id));
        g_return_if_fail(def);

        if (def->type != srcdef->type) {
            g_warning("Destination parameter %d type is %d, but source's is %d.", id, def->type, srcdef->type);
            continue;
        }
        if (srcdef->name && def->name && !gwy_strequal(srcdef->name, def->name)) {
            g_warning("Destination parameter %d is called %s, but source's is %s.", id, def->name, srcdef->name);
            continue;
        }

        if (type == GWY_PARAM_CONTROL_CHECKBOX || type == GWY_PARAM_CONTROL_ENABLER)
            togglebutton_set_value(control, partable, gwy_params_get_boolean(params, id), FALSE);
        else if (type == GWY_PARAM_CONTROL_COMBO) {
            GwyParamControlCombo *combo = (GwyParamControlCombo*)control->impl;
            if (combo->is_resource)
                resource_combo_set_value(control, partable, gwy_params_get_string(params, id), FALSE);
            else
                enum_combo_set_value(control, partable, gwy_params_get_enum(params, id), FALSE);
        }
        else if (type == GWY_PARAM_CONTROL_UNIT_CHOOSER)
            unit_chooser_set_value(control, partable, gwy_params_get_string(params, id), FALSE);
        else if (control_is_some_kind_of_radio(type))
            radio_set_value(control, partable, gwy_params_get_enum(params, id), FALSE);
        else if (type == GWY_PARAM_CONTROL_CHECKBOXES)
            checkboxes_set_value(control, partable, gwy_params_get_flags(params, id), FALSE);
        else if (type == GWY_PARAM_CONTROL_SLIDER) {
            GwyParamControlSlider *slider = (GwyParamControlSlider*)control->impl;
            if (slider->is_int)
                slider_set_value(control, partable, gwy_params_get_int(params, id), FALSE);
            else
                slider_set_value(control, partable, gwy_params_get_double(params, id), FALSE);
        }
        else if (type == GWY_PARAM_CONTROL_ENTRY) {
            GwyParamControlEntry *entry = (GwyParamControlEntry*)control->impl;
            if (entry->is_numeric) {
                if (entry->is_int)
                    int_entry_set_value(control, partable, gwy_params_get_int(params, id), FALSE);
                else
                    double_entry_set_value(control, partable, gwy_params_get_double(params, id), FALSE);
            }
            else
                string_entry_set_value(control, partable, gwy_params_get_string(params, id), FALSE);
        }
        else if (type == GWY_PARAM_CONTROL_RANGE_FROM)
            range_set_value(control, partable, gwy_params_get_double(params, id), FALSE);
        else if (type == GWY_PARAM_CONTROL_RANGE_TO)
            range_set_value(control, partable, gwy_params_get_double(params, id), FALSE);
        else if (control_is_some_kind_of_data_id(control->type)) {
            data_id_set_value(control, partable, gwy_params_get_data_id(params, id), FALSE);
        }
        else if (control_is_some_kind_of_curve_no(control->type))
            curve_no_set_value(control, partable, gwy_params_get_int(params, id), FALSE);
        else if (type == GWY_PARAM_CONTROL_COLOR || type == GWY_PARAM_CONTROL_MASK_COLOR)
            color_set_value(control, partable, gwy_params_get_color(params, id), FALSE);
        else if (type == GWY_PARAM_CONTROL_REPORT)
            report_set_value(control, partable, gwy_params_get_report_type(params, id), FALSE);
        else if (type == GWY_PARAM_CONTROL_HOLD_SELECTION)
            hold_selection_set_value(control, partable, gwy_params_get_flags(params, id), FALSE);
        else {
            g_critical("Unhandled control type %d.", type);
        }
    }
    _gwy_param_table_in_update(partable, FALSE);
    gwy_param_table_param_changed(partable, -1);
    gwy_debug("set_from_params finished");
}

/**
 * gwy_param_table_exists:
 * @partable: A parameter table.
 * @id: Parameter identifier.
 *
 * Tests if a parameter table has controls for a parameter.
 *
 * Returns: %TRUE if the table has controls for parameter @id, %FALSE if it does not.
 *
 * Since: 2.59
 **/
gboolean
gwy_param_table_exists(GwyParamTable *partable,
                       gint id)
{
    g_return_val_if_fail(GWY_IS_PARAM_TABLE(partable), FALSE);
    return !!find_first_control(partable, id);
}

/**
 * gwy_param_table_param_changed:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 *
 * Emits the param-changed signal for a parameter table.
 *
 * This function is rarely needed because the parameter table emits this signal itself.  It can be occassionally
 * useful for parameters which are not managed by @partable and it cannot tell that they have changed.  If you want to
 * integrate them, i.e. treat more or less as if they were managed by @partable, you should probably use this function
 * when they change.
 *
 * Signal recursion is prevented.  If this function is called from within a #GwyParamTable::param-changed handler it
 * just immediately returns.  Similarly, if the signal is really emitted and other parameters change as the result,
 * their changes no longer cause any recursive #GwyParamTable::param-changed.
 *
 * The value of @id may correspond to a parameter #GwyParamTable has no knowledge of.  So it is in effect an arbitrary
 * integer.
 *
 * Since: 2.59
 **/
void
gwy_param_table_param_changed(GwyParamTable *partable,
                              gint id)
{
    GwyParamTablePrivate *priv;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    priv = partable->priv;
    if (priv->in_update)
        return;

    if (priv->parent_dialog) {
        /* We are inside a GwyDialog. */
        _gwy_dialog_param_table_update_started(priv->parent_dialog);
        g_signal_emit(partable, signals[SGNL_PARAM_CHANGED], 0, id);
        _gwy_dialog_param_table_update_finished(priv->parent_dialog);
    }
    else if (priv->parent_tool) {
        /* We are inside a tool dialog which supports ParamTables to a degree. */
        _gwy_plain_tool_param_table_update_started(priv->parent_tool);
        g_signal_emit(partable, signals[SGNL_PARAM_CHANGED], 0, id);
        _gwy_plain_tool_param_table_update_finished(priv->parent_tool);
    }
    else {
        /* A brave soul is using us standalone. */
        _gwy_param_table_in_update(partable, TRUE);
        g_signal_emit(partable, signals[SGNL_PARAM_CHANGED], 0, id);
        _gwy_param_table_in_update(partable, FALSE);
    }
}

/**
 * gwy_param_table_set_no_reset:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @setting: %TRUE to set the no-reset flag, %FALSE to unset it.
 *
 * Sets the no-reset flag for a parameter in a parameter table.
 *
 * No-reset parameters are untouched by gwy_param_table_reset().  Hence they are not reset by the #GwyDialog reset
 * response handler either.  This can be useful for parameters that do not have static default values and need special
 * treatment during the reset.
 *
 * Some parameters are no-reset by default: for instance data ids or mask colour.
 *
 * Since: 2.59
 **/
void
gwy_param_table_set_no_reset(GwyParamTable *partable,
                             gint id,
                             gboolean setting)
{
    gwy_param_table_set_no_resetv(partable, &id, 1, setting);
}

/**
 * gwy_param_table_set_no_resetv:
 * @partable: Set of parameter value controls.
 * @ids: Array of parameter identifiers.
 * @n: Number of items in @ids.
 * @setting: %TRUE to set the no-reset flag, %FALSE to unset it.
 *
 * Sets the no-reset flag for multiple parameters in a parameter table.
 *
 * See gwy_param_table_set_no_reset() for description.
 *
 * Since: 2.62
 **/
void
gwy_param_table_set_no_resetv(GwyParamTable *partable,
                              const gint *ids,
                              gint n,
                              gboolean setting)
{
    GwyParamControl *controls;
    GwyParamControlType type;
    guint nc, k, j;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    if (!n)
        return;
    g_return_if_fail(ids);
    controls = partable->priv->controls;
    nc = partable->priv->ncontrols;
    for (k = 0; k < nc; k++) {
        for (j = 0; j < n; j++) {
            if (controls[k].id == ids[j]) {
                type = controls[k].type;
                if (control_has_no_parameter(type)) {
                    g_warning("Setting no-reset makes no sense as control %d has no actual parameter.", ids[j]);
                    continue;
                }
                controls[k].do_not_reset = setting;
                break;
            }
        }
    }
}

/**
 * gwy_param_table_set_sensitive:
 * @partable: A parameter table.
 * @id: Parameter identifier.
 * @sensitive: %TRUE to make the control sensitive, %FALSE to make it insensitive.
 *
 * Sets the sensitivity of a single control in a parameter table.
 *
 * If the parameter identified by @id corresponds to multiple widgets all are set sensitive or insensitive as
 * requested.  In most cases this is the right function to enable and disable parameters.  However, some may have to
 * be managed in more detail.
 *
 * For radio buttons this function enables/disables the entire button group, including any header label.  Use
 * gwy_param_table_radio_set_sensitive() to set sensitivity of individual radio buttons.
 *
 * Since: 2.59
 **/
void
gwy_param_table_set_sensitive(GwyParamTable *partable,
                              gint id,
                              gboolean sensitive)
{
    GwyParamControl *controls;
    GwyToggleListInfo *toggles_info;
    guint k, n;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    controls = partable->priv->controls;
    n = partable->priv->ncontrols;
    for (k = 0; k < n; k++) {
        if (controls[k].id == id) {
            if ((toggles_info = find_toggles_info(partable, id))) {
                if (!toggles_info->sensitive == !sensitive)
                    continue;
                toggles_info->sensitive = !!sensitive;
            }
            else {
                if (!controls[k].sensitive == !sensitive)
                    continue;
            }
            controls[k].sensitive = !!sensitive;
            if (controls[k].widget || controls[k].label)
                update_control_sensitivity(partable, k);
        }
    }
}

/**
 * gwy_param_table_set_boolean:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @value: The new value.
 *
 * Sets the value of a boolean parameter in a parameter table.
 *
 * The parameter identified by @id must be a boolean defined by gwy_param_def_add_boolean() or a predefined boolean.
 *
 * Since: 2.59
 **/
void
gwy_param_table_set_boolean(GwyParamTable *partable,
                            gint id,
                            gboolean value)
{
    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    togglebutton_set_value(find_first_control(partable, id), partable, value, FALSE);
}

/**
 * gwy_param_table_set_int:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @value: The new value.
 *
 * Sets the value of an integer parameter in a parameter table.
 *
 * The parameter identified by @id must be an integer parameter defined by gwy_param_def_add_int() or a predefined
 * integer parameter.
 *
 * Since: 2.59
 **/
void
gwy_param_table_set_int(GwyParamTable *partable,
                        gint id,
                        gint value)
{
    GwyParamControl *control;
    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    control = find_first_control(partable, id);
    g_return_if_fail(control);
    if (control->type == GWY_PARAM_CONTROL_SLIDER)
        slider_set_value(control, partable, value, FALSE);
    else if (control->type == GWY_PARAM_CONTROL_ENTRY)
        int_entry_set_value(control, partable, value, FALSE);
    else {
        g_assert_not_reached();
    }
}

/**
 * gwy_param_table_set_double:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @value: The new value.
 *
 * Sets the value of a double parameter in a parameter table.
 *
 * The parameter identified by @id must be an floating point parameter defined by gwy_param_def_add_double() or
 * a predefined floating point parameter.
 *
 * Since: 2.59
 **/
void
gwy_param_table_set_double(GwyParamTable *partable,
                           gint id,
                           gdouble value)
{
    GwyParamControl *control;
    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    control = find_first_control(partable, id);
    g_return_if_fail(control);
    if (control->type == GWY_PARAM_CONTROL_SLIDER)
        slider_set_value(control, partable, value, FALSE);
    else if (control->type == GWY_PARAM_CONTROL_ENTRY)
        double_entry_set_value(control, partable, value, FALSE);
    else if (control->type == GWY_PARAM_CONTROL_RANGE_FROM
             || control->type == GWY_PARAM_CONTROL_RANGE_TO)
        range_set_value(control, partable, value, FALSE);
    else {
        g_assert_not_reached();
    }
}

/**
 * gwy_param_table_set_enum:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @value: The new value.
 *
 * Sets the value of an enum parameter in a parameter table.
 *
 * The parameter identified by @id must be a custom enum defined by gwy_param_def_add_gwyenum() or predefined enum set
 * up by gwy_param_def_add_enum().
 *
 * Since: 2.59
 **/
void
gwy_param_table_set_enum(GwyParamTable *partable,
                         gint id,
                         gint value)
{
    GwyParamControl *control;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    control = find_first_control(partable, id);
    g_return_if_fail(control);
    if (control->type == GWY_PARAM_CONTROL_COMBO)
        enum_combo_set_value(control, partable, value, FALSE);
    else if (control_is_some_kind_of_radio(control->type))
        radio_set_value(control, partable, value, FALSE);
    else {
        g_assert_not_reached();
    }
}

/**
 * gwy_param_table_set_flags:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @value: The new value.
 *
 * Sets the value of an flags parameter in a parameter table.
 *
 * Since: 2.59
 **/
void
gwy_param_table_set_flags(GwyParamTable *partable,
                          gint id,
                          guint value)
{
    GwyParamControl *control;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    control = find_first_control(partable, id);
    g_return_if_fail(control);
    if (control->type == GWY_PARAM_CONTROL_CHECKBOXES)
        checkboxes_set_value(control, partable, value, FALSE);
    else if (control->type == GWY_PARAM_CONTROL_HOLD_SELECTION)
        hold_selection_set_value(control, partable, value, FALSE);
    else {
        g_assert_not_reached();
    }
}

/**
 * gwy_param_table_set_string:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @value: The new value.
 *
 * Sets the value of a string parameter.
 *
 * The function can be used with string entries, unit choosers and resource combos.
 *
 * Since: 2.59
 **/
void
gwy_param_table_set_string(GwyParamTable *partable,
                           gint id,
                           const gchar *value)
{
    GwyParamControl *control;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    control = find_first_control(partable, id);
    g_return_if_fail(control);
    if (control->type == GWY_PARAM_CONTROL_COMBO) {
        GwyParamControlCombo *combo = (GwyParamControlCombo*)control->impl;
        g_return_if_fail(combo->inventory);
        resource_combo_set_value(control, partable, value, FALSE);
    }
    else if (control->type == GWY_PARAM_CONTROL_UNIT_CHOOSER) {
        unit_chooser_set_value(control, partable, value, FALSE);
    }
    else if (control->type == GWY_PARAM_CONTROL_ENTRY) {
        GwyParamControlEntry *entry = (GwyParamControlEntry*)control->impl;
        g_return_if_fail(!entry->is_numeric);
        string_entry_set_value(control, partable, value, FALSE);
    }
    else {
        g_return_if_reached();
    }
}

/**
 * gwy_param_table_set_data_id:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @value: The new value.
 *
 * Sets the value of a data identifier parameter in a parameter table.
 *
 * Since: 2.59
 **/
void
gwy_param_table_set_data_id(GwyParamTable *partable,
                            gint id,
                            GwyAppDataId value)
{
    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    data_id_set_value(find_first_control(partable, id), partable, value, FALSE);
}

/**
 * gwy_param_table_set_curve:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @value: The new value.
 *
 * Sets the value of a curve number parameter in a parameter table.
 *
 * Since: 2.60
 **/
void
gwy_param_table_set_curve(GwyParamTable *partable,
                          gint id,
                          gint value)
{
    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    curve_no_set_value(find_first_control(partable, id), partable, value, FALSE);
}

/**
 * gwy_param_table_append_header:
 * @partable: A parameter table.
 * @id: Parameter identifier, but usually -1.
 * @label: Header text.  It should be in Header Capitalisation Style.
 *
 * Adds a control group header to a parameter table.
 *
 * Headers can be used to visually separate parameters into groups.  They are typeset to stand out and have extra
 * space before them.
 *
 * If @id is supplied it must be unique, different from all parameter identifiers.  This is best achieved by taking
 * them all from the same enum.  It is only useful for changing the header text or sensitivity later.
 *
 * Since: 2.59
 **/
void
gwy_param_table_append_header(GwyParamTable *partable,
                              gint id,
                              const gchar *label)
{
    GwyParamControl *control;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    g_return_if_fail(label);
    control = append_control(partable, GWY_PARAM_CONTROL_HEADER, id, 1);
    gwy_assign_string(&control->label_text, label);
    if (partable->priv->widget)
        header_make_control(partable, partable->priv->ncontrols-1);
}

/**
 * gwy_param_table_append_separator:
 * @partable: A parameter table.
 *
 * Adds a separator to a parameter table.
 *
 * Separator adds some extra space between parameters and can be used for visual grouping.  Control group headers
 * added by gwy_param_table_append_header() have extra space before them added automatically.
 *
 * Since: 2.59
 **/
void
gwy_param_table_append_separator(GwyParamTable *partable)
{
    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    append_control(partable, GWY_PARAM_CONTROL_SEPARATOR, -1, 0);
    /* There is no immediate widget effect.  We modify the the spacing when we attach the next row, if any. */
}

/**
 * gwy_param_table_set_unitstr:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier of the control (not a new identifier).
 * @unitstr: Units for the control (with Pango markup).
 *
 * Sets a fixed units label for a control in a parameter table.
 *
 * Unit labels are added to the right of the main parameter control.  Usually they are used for sliders, although they
 * can be set for most parameter controls except separators, enablers and vertical radio button lists.
 *
 * Since: 2.59
 **/
void
gwy_param_table_set_unitstr(GwyParamTable *partable,
                            gint id,
                            const gchar *unitstr)
{
    GwyParamControl *control;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    control = find_first_control(partable, id);
    g_return_if_fail(control && control_can_integrate_unitstr(control->type));
    if (!gwy_assign_string(&control->unitstr, unitstr))
        return;
    if (control->widget)
        update_control_unit_label(control, partable);
}

/**
 * gwy_param_table_append_checkbox:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 *
 * Adds a checkbox to a parameter table.
 *
 * The parameter identified by @id must be a boolean defined by gwy_param_def_add_boolean() or a predefined boolean.
 *
 * The parameter must have a description which will be used as the label.
 *
 * See also gwy_param_table_add_enabler() for a checkbox integrated into another control and
 * gwy_param_table_append_checkboxes() for a set of flags presented as checkboxes.
 *
 * Since: 2.59
 **/
void
gwy_param_table_append_checkbox(GwyParamTable *partable,
                                gint id)
{
    GwyParams *params;
    const GwyParamDefItem *def;

    if (!find_def_common(partable, id, &params, &def))
        return;
    g_return_if_fail(def->type == GWY_PARAM_BOOLEAN);
    append_control(partable, GWY_PARAM_CONTROL_CHECKBOX, id, 1);
    if (partable->priv->widget)
        checkbox_make_control(partable, partable->priv->ncontrols-1, def);
}

/**
 * gwy_param_table_add_enabler:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @other_id: Identifier of parameter to enable/distable.
 *
 * Adds checkbox which enables and disables other parameter in a parameter table.
 *
 * The parameter identified by @id must be a boolean defined by gwy_param_def_add_boolean() or a predefined boolean.
 *
 * The parameter identified by @other_id must be added as combo box, data chooser, slider or radio button row (text
 * or image buttons).  The check box will then be integrated in the control of that parameter.  If you have a generic
 * enable/disable parameter with its own standalone checkbox use gwy_param_table_append_checkbox() instead (and set
 * widget sensitivity using gwy_param_table_set_sensitive() in the #GwyParamTable::param-changed signal handler).
 *
 * Since: 2.59
 **/
void
gwy_param_table_add_enabler(GwyParamTable *partable,
                            gint id,
                            gint other_id)
{
    GwyParamTablePrivate *priv;
    GwyParamControl *control, *other_control;
    GwyParamControlEnabler *enabler;
    GwyParams *params;
    const GwyParamDefItem *def;

    if (!find_def_common(partable, id, &params, &def))
        return;
    priv = partable->priv;
    g_return_if_fail(def->type == GWY_PARAM_BOOLEAN);
    other_control = find_first_control(partable, other_id);
    if (other_control) {
        g_return_if_fail(control_can_integrate_enabler(other_control->type));
        if (find_aux_for_control(partable, &priv->enabler, other_id) >= 0) {
            g_warning("Control for parameter id %d already has an enabler.", other_id);
            return;
        }
    }
    control = append_control(partable, GWY_PARAM_CONTROL_ENABLER, id, 0);
    control->impl = enabler = g_slice_new0(GwyParamControlEnabler);
    add_aux_assoc(&priv->enabler, id, other_id);
    if (priv->widget && other_control && other_control->widget) {
        enabler_make_control(partable, priv->ncontrols-1, other_control - priv->controls);
        update_control_sensitivity(partable, priv->ncontrols-1);
    }
}

/**
 * gwy_param_table_append_combo:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 *
 * Adds a combo box to a parameter table.
 *
 * The parameter identified by @id must be either an enum or a resource.  Custom enums are defined by
 * gwy_param_def_add_gwyenum(); predefined enum are set up by gwy_param_def_add_enum(). Resource names are defined by
 * gwy_param_def_add_resource().
 *
 * Since: 2.59
 **/
void
gwy_param_table_append_combo(GwyParamTable *partable,
                             gint id)
{
    GwyParamControl *control;
    GwyParamControlCombo *combo;
    GwyParams *params;
    const GwyParamDefItem *def;

    if (!find_def_common(partable, id, &params, &def))
        return;
    g_return_if_fail(def->type == GWY_PARAM_ENUM || def->type == GWY_PARAM_RESOURCE_NAME);
    control = append_control(partable, GWY_PARAM_CONTROL_COMBO, id, 1);
    control->impl = combo = g_slice_new0(GwyParamControlCombo);
    if (def->type == GWY_PARAM_ENUM) {
        combo->modified_enum = modify_enum_labels(partable, def->def.e.table, def->def.e.nvalues, FALSE, TRUE);
        combo->inventory = gwy_enum_inventory_new(combo->modified_enum ? combo->modified_enum : def->def.e.table,
                                                  def->def.e.nvalues);
    }
    else {
        combo->inventory = g_object_ref(def->def.res.inventory);
        combo->is_resource = TRUE;
    }
    if (partable->priv->widget)
        combo_make_control(partable, partable->priv->ncontrols-1, def);
}

/**
 * gwy_param_table_combo_set_filter:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @filter: The filter function.
 * @user_data: The data passed to @filter.
 * @destroy: Destroy notifier of @user_data or %NULL.
 *
 * The parameter identified by @id must be a resource defined by a function such as gwy_param_def_add_resource().
 *
 * Setting the filter to a different one automatically refilters the data chooser.
 *
 * Since: 2.59
 **/
void
gwy_param_table_combo_set_filter(GwyParamTable *partable,
                                 gint id,
                                 GwyEnumFilterFunc filter,
                                 gpointer user_data,
                                 GDestroyNotify destroy)
{
    GwyParamControl *control;
    GwyParamControlCombo *combo;
    GDestroyNotify old_destroy;
    gpointer old_data;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    control = find_first_control(partable, id);
    g_return_if_fail(control && control->type == GWY_PARAM_CONTROL_COMBO);

    combo = (GwyParamControlCombo*)control->impl;
    old_destroy = combo->filter_destroy;
    old_data = combo->filter_data;
    combo->filter_func = filter;
    combo->filter_data = user_data;
    combo->filter_destroy = destroy;
    gwy_param_table_combo_refilter(partable, id);
    if (old_destroy)
        old_destroy(old_data);
}

/**
 * gwy_param_table_combo_refilter:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 *
 * Requests refiltering of choices in a resource combo in a parameter table.
 *
 * The parameter identified by @id must be a resource defined gwy_param_def_add_resource().
 *
 * It is possible to call this function when the table widget does not exist yet.
 *
 * Since: 2.59
 **/
/* XXX: Currently only works for resource combos! */
void
gwy_param_table_combo_refilter(GwyParamTable *partable,
                               gint id)
{
    GwyParamControl *control;
    GwyParamControlCombo *combo;
    GtkComboBox *combobox;
    GtkTreeModel *model, *childmodel;
    GtkTreeIter iter, childiter;
    GtkTreeModelFilter *filtermodel;
    GwyResource *resource = NULL;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    control = find_first_control(partable, id);
    g_return_if_fail(control && control->type == GWY_PARAM_CONTROL_COMBO);
    combo = (GwyParamControlCombo*)control->impl;
    g_return_if_fail(combo->filter_func);

    if (!control->widget)
        return;

    combobox = GTK_COMBO_BOX(control->widget);
    model = gtk_combo_box_get_model(combobox);
    g_return_if_fail(GTK_IS_TREE_MODEL_FILTER(model));
    filtermodel = GTK_TREE_MODEL_FILTER(model);
    /* Do not refilter combo boxes live.  Temporarily set the model to NULL.  See datachooser.c. */
    g_object_ref(model);
    if (gtk_combo_box_get_active_iter(combobox, &iter))
        gtk_tree_model_get(model, &iter, 0, &resource, -1);
    gtk_combo_box_set_model(combobox, NULL);
    gtk_tree_model_filter_refilter(filtermodel);
    gtk_combo_box_set_model(combobox, model);
    g_object_unref(model);

    if (resource
        && (childmodel = gtk_tree_model_filter_get_model(filtermodel))
        && gwy_inventory_store_get_iter(GWY_INVENTORY_STORE(childmodel), gwy_resource_get_name(resource), &childiter)
        && gtk_tree_model_filter_convert_child_iter_to_iter(filtermodel, &iter, &childiter))
        gtk_combo_box_set_active_iter(combobox, &iter);

    /* Try to ensure something valid is selected after the refiltering. */
    if (gtk_combo_box_get_active_iter(combobox, &iter))
        return;

    resource_combo_set_value(control, partable, NULL, TRUE);
    if (gtk_combo_box_get_active_iter(combobox, &iter))
        return;

    if (gtk_tree_model_get_iter_first(model, &iter))
        gtk_combo_box_set_active_iter(combobox, &iter);
}

/**
 * gwy_param_table_append_radio:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 *
 * Adds a set of radio buttons to a parameter table as a vertical list.
 *
 * The parameter identified by @id must be a custom enum defined by gwy_param_def_add_gwyenum() or predefined enum
 * set up by gwy_param_def_add_enum().
 *
 * If the parameter has a description it will be used as the radio button list header.  Otherwise the buttons will be
 * free-standing.
 *
 * Use gwy_param_table_append_radio_header() and gwy_param_table_append_radio_item() if you need to construct the list
 * piecewise, for instance interspersing the radio buttons with other controls.  Use
 * gwy_param_table_append_radio_row() for a compact one-row list and gwy_param_table_append_radio_buttons() for a
 * one-row list of image buttons.
 *
 * Since: 2.59
 **/
void
gwy_param_table_append_radio(GwyParamTable *partable,
                             gint id)
{
    GwyParams *params;
    const GwyParamDefItem *def;

    if (!find_def_common(partable, id, &params, &def))
        return;
    g_return_if_fail(def->type == GWY_PARAM_ENUM);
    g_warn_if_fail(def->def.e.nvalues < 32);
    append_control(partable, GWY_PARAM_CONTROL_RADIO, id, (def->desc ? 1 : 0) + def->def.e.nvalues);
    add_toggles_info(partable, id, TRUE);
    if (partable->priv->widget)
        radio_make_control(partable, partable->priv->ncontrols-1, def);
}

/**
 * gwy_param_table_append_radio_header:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 *
 * Adds a list header for radio buttons to a parameter table.
 *
 * The parameter identified by @id must be a custom enum defined by gwy_param_def_add_gwyenum() or predefined enum
 * set up by gwy_param_def_add_enum().
 *
 * The parameter must have a description which will be used as the radio button list header.
 *
 * Use gwy_param_table_append_radio_item() to add individual radio buttons.
 * Use gwy_param_table_append_radio() instead to add an entire set of radio buttons at once.
 *
 * Since: 2.59
 **/
void
gwy_param_table_append_radio_header(GwyParamTable *partable,
                                    gint id)
{
    GwyParams *params;
    const GwyParamDefItem *def;

    if (!find_def_common(partable, id, &params, &def))
        return;
    g_return_if_fail(def->type == GWY_PARAM_ENUM);
    g_return_if_fail(def->desc);
    append_control(partable, GWY_PARAM_CONTROL_RADIO_HEADER, id, 1);
    add_toggles_info(partable, id, TRUE);
    if (partable->priv->widget)
        radio_header_make_control(partable, partable->priv->ncontrols-1, def);
}

/**
 * gwy_param_table_append_radio_item:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @value: Enumerated value for a particular radio button.
 *
 * Adds a single radio button to a parameter table.
 *
 * The parameter identified by @id must be a custom enum defined by gwy_param_def_add_gwyenum() or predefined enum set
 * up by gwy_param_def_add_enum(). The value must belong to the enum.
 *
 * Use gwy_param_table_append_radio_header() to add the header for a radio button list.
 * Use gwy_param_table_append_radio() instead to add an entire set of radio buttons at once.
 *
 * Since: 2.59
 **/
void
gwy_param_table_append_radio_item(GwyParamTable *partable,
                                  gint id,
                                  gint value)
{
    GwyParamControl *control;
    GwyParamControlRadioItem *radioitem;
    GwyParams *params;
    const GwyParamDefItem *def;

    if (!find_def_common(partable, id, &params, &def))
        return;
    g_return_if_fail(def->type == GWY_PARAM_ENUM);
    control = append_control(partable, GWY_PARAM_CONTROL_RADIO_ITEM, id, 1);
    control->impl = radioitem = g_slice_new0(GwyParamControlRadioItem);
    radioitem->value = value;
    add_toggles_info(partable, id, FALSE);
    if (partable->priv->widget)
        radio_item_make_control(partable, partable->priv->ncontrols-1, def);
}

/**
 * gwy_param_table_append_radio_row:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 *
 * Adds a set of radio buttons to a parameter table as a compact horizontal list.
 *
 * The parameter identified by @id must be a custom enum defined by gwy_param_def_add_gwyenum() or predefined enum set
 * up by gwy_param_def_add_enum().
 *
 * This function is only suitable for a small set of choices, each with a rather short label.  Use
 * gwy_param_table_append_radio() for a vertical list and gwy_param_table_append_radio_buttons() for a one-row list of
 * image buttons.
 *
 * Since: 2.59
 **/
void
gwy_param_table_append_radio_row(GwyParamTable *partable,
                                 gint id)
{
    GwyParams *params;
    const GwyParamDefItem *def;

    if (!find_def_common(partable, id, &params, &def))
        return;
    g_return_if_fail(def->type == GWY_PARAM_ENUM);
    g_return_if_fail(def->desc);
    g_warn_if_fail(def->def.e.nvalues < 32);
    append_control(partable, GWY_PARAM_CONTROL_RADIO_ROW, id, 1);
    add_toggles_info(partable, id, TRUE);
    if (partable->priv->widget)
        radio_row_make_control(partable, partable->priv->ncontrols-1, def);
}

/**
 * gwy_param_table_append_radio_buttons:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @stock_ids: Stock image identifiers corresponding to the enum values.  It may be %NULL for standard enums for which
 *             stock icons are predefined.
 *
 * Adds a set of radio buttons to a parameter table as a row of image buttons.
 *
 * The parameter identified by @id must be a custom enum defined by gwy_param_def_add_gwyenum() or predefined enum set
 * up by gwy_param_def_add_enum().
 *
 * Button order is determined by parameter definition. Stock image identifiers @stock_ids do not influence it and can
 * be given in any order.
 *
 * The enum names from parameter definition will be used for button tooltips.  Use gwy_param_table_append_radio() for
 * a vertical list and gwy_param_table_append_radio_row() for a compact one-row list with text labels.
 *
 * Since: 2.59
 **/
void
gwy_param_table_append_radio_buttons(GwyParamTable *partable,
                                     gint id,
                                     const GwyEnum *stock_ids)
{
    GwyParamControl *control;
    GwyParamControlRadioButtons *radiobuttons;
    GwyParams *params;
    const GwyParamDefItem *def;

    if (!find_def_common(partable, id, &params, &def))
        return;
    g_return_if_fail(def->type == GWY_PARAM_ENUM);
    g_return_if_fail(def->desc);
    g_warn_if_fail(def->def.e.nvalues < 32);
    if (!stock_ids)
        stock_ids = guess_standard_stock_ids(def);
    g_return_if_fail(stock_ids);
    control = append_control(partable, GWY_PARAM_CONTROL_RADIO_BUTTONS, id, 1);
    control->impl = radiobuttons = g_slice_new0(GwyParamControlRadioButtons);
    radiobuttons->stock_ids = stock_ids;
    add_toggles_info(partable, id, TRUE);
    if (partable->priv->widget)
        radio_buttons_make_control(partable, partable->priv->ncontrols-1, def);
}

/**
 * gwy_param_table_radio_set_sensitive:
 * @partable: A parameter table.
 * @id: Parameter identifier.
 * @value: Enumerated value for a particular radio button.
 * @sensitive: %TRUE to make the control sensitive, %FALSE to make it insensitive.
 *
 * Sets the sensitivity of a single radio button in a parameter table.
 *
 * This function sets the sensitivity of a radio button corresponding to a specific value.  Use
 * gwy_param_table_set_sensitive() to set the sensitivity of an enitre group of radio buttons.
 *
 * Since: 2.59
 **/
void
gwy_param_table_radio_set_sensitive(GwyParamTable *partable,
                                    gint id,
                                    gint value,
                                    gboolean sensitive)
{
    GwyParamControl *control;
    GtkWidget *widget;
    GwyToggleListInfo *toggles_info;
    const GwyParamDefItem *def;
    guint32 flags, oldbits;

    if (!find_def_common(partable, id, NULL, &def))
        return;
    control = find_first_control(partable, id);
    g_return_if_fail(control && control_is_some_kind_of_radio(control->type));
    toggles_info = find_toggles_info(partable, id);
    g_return_if_fail(toggles_info);
    flags = bit_mask_for_enum_value(def, value);
    oldbits = toggles_info->sensitive_bits;
    if ((toggles_info->sensitive_bits = (sensitive ? oldbits | flags : oldbits & ~flags)) == oldbits)
        return;
    if (control->widget) {
        widget = gwy_radio_buttons_find(gtk_radio_button_get_group(GTK_RADIO_BUTTON(control->widget)), value);
        g_return_if_fail(widget);
        gtk_widget_set_sensitive(widget, sensitive && toggles_info->sensitive);
    }
}

/**
 * gwy_param_table_append_checkboxes:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 *
 * Adds a set of checkboxes to a parameter table as a vertical list.
 *
 * The parameter identified by @id must be a generic enum defined by gwy_param_def_add_gwyenum() or
 * gwy_param_def_add_enum().
 *
 * If the parameter has a description it will be used as the checkbox list header.  Otherwise the checkboxes will be
 * free-standing.
 *
 * Use gwy_param_table_append_checkbox() for individual boolean variables.
 *
 * Since: 2.59
 **/
void
gwy_param_table_append_checkboxes(GwyParamTable *partable,
                                  gint id)
{
    GwyParams *params;
    const GwyParamDefItem *def;

    if (!find_def_common(partable, id, &params, &def))
        return;
    g_return_if_fail(def->type == GWY_PARAM_FLAGS);
    g_warn_if_fail(def->def.f.nvalues < 32);
    append_control(partable, GWY_PARAM_CONTROL_CHECKBOXES, id, (def->desc ? 1 : 0) + def->def.f.nvalues);
    add_toggles_info(partable, id, TRUE);
    if (partable->priv->widget)
        checkboxes_make_control(partable, partable->priv->ncontrols-1, def);
}

/**
 * gwy_param_table_checkboxes_set_sensitive:
 * @partable: A parameter table.
 * @id: Parameter identifier.
 * @flags: Flag values for a particular radio button.  It can have multiple bits set – all corresponding checkboxes
 *         are made sensitive/insensitive accordingly.
 * @sensitive: %TRUE to make the control sensitive, %FALSE to make it insensitive.
 *
 * Sets the sensitivity of a subgroup of flag checkboxes in a parameter table.
 *
 * This function sets the sensitivity of a checkboxes corresponding to specific values.  Use
 * gwy_param_table_set_sensitive() to set the sensitivity of an enitre group of checkboxes.
 *
 * Since: 2.59
 **/
void
gwy_param_table_checkboxes_set_sensitive(GwyParamTable *partable,
                                         gint id,
                                         guint flags,
                                         gboolean sensitive)
{
    GwyParamControl *control;
    GtkWidget *widget;
    GwyToggleListInfo *toggles_info;
    const GwyParamDefItem *def;
    guint32 oldbits, bit;
    guint i;

    if (!find_def_common(partable, id, NULL, &def))
        return;
    control = find_first_control(partable, id);
    g_return_if_fail(control && control->type == GWY_PARAM_CONTROL_CHECKBOXES);
    toggles_info = find_toggles_info(partable, id);
    g_return_if_fail(toggles_info);
    oldbits = toggles_info->sensitive_bits;
    if ((toggles_info->sensitive_bits = (sensitive ? oldbits | flags : oldbits & ~flags)) == oldbits)
        return;
    for (i = 0; flags; i++) {
        bit = 1u << i;
        if (flags & bit) {
            widget = gwy_check_boxes_find(gwy_check_box_get_group(control->widget), bit);
            g_return_if_fail(widget);
            gtk_widget_set_sensitive(widget, sensitive && toggles_info->sensitive);
        }
        flags &= ~bit;
    }
}

/**
 * gwy_param_table_append_target_graph:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @gmodel: Graph model to use as template for filtering (or %NULL).
 *
 * Adds a target graph chooser to a parameter table.
 *
 * The parameter identified by @id must be a graph id defined by gwy_param_def_add_target_graph().
 *
 * If @gmodel is not %NULL it will be used for filtering.  Only graphs with units matching @gmodel will be allowed in
 * the chooser.  Filtering is only done upon construction and when explicitly requested using
 * gwy_param_table_data_id_refilter().  Therefore, @gmodel can be changed piecewise without invoking reflitering in
 * the intermediate states.
 *
 * Since: 2.59
 **/
void
gwy_param_table_append_target_graph(GwyParamTable *partable,
                                    gint id,
                                    GwyGraphModel *gmodel)
{
    GwyParamControl *control;
    GwyParamControlDataChooser *datachooser;
    GwyParams *params;
    const GwyParamDefItem *def;

    if (!find_def_common(partable, id, &params, &def))
        return;
    g_return_if_fail(def->type == GWY_PARAM_GRAPH_ID);
    g_return_if_fail(def->def.di.is_target_graph);
    control = append_control(partable, GWY_PARAM_CONTROL_GRAPH_ID_COMBO, id, 1);
    control->do_not_reset = TRUE;
    control->impl = datachooser = g_slice_new0(GwyParamControlDataChooser);
    if (gmodel) {
        /* It may seem a good idea to refilter automatically by connecting to gmodel::notify, etc.  However, the
         * module may update the graph model piecewise because it is its actual output graph.  This would cause
         * refiltering in the intermediate states, mostly likely resetting the selected graph to none.  So the caller
         * must request refiltering explicitly. */
        g_assert(GWY_IS_GRAPH_MODEL(gmodel));
        datachooser->filter_data = g_object_ref(gmodel);
        datachooser->filter_func = filter_graph_model;
        datachooser->filter_destroy = g_object_unref;
        datachooser->none = _("New graph");
    }
    if (partable->priv->widget)
        data_id_make_control(partable, partable->priv->ncontrols-1, def);
}

/**
 * gwy_param_table_append_graph_id:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 *
 * Adds an graph data chooser to a parameter table.
 *
 * The parameter identified by @id must be an graph id defined by gwy_param_def_add_graph_id() or possibly
 * a predefined parameter of this type.
 *
 * Since: 2.60
 **/
void
gwy_param_table_append_graph_id(GwyParamTable *partable,
                                gint id)
{
    GwyParamControl *control;
    GwyParamControlDataChooser *datachooser;
    GwyParams *params;
    const GwyParamDefItem *def;

    if (!find_def_common(partable, id, &params, &def))
        return;
    g_return_if_fail(def->type == GWY_PARAM_GRAPH_ID);
    g_return_if_fail(!def->def.di.is_target_graph);
    control = append_control(partable, GWY_PARAM_CONTROL_GRAPH_ID_COMBO, id, 1);
    control->do_not_reset = TRUE;
    control->impl = datachooser = g_slice_new0(GwyParamControlDataChooser);
    if (partable->priv->widget)
        data_id_make_control(partable, partable->priv->ncontrols-1, def);
}

/**
 * gwy_param_table_append_image_id:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 *
 * Adds an image data chooser to a parameter table.
 *
 * The parameter identified by @id must be an image id defined by gwy_param_def_add_image_id() or possibly
 * a predefined parameter of this type.
 *
 * Since: 2.59
 **/
void
gwy_param_table_append_image_id(GwyParamTable *partable,
                                gint id)
{
    GwyParamControl *control;
    GwyParamControlDataChooser *datachooser;
    GwyParams *params;
    const GwyParamDefItem *def;

    if (!find_def_common(partable, id, &params, &def))
        return;
    g_return_if_fail(def->type == GWY_PARAM_IMAGE_ID);
    control = append_control(partable, GWY_PARAM_CONTROL_IMAGE_ID_COMBO, id, 1);
    control->do_not_reset = TRUE;
    control->impl = datachooser = g_slice_new0(GwyParamControlDataChooser);
    if (partable->priv->widget)
        data_id_make_control(partable, partable->priv->ncontrols-1, def);
}

/**
 * gwy_param_table_append_volume_id:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 *
 * Adds an volume data chooser to a parameter table.
 *
 * The parameter identified by @id must be a volume data id defined by gwy_param_def_add_volume_id() or possibly
 * a predefined parameter of this type.
 *
 * Since: 2.59
 **/
void
gwy_param_table_append_volume_id(GwyParamTable *partable,
                                 gint id)
{
    GwyParamControl *control;
    GwyParamControlDataChooser *datachooser;
    GwyParams *params;
    const GwyParamDefItem *def;

    if (!find_def_common(partable, id, &params, &def))
        return;
    g_return_if_fail(def->type == GWY_PARAM_VOLUME_ID);
    control = append_control(partable, GWY_PARAM_CONTROL_VOLUME_ID_COMBO, id, 1);
    control->do_not_reset = TRUE;
    control->impl = datachooser = g_slice_new0(GwyParamControlDataChooser);
    if (partable->priv->widget)
        data_id_make_control(partable, partable->priv->ncontrols-1, def);
}

/**
 * gwy_param_table_append_xyz_id:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 *
 * Adds an XYZ data chooser to a parameter table.
 *
 * The parameter identified by @id must be an XYZ data id defined by gwy_param_def_add_xyz_id() or possibly
 * a predefined parameter of this type.
 *
 * Since: 2.59
 **/
void
gwy_param_table_append_xyz_id(GwyParamTable *partable,
                              gint id)
{
    GwyParamControl *control;
    GwyParamControlDataChooser *datachooser;
    GwyParams *params;
    const GwyParamDefItem *def;

    if (!find_def_common(partable, id, &params, &def))
        return;
    g_return_if_fail(def->type == GWY_PARAM_XYZ_ID);
    control = append_control(partable, GWY_PARAM_CONTROL_XYZ_ID_COMBO, id, 1);
    control->do_not_reset = TRUE;
    control->impl = datachooser = g_slice_new0(GwyParamControlDataChooser);
    if (partable->priv->widget)
        data_id_make_control(partable, partable->priv->ncontrols-1, def);
}

/**
 * gwy_param_table_append_curve_map_id:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 *
 * Adds a curve map data chooser to a parameter table.
 *
 * The parameter identified by @id must be a curve map id defined by gwy_param_def_add_curve_map_id() or possibly
 * a predefined parameter of this type.
 *
 * Since: 2.60
 **/
void
gwy_param_table_append_curve_map_id(GwyParamTable *partable,
                                    gint id)
{
    GwyParamControl *control;
    GwyParamControlDataChooser *datachooser;
    GwyParams *params;
    const GwyParamDefItem *def;

    if (!find_def_common(partable, id, &params, &def))
        return;
    g_return_if_fail(def->type == GWY_PARAM_CURVE_MAP_ID);
    control = append_control(partable, GWY_PARAM_CONTROL_CURVE_MAP_ID_COMBO, id, 1);
    control->do_not_reset = TRUE;
    control->impl = datachooser = g_slice_new0(GwyParamControlDataChooser);
    if (partable->priv->widget)
        data_id_make_control(partable, partable->priv->ncontrols-1, def);
}

/**
 * gwy_param_table_data_id_set_filter:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @filter: The filter function.
 * @user_data: The data passed to @filter.
 * @destroy: Destroy notifier of @user_data or %NULL.
 *
 * The parameter identified by @id must be a data id defined by a function such as gwy_param_def_add_image_id()
 * or a predefined parameter of this type, for instance defined by gwy_param_def_add_target_graph().
 *
 * The arguments have the same meaning as in gwy_data_chooser_set_filter().  Setting the filter to a different one
 * automatically refilters the data chooser.
 *
 * Since: 2.59
 **/
void
gwy_param_table_data_id_set_filter(GwyParamTable *partable,
                                   gint id,
                                   GwyDataChooserFilterFunc filter,
                                   gpointer user_data,
                                   GDestroyNotify destroy)
{
    GwyParamControl *control;
    GwyParamControlDataChooser *datachooser;
    GDestroyNotify old_destroy;
    gpointer old_data;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    control = find_first_control(partable, id);
    g_return_if_fail(control && control_is_some_kind_of_data_id(control->type));

    datachooser = (GwyParamControlDataChooser*)control->impl;
    old_destroy = datachooser->filter_destroy;
    old_data = datachooser->filter_data;
    datachooser->filter_func = filter;
    datachooser->filter_data = user_data;
    datachooser->filter_destroy = destroy;
    /* This does refiltering inside gwy_data_chooser_set_filter(). Passing NULL as the destroy function is correct.
     * We change the life-cycle.  The data passed to the filter function must not be destroyed when the chooser goes
     * poof because we can re-create the chooser.  The data must be destroyed when the partable itself is destroyed. */
    if (control->widget) {
        gwy_data_chooser_set_filter(GWY_DATA_CHOOSER(control->widget),
                                    datachooser->filter_func, datachooser->filter_data, NULL);
    }
    if (old_destroy)
        old_destroy(old_data);
}

/**
 * gwy_param_table_data_id_refilter:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 *
 * Requests refiltering of choices in a data chooser in a parameter table.
 *
 * The parameter identified by @id must be a data id defined by a function such as gwy_param_def_add_image_id()
 * or a predefined parameter of this type, for instance defined by gwy_param_def_add_target_graph().
 *
 * It is possible to call this function when the table widget does not exist yet.
 *
 * Since: 2.59
 **/
void
gwy_param_table_data_id_refilter(GwyParamTable *partable,
                                 gint id)
{
    GwyParamControl *control;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    control = find_first_control(partable, id);
    g_return_if_fail(control && control_is_some_kind_of_data_id(control->type));

    if (control->widget)
        gwy_data_chooser_refilter(GWY_DATA_CHOOSER(control->widget));
}

/**
 * gwy_param_table_append_graph_curve:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @gmodel: Graph model to choose the curve from.
 *
 * Adds a curve data chooser for a graph model to a parameter table.
 *
 * The parameter identified by @id must be an curve number defined by gwy_param_def_add_graph_curve() or possibly
 * a predefined parameter of this type.
 *
 * Since: 2.60
 **/
void
gwy_param_table_append_graph_curve(GwyParamTable *partable,
                                   gint id,
                                   GwyGraphModel *gmodel)
{
    GwyParamControl *control;
    GwyParamControlCurveChooser *curvechooser;
    GwyGraphCurveModel *gcmodel;
    GwyParams *params;
    const GwyParamDefItem *def;

    if (!find_def_common(partable, id, &params, &def))
        return;
    g_return_if_fail(def->type == GWY_PARAM_GRAPH_CURVE);
    g_return_if_fail(GWY_IS_GRAPH_MODEL(gmodel));
    if (_gwy_params_curve_get_use_string(params, id)) {
        if ((gcmodel = gwy_graph_model_get_curve_by_description(gmodel, gwy_params_get_string(params, id))))
            gwy_params_set_curve(params, id, gwy_graph_model_get_curve_index(gmodel, gcmodel));
    }
    control = append_control(partable, GWY_PARAM_CONTROL_GRAPH_CURVE_COMBO, id, 1);
    control->do_not_reset = TRUE;
    control->impl = curvechooser = g_slice_new0(GwyParamControlCurveChooser);
    gwy_set_member_object(partable, gmodel, GWY_TYPE_GRAPH_MODEL, &curvechooser->parent, NULL);
    if (partable->priv->widget)
        curve_no_make_control(partable, partable->priv->ncontrols-1, def);
}

/**
 * gwy_param_table_graph_curve_set_model:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @gmodel: Graph model to choose the curve from.
 *
 * Changes the graph model for a curve data chooser.
 *
 * The chooser must be created by gwy_param_table_append_graph_curve().
 *
 * Since: 2.60
 **/
void
gwy_param_table_graph_curve_set_model(GwyParamTable *partable,
                                      gint id,
                                      GwyGraphModel *gmodel)
{
    GwyParamControl *control;
    GwyParamControlCurveChooser *curvechooser;
    GwyParamTablePrivate *priv;
    GwyGraphCurveModel *gcmodel;
    GtkWidget *hbox;
    const gchar *prevdescription;
    gint n, curveno = 0;
    guint i;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    g_return_if_fail(GWY_IS_GRAPH_MODEL(gmodel));
    control = find_first_control(partable, id);
    g_return_if_fail(control && control->type == GWY_PARAM_CONTROL_GRAPH_CURVE_COMBO);

    curvechooser = (GwyParamControlCurveChooser*)control->impl;
    if (!gwy_set_member_object(partable, gmodel, GWY_TYPE_GRAPH_MODEL, &curvechooser->parent, NULL))
        return;

    priv = partable->priv;
    if ((prevdescription = gwy_params_get_string(priv->params, id))
        && (gcmodel = gwy_graph_model_get_curve_by_description(gmodel, prevdescription)))
        curveno = gwy_graph_model_get_curve_index(gmodel, gcmodel);

    n = gwy_graph_model_get_n_curves(gmodel);
    curveno = (n ? CLAMP(curveno, 0, n-1) : -1);

    _gwy_param_table_in_update(partable, TRUE);
    gwy_params_set_curve(priv->params, control->id, curveno);
    if (control->widget) {
        /* There is no API for switching graph curve chooser backend.  Resort to creating a new widget. */
        hbox = gtk_widget_get_parent(control->widget);
        g_assert(GTK_IS_HBOX(hbox));
        gtk_widget_destroy(control->widget);
        control->widget = gwy_combo_box_graph_curve_new(G_CALLBACK(graph_curve_changed), partable,
                                                        GWY_GRAPH_MODEL(curvechooser->parent), curveno);
        i = control - priv->controls;
        g_object_set_qdata(G_OBJECT(control->widget), param_control_quark, GUINT_TO_POINTER(i));
        gtk_box_pack_end(GTK_BOX(hbox), control->widget, TRUE, TRUE, 0);
        gtk_widget_show(control->widget);
        if (control->label)
            gtk_label_set_mnemonic_widget(GTK_LABEL(control->label), control->widget);
        update_control_sensitivity(partable, i);
    }
    _gwy_param_table_in_update(partable, FALSE);
    gwy_param_table_param_changed(partable, id);
}

/**
 * gwy_param_table_append_lawn_curve:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @lawn: Lawn curve map object to choose the curve from.
 *
 * Adds a curve data chooser for a lawn to a parameter table.
 *
 * The parameter identified by @id must be an curve number defined by gwy_param_def_add_lawn_curve() or possibly
 * a predefined parameter of this type.
 *
 * Since: 2.60
 **/
void
gwy_param_table_append_lawn_curve(GwyParamTable *partable,
                                  gint id,
                                  GwyLawn *lawn)
{
    GwyParamControl *control;
    GwyParamControlCurveChooser *curvechooser;
    GwyParams *params;
    const GwyParamDefItem *def;
    const gchar *label, *selected;
    gint i, n;

    if (!find_def_common(partable, id, &params, &def))
        return;
    g_return_if_fail(def->type == GWY_PARAM_LAWN_CURVE);
    g_return_if_fail(GWY_IS_LAWN(lawn));
    if (_gwy_params_curve_get_use_string(params, id)) {
        selected = gwy_params_get_string(params, id);
        n = gwy_lawn_get_n_curves(lawn);
        for (i = 0; i < n; i++) {
            if ((label = gwy_lawn_get_curve_label(lawn, i)) && gwy_strequal(label, selected)) {
                gwy_params_set_curve(params, id, i);
                break;
            }
        }
    }
    control = append_control(partable, GWY_PARAM_CONTROL_LAWN_CURVE_COMBO, id, 1);
    control->do_not_reset = TRUE;
    control->impl = curvechooser = g_slice_new0(GwyParamControlCurveChooser);
    gwy_set_member_object(partable, lawn, GWY_TYPE_LAWN, &curvechooser->parent, NULL);
    if (partable->priv->widget)
        curve_no_make_control(partable, partable->priv->ncontrols-1, def);
}

/* XXX: This is complicated and apparently not needed.  Avoid exposing it as a public API until we need it. */
#if 0
/**
 * gwy_param_table_lawn_curve_set_model:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @lawn: Lawn curve map object to choose the curve from.
 *
 * Changes the lawn model for a curve data chooser.
 *
 * The chooser must be created by gwy_param_table_append_lawn_curve().
 *
 * Since: 2.60
 **/
void
gwy_param_table_lawn_curve_set_model(GwyParamTable *partable,
                                     gint id,
                                     GwyLawn *lawn)
{
    GwyParamControl *control;
    GwyParamControlCurveChooser *curvechooser;
    GwyParamTablePrivate *priv;
    GwyLawn *oldlawn;
    GtkWidget *hbox;
    gint n, j, curveno = 0;
    const gchar *selected, *label;
    guint i;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    g_return_if_fail(GWY_IS_LAWN(lawn));
    control = find_first_control(partable, id);
    g_return_if_fail(control && control->type == GWY_PARAM_CONTROL_LAWN_CURVE_COMBO);

    curvechooser = (GwyParamControlCurveChooser*)control->impl;
    oldlawn = GWY_LAWN(curvechooser->parent);
    if (oldlawn == lawn)
        return;

    priv = partable->priv;
    n = gwy_lawn_get_n_curves(lawn);
    selected = gwy_params_get_string(priv->params, id);
    for (j = 0; j < n; j++) {
        if ((label = gwy_lawn_get_curve_label(lawn, j)) && gwy_strequal(label, selected)) {
            curveno = j;
            break;
        }
    }
    curveno = (n ? CLAMP(curveno, 0, n-1) : -1);

    _gwy_param_table_in_update(partable, TRUE);
    curvechooser->parent = G_OBJECT(g_object_ref(lawn));
    gwy_params_set_curve(priv->params, control->id, curveno);
    if (control->widget) {
        /* There is no API for switching lawn curve chooser backend.  Resort to creating a new widget. */
        hbox = gtk_widget_get_parent(control->widget);
        g_assert(GTK_IS_HBOX(hbox));
        gtk_widget_destroy(control->widget);
        control->widget = gwy_combo_box_lawn_curve_new(G_CALLBACK(lawn_curve_changed), partable,
                                                       GWY_LAWN(curvechooser->parent), curveno);
        i = control - priv->controls;
        g_object_set_qdata(G_OBJECT(control->widget), param_control_quark, GUINT_TO_POINTER(i));
        gtk_box_pack_end(GTK_BOX(hbox), control->widget, TRUE, TRUE, 0);
        gtk_widget_show(control->widget);
        if (control->label)
            gtk_label_set_mnemonic_widget(GTK_LABEL(control->label), control->widget);
        update_control_sensitivity(partable, i);
    }
    _gwy_param_table_in_update(partable, FALSE);
    g_object_unref(oldlawn);
    gwy_param_table_param_changed(partable, id);
}
#endif

/**
 * gwy_param_table_append_lawn_segment:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @lawn: Lawn curve map object to choose the segment from.
 *
 * Adds a segment data chooser for a lawn to a parameter table.
 *
 * The parameter identified by @id must be an segment number defined by gwy_param_def_add_lawn_segment() or possibly
 * a predefined parameter of this type.
 *
 * Since: 2.60
 **/
void
gwy_param_table_append_lawn_segment(GwyParamTable *partable,
                                    gint id,
                                    GwyLawn *lawn)
{
    GwyParamControl *control;
    GwyParamControlCurveChooser *curvechooser;
    GwyParams *params;
    const GwyParamDefItem *def;
    const gchar *label, *selected;
    gint i, n;

    if (!find_def_common(partable, id, &params, &def))
        return;
    g_return_if_fail(def->type == GWY_PARAM_LAWN_SEGMENT);
    g_return_if_fail(GWY_IS_LAWN(lawn));
    if (_gwy_params_curve_get_use_string(params, id)) {
        selected = gwy_params_get_string(params, id);
        n = gwy_lawn_get_n_segments(lawn);
        for (i = 0; i < n; i++) {
            if ((label = gwy_lawn_get_segment_label(lawn, i)) && gwy_strequal(label, selected)) {
                gwy_params_set_curve(params, id, i);
                break;
            }
        }
    }
    control = append_control(partable, GWY_PARAM_CONTROL_LAWN_SEGMENT_COMBO, id, 1);
    control->do_not_reset = TRUE;
    control->impl = curvechooser = g_slice_new0(GwyParamControlCurveChooser);
    gwy_set_member_object(partable, lawn, GWY_TYPE_LAWN, &curvechooser->parent, NULL);
    if (partable->priv->widget)
        curve_no_make_control(partable, partable->priv->ncontrols-1, def);
}

/**
 * gwy_param_table_append_slider:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 *
 * Adds a numerical slider to a parameter table.
 *
 * The parameter identified by @id must be an integer or floating point numerical parameter defined by
 * gwy_param_def_add_int(), gwy_param_def_add_double() or a predefined parameter of one of these types.
 *
 * The parameter must have a description which will be used as the label.
 *
 * Since: 2.59
 **/
void
gwy_param_table_append_slider(GwyParamTable *partable,
                              gint id)
{
    GwyParamControl *control;
    GwyParamControlSlider *slider;
    GwyParams *params;
    const GwyParamDefItem *def;
    guint i;

    if (!find_def_common(partable, id, &params, &def))
        return;
    g_return_if_fail(def->type == GWY_PARAM_DOUBLE || def->type == GWY_PARAM_INT);
    control = append_control(partable, GWY_PARAM_CONTROL_SLIDER, id, 1);
    control->impl = slider = g_slice_new0(GwyParamControlSlider);
    slider->is_int = (def->type == GWY_PARAM_INT);
    slider_auto_configure(control, def);
    i = partable->priv->ncontrols-1;
    if (def->type == GWY_PARAM_DOUBLE) {
        if (def->def.d.is_angle)
            slider_make_angle(partable, i);
        else if (def->def.d.is_percentage)
            slider_make_percentage(partable, i);
    }
    if (partable->priv->widget)
        slider_make_control(partable, i, def);
}

/**
 * gwy_param_table_slider_set_mapping:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @mapping: Mapping type to use.
 *
 * Sets the mapping type for a slider in a parameter table.
 *
 * Since: 2.59
 **/
void
gwy_param_table_slider_set_mapping(GwyParamTable *partable,
                                   gint id,
                                   GwyScaleMappingType mapping)
{
    GwyParamControl *control;
    GwyParamControlSlider *slider;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    control = find_first_control(partable, id);
    g_return_if_fail(control && control->type == GWY_PARAM_CONTROL_SLIDER);
    g_return_if_fail(mapping == GWY_SCALE_MAPPING_LINEAR
                     || mapping == GWY_SCALE_MAPPING_SQRT
                     || mapping == GWY_SCALE_MAPPING_LOG
                     || mapping == GWY_SCALE_MAPPING_LOG1P);

    slider = (GwyParamControlSlider*)control->impl;
    if (slider->mapping_set && mapping == slider->mapping)
        return;

    slider->mapping_set = TRUE;
    slider->mapping = mapping;
    if (control->widget)
        gwy_adjust_bar_set_mapping(GWY_ADJUST_BAR(control->widget), mapping);
}

/**
 * gwy_param_table_slider_set_steps:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @step: The step (in true parameter values), as in #GtkAdjustment.
 * @page: The page step (in true parameter values), as in #GtkAdjustment.
 *
 * Sets the step and page step for a slider in a parameter table.
 *
 * The parameter table sets automatically reasonable steps according to the parameter type and range.  This function
 * allows overriding them when more detailed control is needed.
 *
 * Since: 2.59
 **/
void
gwy_param_table_slider_set_steps(GwyParamTable *partable,
                                 gint id,
                                 gdouble step,
                                 gdouble page)
{
    GwyParamControl *control;
    GwyParamControlSlider *slider;
    GwyParams *params;
    const GwyParamDefItem *def;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    control = find_first_control(partable, id);
    g_return_if_fail(control && control->type == GWY_PARAM_CONTROL_SLIDER);

    slider = (GwyParamControlSlider*)control->impl;
    if (!step || !page) {
        if (!slider->steps_set)
            return;
        slider->steps_set = FALSE;
    }
    else {
        if (slider->steps_set && slider->step == step && slider->page == page)
            return;
        slider->steps_set = TRUE;
        slider->step = step;
        slider->page = page;
    }
    /* XXX: There are lots of weird things the caller could try here.  Like setting non-integer step for integer
     * values.  Hopefully he doesn't. */

    if (!find_def_common(partable, id, &params, &def))
        return;
    slider_auto_configure(control, def);
    if (control->widget)
        slider_reconfigure_adjustment(control, partable);
}

/**
 * gwy_param_table_slider_set_digits:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @digits: The number of decimal places of the numerical value.
 *
 * Sets the numnber of decimal place for a slider in a parameter table.
 *
 * The parameter table sets automatically a reasonable number decimal places according to the parameter type, range
 * and steps.  This function allows overriding it when more detailed control is needed.
 *
 * Since: 2.59
 **/
void
gwy_param_table_slider_set_digits(GwyParamTable *partable,
                                  gint id,
                                  gint digits)
{
    GwyParamControl *control;
    GwyParamControlSlider *slider;
    GwyParams *params;
    const GwyParamDefItem *def;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    control = find_first_control(partable, id);
    g_return_if_fail(control && control->type == GWY_PARAM_CONTROL_SLIDER);

    slider = (GwyParamControlSlider*)control->impl;
    if (digits < 0) {
        if (!slider->digits_set)
            return;
        slider->digits_set = FALSE;
    }
    else {
        if (slider->digits_set && slider->digits == digits)
            return;
        slider->digits_set = TRUE;
        slider->digits = digits;
    }

    if (!find_def_common(partable, id, &params, &def))
        return;
    slider_auto_configure(control, def);
    if (control->widget)
        slider_reconfigure_adjustment(control, partable);
}

/**
 * gwy_param_table_slider_set_snapping:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @setting: %TRUE to enable snapping; %FALSE to disable it.
 *
 * Sets the snapping option of a slider in a parameter table.
 *
 * This function is rarely needed. By default, snapping is enabled for integer sliders as they can only represent the
 * discrete integers, whereas floating point sliders are free. In some cases you may want to override it. Note that
 * once you set the snapping option explicitly, you are responsible for it. There is no way to tell the slider to
 * revert to its default behaviour.
 *
 * Since: 2.62
 **/
void
gwy_param_table_slider_set_snapping(GwyParamTable *partable,
                                    gint id,
                                    gboolean setting)
{
    GwyParamControl *control;
    GwyParamControlSlider *slider;
    GwyParams *params;
    const GwyParamDefItem *def;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    control = find_first_control(partable, id);
    g_return_if_fail(control && control->type == GWY_PARAM_CONTROL_SLIDER);

    slider = (GwyParamControlSlider*)control->impl;
    slider->snap_set = TRUE;
    if (!slider->snap == !setting)
        return;

    slider->snap = setting;
    if (!find_def_common(partable, id, &params, &def))
        return;
    slider_auto_configure(control, def);
    if (control->widget)
        slider_reconfigure_adjustment(control, partable);
}

/**
 * gwy_param_table_slider_restrict_range:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @minimum: Minimum allowed value (inclusive), in true parameter values.
 * @maximum:  Minimum allowed value (inclusive), in true parameter values.
 *
 * Sets the parameter range a slider in a parameter table to a subset of the full range.
 *
 * This function allows restricting the slider range to a smaller range than the one set in the #GwyParamDef.  The
 * range can never be extended (it can be set to be less restricted than previously of course).
 *
 * If there is a transformation between true and displayed values of parameters (for instance for angles) the minimum
 * and maximum refer to the true values.
 *
 * Since: 2.59
 **/
void
gwy_param_table_slider_restrict_range(GwyParamTable *partable,
                                      gint id,
                                      gdouble minimum,
                                      gdouble maximum)
{
    GwyParamControl *control;
    GwyParamControlSlider *slider;
    const GwyParamDefItem *def;
    GwyParams *params;
    gdouble fullmin, fullmax;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    control = find_first_control(partable, id);
    g_return_if_fail(control && control->type == GWY_PARAM_CONTROL_SLIDER);
    if (!find_def_common(partable, id, &params, &def))
        return;

    slider = (GwyParamControlSlider*)control->impl;
    if (slider->is_int) {
        fullmin = def->def.i.minimum;
        fullmax = def->def.i.maximum;
    }
    else {
        fullmin = def->def.d.minimum;
        fullmax = def->def.d.maximum;
    }

    if (minimum > maximum) {
        g_warning("Inverted slider range min %g > %g max.", minimum, maximum);
        GWY_SWAP(gdouble, minimum, maximum);
    }
    if (minimum < fullmin) {
        g_warning("Cannot extend slider minimum beyond %g to %g.", fullmin, minimum);
        minimum = fullmin;
    }
    if (maximum > fullmax) {
        g_warning("Cannot extend slider maximum beyond %g to %g.", fullmax, maximum);
        maximum = fullmax;
    }

    /* XXX: We have no real mechanism for setting the range back to full.  But the caller can do it easily himself. */
    gwy_debug("%s current: %g..%g, new %g..%g", def->desc, slider->minimum, slider->maximum, minimum, maximum);
    if (minimum == slider->minimum && maximum == slider->maximum)
        return;

    slider->minimum = minimum;
    slider->maximum = maximum;
    slider->range_set = !(minimum == fullmin && maximum == fullmax);
    slider_auto_configure(control, def);
    if (control->widget)
        slider_reconfigure_adjustment(control, partable);
}

/**
 * gwy_param_table_slider_set_transform:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @value_to_gui: Function transforming true values to displayed values.
 * @gui_to_value: Function transforming displayed values to true values.
 * @user_data: Data passed to @value_to_gui and @gui_to_value.
 * @destroy: Destroy notifier of @user_data or %NULL.
 *
 * Sets the transformation function for a slider in a parameter table.
 *
 * The functions have to be monotonically increasing in the allowed parameter range.
 *
 * Note that #GtkSpinButton behaves reasonably for human-sized values.  Neither the true nor the transformed value can
 * be too many orders of magnitude far from unity.  For values of physical quantities it is necessary to keep the
 * base power of 10 separately.  See also gwy_param_table_slider_add_alt().
 *
 * Since: 2.59
 **/
void
gwy_param_table_slider_set_transform(GwyParamTable *partable,
                                     gint id,
                                     GwyRealFunc value_to_gui,
                                     GwyRealFunc gui_to_value,
                                     gpointer user_data,
                                     GDestroyNotify destroy)
{
    GwyParamControl *control;
    const GwyParamDefItem *def;
    GwyParamControlSlider *slider;
    GwyParams *params;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    control = find_first_control(partable, id);
    g_return_if_fail(control && control->type == GWY_PARAM_CONTROL_SLIDER);
    if (!find_def_common(partable, id, &params, &def))
        return;
    slider = (GwyParamControlSlider*)control->impl;
    slider->is_percentage = slider->is_angle = FALSE;
    slider_set_transformation(partable, control - partable->priv->controls,
                              value_to_gui, gui_to_value, user_data, destroy);
}

/**
 * gwy_param_table_slider_set_factor:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @q_value_to_gui: Conversion factor for transforming true values to displayed values.
 *
 * Sets a constant factor transformation for a slider in a parameter table.
 *
 * Since: 2.59
 **/
void
gwy_param_table_slider_set_factor(GwyParamTable *partable,
                                  gint id,
                                  gdouble q_value_to_gui)
{
    GwyParamControl *control;
    const GwyParamDefItem *def;
    GwyParamControlSlider *slider;
    GwyParams *params;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    control = find_first_control(partable, id);
    g_return_if_fail(control && control->type == GWY_PARAM_CONTROL_SLIDER);
    if (q_value_to_gui <= 0.0 || gwy_isinf(q_value_to_gui) || gwy_isnan(q_value_to_gui)) {
        g_warning("Invalid conversion factor %g.", q_value_to_gui);
        return;
    }
    if (!find_def_common(partable, id, &params, &def))
        return;
    slider = (GwyParamControlSlider*)control->impl;
    gwy_debug("setting q = %g", q_value_to_gui);
    slider->q_value_to_gui = q_value_to_gui;
    slider->is_percentage = slider->is_angle = FALSE;
    slider_set_transformation(partable, control - partable->priv->controls,
                              multiply_by_constant, divide_by_constant, &slider->q_value_to_gui, NULL);
}

/**
 * gwy_param_table_slider_add_alt:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 *
 * Sets up an alternative value for a slider in a parameter table.
 *
 * The parameter identified by @id must correspond to a slider already added by gwy_param_table_append_slider().  This
 * functions sets up the alternate representation.  It is initally just an identity though.  You need to use
 * a function specify a useful alternative representation afterwards, for instance using
 * gwy_param_table_alt_set_field_pixel_x() which uses real dimensions in a #GwyDataField alternative values for
 * pixels.
 *
 * Since: 2.59
 **/
void
gwy_param_table_slider_add_alt(GwyParamTable *partable,
                               gint id)
{
    GwyParamTablePrivate *priv;
    GwyParamControl *control;
    const GwyParamDefItem *def;
    GwyParamControlSlider *slider;
    guint i;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    priv = partable->priv;
    control = find_first_control(partable, id);
    g_return_if_fail(control && control->type == GWY_PARAM_CONTROL_SLIDER);
    slider = (GwyParamControlSlider*)control->impl;
    if (slider->has_alt)
        return;
    priv->ncols = MAX(priv->ncols, 5);
    slider->has_alt = TRUE;
    slider->alt_q_to_gui = 1.0;
    slider->alt_offset_to_gui = 0.0;
    i = control - priv->controls;
    if (priv->widget && control->widget) {
        alt_make_control(partable, i, def);
        update_control_sensitivity(partable, i);
    }
}

/**
 * gwy_param_table_alt_set_field_pixel_x:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @field: Data field defining the alternative value transformation.
 *
 * Defines a parameter table alternative value for a pixel slider using to physical sizes in a data field.
 *
 * The slider needs to have an alternative value set up using gwy_param_table_slider_add_alt().  Unit value of the
 * true parameter value will correspond to horizontal pixel size, as returned by gwy_data_field_get_dx().
 *
 * The data field @field is only used by this function to set up the transformation. It can be destroyed afterwards
 * if no longer needed by the caller. No reference is taken and later changes to its data or properties do not have
 * any effect on the alternative values. Use gwy_param_table_alt_set_field_pixel_x() again if you need to adjust the
 * transformation for a modified (or different) data field.
 *
 * Since: 2.59
 **/
void
gwy_param_table_alt_set_field_pixel_x(GwyParamTable *partable,
                                      gint id,
                                      GwyDataField *field)
{
    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    g_return_if_fail(GWY_IS_DATA_FIELD(field));
    gwy_si_unit_get_format_with_resolution(gwy_data_field_get_si_unit_xy(field), GWY_SI_UNIT_FORMAT_VFMARKUP,
                                           gwy_data_field_get_xreal(field), gwy_data_field_get_dx(field),
                                           partable->priv->vf);
    alt_set_from_value_format(partable, id, _("px"), gwy_data_field_get_dx(field), 0.0);
}

/**
 * gwy_param_table_alt_set_field_pixel_y:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @field: Data field defining the alternative value transformation.
 *
 * Defines a parameter table alternative value for a pixel slider using to physical sizes in a data field.
 *
 * The slider needs to have an alternative value set up using gwy_param_table_slider_add_alt().  Unit value of the
 * true parameter value will correspond to vertical pixel size, as returned by gwy_data_field_get_dy().
 *
 * See gwy_param_table_alt_set_field_pixel_x() for a discussion how @field is used.
 *
 * Since: 2.59
 **/
void
gwy_param_table_alt_set_field_pixel_y(GwyParamTable *partable,
                                      gint id,
                                      GwyDataField *field)
{
    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    g_return_if_fail(GWY_IS_DATA_FIELD(field));
    gwy_si_unit_get_format_with_resolution(gwy_data_field_get_si_unit_xy(field), GWY_SI_UNIT_FORMAT_VFMARKUP,
                                           gwy_data_field_get_yreal(field), gwy_data_field_get_dy(field),
                                           partable->priv->vf);
    alt_set_from_value_format(partable, id, _("px"), gwy_data_field_get_dy(field), 0.0);
}

/**
 * gwy_param_table_alt_set_brick_pixel_x:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @brick: Data brick definiting the alternative value transformation.
 *
 * Defines a parameter table alternative value for a pixel slider using to physical sizes in a data brick.
 *
 * The slider needs to have an alternative value set up using gwy_param_table_slider_add_alt().  Unit value of the
 * true parameter value will correspond to horizontal pixel size in the XY plane, as returned by gwy_brick_get_dx().
 *
 * See gwy_param_table_alt_set_field_pixel_x() for a discussion how @brick is used.
 *
 * Since: 2.62
 **/
void
gwy_param_table_alt_set_brick_pixel_x(GwyParamTable *partable,
                                      gint id,
                                      GwyBrick *brick)
{
    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    g_return_if_fail(GWY_IS_BRICK(brick));
    gwy_si_unit_get_format_with_resolution(gwy_brick_get_si_unit_x(brick), GWY_SI_UNIT_FORMAT_VFMARKUP,
                                           gwy_brick_get_xreal(brick), gwy_brick_get_dx(brick),
                                           partable->priv->vf);
    alt_set_from_value_format(partable, id, _("px"), gwy_brick_get_dx(brick), 0.0);
}

/**
 * gwy_param_table_alt_set_brick_pixel_y:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @brick: Data brick definiting the alternative value transformation.
 *
 * Defines a parameter table alternative value for a pixel slider using to physical sizes in a data brick.
 *
 * The slider needs to have an alternative value set up using gwy_param_table_slider_add_alt().  Unit value of the
 * true parameter value will correspond to vertical pixel size in the XY plane, as returned by gwy_brick_get_dy().
 *
 * See gwy_param_table_alt_set_field_pixel_x() for a discussion how @brick is used.
 *
 * Since: 2.62
 **/
void
gwy_param_table_alt_set_brick_pixel_y(GwyParamTable *partable,
                                      gint id,
                                      GwyBrick *brick)
{
    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    g_return_if_fail(GWY_IS_BRICK(brick));
    gwy_si_unit_get_format_with_resolution(gwy_brick_get_si_unit_y(brick), GWY_SI_UNIT_FORMAT_VFMARKUP,
                                           gwy_brick_get_yreal(brick), gwy_brick_get_dy(brick),
                                           partable->priv->vf);
    alt_set_from_value_format(partable, id, _("px"), gwy_brick_get_dy(brick), 0.0);
}

/**
 * gwy_param_table_alt_set_brick_pixel_z:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @brick: Data brick definiting the alternative value transformation.
 *
 * Defines a parameter table alternative value for a pixel slider using to physical sizes in a data brick.
 *
 * The slider needs to have an alternative value set up using gwy_param_table_slider_add_alt().  Unit value of the
 * true parameter value will correspond to depth-wise pixel size (more precisely, voxel size), as returned by
 * gwy_brick_get_dz().
 *
 * See gwy_param_table_alt_set_field_pixel_x() for a discussion how @brick is used.
 *
 * See gwy_param_table_alt_set_calibration() which can be used with bricks which have Z-calibration.
 *
 * Since: 2.62
 **/
void
gwy_param_table_alt_set_brick_pixel_z(GwyParamTable *partable,
                                      gint id,
                                      GwyBrick *brick)
{
    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    g_return_if_fail(GWY_IS_BRICK(brick));
    gwy_si_unit_get_format_with_resolution(gwy_brick_get_si_unit_z(brick), GWY_SI_UNIT_FORMAT_VFMARKUP,
                                           gwy_brick_get_zreal(brick), gwy_brick_get_dz(brick),
                                           partable->priv->vf);
    alt_set_from_value_format(partable, id, _("px"), gwy_brick_get_dz(brick), 0.0);
}

/**
 * gwy_param_table_alt_set_lawn_pixel_x:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @lawn: Data lawn definiting the alternative value transformation.
 *
 * Defines a parameter table alternative value for a pixel slider using to physical sizes in a data lawn.
 *
 * The slider needs to have an alternative value set up using gwy_param_table_slider_add_alt().  Unit value of the
 * true parameter value will correspond to horizontal pixel size, as returned by gwy_lawn_get_dx().
 *
 * See gwy_param_table_alt_set_field_pixel_x() for a discussion how @lawn is used.
 *
 * Since: 2.62
 **/
void
gwy_param_table_alt_set_lawn_pixel_x(GwyParamTable *partable,
                                     gint id,
                                     GwyLawn *lawn)
{
    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    g_return_if_fail(GWY_IS_LAWN(lawn));
    gwy_si_unit_get_format_with_resolution(gwy_lawn_get_si_unit_xy(lawn), GWY_SI_UNIT_FORMAT_VFMARKUP,
                                           gwy_lawn_get_xreal(lawn), gwy_lawn_get_dx(lawn),
                                           partable->priv->vf);
    alt_set_from_value_format(partable, id, _("px"), gwy_lawn_get_dx(lawn), 0.0);
}

/**
 * gwy_param_table_alt_set_lawn_pixel_y:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @lawn: Data lawn definiting the alternative value transformation.
 *
 * Defines a parameter table alternative value for a pixel slider using to physical sizes in a data lawn.
 *
 * The slider needs to have an alternative value set up using gwy_param_table_slider_add_alt().  Unit value of the
 * true parameter value will correspond to vertical pixel size, as returned by gwy_lawn_get_dy().
 *
 * See gwy_param_table_alt_set_field_pixel_x() for a discussion how @lawn is used.
 *
 * Since: 2.62
 **/
void
gwy_param_table_alt_set_lawn_pixel_y(GwyParamTable *partable,
                                     gint id,
                                     GwyLawn *lawn)
{
    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    g_return_if_fail(GWY_IS_LAWN(lawn));
    gwy_si_unit_get_format_with_resolution(gwy_lawn_get_si_unit_xy(lawn), GWY_SI_UNIT_FORMAT_VFMARKUP,
                                           gwy_lawn_get_yreal(lawn), gwy_lawn_get_dy(lawn),
                                           partable->priv->vf);
    alt_set_from_value_format(partable, id, _("px"), gwy_lawn_get_dy(lawn), 0.0);
}

/**
 * gwy_param_table_alt_set_field_frac_z:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @field: Data field defining the alternative value transformation.
 *
 * Defines a parameter table alternative value for a fraction slider using to physical values in a data field.
 *
 * The slider needs to have an alternative value set up using gwy_param_table_slider_add_alt().  The range [0,1] of
 * true parameter values will correspond to the range of values in the data field, as returned by
 * gwy_data_field_get_min_max().
 *
 * See gwy_param_table_alt_set_field_pixel_x() for a discussion how @field is used.
 *
 * Since: 2.59
 **/
void
gwy_param_table_alt_set_field_frac_z(GwyParamTable *partable,
                                     gint id,
                                     GwyDataField *field)
{
    gdouble min, max, m;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    g_return_if_fail(GWY_IS_DATA_FIELD(field));
    gwy_data_field_get_min_max(field, &min, &max);
    m = (max >= min ? max - min : fabs(max));
    gwy_si_unit_get_format_with_resolution(gwy_data_field_get_si_unit_z(field), GWY_SI_UNIT_FORMAT_VFMARKUP,
                                           m, 0.001*m, partable->priv->vf);
    alt_set_from_value_format(partable, id, NULL, max - min, min);
}

/**
 * gwy_param_table_alt_set_linear:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @q_to_gui: Conversion factor for transforming true values to displayed values.
 * @off_to_gui: Offset for transforming true values to displayed values.
 * @unitstr: Units for the alternative value (with Pango markup).
 *
 * Defines a parameter table alternative value for a fraction slider using a linear function.
 *
 * This function enables setting up a general linear transformation for the alternative value.  The usual cases are
 * more conveniently handled by functions like gwy_param_table_alt_set_field_pixel_x() or
 * gwy_param_table_alt_set_field_frac_z().
 *
 * The slider needs to have an alternative value set up using gwy_param_table_slider_add_alt().  The displayed value
 * is calculated from true parameter as @q_to_gui*@value + @off_to_gui.
 *
 * The factor and offset include any power-of-10 factors corresponding to the units @unitstr.  If @unitstr comes from
 * a #GwySIValueFormat, then @q_to_gui and @off_to_gui need to be divided by @magnitude.
 *
 * Since: 2.59
 **/
void
gwy_param_table_alt_set_linear(GwyParamTable *partable,
                               gint id,
                               gdouble q_to_gui,
                               gdouble off_to_gui,
                               const gchar *unitstr)
{
    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    g_return_if_fail(q_to_gui > 0.0);
    gwy_si_unit_value_format_set_units(partable->priv->vf, unitstr);
    partable->priv->vf->magnitude = 1;
    alt_set_from_value_format(partable, id, NULL, q_to_gui, off_to_gui);
}

/**
 * gwy_param_table_alt_set_calibration:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @calibration: Data line defining the calibration. It will be referenced by the parameter table.
 *
 * Defines a parameter table alternative value for a fraction slider using to a table given by a data line.
 *
 * The slider needs to have an alternative value set up using gwy_param_table_slider_add_alt().  The range [0,@res-1]
 * of true parameter values will correspond to the values in @calibration, in order. It has to contain a monotonically
 * increasing sequence of values.
 *
 * Modification of @calibration during the existence of @partable is not supported. The data line has to be static.
 *
 * Since: 2.62
 **/
void
gwy_param_table_alt_set_calibration(GwyParamTable *partable,
                                    gint id,
                                    GwyDataLine *calibration)
{
    GwyParamTablePrivate *priv;
    GwySIValueFormat *vf;
    GwyParamControl *control;
    GwyParamControlSlider *slider;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    /* TODO: We may want to allow resetting the alt to identity by passing NULL calibration, if it is useful. */
    g_return_if_fail(GWY_IS_DATA_LINE(calibration));
    control = find_first_control(partable, id);
    g_return_if_fail(control && control->type == GWY_PARAM_CONTROL_SLIDER);

    slider = (GwyParamControlSlider*)control->impl;
    if (!slider->has_alt) {
        g_warning("Slider has no alterative value set up.  Trying to add it now.");
        gwy_param_table_slider_add_alt(partable, id);
    }
    gwy_set_member_object(partable, calibration, GWY_TYPE_DATA_LINE, &slider->calibration, NULL);

    priv = partable->priv;
    vf = gwy_data_line_get_value_format_y(calibration, GWY_SI_UNIT_FORMAT_UNICODE, priv->vf);
    gwy_assign_string(&slider->alt_unitstr, vf->units);
    slider->alt_q_to_gui = 1.0/vf->magnitude;
    slider->alt_offset_to_gui = 0.0;
    compute_calibration_steps(calibration, &slider->calibration_step, &slider->calibration_delta);
    if (control->widget)
        slider_reconfigure_alt(control, partable);
}

/**
 * gwy_param_table_append_entry:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 *
 * Adds an entry to a parameter table.
 *
 * The parameter identified by @id must be a string, integer or double.  Other types may be supported in future.
 *
 * Since: 2.60
 **/
void
gwy_param_table_append_entry(GwyParamTable *partable,
                             gint id)
{
    GwyParamControl *control;
    GwyParamControlEntry *entry;
    GwyParams *params;
    const GwyParamDefItem *def;

    if (!find_def_common(partable, id, &params, &def))
        return;
    g_return_if_fail(def->type == GWY_PARAM_STRING || def->type == GWY_PARAM_INT || def->type == GWY_PARAM_DOUBLE);
    control = append_control(partable, GWY_PARAM_CONTROL_ENTRY, id, 1);
    control->impl = entry = g_slice_new0(GwyParamControlEntry);
    entry->width = -1;
    entry->str = partable->priv->str;
    entry->is_numeric = (def->type == GWY_PARAM_INT || def->type == GWY_PARAM_DOUBLE);
    entry->is_int = (def->type == GWY_PARAM_INT);
    if (partable->priv->widget)
        entry_make_control(partable, partable->priv->ncontrols-1, def);
}

/**
 * gwy_param_table_entry_set_width:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @width_chars: Entry width in characters.
 *
 * Sets the width of an entry in a parameter table.
 *
 * For numeric formats the automatic width should be sufficient.
 *
 * Since: 2.60
 **/
void
gwy_param_table_entry_set_width(GwyParamTable *partable,
                                gint id,
                                gint width_chars)
{
    GwyParamControl *control;
    GwyParamControlEntry *entry;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    control = find_first_control(partable, id);
    g_return_if_fail(control && control->type == GWY_PARAM_CONTROL_ENTRY);
    entry = (GwyParamControlEntry*)control->impl;
    if (entry->width == width_chars)
        return;
    entry->width = width_chars;
    if (control->widget)
        gtk_entry_set_width_chars(GTK_ENTRY(control->widget), width_chars);
}

/**
 * gwy_param_table_entry_set_value_format:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @vf: Value format to use.  The parameter table makes its own copy of the format.
 *
 * Sets the parsing and formatting for a numeric entry to a given value format.
 *
 * The parameter must be a double defined by gwy_param_def_add_double() or a predefined parameter of one of these
 * types.
 *
 * Setting the value format also sets the unit label accordingly.  If you want a different unit label you can override
 * it by using gwy_param_table_set_unitstr() after this function.
 *
 * Since: 2.60
 **/
void
gwy_param_table_entry_set_value_format(GwyParamTable *partable,
                                       gint id,
                                       GwySIValueFormat *vf)
{
    const GwyParamDefItem *def;
    GwyParams *params;
    GwyParamControl *control;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    control = find_first_control(partable, id);
    g_return_if_fail(control && (control->type == GWY_PARAM_CONTROL_ENTRY
                                 || control->type == GWY_PARAM_CONTROL_RANGE_FROM
                                 || control->type == GWY_PARAM_CONTROL_RANGE_TO));
    if (!find_def_common(partable, id, &params, &def))
        return;
    g_return_if_fail(def->type == GWY_PARAM_DOUBLE);
    if (control->type == GWY_PARAM_CONTROL_ENTRY) {
        GwyParamControlEntry *entry = (GwyParamControlEntry*)control->impl;
        if (entry->transform_destroy) {
            entry->transform_destroy(entry->transform_data);
            entry->transform_destroy = NULL;
        }
        if (vf) {
            entry->vf = gwy_si_unit_value_format_clone(vf, entry->vf);
            entry->transform_data = entry;
            entry->parse = entry_parse_double_vf;
            entry->format = entry_format_double_vf;
        }
        else {
            GWY_SI_VALUE_FORMAT_FREE(entry->vf);
            entry->transform_data = NULL;
            entry->parse = NULL;
            entry->format = NULL;
        }
        gwy_param_table_set_unitstr(partable, control->id, entry->vf ? entry->vf->units : NULL);
        if (control->widget)
            entry_output(partable, control);
    }
    else {
        GwyParamControlRangeFrom *range = range_get_impl(control, partable);
        GwyParamTablePrivate *priv = partable->priv;
        GwyParamControl *control_from, *control_to;

        g_return_if_fail(range);
        if (vf)
            range->vf = gwy_si_unit_value_format_clone(vf, range->vf);
        else
            GWY_SI_VALUE_FORMAT_FREE(range->vf);
        gwy_param_table_set_unitstr(partable, control->id, range->vf ? range->vf->units : NULL);

        if (control->type == GWY_PARAM_CONTROL_RANGE_FROM) {
            control_from = control;
            control_to = priv->controls + find_aux_for_control(partable, &priv->range, id);
        }
        else {
            control_from = priv->controls + find_control_for_aux(partable, &priv->range, id);
            control_to = control;
        }
        if (control_from->widget)
            range_output(partable, control_from);
        if (control_to->widget)
            range_output(partable, control_to);
    }
}

/**
 * gwy_param_table_entry_set_instant_changes:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @setting: %TRUE to enable instant changes; %FALSE to disable them.
 *
 * Enables or disables instant change signals of a parameter table entriy.
 *
 * With instant changes any edit causes a change of the parameter and emission of GwyParamTable::param-changed. By
 * default, both only occur only when the entry loses focus or upon entry activation (pressing Enter).
 *
 * Instant changes are not suitable for parameters with rectifiers because it can lead to the user and rectification
 * function fighting over the entry content.
 *
 * Since: 2.62
 **/
void
gwy_param_table_entry_set_instant_changes(GwyParamTable *partable,
                                          gint id,
                                          gboolean setting)
{
    GwyParamControl *control;
    GwyParamControlEntry *entry;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    control = find_first_control(partable, id);
    g_return_if_fail(control && control->type == GWY_PARAM_CONTROL_ENTRY);
    entry = (GwyParamControlEntry*)control->impl;
    if (!entry->instant_changes == !setting)
        return;
    entry->instant_changes = !!setting;
    if (control->widget) {
        if (setting)
            g_signal_connect(control->widget, "changed", G_CALLBACK(entry_changed), partable);
        else {
            g_signal_handlers_disconnect_matched(control->widget, G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA,
                                                 0, 0, NULL, entry_changed, partable);
        }
    }
}

/**
 * gwy_param_table_append_range:
 * @partable: Set of parameter value controls.
 * @id_from: Parameter identifier for the beginning.
 * @id_to: Parameter identifier for the end.
 *
 * Adds a pair of numerical entries representing a range to a parameter table.
 *
 * This is mostly useful for entering graph ranges numerically. For image ranges separate controls are preferred as
 * they allow pixel plus alternative real values.
 *
 * The two parameters identified by @id_from and @id_to must be doubles and distinct. Parameter @id_from is considered
 * primary and should be used for referring to the control as a whole (for instance to make it insensitive), even
 * though for convenience both are passed directly to this function.
 *
 * Use gwy_param_table_entry_set_value_format() to set the value format.
 *
 * Since: 2.63
 **/
void
gwy_param_table_append_range(GwyParamTable *partable,
                             gint id_from,
                             gint id_to)
{
    GwyParamControl *control;
    GwyParamControlRangeFrom *range;
    GwyParams *params;
    const GwyParamDefItem *def_from, *def_to;
    GwyParamTablePrivate *priv;

    g_return_if_fail(id_to != id_from);
    if (!find_def_common(partable, id_from, &params, &def_from)
        || !find_def_common(partable, id_to, &params, &def_to))
        return;
    g_return_if_fail(def_from->type == GWY_PARAM_DOUBLE);
    g_return_if_fail(def_to->type == GWY_PARAM_DOUBLE);

    /* This one is a bit odd. We construct the range as a main ‘from’ control and add the ‘to’ control as an
     * associated auxiliary control, similarly to enablers. It should not matter to the caller much. */
    priv = partable->priv;
    control = append_control(partable, GWY_PARAM_CONTROL_RANGE_FROM, id_from, 1);
    control->do_not_reset = TRUE;
    control->impl = range = g_slice_new0(GwyParamControlRangeFrom);
    range->str = priv->str;
    append_control(partable, GWY_PARAM_CONTROL_RANGE_TO, id_to, 0);

    add_aux_assoc(&priv->range, id_to, id_from);
    /* This will chain-construct both, similarly to enablers. */
    if (priv->widget)
        range_from_make_control(partable, priv->ncontrols-2, def_from);
}

/**
 * gwy_param_table_append_unit_chooser:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 *
 * Adds a unit chooser to a parameter table.
 *
 * The parameter identified by @id must be a unit parameter defined by gwy_param_def_add_unit().
 *
 * Since: 2.59
 **/
void
gwy_param_table_append_unit_chooser(GwyParamTable *partable,
                                    gint id)
{
    GwyParamControl *control;
    GwyParamControlUnitChooser *unit;
    GwyParams *params;
    const GwyParamDefItem *def;

    if (!find_def_common(partable, id, &params, &def))
        return;
    g_return_if_fail(def->type == GWY_PARAM_UNIT);
    control = append_control(partable, GWY_PARAM_CONTROL_UNIT_CHOOSER, id, 1);
    control->impl = unit = g_slice_new0(GwyParamControlUnitChooser);
    if (partable->priv->widget)
        unit_chooser_make_control(partable, partable->priv->ncontrols-1, def);
}

/**
 * gwy_param_table_append_color:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 *
 * Adds a generic colour button to a parameter table.
 *
 * The parameter identified by @id must be a colour parameter defined by gwy_param_def_add_color(). See
 * gwy_param_table_append_mask_color() for mask colours.
 *
 * Since: 2.64
 **/
void
gwy_param_table_append_color(GwyParamTable *partable,
                             gint id)
{
    GwyParamControl *control;
    GwyParamControlColor *colour;
    GwyParams *params;
    const GwyParamDefItem *def;

    if (!find_def_common(partable, id, &params, &def))
        return;
    g_return_if_fail(def->type == GWY_PARAM_COLOR);
    control = append_control(partable, GWY_PARAM_CONTROL_COLOR, id, 1);
    control->impl = colour = g_slice_new0(GwyParamControlColor);
    /* Do not allocate control->impl->presets if not needed. */
    if (partable->priv->widget)
        color_make_control(partable, partable->priv->ncontrols-1, def);
}

/**
 * gwy_param_table_color_add_preset:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @rgba: The predefined colour.
 * @name: Label to display alongside, possibly %NULL.
 *
 * Adds a preset colour to a generic colour button in a parameter table.
 *
 * Although @name may be %NULL, unlabelled presets look too similar to the colour button. So it is recommended to
 * label the colours.
 *
 * Since: 2.64
 **/
void
gwy_param_table_color_add_preset(GwyParamTable *partable,
                                 gint id,
                                 GwyRGBA rgba,
                                 const gchar *name)
{
    GwyParamControl *control;
    GwyParamControlColor *colour;
    GwyPresetColor preset;
    guint i;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    control = find_first_control(partable, id);
    g_return_if_fail(control && control->type == GWY_PARAM_CONTROL_COLOR);
    i = control - partable->priv->controls;
    colour = (GwyParamControlColor*)control->impl;

    if (!colour->presets)
        colour->presets = g_array_new(FALSE, FALSE, sizeof(GwyPresetColor));
    preset.name = name;
    preset.rgba = rgba;
    g_array_append_val(colour->presets, preset);
    if (control->widget) {
        make_color_button_preset(partable, i, colour->presets->len - 1);
        update_control_sensitivity(partable, i);
    }
}

/**
 * gwy_param_table_append_mask_color:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @preview_data: Data container for the preview data.
 * @preview_i: Id of the data field in @preview_data.
 * @data: Application data container with the mask (or %NULL).
 * @i: Id of the data field in @data (or -1).
 *
 * Adds a preview mask colour button to a parameter table.
 *
 * The mask colour will use the standard prefix "/0/mask" where 0 is replaced by @preview_i.  So it should be also
 * used when setting up the mask layer.
 *
 * It is possible to have multiple masks and colours in the preview data.  However, you need to consider that the
 * colour managed by this colour button will be given by @preview_i.
 *
 * Usually the mask colour should be initialised from the file using gwy_app_sync_data_items().  When the dialog is
 * finished it the mask colour should be set on the output again using gwy_app_sync_data_items().  If @data and @i are
 * supplied then this is done automatically, which is suitable for typical mask-creating modules.
 *
 * The parameter identified by @id must be a colour parameter defined by gwy_param_def_add_mask_color().
 *
 * Since: 2.59
 **/
void
gwy_param_table_append_mask_color(GwyParamTable *partable,
                                  gint id,
                                  GwyContainer *preview_data,
                                  gint preview_i,
                                  GwyContainer *data,
                                  gint i)
{
    GwyParamControl *control;
    GwyParamControlMaskColor *maskcolor;
    GwyParams *params;
    const GwyParamDefItem *def;
    const gchar *key;
    GwyRGBA color;

    if (!find_def_common(partable, id, &params, &def))
        return;
    g_return_if_fail(def->type == GWY_PARAM_COLOR);
    g_return_if_fail(GWY_IS_CONTAINER(preview_data));
    g_return_if_fail(!data || (GWY_IS_CONTAINER(data) && i >= 0));
    g_return_if_fail(preview_i >= 0);
    control = append_control(partable, GWY_PARAM_CONTROL_MASK_COLOR, id, 1);
    control->do_not_reset = TRUE;
    control->impl = maskcolor = g_slice_new0(GwyParamControlMaskColor);
    gwy_set_member_object(partable, preview_data, GWY_TYPE_CONTAINER, &maskcolor->preview_data, NULL);
    gwy_set_member_object(partable, data, GWY_TYPE_CONTAINER, &maskcolor->data, NULL);
    maskcolor->preview_i = preview_i;
    maskcolor->i = i;
    if (data)
        gwy_app_sync_data_items(data, preview_data, i, preview_i, FALSE, GWY_DATA_ITEM_MASK_COLOR, 0);
    key = g_quark_to_string(gwy_app_get_mask_key_for_id(preview_i));
    /* Try to load the colour from the preview data.  However, if there is not any, make sure we are in sync the other
     * way round. */
    if (gwy_rgba_get_from_container(&color, preview_data, key))
        gwy_params_set_color(params, id, color);
    else {
        color = gwy_params_get_color(params, id);
        gwy_rgba_store_to_container(&color, preview_data, key);
    }
    if (partable->priv->widget)
        mask_color_make_control(partable, partable->priv->ncontrols-1, def);
}

/**
 * gwy_param_table_append_button:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @sibling_id: Identifier of another button in the same row, or -1 for a new button row.
 * @response: Dialog response to emit.
 * @text: Text on the button.
 *
 * Adds a button to a parameter table.
 *
 * Action buttons occasionally appear inside parameter tables when they do not represent a global action but act on
 * a specific control.  For instance they can be used to run an automatic estimation of one specific parameter.
 *
 * Each button must have its own unique @id, different from all parameter identifiers.  This is best achieved by
 * taking them all from the same enum.  However, pressing the button does not change any parameter.  It emits
 * GtkDialog::response signal on the parent dialog, with response id given by @response.  Connect to this signal to
 * actually perform the action.
 *
 * A row with multiple buttons can be created by passing -1 as @sibling_id for the first button and then ids of some
 * of the previous buttons for the other buttons.
 *
 * Since: 2.59
 **/
void
gwy_param_table_append_button(GwyParamTable *partable,
                              gint id,
                              gint sibling_id,
                              gint response,
                              const gchar *text)
{
    GwyParamControl *control, *othercontrol;
    GwyParamControlButton *button;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    g_return_if_fail(id >= 0);
    if (sibling_id >= 0) {
        othercontrol = find_first_control(partable, sibling_id);
        /* The caller can pass any existing button as the sibling.  But we organise them in a neat sequence. */
        if (othercontrol && othercontrol->type == GWY_PARAM_CONTROL_BUTTON) {
            othercontrol = find_button_box_end(partable, othercontrol, TRUE);
            button = (GwyParamControlButton*)othercontrol->impl;
            button->sibling_id_next = id;
            sibling_id = othercontrol->id;
        }
        else {
            g_warning("There is no button with id=%d", sibling_id);
            sibling_id = -1;
        }
    }
    control = append_control(partable, GWY_PARAM_CONTROL_BUTTON, id, sibling_id < 0);
    control->impl = button = g_slice_new0(GwyParamControlButton);
    button->response = response;
    button->sibling_id_prev = MAX(sibling_id, -1);
    button->sibling_id_next = -1;
    gwy_assign_string(&button->label, text);
    if (partable->priv->widget)
        button_make_control(partable, partable->priv->ncontrols-1);
}

/**
 * gwy_param_table_append_results:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @results: A set of reported scalar values.
 * @result_id: String identifier of the first result in @result to add.
 * @...: %NULL-terminated list of more result identifiers.
 *
 * Adds a set of reported scalar variables to a parameter table.
 *
 * Results are not actual user modifiable settings.  Yet they often appear in parameter tables.  This function
 * integrates them to the parameter table.  It appends a single contiguous block of results.  Use it multiple times,
 * perhaps interspersed with gwy_param_table_append_header(), to create multiple blocks for the same @results object.
 *
 * Multiple result blocks can share one @id.  Functions such as gwy_param_table_results_fill() and
 * gwy_param_table_results_clear() then act on all blocks with given id.
 *
 * Since: 2.59
 **/
void
gwy_param_table_append_results(GwyParamTable *partable,
                               gint id,
                               GwyResults *results,
                               const gchar *result_id,
                               ...)
{
    va_list ap;
    const gchar **result_ids;
    guint n, i;

    n = 1;
    va_start(ap, result_id);
    while (va_arg(ap, const gchar*))
        n++;
    va_end(ap);

    result_ids = g_new(const gchar*, n+1);
    result_ids[0] = result_id;
    result_ids[n] = NULL;
    va_start(ap, result_id);
    for (i = 1; i < n; i++)
        result_ids[i] = va_arg(ap, const gchar*);
    va_end(ap);

    gwy_param_table_append_resultsv(partable, id, results, result_ids, n);
    g_free(result_ids);
}

/**
 * gwy_param_table_append_resultsv:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @results: A set of reported scalar values.
 * @result_ids: Array of string result identifiers in @results.
 * @nids: Number of items in @result_ids.  You can pass -1 if the array is %NULL-terminated.
 *
 * Adds a set of reported scalar variables to a parameter table.
 *
 * See gwy_param_table_append_results() for details.
 *
 * Since: 2.59
 **/
void
gwy_param_table_append_resultsv(GwyParamTable *partable,
                                gint id,
                                GwyResults *results,
                                const gchar **result_ids,
                                gint nids)
{
    GwyParamControl *control, *other_control;
    GwyParamControlResults *resultlist;
    gboolean wants_to_be_filled = FALSE;
    gint i;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    g_return_if_fail(GWY_IS_RESULTS(results));
    g_return_if_fail(id >= 0);
    if (nids < 0) {
        g_return_if_fail(GWY_IS_RESULTS(result_ids));
        nids = g_strv_length((gchar**)result_ids);
    }
    if ((other_control = find_first_control(partable, id))) {
        g_return_if_fail(other_control->type == GWY_PARAM_CONTROL_RESULTS);
        resultlist = (GwyParamControlResults*)other_control->impl;
        wants_to_be_filled = resultlist->wants_to_be_filled;
    }
    control = append_control(partable, GWY_PARAM_CONTROL_RESULTS, id, nids);
    control->impl = resultlist = g_slice_new0(GwyParamControlResults);
    resultlist->wants_to_be_filled = wants_to_be_filled;
    gwy_set_member_object(partable, results, GWY_TYPE_RESULTS, &resultlist->results, NULL);
    /* Cannot use g_strdupv() because it may not be NULL-terminated. */
    resultlist->result_ids = g_new(gchar*, nids+1);
    resultlist->nresults = nids;
    for (i = 0; i < nids; i++)
        resultlist->result_ids[i] = g_strdup(result_ids[i]);
    resultlist->result_ids[nids] = NULL;
    if (partable->priv->widget)
        results_make_control(partable, partable->priv->ncontrols-1);
}

/**
 * gwy_param_table_results_fill:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 *
 * Fills displayed values in a set of reported scalar variables in a parameter table.
 *
 * The identifier @id must correspond to a results block added by gwy_param_table_append_results() or a similar
 * function.
 *
 * Since: 2.59
 **/
void
gwy_param_table_results_fill(GwyParamTable *partable,
                             gint id)
{
    GwyParamControl *controls;
    GwyParamControlResults *resultlist;
    gint i, k, ncontrols;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    controls = partable->priv->controls;
    ncontrols = partable->priv->ncontrols;
    for (i = 0; i < ncontrols; i++) {
        if (controls[i].id != id)
            continue;
        g_return_if_fail(controls[i].type == GWY_PARAM_CONTROL_RESULTS);
        resultlist = (GwyParamControlResults*)controls[i].impl;
        resultlist->wants_to_be_filled = TRUE;
        if (!partable->priv->widget)
            continue;
        for (k = 0; k < resultlist->nresults; k++) {
            gtk_label_set_markup(GTK_LABEL(resultlist->value_labels[k]),
                                 gwy_results_get_full(resultlist->results, resultlist->result_ids[k]));
        }
    }
}

/**
 * gwy_param_table_results_clear:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 *
 * Clears all displayed values in a set of reported scalar variables in a parameter table.
 *
 * The identifier @id must correspond to a results block added by gwy_param_table_append_results() or a similar
 * function.
 *
 * Since: 2.59
 **/
void
gwy_param_table_results_clear(GwyParamTable *partable,
                              gint id)
{
    GwyParamControl *controls;
    GwyParamControlResults *resultlist;
    gint i, k, ncontrols;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    controls = partable->priv->controls;
    ncontrols = partable->priv->ncontrols;
    for (i = 0; i < ncontrols; i++) {
        if (controls[i].id != id)
            continue;
        g_return_if_fail(controls[i].type == GWY_PARAM_CONTROL_RESULTS);
        resultlist = (GwyParamControlResults*)controls[i].impl;
        resultlist->wants_to_be_filled = FALSE;
        if (!partable->priv->widget)
            continue;
        for (k = 0; k < resultlist->nresults; k++)
            gtk_label_set_markup(GTK_LABEL(resultlist->value_labels[k]), "");
    }
}

/**
 * gwy_param_table_append_report:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 *
 * Adds controls for report formatting to a parameter table.
 *
 * The parameter identified by @id must be a report type defined by gwy_param_def_add_report_type().
 *
 * You also need to provide means of report creation by gwy_param_table_report_set_results() or
 * gwy_param_table_report_set_formatter().  Otherwise the controls allow changing the format type parameter, but the
 * action buttons cannot do anything.
 *
 * Since: 2.59
 **/
void
gwy_param_table_append_report(GwyParamTable *partable,
                              gint id)
{
    GwyParamControl *control;
    GwyParamControlReport *report;
    GwyParams *params;
    const GwyParamDefItem *def;

    if (!find_def_common(partable, id, &params, &def))
        return;
    g_return_if_fail(def->type == GWY_PARAM_REPORT_TYPE);
    control = append_control(partable, GWY_PARAM_CONTROL_REPORT, id, 1);
    control->do_not_reset = TRUE;
    control->impl = report = g_slice_new0(GwyParamControlReport);
    if (partable->priv->widget)
        report_make_control(partable, partable->priv->ncontrols-1, def);
}

/**
 * gwy_param_table_report_set_results:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @results: A set of reported scalar values.
 *
 * Sets up report export controls in a parameter table to format #GwyResults.
 *
 * The results would be typically added to the table just above using gwy_param_table_append_results().  However, you
 * can use aribtrary #GwyResults object.
 *
 * Since: 2.59
 **/
void
gwy_param_table_report_set_results(GwyParamTable *partable,
                                   gint id,
                                   GwyResults *results)
{
    GwyParamControl *control;
    GwyParamControlReport *report;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    g_return_if_fail(!results || GWY_IS_RESULTS(results));
    control = find_first_control(partable, id);
    g_return_if_fail(control && control->type == GWY_PARAM_CONTROL_REPORT);

    report = (GwyParamControlReport*)control->impl;
    if (!gwy_set_member_object(partable, results, GWY_TYPE_RESULTS, &report->results, NULL))
        return;
    if (results && report->format_report) {
        gwy_debug("switching from formatting function %p to results %p", report->format_report, results);
        report_set_formatter(control, NULL, NULL, NULL);
        GWY_SIGNAL_HANDLER_DISCONNECT(control->widget, report->copy_sid);
        GWY_SIGNAL_HANDLER_DISCONNECT(control->widget, report->save_sid);
    }
    if (control->widget)
        gwy_results_export_set_results(GWY_RESULTS_EXPORT(control->widget), report->results);
}

/**
 * gwy_param_table_report_set_formatter:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @format_report: Function which will format the report for Copy and Save actions.
 * @user_data: Data passed to @format_report.
 * @destroy: Destroy notifier of @user_data or %NULL.
 *
 * Sets up report export controls in a parameter table to use a custom function to format the report.
 *
 * When using a custom formatting function, the report would typically be added just above as a #GtkTreeView or
 * a similar data display widget.
 *
 * Since: 2.59
 **/
void
gwy_param_table_report_set_formatter(GwyParamTable *partable,
                                     gint id,
                                     GwyCreateTextFunc format_report,
                                     gpointer user_data,
                                     GDestroyNotify destroy)
{
    GwyParamControl *control;
    GwyParamControlReport *report;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    control = find_first_control(partable, id);
    g_return_if_fail(control && control->type == GWY_PARAM_CONTROL_REPORT);

    report = (GwyParamControlReport*)control->impl;
    report_set_formatter(control, format_report, user_data, destroy);
    if (format_report && report->results) {
        gwy_debug("switching from results %p to formatting function %p", report->results, format_report);
        if (control->widget)
            gwy_results_export_set_results(GWY_RESULTS_EXPORT(control->widget), NULL);
        gwy_set_member_object(partable, NULL, GWY_TYPE_RESULTS, &report->results, NULL);
    }
    if (control->widget)
        report_ensure_actions(control, partable);
}

/**
 * gwy_param_table_append_seed:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 *
 * Adds a random seed parameter to a parameter table.
 *
 * The parameter identified by @id must be a random seed parameter defined by gwy_param_def_add_seed().
 *
 * Usually there is an associated boolean parameter controlling randomization which should be added just below using
 * gwy_param_table_append_checkbox().
 *
 * Since: 2.59
 **/
void
gwy_param_table_append_seed(GwyParamTable *partable,
                            gint id)
{
    GwyParamControl *control;
    GwyParamControlRandomSeed *randomseed;
    GwyParams *params;
    const GwyParamDefItem *def;

    if (!find_def_common(partable, id, &params, &def))
        return;
    g_return_if_fail(def->type == GWY_PARAM_RANDOM_SEED);
    control = append_control(partable, GWY_PARAM_CONTROL_RANDOM_SEED, id, 1);
    control->do_not_reset = TRUE;
    control->impl = randomseed = g_slice_new0(GwyParamControlRandomSeed);
    if (partable->priv->widget)
        random_seed_make_control(partable, partable->priv->ncontrols-1, def);
}

/**
 * gwy_param_table_append_hold_selection:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 *
 * Adds a selection holding parameter to a parameter table.
 *
 * The parameter identified by @id must be a selection holding flags parameter defined by
 * gwy_param_def_add_hold_selection().
 *
 * Since: 2.63
 **/
void
gwy_param_table_append_hold_selection(GwyParamTable *partable,
                                      gint id)
{
    GwyParamControl *control;
    GwyParamControlHoldSelection *holdsel;
    GwyParams *params;
    const GwyParamDefItem *def;

    if (!find_def_common(partable, id, &params, &def))
        return;
    g_return_if_fail(def->type == GWY_PARAM_FLAGS && def->def.f.gtype == GWY_TYPE_HOLD_SELECTION_FLAGS);
    control = append_control(partable, GWY_PARAM_CONTROL_HOLD_SELECTION, id, 1);
    control->do_not_reset = TRUE;
    control->impl = holdsel = g_slice_new0(GwyParamControlHoldSelection);
    if (partable->priv->widget)
        hold_selection_make_control(partable, partable->priv->ncontrols-1, def);
}

/**
 * gwy_param_table_append_message:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier (possibly -1).
 * @text: Message text (may be %NULL for initially empty message).
 *
 * Adds a simple message to a parameter table.
 *
 * Messages are not actual user modifiable settings.  Yet they often appear in parameter tables.  Each message must
 * have its own unique @id, different from all parameter identifiers, if you need to refer to it later.  This is best
 * achieved by taking them all from the same enum.  For static texts you can also pass -1 as @id; you will not be
 * able to refer to them later.
 *
 * Use gwy_param_table_set_label() to change the text later.  Use gwy_param_table_message_set_type() to set the
 * message type.
 *
 * This function is intended for unstructured and potentially long, even multiline texts.  They can take the full
 * width but cannot have unit text.  Use gwy_param_table_append_info() instead for information labels with the common
 * label-value-unit structure.
 *
 * Since: 2.59
 **/
void
gwy_param_table_append_message(GwyParamTable *partable,
                               gint id,
                               const gchar *text)
{
    GwyParamControl *control;
    GwyParamControlMessage *message;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    control = append_control(partable, GWY_PARAM_CONTROL_MESSAGE, id, 1);
    control->impl = message = g_slice_new0(GwyParamControlMessage);
    gwy_assign_string(&control->label_text, text);
    message->type = GTK_MESSAGE_INFO;
    if (partable->priv->widget)
        message_make_control(partable, partable->priv->ncontrols-1);
}

/**
 * gwy_param_table_append_info:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @label: Information label text.
 *
 * Add a structured informational value label to a parameter table.
 *
 * Informational values not modifiable by the users often appear in parameter tables.  Each must have its own unique
 * @id, different from all parameter identifiers, if you need to refer to it later.  This is best achieved by taking
 * them all from the same enum.  For static texts you can also pass -1 as @id; you will not be able to refer to them
 * later.
 *
 * Use gwy_param_table_info_set_valuestr() to set the value part of the information; use gwy_param_table_set_unitstr()
 * to set the unit part.
 *
 * This function is suitable for one-off labels.  For larger sets of values consider using #GwyResults and
 * gwy_param_table_append_results().  See also gwy_param_table_append_message() for unstructured texts.
 *
 * Since: 2.59
 **/
void
gwy_param_table_append_info(GwyParamTable *partable,
                            gint id,
                            const gchar *label)
{
    GwyParamControl *control;
    GwyParamControlInfo *info;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    control = append_control(partable, GWY_PARAM_CONTROL_INFO, id, 1);
    control->impl = info = g_slice_new0(GwyParamControlInfo);
    gwy_assign_string(&control->label_text, label);
    info->add_punctuation = TRUE;
    if (partable->priv->widget)
        info_make_control(partable, partable->priv->ncontrols-1);
}

/**
 * gwy_param_table_set_label:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @text: New label text (may be %NULL for the default text).
 *
 * Sets the label text of a control in a parameter table.
 *
 * Usually label texts are taken from parameter definitions.  This function modifies them dynamically.  It can also be
 * used to set the text of messages created by gwy_param_table_append_message().  Only controls which naturally have
 * labels can have the label set.  Some do not, for instance separators, results or foreign widgets.
 *
 * Since: 2.59
 **/
void
gwy_param_table_set_label(GwyParamTable *partable,
                          gint id,
                          const gchar *text)
{
    GwyParamControl *control;
    GwyParamControlType type;
    const GwyParamDefItem *def = NULL;
    const gchar *new_label;
    GtkWidget *alignment, *hbox;
    GString *str;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    control = find_first_control(partable, id);
    type = control->type;
    if (type == GWY_PARAM_CONTROL_SEPARATOR
        || type == GWY_PARAM_CONTROL_ENABLER
        || type == GWY_PARAM_CONTROL_RESULTS
        || type == GWY_PARAM_CONTROL_FOREIGN) {
        g_warning("Control does not have native label to modify.");
        return;
    }
    if (!gwy_assign_string(&control->label_text, text))
        return;

    /* Does not have its own (non-empty) default text and has label (not widget) as the main thing. */
    if (type == GWY_PARAM_CONTROL_MESSAGE) {
        if (control->label)
            gtk_label_set_markup(GTK_LABEL(control->label), control->label_text);
        return;
    }

    if (!control->widget)
        return;

    /* Does not have its own (non-empty) default text. */
    str = partable->priv->str;
    if (type == GWY_PARAM_CONTROL_HEADER) {
        g_string_assign(str, "<b>");
        g_string_append(str, control->label_text);
        g_string_assign(str, "/<b>");
        gtk_label_set_markup(GTK_LABEL(control->widget), str->str);
        return;
    }

    new_label = control->label_text;
    if (type == GWY_PARAM_CONTROL_BUTTON)
        control = find_button_box_end(partable, control, FALSE);
    else {
        /* The rest is reset to definition description. */
        if (!new_label) {
            if (!find_def_common(partable, id, NULL, &def))
                return;
            new_label = def->desc;
        }
    }

    /* XXX: Is this broken for some widgets when enablers are present? */
    if (type == GWY_PARAM_CONTROL_CHECKBOX)
        gtk_button_set_label(GTK_BUTTON(control->widget), new_label);
    else if (type == GWY_PARAM_CONTROL_SLIDER)
        gtk_label_set_markup(GTK_LABEL(gwy_adjust_bar_get_label(GWY_ADJUST_BAR(control->widget))), new_label);
    else if (type == GWY_PARAM_CONTROL_RANDOM_SEED
             || type == GWY_PARAM_CONTROL_ENTRY
             || type == GWY_PARAM_CONTROL_RANGE_FROM /* FIXME FIXME FIXME: correct? */
             || type == GWY_PARAM_CONTROL_RANGE_TO)
        gtk_label_set_markup(GTK_LABEL(control->label), new_label);
    else if (type == GWY_PARAM_CONTROL_COMBO
             || type == GWY_PARAM_CONTROL_IMAGE_ID_COMBO
             || type == GWY_PARAM_CONTROL_GRAPH_ID_COMBO
             || type == GWY_PARAM_CONTROL_VOLUME_ID_COMBO
             || type == GWY_PARAM_CONTROL_XYZ_ID_COMBO
             || type == GWY_PARAM_CONTROL_CURVE_MAP_ID_COMBO
             || type == GWY_PARAM_CONTROL_GRAPH_CURVE_COMBO
             || type == GWY_PARAM_CONTROL_LAWN_CURVE_COMBO
             || type == GWY_PARAM_CONTROL_LAWN_SEGMENT_COMBO
             || type == GWY_PARAM_CONTROL_RADIO_ROW
             || type == GWY_PARAM_CONTROL_RADIO_BUTTONS
             || type == GWY_PARAM_CONTROL_BUTTON
             || type == GWY_PARAM_CONTROL_COLOR
             || type == GWY_PARAM_CONTROL_MASK_COLOR
             || type == GWY_PARAM_CONTROL_INFO
             || type == GWY_PARAM_CONTROL_UNIT_CHOOSER) {
        gboolean remove_underline = (type == GWY_PARAM_CONTROL_RADIO_ROW
                                     || type == GWY_PARAM_CONTROL_RADIO_BUTTONS
                                     || type == GWY_PARAM_CONTROL_BUTTON);
        new_label = new_label ? modify_label(partable, new_label, TRUE, remove_underline) : NULL;
        if (control->label && new_label)
            gtk_label_set_markup(GTK_LABEL(control->label), new_label);
        else if (control->label && !new_label) {
            alignment = gtk_widget_get_parent(control->label);
            g_return_if_fail(GTK_IS_ALIGNMENT(alignment));
            gtk_widget_destroy(alignment);
            control->label = NULL;
        }
        else if (!control->label && new_label) {
            control->label = gtk_label_new(new_label);
            alignment = add_right_padding(control->label, GWY_PARAM_TABLE_COLSEP);
            hbox = gtk_widget_get_parent(control->widget);
            g_return_if_fail(GTK_IS_HBOX(hbox));
            gtk_box_pack_start(GTK_BOX(hbox), alignment, FALSE, FALSE, 0);
        }
    }
    else if (type == GWY_PARAM_CONTROL_RADIO
             || type == GWY_PARAM_CONTROL_RADIO_HEADER
             || type == GWY_PARAM_CONTROL_RADIO_ITEM
             || type == GWY_PARAM_CONTROL_CHECKBOXES) {
        if (new_label && !control->label) {
            g_warning("Cannot modify list header text if it does not exist.");
            return;
        }
        new_label = new_label ? modify_label(partable, new_label, TRUE, TRUE) : "";
        gtk_label_set_markup(GTK_LABEL(control->label), new_label);
    }
    else if (type == GWY_PARAM_CONTROL_REPORT)
        gwy_results_export_set_title(GWY_RESULTS_EXPORT(control->widget), control->label_text);
    else {
        g_assert_not_reached();
    }
}

/**
 * gwy_param_table_info_set_valuestr:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier of the informational value label (not a new identifier).
 * @text: Value text (may be %NULL for empty value).
 *
 * Sets the value text of a informational value label in a parameter table.
 *
 * The value text is right-aligned and placed to the right part of the row.  This can be used to create simple
 * structured label-value-units messages.
 *
 * Since: 2.59
 **/
void
gwy_param_table_info_set_valuestr(GwyParamTable *partable,
                                  gint id,
                                  const gchar *text)
{
    GwyParamControl *control;
    GwyParamControlInfo *info;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    control = find_first_control(partable, id);
    g_return_if_fail(control && control->type == GWY_PARAM_CONTROL_INFO);
    info = (GwyParamControlInfo*)control->impl;
    if (!gwy_assign_string(&info->valuestr, text))
        return;
    if (!partable->priv->widget)
        return;
    g_assert(control->widget);
    gtk_label_set_markup(GTK_LABEL(control->widget), text);
}

/**
 * gwy_param_table_info_set_add_punctuation:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier of the informational value label (not a new identifier).
 * @setting: %TRUE to add punctuation (usually a colon), %FALSE to keep the label intact.
 *
 * Sets whether standard punctuation is added to an informational value label in a parameter table.
 *
 * The default behaviour is to add the same punctuation as to parameter labels. Use this function to disable it.
 *
 * Since: 2.62
 **/
void
gwy_param_table_info_set_add_punctuation(GwyParamTable *partable,
                                         gint id,
                                         gboolean setting)
{
    GwyParamControl *control;
    GwyParamControlInfo *info;
    const gchar *text;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    control = find_first_control(partable, id);
    g_return_if_fail(control && control->type == GWY_PARAM_CONTROL_INFO);
    info = (GwyParamControlInfo*)control->impl;
    if (!setting == !info->add_punctuation)
        return;
    info->add_punctuation = setting;
    if (!partable->priv->widget)
        return;
    g_assert(control->label);

    text = modify_label(partable, control->label_text, setting, FALSE);
    gtk_label_set_text(GTK_LABEL(control->label), text);
}

/**
 * gwy_param_table_message_set_type:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier of the message.
 * @type: Message type.
 *
 * Sets the type of a message in a parameter table.
 *
 * This function modifies the visual style of the text according to the given type, the default %GTK_MESSAGE_INFO
 * corresponding to a neutral presentation.  Not all types are equally meaningful in a parameter table.  Do not use
 * %GTK_MESSAGE_QUESTION and %GTK_MESSAGE_OTHER.
 *
 * If the message has a value text set by gwy_param_table_message_set_value_text() both are styled the same.
 *
 * Since: 2.59
 **/
void
gwy_param_table_message_set_type(GwyParamTable *partable,
                                 gint id,
                                 GtkMessageType type)
{
    GwyParamControl *control;
    GwyParamControlMessage *message;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    control = find_first_control(partable, id);
    g_return_if_fail(control && control->type == GWY_PARAM_CONTROL_MESSAGE);
    message = (GwyParamControlMessage*)control->impl;
    if (type == message->type)
        return;
    message->type = type;
    if (partable->priv->widget)
        message_update_type(control);
}

/**
 * gwy_param_table_append_foreign:
 * @partable: Set of parameter value controls.
 * @id: Parameter identifier.
 * @create_widget: Function to create the widget.
 * @user_data: The data passed to @create_widget.
 * @destroy: Destroy notifier of @user_data or %NULL.
 *
 * Adds a widget that is not supported natively to a parameter table.
 *
 * This function takes a function instead of the widget itself.  If the table widget is destroyed and recreated then
 * @create_widget can be called multiple times.  Typically, however, it will just be called once.  The destroy notifier
 * will only be called when @partable itself is destroyed.
 *
 * The created widget must not be indepdendently destroyed while the table widget exists.  If the widget is
 * a container, like #GtkBox, it will give you considerable freedom to change its contents later.
 *
 * The identifier @id may be passed as -1 if you are not interested in referring to the widget using #GwyParamTable
 * functions.  A real identifier enables some rudimentary functionality such as gwy_param_table_exists() and
 * gwy_param_table_set_sensitive().
 *
 * Since: 2.59
 **/
void
gwy_param_table_append_foreign(GwyParamTable *partable,
                               gint id,
                               GwyCreateWidgetFunc create_widget,
                               gpointer user_data,
                               GDestroyNotify destroy)
{
    GwyParamControl *control;
    GwyParamControlForeign *foreign;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    g_return_if_fail(create_widget);
    control = append_control(partable, GWY_PARAM_CONTROL_FOREIGN, id, 1);
    control->impl = foreign = g_slice_new0(GwyParamControlForeign);
    foreign->create_widget = create_widget;
    foreign->user_data = user_data;
    foreign->destroy = destroy;
    if (partable->priv->widget)
        foreign_make_control(partable, partable->priv->ncontrols-1);
}

void
_gwy_param_table_in_update(GwyParamTable *partable,
                           gboolean is_in_update)
{
    GwyParamTablePrivate *priv = partable->priv;

    gwy_debug("(%p) in_update = %d -> %d", partable, priv->in_update, priv->in_update + (is_in_update ? 1 : -1));
    if (is_in_update)
        priv->in_update++;
    else {
        g_assert(priv->in_update > 0);
        priv->in_update--;
    }
}

void
_gwy_param_table_set_parent_dialog(GwyParamTable *partable,
                                   GwyDialog *dialog)
{
    GwyParamTablePrivate *priv = partable->priv;

    g_return_if_fail(!dialog || GWY_IS_DIALOG(dialog));
    if (dialog == priv->parent_dialog)
        return;

    g_return_if_fail(!priv->parent_tool);
    g_return_if_fail(!dialog || !priv->parent_dialog);
    priv->parent_dialog = dialog;
}

void
_gwy_param_table_set_parent_plain_tool(GwyParamTable *partable,
                                       GwyPlainTool *plain_tool)
{
    GwyParamTablePrivate *priv = partable->priv;

    g_return_if_fail(!plain_tool || GWY_IS_PLAIN_TOOL(plain_tool));
    if (plain_tool == priv->parent_tool)
        return;

    g_return_if_fail(!priv->parent_dialog);
    g_return_if_fail(!plain_tool || !priv->parent_tool);
    priv->parent_tool = plain_tool;
}

void
_gwy_param_table_proceed(GwyParamTable *partable)
{
    partable->priv->proceed = TRUE;
}

static GwyParamControl*
find_first_control(GwyParamTable *partable, gint id)
{
    GwyParamTablePrivate *priv = partable->priv;
    guint k, n = priv->ncontrols;

    /* Do not find random junk if we somehow get passed id = -1. */
    if (id < 0)
        return NULL;

    for (k = 0; k < n; k++) {
        GwyParamControl *control = priv->controls + k;
        if (control->id == id)
            return control;
    }

    return NULL;
}

static gboolean
find_def_common(GwyParamTable *partable, gint id, GwyParams **pparams, const GwyParamDefItem **def)
{
    GwyParamTablePrivate *priv;
    GwyParamDef *pardef;
    GwyParams *params;

    g_return_val_if_fail(GWY_IS_PARAM_TABLE(partable), FALSE);
    priv = partable->priv;
    params = priv->params;
    g_return_val_if_fail(GWY_IS_PARAMS(params), FALSE);
    pardef = gwy_params_get_def(params);
    g_return_val_if_fail(GWY_IS_PARAM_DEF(pardef), FALSE);
    *def = _gwy_param_def_item(pardef, _gwy_param_def_index(pardef, id));
    g_return_val_if_fail(*def, FALSE);
    if (pparams)
        *pparams = params;

    return TRUE;
}

/* The arguments must be lhs without side effects, not general expressions. */
#define extend_open_coded_array(array,n,nalloc,type) \
    G_STMT_START \
    if (n == nalloc) { \
        nalloc = MAX(2*nalloc, nalloc + 4); \
        array = g_renew(type, array, nalloc); \
    } \
    G_STMT_END

static GwyParamControl*
append_control(GwyParamTable *partable, GwyParamControlType type, gint id, gint nrows)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control;

    extend_open_coded_array(priv->controls, priv->ncontrols, priv->nalloc_control, GwyParamControl);
    control = priv->controls + priv->ncontrols;
    gwy_clear(control, 1);
    control->type = type;
    /* Normalise negative ids to -1. */
    control->id = MAX(id, -1);
    control->sensitive = TRUE;
    priv->nrows += control->nrows = nrows;
    priv->ncontrols++;

    return control;
}

static void
add_aux_assoc(GwyParamControlAssocTable *assoc_table, gint aux_id, gint other_id)
{
    extend_open_coded_array(assoc_table->assoc, assoc_table->n, assoc_table->nalloc, GwyParamControlAssoc);
    assoc_table->assoc[assoc_table->n].aux_id = aux_id;
    assoc_table->assoc[assoc_table->n].other_id = other_id;
    assoc_table->n++;
}

static void
add_toggles_info(GwyParamTable *partable, gint id, gboolean must_not_exist)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyToggleListInfo *toggle_info;

    if ((toggle_info = find_toggles_info(partable, id))) {
        g_return_if_fail(!must_not_exist);
        return;
    }
    extend_open_coded_array(priv->toggles_info, priv->ntoggles, priv->nalloc_toggles, GwyToggleListInfo);
    priv->toggles_info[priv->ntoggles].id = id;
    priv->toggles_info[priv->ntoggles].sensitive_bits = G_MAXUINT32;
    priv->toggles_info[priv->ntoggles].sensitive = TRUE;
    priv->ntoggles++;
}

static GtkWidget*
ensure_widget(GwyParamTable *partable)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *controls;
    GwyParamDef *pardef;
    GwyParams *params;
    GtkWidget *widget;
    GtkTable *table;
    guint k, n, row;

    if (priv->widget)
        return priv->widget;

    params = priv->params;

    widget = priv->widget = gtk_table_new(priv->nrows, priv->ncols, FALSE);
    priv->destroy_sid = g_signal_connect(widget, "destroy", G_CALLBACK(widget_dispose), partable);
    table = GTK_TABLE(widget);
    gtk_table_set_row_spacings(table, GWY_PARAM_TABLE_ROWSEP);
    gtk_table_set_col_spacings(table, GWY_PARAM_TABLE_COLSEP);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);

    g_return_val_if_fail(GWY_IS_PARAMS(params), widget);
    pardef = gwy_params_get_def(params);
    g_return_val_if_fail(GWY_IS_PARAM_DEF(pardef), widget);

    controls = priv->controls;
    n = priv->ncontrols;
    row = 0;
    gwy_debug("nalloc_control %u, ncontrols %u", priv->nalloc_control, priv->ncontrols);
    for (k = 0; k < n; k++) {
        GwyParamControlType type = controls[k].type;
        const GwyParamDefItem *def;

        gwy_debug("[%u] type %d (%d rows)", k, type, controls[k].nrows);
        controls[k].row = row;
        row += controls[k].nrows;
        if (type == GWY_PARAM_CONTROL_SEPARATOR)
            continue;
        if (type == GWY_PARAM_CONTROL_ENABLER || type == GWY_PARAM_CONTROL_RANGE_TO)
            continue;
        if (type == GWY_PARAM_CONTROL_HEADER)
            header_make_control(partable, k);
        else if (type == GWY_PARAM_CONTROL_BUTTON)
            button_make_control(partable, k);
        else if (type == GWY_PARAM_CONTROL_RESULTS)
            results_make_control(partable, k);
        else if (type == GWY_PARAM_CONTROL_MESSAGE)
            message_make_control(partable, k);
        else if (type == GWY_PARAM_CONTROL_INFO)
            info_make_control(partable, k);
        else if (type == GWY_PARAM_CONTROL_FOREIGN)
            foreign_make_control(partable, k);
        else {
            def = _gwy_param_def_item(pardef, _gwy_param_def_index(pardef, controls[k].id));
            if (type == GWY_PARAM_CONTROL_CHECKBOX)
                checkbox_make_control(partable, k, def);
            else if (type == GWY_PARAM_CONTROL_COMBO)
                combo_make_control(partable, k, def);
            else if (type == GWY_PARAM_CONTROL_RADIO)
                radio_make_control(partable, k, def);
            else if (type == GWY_PARAM_CONTROL_CHECKBOXES)
                checkboxes_make_control(partable, k, def);
            else if (type == GWY_PARAM_CONTROL_RADIO_HEADER)
                radio_header_make_control(partable, k, def);
            else if (type == GWY_PARAM_CONTROL_RADIO_ITEM)
                radio_item_make_control(partable, k, def);
            else if (type == GWY_PARAM_CONTROL_RADIO_ROW)
                radio_row_make_control(partable, k, def);
            else if (type == GWY_PARAM_CONTROL_RADIO_BUTTONS)
                radio_buttons_make_control(partable, k, def);
            else if (control_is_some_kind_of_data_id(type))
                data_id_make_control(partable, k, def);
            else if (control_is_some_kind_of_curve_no(type))
                curve_no_make_control(partable, k, def);
            else if (type == GWY_PARAM_CONTROL_SLIDER)
                slider_make_control(partable, k, def);
            else if (type == GWY_PARAM_CONTROL_ENTRY)
                entry_make_control(partable, k, def);
            else if (type == GWY_PARAM_CONTROL_RANGE_FROM)
                range_from_make_control(partable, k, def);
            else if (type == GWY_PARAM_CONTROL_COLOR)
                color_make_control(partable, k, def);
            else if (type == GWY_PARAM_CONTROL_MASK_COLOR)
                mask_color_make_control(partable, k, def);
            else if (type == GWY_PARAM_CONTROL_UNIT_CHOOSER)
                unit_chooser_make_control(partable, k, def);
            else if (type == GWY_PARAM_CONTROL_REPORT)
                report_make_control(partable, k, def);
            else if (type == GWY_PARAM_CONTROL_HOLD_SELECTION)
                hold_selection_make_control(partable, k, def);
            else if (type == GWY_PARAM_CONTROL_RANDOM_SEED)
                random_seed_make_control(partable, k, def);
            else {
                g_assert_not_reached();
            }
        }
    }

    return g_object_ref_sink(widget);
}

static void
slider_set_aux_sensitive(GwyParamControl *control, gboolean sensitive)
{
    GwyParamControlSlider *slider = (GwyParamControlSlider*)control->impl;

    if (control->unitlabel)
        gtk_widget_set_sensitive(control->unitlabel, sensitive);
    if (slider->alt_spin)
        gtk_widget_set_sensitive(slider->alt_spin, sensitive);
    if (slider->alt_unitlabel)
        gtk_widget_set_sensitive(slider->alt_unitlabel, sensitive);
}

static void
update_control_sensitivity(GwyParamTable *partable, gint i)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    gboolean sensitive = control->sensitive;
    GwyParamControl *enabler_control = NULL;
    gboolean enabler_on = TRUE, item_sens;
    const GwyParamDefItem *def;
    GwyParamControlType type;
    GwyToggleListInfo *toggles_info;
    GtkWidget *hbox;
    gint id, ienabler;

    g_return_if_fail(i >= 0 && i < priv->ncontrols);
    id = control->id;

    /* The logic with enablers is that the enabler checkbox controls the sensitivity of everything else, but not self.
     * If the parameter is PARAM_FOO and you make it insensitive, if makes insensitive also the enabler.  Setting the
     * sensitivity of just the enabler might also work (not sure why one would do that).
     *
     * 1. If a control->sensitive = FALSE, make it completely insensitive, including any enabler (works sort of
     *    natively with GwyAdjustBar).
     * 2. Otherwise, if control has an enabler and it is off, make it completely insensitive except the enabler (works
     *    sort of natively with GwyAdjustBar).
     * 3. Otherwise the control is sensitive, except for single radio buttons which can be disabled individually.
     */
    if ((ienabler = find_aux_for_control(partable, &priv->enabler, id)) >= 0) {
        enabler_control = priv->controls + ienabler;
        gwy_debug("found enabler %d for control %d", enabler_control->id, id);
        enabler_on = gwy_params_get_boolean(priv->params, enabler_control->id);
    }

    type = control->type;
    if (type == GWY_PARAM_CONTROL_SEPARATOR || type == GWY_PARAM_CONTROL_RESULTS) {
        g_warning("Trying to update sensitivity of auxiliary widget.  How did we get here?");
        return;
    }
    if (type == GWY_PARAM_CONTROL_ENABLER) {
        g_warning("Trying to set sensitivity of an enabler.  Do you really need this?");
        return;
    }

    gwy_debug("sensitive %d, enabler %d", sensitive, enabler_on);
    if (type == GWY_PARAM_CONTROL_SLIDER) {
        GwyParamControlSlider *slider = (GwyParamControlSlider*)control->impl;
        if (!sensitive || !enabler_on) {
            gtk_widget_set_sensitive(slider->spin, FALSE);
            slider_set_aux_sensitive(control, FALSE);
        }
        if (!sensitive) {
            /* This sets insensitive also the integrated checkbox, if any. */
            gtk_widget_set_sensitive(control->widget, FALSE);
            return;
        }
        gtk_widget_set_sensitive(control->widget, TRUE);
        if (!enabler_on) {
            gwy_adjust_bar_set_bar_sensitive(GWY_ADJUST_BAR(control->widget), FALSE);
            slider_set_aux_sensitive(control, FALSE);
            return;
        }
        gtk_widget_set_sensitive(control->widget, TRUE);
        gwy_adjust_bar_set_bar_sensitive(GWY_ADJUST_BAR(control->widget), TRUE);
        gtk_widget_set_sensitive(slider->spin, TRUE);
        slider_set_aux_sensitive(control, TRUE);
        return;
    }

    if (type == GWY_PARAM_CONTROL_REPORT) {
        /* Just set the action buttons; keep the format controls sensitive.  There is no scenario in which you could
         * not fiddle with them. */
        gwy_results_export_set_actions_sensitive(GWY_RESULTS_EXPORT(control->widget), sensitive);
        return;
    }
    if (type == GWY_PARAM_CONTROL_CHECKBOX || type == GWY_PARAM_CONTROL_HEADER || type == GWY_PARAM_CONTROL_FOREIGN) {
        gtk_widget_set_sensitive(control->widget, sensitive);
        if (control->unitlabel)
            gtk_widget_set_sensitive(control->unitlabel, sensitive);
        return;
    }
    if (type == GWY_PARAM_CONTROL_BUTTON) {
        GwyParamControl *firstcontrol = find_button_box_end(partable, control, FALSE);

        gtk_widget_set_sensitive(control->widget, sensitive);
        /* We can have multiple buttons in a row.  The label and unitstr should be sensitive if any button is
         * sensitive.  This covers the single-button case and behaves naturally in the multi-button case.
         *
         * Fields label and unitlabel are set only in the first button struct. */
        if (firstcontrol->label || firstcontrol->unitlabel) {
            item_sens = button_box_has_any_sensitive(partable, firstcontrol);
            if (firstcontrol->label)
                gtk_widget_set_sensitive(firstcontrol->label, item_sens);
            if (firstcontrol->unitlabel)
                gtk_widget_set_sensitive(firstcontrol->unitlabel, item_sens);
        }
        return;
    }
    if (type == GWY_PARAM_CONTROL_MESSAGE) {
        /* Messages have control->label as the main widget. */
        gtk_widget_set_sensitive(control->label, sensitive);
        return;
    }
    if (type == GWY_PARAM_CONTROL_RANDOM_SEED) {
        GwyParamControlRandomSeed *randomseed = (GwyParamControlRandomSeed*)control->impl;
        gtk_widget_set_sensitive(control->label, sensitive);
        gtk_widget_set_sensitive(control->widget, sensitive);
        gtk_widget_set_sensitive(randomseed->new_button, sensitive);
        return;
    }
    if (type == GWY_PARAM_CONTROL_ENTRY) {
        if (!enabler_on)
            sensitive = FALSE;
        gtk_widget_set_sensitive(control->widget, sensitive);
        if (control->label)
            gtk_widget_set_sensitive(control->label, sensitive);
        if (control->unitlabel)
            gtk_widget_set_sensitive(control->unitlabel, sensitive);
        return;
    }

    if (type == GWY_PARAM_CONTROL_UNIT_CHOOSER) {
        GwyParamControlUnitChooser *unit = (GwyParamControlUnitChooser*)control->impl;
        gtk_widget_set_sensitive(unit->change_button, sensitive);
        /* And continue to the hbox case. */
    }

    toggles_info = find_toggles_info(partable, id);
    if (toggles_info)
        sensitive = toggles_info->sensitive;

    gwy_debug("has hbox: %d", control_has_hbox(type));
    if (control_has_hbox(type)) {
        if (control_is_some_kind_of_curve_no(type) && !curve_no_get_ncurves(control))
            sensitive = FALSE;

        hbox = gtk_widget_get_ancestor(control->widget, GTK_TYPE_HBOX);
        gwy_debug("hbox: %p", hbox);
        g_return_if_fail(hbox);
        /* HBoxes without enablers are easy; we can just set the sensitivity of the box. And possibly the units. */
        if (!sensitive) {
            gwy_debug("making entire hbox insensitive");
            gtk_widget_set_sensitive(hbox, FALSE);
            if (control->unitlabel)
                gtk_widget_set_sensitive(control->unitlabel, FALSE);
            return;
        }
        gwy_debug("making hbox sensitive");
        gtk_widget_set_sensitive(hbox, TRUE);

        if (enabler_control) {
            GwyParamControlEnabler *enabler = (GwyParamControlEnabler*)enabler_control->impl;
            GList *list = gtk_container_get_children(GTK_CONTAINER(hbox));

            while (list) {
                gwy_debug("making hbox children %ssensitive", enabler_on ? "" : "in");
                if (list->data != enabler->container_child)
                    gtk_widget_set_sensitive(GTK_WIDGET(list->data), enabler_on);
                list = g_list_next(list);
            }
            if (control->unitlabel)
                gtk_widget_set_sensitive(control->unitlabel, enabler_on);
            if (!enabler_on)
                return;
        }
        if (control->unitlabel)
            gtk_widget_set_sensitive(control->unitlabel, TRUE);

        /* Selection holding flags buttons have a specific cascading logic. */
        if (type == GWY_PARAM_CONTROL_HOLD_SELECTION) {
            GwyParamControlHoldSelection *holdsel = (GwyParamControlHoldSelection*)control->impl;
            GwyHoldSelectionFlags flags = gwy_params_get_flags(priv->params, id);
            gboolean hold_enabled = flags & GWY_HOLD_SELECTION_HOLD;
            gboolean overwrite_enabled = flags & GWY_HOLD_SELECTION_OVERWRITE;

            gtk_widget_set_sensitive(holdsel->overwrite, hold_enabled);
            gtk_widget_set_sensitive(holdsel->clear, hold_enabled && overwrite_enabled);
        }

        /* If the row contains lists of radio buttons and it is sensitive, we must check also individual buttons. */
        if (type != GWY_PARAM_CONTROL_RADIO_ROW && type != GWY_PARAM_CONTROL_RADIO_BUTTONS)
            return;
    }

    g_return_if_fail(toggles_info);

    if (type == GWY_PARAM_CONTROL_RADIO_HEADER) {
        gtk_widget_set_sensitive(control->label, sensitive);
        return;
    }

    if (type == GWY_PARAM_CONTROL_RADIO || type == GWY_PARAM_CONTROL_CHECKBOXES) {
        if (control->label)
            gtk_widget_set_sensitive(control->label, sensitive);
    }

    if (!find_def_common(partable, id, NULL, &def))
        return;

    if (type == GWY_PARAM_CONTROL_CHECKBOXES) {
        GSList *list = gwy_check_box_get_group(control->widget);

        while (list) {
            if ((item_sens = sensitive))
                item_sens = toggles_info->sensitive_bits & gwy_check_box_get_value(GTK_WIDGET(list->data));
            gtk_widget_set_sensitive(GTK_WIDGET(list->data), item_sens);
            list = g_slist_next(list);
        }
        return;
    }

    g_return_if_fail(control_is_some_kind_of_radio(type));

    if (type == GWY_PARAM_CONTROL_RADIO_ITEM) {
        if ((item_sens = sensitive)) {
            GwyParamControlRadioItem *radioitem = (GwyParamControlRadioItem*)control->impl;
            item_sens = toggles_info->sensitive_bits & bit_mask_for_enum_value(def, radioitem->value);
        }
        gtk_widget_set_sensitive(control->widget, item_sens);
        return;
    }

    if (type == GWY_PARAM_CONTROL_RADIO
        || type == GWY_PARAM_CONTROL_RADIO_ROW
        || type == GWY_PARAM_CONTROL_RADIO_BUTTONS) {
        GSList *list = gtk_radio_button_get_group(GTK_RADIO_BUTTON(control->widget));

        while (list) {
            if ((item_sens = sensitive)) {
                gint value = gwy_radio_button_get_value(GTK_WIDGET(list->data));
                item_sens = toggles_info->sensitive_bits & bit_mask_for_enum_value(def, value);
            }
            gtk_widget_set_sensitive(GTK_WIDGET(list->data), item_sens);
            list = g_slist_next(list);
        }
        return;
    }

    g_assert_not_reached();
}

static void
header_make_control(GwyParamTable *partable, guint i)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    gint row = control->row;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_HEADER);
    make_control_common(partable, i);

    control->widget = gwy_label_new_header(control->label_text);
    gwy_debug("attach %s at %d..%d", g_type_name(G_TYPE_FROM_INSTANCE(control->widget)), row, row+1);
    gtk_table_attach(GTK_TABLE(priv->widget), control->widget, 0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    gtk_widget_show(control->widget);
    update_control_unit_label(control, partable);
}

static void
checkbox_make_control(GwyParamTable *partable, guint i, const GwyParamDefItem *def)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    gint row = control->row;
    const gchar *label;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_CHECKBOX);
    make_control_common(partable, i);

    label = (control->label_text ? control->label_text : def->desc);
    control->widget = gtk_check_button_new_with_mnemonic(label);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(control->widget),
                                 gwy_params_get_boolean(priv->params, control->id));
    gwy_debug("attach %s at %d..%d", g_type_name(G_TYPE_FROM_INSTANCE(control->widget)), row, row+1);
    gtk_table_attach(GTK_TABLE(priv->widget), control->widget, 0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    gtk_widget_show(control->widget);
    g_object_set_qdata(G_OBJECT(control->widget), param_control_quark, GUINT_TO_POINTER(i));
    g_signal_connect(control->widget, "toggled", G_CALLBACK(checkbox_toggled), partable);
    update_control_unit_label(control, partable);
    update_control_sensitivity(partable, i);
}

static void
enabler_make_control(GwyParamTable *partable, guint i, guint iother)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParams *params = priv->params;
    GwyParamControl *control = priv->controls + i, *other_control = priv->controls + iother;
    const GwyParamDefItem *otherdef;

    gwy_debug("enabler control(%u) type %d", i, control->type);
    gwy_debug("other control(%u) type %d", iother, other_control->type);
    g_return_if_fail(control->type == GWY_PARAM_CONTROL_ENABLER);
    if (!find_def_common(partable, other_control->id, NULL, &otherdef))
        return;
    control->row = other_control->row;

    if (other_control->type == GWY_PARAM_CONTROL_SLIDER) {
        /* GwyAdjustBar has native check button support. */
        GwyAdjustBar *adjbar = GWY_ADJUST_BAR(other_control->widget);

        gwy_adjust_bar_set_has_check_button(adjbar, TRUE);
        control->widget = gwy_adjust_bar_get_check_button(adjbar);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(control->widget), gwy_params_get_boolean(params, control->id));
        g_object_set_qdata(G_OBJECT(control->widget), param_control_quark, GUINT_TO_POINTER(i));
        g_signal_connect(control->widget, "toggled", G_CALLBACK(enabler_toggled), partable);
    }
    else if (other_control->type == GWY_PARAM_CONTROL_COMBO
             || other_control->type == GWY_PARAM_CONTROL_GRAPH_ID_COMBO
             || other_control->type == GWY_PARAM_CONTROL_IMAGE_ID_COMBO
             || other_control->type == GWY_PARAM_CONTROL_VOLUME_ID_COMBO
             || other_control->type == GWY_PARAM_CONTROL_XYZ_ID_COMBO
             || other_control->type == GWY_PARAM_CONTROL_CURVE_MAP_ID_COMBO
             || other_control->type == GWY_PARAM_CONTROL_GRAPH_CURVE_COMBO
             || other_control->type == GWY_PARAM_CONTROL_LAWN_CURVE_COMBO
             || other_control->type == GWY_PARAM_CONTROL_LAWN_SEGMENT_COMBO
             || other_control->type == GWY_PARAM_CONTROL_RADIO_ROW
             || other_control->type == GWY_PARAM_CONTROL_RADIO_BUTTONS) {
        /* Insert checkbox at the first position in the hbox, trying to mimic real checkboxes. */
        GwyParamControlEnabler *enabler = (GwyParamControlEnabler*)control->impl;
        GtkWidget *alignment, *hbox;
        const gchar *otherlabel;
        gchar *labeltext = NULL;

        hbox = gtk_widget_get_ancestor(other_control->widget, GTK_TYPE_HBOX);
        if (other_control->label) {
            if (GTK_IS_LABEL(other_control->label))
                labeltext = g_strdup(gtk_label_get_label(GTK_LABEL(other_control->label)));
            gtk_widget_destroy(other_control->label);
            other_control->label = NULL;
        }
        otherlabel = (labeltext ? labeltext : (other_control->label_text ? other_control->label_text : otherdef->desc));
        control->widget = gtk_check_button_new_with_mnemonic(otherlabel ? otherlabel : " ");
        alignment = enabler->container_child = add_right_padding(control->widget, GWY_PARAM_TABLE_COLSEP);
        gtk_box_pack_start(GTK_BOX(hbox), alignment, FALSE, FALSE, 0);
        gtk_box_reorder_child(GTK_BOX(hbox), alignment, 0);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(control->widget), gwy_params_get_boolean(params, control->id));
        g_object_set_qdata(G_OBJECT(control->widget), param_control_quark, GUINT_TO_POINTER(i));
        gtk_widget_show_all(alignment);
        g_signal_connect(control->widget, "toggled", G_CALLBACK(enabler_toggled), partable);
        g_free(labeltext);
    }
    else if (other_control->type == GWY_PARAM_CONTROL_ENTRY) {
        const gchar *otherlabel;
        gchar *labeltext = NULL;
        gint row = control->row;

        if (other_control->label) {
            if (GTK_IS_LABEL(other_control->label))
                labeltext = g_strdup(gtk_label_get_label(GTK_LABEL(other_control->label)));
            gtk_widget_destroy(other_control->label);
            other_control->label = NULL;
        }
        otherlabel = (labeltext ? labeltext : (other_control->label_text ? other_control->label_text : otherdef->desc));
        control->widget = gtk_check_button_new_with_mnemonic(otherlabel ? otherlabel : " ");
        gtk_table_attach(GTK_TABLE(priv->widget), control->widget, 0, 1, row, row+1, GTK_FILL, 0, 0, 0);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(control->widget), gwy_params_get_boolean(params, control->id));
        g_object_set_qdata(G_OBJECT(control->widget), param_control_quark, GUINT_TO_POINTER(i));
        gtk_widget_show_all(control->widget);
        g_signal_connect(control->widget, "toggled", G_CALLBACK(enabler_toggled), partable);
        g_free(labeltext);
    }
    else {
        g_assert_not_reached();
    }
}

static void
render_translated_name(G_GNUC_UNUSED GtkCellLayout *layout,
                       GtkCellRenderer *renderer,
                       GtkTreeModel *model,
                       GtkTreeIter *iter,
                       gpointer user_data)
{
    GwyParamControl *control = (GwyParamControl*)user_data;
    GwyParamControlCombo *combo = (GwyParamControlCombo*)control->impl;
    GwyResource *resource;
    const GwyEnum *penumval;

    if (combo->is_resource) {
        gtk_tree_model_get(model, iter, 0, &resource, -1);
        if (gwy_resource_get_is_modifiable(resource))
            g_object_set(renderer, "text", gwy_resource_get_name(resource), NULL);
        else
            g_object_set(renderer, "markup", gwy_sgettext(gwy_resource_get_name(resource)), NULL);
    }
    else {
        gtk_tree_model_get(model, iter, 0, &penumval, -1);
        g_object_set(renderer, "markup", gwy_sgettext(penumval->name), NULL);
    }
}

static gboolean
resource_combo_visibility_filter(GtkTreeModel *model, GtkTreeIter *iter, gpointer user_data)
{
    GwyParamControl *control = (GwyParamControl*)user_data;
    GwyParamControlCombo *combo = (GwyParamControlCombo*)control->impl;
    GwyResource *resource;
    const GwyEnum *penumval;
    GwyEnum enumval;

    if (!combo->filter_func)
        return TRUE;

    if (combo->is_resource) {
        gtk_tree_model_get(model, iter, 0, &resource, -1);
        enumval.name = gwy_resource_get_name(resource);
        enumval.value = gwy_inventory_get_item_position(combo->inventory, enumval.name);
    }
    else {
        gtk_tree_model_get(model, iter, 0, &penumval, -1);
        enumval = *penumval;
    }
    return combo->filter_func(&enumval, combo->filter_data);
}

static void
combo_make_control(GwyParamTable *partable, guint i, const GwyParamDefItem *def)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParams *params = priv->params;
    GwyParamControl *control = priv->controls + i;
    GwyParamControlCombo *combo = (GwyParamControlCombo*)control->impl;
    GwyInventoryStore *store;
    GtkTreeModel *model;
    GtkCellRenderer *renderer;
    GtkTreeIter iter, filter_iter;
    gint row = control->row, ienabler;
    const gchar *name;
    const GwyEnum *enumtable = NULL;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_COMBO);
    make_control_common(partable, i);

    store = gwy_inventory_store_new(combo->inventory);
    model = GTK_TREE_MODEL(store);

    if (combo->filter_func) {
        model = gtk_tree_model_filter_new(model, NULL);
        gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(model),
                                               resource_combo_visibility_filter, control, NULL);
        g_object_unref(store);
    }
    control->widget = gtk_combo_box_new_with_model(model);
    g_object_unref(model);

    renderer = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(control->widget), renderer, TRUE);
    gtk_cell_layout_set_cell_data_func(GTK_CELL_LAYOUT(control->widget), renderer,
                                       render_translated_name, control, NULL);

    /* This is a bit silly because we use the string lookup also for enums.  But it allows unified code and we
     * do it just upon construction. */
    if (combo->is_resource)
        name = gwy_params_get_string(params, control->id);
    else {
        enumtable = (combo->modified_enum ? combo->modified_enum : def->def.e.table);
        name = gwy_enum_to_string(gwy_params_get_enum(params, control->id), enumtable, def->def.e.nvalues);
    }
    gwy_inventory_store_get_iter(store, name, &iter);
    if (combo->filter_func) {
        if (!resource_combo_visibility_filter(GTK_TREE_MODEL(store), &iter, control)) {
            /* The caller is an idiot.  Set the parameter value to default and cross fingers. */
            gwy_params_reset(params, control->id);
            if (combo->is_resource)
                name = gwy_params_get_string(params, control->id);
            else
                name = gwy_enum_to_string(gwy_params_get_enum(params, control->id), enumtable, def->def.e.nvalues);
            gwy_inventory_store_get_iter(store, name, &iter);
        }
        gtk_tree_model_filter_convert_child_iter_to_iter(GTK_TREE_MODEL_FILTER(model), &filter_iter, &iter);
        iter = filter_iter;
    }
    gtk_combo_box_set_active_iter(GTK_COMBO_BOX(control->widget), &iter);
    g_signal_connect(control->widget, "changed", G_CALLBACK(combo_changed), partable);

    gwy_debug("attach %s at %d..%d", g_type_name(G_TYPE_FROM_INSTANCE(control->widget)), row, row+1);
    g_object_set_qdata(G_OBJECT(control->widget), param_control_quark, GUINT_TO_POINTER(i));
    attach_hbox_row(partable, row, control, (control->label_text ? control->label_text : def->desc));
    update_control_unit_label(control, partable);
    if ((ienabler = find_aux_for_control(partable, &priv->enabler, control->id)) >= 0)
        enabler_make_control(partable, ienabler, i);
    update_control_sensitivity(partable, i);
}

static void
radio_make_control(GwyParamTable *partable, guint i, const GwyParamDefItem *def)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    gint row = control->row;
    GtkTable *table;
    GSList *list;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_RADIO);
    make_control_common(partable, i);

    construct_radio_widgets(partable, i, def);
    table = GTK_TABLE(priv->widget);
    if (control->label) {
        gwy_debug("attach %s at %d..%d", g_type_name(G_TYPE_FROM_INSTANCE(control->label)), row, row+1);
        gtk_table_attach(table, control->label, 0, 2, row, row+1, GTK_FILL, 0, 0, 0);
        gtk_widget_show(control->label);
        row++;
    }
    for (list = gtk_radio_button_get_group(GTK_RADIO_BUTTON(control->widget)); list; list = g_slist_next(list)) {
        gwy_debug("attach %s at %d..%d", g_type_name(G_TYPE_FROM_INSTANCE(list->data)), row, row+1);
        gtk_table_attach(table, GTK_WIDGET(list->data), 0, 2, row, row+1, GTK_FILL, 0, 0, 0);
        gtk_widget_show(GTK_WIDGET(list->data));
        row++;
    }
    update_control_sensitivity(partable, i);
}

static void
radio_header_make_control(GwyParamTable *partable, guint i, const GwyParamDefItem *def)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    gint row = control->row;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_RADIO_HEADER);
    g_return_if_fail(def->desc);
    make_control_common(partable, i);

    construct_radio_widgets(partable, i, def);
    gtk_table_attach(GTK_TABLE(priv->widget), control->label, 0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    gtk_widget_show(control->label);
    gwy_debug("attach %s at %d..%d", g_type_name(G_TYPE_FROM_INSTANCE(control->label)), row, row+1);
    update_control_sensitivity(partable, i);
}

static void
radio_item_make_control(GwyParamTable *partable, guint i, const GwyParamDefItem *def)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    GwyParamControlRadioItem *radioitem = (GwyParamControlRadioItem*)control->impl;
    gint row = control->row;
    GSList *list;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_RADIO_ITEM);
    make_control_common(partable, i);

    construct_radio_widgets(partable, i, def);
    for (list = gtk_radio_button_get_group(GTK_RADIO_BUTTON(control->widget)); list; list = g_slist_next(list)) {
        gint buttonvalue = gwy_radio_button_get_value(GTK_WIDGET(list->data));
        if (buttonvalue == radioitem->value) {
            gtk_table_attach(GTK_TABLE(priv->widget), GTK_WIDGET(list->data), 0, 2, row, row+1, GTK_FILL, 0, 0, 0);
            gtk_widget_show(GTK_WIDGET(list->data));
            gwy_debug("attach %s at %d..%d", g_type_name(G_TYPE_FROM_INSTANCE(list->data)), row, row+1);
            update_control_sensitivity(partable, i);
            return;
        }
    }
    g_assert_not_reached();
}

static void
radio_row_make_control(GwyParamTable *partable, guint i, const GwyParamDefItem *def)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    gint row = control->row, ienabler;
    GtkWidget *hbox;
    GSList *list, *l;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_RADIO_ROW);
    make_control_common(partable, i);

    hbox = gwy_hbox_new(GWY_PARAM_TABLE_COLSEP);
    gtk_table_attach(GTK_TABLE(priv->widget), hbox, 0, 2, row, row+1, GTK_FILL, 0, 0, 0);

    construct_radio_widgets(partable, i, def);
    /* We are attaching from the end so the order needs to be reversed. */
    list = gtk_radio_button_get_group(GTK_RADIO_BUTTON(control->widget));
    list = g_slist_reverse(g_slist_copy(list));
    gwy_debug("attach %s at %d..%d", g_type_name(G_TYPE_FROM_INSTANCE(list->data)), row, row+1);
    for (l = list; l; l = g_slist_next(l))
        gtk_box_pack_end(GTK_BOX(hbox), GTK_WIDGET(l->data), FALSE, FALSE, 0);
    g_slist_free(list);
    if (control->label)
        gtk_box_pack_start(GTK_BOX(hbox), control->label, FALSE, FALSE, 0);
    update_control_unit_label(control, partable);
    if ((ienabler = find_aux_for_control(partable, &priv->enabler, control->id)) >= 0)
        enabler_make_control(partable, ienabler, i);
    update_control_sensitivity(partable, i);
}

static void
radio_buttons_make_control(GwyParamTable *partable, guint i, const GwyParamDefItem *def)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    GwyParamControlRadioButtons *radiobuttons = (GwyParamControlRadioButtons*)control->impl;
    gint id = control->id, row = control->row, ienabler;
    GtkWidget *hbox, *alignment, *button;
    GSList *list;
    const gchar *label;
    guint k, n;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_RADIO_BUTTONS);
    make_control_common(partable, i);

    hbox = gwy_hbox_new(0);
    gtk_table_attach(GTK_TABLE(priv->widget), hbox, 0, 2, row, row+1, GTK_FILL, 0, 0, 0);

    n = def->def.e.nvalues;
    button = NULL;
    for (k = 0; k < n; k++) {
        const gchar *tooltip = def->def.e.table[n-1 - k].name;
        gint value = def->def.e.table[n-1 - k].value;
        const gchar *stock_id = gwy_enum_to_string(value, radiobuttons->stock_ids, n);

        button = gtk_radio_button_new_from_widget(GTK_RADIO_BUTTON(button));
        g_object_set_qdata(G_OBJECT(button), param_control_quark, GUINT_TO_POINTER(i));
        g_object_set_qdata(G_OBJECT(button), radio_button_quark, GINT_TO_POINTER(value));
        gtk_container_add(GTK_CONTAINER(button), gtk_image_new_from_stock(stock_id, GTK_ICON_SIZE_LARGE_TOOLBAR));
        gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(button), FALSE);
        gtk_widget_set_tooltip_text(button, gwy_sgettext(tooltip));
        gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
    }
    list = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
    gwy_radio_buttons_set_current(list, gwy_params_get_enum(priv->params, id));
    gwy_debug("attach %s at %d..%d", g_type_name(G_TYPE_FROM_INSTANCE(list->data)), row, row+1);
    control->widget = GTK_WIDGET(list->data);
    while (list) {
        g_signal_connect(list->data, "clicked", G_CALLBACK(radio_changed), partable);
        list = g_slist_next(list);
    }
    label = (control->label_text ? control->label_text : def->desc);
    if (label) {
        control->label = gtk_label_new(modify_label(partable, label, TRUE, TRUE));
        alignment = add_right_padding(control->label, GWY_PARAM_TABLE_COLSEP);
        gtk_box_pack_start(GTK_BOX(hbox), alignment, FALSE, FALSE, 0);
    }
    update_control_unit_label(control, partable);
    if ((ienabler = find_aux_for_control(partable, &priv->enabler, control->id)) >= 0)
        enabler_make_control(partable, ienabler, i);
    update_control_sensitivity(partable, i);
}

static void
construct_radio_widgets(GwyParamTable *partable, guint i, const GwyParamDefItem *def)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *controls = priv->controls, *control = controls + i;
    guint k, n = priv->ncontrols;
    gint id = control->id;
    const gchar *label;
    GSList *list;

    g_return_if_fail(control_is_some_kind_of_radio(control->type));
    /* If there is already a radio button group for the same id then just copy the pointers.  The caller is
     * responsible for only adding controls once to the GUI. */
    for (k = 0; k < n; k++) {
        if (k != i && controls[k].id == id && controls[k].widget) {
            control->label = controls[k].label;
            control->widget = controls[k].widget;
            return;
        }
    }

    label = (control->label_text ? control->label_text : def->desc);
    if (label) {
        control->label = gtk_label_new(modify_label(partable, label, TRUE, TRUE));
        gtk_misc_set_alignment(GTK_MISC(control->label), 0.0, 0.5);
    }
    list = gwy_radio_buttons_create(def->def.e.table, def->def.e.nvalues, G_CALLBACK(radio_changed), partable,
                                    gwy_params_get_enum(priv->params, id));
    control->widget = GTK_WIDGET(list->data);
    while (list) {
        g_object_set_qdata(G_OBJECT(list->data), param_control_quark, GUINT_TO_POINTER(i));
        list = g_slist_next(list);
    }
    /* The caller must do update_control_sensitivity() because at this point the widget may be still only partially
     * constructed. */
}

static void
checkboxes_make_control(GwyParamTable *partable, guint i, const GwyParamDefItem *def)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    gint row = control->row;
    GtkTable *table;
    GSList *list;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_CHECKBOXES);
    make_control_common(partable, i);

    construct_checkbox_widgets(partable, i, def);
    table = GTK_TABLE(priv->widget);
    if (control->label) {
        gwy_debug("attach %s at %d..%d", g_type_name(G_TYPE_FROM_INSTANCE(control->label)), row, row+1);
        gtk_table_attach(table, control->label, 0, 2, row, row+1, GTK_FILL, 0, 0, 0);
        gtk_widget_show(control->label);
        row++;
    }
    for (list = gwy_check_box_get_group(control->widget); list; list = g_slist_next(list)) {
        gwy_debug("attach %s at %d..%d", g_type_name(G_TYPE_FROM_INSTANCE(list->data)), row, row+1);
        gtk_table_attach(table, GTK_WIDGET(list->data), 0, 2, row, row+1, GTK_FILL, 0, 0, 0);
        gtk_widget_show(GTK_WIDGET(list->data));
        row++;
    }
    update_control_sensitivity(partable, i);
}

static void
construct_checkbox_widgets(GwyParamTable *partable, guint i, const GwyParamDefItem *def)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *controls = priv->controls, *control = controls + i;
    guint k, n = priv->ncontrols;
    gint id = control->id;
    const gchar *label;
    GSList *list;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_CHECKBOXES);
    /* If there is already a checkbox group for the same id then just copy the pointers.  The caller is responsible
     * for only adding controls once to the GUI. */
    for (k = 0; k < n; k++) {
        if (k != i && controls[k].id == id && controls[k].widget) {
            control->label = controls[k].label;
            control->widget = controls[k].widget;
            return;
        }
    }

    label = (control->label_text ? control->label_text : def->desc);
    if (label) {
        control->label = gtk_label_new(modify_label(partable, label, TRUE, TRUE));
        gtk_misc_set_alignment(GTK_MISC(control->label), 0.0, 0.5);
    }
    list = gwy_check_boxes_create(def->def.f.table, def->def.f.nvalues, G_CALLBACK(checkbox_changed), partable,
                                  gwy_params_get_flags(priv->params, id));
    control->widget = GTK_WIDGET(list->data);
    while (list) {
        g_object_set_qdata(G_OBJECT(list->data), param_control_quark, GUINT_TO_POINTER(i));
        list = g_slist_next(list);
    }
    update_control_sensitivity(partable, i);
}

static void
data_id_make_control(GwyParamTable *partable, guint i, const GwyParamDefItem *def)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParams *params = priv->params;
    GwyParamControl *control = priv->controls + i;
    GwyParamControlType type = control->type;
    GwyParamControlDataChooser *datachooser = (GwyParamControlDataChooser*)control->impl;
    GwyDataChooser *chooser;
    GwyAppDataId dataid;
    gint row = control->row, ienabler;
    GCallback callback;

    make_control_common(partable, i);
    if (type == GWY_PARAM_CONTROL_GRAPH_ID_COMBO)
        control->widget = gwy_data_chooser_new_graphs();
    else if (type == GWY_PARAM_CONTROL_IMAGE_ID_COMBO)
        control->widget = gwy_data_chooser_new_channels();
    else if (type == GWY_PARAM_CONTROL_VOLUME_ID_COMBO)
        control->widget = gwy_data_chooser_new_volumes();
    else if (type == GWY_PARAM_CONTROL_XYZ_ID_COMBO)
        control->widget = gwy_data_chooser_new_xyzs();
    else if (type == GWY_PARAM_CONTROL_CURVE_MAP_ID_COMBO)
        control->widget = gwy_data_chooser_new_curve_maps();
    else {
        g_return_if_reached();
    }
    gwy_debug("attach %s at %d..%d", g_type_name(G_TYPE_FROM_INSTANCE(control->widget)), row, row+1);
    chooser = GWY_DATA_CHOOSER(control->widget);
    if (datachooser->none)
        gwy_data_chooser_set_none(chooser, datachooser->none);
    if (datachooser->filter_func)
        gwy_data_chooser_set_filter(chooser, datachooser->filter_func, datachooser->filter_data, NULL);
    if (gwy_params_data_id_is_none(params, control->id))
        gwy_data_chooser_set_active_id(chooser, NULL);
    else {
        dataid = gwy_params_get_data_id(params, control->id);
        gwy_data_chooser_set_active_id(chooser, &dataid);
    }
    gwy_data_chooser_get_active_id(chooser, &dataid);
    gwy_debug("initial dataid %d, %d", dataid.datano, dataid.id);
    if (type == GWY_PARAM_CONTROL_GRAPH_ID_COMBO)
        gwy_params_set_graph_id(params, control->id, dataid);
    else if (type == GWY_PARAM_CONTROL_IMAGE_ID_COMBO)
        gwy_params_set_image_id(params, control->id, dataid);
    else if (type == GWY_PARAM_CONTROL_VOLUME_ID_COMBO)
        gwy_params_set_volume_id(params, control->id, dataid);
    else if (type == GWY_PARAM_CONTROL_XYZ_ID_COMBO)
        gwy_params_set_xyz_id(params, control->id, dataid);
    else if (type == GWY_PARAM_CONTROL_CURVE_MAP_ID_COMBO)
        gwy_params_set_curve_map_id(params, control->id, dataid);
    else {
        g_assert_not_reached();
    }
    g_object_set_qdata(G_OBJECT(control->widget), param_control_quark, GUINT_TO_POINTER(i));
    attach_hbox_row(partable, row, control, (control->label_text ? control->label_text : def->desc));
    update_control_unit_label(control, partable);
    if ((ienabler = find_aux_for_control(partable, &priv->enabler, control->id)) >= 0)
        enabler_make_control(partable, ienabler, i);
    update_control_sensitivity(partable, i);

    if (type == GWY_PARAM_CONTROL_GRAPH_ID_COMBO)
        callback = G_CALLBACK(graph_id_changed);
    else if (type == GWY_PARAM_CONTROL_IMAGE_ID_COMBO)
        callback = G_CALLBACK(image_id_changed);
    else if (type == GWY_PARAM_CONTROL_VOLUME_ID_COMBO)
        callback = G_CALLBACK(volume_id_changed);
    else if (type == GWY_PARAM_CONTROL_XYZ_ID_COMBO)
        callback = G_CALLBACK(xyz_id_changed);
    else if (type == GWY_PARAM_CONTROL_CURVE_MAP_ID_COMBO)
        callback = G_CALLBACK(curve_map_id_changed);
    else {
        g_assert_not_reached();
    }

    g_signal_connect(control->widget, "changed", callback, partable);
}

static void
curve_no_make_control(GwyParamTable *partable, guint i, const GwyParamDefItem *def)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParams *params = priv->params;
    GwyParamControl *control = priv->controls + i;
    GwyParamControlType type = control->type;
    GwyParamControlCurveChooser *curvechooser = (GwyParamControlCurveChooser*)control->impl;
    gint row = control->row, ienabler, curveno, n;

    make_control_common(partable, i);
    curveno = gwy_params_get_int(params, control->id);
    n = curve_no_get_ncurves(control);
    curveno = (n ? CLAMP(curveno, 0, n-1) : -1);
    gwy_params_set_curve(params, control->id, curveno);
    /* FIXME: Does this connect to the signal too early?  Should we block it for the rest of the setup? */
    if (type == GWY_PARAM_CONTROL_GRAPH_CURVE_COMBO) {
        control->widget = gwy_combo_box_graph_curve_new(G_CALLBACK(graph_curve_changed), partable,
                                                        GWY_GRAPH_MODEL(curvechooser->parent), curveno);
    }
    else if (type == GWY_PARAM_CONTROL_LAWN_CURVE_COMBO) {
        control->widget = gwy_combo_box_lawn_curve_new(G_CALLBACK(lawn_curve_changed), partable,
                                                       GWY_LAWN(curvechooser->parent), curveno);
    }
    else if (type == GWY_PARAM_CONTROL_LAWN_SEGMENT_COMBO) {
        control->widget = gwy_combo_box_lawn_segment_new(G_CALLBACK(lawn_segment_changed), partable,
                                                         GWY_LAWN(curvechooser->parent), curveno);
    }
    else {
        g_return_if_reached();
    }
    gwy_debug("attach %s at %d..%d", g_type_name(G_TYPE_FROM_INSTANCE(control->widget)), row, row+1);
    g_object_set_qdata(G_OBJECT(control->widget), param_control_quark, GUINT_TO_POINTER(i));
    attach_hbox_row(partable, row, control, (control->label_text ? control->label_text : def->desc));
    update_control_unit_label(control, partable);
    if ((ienabler = find_aux_for_control(partable, &priv->enabler, control->id)) >= 0)
        enabler_make_control(partable, ienabler, i);
    update_control_sensitivity(partable, i);
}

static void
adjbar_size_request(G_GNUC_UNUSED GtkWidget *widget, GtkRequisition *requisition)
{
    if (requisition->width < 80)
        requisition->width = 80;
}

static void
slider_make_control(GwyParamTable *partable, guint i, const GwyParamDefItem *def)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParams *params = priv->params;
    GwyParamControl *control = priv->controls + i;
    GwyParamControlSlider *slider = (GwyParamControlSlider*)control->impl;
    gint row = control->row, id = control->id, ienabler;
    GwyAdjustBar *adjbar;
    const gchar *label;
    gdouble value;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_SLIDER);
    make_control_common(partable, i);

    value = (def->type == GWY_PARAM_INT ? gwy_params_get_int(params, id) : gwy_params_get_double(params, id));
    slider->adj = GTK_ADJUSTMENT(gtk_adjustment_new(value, slider->minimum, slider->maximum,
                                                    slider->step, slider->page, 0.0));
    gwy_debug("step=%g, page=%g, digits=%d (snap=%d)", slider->step, slider->page, slider->digits, slider->snap);
    g_object_set_qdata(G_OBJECT(slider->adj), param_control_quark, GUINT_TO_POINTER(i));

    slider->spin = gtk_spin_button_new(slider->adj, 0.5*slider->step, slider->digits);
    g_object_set_qdata(G_OBJECT(slider->spin), param_control_quark, GUINT_TO_POINTER(i));
    // If we do this, it is impossible to paste more precise numbers than the entry has digits. Which is silly.
    // If we do not do this, our input handler rounds the number just fine.
    //gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(slider->spin), TRUE);
    gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(slider->spin), slider->snap);
    gtk_entry_set_alignment(GTK_ENTRY(slider->spin), 1.0);

    label = (control->label_text ? control->label_text : def->desc);
    control->widget = gwy_adjust_bar_new(slider->adj, modify_label(partable, label, TRUE, FALSE));
    gwy_debug("attach %s at %d..%d", g_type_name(G_TYPE_FROM_INSTANCE(control->widget)), row, row+1);
    g_signal_connect_after(control->widget, "size-request", G_CALLBACK(adjbar_size_request), NULL);
    adjbar = GWY_ADJUST_BAR(control->widget);
    if (slider->mapping_set)
        gwy_adjust_bar_set_mapping(adjbar, slider->mapping);
    else if (slider->is_percentage || slider->is_angle)
        gwy_adjust_bar_set_mapping(adjbar, GWY_SCALE_MAPPING_LINEAR);
    gwy_adjust_bar_set_snap_to_ticks(adjbar, slider->snap);
    control->label = gwy_adjust_bar_get_label(adjbar);
    gtk_label_set_mnemonic_widget(GTK_LABEL(control->label), slider->spin);
    slider_set_width_chars(partable, i);

    gtk_table_attach(GTK_TABLE(priv->widget), control->widget, 0, 1, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    gtk_table_attach(GTK_TABLE(priv->widget), slider->spin, 1, 2, row, row+1, GTK_FILL, 0, 0, 0);
    update_control_unit_label(control, partable);
    if ((ienabler = find_aux_for_control(partable, &priv->enabler, id)) >= 0)
        enabler_make_control(partable, ienabler, i);
    gtk_widget_show(control->widget);
    gtk_widget_show(slider->spin);
    if (slider->has_alt)
        alt_make_control(partable, i, def);
    update_control_sensitivity(partable, i);
    g_signal_connect(slider->adj, "value-changed", G_CALLBACK(slider_value_changed), partable);
    g_signal_connect(slider->spin, "input", G_CALLBACK(slider_spin_input), partable);
    g_signal_connect(slider->spin, "output", G_CALLBACK(slider_spin_output), partable);
}

static void
alt_make_control(GwyParamTable *partable, guint i, G_GNUC_UNUSED const GwyParamDefItem *def)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    GwyParamControlSlider *slider = (GwyParamControlSlider*)control->impl;
    gint row = control->row;
    GtkWidget *align;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_SLIDER);
    g_return_if_fail(slider->spin);
    g_return_if_fail(!slider->alt_spin);

    /* We will reconfigure it later. */
    slider->alt_spin = gtk_spin_button_new(slider->adj, 0.5*slider->step, slider->digits);
    gwy_debug("attach %s at %d..%d", g_type_name(G_TYPE_FROM_INSTANCE(slider->alt_spin)), row, row+1);
    g_object_set_qdata(G_OBJECT(slider->alt_spin), param_control_quark, GUINT_TO_POINTER(i));
    // If we do this, it is impossible to paste more precise numbers than the entry has digits. Which is silly.
    // If we do not do this, our input handler rounds the number just fine.
    //gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(slider->alt_spin), TRUE);
    gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(slider->alt_spin), slider->snap);
    gtk_entry_set_alignment(GTK_ENTRY(slider->alt_spin), 1.0);
    align = add_left_padding(slider->alt_spin, GWY_PARAM_TABLE_COLSEP);
    g_object_set(align, "xscale", 1.0, NULL);
    gtk_table_attach(GTK_TABLE(priv->widget), align, 3, 4, row, row+1, GTK_FILL, 0, 0, 0);
    gtk_widget_show_all(align);

    slider->alt_unitlabel = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(slider->alt_unitlabel), 0.0, 0.5);
    gtk_label_set_markup(GTK_LABEL(slider->alt_unitlabel), slider->alt_unitstr);
    gtk_table_attach(GTK_TABLE(priv->widget), slider->alt_unitlabel, 4, 5, row, row+1, GTK_FILL, 0, 0, 0);
    gtk_widget_show_all(slider->alt_unitlabel);

    slider_reconfigure_alt(control, partable);
    alt_set_width_chars(partable, i);
    g_signal_connect(slider->alt_spin, "input", G_CALLBACK(slider_spin_input), partable);
    g_signal_connect(slider->alt_spin, "output", G_CALLBACK(slider_spin_output), partable);
}

static void
entry_make_control(GwyParamTable *partable, guint i, const GwyParamDefItem *def)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    GwyParamControlEntry *entry = (GwyParamControlEntry*)control->impl;
    gint row = control->row, ienabler;
    const gchar *label;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_ENTRY);
    make_control_common(partable, i);

    label = (control->label_text ? control->label_text : def->desc);
    control->label = gtk_label_new(modify_label(partable, label, TRUE, TRUE));
    gtk_misc_set_alignment(GTK_MISC(control->label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(priv->widget), control->label, 0, 1, row, row+1, GTK_FILL, 0, 0, 0);

    control->widget = gtk_entry_new();
    g_object_set_qdata(G_OBJECT(control->widget), param_control_quark, GUINT_TO_POINTER(i));
    gwy_debug("attach %s at %d..%d", g_type_name(G_TYPE_FROM_INSTANCE(control->widget)), row, row+1);
    if (entry->width > 0)
        gtk_entry_set_width_chars(GTK_ENTRY(control->widget), entry->width);
    else if (entry->is_numeric)
        gtk_entry_set_width_chars(GTK_ENTRY(control->widget), entry->is_int ? 7 : 9);

    if (def->type == GWY_PARAM_INT || def->type == GWY_PARAM_DOUBLE)
        gtk_entry_set_alignment(GTK_ENTRY(control->widget), 1.0);
    gtk_table_attach(GTK_TABLE(priv->widget), control->widget, 1, 2, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(control->label), control->widget);

    update_control_unit_label(control, partable);
    if ((ienabler = find_aux_for_control(partable, &priv->enabler, control->id)) >= 0)
        enabler_make_control(partable, ienabler, i);
    gtk_widget_show(control->widget);
    if (control->label)
        gtk_widget_show(control->label);
    update_control_sensitivity(partable, i);
    entry_output(partable, control);
    gwy_widget_set_activate_on_unfocus(control->widget, TRUE);
    g_signal_connect(control->widget, "activate", G_CALLBACK(entry_activated), partable);
    if (entry->instant_changes)
        g_signal_connect(control->widget, "changed", G_CALLBACK(entry_changed), partable);
}

static void
range_from_make_control(GwyParamTable *partable, guint i, const GwyParamDefItem *def)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    gint row = control->row, ito;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_RANGE_FROM);
    make_control_common(partable, i);

    control->widget = gtk_entry_new();
    g_object_set_qdata(G_OBJECT(control->widget), param_control_quark, GUINT_TO_POINTER(i));
    gwy_debug("attach %s at %d..%d", g_type_name(G_TYPE_FROM_INSTANCE(control->widget)), row, row+1);
    gtk_entry_set_width_chars(GTK_ENTRY(control->widget), 9);
    attach_hbox_row(partable, row, control, (control->label_text ? control->label_text : def->desc));
    update_control_unit_label(control, partable);
    range_output(partable, control);

    ito = find_aux_for_control(partable, &priv->range, control->id);
    g_assert(ito >= 0.0);
    range_to_make_control(partable, ito, i);

    gwy_widget_set_activate_on_unfocus(control->widget, TRUE);
    g_signal_connect(control->widget, "activate", G_CALLBACK(range_activated), partable);
}

static void
range_to_make_control(GwyParamTable *partable, guint i, guint ifrom)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i, *control_from = priv->controls + ifrom;
    const gchar *label;
    GtkWidget *hbox;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_RANGE_TO);
    hbox = gtk_widget_get_ancestor(control_from->widget, GTK_TYPE_HBOX);
    g_return_if_fail(hbox);

    control->widget = gtk_entry_new();
    g_object_set_qdata(G_OBJECT(control->widget), param_control_quark, GUINT_TO_POINTER(i));
    gwy_debug("attach %s to hbox", g_type_name(G_TYPE_FROM_INSTANCE(control->widget)));
    gtk_entry_set_width_chars(GTK_ENTRY(control->widget), 9);
    gtk_box_pack_end(GTK_BOX(hbox), control->widget, FALSE, FALSE, 0);

    /* TODO: Perhaps make modifiable. */
    label = gwy_sgettext("range|to");
    gtk_box_pack_end(GTK_BOX(hbox), gtk_label_new(label), FALSE, FALSE, GWY_PARAM_TABLE_COLSEP);

    gtk_box_reorder_child(GTK_BOX(hbox), control_from->widget, -1);

    range_output(partable, control);

    gwy_widget_set_activate_on_unfocus(control->widget, TRUE);
    g_signal_connect(control->widget, "activate", G_CALLBACK(range_activated), partable);
}

static void
color_make_control(GwyParamTable *partable, guint i, const GwyParamDefItem *def)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    GwyParamControlColor *colour = (GwyParamControlColor*)control->impl;
    gint row = control->row, ienabler;
    GwyRGBA color;
    guint idx;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_COLOR);
    make_control_common(partable, i);

    control->widget = gwy_color_button_new();
    gwy_debug("attach %s at %d..%d", g_type_name(G_TYPE_FROM_INSTANCE(control->widget)), row, row+1);
    gwy_color_button_set_use_alpha(GWY_COLOR_BUTTON(control->widget), def->def.c.has_alpha);
    color = gwy_params_get_color(priv->params, control->id);
    gwy_color_button_set_color(GWY_COLOR_BUTTON(control->widget), &color);
    g_object_set_qdata(G_OBJECT(control->widget), param_control_quark, GUINT_TO_POINTER(i));
    attach_hbox_row(partable, row, control, (control->label_text ? control->label_text : def->desc));
    if (colour->presets) {
        for (idx = 0; idx < colour->presets->len; idx++)
            make_color_button_preset(partable, i, idx);
    }
    update_control_unit_label(control, partable);
    if ((ienabler = find_aux_for_control(partable, &priv->enabler, control->id)) >= 0)
        enabler_make_control(partable, ienabler, i);
    update_control_sensitivity(partable, i);
    g_signal_connect(control->widget, "clicked", G_CALLBACK(color_run_selector), partable);
}

static void
set_to_preset_colour(GwyParamTable *partable, GtkWidget *button)
{
    guint i = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(button), param_control_quark));
    guint idx = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "index"));
    GwyParamControl *control = partable->priv->controls + i;
    GwyParamControlColor *colour = (GwyParamControlColor*)control->impl;
    GwyRGBA rgba = g_array_index(colour->presets, GwyPresetColor, idx).rgba;

    gwy_color_button_set_color(GWY_COLOR_BUTTON(control->widget), &rgba);
    color_set_value(control, partable, rgba, FALSE);
}

static void
make_color_button_preset(GwyParamTable *partable, guint i, guint idx)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    GwyParamControlColor *colour = (GwyParamControlColor*)control->impl;
    GwyPresetColor *preset;
    GtkWidget *hbox, *button, *image;
    GdkPixbuf *pixbuf;
    gint width, height;
    guint32 pixel;

    g_return_if_fail(colour->presets && idx < colour->presets->len);
    hbox = gtk_widget_get_parent(control->widget);
    g_return_if_fail(GTK_IS_HBOX(hbox));

    if (!colour->sizegroup)
        colour->sizegroup = gtk_size_group_new(GTK_SIZE_GROUP_BOTH);

    preset = &g_array_index(colour->presets, GwyPresetColor, idx);

    gtk_icon_size_lookup(GTK_ICON_SIZE_MENU, &width, &height);
    width = MAX(2, 5*width/6);
    height = MAX(2, 5*height/6);
    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, width, height);
    pixel = gwy_rgba_to_pixbuf_pixel(&preset->rgba);
    gdk_pixbuf_fill(pixbuf, pixel);

    image = gtk_image_new_from_pixbuf(pixbuf);
    g_object_unref(pixbuf);

    button = gtk_button_new_with_label(preset->name ? preset->name : "");
    gtk_button_set_image(GTK_BUTTON(button), image);
    gtk_size_group_add_widget(colour->sizegroup, button);
    g_object_set_qdata(G_OBJECT(button), param_control_quark, GUINT_TO_POINTER(i));
    g_object_set_data(G_OBJECT(button), "index", GUINT_TO_POINTER(idx));
    gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
    gtk_box_reorder_child(GTK_BOX(hbox), button, 0);
    gtk_widget_show(button);
    g_signal_connect_swapped(button, "clicked", G_CALLBACK(set_to_preset_colour), partable);
}

static void
mask_color_make_control(GwyParamTable *partable, guint i, const GwyParamDefItem *def)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    gint row = control->row, ienabler;
    GwyRGBA color;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_MASK_COLOR);
    make_control_common(partable, i);

    control->widget = gwy_color_button_new();
    gwy_debug("attach %s at %d..%d", g_type_name(G_TYPE_FROM_INSTANCE(control->widget)), row, row+1);
    gwy_color_button_set_use_alpha(GWY_COLOR_BUTTON(control->widget), TRUE);
    color = gwy_params_get_color(priv->params, control->id);
    gwy_color_button_set_color(GWY_COLOR_BUTTON(control->widget), &color);
    g_object_set_qdata(G_OBJECT(control->widget), param_control_quark, GUINT_TO_POINTER(i));
    attach_hbox_row(partable, row, control, (control->label_text ? control->label_text : def->desc));
    update_control_unit_label(control, partable);
    if ((ienabler = find_aux_for_control(partable, &priv->enabler, control->id)) >= 0)
        enabler_make_control(partable, ienabler, i);
    update_control_sensitivity(partable, i);
    g_signal_connect(control->widget, "clicked", G_CALLBACK(mask_color_run_selector), partable);
}

static void
report_make_control(GwyParamTable *partable, guint i, const GwyParamDefItem *def)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    GwyParamControlReport *report = (GwyParamControlReport*)control->impl;
    gint row = control->row;
    GwyResultsReportType report_type;
    const gchar *label;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_REPORT);
    make_control_common(partable, i);

    report_type = gwy_params_get_report_type(priv->params, control->id);
    control->widget = gwy_results_export_new(report_type);
    g_object_set_qdata(G_OBJECT(control->widget), param_control_quark, GUINT_TO_POINTER(i));
    gwy_debug("attach %s at %d..%d", g_type_name(G_TYPE_FROM_INSTANCE(control->widget)), row, row+1);
    gwy_results_export_set_style(GWY_RESULTS_EXPORT(control->widget), def->def.rt.style);
    label = (control->label_text ? control->label_text : def->desc);
    if (label)
        gwy_results_export_set_title(GWY_RESULTS_EXPORT(control->widget), modify_label(partable, label, FALSE, TRUE));
    attach_hbox_row(partable, row, control, NULL);
    update_control_sensitivity(partable, i);
    g_signal_connect(control->widget, "format-changed", G_CALLBACK(report_format_changed), partable);
    gwy_results_export_set_results(GWY_RESULTS_EXPORT(control->widget), report->results);
    if (report->format_report)
        report_ensure_actions(control, partable);
}

static GtkWidget*
hold_selection_make_toggle(GwyHoldSelectionFlags value,
                           const gchar *stock_id,
                           const GwyParamDefItem *def,
                           GtkWidget *hbox,
                           GtkSizeGroup *sizegroup)
{
    GtkWidget *button;

    button = gtk_toggle_button_new();
    gtk_container_add(GTK_CONTAINER(button), gtk_image_new_from_stock(stock_id, GTK_ICON_SIZE_SMALL_TOOLBAR));
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(button,
                                gwy_sgettext(gwy_enum_to_string(value, def->def.f.table, def->def.f.nvalues)));
    gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
    gtk_size_group_add_widget(sizegroup, button);

    return button;
}

static void
hold_selection_set_up_button(GtkWidget *button,
                             GwyHoldSelectionFlags value, GwyHoldSelectionFlags flags,
                             guint i, GwyParamTable *partable)
{
    g_object_set_qdata(G_OBJECT(button), param_control_quark, GUINT_TO_POINTER(i));
    g_object_set_data(G_OBJECT(button), "gwy-checkboxes-key", GUINT_TO_POINTER(value));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), flags & value);
    g_signal_connect(button, "toggled", G_CALLBACK(hold_selection_changed), partable);
}

static void
hold_selection_make_control(GwyParamTable *partable, guint i, const GwyParamDefItem *def)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    GwyParamControlHoldSelection *holdsel = (GwyParamControlHoldSelection*)control->impl;
    gint row = control->row;
    GwyHoldSelectionFlags flags = gwy_params_get_flags(priv->params, control->id);
    GtkSizeGroup *sizegroup;
    GtkWidget *hbox;
    const gchar *label;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_HOLD_SELECTION);
    make_control_common(partable, i);

    label = (control->label_text ? control->label_text : def->desc);
    control->widget = gtk_check_button_new_with_mnemonic(label);
    gwy_debug("attach %s at %d..%d", g_type_name(G_TYPE_FROM_INSTANCE(control->widget)), row, row+1);

    hbox = gwy_hbox_new(0);
    gtk_table_attach(GTK_TABLE(partable->priv->widget), hbox, 0, 2, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    gtk_box_pack_start(GTK_BOX(hbox), control->widget, TRUE, TRUE, 0);

    sizegroup = gtk_size_group_new(GTK_SIZE_GROUP_BOTH);
    /* Pack in reverse order since we pack-end. */
    holdsel->clear = hold_selection_make_toggle(GWY_HOLD_SELECTION_CLEAR, GWY_STOCK_HOLD_SELECTION_CLEAR,
                                                def, hbox, sizegroup);
    holdsel->overwrite = hold_selection_make_toggle(GWY_HOLD_SELECTION_OVERWRITE, GWY_STOCK_HOLD_SELECTION_REPLACE,
                                                    def, hbox, sizegroup);
    g_object_unref(sizegroup);
    gtk_widget_show_all(hbox);

    hold_selection_set_up_button(control->widget, GWY_HOLD_SELECTION_HOLD, flags, i, partable);
    hold_selection_set_up_button(holdsel->overwrite, GWY_HOLD_SELECTION_OVERWRITE, flags, i, partable);
    hold_selection_set_up_button(holdsel->clear, GWY_HOLD_SELECTION_CLEAR, flags, i, partable);

    update_control_sensitivity(partable, i);
}

static void
unit_chooser_make_control(GwyParamTable *partable, guint i, const GwyParamDefItem *def)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    GwyParamControlUnitChooser *unit = (GwyParamControlUnitChooser*)control->impl;
    GtkWidget *alignment;
    gint row = control->row;
    GwySIUnit *siunit;
    gint power10;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_UNIT_CHOOSER);
    make_control_common(partable, i);

    siunit = gwy_params_get_unit(priv->params, control->id, &power10);
    control->widget = gwy_combo_box_metric_unit_new(G_CALLBACK(unit_chosen), partable,
                                                    power10 - 6, power10 + 6, siunit, power10);
    g_object_set_qdata(G_OBJECT(control->widget), param_control_quark, GUINT_TO_POINTER(i));
    gwy_debug("attach %s at %d..%d", g_type_name(G_TYPE_FROM_INSTANCE(control->widget)), row, row+1);
    attach_hbox_row(partable, row, control, (control->label_text ? control->label_text : def->desc));

    unit->change_button = gtk_button_new_with_mnemonic(gwy_sgettext("verb|Change"));
    g_object_set_qdata(G_OBJECT(unit->change_button), param_control_quark, GUINT_TO_POINTER(i));
    alignment = gtk_alignment_new(0.0, 0.5, 0.0, 0.0);
    gtk_container_add(GTK_CONTAINER(alignment), unit->change_button);
    gtk_table_attach(GTK_TABLE(priv->widget), alignment, 2, priv->ncols, row, row+1, GTK_FILL, 0, 0, 0);
    gtk_widget_show_all(alignment);

    update_control_sensitivity(partable, i);
    g_signal_connect(unit->change_button, "clicked", G_CALLBACK(unit_chooser_change), partable);
}

static void
random_seed_make_control(GwyParamTable *partable, guint i, const GwyParamDefItem *def)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    GwyParamControlRandomSeed *randomseed = (GwyParamControlRandomSeed*)control->impl;
    GtkWidget *alignment;
    gint row = control->row;
    const gchar *label;
    gint value;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_RANDOM_SEED);
    make_control_common(partable, i);

    value = gwy_params_get_int(priv->params, control->id);
    randomseed->adj = GTK_ADJUSTMENT(gtk_adjustment_new(value, 1, 0x7fffffff, 1, 10, 0));
    g_object_set_qdata(G_OBJECT(randomseed->adj), param_control_quark, GUINT_TO_POINTER(i));
    control->widget = gtk_spin_button_new(randomseed->adj, 0, 0);
    g_object_set_qdata(G_OBJECT(control->widget), param_control_quark, GUINT_TO_POINTER(i));
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(control->widget), TRUE);
    gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(control->widget), TRUE);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(control->widget), 0);
    gtk_entry_set_alignment(GTK_ENTRY(control->widget), 1.0);
    gwy_debug("attach %s at %d..%d", g_type_name(G_TYPE_FROM_INSTANCE(control->widget)), row, row+1);
    gtk_table_attach(GTK_TABLE(priv->widget), control->widget, 1, 2, row, row+1, GTK_FILL, 0, 0, 0);
    gtk_widget_show(control->widget);

    label = (control->label_text ? control->label_text : def->desc);
    control->label = gtk_label_new_with_mnemonic(modify_label(partable, label, TRUE, FALSE));
    gtk_misc_set_alignment(GTK_MISC(control->label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(priv->widget), control->label, 0, 1, row, row+1, GTK_FILL, 0, 0, 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(control->label), control->widget);
    gtk_widget_show(control->label);

    randomseed->new_button = gtk_button_new_with_mnemonic(gwy_sgettext("seed|_New"));
    g_object_set_qdata(G_OBJECT(randomseed->new_button), param_control_quark, GUINT_TO_POINTER(i));
    alignment = gtk_alignment_new(0.0, 0.5, 0.0, 0.0);
    gtk_container_add(GTK_CONTAINER(alignment), randomseed->new_button);
    gtk_table_attach(GTK_TABLE(priv->widget), alignment, 2, priv->ncols, row, row+1, GTK_FILL, 0, 0, 0);
    gtk_widget_show_all(alignment);

    update_control_sensitivity(partable, i);
    g_signal_connect(randomseed->adj, "value-changed", G_CALLBACK(random_seed_changed), partable);
    g_signal_connect(randomseed->new_button, "clicked", G_CALLBACK(random_seed_new), partable);
}

static void
button_make_control(GwyParamTable *partable, guint i)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i, *othercontrol;
    GwyParamControlButton *button = (GwyParamControlButton*)control->impl, *otherbutton;
    GtkWidget *hbox;
    gint row = control->row;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_BUTTON);
    make_control_common(partable, i);

    control->widget = gtk_button_new_with_mnemonic(button->label);
    gwy_debug("attach %s at %d..%d", g_type_name(G_TYPE_FROM_INSTANCE(control->widget)), row, row+1);
    g_object_set_qdata(G_OBJECT(control->widget), param_control_quark, GUINT_TO_POINTER(i));
    if (button->sibling_id_prev >= 0) {
        othercontrol = find_button_box_end(partable, control, FALSE);
        otherbutton = (GwyParamControlButton*)othercontrol->impl;
        if (!otherbutton->sizegroup) {
            otherbutton->sizegroup = gtk_size_group_new(GTK_SIZE_GROUP_BOTH);
            gtk_size_group_add_widget(otherbutton->sizegroup, othercontrol->widget);
            g_object_unref(otherbutton->sizegroup);
        }
        gtk_size_group_add_widget(otherbutton->sizegroup, control->widget);

        hbox = gtk_widget_get_parent(othercontrol->widget);
        g_assert(GTK_IS_HBOX(hbox));
        gtk_box_pack_end(GTK_BOX(hbox), control->widget, TRUE, TRUE, 0);
        gtk_box_reorder_child(GTK_BOX(hbox), control->widget, 0);
        gtk_widget_show(control->widget);
    }
    else {
        attach_hbox_row(partable, row, control, NULL);
        update_control_unit_label(control, partable);
    }
    update_control_sensitivity(partable, i);
    g_signal_connect(control->widget, "clicked", G_CALLBACK(button_clicked), partable);
}

static void
results_make_control(GwyParamTable *partable, guint i)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    GwyParamControlResults *resultlist = (GwyParamControlResults*)control->impl;
    gint k, nresults, row = control->row;
    GwyResults *results;
    GtkSizeGroup *sizegroup;
    gchar **result_ids;
    GtkTable *table;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_RESULTS);
    make_control_common(partable, i);
    nresults = resultlist->nresults;
    result_ids = resultlist->result_ids;
    results = resultlist->results;
    g_assert(!resultlist->value_labels);
    resultlist->value_labels = g_new(GtkWidget*, nresults);
    sizegroup = gtk_size_group_new(GTK_SIZE_GROUP_BOTH);
    table = GTK_TABLE(priv->widget);
    for (k = 0; k < nresults; k++) {
        const gchar *desc = gwy_results_get_label_with_symbol(results, result_ids[k]);
        GtkWidget *label = gtk_label_new(NULL);
        GtkWidget *hbox = gwy_hbox_new(0);
        GtkWidget *alignment;

        gtk_label_set_markup(GTK_LABEL(label), modify_label(partable, desc, TRUE, TRUE));
        alignment = add_right_padding(label, GWY_PARAM_TABLE_COLSEP);
        gtk_table_attach(table, hbox, 0, 2, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

        gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
        gtk_box_pack_start(GTK_BOX(hbox), alignment, FALSE, FALSE, 0);

        resultlist->value_labels[k] = label = gtk_label_new(NULL);
        gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
        gtk_label_set_selectable(GTK_LABEL(label), TRUE);
        gtk_size_group_add_widget(sizegroup, label);
        gtk_box_pack_end(GTK_BOX(hbox), label, FALSE, FALSE, 0);
        gtk_widget_show_all(hbox);
        row++;
    }
    g_object_unref(sizegroup);
    if (resultlist->wants_to_be_filled)
        gwy_param_table_results_fill(partable, control->id);
}

static void
message_make_control(GwyParamTable *partable, guint i)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    gint row = control->row;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_MESSAGE);
    make_control_common(partable, i);

    control->label = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(control->label), 0.0, 0.5);
    gtk_label_set_markup(GTK_LABEL(control->label), control->label_text);
    gtk_label_set_line_wrap(GTK_LABEL(control->label), TRUE);
    gtk_table_attach(GTK_TABLE(priv->widget), control->label, 0, priv->ncols-1, row, row+1, GTK_FILL, 0, 0, 0);
    message_update_type(control);
}

static void
info_make_control(GwyParamTable *partable, guint i)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    GwyParamControlInfo *info = (GwyParamControlInfo*)control->impl;
    gint row = control->row;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_INFO);
    make_control_common(partable, i);

    control->widget = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(control->widget), 1.0, 0.5);
    gtk_label_set_markup(GTK_LABEL(control->widget), info->valuestr);
    g_object_set_qdata(G_OBJECT(control->widget), param_control_quark, GUINT_TO_POINTER(i));
    gwy_debug("attach %s at %d..%d", g_type_name(G_TYPE_FROM_INSTANCE(control->widget)), row, row+1);
    attach_hbox_row(partable, row, control, (control->label_text ? control->label_text : ""));
    update_control_unit_label(control, partable);
    update_control_sensitivity(partable, i);
}

static void
foreign_make_control(GwyParamTable *partable, guint i)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    GwyParamControlForeign *foreign = (GwyParamControlForeign*)control->impl;
    gint row = control->row;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_FOREIGN);
    make_control_common(partable, i);
    control->widget = foreign->create_widget(foreign->user_data);
    g_return_if_fail(GTK_IS_WIDGET(control->widget));
    gtk_table_attach(GTK_TABLE(priv->widget), control->widget, 0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    gtk_widget_show_all(control->widget);
    update_control_sensitivity(partable, i);
}

static void
checkbox_toggled(GtkToggleButton *toggle, GwyParamTable *partable)
{
    guint i = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(toggle), param_control_quark));
    gint id = partable->priv->controls[i].id;
    gboolean value = gtk_toggle_button_get_active(toggle);

    gwy_params_set_boolean(partable->priv->params, id, value);
    gwy_param_table_param_changed(partable, id);
}

static void
enabler_toggled(GtkToggleButton *toggle, GwyParamTable *partable)
{
    guint i = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(toggle), param_control_quark));
    gint id = partable->priv->controls[i].id;
    gboolean value = gtk_toggle_button_get_active(toggle);

    gwy_params_set_boolean(partable->priv->params, id, value);
    update_control_sensitivity(partable, find_control_for_aux(partable, &partable->priv->enabler, id));
    gwy_param_table_param_changed(partable, id);
}

static void
togglebutton_set_value(GwyParamControl *control, GwyParamTable *partable,
                       gboolean value, gboolean use_default_instead)
{
    GwyParams *params = partable->priv->params;
    gboolean changed;
    gint id;

    g_return_if_fail(control);
    g_return_if_fail(control->type == GWY_PARAM_CONTROL_CHECKBOX || control->type == GWY_PARAM_CONTROL_ENABLER);
    id = control->id;
    if (use_default_instead)
        changed = gwy_params_reset(params, id);
    else
        changed = gwy_params_set_boolean(params, id, value);

    if (changed && control->widget) {
        _gwy_param_table_in_update(partable, TRUE);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(control->widget), gwy_params_get_boolean(params, id));
        _gwy_param_table_in_update(partable, FALSE);
    }
    if (changed)
        gwy_param_table_param_changed(partable, id);
}

static void
combo_changed(GtkComboBox *gtkcombo, GwyParamTable *partable)
{
    guint i = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(gtkcombo), param_control_quark));
    GwyParamControl *control = partable->priv->controls + i;
    GwyParamControlCombo *combo = (GwyParamControlCombo*)control->impl;
    gint id = control->id;
    GwyResource *resource;
    const GwyEnum *penumval;
    GtkTreeModel *model;
    GtkTreeIter iter;

    gtk_combo_box_get_active_iter(gtkcombo, &iter);
    model = gtk_combo_box_get_model(gtkcombo);
    g_assert(model);
    if (combo->is_resource) {
        gtk_tree_model_get(model, &iter, 0, &resource, -1);
        gwy_debug("resource changed to \"%s\"", gwy_resource_get_name(resource));
        if (gwy_params_set_resource(partable->priv->params, id, gwy_resource_get_name(resource)))
            gwy_param_table_param_changed(partable, id);
    }
    else {
        gtk_tree_model_get(model, &iter, 0, &penumval, -1);
        if (gwy_params_set_enum(partable->priv->params, id, penumval->value))
            gwy_param_table_param_changed(partable, id);
    }
}

static void
enum_combo_set_value(GwyParamControl *control, GwyParamTable *partable,
                     gint value, gboolean use_default_instead)
{
    GwyParams *params = partable->priv->params;
    gboolean changed;
    const GwyEnum *penumval;
    GtkTreeModel *model;
    GtkTreeIter iter;
    gint id;

    g_return_if_fail(control && control->type == GWY_PARAM_CONTROL_COMBO);
    id = control->id;
    if (use_default_instead)
        changed = gwy_params_reset(params, id);
    else
        changed = gwy_params_set_enum(params, id, value);

    if (changed && control->widget) {
        _gwy_param_table_in_update(partable, TRUE);
        value = gwy_params_get_enum(params, id);
        model = gtk_combo_box_get_model(GTK_COMBO_BOX(control->widget));
        if (gtk_tree_model_get_iter_first(model, &iter)) {
            do {
                gtk_tree_model_get(model, &iter, 0, &penumval, -1);
                if (penumval->value == value) {
                    gtk_combo_box_set_active_iter(GTK_COMBO_BOX(control->widget), &iter);
                    break;
                }
            } while (gtk_tree_model_iter_next(model, &iter));
        }
        _gwy_param_table_in_update(partable, FALSE);
    }
    if (changed)
        gwy_param_table_param_changed(partable, id);
}

static void
unit_chosen(GtkComboBox *combo, GwyParamTable *partable)
{
    guint i = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(combo), param_control_quark));
    gint power10 = gwy_enum_combo_box_get_active(combo);
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    GwyParamControlUnitChooser *unit = (GwyParamControlUnitChooser*)control->impl;
    gint id = control->id;
    GwySIUnit *siunit;

    /* When the user entered some string, just keep it without reparsing. */
    if (unit->changing_unit)
        return;

    siunit = gwy_params_get_unit(priv->params, id, NULL);
    gwy_si_unit_get_format_for_power10(siunit, GWY_SI_UNIT_FORMAT_UNICODE, power10, priv->vf);
    if (gwy_params_set_unit(priv->params, id, priv->vf->units))
        gwy_param_table_param_changed(partable, id);
}

static void
unit_chooser_change(GtkButton *button, GwyParamTable *partable)
{
    guint i = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(button), param_control_quark));
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    GtkWindow *window;
    gchar *newunitstr;

    window = get_parent_window(partable, FALSE);
    if (!(newunitstr = unit_change_dialog_run(window, gwy_params_get_string(priv->params, control->id))))
        return;

    unit_chooser_set_value(control, partable, newunitstr, FALSE);
    g_free(newunitstr);
}

static gchar*
unit_change_dialog_run(GtkWindow *parent, const gchar *unitstr)
{
    GtkWidget *dialog, *hbox, *label, *entry, *content_vbox;
    gboolean parent_is_modal;
    gchar *unit = NULL;
    gint response;

    dialog = gtk_dialog_new_with_buttons(_("Change Units"), NULL, 0,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         GTK_STOCK_OK, GTK_RESPONSE_OK,
                                         NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
        /* Steal modality from the parent window, prevents appearing under it on MS Windows */
        parent_is_modal = gtk_window_get_modal(parent);
        if (parent_is_modal)
            gtk_window_set_modal(parent, FALSE);
    }
    else
        parent_is_modal = FALSE;

    hbox = gwy_hbox_new(6);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    content_vbox = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_box_pack_start(GTK_BOX(content_vbox), hbox, FALSE, FALSE, 0);

    label = gtk_label_new_with_mnemonic(_("New _units:"));
    gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);

    entry = gtk_entry_new();
    if (unitstr)
        gtk_entry_set_text(GTK_ENTRY(entry), unitstr);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), entry);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);

    gtk_widget_show_all(dialog);
    response = gtk_dialog_run(GTK_DIALOG(dialog));

    if (parent_is_modal)
        gtk_window_set_modal(parent, TRUE);

    if (response == GTK_RESPONSE_NONE)
        return NULL;
    if (response == GTK_RESPONSE_OK)
        unit = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
    gtk_widget_destroy(dialog);

    return unit;
}

static void
unit_chooser_set_value(GwyParamControl *control, GwyParamTable *partable,
                       const gchar *value, gboolean use_default_instead)
{
    GwyParamControlUnitChooser *unit = (GwyParamControlUnitChooser*)control->impl;
    GwyParams *params = partable->priv->params;
    GwySIUnit *siunit;
    gint id, power10;
    gboolean changed;

    g_return_if_fail(control && control->type == GWY_PARAM_CONTROL_UNIT_CHOOSER);
    id = control->id;
    if (use_default_instead)
        changed = gwy_params_reset(params, id);
    else
        changed = gwy_params_set_unit(params, id, value);

    if (changed && control->widget) {
        siunit = gwy_params_get_unit(params, id, &power10);
        g_assert(!unit->changing_unit);
        unit->changing_unit = TRUE;
        _gwy_param_table_in_update(partable, TRUE);
        gwy_combo_box_metric_unit_set_unit(GTK_COMBO_BOX(control->widget), power10 - 6, power10 + 6, siunit);
        gwy_enum_combo_box_set_active(GTK_COMBO_BOX(control->widget), power10);
        _gwy_param_table_in_update(partable, FALSE);
        unit->changing_unit = FALSE;
    }
    if (changed)
        gwy_param_table_param_changed(partable, id);
}

static void
resource_combo_set_value(GwyParamControl *control, GwyParamTable *partable,
                         const gchar *value, gboolean use_default_instead)
{
    GwyParams *params = partable->priv->params;
    gboolean changed;
    gint id;

    g_return_if_fail(control && control->type == GWY_PARAM_CONTROL_COMBO);
    id = control->id;
    if (use_default_instead)
        changed = gwy_params_reset(params, id);
    else
        changed = gwy_params_set_resource(params, id, value);

    if (changed && control->widget) {
        GwyParamControlCombo *combo = (GwyParamControlCombo*)control->impl;
        GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(control->widget));
        GtkTreeIter iter, filter_iter;

        value = gwy_params_get_string(params, id);
        _gwy_param_table_in_update(partable, TRUE);
        if (combo->filter_func) {
            GtkTreeModel *store = gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(model));
            gwy_inventory_store_get_iter(GWY_INVENTORY_STORE(store), value, &iter);
            gtk_tree_model_filter_convert_child_iter_to_iter(GTK_TREE_MODEL_FILTER(model), &filter_iter, &iter);
            iter = filter_iter;
        }
        else
            gwy_inventory_store_get_iter(GWY_INVENTORY_STORE(model), value, &iter);
        gtk_combo_box_set_active_iter(GTK_COMBO_BOX(control->widget), &iter);
        _gwy_param_table_in_update(partable, FALSE);
    }
    if (changed)
        gwy_param_table_param_changed(partable, id);
}

static void
radio_changed(GtkRadioButton *radio, GwyParamTable *partable)
{
    guint i = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(radio), param_control_quark));
    gint id = partable->priv->controls[i].id;
    GSList *group = gtk_radio_button_get_group(radio);
    gint value = gwy_radio_buttons_get_current(group);

    if (gwy_params_set_enum(partable->priv->params, id, value))
        gwy_param_table_param_changed(partable, id);
}

static void
radio_set_value(GwyParamControl *control, GwyParamTable *partable,
                gint value, gboolean use_default_instead)
{
    GwyParams *params = partable->priv->params;
    gboolean changed;
    gint id;

    g_return_if_fail(control && control_is_some_kind_of_radio(control->type));
    id = control->id;
    if (use_default_instead)
        changed = gwy_params_reset(params, id);
    else
        changed = gwy_params_set_enum(params, id, value);

    if (changed && control->widget) {
        _gwy_param_table_in_update(partable, TRUE);
        gwy_radio_buttons_set_current(gtk_radio_button_get_group(GTK_RADIO_BUTTON(control->widget)),
                                      gwy_params_get_enum(params, id));
        _gwy_param_table_in_update(partable, FALSE);
    }
    if (changed)
        gwy_param_table_param_changed(partable, id);
}

static void
checkbox_changed(GtkToggleButton *toggle, GwyParamTable *partable)
{
    guint i = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(toggle), param_control_quark));
    gint id = partable->priv->controls[i].id;
    guint flag = gwy_check_box_get_value(GTK_WIDGET(toggle));
    gboolean value = gtk_toggle_button_get_active(toggle);

    if (gwy_params_set_flag(partable->priv->params, id, flag, value))
        gwy_param_table_param_changed(partable, id);
}

static void
checkboxes_set_value(GwyParamControl *control, GwyParamTable *partable,
                     guint value, gboolean use_default_instead)
{
    GwyParams *params = partable->priv->params;
    gboolean changed;
    gint id;

    g_return_if_fail(control && control->type == GWY_PARAM_CONTROL_CHECKBOXES);
    id = control->id;
    if (use_default_instead)
        changed = gwy_params_reset(params, id);
    else
        changed = gwy_params_set_flags(params, id, value);

    if (changed && control->widget) {
        _gwy_param_table_in_update(partable, TRUE);
        gwy_check_boxes_set_selected(gwy_check_box_get_group(control->widget), gwy_params_get_flags(params, id));
        _gwy_param_table_in_update(partable, FALSE);
    }
    if (changed)
        gwy_param_table_param_changed(partable, id);
}

static void
graph_id_changed(GwyDataChooser *chooser, GwyParamTable *partable)
{
    guint i = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(chooser), param_control_quark));
    gint id = partable->priv->controls[i].id;
    GwyAppDataId value;

    gwy_data_chooser_get_active_id(chooser, &value);
    if (gwy_params_set_graph_id(partable->priv->params, id, value))
        gwy_param_table_param_changed(partable, id);
}

static void
image_id_changed(GwyDataChooser *chooser, GwyParamTable *partable)
{
    guint i = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(chooser), param_control_quark));
    gint id = partable->priv->controls[i].id;
    GwyAppDataId value;

    gwy_data_chooser_get_active_id(chooser, &value);
    if (gwy_params_set_image_id(partable->priv->params, id, value))
        gwy_param_table_param_changed(partable, id);
}

static void
volume_id_changed(GwyDataChooser *chooser, GwyParamTable *partable)
{
    guint i = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(chooser), param_control_quark));
    gint id = partable->priv->controls[i].id;
    GwyAppDataId value;

    gwy_data_chooser_get_active_id(chooser, &value);
    if (gwy_params_set_volume_id(partable->priv->params, id, value))
        gwy_param_table_param_changed(partable, id);
}

static void
xyz_id_changed(GwyDataChooser *chooser, GwyParamTable *partable)
{
    guint i = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(chooser), param_control_quark));
    gint id = partable->priv->controls[i].id;
    GwyAppDataId value;

    gwy_data_chooser_get_active_id(chooser, &value);
    if (gwy_params_set_xyz_id(partable->priv->params, id, value))
        gwy_param_table_param_changed(partable, id);
}

static void
curve_map_id_changed(GwyDataChooser *chooser, GwyParamTable *partable)
{
    guint i = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(chooser), param_control_quark));
    gint id = partable->priv->controls[i].id;
    GwyAppDataId value;

    gwy_data_chooser_get_active_id(chooser, &value);
    if (gwy_params_set_curve_map_id(partable->priv->params, id, value))
        gwy_param_table_param_changed(partable, id);
}

static void
data_id_set_value(GwyParamControl *control,
                  GwyParamTable *partable,
                  GwyAppDataId value,
                  gboolean use_default_instead)
{
    GwyParams *params = partable->priv->params;
    gboolean changed = FALSE, is_ok = TRUE;
    gint id;

    g_return_if_fail(control && control_is_some_kind_of_data_id(control->type));
    id = control->id;
    /* FIXME: This is not so simple.  We have to apply the filter!  */
    if (use_default_instead)
        changed = gwy_params_reset(params, id);
    else {
        if (control->type == GWY_PARAM_CONTROL_GRAPH_ID_COMBO) {
            GwyParamControlDataChooser *datachooser = (GwyParamControlDataChooser*)control->impl;

            if (value.datano > 0) {
                GwyContainer *container = gwy_app_data_browser_get(value.datano);
                if (datachooser->filter_func)
                    is_ok = datachooser->filter_func(container, value.id, datachooser->filter_data);
            }
            if (is_ok)
                changed = gwy_params_set_graph_id(params, id, value);
            gwy_debug("dataid is_ok %d, changed %d", is_ok, changed);
        }
        else if (control->type == GWY_PARAM_CONTROL_IMAGE_ID_COMBO)
            changed = gwy_params_set_image_id(params, id, value);
        else if (control->type == GWY_PARAM_CONTROL_VOLUME_ID_COMBO)
            changed = gwy_params_set_volume_id(params, id, value);
        else if (control->type == GWY_PARAM_CONTROL_XYZ_ID_COMBO)
            changed = gwy_params_set_xyz_id(params, id, value);
        else if (control->type == GWY_PARAM_CONTROL_CURVE_MAP_ID_COMBO)
            changed = gwy_params_set_curve_map_id(params, id, value);
    }

    if (changed && control->widget) {
        _gwy_param_table_in_update(partable, TRUE);
        value = gwy_params_get_data_id(params, id);
        gwy_data_chooser_set_active_id(GWY_DATA_CHOOSER(control->widget), &value);
        _gwy_param_table_in_update(partable, FALSE);
    }
    if (changed)
        gwy_param_table_param_changed(partable, id);
}

static void
graph_curve_changed(GtkComboBox *combo, GwyParamTable *partable)
{
    guint i = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(combo), param_control_quark));
    GwyParamTablePrivate *priv = partable->priv;
    gint id = priv->controls[i].id;
    gint value = gwy_enum_combo_box_get_active(combo);
    GwyParamControl *control = priv->controls + i;
    GwyParamControlCurveChooser *curvechooser = (GwyParamControlCurveChooser*)control->impl;
    GwyParams *params = priv->params;
    GwyGraphCurveModel *gcmodel = gwy_graph_model_get_curve(GWY_GRAPH_MODEL(curvechooser->parent), value);
    gchar *description;

    if (gcmodel) {
        g_object_get(gcmodel, "description", &description, NULL);
        gwy_params_set_string(params, id, description);
        g_free(description);
    }
    if (gwy_params_set_curve(params, id, value))
        gwy_param_table_param_changed(partable, id);
}

static void
lawn_curve_changed(GtkComboBox *combo, GwyParamTable *partable)
{
    guint i = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(combo), param_control_quark));
    GwyParamTablePrivate *priv = partable->priv;
    gint id = priv->controls[i].id;
    gint value = gwy_enum_combo_box_get_active(combo);
    GwyParamControl *control = priv->controls + i;
    GwyParamControlCurveChooser *curvechooser = (GwyParamControlCurveChooser*)control->impl;
    GwyParams *params = priv->params;
    const gchar *label;

    label = gwy_lawn_get_curve_label(GWY_LAWN(curvechooser->parent), value);
    gwy_params_set_string(params, id, label ? label : "");
    if (gwy_params_set_curve(params, id, value))
        gwy_param_table_param_changed(partable, id);
}

static void
lawn_segment_changed(GtkComboBox *combo, GwyParamTable *partable)
{
    guint i = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(combo), param_control_quark));
    GwyParamTablePrivate *priv = partable->priv;
    gint id = priv->controls[i].id;
    gint value = gwy_enum_combo_box_get_active(combo);
    GwyParamControl *control = priv->controls + i;
    GwyParamControlCurveChooser *curvechooser = (GwyParamControlCurveChooser*)control->impl;
    GwyParams *params = priv->params;
    const gchar *label;

    label = gwy_lawn_get_segment_label(GWY_LAWN(curvechooser->parent), value);
    gwy_params_set_string(params, id, label ? label : "");
    if (gwy_params_set_curve(params, id, value))
        gwy_param_table_param_changed(partable, id);
}

static void
curve_no_set_value(GwyParamControl *control,
                   GwyParamTable *partable,
                   gint value,
                   gboolean use_default_instead)
{
    GwyParams *params = partable->priv->params;
    gboolean changed = FALSE;
    gint id, n;

    g_return_if_fail(control && control_is_some_kind_of_curve_no(control->type));
    n = curve_no_get_ncurves(control);
    id = control->id;
    /* Special-case the no-curve case.  Plain reset does not cut it. */
    if (n && use_default_instead)
        changed = gwy_params_reset(params, id);
    else {
        if (!n)
            value = -1;
        changed = gwy_params_set_curve(params, id, value);
    }

    if (changed && control->widget) {
        _gwy_param_table_in_update(partable, TRUE);
        value = gwy_params_get_int(params, id);
        gwy_enum_combo_box_set_active(GTK_COMBO_BOX(control->widget), value);
        _gwy_param_table_in_update(partable, FALSE);
    }
    if (changed)
        gwy_param_table_param_changed(partable, id);
}

static gint
curve_no_get_ncurves(GwyParamControl *control)
{
    GwyParamControlCurveChooser *curvechooser = (GwyParamControlCurveChooser*)control->impl;

    if (control->type == GWY_PARAM_CONTROL_GRAPH_CURVE_COMBO)
        return gwy_graph_model_get_n_curves(GWY_GRAPH_MODEL(curvechooser->parent));
    if (control->type == GWY_PARAM_CONTROL_LAWN_CURVE_COMBO)
        return gwy_lawn_get_n_curves(GWY_LAWN(curvechooser->parent));
    if (control->type == GWY_PARAM_CONTROL_LAWN_SEGMENT_COMBO)
        return gwy_lawn_get_n_segments(GWY_LAWN(curvechooser->parent));
    g_assert_not_reached();
    return -1;
}

static void
random_seed_changed(GtkAdjustment *adj, GwyParamTable *partable)
{
    guint i = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(adj), param_control_quark));
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    gint id = control->id;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_RANDOM_SEED);
    if (gwy_params_set_int(priv->params, id, gwy_adjustment_get_int(adj)))
        gwy_param_table_param_changed(partable, id);
}

static void
random_seed_new(GtkButton *button, GwyParamTable *partable)
{
    guint i = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(button), param_control_quark));
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    GwyParamControlRandomSeed *randomseed = (GwyParamControlRandomSeed*)control->impl;
    gint id = control->id;

    _gwy_param_table_in_update(partable, TRUE);
    gtk_adjustment_set_value(randomseed->adj, gwy_params_randomize_seed(priv->params, id));
    _gwy_param_table_in_update(partable, FALSE);
    gwy_param_table_param_changed(partable, id);
}

static void
button_clicked(GtkButton *gtkbutton, GwyParamTable *partable)
{
    guint i = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(gtkbutton), param_control_quark));
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    GwyParamControlButton *button = (GwyParamControlButton*)control->impl;
    GtkWindow *dialog;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_BUTTON);
    dialog = get_parent_window(partable, TRUE);
    if (dialog)
        gtk_dialog_response(GTK_DIALOG(dialog), button->response);
    else {
        g_warning("Cannot find any dialog for button with response %d.", button->response);
    }
}

static void
color_set_value(GwyParamControl *control, GwyParamTable *partable,
                GwyRGBA color, gboolean use_default_instead)
{
    GwyParams *params = partable->priv->params;
    gboolean changed;
    gint id;

    g_return_if_fail(control && (control->type == GWY_PARAM_CONTROL_COLOR
                                 || control->type == GWY_PARAM_CONTROL_MASK_COLOR));
    id = control->id;
    changed = (use_default_instead ? gwy_params_reset(params, id) : gwy_params_set_color(params, id, color));

    if (changed && control->widget) {
        color = gwy_params_get_color(params, id);
        _gwy_param_table_in_update(partable, TRUE);
        gwy_color_button_set_color(GWY_COLOR_BUTTON(control->widget), &color);
        _gwy_param_table_in_update(partable, FALSE);
    }
    if (changed)
        gwy_param_table_param_changed(partable, id);
}

static void
report_format_changed(GwyResultsExport *rexport, GwyParamTable *partable)
{
    guint i = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(rexport), param_control_quark));
    GwyParamControl *control = partable->priv->controls + i;
    gint id = control->id;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_REPORT);
    if (gwy_params_set_report_type(partable->priv->params, id, gwy_results_export_get_format(rexport)))
        gwy_param_table_param_changed(partable, id);
}

static void
report_copy(GwyResultsExport *rexport, GwyParamTable *partable)
{
    guint i = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(rexport), param_control_quark));
    GwyParamControl *control = partable->priv->controls + i;
    GwyParamControlReport *report = (GwyParamControlReport*)control->impl;
    GtkClipboard *clipboard;
    GdkDisplay *display;
    gchar *text;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_REPORT);
    g_return_if_fail(report->format_report);

    text = report->format_report(report->formatter_data);
    display = gtk_widget_get_display(control->widget);
    clipboard = gtk_clipboard_get_for_display(display, GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clipboard, text, -1);
    g_free(text);
}

static void
report_save(GwyResultsExport *rexport, GwyParamTable *partable)
{
    guint i = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(rexport), param_control_quark));
    GwyParamControl *control = partable->priv->controls + i;
    GwyParamControlReport *report = (GwyParamControlReport*)control->impl;
    const gchar *title = NULL;
    gchar *text;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_REPORT);
    g_return_if_fail(report->format_report);
    if (control->label_text)
        title = control->label_text;
    else {
        const GwyParamDefItem *def;
        if (find_def_common(partable, control->id, NULL, &def))
            title = def->desc;
    }
    if (!title)
        title = _("Save Results to File");

    text = report->format_report(report->formatter_data);
    gwy_save_auxiliary_data(title, get_parent_window(partable, FALSE), -1, text);
    g_free(text);
}

static void
report_set_formatter(GwyParamControl *control,
                     GwyCreateTextFunc format_report,
                     gpointer user_data,
                     GDestroyNotify destroy)
{
    GwyParamControlReport *report = control->impl;
    GDestroyNotify old_destroy = report->formatter_destroy;
    gpointer old_data = report->formatter_data;

    report->format_report = format_report;
    report->formatter_data = user_data;
    report->formatter_destroy = destroy;
    if (old_destroy)
        old_destroy(old_data);
}

static void
report_ensure_actions(GwyParamControl *control, GwyParamTable *partable)
{
    GwyParamControlReport *report = control->impl;

    g_return_if_fail(control->widget);
    if (!report->copy_sid)
        report->copy_sid = g_signal_connect(control->widget, "copy", G_CALLBACK(report_copy), partable);
    if (!report->save_sid)
        report->save_sid = g_signal_connect(control->widget, "save", G_CALLBACK(report_save), partable);
}

static void
report_set_value(GwyParamControl *control, GwyParamTable *partable,
                 GwyResultsReportType value, gboolean use_default_instead)
{
    GwyParams *params = partable->priv->params;
    gboolean changed;
    gint id;

    g_return_if_fail(control && control->type == GWY_PARAM_CONTROL_REPORT);
    id = control->id;
    if (use_default_instead)
        changed = gwy_params_reset(params, id);
    else
        changed = gwy_params_set_report_type(params, id, value);

    if (changed && control->widget) {
        value = gwy_params_get_report_type(params, id);
        _gwy_param_table_in_update(partable, TRUE);
        gwy_results_export_set_format(GWY_RESULTS_EXPORT(control->widget), value);
        _gwy_param_table_in_update(partable, FALSE);
    }
    if (changed)
        gwy_param_table_param_changed(partable, id);
}

static void
hold_selection_changed(GtkWidget *button, GwyParamTable *partable)
{
    guint i = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(button), param_control_quark));
    GwyParamControl *control = partable->priv->controls + i;
    gboolean is_set = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button));
    GwyHoldSelectionFlags flag_to_set = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "gwy-checkboxes-key"));
    gint id = control->id;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_HOLD_SELECTION);
    if (gwy_params_set_flag(partable->priv->params, id, flag_to_set, is_set)) {
        gwy_param_table_param_changed(partable, id);
        update_control_sensitivity(partable, i);
    }
}

static void
hold_selection_set_value(GwyParamControl *control, GwyParamTable *partable,
                         GwyHoldSelectionFlags value, gboolean use_default_instead)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParams *params = priv->params;
    guint i = control - priv->controls;
    gboolean changed;
    gint id;

    g_return_if_fail(control && control->type == GWY_PARAM_CONTROL_HOLD_SELECTION);
    id = control->id;
    if (use_default_instead)
        changed = gwy_params_reset(params, id);
    else
        changed = gwy_params_set_flags(params, id, value);

    if (changed) {
        if (control->widget)
            update_control_sensitivity(partable, i);
        gwy_param_table_param_changed(partable, id);
    }
}

static void
slider_value_changed(GtkAdjustment *adj, GwyParamTable *partable)
{
    guint i = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(adj), param_control_quark));
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    GwyParamControlSlider *slider = (GwyParamControlSlider*)control->impl;
    gint id = control->id;
    gboolean changed;

    gwy_debug("start");
    g_return_if_fail(control->type == GWY_PARAM_CONTROL_SLIDER);
    if (slider->is_int)
        changed = gwy_params_set_int(priv->params, id, gwy_adjustment_get_int(adj));
    else
        changed = gwy_params_set_double(priv->params, id, gtk_adjustment_get_value(adj));

    if (changed)
        gwy_param_table_param_changed(partable, id);
    gwy_debug("end");
}

static gint
slider_spin_input(GtkSpinButton *spin, gdouble *new_value, GwyParamTable *partable)
{
    guint i = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(spin), param_control_quark));
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    GwyParamControlSlider *slider = (GwyParamControlSlider*)control->impl;
    gdouble value, alt_q = slider->alt_q_to_gui;
    gchar *end;

    gwy_debug("%p (spin=%p, alt=%p)", spin, slider->spin, slider->alt_spin);
    value = g_strtod(gtk_entry_get_text(GTK_ENTRY(spin)), &end);
    if (*end)
        return GTK_INPUT_ERROR;

    if (spin == (GtkSpinButton*)slider->alt_spin) {
        gwy_debug("transforming from alt %g", value);
        if (slider->calibration)
            value = calibration_transform_from_gui(slider->calibration, value/alt_q);
        else if (alt_q > 0.0)
            value = (value - slider->alt_offset_to_gui)/alt_q;
        else
            value = 0.0;
    }
    else if (slider->transform_from_gui) {
        gwy_debug("transforming from %g", value);
        value = slider->transform_from_gui(value, slider->transform_data);
    }

    gwy_debug("obtained true value %g", value);
    *new_value = value;

    return TRUE;
}

static gboolean
slider_spin_output(GtkSpinButton *spin, GwyParamTable *partable)
{
    guint i = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(spin), param_control_quark));
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    GwyParamControlSlider *slider = (GwyParamControlSlider*)control->impl;
    gint digits = slider->digits;
    GString *str = priv->str;
    gdouble value, alt_q = slider->alt_q_to_gui;

    gwy_debug("%p (spin=%p, alt=%p)", spin, slider->spin, slider->alt_spin);
    value = gtk_adjustment_get_value(slider->adj);
    gwy_debug("transforming true value %g", value);
    if (spin == (GtkSpinButton*)slider->alt_spin) {
        gwy_debug("transforming for alt");
        if (slider->calibration)
            value = alt_q*calibration_transform_to_gui(slider->calibration, value);
        else
            value = alt_q*value + slider->alt_offset_to_gui;
        digits = slider->alt_digits;
    }
    else if (slider->transform_to_gui) {
        gwy_debug("transforming for primary");
        value = slider->transform_to_gui(value, slider->transform_data);
    }

    gwy_debug("transformed value %g", value);
    format_numerical_value(str, value, digits);
    if (!gwy_strequal(str->str, gtk_entry_get_text(GTK_ENTRY(spin))))
        gtk_entry_set_text(GTK_ENTRY(spin), str->str);

    return TRUE;
}

static void
slider_set_value(GwyParamControl *control, GwyParamTable *partable,
                 gdouble value, gboolean use_default_instead)
{
    GwyParams *params = partable->priv->params;
    GwyParamControlSlider *slider;
    gboolean changed = FALSE;
    gint id;

    g_return_if_fail(control && control->type == GWY_PARAM_CONTROL_SLIDER);
    id = control->id;
    slider = (GwyParamControlSlider*)control->impl;
    if (use_default_instead) {
        changed = gwy_params_reset(params, id);
        value = (slider->is_int ? gwy_params_get_int(params, id) : gwy_params_get_double(params, id));
    }
    value = CLAMP(value, slider->minimum, slider->maximum);
    changed = (slider->is_int
               ? gwy_params_set_int(params, id, GWY_ROUND(value))
               : gwy_params_set_double(params, id, value)) || changed;

    /* Update the controls.  But we already set the correct value so slider_value_changed() will not emit any signal.
     * Make sure a signal is emitted, but also make sure we do not emit it twice if the adjustment update decides to
     * emit it too (rounding errors or something).  */
    if (changed && control->widget) {
        value = (slider->is_int ? gwy_params_get_int(params, id) : gwy_params_get_double(params, id));
        _gwy_param_table_in_update(partable, TRUE);
        gtk_adjustment_set_value(slider->adj, value);
        _gwy_param_table_in_update(partable, FALSE);
    }
    if (changed)
        gwy_param_table_param_changed(partable, id);
}

static void
slider_make_angle(GwyParamTable *partable, guint i)
{
    GwyParamControl *control = partable->priv->controls + i;
    GwyParamControlSlider *slider = (GwyParamControlSlider*)control->impl;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_SLIDER);
    if (slider->is_angle)
        return;
    slider->q_value_to_gui = 180.0/G_PI;
    slider->is_angle = TRUE;
    slider->is_percentage = FALSE;
    slider_set_transformation(partable, i, multiply_by_constant, divide_by_constant, &slider->q_value_to_gui, NULL);
    gwy_param_table_set_unitstr(partable, control->id, _("deg"));
}

static void
slider_make_percentage(GwyParamTable *partable, guint i)
{
    GwyParamControl *control = partable->priv->controls + i;
    GwyParamControlSlider *slider = (GwyParamControlSlider*)control->impl;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_SLIDER);
    if (slider->is_percentage)
        return;
    slider->q_value_to_gui = 100.0;
    slider->is_percentage = TRUE;
    slider->is_angle = FALSE;
    slider_set_transformation(partable, i, multiply_by_constant, divide_by_constant, &slider->q_value_to_gui, NULL);
    gwy_param_table_set_unitstr(partable, control->id, "%");
}

static void
slider_auto_configure(GwyParamControl *control, const GwyParamDefItem *def)
{
    GwyParamControlSlider *slider = (GwyParamControlSlider*)control->impl;
    GwyRealFunc transform_to_gui = slider->transform_to_gui;
    gpointer data = slider->transform_data;
    gdouble min, max;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_SLIDER);

    if (!slider->snap_set)
        slider->snap = slider->is_int;

    /* GwyAdjustBar should handle default mapping style itself.  Do not try to guess anything. */

    if (!slider->range_set) {
        gwy_debug("range not limited; using the full range");
        if (slider->is_int) {
            slider->minimum = def->def.i.minimum;
            slider->maximum = def->def.i.maximum;
        }
        else {
            slider->minimum = def->def.d.minimum;
            slider->maximum = def->def.d.maximum;
        }
    }

    min = slider->minimum;
    max = slider->maximum;

    if (!slider->digits_set && !slider->is_int) {
        /* Digits are for displayed values, so for these we need to transform. */
        gdouble spin_min = min, spin_max = max;

        if (transform_to_gui) {
            spin_min = transform_to_gui(spin_min, data);
            spin_max = transform_to_gui(spin_max, data);
        }
        spin_max = fabs(spin_max);
        gwy_debug("digits not set, using spinner range [%g..%g]", spin_min, spin_max);
        if (spin_min > 0.0) {
            /* For all-positive range the minimum value should allow guessing a good precision, except when the range
             * is small like [0.5, 1].  Allow slightly more precise numbers than the minimum. */
            if (spin_max > 0.0)
                slider->digits = (gint)ceil(fmax(-log10(spin_min) + 0.5, -log10(spin_max) + 3.5));
            else
                slider->digits = (gint)ceil(-log10(spin_min) + 0.5);
        }
        else {
            /* About 4 decimal places is a good rule of thumb.  For range [0..1] we usually like 0.xxx.  */
            gdouble rmax = fmax(-spin_min, spin_max);
            if (rmax > 0.0)
                slider->digits = (gint)floor(-log10(rmax)) + 4;
        }
        if (slider->steps_set) {
            gdouble tstep = slider->step;
            if (transform_to_gui)
                tstep = (transform_to_gui(min + tstep, data) - transform_to_gui(min, data));
            slider->digits = fmax(slider->digits, (gint)ceil(-log10(tstep) - 0.01));
        }
        /* Keep at least one decimal place for floats – there must be a reason why they are not integers. */
        slider->digits = MIN(slider->digits, 8);
        slider->digits = MAX(slider->digits, 1);
    }
    else if (!slider->digits_set)
        slider->digits = 0;

    if (!slider->steps_set) {
        gdouble range = max - min;

        if (slider->is_int) {
            slider->step = 1.0;
            if (range <= 5.0)
                slider->page = 1.0;
            else if (range <= 20.0)
                slider->page = 2.0;
            else if (range <= 50.0)
                slider->page = 5.0;
            else if (range <= 1000.0)
                slider->page = 10.0;
            else
                slider->page = 100.0;
        }
        else {
            gdouble toff = 0.0;
            if (transform_to_gui) {
                gdouble h = 0.0001*range;
                toff = (transform_to_gui(min + h, data) - transform_to_gui(min, data))/h;
                if (toff > 1e-8)
                    toff = log10(toff);
                else
                    toff = 0.0;
            }
            slider->step = pow10(-ceil(slider->digits + toff - 1.5));
            slider->page = pow10(ceil((2.0*log10(slider->step) + log10(fabs(max)))/3.0));
            slider->page = CLAMP(slider->page, slider->step, max - min);
        }
    }
    gwy_debug("step=%g, page=%g, digits=%d (snap=%d)", slider->step, slider->page, slider->digits, slider->snap);
}

static void
slider_reconfigure_adjustment(GwyParamControl *control, GwyParamTable *partable)
{
    GwyParams *params = partable->priv->params;
    GwyParamControlSlider *slider;
    gint id = control->id;
    gdouble value;
    guint i;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_SLIDER);
    slider = (GwyParamControlSlider*)control->impl;
    g_return_if_fail(GTK_IS_ADJUSTMENT(slider->adj));
    g_return_if_fail(GTK_IS_SPIN_BUTTON(slider->spin));
    i = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(slider->spin), param_control_quark));

    value = (slider->is_int ? gwy_params_get_int(params, id) : gwy_params_get_double(params, id));
    value = CLAMP(value, slider->minimum, slider->maximum);

    /* We set the value again below.  But gtk_adjustment_configure() needs the value. */
    gwy_debug("step=%g, page=%g, digits=%d (snap=%d)", slider->step, slider->page, slider->digits, slider->snap);
    gtk_adjustment_configure(slider->adj, value, slider->minimum, slider->maximum, slider->step, slider->page, 0.0);
    gtk_spin_button_configure(GTK_SPIN_BUTTON(slider->spin), slider->adj, 0.5*slider->step, slider->digits);
    slider_set_width_chars(partable, i);
    if (slider->has_alt)
        slider_reconfigure_alt(control, partable);
    slider_set_value(control, partable, value, FALSE);
}

static void
slider_reconfigure_alt(GwyParamControl *control, GwyParamTable *partable)
{
    GwyParamControlSlider *slider;
    GtkSpinButton *alt_spin;
    gdouble h;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_SLIDER);
    slider = (GwyParamControlSlider*)control->impl;
    g_return_if_fail(slider->has_alt);
    g_return_if_fail(GTK_IS_SPIN_BUTTON(slider->alt_spin));
    alt_spin = GTK_SPIN_BUTTON(slider->alt_spin);
    _gwy_param_table_in_update(partable, TRUE);
    if (slider->calibration) {
        gwy_debug("alt transformation is a calibration table");
        /* XXX XXX XXX: The digits may be OK, but the step is probably broken. */
        h = slider->calibration_step*slider->alt_q_to_gui;
        slider->alt_digits = (gint)floor(-log10(fabs(slider->calibration_delta*slider->alt_q_to_gui)) + 0.999999);
    }
    else {
        gwy_debug("alt transformation is linear");
        h = pow10(-slider->digits);
        if (slider->transform_to_gui && slider->transform_from_gui) {
            gpointer data = slider->transform_data;
            gdouble min = slider->minimum;
            h = slider->transform_from_gui(slider->transform_to_gui(min, data) + h, data) - min;
            if (!(h > 0.0))
                h = G_MAXDOUBLE;
        }
        h = fmin(h, slider->step);
        gwy_debug("master h=%g", h);
        if (slider->alt_q_to_gui > 0.0)
            h *= slider->alt_q_to_gui;
        gwy_debug("transformed h=%g", h);
        /* We cannot have user spinning the alt spiner and nothing happening because it gets rounded to the same value
         * again. */
        slider->alt_digits = (gint)floor(-log10(fabs(h)) + 0.999999);
    }
    gwy_debug("digits %d", slider->alt_digits);
    slider->alt_digits = MIN(slider->alt_digits, 8);
    gtk_spin_button_set_digits(alt_spin, slider->alt_digits);
    gtk_spin_button_set_snap_to_ticks(alt_spin, slider->snap);
    g_object_set(alt_spin, "climb-rate", 0.5*slider->step, NULL);
    gtk_label_set_markup(GTK_LABEL(slider->alt_unitlabel), slider->alt_unitstr);
    _gwy_param_table_in_update(partable, FALSE);
}

static void
slider_set_transformation(GwyParamTable *partable, guint i,
                          GwyRealFunc value_to_gui, GwyRealFunc gui_to_value,
                          gpointer user_data, GDestroyNotify destroy)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    GwyParams *params = priv->params;
    gint id = control->id;
    const GwyParamDefItem *def;
    GwyParamControlSlider *slider;

    g_return_if_fail((value_to_gui && gui_to_value) || (!value_to_gui && !gui_to_value));
    g_return_if_fail(!destroy || value_to_gui);
    g_return_if_fail(control->type == GWY_PARAM_CONTROL_SLIDER);
    if (!find_def_common(partable, id, NULL, &def))
        return;
    g_return_if_fail(def->type == GWY_PARAM_DOUBLE);
    slider = (GwyParamControlSlider*)control->impl;

    if (slider->transform_destroy)
        slider->transform_destroy(slider->transform_data);
    slider->transform_to_gui = value_to_gui;
    slider->transform_from_gui = gui_to_value;
    slider->transform_data = user_data;
    slider->transform_destroy = destroy;
    slider_auto_configure(control, def);

    if (!control->widget)
        return;

    gwy_debug("attempting run-time slider transformation switch (id %d)", id);
    _gwy_param_table_in_update(partable, TRUE);
    if (slider->is_int) {
        gint value = gwy_params_get_int(params, id);
        slider_set_width_chars(partable, i);
        gwy_param_table_set_int(partable, id, value);
    }
    else {
        gdouble value = gwy_params_get_double(params, id);
        slider_set_width_chars(partable, i);
        gwy_param_table_set_double(partable, id, value);
    }
    /* Setting value above should usually do nothing.  This is where the displayed value changes. */
    slider_spin_output(GTK_SPIN_BUTTON(slider->spin), partable);
    _gwy_param_table_in_update(partable, FALSE);
}

static void
slider_set_width_chars(GwyParamTable *partable, guint i)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    GwyParamControlSlider *slider = (GwyParamControlSlider*)control->impl;
    GwyRealFunc transform_to_gui = slider->transform_to_gui;
    gpointer data = slider->transform_data;
    GString *str = priv->str;
    gdouble value;
    guint len;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_SLIDER);
    g_return_if_fail(slider->spin);

    value = slider->minimum;
    if (transform_to_gui)
        value = transform_to_gui(value, data);
    format_numerical_value(str, value, slider->digits);
    len = str->len;

    value = slider->maximum;
    if (transform_to_gui)
        value = transform_to_gui(value, data);
    format_numerical_value(str, value, slider->digits);
    len = MAX(len, str->len);

    gtk_entry_set_width_chars(GTK_ENTRY(slider->spin), len);
}

static void
alt_set_width_chars(GwyParamTable *partable, guint i)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    GwyParamControlSlider *slider = (GwyParamControlSlider*)control->impl;
    GString *str = priv->str;
    gdouble value;
    guint len;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_SLIDER);
    g_return_if_fail(slider->alt_spin);

    value = slider->alt_q_to_gui*slider->minimum + slider->alt_offset_to_gui;
    format_numerical_value(str, value, slider->alt_digits);
    len = str->len;

    value = slider->alt_q_to_gui*slider->maximum + slider->alt_offset_to_gui;
    format_numerical_value(str, value, slider->alt_digits);
    len = MAX(len, str->len);

    gtk_entry_set_width_chars(GTK_ENTRY(slider->alt_spin), len);
}

static void
alt_set_from_value_format(GwyParamTable *partable, gint id, const gchar *unitstr,
                          gdouble raw_q, gdouble raw_offset)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwySIValueFormat *vf = priv->vf;
    GwyParamControl *control;
    GwyParamControlSlider *slider;

    control = find_first_control(partable, id);
    g_return_if_fail(control && control->type == GWY_PARAM_CONTROL_SLIDER);
    slider = (GwyParamControlSlider*)control->impl;
    if (!slider->has_alt) {
        g_warning("Slider has no alterative value set up.  Trying to add it now.");
        gwy_param_table_slider_add_alt(partable, id);
    }
    if (unitstr)
        gwy_param_table_set_unitstr(partable, id, unitstr);
    gwy_assign_string(&slider->alt_unitstr, vf->units);
    slider->alt_q_to_gui = raw_q/vf->magnitude;
    slider->alt_offset_to_gui = raw_offset/vf->magnitude;
    gwy_set_member_object(partable, NULL, GWY_TYPE_DATA_LINE, &slider->calibration, NULL);
    if (control->widget)
        slider_reconfigure_alt(control, partable);
}

static void
entry_activated(GtkEntry *gtkentry, GwyParamTable *partable)
{
    entry_input(gtkentry, partable, FALSE);
}

static void
entry_changed(GtkEntry *gtkentry, GwyParamTable *partable)
{
    entry_input(gtkentry, partable, TRUE);
}

static void
entry_input(GtkEntry *gtkentry, GwyParamTable *partable, gboolean is_instant_edit)
{
    guint i = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(gtkentry), param_control_quark));
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    GwyParams *params = priv->params;
    const GwyParamDefItem *def;
    GwyParamControlEntry *entry;
    gint id = control->id;
    gboolean changed = FALSE;
    gchar *text, *end;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_ENTRY);
    entry = (GwyParamControlEntry*)control->impl;
    if (entry->is_numeric) {
        if (!find_def_common(partable, id, NULL, &def))
            return;
        /* Strip whitespace to make the if(!*end) check more friendly. */
        text = g_strstrip(gtk_editable_get_chars(GTK_EDITABLE(gtkentry), 0, -1));
        if (entry->is_int) {
            gint value = strtol(text, &end, 10);
            if (!*end) {
                value = _gwy_param_def_rectify_int(def, value);
                changed = gwy_params_set_int(params, id, value);
            }
        }
        else {
            gdouble value = (entry->parse ? entry->parse(text, &end, entry->transform_data) : g_strtod(text, &end));
            if (!*end) {
                value = _gwy_param_def_rectify_double(def, value);
                changed = gwy_params_set_double(params, id, value);
            }
        }
        g_free(text);
    }
    else {
        changed = gwy_params_set_string(params, id, gtk_entry_get_text(gtkentry));
    }

    /* Do not fight the user on instant edits. It is a bit odd. Any behaviour mixing instant updates with rectifiers
     * is odd. This one seems to be the least bad one. */
    if (!is_instant_edit)
        entry_output(partable, control);
    if (changed)
        gwy_param_table_param_changed(partable, id);
}

static void
entry_output(GwyParamTable *partable, GwyParamControl *control)
{
    GwyParamControlEntry *entry = (GwyParamControlEntry*)control->impl;
    GwyParams *params = partable->priv->params;
    gint id = control->id;

    if (entry->is_numeric) {
        if (entry->is_int)
            g_string_printf(entry->str, "%d", gwy_params_get_int(params, id));
        else if (entry->format) {
            /* This is a bit silly because with entry_parse_double_vf() we asing entry->str to itself.  However,
             * GString can handle that. */
            g_string_assign(entry->str, entry->format(gwy_params_get_double(params, id), entry->transform_data));
        }
        else
            g_string_printf(entry->str, "%.6g", gwy_params_get_double(params, id));
    }
    else {
        const gchar *s = gwy_params_get_string(params, id);
        g_string_assign(entry->str, s ? s : "");
    }

    gtk_entry_set_text(GTK_ENTRY(control->widget), entry->str->str);
}

static gdouble
entry_parse_double_vf(const gchar *text, gchar **end, gpointer user_data)
{
    GwyParamControlEntry *entry = (GwyParamControlEntry*)user_data;
    GwySIValueFormat *vf = entry->vf;

    return g_strtod(text, end)*vf->magnitude;
}

static const gchar*
entry_format_double_vf(gdouble value, gpointer user_data)
{
    GwyParamControlEntry *entry = (GwyParamControlEntry*)user_data;

    return format_double_vf_common(value, entry->vf, entry->str);
}

static void
string_entry_set_value(GwyParamControl *control, GwyParamTable *partable,
                       const gchar *value, gboolean use_default_instead)
{
    GwyParams *params = partable->priv->params;
    GwyParamControlEntry *entry;
    gboolean changed = FALSE;
    gint id;

    g_return_if_fail(control && control->type == GWY_PARAM_CONTROL_ENTRY);
    id = control->id;
    entry = (GwyParamControlEntry*)control->impl;
    g_return_if_fail(!entry->is_numeric);
    if (use_default_instead)
        changed = gwy_params_reset(params, id);
    else
        changed = gwy_params_set_string(params, id, value);

    /* Update the controls.  Make sure the signal is emitted at the very end. */
    if (changed && control->widget) {
        _gwy_param_table_in_update(partable, TRUE);
        entry_output(partable, control);
        _gwy_param_table_in_update(partable, FALSE);
    }
    if (changed)
        gwy_param_table_param_changed(partable, id);
}

static void
int_entry_set_value(GwyParamControl *control, GwyParamTable *partable,
                    gint value, gboolean use_default_instead)
{
    GwyParams *params = partable->priv->params;
    GwyParamControlEntry *entry;
    gboolean changed = FALSE;
    gint id;

    g_return_if_fail(control && control->type == GWY_PARAM_CONTROL_ENTRY);
    id = control->id;
    entry = (GwyParamControlEntry*)control->impl;
    g_return_if_fail(entry->is_numeric && entry->is_int);
    if (use_default_instead)
        changed = gwy_params_reset(params, id);
    else
        changed = gwy_params_set_int(params, id, value);

    /* Update the controls.  Make sure the signal is emitted at the very end. */
    if (changed && control->widget) {
        _gwy_param_table_in_update(partable, TRUE);
        entry_output(partable, control);
        _gwy_param_table_in_update(partable, FALSE);
    }
    if (changed)
        gwy_param_table_param_changed(partable, id);
}

static void
double_entry_set_value(GwyParamControl *control, GwyParamTable *partable,
                       gdouble value, gboolean use_default_instead)
{
    GwyParams *params = partable->priv->params;
    GwyParamControlEntry *entry;
    gboolean changed = FALSE;
    gint id;

    g_return_if_fail(control && control->type == GWY_PARAM_CONTROL_ENTRY);
    id = control->id;
    entry = (GwyParamControlEntry*)control->impl;
    g_return_if_fail(entry->is_numeric && !entry->is_int);
    if (use_default_instead)
        changed = gwy_params_reset(params, id);
    else
        changed = gwy_params_set_double(params, id, value);

    /* Update the controls.  Make sure the signal is emitted at the very end. */
    if (changed && control->widget) {
        _gwy_param_table_in_update(partable, TRUE);
        entry_output(partable, control);
        _gwy_param_table_in_update(partable, FALSE);
    }
    if (changed)
        gwy_param_table_param_changed(partable, id);
}

static void
range_input(GtkEntry *gtkentry, GwyParamTable *partable)
{
    guint i = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(gtkentry), param_control_quark));
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    GwyParams *params = priv->params;
    const GwyParamDefItem *def;
    GwyParamControlRangeFrom *range;
    gint id = control->id;
    gboolean changed = FALSE;
    gdouble value;
    gchar *text, *end;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_RANGE_FROM
                     || control->type == GWY_PARAM_CONTROL_RANGE_TO);
    if (!find_def_common(partable, id, NULL, &def))
        return;

    range = range_get_impl(control, partable);
    /* Strip whitespace to make the if(!*end) check more friendly. */
    text = g_strstrip(gtk_editable_get_chars(GTK_EDITABLE(gtkentry), 0, -1));
    value = g_strtod(text, &end);
    if (!*end) {
        if (range->vf)
            value *= range->vf->magnitude;
        value = _gwy_param_def_rectify_double(def, value);
        changed = gwy_params_set_double(params, id, value);
    }

    range_output(partable, control);
    if (changed)
        gwy_param_table_param_changed(partable, id);

    g_free(text);
}

static void
range_output(GwyParamTable *partable, GwyParamControl *control)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParams *params = priv->params;
    GwyParamControlRangeFrom *range;
    gint id = control->id;

    range = range_get_impl(control, partable);
    if (range->vf)
        g_string_assign(range->str, format_double_vf_common(gwy_params_get_double(params, id), range->vf, range->str));
    else
        g_string_printf(range->str, "%.6g", gwy_params_get_double(params, id));

    gtk_entry_set_text(GTK_ENTRY(control->widget), range->str->str);
}

static void
range_set_value(GwyParamControl *control, GwyParamTable *partable,
                gdouble value, gboolean use_default_instead)
{
    GwyParams *params = partable->priv->params;
    GwyParamControlRangeFrom *range;
    gboolean changed = FALSE;
    gint id;

    g_return_if_fail(control);
    id = control->id;
    range = range_get_impl(control, partable);
    g_return_if_fail(range);

    if (use_default_instead)
        changed = gwy_params_reset(params, id);
    else
        changed = gwy_params_set_double(params, id, value);

    /* Update the controls.  Make sure the signal is emitted at the very end. */
    if (changed && control->widget) {
        _gwy_param_table_in_update(partable, TRUE);
        range_output(partable, control);
        _gwy_param_table_in_update(partable, FALSE);
    }
    if (changed)
        gwy_param_table_param_changed(partable, id);
}

static void
range_activated(GtkEntry *gtkentry, GwyParamTable *partable)
{
    range_input(gtkentry, partable);
}

static GwyParamControlRangeFrom*
range_get_impl(GwyParamControl *control, GwyParamTable *partable)
{
    GwyParamTablePrivate *priv = partable->priv;
    gint ifrom;

    if (control->type == GWY_PARAM_CONTROL_RANGE_FROM)
        return (GwyParamControlRangeFrom*)control->impl;

    g_return_val_if_fail(control->type == GWY_PARAM_CONTROL_RANGE_TO, NULL);
    ifrom = find_control_for_aux(partable, &priv->range, control->id);
    g_return_val_if_fail(ifrom >= 0, NULL);

    return (GwyParamControlRangeFrom*)(priv->controls[ifrom].impl);
}

static void
update_control_unit_label(GwyParamControl *control, GwyParamTable *partable)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControlType type = control->type;
    gint row = control->row;

    g_return_if_fail(control_can_integrate_unitstr(type));
    if (type == GWY_PARAM_CONTROL_BUTTON)
        control = find_button_box_end(partable, control, FALSE);
    if (control->unitstr) {
        if (!control->unitlabel) {
            control->unitlabel = gtk_label_new(NULL);
            gtk_misc_set_alignment(GTK_MISC(control->unitlabel), 0.0, 0.5);
            gtk_table_attach(GTK_TABLE(priv->widget), control->unitlabel, 2, 3, row, row+1, GTK_FILL, 0, 0, 0);
            gtk_widget_show(control->unitlabel);
        }
        gtk_label_set_markup(GTK_LABEL(control->unitlabel), control->unitstr);
    }
    else {
        if (control->unitlabel) {
            gtk_widget_destroy(control->unitlabel);
            control->unitlabel = NULL;
        }
    }
}

static void
update_rgba_colour(GtkColorSelection *colorsel, GwyRGBA *rgba)
{
    GdkColor gdkcolor;

    gtk_color_selection_get_current_color(colorsel, &gdkcolor);
    if (!gtk_color_selection_get_has_opacity_control(colorsel))
        gwy_rgba_from_gdk_color(rgba, &gdkcolor);
    else
        gwy_rgba_from_gdk_color_and_alpha(rgba, &gdkcolor, gtk_color_selection_get_current_alpha(colorsel));
}

static void
color_run_selector(GwyColorButton *colour_button, GwyParamTable *partable)
{
    guint i = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(colour_button), param_control_quark));
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    const GwyParamDefItem *def;
    gint id = control->id;
    GtkColorSelection *colorsel;
    GtkWindow *window;
    GtkWidget *colour_dialog;
    GdkColor gdkcolor;
    GwyRGBA rgba;

    if (!find_def_common(partable, id, NULL, &def))
        return;

    window = get_parent_window(partable, FALSE);
    g_return_if_fail(window);

    rgba = gwy_params_get_color(priv->params, control->id);
    gwy_rgba_to_gdk_color(&rgba, &gdkcolor);

    colour_dialog = gtk_color_selection_dialog_new(_("Select Color"));
    colorsel = GTK_COLOR_SELECTION(GTK_COLOR_SELECTION_DIALOG(colour_dialog)->colorsel);
    gtk_color_selection_set_current_color(colorsel, &gdkcolor);
    gtk_color_selection_set_has_palette(colorsel, FALSE);
    gtk_color_selection_set_has_opacity_control(colorsel, def->def.c.has_alpha);
    if (def->def.c.has_alpha)
        gtk_color_selection_set_current_alpha(colorsel, gwy_rgba_to_gdk_alpha(&rgba));
    g_signal_connect(colorsel, "color-changed", G_CALLBACK(update_rgba_colour), &rgba);

    gtk_window_set_transient_for(GTK_WINDOW(colour_dialog), window);
    if (gwy_run_subdialog(GTK_DIALOG(colour_dialog)) == GTK_RESPONSE_OK) {
        gwy_color_button_set_color(colour_button, &rgba);
        color_set_value(control, partable, rgba, FALSE);
    }
}

static void
mask_color_run_selector(GwyColorButton *color_button, GwyParamTable *partable)
{
    guint i = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(color_button), param_control_quark));
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;
    GwyParamControlMaskColor *maskcolor = (GwyParamControlMaskColor*)control->impl;
    GtkWindow *window;
    const gchar *key;
    GwyRGBA color;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_MASK_COLOR);
    g_return_if_fail(priv->widget);
    window = get_parent_window(partable, FALSE);
    key = g_quark_to_string(gwy_app_get_mask_key_for_id(maskcolor->preview_i));
    gwy_mask_color_selector_run(NULL, window, color_button, maskcolor->preview_data, key);
    gwy_rgba_get_from_container(&color, maskcolor->preview_data, key);
    gwy_color_button_set_color(GWY_COLOR_BUTTON(color_button), &color);
    gwy_params_set_color(priv->params, control->id, color);
    /* XXX: In fact, we have no idea.  Just run it… */
    gwy_param_table_param_changed(partable, control->id);
}

static void
message_update_type(GwyParamControl *control)
{
    /* XXX: This is completely broken from theming standpoint.  But at least each module is not doing something like
     * this on its own and we can try to make it better here. */
    static const GdkColor gdkcolor_error = { 0, 51118, 0, 0 };
    static const GdkColor gdkcolor_warning = { 0, 45056, 20480, 0 };

    GwyParamControlMessage *message = (GwyParamControlMessage*)control->impl;
    const GdkColor *color = NULL;

    g_return_if_fail(control->type == GWY_PARAM_CONTROL_MESSAGE);
    if (message->type == GTK_MESSAGE_ERROR)
        color = &gdkcolor_error;
    else if (message->type == GTK_MESSAGE_WARNING)
        color = &gdkcolor_warning;

    gtk_widget_modify_fg(control->label, GTK_STATE_NORMAL, color);
    gtk_widget_modify_fg(control->label, GTK_STATE_ACTIVE, color);
    gtk_widget_modify_fg(control->label, GTK_STATE_PRELIGHT, color);
    gtk_widget_modify_fg(control->label, GTK_STATE_INSENSITIVE, color);
    if (control->widget) {
        gtk_widget_modify_fg(control->widget, GTK_STATE_NORMAL, color);
        gtk_widget_modify_fg(control->widget, GTK_STATE_ACTIVE, color);
        gtk_widget_modify_fg(control->widget, GTK_STATE_PRELIGHT, color);
        gtk_widget_modify_fg(control->widget, GTK_STATE_INSENSITIVE, color);
    }
}

static gboolean
filter_graph_model(GwyContainer *data, gint id, gpointer user_data)
{
    GwyGraphModel *targetgmodel, *gmodel = (GwyGraphModel*)user_data;

    if (!gmodel)
        return FALSE;
    if (!gwy_container_gis_object(data, gwy_app_get_graph_key_for_id(id), (GObject**)&targetgmodel))
        return FALSE;
    if (!gwy_graph_model_units_are_compatible(gmodel, targetgmodel))
        return FALSE;
    return TRUE;
}

/* Find the first (FALSE) or last (TRUE) button in a row with reseponse buttons. */
static GwyParamControl*
find_button_box_end(GwyParamTable *partable, GwyParamControl *control, gboolean forward)
{
    GwyParamControlButton *button;
    gint sibling_id;

    while (TRUE) {
        g_return_val_if_fail(control->type == GWY_PARAM_CONTROL_BUTTON, control);
        button = (GwyParamControlButton*)control->impl;
        sibling_id = (forward ? button->sibling_id_next : button->sibling_id_prev);
        if (sibling_id < 0)
            return control;
        control = find_first_control(partable, sibling_id);
    }
    return NULL;
}

static gboolean
button_box_has_any_sensitive(GwyParamTable *partable, GwyParamControl *control)
{
    GwyParamControlButton *button;
    gint sibling_id;

    control = find_button_box_end(partable, control, FALSE);
    while (TRUE) {
        if (control->sensitive)
            return TRUE;
        button = (GwyParamControlButton*)control->impl;
        sibling_id = button->sibling_id_next;
        if (sibling_id < 0)
            return FALSE;
        control = find_first_control(partable, sibling_id);
    }
    return TRUE;
}

static void
widget_dispose(GtkWidget *widget, GwyParamTable *partable)
{
    GwyParamTablePrivate *priv = partable->priv;
    guint k, n = priv->ncontrols;

    gwy_debug("destroy %p (%d)", widget, G_OBJECT(widget)->ref_count);
    g_assert((gpointer)widget == (gpointer)priv->widget);
    priv->widget = NULL;

    for (k = 0; k < n; k++) {
        GwyParamControl *control = priv->controls + k;
        GwyParamControlType type = control->type;

        control->widget = control->label = control->unitlabel = NULL;
        if (type == GWY_PARAM_CONTROL_SLIDER) {
            GwyParamControlSlider *slider = (GwyParamControlSlider*)control->impl;
            slider->spin = slider->alt_spin = NULL;
            slider->alt_unitlabel = NULL;
            slider->adj = NULL;
        }
        else if (type == GWY_PARAM_CONTROL_ENABLER) {
            GwyParamControlEnabler *enabler = (GwyParamControlEnabler*)control->impl;
            enabler->container_child = NULL;
        }
        else if (type == GWY_PARAM_CONTROL_UNIT_CHOOSER) {
            GwyParamControlUnitChooser *unit = (GwyParamControlUnitChooser*)control->impl;
            unit->change_button = NULL;
        }
        else if (type == GWY_PARAM_CONTROL_HOLD_SELECTION) {
            GwyParamControlHoldSelection *holdsel = (GwyParamControlHoldSelection*)control->impl;
            holdsel->overwrite = holdsel->clear = NULL;
        }
        else if (type == GWY_PARAM_CONTROL_RANDOM_SEED) {
            GwyParamControlRandomSeed *randomseed = (GwyParamControlRandomSeed*)control->impl;
            randomseed->adj = NULL;
            randomseed->new_button = NULL;
        }
        else if (type == GWY_PARAM_CONTROL_RESULTS) {
            GwyParamControlResults *resultlist = (GwyParamControlResults*)control->impl;
            g_free(resultlist->value_labels);
            resultlist->value_labels = NULL;
        }
        else if (type == GWY_PARAM_CONTROL_REPORT) {
            GwyParamControlReport *report = (GwyParamControlReport*)control->impl;
            report->copy_sid = report->save_sid = 0;
        }
        else if (type == GWY_PARAM_CONTROL_BUTTON) {
            GwyParamControlButton *button = (GwyParamControlButton*)control->impl;
            button->sizegroup = NULL;
        }
        else if (type == GWY_PARAM_CONTROL_COLOR) {
            GwyParamControlColor *colour = (GwyParamControlColor*)control->impl;
            colour->sizegroup = NULL;
        }
        /* XXX: If the caller did not actually place all the radio buttons to the table we are screwed. */
    }

    g_object_unref(widget);
}

static void
make_control_common(GwyParamTable *partable, guint i)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyParamControl *control = priv->controls + i;

    g_assert(!control->widget);
    add_separator_as_needed(partable, i);
    expand_table(partable);
}

static void
add_separator_as_needed(GwyParamTable *partable, guint i)
{
    GwyParamControl *controls = partable->priv->controls;

    if (i == 0)
        return;
    if (controls[i].type == GWY_PARAM_CONTROL_HEADER || controls[i-1].type == GWY_PARAM_CONTROL_SEPARATOR) {
        gint row = controls[i].row;
        gwy_debug("adding separator between %d and %d", row-1, row);
        gtk_table_set_row_spacing(GTK_TABLE(partable->priv->widget), row-1, GWY_PARAM_TABLE_BIGROWSEP);
    }
}

static void
attach_hbox_row(GwyParamTable *partable, gint row,
                GwyParamControl *control, const gchar *desc)
{
    GtkWidget *hbox, *alignment;
    gboolean has_mnemonic = FALSE;

    hbox = gwy_hbox_new(0);
    gtk_table_attach(GTK_TABLE(partable->priv->widget), hbox, 0, 2, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    if (control->label_text)
        desc = control->label_text;

    if (desc) {
        if (control->type == GWY_PARAM_CONTROL_INFO) {
            GwyParamControlInfo *info = (GwyParamControlInfo*)control->impl;

            control->label = gtk_label_new(modify_label(partable, desc, info->add_punctuation, FALSE));
        }
        else if (control->type == GWY_PARAM_CONTROL_REPORT || control->type == GWY_PARAM_CONTROL_BUTTON)
            control->label = gtk_label_new(modify_label(partable, desc, TRUE, TRUE));
        else {
            control->label = gtk_label_new_with_mnemonic(modify_label(partable, desc, TRUE, FALSE));
            has_mnemonic = TRUE;
        }

        alignment = add_right_padding(control->label, GWY_PARAM_TABLE_COLSEP);
        gtk_box_pack_start(GTK_BOX(hbox), alignment, FALSE, FALSE, 0);

        gtk_box_pack_end(GTK_BOX(hbox), control->widget, FALSE, FALSE, 0);
        if (has_mnemonic)
            gtk_label_set_mnemonic_widget(GTK_LABEL(control->label), control->widget);
    }
    else {
        gtk_box_pack_end(GTK_BOX(hbox), control->widget, TRUE, TRUE, 0);
    }
    gtk_widget_show_all(hbox);
}

static GtkWidget*
add_left_padding(GtkWidget *widget, gint left_pad)
{
    GtkWidget *alignment;

    alignment = gtk_alignment_new(1.0, 0.5, 0.0, 0.0);
    gtk_alignment_set_padding(GTK_ALIGNMENT(alignment), 0, 0, left_pad, 0);
    gtk_container_add(GTK_CONTAINER(alignment), widget);

    return alignment;
}

static GtkWidget*
add_right_padding(GtkWidget *widget, gint right_pad)
{
    GtkWidget *alignment;

    alignment = gtk_alignment_new(0.0, 0.5, 0.0, 0.0);
    gtk_alignment_set_padding(GTK_ALIGNMENT(alignment), 0, 0, 0, right_pad);
    gtk_container_add(GTK_CONTAINER(alignment), widget);

    return alignment;
}

static void
expand_table(GwyParamTable *partable)
{
    GwyParamTablePrivate *priv = partable->priv;
    gint table_nrows, table_ncols;

    g_object_get(priv->widget, "n-rows", &table_nrows, NULL);
    if (table_nrows < priv->nrows)
        g_object_set(priv->widget, "n-rows", priv->nrows, NULL);

    g_object_get(priv->widget, "n-columns", &table_ncols, NULL);
    if (table_ncols < priv->ncols)
        g_object_set(priv->widget, "n-columns", priv->ncols, NULL);
}

static gint
find_control_for_aux(GwyParamTable *partable, const GwyParamControlAssocTable *assoc_table, gint id)
{
    GwyParamTablePrivate *priv = partable->priv;
    const GwyParamControlAssoc *assoc = assoc_table->assoc;
    const GwyParamControl *controls = priv->controls;
    guint i, n = assoc_table->n;

    for (i = 0; i < n; i++) {
        if (assoc[i].aux_id == id) {
            id = assoc[i].other_id;
            n = priv->ncontrols;
            for (i = 0; i < n; i++) {
                if (controls[i].id == id)
                    return i;
            }
            g_return_val_if_reached(-1);
        }
    }
    return -1;
}

static gint
find_aux_for_control(GwyParamTable *partable, const GwyParamControlAssocTable *assoc_table, gint id)
{
    GwyParamTablePrivate *priv = partable->priv;
    const GwyParamControlAssoc *assoc = assoc_table->assoc;
    const GwyParamControl *controls = priv->controls;
    guint i, n = assoc_table->n;

    for (i = 0; i < n; i++) {
        gwy_debug("[%u] id_other=%d id_aux=%d", i, assoc[i].other_id, assoc[i].aux_id);
        if (assoc[i].other_id == id) {
            id = assoc[i].aux_id;
            n = priv->ncontrols;
            for (i = 0; i < n; i++) {
                if (controls[i].id == id)
                    return i;
            }
            g_return_val_if_reached(-1);
        }
    }
    return -1;
}

static GwyToggleListInfo*
find_toggles_info(GwyParamTable *partable, gint id)
{
    GwyParamTablePrivate *priv = partable->priv;
    GwyToggleListInfo *toggles_info = priv->toggles_info;
    guint i, n = priv->ntoggles;

    for (i = 0; i < n; i++) {
        if (toggles_info[i].id == id)
            return toggles_info + i;
    }
    return NULL;
}

static GwyEnum*
modify_enum_labels(GwyParamTable *partable,
                   const GwyEnum *values, guint nvalues,
                   gboolean end_with_colon, gboolean remove_underline)
{
    GwyEnum *newvalues;
    guint i;

    /* Check if we have to bother creating a modified table. */
    for (i = 0; i < nvalues; i++) {
        const gchar *s = values[i].name;
        guint len = strlen(s);
        gboolean has_colon = (len && s[len-1] == ':');

        if (!end_with_colon ^ !has_colon)
            break;
        if (remove_underline && memchr(s, '_', len))
            break;
    }
    if (i == nvalues)
        return NULL;

    /* We do. */
    newvalues = g_new(GwyEnum, nvalues+1);
    for (i = 0; i < nvalues; i++) {
        newvalues[i].value = values[i].value;
        newvalues[i].name = g_strdup(modify_label(partable, values[i].name, end_with_colon, remove_underline));
    }
    newvalues[nvalues].value = -1;
    newvalues[nvalues].name = NULL;

    return newvalues;
}

/*
 * Usual usage
 * TRUE, FALSE = mnemonic widget in a table row
 * TRUE, TRUE  = radio list header
 * FALSE, TRUE = combo box item (enums are representable both as radios and combos)
 */
static const gchar*
modify_label(GwyParamTable *partable, const gchar *label,
             gboolean end_with_colon, gboolean remove_underline)
{
    GString *str = partable->priv->str;
    guint n;

    if (!label) {
        g_string_truncate(str, 0);
        return str->str;
    }

    g_string_assign(str, label);
    /* The string is actually UTF-8 but this is OK as we only manipulate 7bit bytes. */
    if (remove_underline && strchr(label, '_')) {
        gchar *s = str->str;
        guint i, j;

        n = str->len;
        for (i = j = 0; i < n; i++) {
            if (s[i] != '_')
                s[j++] = s[i];
            else if (s[i+1] == '_') {
                s[j++] = '_';
                i++;
            }
        }
        g_string_truncate(str, j);
    }
    while ((n = str->len) && (str->str[n-1] == ':' || g_ascii_isspace(str->str[n-1])))
        g_string_truncate(str, n-1);
    if (end_with_colon && str->len)
        g_string_append(str, colonext);

    return str->str;
}

static GtkWindow*
get_parent_window(GwyParamTable *partable, gboolean must_be_dialog)
{
    GwyParamTablePrivate *priv = partable->priv;
    GtkWidget *toplevel;

    if (priv->parent_dialog)
        return GTK_WINDOW(priv->parent_dialog);

    toplevel = gtk_widget_get_toplevel(priv->widget);
    if (toplevel && gtk_widget_is_toplevel(toplevel)) {
        if ((must_be_dialog && GTK_IS_DIALOG(toplevel)) || GTK_IS_WINDOW(toplevel))
            return GTK_WINDOW(toplevel);
    }

    return NULL;
}

static const gchar*
format_double_vf_common(gdouble value, GwySIValueFormat *vf, GString *str)
{
    gdouble v, ldiff;
    guint i;

    v = value/vf->magnitude;
    if (v == 0.0)
        return "0";

    /* If we have a value too far from vf->magnitude fall back to scientific number notation. */
    ldiff = log10(fabs(v));
    if (ldiff > 3.5 || ldiff < 2.0) {
        g_string_printf(str, "%.6g", v);
        return str->str;
    }

    format_numerical_value(str, v, vf->precision);
    /* Remove trailing zeros, but only after a decimal separator.  Keep anything with 'e' in it intact. */
    if (strchr(str->str, 'e'))
        return str->str;

    for (i = str->len; i; i--) {
        if (!g_ascii_isdigit(str->str[i-1]))
            break;
    }
    if (i) {
        i = str->len;
        while (i && str->str[i-1] == '0')
            i--;
        while (i && !g_ascii_isdigit(str->str[i-1]))
            i--;
        if (i)
            g_string_truncate(str, i);
    }
    return str->str;
}

/* NB: value is the value to display, after any transformations. */
static void
format_numerical_value(GString *str, gdouble value, gint digits)
{
    const gchar *s;
    guint i;

    g_string_printf(str, "%0.*f", digits, value);
    s = str->str;
    if (s[0] != '-')
        return;
    /* Weed out negative zero. */
    i = 1;
    while (s[i] == '0')
        i++;
    if (s[i] == '.' || s[i] == ',')
        i++;
    while (s[i] == '0')
        i++;
    if (i == str->len)
        g_string_erase(str, 0, 1);
}

static guint32
bit_mask_for_enum_value(const GwyParamDefItem *def, gint value)
{
    const GwyEnum *values;
    guint i, nvalues, bits = 0;

    g_return_val_if_fail(def->type == GWY_PARAM_ENUM, 0);
    nvalues = def->def.e.nvalues;
    values = def->def.e.table;
    for (i = 0; i < nvalues; i++) {
        if (values[i].value == value)
            bits |= (1u << i);
    }
    return bits;
}

static void
compute_calibration_steps(GwyDataLine *calibration,
                          gdouble *mean_step, gdouble *min_step)
{
    gint res, i;
    const gdouble *d;
    gdouble h_min = G_MAXDOUBLE, min, max;

    *mean_step = *min_step = 1.0;
    g_return_if_fail(GWY_IS_DATA_LINE(calibration));
    res = gwy_data_line_get_res(calibration);
    if (res == 1) {
        g_warning("Calibration has only one point.");
        return;
    }
    gwy_data_line_get_min_max(calibration, &min, &max);
    if (max == min) {
        g_warning("Calibration is completely flat.");
        return;
    }

    d = gwy_data_line_get_data(calibration);
    for (i = 1; i < res; i++) {
        if (d[i] <= d[i-1])
            g_warning("Calibration is not a monotonically increasing curve.");
        else
            h_min = fmin(h_min, d[i] - d[i-1]);
    }

    *mean_step = (max - min)/res;
    *min_step = fmax(h_min, 1e-9*(max - min));
}

static gdouble
calibration_transform_to_gui(GwyDataLine *calibration, gdouble v)
{
    const gdouble *cdata = gwy_data_line_get_data(calibration);
    gint n = gwy_data_line_get_res(calibration);
    gint i = (gint)floor(v);

    if (i < 0)
        return cdata[0];
    if (i >= n-1)
        return cdata[n-1];

    v -= i;
    return cdata[i]*(1.0 - v) + cdata[i+1]*v;
}

static gdouble
calibration_transform_from_gui(GwyDataLine *calibration, gdouble v)
{
    const gdouble *cdata = gwy_data_line_get_data(calibration);
    gint i = 0, j = gwy_data_line_get_res(calibration) - 1, k;
    gdouble t;

    gwy_debug("%g vs. [%g, %g]", v, cdata[i], cdata[j]);
    if (v <= cdata[i])
        return i;
    if (v >= cdata[j])
        return j;

    /* Now cdata[i] < v < cdata[j]. Use bisection. */
    do {
        k = (i + j)/2;
        if (v < cdata[k])
            j = k;
        else
            i = k;
    } while (j > i+1);

    g_assert(j == i+1);

    if (cdata[j] <= cdata[i])
        return i;

    t = (v - cdata[i])/(cdata[j] - cdata[i]);
    return i + GWY_CLAMP(t, 0.0, 1.0);
}

static const GwyEnum*
guess_standard_stock_ids(const GwyParamDefItem *def)
{
    static const GwyEnum merge_type_stock_ids[] = {
        { GWY_STOCK_MASK_INTERSECT, GWY_MERGE_INTERSECTION },
        { GWY_STOCK_MASK_ADD,       GWY_MERGE_UNION        },
        { NULL,                     -1                     },
    };
    GType type;

    g_return_val_if_fail(def->type == GWY_PARAM_ENUM, NULL);
    type = def->def.e.gtype;
    if (type == GWY_TYPE_MERGE_TYPE)
        return merge_type_stock_ids;
    return NULL;
}

static inline gboolean
control_has_no_parameter(GwyParamControlType type)
{
    return (type == GWY_PARAM_CONTROL_HEADER
            || type == GWY_PARAM_CONTROL_RADIO_HEADER
            || type == GWY_PARAM_CONTROL_SEPARATOR
            || type == GWY_PARAM_CONTROL_BUTTON
            || type == GWY_PARAM_CONTROL_RESULTS
            || type == GWY_PARAM_CONTROL_MESSAGE
            || type == GWY_PARAM_CONTROL_FOREIGN
            || type == GWY_PARAM_CONTROL_INFO);
}

static inline gboolean
control_is_some_kind_of_radio(GwyParamControlType type)
{
    return (type == GWY_PARAM_CONTROL_RADIO
            || type == GWY_PARAM_CONTROL_RADIO_HEADER
            || type == GWY_PARAM_CONTROL_RADIO_ITEM
            || type == GWY_PARAM_CONTROL_RADIO_ROW
            || type == GWY_PARAM_CONTROL_RADIO_BUTTONS);
}

static inline gboolean
control_is_some_kind_of_data_id(GwyParamControlType type)
{
    return (type == GWY_PARAM_CONTROL_IMAGE_ID_COMBO
            || type == GWY_PARAM_CONTROL_GRAPH_ID_COMBO
            || type == GWY_PARAM_CONTROL_VOLUME_ID_COMBO
            || type == GWY_PARAM_CONTROL_XYZ_ID_COMBO
            || type == GWY_PARAM_CONTROL_CURVE_MAP_ID_COMBO);
}

static inline gboolean
control_is_some_kind_of_curve_no(GwyParamControlType type)
{
    return (type == GWY_PARAM_CONTROL_GRAPH_CURVE_COMBO
            || type == GWY_PARAM_CONTROL_LAWN_CURVE_COMBO
            || type == GWY_PARAM_CONTROL_LAWN_SEGMENT_COMBO);
}

static inline gboolean
control_can_integrate_enabler(GwyParamControlType type)
{
    return (type == GWY_PARAM_CONTROL_SLIDER
            || type == GWY_PARAM_CONTROL_ENTRY
            || type == GWY_PARAM_CONTROL_COMBO
            || type == GWY_PARAM_CONTROL_IMAGE_ID_COMBO
            || type == GWY_PARAM_CONTROL_GRAPH_ID_COMBO
            || type == GWY_PARAM_CONTROL_VOLUME_ID_COMBO
            || type == GWY_PARAM_CONTROL_XYZ_ID_COMBO
            || type == GWY_PARAM_CONTROL_CURVE_MAP_ID_COMBO
            || type == GWY_PARAM_CONTROL_GRAPH_CURVE_COMBO
            || type == GWY_PARAM_CONTROL_LAWN_CURVE_COMBO
            || type == GWY_PARAM_CONTROL_LAWN_SEGMENT_COMBO
            || type == GWY_PARAM_CONTROL_RADIO_ROW
            || type == GWY_PARAM_CONTROL_RADIO_BUTTONS);
}

static inline gboolean
control_can_integrate_unitstr(GwyParamControlType type)
{
    /* Some of these are silly, but the user may want to put some label there which is not really a unit. */
    return (type == GWY_PARAM_CONTROL_HEADER
            || type == GWY_PARAM_CONTROL_CHECKBOX
            || type == GWY_PARAM_CONTROL_SLIDER
            || type == GWY_PARAM_CONTROL_ENTRY
            || type == GWY_PARAM_CONTROL_RANGE_FROM
            || type == GWY_PARAM_CONTROL_COMBO
            || type == GWY_PARAM_CONTROL_IMAGE_ID_COMBO
            || type == GWY_PARAM_CONTROL_GRAPH_ID_COMBO
            || type == GWY_PARAM_CONTROL_VOLUME_ID_COMBO
            || type == GWY_PARAM_CONTROL_XYZ_ID_COMBO
            || type == GWY_PARAM_CONTROL_CURVE_MAP_ID_COMBO
            || type == GWY_PARAM_CONTROL_GRAPH_CURVE_COMBO
            || type == GWY_PARAM_CONTROL_LAWN_CURVE_COMBO
            || type == GWY_PARAM_CONTROL_LAWN_SEGMENT_COMBO
            || type == GWY_PARAM_CONTROL_RADIO_ROW
            || type == GWY_PARAM_CONTROL_RADIO_BUTTONS
            || type == GWY_PARAM_CONTROL_COLOR
            || type == GWY_PARAM_CONTROL_MASK_COLOR
            || type == GWY_PARAM_CONTROL_BUTTON
            || type == GWY_PARAM_CONTROL_INFO);
}

static inline gboolean
control_has_hbox(GwyParamControlType type)
{
    /* REPORT has a hbox, but we need always special-case it. */
    return (type == GWY_PARAM_CONTROL_COMBO
            || type == GWY_PARAM_CONTROL_IMAGE_ID_COMBO
            || type == GWY_PARAM_CONTROL_GRAPH_ID_COMBO
            || type == GWY_PARAM_CONTROL_VOLUME_ID_COMBO
            || type == GWY_PARAM_CONTROL_XYZ_ID_COMBO
            || type == GWY_PARAM_CONTROL_CURVE_MAP_ID_COMBO
            || type == GWY_PARAM_CONTROL_GRAPH_CURVE_COMBO
            || type == GWY_PARAM_CONTROL_LAWN_CURVE_COMBO
            || type == GWY_PARAM_CONTROL_LAWN_SEGMENT_COMBO
            || type == GWY_PARAM_CONTROL_UNIT_CHOOSER
            || type == GWY_PARAM_CONTROL_HOLD_SELECTION
            || type == GWY_PARAM_CONTROL_RADIO_ROW
            || type == GWY_PARAM_CONTROL_RADIO_BUTTONS
            || type == GWY_PARAM_CONTROL_COLOR
            || type == GWY_PARAM_CONTROL_MASK_COLOR
            || type == GWY_PARAM_CONTROL_BUTTON
            || type == GWY_PARAM_CONTROL_INFO);
}

static gdouble
multiply_by_constant(gdouble v, gpointer user_data)
{
    gdouble q = *(gdouble*)user_data;
    return v*q;
}

static gdouble
divide_by_constant(gdouble v, gpointer user_data)
{
    gdouble q = *(gdouble*)user_data;
    return v/q;
}

/**
 * SECTION:param-table
 * @title: GwyParamTable
 * @short_description: User interface for parameter sets
 *
 * #GwyParamTable manages the user interface for module parameters.  It is not a #GtkWidget itself, but it can create
 * widgets.  Most of what is needed for the parameter controls is already contained in #GwyParamDef.  Therefore, the
 * corresponding user interface can be as simple as:
 * |[
 * GwyParamTable *table = gwy_param_table_new(params);
 *
 * gwy_param_table_append_header(table, -1, _("Detection"));
 * gwy_param_table_append_combo(table, PARAM_METHOD);
 * gwy_param_table_append_slider(table, PARAM_DEGREE);
 * gwy_param_table_append_checkbox(table, PARAM_ADAPTIVE);
 *
 * gwy_param_table_append_header(table, -1, _("Output"));
 * gwy_param_table_append_checkbox(table, PARAM_EXTRACT_BACKGROUND);
 * gwy_param_table_append_checkbox(table, PARAM_CREATE_MASK);
 *
 * gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);
 * gwy_dialog_add_param_table(dialog, table);
 * ]|
 * After creating the table, you append the various elements in the order in which they should appear in the user
 * interface.  Function gwy_param_table_widget() then creates the widget for the entire parameter table, which can be
 * packed to the dialog.  Note that the created widget type is unspecified.  It may be #GtkTable, #GtkGrid or
 * something else.
 *
 * Finally, #GwyDialog should be notified that a new table was added using gwy_dialog_add_param_table().  It is
 * possible to add tables #GwyDialog does not know about, although this is seldom useful.
 *
 * #GwyParamTable updates parameter values in #GwyParams when the user changes something.  Therefore, when there is no
 * preview and no relations between parameters to manage, no further setup is necessary.  If you need to respond to
 * parameter changes, connect to the #GwyParamTable::param-changed signal.
 *
 * If you change parameters in response to the user changing other parameters, always use the #GwyParamTable functions
 * such as gwy_param_table_set_boolean() or gwy_param_table_set_enum().  This ensures the controls are updated
 * accordingly.  Functions such as gwy_params_set_boolean() or gwy_params_set_enum() would only change the value but
 * not the user interface.
 **/

/**
 * GwyCreateWidgetFunc:
 * @user_data: The data passed when the function was set up.
 *
 * Type of function constructing a widget.
 *
 * Returns: A new widget.
 *
 * Since: 2.59
 **/

/**
 * GwyCreateTextFunc:
 * @user_data: The data passed when the function was set up.
 *
 * Type of function constructing a string.
 *
 * Returns: A new string.
 *
 * Since: 2.59
 **/

/**
 * GwyParamTable:
 *
 * Object managing user interface controls for parameters.
 *
 * The #GwyParamTable struct contains no public fields.
 *
 * Since: 2.59
 **/

/**
 * GwyParamTableClass:
 *
 * Class of user interface parameter controls manager.
 *
 * Since: 2.59
 **/

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
