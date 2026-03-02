/*
 *  $Id: wrap_calls.h 25260 2023-03-06 11:25:17Z yeti-dn $
 *  Copyright (C) 2008 Jan Horak, 2015-2023 David Necas (Yeti)
 *  E-mail: xhorak@gmail.com, yeti@gwyddion.net.
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
 *
 *  Description: This file contains custom fuctions for automatically generated wrapping by using pygwy-codegen.
 */

#ifndef __PYGWY_WRAP_CALLS_H__
#define __PYGWY_WRAP_CALLS_H__

#include <libgwyddion/gwyddion.h>
#include <libprocess/gwyprocess.h>
#include <libdraw/gwydraw.h>
#include <libgwydgets/gwydgets.h>
#include <libgwymodule/gwymodule.h>
#include <app/gwyapp.h>

typedef gboolean GwyArrayFuncStatus;

typedef GArray *GwyDoubleArrayOutArg;
typedef GArray GwyDoubleArray;
typedef GArray *GwyIntArrayOutArg;
typedef GArray GwyIntArray;
typedef GArray GwyStringArray;        /* Must be freed. */
typedef GArray GwyConstStringArray;   /* Must not be touched. */
typedef GArray GwyDataFieldArray;

gdouble              gwy_math_median_pygwy                                 (GwyDoubleArray *array);
gdouble              gwy_math_kth_rank_pygwy                               (GwyDoubleArray *array,
                                                                            guint k);
gdouble              gwy_math_trimmed_mean_pygwy                           (GwyDoubleArray *array,
                                                                            guint nlowest,
                                                                            guint nhighest);
GwyArrayFuncStatus   gwy_math_curvature_pygwy                              (GwyDoubleArray *coeffs,
                                                                            gint *dimen,
                                                                            gdouble *kappa1,
                                                                            gdouble *kappa2,
                                                                            gdouble *phi1,
                                                                            gdouble *phi2,
                                                                            gdouble *xc,
                                                                            gdouble *yc,
                                                                            gdouble *zc);
GwyArrayFuncStatus   gwy_math_refine_maximum_pygwy                         (GwyDoubleArray *z,
                                                                            gdouble *x,
                                                                            gdouble *y,
                                                                            gboolean *refined);
GwyArrayFuncStatus   gwy_math_refine_maximum_2d_pygwy                      (GwyDoubleArray *z,
                                                                            gdouble *x,
                                                                            gdouble *y,
                                                                            gboolean *refined);
GwyArrayFuncStatus   gwy_math_refine_maximum_1d_pygwy                      (GwyDoubleArray *y,
                                                                            gdouble *x,
                                                                            gboolean *refined);
GwyArrayFuncStatus   gwy_math_is_in_polygon_pygwy                          (gdouble x,
                                                                            gdouble y,
                                                                            GwyDoubleArray *poly,
                                                                            gboolean *is_inside);
GwyArrayFuncStatus   gwy_math_find_nearest_line_pygwy                      (gdouble x,
                                                                            gdouble y,
                                                                            GwyDoubleArray *coords,
                                                                            GwyDoubleArray *metric,
                                                                            gint *idx,
                                                                            gdouble *d2min);
GwyArrayFuncStatus   gwy_math_find_nearest_point_pygwy                     (gdouble x,
                                                                            gdouble y,
                                                                            GwyDoubleArray *coords,
                                                                            GwyDoubleArray *metric,
                                                                            gint *idx,
                                                                            gdouble *d2min);
GwyArrayFuncStatus   gwy_math_fit_polynom_pygwy                            (GwyDoubleArray *xdata,
                                                                            GwyDoubleArray *ydata,
                                                                            gint n,
                                                                            GwyDoubleArrayOutArg coeffs);
GwyDoubleArray*      gwy_fft_window_pygwy                                  (GwyDoubleArray *data,
                                                                            GwyWindowingType windowing);
GwyDoubleArray*      gwy_interpolation_resolve_coeffs_1d_pygwy             (GwyDoubleArray *data,
                                                                            GwyInterpolationType interpolation);
GwyDoubleArray*      gwy_interpolation_resolve_coeffs_2d_pygwy             (gint width,
                                                                            gint height,
                                                                            gint rowstride,
                                                                            GwyDoubleArray *data,
                                                                            GwyInterpolationType interpolation);
