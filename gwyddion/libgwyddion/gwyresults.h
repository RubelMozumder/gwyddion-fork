/*
 *  $Id: gwyresults.h 24606 2022-02-16 14:45:59Z yeti-dn $
 *  Copyright (C) 2017-2022 David Necas (Yeti).
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

#ifndef __GWY_RESULTS_H__
#define __GWY_RESULTS_H__

#include <glib.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwysiunit.h>

G_BEGIN_DECLS

typedef enum {
    GWY_RESULTS_VALUE_FLOAT  = 1,
    GWY_RESULTS_VALUE_STRING = 2,
    GWY_RESULTS_VALUE_INT    = 3,
    GWY_RESULTS_VALUE_YESNO  = 4,
} GwyResultsValueType;

typedef enum {
    GWY_RESULTS_REPORT_COLON   = 0,
    GWY_RESULTS_REPORT_TABSEP  = 1,
    GWY_RESULTS_REPORT_CSV     = 2,
    GWY_RESULTS_REPORT_MACHINE = (1 << 8),
} GwyResultsReportType;

#define GWY_TYPE_RESULTS                  (gwy_results_get_type())
#define GWY_RESULTS(obj)                  (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_RESULTS, GwyResults))
#define GWY_RESULTS_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST((klass), GWY_TYPE_RESULTS, GwyResultsClass))
#define GWY_IS_RESULTS(obj)               (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_RESULTS))
#define GWY_IS_RESULTS_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE((klass), GWY_TYPE_RESULTS))
#define GWY_RESULTS_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_RESULTS, GwyResultsClass))

typedef struct _GwyResults GwyResults;
typedef struct _GwyResultsClass GwyResultsClass;

struct _GwyResults {
    GObject parent_instance;
    struct _GwyResultsPrivate *priv;
};

struct _GwyResultsClass {
    GObjectClass parent_class;

    /*< private >*/
    void (*reserved1)(void);
    void (*reserved2)(void);
};

GType        gwy_results_get_type               (void)                             G_GNUC_CONST;
GwyResults*  gwy_results_new                    (void)                             G_GNUC_MALLOC;
GwyResults*  gwy_results_copy                   (GwyResults *results)              G_GNUC_MALLOC;
void         gwy_results_add_header             (GwyResults *results,
                                                 const gchar *label);
void         gwy_results_add_separator          (GwyResults *results);
void         gwy_results_add_value              (GwyResults *results,
                                                 const gchar *id,
                                                 const gchar *label,
                                                 ...)                              G_GNUC_NULL_TERMINATED;
void         gwy_results_add_format             (GwyResults *results,
                                                 const gchar *id,
                                                 const gchar *label,
                                                 gboolean translate_format,
                                                 const gchar *format,
                                                 ...)                              G_GNUC_NULL_TERMINATED;
void         gwy_results_add_value_str          (GwyResults *results,
                                                 const gchar *id,
                                                 const gchar *label);
void         gwy_results_add_value_x            (GwyResults *results,
                                                 const gchar *id,
                                                 const gchar *label);
void         gwy_results_add_value_y            (GwyResults *results,
                                                 const gchar *id,
                                                 const gchar *label);
void         gwy_results_add_value_z            (GwyResults *results,
                                                 const gchar *id,
                                                 const gchar *label);
void         gwy_results_add_value_plain        (GwyResults *results,
                                                 const gchar *id,
                                                 const gchar *label);
void         gwy_results_add_value_int          (GwyResults *results,
                                                 const gchar *id,
                                                 const gchar *label);
void         gwy_results_add_value_angle        (GwyResults *results,
                                                 const gchar *id,
                                                 const gchar *label);
void         gwy_results_add_value_percents     (GwyResults *results,
                                                 const gchar *id,
                                                 const gchar *label);
void         gwy_results_add_value_yesno        (GwyResults *results,
                                                 const gchar *id,
                                                 const gchar *label);
void         gwy_results_add_covariance_matrix  (GwyResults *results,
                                                 const gchar *id,
                                                 const gchar *label,
                                                 ...);
void         gwy_results_add_covariance_matrixv (GwyResults *results,
                                                 const gchar *id,
                                                 const gchar *label,
                                                 guint n,
                                                 const gchar **symbols);
void         gwy_results_bind_formats           (GwyResults *results,
                                                 const gchar *id,
                                                 ...)                              G_GNUC_NULL_TERMINATED;
void         gwy_results_unbind_formats         (GwyResults *results,
                                                 const gchar *id,
                                                 ...)                              G_GNUC_NULL_TERMINATED;
void         gwy_results_set_unit               (GwyResults *results,
                                                 const gchar *name,
                                                 GwySIUnit *unit);
void         gwy_results_set_unit_str           (GwyResults *results,
                                                 const gchar *name,
                                                 const gchar *unitstr);
void         gwy_results_fill_values            (GwyResults *results,
                                                 ...)                              G_GNUC_NULL_TERMINATED;
void         gwy_results_fill_values_with_errors(GwyResults *results,
                                                 ...)                              G_GNUC_NULL_TERMINATED;
void         gwy_results_fill_format            (GwyResults *results,
                                                 const gchar *id,
                                                 ...)                              G_GNUC_NULL_TERMINATED;
void         gwy_results_fill_covariance_matrix (GwyResults *results,
                                                 const gchar *id,
                                                 const gboolean *fixed_params,
                                                 const gdouble *covar_matrix);
void         gwy_results_set_na                 (GwyResults *results,
                                                 const gchar *id,
                                                 ...)                              G_GNUC_NULL_TERMINATED;
void         gwy_results_set_nav                (GwyResults *results,
                                                 guint n,
                                                 const gchar **id);
gchar*       gwy_results_create_report          (GwyResults *results,
                                                 GwyResultsReportType report_type) G_GNUC_MALLOC;
const gchar* gwy_results_get_label              (GwyResults *results,
                                                 const gchar *id);
const gchar* gwy_results_get_symbol             (GwyResults *results,
                                                 const gchar *id);
const gchar* gwy_results_get_label_with_symbol  (GwyResults *results,
                                                 const gchar *id);
const gchar* gwy_results_get_value              (GwyResults *results,
                                                 const gchar *id);
const gchar* gwy_results_get_error              (GwyResults *results,
                                                 const gchar *id);
const gchar* gwy_results_get_value_with_error   (GwyResults *results,
                                                 const gchar *id);
const gchar* gwy_results_get_units              (GwyResults *results,
                                                 const gchar *id);
const gchar* gwy_results_get_full               (GwyResults *results,
                                                 const gchar *id);
gboolean     gwy_results_value_is_na            (GwyResults *results,
                                                 const gchar *id);
void         gwy_format_result_table_row        (GString *str,
                                                 GwyResultsReportType report_type,
                                                 guint n,
                                                 ...);
void         gwy_format_result_table_rowv       (GString *str,
                                                 GwyResultsReportType report_type,
                                                 guint n,
                                                 const gdouble *values);
void         gwy_format_result_table_strings    (GString *str,
                                                 GwyResultsReportType report_type,
                                                 guint n,
                                                 ...);
void         gwy_format_result_table_stringsv   (GString *str,
                                                 GwyResultsReportType report_type,
                                                 guint n,
                                                 const gchar **values);
void         gwy_format_result_table_mixed      (GString *str,
                                                 GwyResultsReportType report_type,
                                                 const gchar *fields,
                                                 ...);

G_END_DECLS

#endif /* __GWY_RESULTS_H__ */

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
