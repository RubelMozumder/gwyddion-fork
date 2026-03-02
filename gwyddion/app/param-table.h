/*
 *  $Id: param-table.h 25446 2023-06-15 16:25:08Z yeti-dn $
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

#ifndef __GWY_PARAM_TABLE_H__
#define __GWY_PARAM_TABLE_H__

#include <gtk/gtk.h>
#include <libgwyddion/gwyresults.h>
#include <libprocess/dataline.h>
#include <libprocess/datafield.h>
#include <libprocess/brick.h>
#include <libprocess/lawn.h>
#include <libgwydgets/gwydgets.h>
#include <app/datachooser.h>
#include <app/gwyresultsexport.h>
#include <app/params.h>

G_BEGIN_DECLS

typedef GtkWidget* (*GwyCreateWidgetFunc)(gpointer user_data);
typedef gchar*     (*GwyCreateTextFunc)  (gpointer user_data);
typedef gboolean   (*GwyEnumFilterFunc)  (const GwyEnum *enumval,
                                          gpointer user_data);

#define GWY_TYPE_PARAM_TABLE            (gwy_param_table_get_type())
#define GWY_PARAM_TABLE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_PARAM_TABLE, GwyParamTable))
#define GWY_PARAM_TABLE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GWY_TYPE_PARAM_TABLE, GwyParamTableClass))
#define GWY_IS_PARAM_TABLE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_PARAM_TABLE))
#define GWY_IS_PARAM_TABLE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GWY_TYPE_PARAM_TABLE))
#define GWY_PARAM_TABLE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_PARAM_TABLE, GwyParamTableClass))

typedef struct _GwyParamTable      GwyParamTable;
typedef struct _GwyParamTableClass GwyParamTableClass;

struct _GwyParamTable {
    GInitiallyUnowned parent;
    struct _GwyParamTablePrivate *priv;
};

struct _GwyParamTableClass {
    GInitiallyUnownedClass parent;
};

GType          gwy_param_table_get_type                 (void)                              G_GNUC_CONST;
GwyParamTable* gwy_param_table_new                      (GwyParams *params);
GwyParams*     gwy_param_table_params                   (GwyParamTable *partable);
GtkWidget*     gwy_param_table_widget                   (GwyParamTable *partable);
void           gwy_param_table_reset                    (GwyParamTable *partable);
void           gwy_param_table_set_from_params          (GwyParamTable *partable,
                                                         GwyParams *params);
gboolean       gwy_param_table_exists                   (GwyParamTable *partable,
                                                         gint id);
void           gwy_param_table_param_changed            (GwyParamTable *partable,
                                                         gint id);
void           gwy_param_table_set_no_reset             (GwyParamTable *partable,
                                                         gint id,
                                                         gboolean setting);
void           gwy_param_table_set_no_resetv            (GwyParamTable *partable,
                                                         const gint *ids,
                                                         gint n,
                                                         gboolean setting);
void           gwy_param_table_set_sensitive            (GwyParamTable *partable,
                                                         gint id,
                                                         gboolean sensitive);
void           gwy_param_table_set_label                (GwyParamTable *partable,
                                                         gint id,
                                                         const gchar *text);
void           gwy_param_table_set_boolean              (GwyParamTable *partable,
                                                         gint id,
                                                         gboolean value);
void           gwy_param_table_set_int                  (GwyParamTable *partable,
                                                         gint id,
                                                         gint value);
void           gwy_param_table_set_double               (GwyParamTable *partable,
                                                         gint id,
                                                         gdouble value);
void           gwy_param_table_set_enum                 (GwyParamTable *partable,
                                                         gint id,
                                                         gint value);
void           gwy_param_table_set_flags                (GwyParamTable *partable,
                                                         gint id,
                                                         guint value);
void           gwy_param_table_set_string               (GwyParamTable *partable,
                                                         gint id,
                                                         const gchar *value);
void           gwy_param_table_set_data_id              (GwyParamTable *partable,
                                                         gint id,
                                                         GwyAppDataId value);
void           gwy_param_table_set_curve                (GwyParamTable *partable,
                                                         gint id,
                                                         gint value);
void           gwy_param_table_append_header            (GwyParamTable *partable,
                                                         gint id,
                                                         const gchar *label);
void           gwy_param_table_append_separator         (GwyParamTable *partable);
void           gwy_param_table_set_unitstr              (GwyParamTable *partable,
                                                         gint id,
                                                         const gchar *unitstr);
void           gwy_param_table_append_checkbox          (GwyParamTable *partable,
                                                         gint id);
void           gwy_param_table_add_enabler              (GwyParamTable *partable,
                                                         gint id,
                                                         gint other_id);
void           gwy_param_table_append_combo             (GwyParamTable *partable,
                                                         gint id);
void           gwy_param_table_combo_set_filter         (GwyParamTable *partable,
                                                         gint id,
                                                         GwyEnumFilterFunc filter,
                                                         gpointer user_data,
                                                         GDestroyNotify destroy);
void           gwy_param_table_combo_refilter           (GwyParamTable *partable,
                                                         gint id);
void           gwy_param_table_append_radio             (GwyParamTable *partable,
                                                         gint id);
void           gwy_param_table_append_radio_header      (GwyParamTable *partable,
                                                         gint id);
void           gwy_param_table_append_radio_item        (GwyParamTable *partable,
                                                         gint id,
                                                         gint value);
void           gwy_param_table_append_radio_row         (GwyParamTable *partable,
                                                         gint id);
void           gwy_param_table_append_radio_buttons     (GwyParamTable *partable,
                                                         gint id,
                                                         const GwyEnum *stock_ids);
void           gwy_param_table_radio_set_sensitive      (GwyParamTable *partable,
                                                         gint id,
                                                         gint value,
                                                         gboolean sensitive);
void           gwy_param_table_append_checkboxes        (GwyParamTable *partable,
                                                         gint id);
void           gwy_param_table_checkboxes_set_sensitive (GwyParamTable *partable,
                                                         gint id,
                                                         guint flags,
                                                         gboolean sensitive);
void           gwy_param_table_append_graph_id          (GwyParamTable *partable,
                                                         gint id);
void           gwy_param_table_append_image_id          (GwyParamTable *partable,
                                                         gint id);
void           gwy_param_table_append_volume_id         (GwyParamTable *partable,
                                                         gint id);
void           gwy_param_table_append_xyz_id            (GwyParamTable *partable,
                                                         gint id);
void           gwy_param_table_append_curve_map_id      (GwyParamTable *partable,
                                                         gint id);
void           gwy_param_table_append_target_graph      (GwyParamTable *partable,
                                                         gint id,
                                                         GwyGraphModel *gmodel);
void           gwy_param_table_append_graph_curve       (GwyParamTable *partable,
                                                         gint id,
                                                         GwyGraphModel *gmodel);
void           gwy_param_table_graph_curve_set_model    (GwyParamTable *partable,
                                                         gint id,
                                                         GwyGraphModel *gmodel);
void           gwy_param_table_append_lawn_curve        (GwyParamTable *partable,
                                                         gint id,
                                                         GwyLawn *lawn);
void           gwy_param_table_append_lawn_segment      (GwyParamTable *partable,
                                                         gint id,
                                                         GwyLawn *lawn);
void           gwy_param_table_data_id_set_filter       (GwyParamTable *partable,
                                                         gint id,
                                                         GwyDataChooserFilterFunc filter,
                                                         gpointer user_data,
                                                         GDestroyNotify destroy);
void           gwy_param_table_data_id_refilter         (GwyParamTable *partable,
                                                         gint id);
void           gwy_param_table_append_slider            (GwyParamTable *partable,
                                                         gint id);
void           gwy_param_table_slider_set_mapping       (GwyParamTable *partable,
                                                         gint id,
                                                         GwyScaleMappingType mapping);
void           gwy_param_table_slider_set_steps         (GwyParamTable *partable,
                                                         gint id,
                                                         gdouble step,
                                                         gdouble page);
void           gwy_param_table_slider_set_digits        (GwyParamTable *partable,
                                                         gint id,
                                                         gint digits);
void           gwy_param_table_slider_set_snapping      (GwyParamTable *partable,
                                                         gint id,
                                                         gboolean setting);
void           gwy_param_table_slider_restrict_range    (GwyParamTable *partable,
                                                         gint id,
                                                         gdouble minimum,
                                                         gdouble maximum);
void           gwy_param_table_slider_set_transform     (GwyParamTable *partable,
                                                         gint id,
                                                         GwyRealFunc value_to_gui,
                                                         GwyRealFunc gui_to_value,
                                                         gpointer user_data,
                                                         GDestroyNotify destroy);
void           gwy_param_table_slider_set_factor        (GwyParamTable *partable,
                                                         gint id,
                                                         gdouble q_value_to_gui);
void           gwy_param_table_slider_add_alt           (GwyParamTable *partable,
                                                         gint id);
void           gwy_param_table_alt_set_field_pixel_x    (GwyParamTable *partable,
                                                         gint id,
                                                         GwyDataField *field);
void           gwy_param_table_alt_set_field_pixel_y    (GwyParamTable *partable,
                                                         gint id,
                                                         GwyDataField *field);
void           gwy_param_table_alt_set_brick_pixel_x    (GwyParamTable *partable,
                                                         gint id,
                                                         GwyBrick *brick);
void           gwy_param_table_alt_set_brick_pixel_y    (GwyParamTable *partable,
                                                         gint id,
                                                         GwyBrick *brick);
void           gwy_param_table_alt_set_brick_pixel_z    (GwyParamTable *partable,
                                                         gint id,
                                                         GwyBrick *brick);
void           gwy_param_table_alt_set_lawn_pixel_x     (GwyParamTable *partable,
                                                         gint id,
                                                         GwyLawn *lawn);
void           gwy_param_table_alt_set_lawn_pixel_y     (GwyParamTable *partable,
                                                         gint id,
                                                         GwyLawn *lawn);
void           gwy_param_table_alt_set_field_frac_z     (GwyParamTable *partable,
                                                         gint id,
                                                         GwyDataField *field);
void           gwy_param_table_alt_set_linear           (GwyParamTable *partable,
                                                         gint id,
                                                         gdouble q_to_gui,
                                                         gdouble off_to_gui,
                                                         const gchar *unitstr);
void           gwy_param_table_alt_set_calibration      (GwyParamTable *partable,
                                                         gint id,
                                                         GwyDataLine *calibration);
void           gwy_param_table_append_entry             (GwyParamTable *partable,
                                                         gint id);
void           gwy_param_table_entry_set_width          (GwyParamTable *partable,
                                                         gint id,
                                                         gint width_chars);
void           gwy_param_table_entry_set_value_format   (GwyParamTable *partable,
                                                         gint id,
                                                         GwySIValueFormat *vf);
void           gwy_param_table_entry_set_instant_changes(GwyParamTable *partable,
                                                         gint id,
                                                         gboolean setting);
void           gwy_param_table_append_range             (GwyParamTable *partable,
                                                         gint id_from,
                                                         gint id_to);
void           gwy_param_table_append_unit_chooser      (GwyParamTable *partable,
                                                         gint id);
void           gwy_param_table_append_color             (GwyParamTable *partable,
                                                         gint id);
void           gwy_param_table_color_add_preset         (GwyParamTable *partable,
                                                         gint id,
                                                         GwyRGBA rgba,
                                                         const gchar *name);
void           gwy_param_table_append_mask_color        (GwyParamTable *partable,
                                                         gint id,
                                                         GwyContainer *preview_data,
                                                         gint preview_i,
                                                         GwyContainer *data,
                                                         gint i);
void           gwy_param_table_append_button            (GwyParamTable *partable,
                                                         gint id,
                                                         gint sibling_id,
                                                         gint response,
                                                         const gchar *text);
void           gwy_param_table_append_info              (GwyParamTable *partable,
                                                         gint id,
                                                         const gchar *label);
void           gwy_param_table_append_results           (GwyParamTable *partable,
                                                         gint id,
                                                         GwyResults *results,
                                                         const gchar *result_id,
                                                         ...)                               G_GNUC_NULL_TERMINATED;
void           gwy_param_table_append_resultsv          (GwyParamTable *partable,
                                                         gint id,
                                                         GwyResults *results,
                                                         const gchar **result_ids,
                                                         gint nids);
void           gwy_param_table_results_fill             (GwyParamTable *partable,
                                                         gint id);
void           gwy_param_table_results_clear            (GwyParamTable *partable,
                                                         gint id);
void           gwy_param_table_append_report            (GwyParamTable *partable,
                                                         gint id);
void           gwy_param_table_report_set_results       (GwyParamTable *partable,
                                                         gint id,
                                                         GwyResults *results);
void           gwy_param_table_report_set_formatter     (GwyParamTable *partable,
                                                         gint id,
                                                         GwyCreateTextFunc format_report,
                                                         gpointer user_data,
                                                         GDestroyNotify destroy);
void           gwy_param_table_append_hold_selection    (GwyParamTable *partable,
                                                         gint id);
void           gwy_param_table_append_seed              (GwyParamTable *partable,
                                                         gint id);
void           gwy_param_table_append_message           (GwyParamTable *partable,
                                                         gint id,
                                                         const gchar *text);
void           gwy_param_table_info_set_valuestr        (GwyParamTable *partable,
                                                         gint id,
                                                         const gchar *text);
void           gwy_param_table_info_set_add_punctuation (GwyParamTable *partable,
                                                         gint id,
                                                         gboolean setting);
void           gwy_param_table_message_set_type         (GwyParamTable *partable,
                                                         gint id,
                                                         GtkMessageType type);
void           gwy_param_table_append_foreign           (GwyParamTable *partable,
                                                         gint id,
                                                         GwyCreateWidgetFunc create_widget,
                                                         gpointer user_data,
                                                         GDestroyNotify destroy);

G_END_DECLS

#endif

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