GwyArrayFuncStatus   gwy_interpolation_get_dval_of_equidists_pygwy         (gdouble x,
                                                                            GwyDoubleArray *data,
                                                                            GwyInterpolationType interpolation,
                                                                            gdouble *result);
GwyArrayFuncStatus   gwy_interpolation_interpolate_1d_pygwy                (gdouble x,
                                                                            GwyDoubleArray *coeff,
                                                                            GwyInterpolationType interpolation,
                                                                            gdouble *result);
GwyArrayFuncStatus   gwy_interpolation_interpolate_2d_pygwy                (gdouble x,
                                                                            gdouble y,
                                                                            gint rowstride,
                                                                            GwyDoubleArray *coeff,
                                                                            GwyInterpolationType interpolation,
                                                                            gdouble *result);
GwyDoubleArray*      gwy_interpolation_resample_block_1d_pygwy             (GwyDoubleArray *data,
                                                                            gint newlength,
                                                                            GwyInterpolationType interpolation);
GwyDoubleArray*      gwy_interpolation_resample_block_2d_pygwy             (GwyDoubleArray *data,
                                                                            gint width,
                                                                            gint height,
                                                                            gint rowstride,
                                                                            gint newwidth,
                                                                            gint newheight,
                                                                            gint newrowstride,
                                                                            GwyInterpolationType interpolation);
GwyDoubleArray*      gwy_interpolation_shift_block_1d_pygwy                (GwyDoubleArray *data,
                                                                            gdouble offset,
                                                                            GwyInterpolationType interpolation,
                                                                            GwyExteriorType exterior,
                                                                            gdouble fill_value);
GwyDoubleArray*      gwy_selection_get_data_pygwy                          (GwySelection *selection);
GwyDoubleArray*      gwy_selection_get_object_pygwy                        (GwySelection *selection,
                                                                            gint i);
GwyArrayFuncStatus   gwy_selection_set_data_pygwy                          (GwySelection *selection,
                                                                            GwyDoubleArray *data);
GwyArrayFuncStatus   gwy_selection_set_object_pygwy                        (GwySelection *selection,
                                                                            gint i,
                                                                            GwyDoubleArray *data);
GwyDoubleArray*      gwy_data_line_get_data_pygwy                          (GwyDataLine *data_line);
GwyDoubleArray*      gwy_data_field_get_data_pygwy                         (GwyDataField *data_field);
GwyDoubleArray*      gwy_brick_get_data_pygwy                              (GwyBrick *brick);
GwyDoubleArray*      gwy_lawn_get_curve_data_pygwy                         (GwyLawn *lawn,
                                                                            gint col,
                                                                            gint row,
                                                                            gint n);
GwyDoubleArray*      gwy_lawn_get_curves_data_pygwy                        (GwyLawn *lawn,
                                                                            gint col,
                                                                            gint row);
GwyArrayFuncStatus   gwy_data_line_set_data_pygwy                          (GwyDataLine *data_line,
                                                                            GwyDoubleArray *data);
GwyArrayFuncStatus   gwy_data_field_set_data_pygwy                         (GwyDataField *data_field,
                                                                            GwyDoubleArray *data);
GwyArrayFuncStatus   gwy_brick_set_data_pygwy                              (GwyBrick *brick,
                                                                            GwyDoubleArray *data);
GwyArrayFuncStatus   gwy_lawn_set_curve_data_pygwy                         (GwyLawn *lawn,
                                                                            gint col,
                                                                            gint row,
                                                                            gint n,
                                                                            GwyDoubleArray *data);
GwyDoubleArray*      gwy_data_field_fit_polynom_pygwy                      (GwyDataField *data_field,
                                                                            gint col_degree,
                                                                            gint row_degree);
GwyDoubleArray*      gwy_data_field_area_fit_polynom_pygwy                 (GwyDataField *data_field,
                                                                            gint col,
                                                                            gint row,
                                                                            gint width,
                                                                            gint height,
                                                                            gint col_degree,
                                                                            gint row_degree);
GwyArrayFuncStatus   gwy_data_field_subtract_polynom_pygwy                 (GwyDataField *data_field,
                                                                            gint col_degree,
                                                                            gint row_degree,
                                                                            GwyDoubleArray *coeffs);
GwyArrayFuncStatus   gwy_data_field_area_subtract_polynom_pygwy            (GwyDataField *data_field,
                                                                            gint col,
                                                                            gint row,
                                                                            gint width,
                                                                            gint height,
                                                                            gint col_degree,
                                                                            gint row_degree,
                                                                            GwyDoubleArray *coeffs);
GwyDoubleArray*      gwy_data_field_fit_legendre_pygwy                     (GwyDataField *data_field,
                                                                            gint col_degree,
                                                                            gint row_degree);
GwyDoubleArray*      gwy_data_field_area_fit_legendre_pygwy                (GwyDataField *data_field,
                                                                            gint col,
                                                                            gint row,
                                                                            gint width,
                                                                            gint height,
                                                                            gint col_degree,
                                                                            gint row_degree);
GwyArrayFuncStatus   gwy_data_field_subtract_legendre_pygwy                (GwyDataField *data_field,
                                                                            gint col_degree,
                                                                            gint row_degree,
                                                                            GwyDoubleArray *coeffs);
GwyArrayFuncStatus   gwy_data_field_area_subtract_legendre_pygwy           (GwyDataField *data_field,
                                                                            gint col,
                                                                            gint row,
                                                                            gint width,
                                                                            gint height,
                                                                            gint col_degree,
                                                                            gint row_degree,
                                                                            GwyDoubleArray *coeffs);
GwyDoubleArray*      gwy_data_field_fit_poly_max_pygwy                     (GwyDataField *data_field,
                                                                            gint max_degree);
GwyDoubleArray*      gwy_data_field_area_fit_poly_max_pygwy                (GwyDataField *data_field,
                                                                            gint col,
                                                                            gint row,
                                                                            gint width,
                                                                            gint height,
                                                                            gint max_degree);
GwyArrayFuncStatus   gwy_data_field_subtract_poly_max_pygwy                (GwyDataField *data_field,
                                                                            gint max_degree,
                                                                            GwyDoubleArray *coeffs);
GwyArrayFuncStatus   gwy_data_field_area_subtract_poly_max_pygwy           (GwyDataField *data_field,
                                                                            gint col,
                                                                            gint row,
                                                                            gint width,
                                                                            gint height,
                                                                            gint max_degree,
                                                                            GwyDoubleArray *coeffs);
GwyArrayFuncStatus   gwy_data_field_fit_poly_pygwy                         (GwyDataField *data_field,
                                                                            GwyDataField *mask_field,
                                                                            GwyIntArray *term_powers,
                                                                            gboolean exclude,
                                                                            GwyDoubleArrayOutArg coeffs);
GwyArrayFuncStatus   gwy_data_field_area_fit_poly_pygwy                    (GwyDataField *data_field,
                                                                            GwyDataField *mask_field,
                                                                            gint col,
                                                                            gint row,
                                                                            gint width,
                                                                            gint height,
                                                                            GwyIntArray *term_powers,
                                                                            gboolean exclude,
                                                                            GwyDoubleArrayOutArg coeffs);
GwyArrayFuncStatus   gwy_data_field_subtract_poly_pygwy                    (GwyDataField *data_field,
                                                                            GwyIntArray *term_powers,
                                                                            GwyDoubleArray *coeffs);
GwyArrayFuncStatus   gwy_data_field_area_subtract_poly_pygwy               (GwyDataField *data_field,
                                                                            gint col,
                                                                            gint row,
                                                                            gint width,
                                                                            gint height,
                                                                            GwyIntArray *term_powers,
                                                                            GwyDoubleArray *coeffs);
GwyDataFieldArray*   gwy_data_field_area_fit_local_planes_pygwy            (GwyDataField *data_field,
                                                                            gint size,
                                                                            gint col,
                                                                            gint row,
                                                                            gint width,
                                                                            gint height,
                                                                            GwyIntArray *types);
GwyDataFieldArray*   gwy_data_field_fit_local_planes_pygwy                 (GwyDataField *data_field,
                                                                            gint size,
                                                                            GwyIntArray *types);
GwyDoubleArray*      gwy_data_field_elliptic_area_extract_pygwy            (GwyDataField *data_field,
                                                                            gint col,
                                                                            gint row,
                                                                            gint width,
                                                                            gint height);
GwyArrayFuncStatus   gwy_data_field_elliptic_area_unextract_pygwy          (GwyDataField *data_field,
                                                                            gint col,
                                                                            gint row,
                                                                            gint width,
                                                                            gint height,
                                                                            GwyDoubleArray *data);
GwyDoubleArray*      gwy_data_field_circular_area_extract_pygwy            (GwyDataField *data_field,
                                                                            gint col,
                                                                            gint row,
                                                                            gdouble radius);
GwyArrayFuncStatus   gwy_data_field_circular_area_unextract_pygwy          (GwyDataField *data_field,
                                                                            gint col,
                                                                            gint row,
                                                                            gdouble radius,
                                                                            GwyDoubleArray *data);
GwyDoubleArray*      gwy_data_field_circular_area_extract_with_pos_pygwy   (GwyDataField *data_field,
                                                                            gint col,
                                                                            gint row,
                                                                            gdouble radius,
                                                                            GwyIntArrayOutArg xpos,
                                                                            GwyIntArrayOutArg ypos);
gboolean             gwy_data_field_local_maximum_pygwy                    (GwyDataField *dfield,
                                                                            gdouble x,
                                                                            gdouble y,
                                                                            gint ax,
                                                                            gint ay,
                                                                            gdouble *x_out,
                                                                            gdouble *y_out);
GwyArrayFuncStatus   gwy_data_field_affine_pygwy                           (GwyDataField *data_field,
                                                                            GwyDataField *dest,
                                                                            GwyDoubleArray *affine,
                                                                            GwyInterpolationType interp,
                                                                            GwyExteriorType exterior,
                                                                            gdouble fill_value);
GwyArrayFuncStatus   gwy_data_field_affine_prepare_pygwy                   (GwyDataField *source,
                                                                            GwyDataField *dest,
                                                                            GwyDoubleArray *a1a2,
                                                                            GwyDoubleArray *a1a2_corr,
                                                                            GwyAffineScalingType scaling,
                                                                            gboolean prevent_rotation,
                                                                            gdouble oversampling,
                                                                            GwyDoubleArrayOutArg a1a2_corr_out,
                                                                            GwyDoubleArrayOutArg invtrans);
gint                 gwy_data_field_waterpour_pygwy                        (GwyDataField *data_field,
                                                                            GwyDataField *result,
                                                                            GwyIntArrayOutArg grains);
GwyArrayFuncStatus   gwy_data_field_measure_lattice_acf_pygwy              (GwyDataField *acf2d,
                                                                            GwyDoubleArray *a1a2,
                                                                            GwyDoubleArrayOutArg a1a2_out,
                                                                            gboolean *succeeded);
GwyArrayFuncStatus   gwy_data_field_measure_lattice_psdf_pygwy             (GwyDataField *psdf2d,
                                                                            GwyDoubleArray *a1a2,
                                                                            GwyDoubleArrayOutArg a1a2_out,
                                                                            gboolean *succeeded);
void                 gwy_data_field_get_local_maxima_list_pygwy            (GwyDataField *dfield,
                                                                            GwyDoubleArrayOutArg xdata,
                                                                            GwyDoubleArrayOutArg ydata,
                                                                            GwyDoubleArrayOutArg zdata,
                                                                            gint ndata,
                                                                            gint skip,
                                                                            gdouble threshold,
                                                                            gboolean subpixel);
GwyPlaneSymmetry     gwy_data_field_unrotate_find_corrections_pygwy        (GwyDataLine *derdist,
                                                                            GwyDoubleArrayOutArg correction);
GwyDoubleArray*      gwy_data_field_get_profile_mask_pygwy                 (GwyDataField *dfield,
                                                                            GwyDataField *mask,
                                                                            GwyMaskingType masking,
                                                                            gdouble xfrom,
                                                                            gdouble yfrom,
                                                                            gdouble xto,
                                                                            gdouble yto,
                                                                            gint res,
                                                                            gint thickness,
                                                                            GwyInterpolationType interpolation);
GwyDoubleArray*      gwy_data_line_part_fit_polynom_pygwy                  (GwyDataLine *data_line,
                                                                            gint n,
                                                                            gint from,
                                                                            gint to);
GwyDoubleArray*      gwy_data_line_fit_polynom_pygwy                       (GwyDataLine *data_line,
                                                                            gint n);
void                 gwy_data_line_part_subtract_polynom_pygwy             (GwyDataLine *data_line,
                                                                            GwyDoubleArray *coeffs,
                                                                            gint from,
                                                                            gint to);
void                 gwy_data_line_subtract_polynom_pygwy                  (GwyDataLine *data_line,
                                                                            GwyDoubleArray *coeffs);
GwyDoubleArray*      gwy_data_line_get_kth_peaks_pygwy                     (GwyDataLine *data_line,
                                                                            gint m,
                                                                            gint rank,
                                                                            gboolean peaks,
                                                                            gboolean average,
                                                                            gdouble pthreshold,
                                                                            gdouble vthreshold);
GQuark               gwy_key_from_name_pygwy                               (const gchar *name);
const gchar*         gwy_name_from_key_pygwy                               (GQuark key);
GwyIntArray*         gwy_container_keys_pygwy                              (GwyContainer *container);
GwyConstStringArray* gwy_container_keys_by_name_pygwy                      (GwyContainer *container);
GwyContainer*        gwy_container_duplicate_by_prefix_pygwy               (GwyContainer *container,
                                                                            GwyStringArray *keys);
GwyStringArray*      gwy_container_serialize_to_text_pygwy                 (GwyContainer *container);
GwyIntArray*         gwy_app_data_browser_get_data_ids_pygwy               (GwyContainer *container);
GwyIntArray*         gwy_app_data_browser_get_graph_ids_pygwy              (GwyContainer *container);
GwyIntArray*         gwy_app_data_browser_get_spectra_ids_pygwy            (GwyContainer *container);
GwyIntArray*         gwy_app_data_browser_get_volume_ids_pygwy             (GwyContainer *container);
GwyIntArray*         gwy_app_data_browser_get_xyz_ids_pygwy                (GwyContainer *container);
GwyIntArray*         gwy_app_data_browser_get_curve_map_ids_pygwy          (GwyContainer *container);
GwyIntArray*         gwy_app_data_browser_find_data_by_title_pygwy         (GwyContainer *data,
                                                                            const gchar *titleglob);
GwyIntArray*         gwy_app_data_browser_find_graphs_by_title_pygwy       (GwyContainer *data,
                                                                            const gchar *titleglob);
GwyIntArray*         gwy_app_data_browser_find_spectra_by_title_pygwy      (GwyContainer *data,
                                                                            const gchar *titleglob);
GwyIntArray*         gwy_app_data_browser_find_volume_by_title_pygwy       (GwyContainer *data,
                                                                            const gchar *titleglob);
GwyIntArray*         gwy_app_data_browser_find_xyz_by_title_pygwy          (GwyContainer *data,
                                                                            const gchar *titleglob);
GwyIntArray*         gwy_app_data_browser_find_curve_map_by_title_pygwy    (GwyContainer *data,
                                                                            const gchar *titleglob);
GwyIntArray*         gwy_data_field_number_grains_pygwy                    (GwyDataField *mask_field);
GwyIntArray*         gwy_data_field_number_grains_periodic_pygwy           (GwyDataField *mask_field);
GwyArrayFuncStatus   gwy_data_field_get_grain_sizes_pygwy                  (GwyDataField *data_field,
                                                                            GwyIntArray *grains,
                                                                            GwyIntArrayOutArg sizes);
GwyArrayFuncStatus   gwy_data_field_get_grain_bounding_boxes_pygwy         (GwyDataField *data_field,
                                                                            GwyIntArray *grains,
                                                                            GwyIntArrayOutArg bboxes);
GwyArrayFuncStatus   gwy_data_field_get_grain_bounding_boxes_periodic_pygwy(GwyDataField *data_field,
                                                                            GwyIntArray *grains,
                                                                            GwyIntArrayOutArg bboxes);
GwyArrayFuncStatus   gwy_data_field_get_grain_inscribed_boxes_pygwy        (GwyDataField *data_field,
                                                                            GwyIntArray *grains,
                                                                            GwyIntArrayOutArg bboxes);
GwyArrayFuncStatus   gwy_data_field_grains_get_values_pygwy                (GwyDataField *data_field,
                                                                            GwyIntArray *grains,
                                                                            GwyGrainQuantity quantity,
                                                                            GwyDoubleArrayOutArg values);
GwyDataLine*         gwy_data_field_grains_get_distribution_pygwy          (GwyDataField *data_field,
                                                                            GwyDataField *grain_field,
                                                                            GwyIntArray *grains,
                                                                            GwyGrainQuantity quantity,
                                                                            gint nstats);
GwyDataField*        gwy_tip_dilation_pygwy                                (GwyDataField *tip,
                                                                            GwyDataField *surface);
GwyDataField*        gwy_tip_erosion_pygwy                                 (GwyDataField *tip,
                                                                            GwyDataField *surface);
GwyDataField*        gwy_tip_cmap_pygwy                                    (GwyDataField *tip,
                                                                            GwyDataField *surface);
GwyDataField*        gwy_data_field_create_full_mask_pygwy                 (GwyDataField *d);
gboolean             gwy_get_grain_quantity_needs_same_units_pygwy         (GwyGrainQuantity quantity);
GwySIUnit*           gwy_construct_grain_quantity_units_pygwy              (GwyGrainQuantity quantity,
                                                                            GwySIUnit *siunitxy,
                                                                            GwySIUnit *siunitz);
GwyIntArray*         gwy_tip_model_preset_get_params_pygwy                 (const GwyTipModelPreset *preset);
GwyArrayFuncStatus   gwy_tip_model_preset_create_pygwy                     (const GwyTipModelPreset *preset,
                                                                            GwyDataField *tip,
                                                                            GwyDoubleArray *params);
GwyArrayFuncStatus   gwy_tip_model_preset_create_for_zrange_pygwy          (const GwyTipModelPreset *preset,
                                                                            GwyDataField *tip,
                                                                            gdouble zrange,
                                                                            gboolean square,
                                                                            GwyDoubleArray *params);
GwySpline*           gwy_spline_new_from_points_pygwy                      (GwyDoubleArray *xy);
GwyDoubleArray*      gwy_spline_get_points_pygwy                           (GwySpline *spline);
GwyDoubleArray*      gwy_spline_get_tangents_pygwy                         (GwySpline *spline);
GwyDoubleArray*      gwy_spline_sample_naturally_pygwy                     (GwySpline *spline);
gdouble              gwy_spline_sample_uniformly_pygwy                     (GwySpline *spline,
                                                                            GwyDoubleArrayOutArg xy,
                                                                            GwyDoubleArrayOutArg t,
                                                                            guint n);
void                 gwy_surface_set_pygwy                                 (GwySurface *surface,
                                                                            guint pos,
                                                                            const GwyXYZ *point);
GwyIntArray*         gwy_spectra_find_nearest_pygwy                        (GwySpectra *spectra,
                                                                            gdouble x,
                                                                            gdouble y,
                                                                            guint n);
GwyDoubleArray*      gwy_graph_curve_model_get_xdata_pygwy                 (GwyGraphCurveModel *gcmodel);
GwyDoubleArray*      gwy_graph_curve_model_get_ydata_pygwy                 (GwyGraphCurveModel *gcmodel);
GwyArrayFuncStatus   gwy_graph_curve_model_set_data_pygwy                  (GwyGraphCurveModel *gcmodel,
                                                                            GwyDoubleArray *xdata,
                                                                            GwyDoubleArray *ydata);
GwyArrayFuncStatus   gwy_graph_curve_model_set_data_interleaved_pygwy      (GwyGraphCurveModel *gcmodel,
                                                                            GwyDoubleArray *xydata);
void                 gwy_graph_area_set_x_grid_data_pygwy                  (GwyGraphArea *area,
                                                                            GwyDoubleArray *grid_data);
void                 gwy_graph_area_set_y_grid_data_pygwy                  (GwyGraphArea *area,
                                                                            GwyDoubleArray *grid_data);
GwyDoubleArray*      gwy_graph_area_get_x_grid_data_pygwy                  (GwyGraphArea *area);
GwyDoubleArray*      gwy_graph_area_get_y_grid_data_pygwy                  (GwyGraphArea *area);
GwyDoubleArray*      gwy_draw_data_field_map_adaptive_pygwy                (GwyDataField *data_field,
                                                                            GwyDoubleArray *z);
GwyDoubleArray*      gwy_data_view_get_metric_pygwy                        (GwyDataView *data_view);
GwyDoubleArray*      gwy_axis_get_major_ticks_pygwy                        (GwyAxis *axis);
gulong               gwy_undo_checkpoint_pygwy                             (GwyContainer *container,
                                                                            GwyStringArray *keys);
gulong               gwy_undo_qcheckpoint_pygwy                            (GwyContainer *container,
                                                                            GwyIntArray *keys);
gulong               gwy_app_undo_checkpoint_pygwy                         (GwyContainer *container,
                                                                            GwyStringArray *keys);
gulong               gwy_app_undo_qcheckpoint_pygwy                        (GwyContainer *container,
                                                                            GwyIntArray *keys);
GObject*             gwy_inventory_get_item_pygwy                          (GwyInventory *inventory,
                                                                            const gchar *name);
GObject*             gwy_inventory_get_item_or_default_pygwy               (GwyInventory *inventory,
                                                                            const gchar *name);
GObject*             gwy_inventory_get_nth_item_pygwy                      (GwyInventory *inventory,
                                                                            guint n);
GObject*             gwy_inventory_get_default_item_pygwy                  (GwyInventory *inventory);
void                 gwy_inventory_insert_item_pygwy                       (GwyInventory *inventory,
                                                                            GObject *object);
void                 gwy_inventory_insert_nth_item_pygwy                   (GwyInventory *inventory,
                                                                            GObject *object,
                                                                            guint n);
void                 gwy_inventory_rename_item_pygwy                       (GwyInventory *inventory,
                                                                            const gchar *name,
                                                                            const gchar *newname);
GObject*             gwy_inventory_new_item_pygwy                          (GwyInventory *inventory,
                                                                            const gchar *name,
                                                                            const gchar *newname);
GwyArrayFuncStatus   gwy_cdline_fit_pygwy                                  (GwyCDLine *cdline,
                                                                            GwyDoubleArray *x,
                                                                            GwyDoubleArray *y,
                                                                            GwyDoubleArrayOutArg params,
                                                                            GwyDoubleArrayOutArg err);
GwyArrayFuncStatus   gwy_cdline_get_value_pygwy                            (GwyCDLine *cdline,
                                                                            gdouble x,
                                                                            GwyDoubleArray *params,
                                                                            gdouble *value,
                                                                            gboolean *fres);
GwyArrayFuncStatus   gwy_peaks_analyze_pygwy                               (GwyPeaks *peaks,
                                                                            GwyDoubleArray *xdata,
                                                                            GwyDoubleArray *ydata,
                                                                            guint maxpeaks,
                                                                            guint *npeaks);
GwyDoubleArray*      gwy_peaks_get_quantity_pygwy                          (GwyPeaks *peaks,
                                                                            GwyPeakQuantity quantity);
GwyDoubleArray*      gwy_marker_box_get_markers_pygwy                      (GwyMarkerBox *mbox);
void                 gwy_marker_box_set_markers_pygwy                      (GwyMarkerBox *mbox,
                                                                            GwyDoubleArray *markers);
void                 gwy_app_sync_data_items_pygwy                         (GwyContainer *source,
                                                                            GwyContainer *dest,
                                                                            gint from_id,
                                                                            gint to_id,
                                                                            gboolean delete_too,
                                                                            GwyIntArray *items);
GtkWidget*           gwy_combo_box_metric_unit_new_pygwy                   (gint from,
                                                                            gint to,
                                                                            GwySIUnit *unit,
                                                                            gint active);
GtkWidget*           gwy_combo_box_graph_curve_new_pygwy                   (GwyGraphModel *gmodel,
                                                                            gint current);
GtkWidget*           gwy_menu_gradient_pygwy                               (void);
GtkWidget*           gwy_gradient_selection_new_pygwy                      (const gchar *active);
GtkWidget*           gwy_gradient_tree_view_new_pygwy                      (const gchar *active);
GtkWidget*           gwy_menu_gl_material_pygwy                            (void);
GtkWidget*           gwy_gl_material_selection_new_pygwy                   (const gchar *active);
GtkWidget*           gwy_gl_material_tree_view_new_pygwy                   (const gchar *active);

#endif

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
