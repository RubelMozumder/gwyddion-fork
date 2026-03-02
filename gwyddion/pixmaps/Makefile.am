# $Id: Makefile.am 26145 2024-01-18 15:55:51Z yeti-dn $

SUBDIRS = src

# Workaround for annoying Inkscape bug (crashing when run in parallel):
# https://gitlab.com/inkscape/inkscape/-/issues/4716
# Slow is better than crashing. We have to put all-am there because we need to
# build the PNGs themselves in parallel (not their prerequisites).
.NOTPARALLEL: all-am

pixmapdatadir = $(pkgdatadir)/pixmaps
icondatadir = $(datadir)/icons/hicolor/48x48/apps

# Since 2.3: arithmetic data_measure drift immerse merge
# Since 2.7: path_level spectrum iso_roughness grains_measure gl_material
# Since 2.7: color_range_auto color_range_full color_range_fixed
# Since 2.7: color_range_adaptive mask_line
# Since 2.22: mask_fill_draw mask_fill_erase mask_paint_draw mask_paint_erase
# Since 2.29: polynom_level
# Since 2.34: pygwy
# Since 2.37: correct_affine extend mark_with synthetic_columnar synthetic_domains synthetic_lattice synthetic_line_noise synthetic_noise synthetic_objects synthetic_particles synthetic_pattern synthetic_waves
# Since 2.38: synthetic_diffusion
# Since 2.45: convolution distribution_angle distribution_slope entropy graph_fd fft_filter_2d fft_2d find_peaks grain_correlation grain_exscribed_circle grain_inscribed_circle grains_edge graph_align graph_cut graph_dos graph_export_ascii graph_export_png graph_export_vector graph_filter level_flatten_base level_median load_debug load_info load_warning mask_distribute mask_extract mask_morph mask_thin measure_lattice neural_apply neural_train psdf_log_phi psdf_section synthetic_ballistic_deposition synthetic_brownian_motion synthetic_spectral tilt tip_dilation tip_erosion tip_estimation tip_map tip_model tip_pid volume volume_calibrate volume_dimensions volume_invert volumize_layers zero_mean
# Since 2.46: distance_transform extract_path enforce_distribution grains_edge_remove mutual_crop poly_distort straighten_path tip_indent_analyze tip_lateral_force volume_fd volume_kmeans volume_kmedians volume_slice volumize
# Since 2.48: correlation_mask disconnected fft_filter_1d fit_shape fractal_correction mark_outliers mark_scars null_offsets xy_denoise synthetic_phases
# Since 2.49: next previous fractal_measure mask_set synthetic_fibres rotate_3d
# Since 2.50: binning curvature facet_analysis grains_statistics limit_range mfm_current_line mfm_field_find_shift mfm_field_shift rank_filter rasterize revolve_arc revolve_sphere scientific_number_format square_samples stitch synthetic_pileup xyzize
# Since 2.51: flip_diagonally synthetic_discs volume_arithmetic
# Since 2.52: convolve deconvolve grains_otsu mfm_convert_to_force mfm_parallel mfm_perpendicular
# Since 2.53: cross_profile radial_profile
# Since 2.54: facet_measure frequency_split graph_statistics graph_terrace_measure image_relation synthetic_anneal synthetic_turing_pattern terrace_measure volume_line_stats volume_plane_stats volume_swap_axes wrap_value
# Since 2.56: correlation_length
# Since 2.57: mask_shift profile_multiple
# Since 2.59: zero_maximum
# Since 2.61: displacement_field grain_bounding_box grain_inscribed_box mask_noisify perspective_distort zero_under_mask
# Since 2.63: graph_period_measure hold_selection_clear hold_selection_replace synthetic_dunes
# Since 2.64: synthetic_plateaus
#
# If you add new stock icons here
# - Add them to a `Since' line above (one Gwyddion version = one line).
# - If they have source files add them to src/ and if they can be generated
#   automatically (the usual case) add them to the built_pixmaps.
# - Run ./utils/stockgen.py from the top-level directory to update the PNG
#   build rules and source code declarations.
# - After that the icon should be probably set as the default for some module
#   or elswhere in the source code.
pixmapdata_DATA = \
	$(built_pixmaps) \
	cd_line.png \
	cd_rline.png \
	cd_rstep.png \
	cd_step.png \
	circle_down.png \
	circle_up.png \
	gwy_drift-24.png \
	gwy_favourite-24.png \
	gwy_gl_material-16.png \
	gwy_gwyddion-32.png \
	gwy_gwyddion-60.png \
	gwy_psdf_log_phi-24.png \
	gwy_psdf_section-24.png \
	gwyddion.ico \
	splash.png

icondata_DATA = \
	gwyddion.png

built_pixmaps = \
	gwy_3d_base-24.png \
	gwy_arithmetic-24.png \
	gwy_binning-24.png \
	gwy_bold-20.png \
	gwy_cantilever-24.png \
	gwy_color_range-24.png \
	gwy_color_range_adaptive-24.png \
	gwy_color_range_auto-24.png \
	gwy_color_range_fixed-24.png \
	gwy_color_range_full-24.png \
	gwy_convolution-24.png \
	gwy_convolve-24.png \
	gwy_correct_affine-24.png \
	gwy_correlation_mask-24.png \
	gwy_correlation_length-24.png \
	gwy_crop-24.png \
	gwy_cross_profile-24.png \
	gwy_curvature-24.png \
	gwy_cwt-24.png \
	gwy_data_measure-24.png \
	gwy_deconvolve-24.png \
	gwy_disconnected-24.png \
	gwy_displacement_field-24.png \
	gwy_distance-24.png \
	gwy_distance_transform-24.png \
	gwy_distribution_angle-24.png \
	gwy_distribution_slope-24.png \
	gwy_dwt-24.png \
	gwy_edge-24.png \
	gwy_enforce_distribution-24.png \
	gwy_entropy-24.png \
	gwy_extend-24.png \
	gwy_extract_path-24.png \
	gwy_facet_analysis-24.png \
	gwy_facet_measure-24.png \
	gwy_facet_level-24.png \
	gwy_favourite-24.png \
	gwy_fft-24.png \
	gwy_fft_2d-24.png \
	gwy_fft_filter_1d-24.png \
	gwy_fft_filter_2d-24.png \
	gwy_filter-24.png \
	gwy_find_peaks-24.png \
	gwy_fit_shape-24.png \
	gwy_fix_zero-24.png \
	gwy_flip_diagonally-24.png \
	gwy_flip_horizontally-24.png \
	gwy_flip_vertically-24.png \
	gwy_fractal-24.png \
	gwy_fractal_measure-24.png \
	gwy_fractal_correction-24.png \
	gwy_frequency_split-24.png \
	gwy_gl_material-16.png \
	gwy_gradient_horizontal-24.png \
	gwy_gradient_vertical-24.png \
	gwy_grain_bounding_box-24.png \
	gwy_grain_correlation-24.png \
	gwy_grain_exscribed_circle-24.png \
	gwy_grain_inscribed_box-24.png \
	gwy_grain_inscribed_circle-24.png \
	gwy_grains-24.png \
	gwy_grains_edge-24.png \
	gwy_grains_edge_remove-24.png \
	gwy_grains_graph-24.png \
	gwy_grains_measure-24.png \
	gwy_grains_otsu-24.png \
	gwy_grains_remove-24.png \
	gwy_grains_statistics-24.png \
	gwy_grains_water-24.png \
	gwy_graph-24.png \
	gwy_graph_align-24.png \
	gwy_graph_cut-24.png \
	gwy_graph_dos-24.png \
	gwy_graph_export_ascii-24.png \
	gwy_graph_export_png-24.png \
	gwy_graph_export_vector-24.png \
	gwy_graph_fd-24.png \
	gwy_graph_filter-24.png \
	gwy_graph_function-24.png \
	gwy_graph_halfgauss-24.png \
	gwy_graph_level-24.png \
	gwy_graph_measure-24.png \
	gwy_graph_palette-24.png \
	gwy_graph_pointer-24.png \
	gwy_graph_ruler-24.png \
	gwy_graph_statistics-24.png \
	gwy_graph_period_measure-24.png \
	gwy_graph_terrace_measure-24.png \
	gwy_graph_vertical-24.png \
	gwy_graph_zoom_fit-24.png \
	gwy_graph_zoom_in-24.png \
	gwy_graph_zoom_out-24.png \
	gwy_hough-24.png \
	gwy_hold_selection_clear-18.png \
	gwy_hold_selection_replace-18.png \
	gwy_immerse-24.png \
	gwy_iso_roughness-24.png \
	gwy_image_relation-24.png \
	gwy_italic-20.png \
	gwy_less-24.png \
	gwy_level-24.png \
	gwy_level_flatten_base-24.png \
	gwy_level_median-24.png \
	gwy_level_triangle-24.png \
	gwy_light_rotate-24.png \
	gwy_limit_range-24.png \
	gwy_line_level-24.png \
	gwy_load_debug-20.png \
	gwy_load_info-20.png \
	gwy_load_warning-20.png \
	gwy_local_slope-24.png \
	gwy_logscale_horizontal-24.png \
	gwy_logscale_vertical-24.png \
	gwy_mark_outliers-24.png \
	gwy_mark_scars-24.png \
	gwy_mark_with-24.png \
	gwy_mask-16.png \
	gwy_mask-24.png \
	gwy_mask_add-24.png \
	gwy_mask_circle-24.png \
	gwy_mask_circle_exclusive-24.png \
	gwy_mask_circle_inclusive-24.png \
	gwy_mask_distribute-24.png \
	gwy_mask_editor-24.png \
	gwy_mask_exclude-24.png \
	gwy_mask_exclude_circle-24.png \
	gwy_mask_extract-24.png \
	gwy_mask_fill_draw-24.png \
	gwy_mask_fill_erase-24.png \
	gwy_mask_grow-24.png \
	gwy_mask_intersect-24.png \
	gwy_mask_invert-24.png \
	gwy_mask_line-24.png \
	gwy_mask_morph-24.png \
	gwy_mask_noisify-24.png \
	gwy_mask_paint_draw-24.png \
	gwy_mask_paint_erase-24.png \
	gwy_mask_rect_exclusive-24.png \
	gwy_mask_rect_inclusive-24.png \
	gwy_mask_remove-24.png \
	gwy_mask_set-24.png \
	gwy_mask_shift-24.png \
	gwy_mask_shrink-24.png \
	gwy_mask_subtract-24.png \
	gwy_mask_thin-24.png \
	gwy_measure_lattice-24.png \
	gwy_merge-24.png \
	gwy_mfm_convert_to_force-24.png \
	gwy_mfm_current_line-24.png \
	gwy_mfm_field_find_shift-24.png \
	gwy_mfm_field_shift-24.png \
	gwy_mfm_parallel-24.png \
	gwy_mfm_perpendicular-24.png \
	gwy_more-24.png \
	gwy_mutual_crop-24.png \
	gwy_neural_apply-24.png \
	gwy_neural_train-24.png \
	gwy_next-24.png \
	gwy_null_offsets-24.png \
	gwy_palettes-16.png \
	gwy_palettes-24.png \
	gwy_path_level-24.png \
	gwy_perspective_distort-24.png \
	gwy_pointer_measure-24.png \
	gwy_poly_distort-24.png \
	gwy_polynom-24.png \
	gwy_polynom_level-24.png \
	gwy_previous-24.png \
	gwy_profile-24.png \
	gwy_profile_multiple-24.png \
	gwy_psdf_log_phi-24.png \
	gwy_pygwy-24.png \
	gwy_rasterize-24.png \
	gwy_radial_profile-24.png \
	gwy_rank_filter-24.png \
	gwy_remove_under_mask-24.png \
	gwy_revolve_arc-24.png \
	gwy_revolve_sphere-24.png \
	gwy_rotate_180-24.png \
	gwy_rotate_3d-24.png \
	gwy_rotate-24.png \
	gwy_rotate_90_ccw-24.png \
	gwy_rotate_90_cw-24.png \
	gwy_scale-24.png \
	gwy_scale_horizontally-24.png \
	gwy_scale_vertically-24.png \
	gwy_scars-24.png \
	gwy_scientific_number_format-18.png \
	gwy_selections-24.png \
	gwy_shader-24.png \
	gwy_spectrum-24.png \
	gwy_spot_remove-24.png \
	gwy_square_samples-24.png \
	gwy_stat_quantities-24.png \
	gwy_stitch-24.png \
	gwy_straighten_path-24.png \
	gwy_subscript-20.png \
	gwy_superscript-20.png \
	gwy_synthetic_anneal-24.png \
	gwy_synthetic_ballistic_deposition-24.png \
	gwy_synthetic_brownian_motion-24.png \
	gwy_synthetic_columnar-24.png \
	gwy_synthetic_diffusion-24.png \
	gwy_synthetic_discs-24.png \
	gwy_synthetic_domains-24.png \
	gwy_synthetic_dunes-24.png \
	gwy_synthetic_fibres-24.png \
	gwy_synthetic_lattice-24.png \
	gwy_synthetic_line_noise-24.png \
	gwy_synthetic_noise-24.png \
	gwy_synthetic_objects-24.png \
	gwy_synthetic_particles-24.png \
	gwy_synthetic_pattern-24.png \
	gwy_synthetic_phases-24.png \
	gwy_synthetic_pileup-24.png \
	gwy_synthetic_plateaus-24.png \
	gwy_synthetic_spectral-24.png \
	gwy_synthetic_turing_pattern-24.png \
	gwy_synthetic_waves-24.png \
	gwy_terrace_measure-24.png \
	gwy_tilt-24.png \
	gwy_tip_dilation-24.png \
	gwy_tip_erosion-24.png \
	gwy_tip_estimation-24.png \
	gwy_tip_indent_analyze-24.png \
	gwy_tip_lateral_force-24.png \
	gwy_tip_map-24.png \
	gwy_tip_model-24.png \
	gwy_tip_pid-24.png \
	gwy_unrotate-24.png \
	gwy_value_invert-24.png \
	gwy_volume-24.png \
	gwy_volume_arithmetic-24.png \
	gwy_volume_calibrate-24.png \
	gwy_volume_dimensions-24.png \
	gwy_volume_fd-24.png \
	gwy_volume_invert-24.png \
	gwy_volume_kmeans-24.png \
	gwy_volume_kmedians-24.png \
	gwy_volume_line_stats-24.png \
	gwy_volume_plane_stats-24.png \
	gwy_volume_slice-24.png \
	gwy_volume_swap_axes-24.png \
	gwy_volumize-24.png \
	gwy_volumize_layers-24.png \
	gwy_wrap_value-24.png \
	gwy_xy_denoise-24.png \
	gwy_xyzize-24.png \
	gwy_zero_maximum-24.png \
	gwy_zero_mean-24.png \
	gwy_zero_under_mask-24.png \
	gwy_zoom_1_1-24.png \
	gwy_zoom_fit-24.png \
	gwy_zoom_in-24.png \
	gwy_zoom_out-24.png

INKSCAPE_EXPORT = $(INKSCAPE) --export-area-page

# Inkscape 1.x will complain about not being able to create windows when
# run without DISPLAY, but seems to be making the images fine.  Inkscape
# 1.1+ also seems to crash when given --batch-process without DISPLAY.
# Do not do that.
if INKSCAPE_EXPORT_NEWSTYLE
INKSCAPE_EXPORT_PNGFILE = --export-type=png --export-filename
else
INKSCAPE_EXPORT_PNGFILE = --without-gui --export-png
endif

# Not built automatically:
# gwy_drift-24.png - must render to 96x96 first, then downsize due to
#                    Inkscape gradient mesh issues
#
# cd_line.png
# cd_rline.png
# cd_rstep.png
# cd_step.png - no SVG
#
# circle_down.png
# circle_up.png - no source whatsoever!
#
# gwy_psdf_section-24.png
#
# gwy_gwyddion-32.png
# gwy_gwyddion-60.png - no SVG

# Note: splash screen source is too big to distribute in every tarball,
# check it out from Subversion

EXTRA_DIST = \
	gwyddion.icns \
	$(icondata_DATA) \
	$(pixmapdata_DATA)

MAINTAINERCLEANFILES = $(built_pixmaps)

list-icons:
	@echo ICONLIST $(pixmapdata_DATA)

list-built-icons:
	@echo ICONLIST $(built_pixmaps)

if MAINTAINER_MODE
# @@@ GENERATED STOCK LIST BEGIN @@@
gwy_3d_base-24.png: $(srcdir)/src/gwy_3d_base-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_3d_base-24.png" "$(srcdir)/src/gwy_3d_base-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_3d_base-24.png" "gwy_3d_base-24.crush.png"
	$(AM_V_at)if test -s "gwy_3d_base-24.crush.png"; then mv -f "gwy_3d_base-24.crush.png" "gwy_3d_base-24.png"; fi

gwy_arithmetic-24.png: $(srcdir)/src/gwy_arithmetic-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_arithmetic-24.png" "$(srcdir)/src/gwy_arithmetic-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_arithmetic-24.png" "gwy_arithmetic-24.crush.png"
	$(AM_V_at)if test -s "gwy_arithmetic-24.crush.png"; then mv -f "gwy_arithmetic-24.crush.png" "gwy_arithmetic-24.png"; fi

gwy_binning-24.png: $(srcdir)/src/gwy_binning-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_binning-24.png" "$(srcdir)/src/gwy_binning-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_binning-24.png" "gwy_binning-24.crush.png"
	$(AM_V_at)if test -s "gwy_binning-24.crush.png"; then mv -f "gwy_binning-24.crush.png" "gwy_binning-24.png"; fi

gwy_bold-20.png: $(srcdir)/src/gwy_bold-20.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=20 --export-height=20 $(INKSCAPE_EXPORT_PNGFILE)="gwy_bold-20.png" "$(srcdir)/src/gwy_bold-20.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_bold-20.png" "gwy_bold-20.crush.png"
	$(AM_V_at)if test -s "gwy_bold-20.crush.png"; then mv -f "gwy_bold-20.crush.png" "gwy_bold-20.png"; fi

gwy_cantilever-24.png: $(srcdir)/src/gwy_cantilever-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_cantilever-24.png" "$(srcdir)/src/gwy_cantilever-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_cantilever-24.png" "gwy_cantilever-24.crush.png"
	$(AM_V_at)if test -s "gwy_cantilever-24.crush.png"; then mv -f "gwy_cantilever-24.crush.png" "gwy_cantilever-24.png"; fi

gwy_color_range-24.png: $(srcdir)/src/gwy_color_range-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_color_range-24.png" "$(srcdir)/src/gwy_color_range-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_color_range-24.png" "gwy_color_range-24.crush.png"
	$(AM_V_at)if test -s "gwy_color_range-24.crush.png"; then mv -f "gwy_color_range-24.crush.png" "gwy_color_range-24.png"; fi

gwy_color_range_adaptive-24.png: $(srcdir)/src/gwy_color_range_adaptive-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_color_range_adaptive-24.png" "$(srcdir)/src/gwy_color_range_adaptive-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_color_range_adaptive-24.png" "gwy_color_range_adaptive-24.crush.png"
	$(AM_V_at)if test -s "gwy_color_range_adaptive-24.crush.png"; then mv -f "gwy_color_range_adaptive-24.crush.png" "gwy_color_range_adaptive-24.png"; fi

gwy_color_range_auto-24.png: $(srcdir)/src/gwy_color_range_auto-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_color_range_auto-24.png" "$(srcdir)/src/gwy_color_range_auto-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_color_range_auto-24.png" "gwy_color_range_auto-24.crush.png"
	$(AM_V_at)if test -s "gwy_color_range_auto-24.crush.png"; then mv -f "gwy_color_range_auto-24.crush.png" "gwy_color_range_auto-24.png"; fi

gwy_color_range_fixed-24.png: $(srcdir)/src/gwy_color_range_fixed-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_color_range_fixed-24.png" "$(srcdir)/src/gwy_color_range_fixed-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_color_range_fixed-24.png" "gwy_color_range_fixed-24.crush.png"
	$(AM_V_at)if test -s "gwy_color_range_fixed-24.crush.png"; then mv -f "gwy_color_range_fixed-24.crush.png" "gwy_color_range_fixed-24.png"; fi

gwy_color_range_full-24.png: $(srcdir)/src/gwy_color_range_full-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_color_range_full-24.png" "$(srcdir)/src/gwy_color_range_full-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_color_range_full-24.png" "gwy_color_range_full-24.crush.png"
	$(AM_V_at)if test -s "gwy_color_range_full-24.crush.png"; then mv -f "gwy_color_range_full-24.crush.png" "gwy_color_range_full-24.png"; fi

gwy_convolution-24.png: $(srcdir)/src/gwy_convolution-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_convolution-24.png" "$(srcdir)/src/gwy_convolution-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_convolution-24.png" "gwy_convolution-24.crush.png"
	$(AM_V_at)if test -s "gwy_convolution-24.crush.png"; then mv -f "gwy_convolution-24.crush.png" "gwy_convolution-24.png"; fi

gwy_convolve-24.png: $(srcdir)/src/gwy_convolve-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_convolve-24.png" "$(srcdir)/src/gwy_convolve-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_convolve-24.png" "gwy_convolve-24.crush.png"
	$(AM_V_at)if test -s "gwy_convolve-24.crush.png"; then mv -f "gwy_convolve-24.crush.png" "gwy_convolve-24.png"; fi

gwy_correct_affine-24.png: $(srcdir)/src/gwy_correct_affine-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_correct_affine-24.png" "$(srcdir)/src/gwy_correct_affine-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_correct_affine-24.png" "gwy_correct_affine-24.crush.png"
	$(AM_V_at)if test -s "gwy_correct_affine-24.crush.png"; then mv -f "gwy_correct_affine-24.crush.png" "gwy_correct_affine-24.png"; fi

gwy_correlation_length-24.png: $(srcdir)/src/gwy_correlation_length-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_correlation_length-24.png" "$(srcdir)/src/gwy_correlation_length-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_correlation_length-24.png" "gwy_correlation_length-24.crush.png"
	$(AM_V_at)if test -s "gwy_correlation_length-24.crush.png"; then mv -f "gwy_correlation_length-24.crush.png" "gwy_correlation_length-24.png"; fi

gwy_correlation_mask-24.png: $(srcdir)/src/gwy_correlation_mask-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_correlation_mask-24.png" "$(srcdir)/src/gwy_correlation_mask-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_correlation_mask-24.png" "gwy_correlation_mask-24.crush.png"
	$(AM_V_at)if test -s "gwy_correlation_mask-24.crush.png"; then mv -f "gwy_correlation_mask-24.crush.png" "gwy_correlation_mask-24.png"; fi

gwy_crop-24.png: $(srcdir)/src/gwy_crop-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_crop-24.png" "$(srcdir)/src/gwy_crop-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_crop-24.png" "gwy_crop-24.crush.png"
	$(AM_V_at)if test -s "gwy_crop-24.crush.png"; then mv -f "gwy_crop-24.crush.png" "gwy_crop-24.png"; fi

gwy_cross_profile-24.png: $(srcdir)/src/gwy_cross_profile-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_cross_profile-24.png" "$(srcdir)/src/gwy_cross_profile-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_cross_profile-24.png" "gwy_cross_profile-24.crush.png"
	$(AM_V_at)if test -s "gwy_cross_profile-24.crush.png"; then mv -f "gwy_cross_profile-24.crush.png" "gwy_cross_profile-24.png"; fi

gwy_curvature-24.png: $(srcdir)/src/gwy_curvature-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_curvature-24.png" "$(srcdir)/src/gwy_curvature-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_curvature-24.png" "gwy_curvature-24.crush.png"
	$(AM_V_at)if test -s "gwy_curvature-24.crush.png"; then mv -f "gwy_curvature-24.crush.png" "gwy_curvature-24.png"; fi

gwy_cwt-24.png: $(srcdir)/src/gwy_cwt-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_cwt-24.png" "$(srcdir)/src/gwy_cwt-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_cwt-24.png" "gwy_cwt-24.crush.png"
	$(AM_V_at)if test -s "gwy_cwt-24.crush.png"; then mv -f "gwy_cwt-24.crush.png" "gwy_cwt-24.png"; fi

gwy_data_measure-24.png: $(srcdir)/src/gwy_data_measure-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_data_measure-24.png" "$(srcdir)/src/gwy_data_measure-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_data_measure-24.png" "gwy_data_measure-24.crush.png"
	$(AM_V_at)if test -s "gwy_data_measure-24.crush.png"; then mv -f "gwy_data_measure-24.crush.png" "gwy_data_measure-24.png"; fi

gwy_deconvolve-24.png: $(srcdir)/src/gwy_deconvolve-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_deconvolve-24.png" "$(srcdir)/src/gwy_deconvolve-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_deconvolve-24.png" "gwy_deconvolve-24.crush.png"
	$(AM_V_at)if test -s "gwy_deconvolve-24.crush.png"; then mv -f "gwy_deconvolve-24.crush.png" "gwy_deconvolve-24.png"; fi

gwy_disconnected-24.png: $(srcdir)/src/gwy_disconnected-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_disconnected-24.png" "$(srcdir)/src/gwy_disconnected-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_disconnected-24.png" "gwy_disconnected-24.crush.png"
	$(AM_V_at)if test -s "gwy_disconnected-24.crush.png"; then mv -f "gwy_disconnected-24.crush.png" "gwy_disconnected-24.png"; fi

gwy_displacement_field-24.png: $(srcdir)/src/gwy_displacement_field-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_displacement_field-24.png" "$(srcdir)/src/gwy_displacement_field-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_displacement_field-24.png" "gwy_displacement_field-24.crush.png"
	$(AM_V_at)if test -s "gwy_displacement_field-24.crush.png"; then mv -f "gwy_displacement_field-24.crush.png" "gwy_displacement_field-24.png"; fi

gwy_distance-24.png: $(srcdir)/src/gwy_distance-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_distance-24.png" "$(srcdir)/src/gwy_distance-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_distance-24.png" "gwy_distance-24.crush.png"
	$(AM_V_at)if test -s "gwy_distance-24.crush.png"; then mv -f "gwy_distance-24.crush.png" "gwy_distance-24.png"; fi

gwy_distance_transform-24.png: $(srcdir)/src/gwy_distance_transform-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_distance_transform-24.png" "$(srcdir)/src/gwy_distance_transform-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_distance_transform-24.png" "gwy_distance_transform-24.crush.png"
	$(AM_V_at)if test -s "gwy_distance_transform-24.crush.png"; then mv -f "gwy_distance_transform-24.crush.png" "gwy_distance_transform-24.png"; fi

gwy_distribution_angle-24.png: $(srcdir)/src/gwy_distribution_angle-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_distribution_angle-24.png" "$(srcdir)/src/gwy_distribution_angle-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_distribution_angle-24.png" "gwy_distribution_angle-24.crush.png"
	$(AM_V_at)if test -s "gwy_distribution_angle-24.crush.png"; then mv -f "gwy_distribution_angle-24.crush.png" "gwy_distribution_angle-24.png"; fi

gwy_distribution_slope-24.png: $(srcdir)/src/gwy_distribution_slope-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_distribution_slope-24.png" "$(srcdir)/src/gwy_distribution_slope-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_distribution_slope-24.png" "gwy_distribution_slope-24.crush.png"
	$(AM_V_at)if test -s "gwy_distribution_slope-24.crush.png"; then mv -f "gwy_distribution_slope-24.crush.png" "gwy_distribution_slope-24.png"; fi

gwy_dwt-24.png: $(srcdir)/src/gwy_dwt-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_dwt-24.png" "$(srcdir)/src/gwy_dwt-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_dwt-24.png" "gwy_dwt-24.crush.png"
	$(AM_V_at)if test -s "gwy_dwt-24.crush.png"; then mv -f "gwy_dwt-24.crush.png" "gwy_dwt-24.png"; fi

gwy_edge-24.png: $(srcdir)/src/gwy_edge-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_edge-24.png" "$(srcdir)/src/gwy_edge-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_edge-24.png" "gwy_edge-24.crush.png"
	$(AM_V_at)if test -s "gwy_edge-24.crush.png"; then mv -f "gwy_edge-24.crush.png" "gwy_edge-24.png"; fi

gwy_enforce_distribution-24.png: $(srcdir)/src/gwy_enforce_distribution-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_enforce_distribution-24.png" "$(srcdir)/src/gwy_enforce_distribution-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_enforce_distribution-24.png" "gwy_enforce_distribution-24.crush.png"
	$(AM_V_at)if test -s "gwy_enforce_distribution-24.crush.png"; then mv -f "gwy_enforce_distribution-24.crush.png" "gwy_enforce_distribution-24.png"; fi

gwy_entropy-24.png: $(srcdir)/src/gwy_entropy-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_entropy-24.png" "$(srcdir)/src/gwy_entropy-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_entropy-24.png" "gwy_entropy-24.crush.png"
	$(AM_V_at)if test -s "gwy_entropy-24.crush.png"; then mv -f "gwy_entropy-24.crush.png" "gwy_entropy-24.png"; fi

gwy_extend-24.png: $(srcdir)/src/gwy_extend-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_extend-24.png" "$(srcdir)/src/gwy_extend-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_extend-24.png" "gwy_extend-24.crush.png"
	$(AM_V_at)if test -s "gwy_extend-24.crush.png"; then mv -f "gwy_extend-24.crush.png" "gwy_extend-24.png"; fi

gwy_extract_path-24.png: $(srcdir)/src/gwy_extract_path-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_extract_path-24.png" "$(srcdir)/src/gwy_extract_path-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_extract_path-24.png" "gwy_extract_path-24.crush.png"
	$(AM_V_at)if test -s "gwy_extract_path-24.crush.png"; then mv -f "gwy_extract_path-24.crush.png" "gwy_extract_path-24.png"; fi

gwy_facet_analysis-24.png: $(srcdir)/src/gwy_facet_analysis-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_facet_analysis-24.png" "$(srcdir)/src/gwy_facet_analysis-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_facet_analysis-24.png" "gwy_facet_analysis-24.crush.png"
	$(AM_V_at)if test -s "gwy_facet_analysis-24.crush.png"; then mv -f "gwy_facet_analysis-24.crush.png" "gwy_facet_analysis-24.png"; fi

gwy_facet_level-24.png: $(srcdir)/src/gwy_facet_level-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_facet_level-24.png" "$(srcdir)/src/gwy_facet_level-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_facet_level-24.png" "gwy_facet_level-24.crush.png"
	$(AM_V_at)if test -s "gwy_facet_level-24.crush.png"; then mv -f "gwy_facet_level-24.crush.png" "gwy_facet_level-24.png"; fi

gwy_facet_measure-24.png: $(srcdir)/src/gwy_facet_measure-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_facet_measure-24.png" "$(srcdir)/src/gwy_facet_measure-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_facet_measure-24.png" "gwy_facet_measure-24.crush.png"
	$(AM_V_at)if test -s "gwy_facet_measure-24.crush.png"; then mv -f "gwy_facet_measure-24.crush.png" "gwy_facet_measure-24.png"; fi

gwy_favourite-24.png: $(srcdir)/src/gwy_favourite-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_favourite-24.png" "$(srcdir)/src/gwy_favourite-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_favourite-24.png" "gwy_favourite-24.crush.png"
	$(AM_V_at)if test -s "gwy_favourite-24.crush.png"; then mv -f "gwy_favourite-24.crush.png" "gwy_favourite-24.png"; fi

gwy_fft-24.png: $(srcdir)/src/gwy_fft-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_fft-24.png" "$(srcdir)/src/gwy_fft-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_fft-24.png" "gwy_fft-24.crush.png"
	$(AM_V_at)if test -s "gwy_fft-24.crush.png"; then mv -f "gwy_fft-24.crush.png" "gwy_fft-24.png"; fi

gwy_fft_2d-24.png: $(srcdir)/src/gwy_fft_2d-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_fft_2d-24.png" "$(srcdir)/src/gwy_fft_2d-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_fft_2d-24.png" "gwy_fft_2d-24.crush.png"
	$(AM_V_at)if test -s "gwy_fft_2d-24.crush.png"; then mv -f "gwy_fft_2d-24.crush.png" "gwy_fft_2d-24.png"; fi

gwy_fft_filter_1d-24.png: $(srcdir)/src/gwy_fft_filter_1d-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_fft_filter_1d-24.png" "$(srcdir)/src/gwy_fft_filter_1d-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_fft_filter_1d-24.png" "gwy_fft_filter_1d-24.crush.png"
	$(AM_V_at)if test -s "gwy_fft_filter_1d-24.crush.png"; then mv -f "gwy_fft_filter_1d-24.crush.png" "gwy_fft_filter_1d-24.png"; fi

gwy_fft_filter_2d-24.png: $(srcdir)/src/gwy_fft_filter_2d-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_fft_filter_2d-24.png" "$(srcdir)/src/gwy_fft_filter_2d-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_fft_filter_2d-24.png" "gwy_fft_filter_2d-24.crush.png"
	$(AM_V_at)if test -s "gwy_fft_filter_2d-24.crush.png"; then mv -f "gwy_fft_filter_2d-24.crush.png" "gwy_fft_filter_2d-24.png"; fi

gwy_filter-24.png: $(srcdir)/src/gwy_filter-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_filter-24.png" "$(srcdir)/src/gwy_filter-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_filter-24.png" "gwy_filter-24.crush.png"
	$(AM_V_at)if test -s "gwy_filter-24.crush.png"; then mv -f "gwy_filter-24.crush.png" "gwy_filter-24.png"; fi

gwy_find_peaks-24.png: $(srcdir)/src/gwy_find_peaks-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_find_peaks-24.png" "$(srcdir)/src/gwy_find_peaks-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_find_peaks-24.png" "gwy_find_peaks-24.crush.png"
	$(AM_V_at)if test -s "gwy_find_peaks-24.crush.png"; then mv -f "gwy_find_peaks-24.crush.png" "gwy_find_peaks-24.png"; fi

gwy_fit_shape-24.png: $(srcdir)/src/gwy_fit_shape-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_fit_shape-24.png" "$(srcdir)/src/gwy_fit_shape-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_fit_shape-24.png" "gwy_fit_shape-24.crush.png"
	$(AM_V_at)if test -s "gwy_fit_shape-24.crush.png"; then mv -f "gwy_fit_shape-24.crush.png" "gwy_fit_shape-24.png"; fi

gwy_fix_zero-24.png: $(srcdir)/src/gwy_fix_zero-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_fix_zero-24.png" "$(srcdir)/src/gwy_fix_zero-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_fix_zero-24.png" "gwy_fix_zero-24.crush.png"
	$(AM_V_at)if test -s "gwy_fix_zero-24.crush.png"; then mv -f "gwy_fix_zero-24.crush.png" "gwy_fix_zero-24.png"; fi

gwy_flip_diagonally-24.png: $(srcdir)/src/gwy_flip_diagonally-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_flip_diagonally-24.png" "$(srcdir)/src/gwy_flip_diagonally-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_flip_diagonally-24.png" "gwy_flip_diagonally-24.crush.png"
	$(AM_V_at)if test -s "gwy_flip_diagonally-24.crush.png"; then mv -f "gwy_flip_diagonally-24.crush.png" "gwy_flip_diagonally-24.png"; fi

gwy_flip_horizontally-24.png: $(srcdir)/src/gwy_flip_horizontally-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_flip_horizontally-24.png" "$(srcdir)/src/gwy_flip_horizontally-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_flip_horizontally-24.png" "gwy_flip_horizontally-24.crush.png"
	$(AM_V_at)if test -s "gwy_flip_horizontally-24.crush.png"; then mv -f "gwy_flip_horizontally-24.crush.png" "gwy_flip_horizontally-24.png"; fi

gwy_flip_vertically-24.png: $(srcdir)/src/gwy_flip_vertically-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_flip_vertically-24.png" "$(srcdir)/src/gwy_flip_vertically-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_flip_vertically-24.png" "gwy_flip_vertically-24.crush.png"
	$(AM_V_at)if test -s "gwy_flip_vertically-24.crush.png"; then mv -f "gwy_flip_vertically-24.crush.png" "gwy_flip_vertically-24.png"; fi

gwy_fractal-24.png: $(srcdir)/src/gwy_fractal-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_fractal-24.png" "$(srcdir)/src/gwy_fractal-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_fractal-24.png" "gwy_fractal-24.crush.png"
	$(AM_V_at)if test -s "gwy_fractal-24.crush.png"; then mv -f "gwy_fractal-24.crush.png" "gwy_fractal-24.png"; fi

gwy_fractal_correction-24.png: $(srcdir)/src/gwy_fractal_correction-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_fractal_correction-24.png" "$(srcdir)/src/gwy_fractal_correction-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_fractal_correction-24.png" "gwy_fractal_correction-24.crush.png"
	$(AM_V_at)if test -s "gwy_fractal_correction-24.crush.png"; then mv -f "gwy_fractal_correction-24.crush.png" "gwy_fractal_correction-24.png"; fi

gwy_fractal_measure-24.png: $(srcdir)/src/gwy_fractal_measure-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_fractal_measure-24.png" "$(srcdir)/src/gwy_fractal_measure-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_fractal_measure-24.png" "gwy_fractal_measure-24.crush.png"
	$(AM_V_at)if test -s "gwy_fractal_measure-24.crush.png"; then mv -f "gwy_fractal_measure-24.crush.png" "gwy_fractal_measure-24.png"; fi

gwy_frequency_split-24.png: $(srcdir)/src/gwy_frequency_split-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_frequency_split-24.png" "$(srcdir)/src/gwy_frequency_split-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_frequency_split-24.png" "gwy_frequency_split-24.crush.png"
	$(AM_V_at)if test -s "gwy_frequency_split-24.crush.png"; then mv -f "gwy_frequency_split-24.crush.png" "gwy_frequency_split-24.png"; fi

gwy_gl_material-16.png: $(srcdir)/src/gwy_gl_material-16.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=16 --export-height=16 $(INKSCAPE_EXPORT_PNGFILE)="gwy_gl_material-16.png" "$(srcdir)/src/gwy_gl_material-16.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_gl_material-16.png" "gwy_gl_material-16.crush.png"
	$(AM_V_at)if test -s "gwy_gl_material-16.crush.png"; then mv -f "gwy_gl_material-16.crush.png" "gwy_gl_material-16.png"; fi

gwy_gradient_horizontal-24.png: $(srcdir)/src/gwy_gradient_horizontal-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_gradient_horizontal-24.png" "$(srcdir)/src/gwy_gradient_horizontal-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_gradient_horizontal-24.png" "gwy_gradient_horizontal-24.crush.png"
	$(AM_V_at)if test -s "gwy_gradient_horizontal-24.crush.png"; then mv -f "gwy_gradient_horizontal-24.crush.png" "gwy_gradient_horizontal-24.png"; fi

gwy_gradient_vertical-24.png: $(srcdir)/src/gwy_gradient_vertical-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_gradient_vertical-24.png" "$(srcdir)/src/gwy_gradient_vertical-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_gradient_vertical-24.png" "gwy_gradient_vertical-24.crush.png"
	$(AM_V_at)if test -s "gwy_gradient_vertical-24.crush.png"; then mv -f "gwy_gradient_vertical-24.crush.png" "gwy_gradient_vertical-24.png"; fi

gwy_grain_bounding_box-24.png: $(srcdir)/src/gwy_grain_bounding_box-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_grain_bounding_box-24.png" "$(srcdir)/src/gwy_grain_bounding_box-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_grain_bounding_box-24.png" "gwy_grain_bounding_box-24.crush.png"
	$(AM_V_at)if test -s "gwy_grain_bounding_box-24.crush.png"; then mv -f "gwy_grain_bounding_box-24.crush.png" "gwy_grain_bounding_box-24.png"; fi

gwy_grain_correlation-24.png: $(srcdir)/src/gwy_grain_correlation-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_grain_correlation-24.png" "$(srcdir)/src/gwy_grain_correlation-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_grain_correlation-24.png" "gwy_grain_correlation-24.crush.png"
	$(AM_V_at)if test -s "gwy_grain_correlation-24.crush.png"; then mv -f "gwy_grain_correlation-24.crush.png" "gwy_grain_correlation-24.png"; fi

gwy_grain_exscribed_circle-24.png: $(srcdir)/src/gwy_grain_exscribed_circle-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_grain_exscribed_circle-24.png" "$(srcdir)/src/gwy_grain_exscribed_circle-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_grain_exscribed_circle-24.png" "gwy_grain_exscribed_circle-24.crush.png"
	$(AM_V_at)if test -s "gwy_grain_exscribed_circle-24.crush.png"; then mv -f "gwy_grain_exscribed_circle-24.crush.png" "gwy_grain_exscribed_circle-24.png"; fi

gwy_grain_inscribed_box-24.png: $(srcdir)/src/gwy_grain_inscribed_box-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_grain_inscribed_box-24.png" "$(srcdir)/src/gwy_grain_inscribed_box-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_grain_inscribed_box-24.png" "gwy_grain_inscribed_box-24.crush.png"
	$(AM_V_at)if test -s "gwy_grain_inscribed_box-24.crush.png"; then mv -f "gwy_grain_inscribed_box-24.crush.png" "gwy_grain_inscribed_box-24.png"; fi

gwy_grain_inscribed_circle-24.png: $(srcdir)/src/gwy_grain_inscribed_circle-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_grain_inscribed_circle-24.png" "$(srcdir)/src/gwy_grain_inscribed_circle-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_grain_inscribed_circle-24.png" "gwy_grain_inscribed_circle-24.crush.png"
	$(AM_V_at)if test -s "gwy_grain_inscribed_circle-24.crush.png"; then mv -f "gwy_grain_inscribed_circle-24.crush.png" "gwy_grain_inscribed_circle-24.png"; fi

gwy_grains-24.png: $(srcdir)/src/gwy_grains-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_grains-24.png" "$(srcdir)/src/gwy_grains-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_grains-24.png" "gwy_grains-24.crush.png"
	$(AM_V_at)if test -s "gwy_grains-24.crush.png"; then mv -f "gwy_grains-24.crush.png" "gwy_grains-24.png"; fi

gwy_grains_edge-24.png: $(srcdir)/src/gwy_grains_edge-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_grains_edge-24.png" "$(srcdir)/src/gwy_grains_edge-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_grains_edge-24.png" "gwy_grains_edge-24.crush.png"
	$(AM_V_at)if test -s "gwy_grains_edge-24.crush.png"; then mv -f "gwy_grains_edge-24.crush.png" "gwy_grains_edge-24.png"; fi

gwy_grains_edge_remove-24.png: $(srcdir)/src/gwy_grains_edge_remove-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_grains_edge_remove-24.png" "$(srcdir)/src/gwy_grains_edge_remove-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_grains_edge_remove-24.png" "gwy_grains_edge_remove-24.crush.png"
	$(AM_V_at)if test -s "gwy_grains_edge_remove-24.crush.png"; then mv -f "gwy_grains_edge_remove-24.crush.png" "gwy_grains_edge_remove-24.png"; fi

gwy_grains_graph-24.png: $(srcdir)/src/gwy_grains_graph-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_grains_graph-24.png" "$(srcdir)/src/gwy_grains_graph-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_grains_graph-24.png" "gwy_grains_graph-24.crush.png"
	$(AM_V_at)if test -s "gwy_grains_graph-24.crush.png"; then mv -f "gwy_grains_graph-24.crush.png" "gwy_grains_graph-24.png"; fi

gwy_grains_measure-24.png: $(srcdir)/src/gwy_grains_measure-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_grains_measure-24.png" "$(srcdir)/src/gwy_grains_measure-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_grains_measure-24.png" "gwy_grains_measure-24.crush.png"
	$(AM_V_at)if test -s "gwy_grains_measure-24.crush.png"; then mv -f "gwy_grains_measure-24.crush.png" "gwy_grains_measure-24.png"; fi

gwy_grains_otsu-24.png: $(srcdir)/src/gwy_grains_otsu-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_grains_otsu-24.png" "$(srcdir)/src/gwy_grains_otsu-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_grains_otsu-24.png" "gwy_grains_otsu-24.crush.png"
	$(AM_V_at)if test -s "gwy_grains_otsu-24.crush.png"; then mv -f "gwy_grains_otsu-24.crush.png" "gwy_grains_otsu-24.png"; fi

gwy_grains_remove-24.png: $(srcdir)/src/gwy_grains_remove-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_grains_remove-24.png" "$(srcdir)/src/gwy_grains_remove-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_grains_remove-24.png" "gwy_grains_remove-24.crush.png"
	$(AM_V_at)if test -s "gwy_grains_remove-24.crush.png"; then mv -f "gwy_grains_remove-24.crush.png" "gwy_grains_remove-24.png"; fi

gwy_grains_statistics-24.png: $(srcdir)/src/gwy_grains_statistics-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_grains_statistics-24.png" "$(srcdir)/src/gwy_grains_statistics-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_grains_statistics-24.png" "gwy_grains_statistics-24.crush.png"
	$(AM_V_at)if test -s "gwy_grains_statistics-24.crush.png"; then mv -f "gwy_grains_statistics-24.crush.png" "gwy_grains_statistics-24.png"; fi

gwy_grains_water-24.png: $(srcdir)/src/gwy_grains_water-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_grains_water-24.png" "$(srcdir)/src/gwy_grains_water-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_grains_water-24.png" "gwy_grains_water-24.crush.png"
	$(AM_V_at)if test -s "gwy_grains_water-24.crush.png"; then mv -f "gwy_grains_water-24.crush.png" "gwy_grains_water-24.png"; fi

gwy_graph-24.png: $(srcdir)/src/gwy_graph-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_graph-24.png" "$(srcdir)/src/gwy_graph-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_graph-24.png" "gwy_graph-24.crush.png"
	$(AM_V_at)if test -s "gwy_graph-24.crush.png"; then mv -f "gwy_graph-24.crush.png" "gwy_graph-24.png"; fi

gwy_graph_align-24.png: $(srcdir)/src/gwy_graph_align-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_graph_align-24.png" "$(srcdir)/src/gwy_graph_align-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_graph_align-24.png" "gwy_graph_align-24.crush.png"
	$(AM_V_at)if test -s "gwy_graph_align-24.crush.png"; then mv -f "gwy_graph_align-24.crush.png" "gwy_graph_align-24.png"; fi

gwy_graph_cut-24.png: $(srcdir)/src/gwy_graph_cut-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_graph_cut-24.png" "$(srcdir)/src/gwy_graph_cut-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_graph_cut-24.png" "gwy_graph_cut-24.crush.png"
	$(AM_V_at)if test -s "gwy_graph_cut-24.crush.png"; then mv -f "gwy_graph_cut-24.crush.png" "gwy_graph_cut-24.png"; fi

gwy_graph_dos-24.png: $(srcdir)/src/gwy_graph_dos-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_graph_dos-24.png" "$(srcdir)/src/gwy_graph_dos-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_graph_dos-24.png" "gwy_graph_dos-24.crush.png"
	$(AM_V_at)if test -s "gwy_graph_dos-24.crush.png"; then mv -f "gwy_graph_dos-24.crush.png" "gwy_graph_dos-24.png"; fi

gwy_graph_export_ascii-24.png: $(srcdir)/src/gwy_graph_export_ascii-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_graph_export_ascii-24.png" "$(srcdir)/src/gwy_graph_export_ascii-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_graph_export_ascii-24.png" "gwy_graph_export_ascii-24.crush.png"
	$(AM_V_at)if test -s "gwy_graph_export_ascii-24.crush.png"; then mv -f "gwy_graph_export_ascii-24.crush.png" "gwy_graph_export_ascii-24.png"; fi

gwy_graph_export_png-24.png: $(srcdir)/src/gwy_graph_export_png-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_graph_export_png-24.png" "$(srcdir)/src/gwy_graph_export_png-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_graph_export_png-24.png" "gwy_graph_export_png-24.crush.png"
	$(AM_V_at)if test -s "gwy_graph_export_png-24.crush.png"; then mv -f "gwy_graph_export_png-24.crush.png" "gwy_graph_export_png-24.png"; fi

gwy_graph_export_vector-24.png: $(srcdir)/src/gwy_graph_export_vector-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_graph_export_vector-24.png" "$(srcdir)/src/gwy_graph_export_vector-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_graph_export_vector-24.png" "gwy_graph_export_vector-24.crush.png"
	$(AM_V_at)if test -s "gwy_graph_export_vector-24.crush.png"; then mv -f "gwy_graph_export_vector-24.crush.png" "gwy_graph_export_vector-24.png"; fi

gwy_graph_fd-24.png: $(srcdir)/src/gwy_graph_fd-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_graph_fd-24.png" "$(srcdir)/src/gwy_graph_fd-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_graph_fd-24.png" "gwy_graph_fd-24.crush.png"
	$(AM_V_at)if test -s "gwy_graph_fd-24.crush.png"; then mv -f "gwy_graph_fd-24.crush.png" "gwy_graph_fd-24.png"; fi

gwy_graph_filter-24.png: $(srcdir)/src/gwy_graph_filter-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_graph_filter-24.png" "$(srcdir)/src/gwy_graph_filter-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_graph_filter-24.png" "gwy_graph_filter-24.crush.png"
	$(AM_V_at)if test -s "gwy_graph_filter-24.crush.png"; then mv -f "gwy_graph_filter-24.crush.png" "gwy_graph_filter-24.png"; fi

gwy_graph_function-24.png: $(srcdir)/src/gwy_graph_function-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_graph_function-24.png" "$(srcdir)/src/gwy_graph_function-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_graph_function-24.png" "gwy_graph_function-24.crush.png"
	$(AM_V_at)if test -s "gwy_graph_function-24.crush.png"; then mv -f "gwy_graph_function-24.crush.png" "gwy_graph_function-24.png"; fi

gwy_graph_halfgauss-24.png: $(srcdir)/src/gwy_graph_halfgauss-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_graph_halfgauss-24.png" "$(srcdir)/src/gwy_graph_halfgauss-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_graph_halfgauss-24.png" "gwy_graph_halfgauss-24.crush.png"
	$(AM_V_at)if test -s "gwy_graph_halfgauss-24.crush.png"; then mv -f "gwy_graph_halfgauss-24.crush.png" "gwy_graph_halfgauss-24.png"; fi

gwy_graph_level-24.png: $(srcdir)/src/gwy_graph_level-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_graph_level-24.png" "$(srcdir)/src/gwy_graph_level-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_graph_level-24.png" "gwy_graph_level-24.crush.png"
	$(AM_V_at)if test -s "gwy_graph_level-24.crush.png"; then mv -f "gwy_graph_level-24.crush.png" "gwy_graph_level-24.png"; fi

gwy_graph_measure-24.png: $(srcdir)/src/gwy_graph_measure-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_graph_measure-24.png" "$(srcdir)/src/gwy_graph_measure-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_graph_measure-24.png" "gwy_graph_measure-24.crush.png"
	$(AM_V_at)if test -s "gwy_graph_measure-24.crush.png"; then mv -f "gwy_graph_measure-24.crush.png" "gwy_graph_measure-24.png"; fi

gwy_graph_palette-24.png: $(srcdir)/src/gwy_graph_palette-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_graph_palette-24.png" "$(srcdir)/src/gwy_graph_palette-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_graph_palette-24.png" "gwy_graph_palette-24.crush.png"
	$(AM_V_at)if test -s "gwy_graph_palette-24.crush.png"; then mv -f "gwy_graph_palette-24.crush.png" "gwy_graph_palette-24.png"; fi

gwy_graph_period_measure-24.png: $(srcdir)/src/gwy_graph_period_measure-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_graph_period_measure-24.png" "$(srcdir)/src/gwy_graph_period_measure-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_graph_period_measure-24.png" "gwy_graph_period_measure-24.crush.png"
	$(AM_V_at)if test -s "gwy_graph_period_measure-24.crush.png"; then mv -f "gwy_graph_period_measure-24.crush.png" "gwy_graph_period_measure-24.png"; fi

gwy_graph_pointer-24.png: $(srcdir)/src/gwy_graph_pointer-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_graph_pointer-24.png" "$(srcdir)/src/gwy_graph_pointer-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_graph_pointer-24.png" "gwy_graph_pointer-24.crush.png"
	$(AM_V_at)if test -s "gwy_graph_pointer-24.crush.png"; then mv -f "gwy_graph_pointer-24.crush.png" "gwy_graph_pointer-24.png"; fi

gwy_graph_ruler-24.png: $(srcdir)/src/gwy_graph_ruler-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_graph_ruler-24.png" "$(srcdir)/src/gwy_graph_ruler-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_graph_ruler-24.png" "gwy_graph_ruler-24.crush.png"
	$(AM_V_at)if test -s "gwy_graph_ruler-24.crush.png"; then mv -f "gwy_graph_ruler-24.crush.png" "gwy_graph_ruler-24.png"; fi

gwy_graph_statistics-24.png: $(srcdir)/src/gwy_graph_statistics-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_graph_statistics-24.png" "$(srcdir)/src/gwy_graph_statistics-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_graph_statistics-24.png" "gwy_graph_statistics-24.crush.png"
	$(AM_V_at)if test -s "gwy_graph_statistics-24.crush.png"; then mv -f "gwy_graph_statistics-24.crush.png" "gwy_graph_statistics-24.png"; fi

gwy_graph_terrace_measure-24.png: $(srcdir)/src/gwy_graph_terrace_measure-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_graph_terrace_measure-24.png" "$(srcdir)/src/gwy_graph_terrace_measure-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_graph_terrace_measure-24.png" "gwy_graph_terrace_measure-24.crush.png"
	$(AM_V_at)if test -s "gwy_graph_terrace_measure-24.crush.png"; then mv -f "gwy_graph_terrace_measure-24.crush.png" "gwy_graph_terrace_measure-24.png"; fi

gwy_graph_vertical-24.png: $(srcdir)/src/gwy_graph_vertical-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_graph_vertical-24.png" "$(srcdir)/src/gwy_graph_vertical-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_graph_vertical-24.png" "gwy_graph_vertical-24.crush.png"
	$(AM_V_at)if test -s "gwy_graph_vertical-24.crush.png"; then mv -f "gwy_graph_vertical-24.crush.png" "gwy_graph_vertical-24.png"; fi

gwy_graph_zoom_fit-24.png: $(srcdir)/src/gwy_graph_zoom_fit-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_graph_zoom_fit-24.png" "$(srcdir)/src/gwy_graph_zoom_fit-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_graph_zoom_fit-24.png" "gwy_graph_zoom_fit-24.crush.png"
	$(AM_V_at)if test -s "gwy_graph_zoom_fit-24.crush.png"; then mv -f "gwy_graph_zoom_fit-24.crush.png" "gwy_graph_zoom_fit-24.png"; fi

gwy_graph_zoom_in-24.png: $(srcdir)/src/gwy_graph_zoom_in-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_graph_zoom_in-24.png" "$(srcdir)/src/gwy_graph_zoom_in-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_graph_zoom_in-24.png" "gwy_graph_zoom_in-24.crush.png"
	$(AM_V_at)if test -s "gwy_graph_zoom_in-24.crush.png"; then mv -f "gwy_graph_zoom_in-24.crush.png" "gwy_graph_zoom_in-24.png"; fi

gwy_graph_zoom_out-24.png: $(srcdir)/src/gwy_graph_zoom_out-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_graph_zoom_out-24.png" "$(srcdir)/src/gwy_graph_zoom_out-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_graph_zoom_out-24.png" "gwy_graph_zoom_out-24.crush.png"
	$(AM_V_at)if test -s "gwy_graph_zoom_out-24.crush.png"; then mv -f "gwy_graph_zoom_out-24.crush.png" "gwy_graph_zoom_out-24.png"; fi

gwy_hold_selection_clear-18.png: $(srcdir)/src/gwy_hold_selection_clear-18.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=18 --export-height=18 $(INKSCAPE_EXPORT_PNGFILE)="gwy_hold_selection_clear-18.png" "$(srcdir)/src/gwy_hold_selection_clear-18.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_hold_selection_clear-18.png" "gwy_hold_selection_clear-18.crush.png"
	$(AM_V_at)if test -s "gwy_hold_selection_clear-18.crush.png"; then mv -f "gwy_hold_selection_clear-18.crush.png" "gwy_hold_selection_clear-18.png"; fi

gwy_hold_selection_replace-18.png: $(srcdir)/src/gwy_hold_selection_replace-18.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=18 --export-height=18 $(INKSCAPE_EXPORT_PNGFILE)="gwy_hold_selection_replace-18.png" "$(srcdir)/src/gwy_hold_selection_replace-18.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_hold_selection_replace-18.png" "gwy_hold_selection_replace-18.crush.png"
	$(AM_V_at)if test -s "gwy_hold_selection_replace-18.crush.png"; then mv -f "gwy_hold_selection_replace-18.crush.png" "gwy_hold_selection_replace-18.png"; fi

gwy_hough-24.png: $(srcdir)/src/gwy_hough-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_hough-24.png" "$(srcdir)/src/gwy_hough-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_hough-24.png" "gwy_hough-24.crush.png"
	$(AM_V_at)if test -s "gwy_hough-24.crush.png"; then mv -f "gwy_hough-24.crush.png" "gwy_hough-24.png"; fi

gwy_image_relation-24.png: $(srcdir)/src/gwy_image_relation-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_image_relation-24.png" "$(srcdir)/src/gwy_image_relation-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_image_relation-24.png" "gwy_image_relation-24.crush.png"
	$(AM_V_at)if test -s "gwy_image_relation-24.crush.png"; then mv -f "gwy_image_relation-24.crush.png" "gwy_image_relation-24.png"; fi

gwy_immerse-24.png: $(srcdir)/src/gwy_immerse-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_immerse-24.png" "$(srcdir)/src/gwy_immerse-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_immerse-24.png" "gwy_immerse-24.crush.png"
	$(AM_V_at)if test -s "gwy_immerse-24.crush.png"; then mv -f "gwy_immerse-24.crush.png" "gwy_immerse-24.png"; fi

gwy_iso_roughness-24.png: $(srcdir)/src/gwy_iso_roughness-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_iso_roughness-24.png" "$(srcdir)/src/gwy_iso_roughness-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_iso_roughness-24.png" "gwy_iso_roughness-24.crush.png"
	$(AM_V_at)if test -s "gwy_iso_roughness-24.crush.png"; then mv -f "gwy_iso_roughness-24.crush.png" "gwy_iso_roughness-24.png"; fi

gwy_italic-20.png: $(srcdir)/src/gwy_italic-20.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=20 --export-height=20 $(INKSCAPE_EXPORT_PNGFILE)="gwy_italic-20.png" "$(srcdir)/src/gwy_italic-20.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_italic-20.png" "gwy_italic-20.crush.png"
	$(AM_V_at)if test -s "gwy_italic-20.crush.png"; then mv -f "gwy_italic-20.crush.png" "gwy_italic-20.png"; fi

gwy_less-24.png: $(srcdir)/src/gwy_less-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_less-24.png" "$(srcdir)/src/gwy_less-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_less-24.png" "gwy_less-24.crush.png"
	$(AM_V_at)if test -s "gwy_less-24.crush.png"; then mv -f "gwy_less-24.crush.png" "gwy_less-24.png"; fi

gwy_level-24.png: $(srcdir)/src/gwy_level-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_level-24.png" "$(srcdir)/src/gwy_level-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_level-24.png" "gwy_level-24.crush.png"
	$(AM_V_at)if test -s "gwy_level-24.crush.png"; then mv -f "gwy_level-24.crush.png" "gwy_level-24.png"; fi

gwy_level_flatten_base-24.png: $(srcdir)/src/gwy_level_flatten_base-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_level_flatten_base-24.png" "$(srcdir)/src/gwy_level_flatten_base-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_level_flatten_base-24.png" "gwy_level_flatten_base-24.crush.png"
	$(AM_V_at)if test -s "gwy_level_flatten_base-24.crush.png"; then mv -f "gwy_level_flatten_base-24.crush.png" "gwy_level_flatten_base-24.png"; fi

gwy_level_median-24.png: $(srcdir)/src/gwy_level_median-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_level_median-24.png" "$(srcdir)/src/gwy_level_median-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_level_median-24.png" "gwy_level_median-24.crush.png"
	$(AM_V_at)if test -s "gwy_level_median-24.crush.png"; then mv -f "gwy_level_median-24.crush.png" "gwy_level_median-24.png"; fi

gwy_level_triangle-24.png: $(srcdir)/src/gwy_level_triangle-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_level_triangle-24.png" "$(srcdir)/src/gwy_level_triangle-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_level_triangle-24.png" "gwy_level_triangle-24.crush.png"
	$(AM_V_at)if test -s "gwy_level_triangle-24.crush.png"; then mv -f "gwy_level_triangle-24.crush.png" "gwy_level_triangle-24.png"; fi

gwy_light_rotate-24.png: $(srcdir)/src/gwy_light_rotate-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_light_rotate-24.png" "$(srcdir)/src/gwy_light_rotate-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_light_rotate-24.png" "gwy_light_rotate-24.crush.png"
	$(AM_V_at)if test -s "gwy_light_rotate-24.crush.png"; then mv -f "gwy_light_rotate-24.crush.png" "gwy_light_rotate-24.png"; fi

gwy_limit_range-24.png: $(srcdir)/src/gwy_limit_range-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_limit_range-24.png" "$(srcdir)/src/gwy_limit_range-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_limit_range-24.png" "gwy_limit_range-24.crush.png"
	$(AM_V_at)if test -s "gwy_limit_range-24.crush.png"; then mv -f "gwy_limit_range-24.crush.png" "gwy_limit_range-24.png"; fi

gwy_line_level-24.png: $(srcdir)/src/gwy_line_level-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_line_level-24.png" "$(srcdir)/src/gwy_line_level-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_line_level-24.png" "gwy_line_level-24.crush.png"
	$(AM_V_at)if test -s "gwy_line_level-24.crush.png"; then mv -f "gwy_line_level-24.crush.png" "gwy_line_level-24.png"; fi

gwy_load_debug-20.png: $(srcdir)/src/gwy_load_debug-20.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=20 --export-height=20 $(INKSCAPE_EXPORT_PNGFILE)="gwy_load_debug-20.png" "$(srcdir)/src/gwy_load_debug-20.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_load_debug-20.png" "gwy_load_debug-20.crush.png"
	$(AM_V_at)if test -s "gwy_load_debug-20.crush.png"; then mv -f "gwy_load_debug-20.crush.png" "gwy_load_debug-20.png"; fi

gwy_load_info-20.png: $(srcdir)/src/gwy_load_info-20.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=20 --export-height=20 $(INKSCAPE_EXPORT_PNGFILE)="gwy_load_info-20.png" "$(srcdir)/src/gwy_load_info-20.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_load_info-20.png" "gwy_load_info-20.crush.png"
	$(AM_V_at)if test -s "gwy_load_info-20.crush.png"; then mv -f "gwy_load_info-20.crush.png" "gwy_load_info-20.png"; fi

gwy_load_warning-20.png: $(srcdir)/src/gwy_load_warning-20.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=20 --export-height=20 $(INKSCAPE_EXPORT_PNGFILE)="gwy_load_warning-20.png" "$(srcdir)/src/gwy_load_warning-20.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_load_warning-20.png" "gwy_load_warning-20.crush.png"
	$(AM_V_at)if test -s "gwy_load_warning-20.crush.png"; then mv -f "gwy_load_warning-20.crush.png" "gwy_load_warning-20.png"; fi

gwy_local_slope-24.png: $(srcdir)/src/gwy_local_slope-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_local_slope-24.png" "$(srcdir)/src/gwy_local_slope-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_local_slope-24.png" "gwy_local_slope-24.crush.png"
	$(AM_V_at)if test -s "gwy_local_slope-24.crush.png"; then mv -f "gwy_local_slope-24.crush.png" "gwy_local_slope-24.png"; fi

gwy_logscale_horizontal-24.png: $(srcdir)/src/gwy_logscale_horizontal-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_logscale_horizontal-24.png" "$(srcdir)/src/gwy_logscale_horizontal-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_logscale_horizontal-24.png" "gwy_logscale_horizontal-24.crush.png"
	$(AM_V_at)if test -s "gwy_logscale_horizontal-24.crush.png"; then mv -f "gwy_logscale_horizontal-24.crush.png" "gwy_logscale_horizontal-24.png"; fi

gwy_logscale_vertical-24.png: $(srcdir)/src/gwy_logscale_vertical-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_logscale_vertical-24.png" "$(srcdir)/src/gwy_logscale_vertical-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_logscale_vertical-24.png" "gwy_logscale_vertical-24.crush.png"
	$(AM_V_at)if test -s "gwy_logscale_vertical-24.crush.png"; then mv -f "gwy_logscale_vertical-24.crush.png" "gwy_logscale_vertical-24.png"; fi

gwy_mark_outliers-24.png: $(srcdir)/src/gwy_mark_outliers-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mark_outliers-24.png" "$(srcdir)/src/gwy_mark_outliers-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mark_outliers-24.png" "gwy_mark_outliers-24.crush.png"
	$(AM_V_at)if test -s "gwy_mark_outliers-24.crush.png"; then mv -f "gwy_mark_outliers-24.crush.png" "gwy_mark_outliers-24.png"; fi

gwy_mark_scars-24.png: $(srcdir)/src/gwy_mark_scars-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mark_scars-24.png" "$(srcdir)/src/gwy_mark_scars-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mark_scars-24.png" "gwy_mark_scars-24.crush.png"
	$(AM_V_at)if test -s "gwy_mark_scars-24.crush.png"; then mv -f "gwy_mark_scars-24.crush.png" "gwy_mark_scars-24.png"; fi

gwy_mark_with-24.png: $(srcdir)/src/gwy_mark_with-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mark_with-24.png" "$(srcdir)/src/gwy_mark_with-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mark_with-24.png" "gwy_mark_with-24.crush.png"
	$(AM_V_at)if test -s "gwy_mark_with-24.crush.png"; then mv -f "gwy_mark_with-24.crush.png" "gwy_mark_with-24.png"; fi

gwy_mask-16.png: $(srcdir)/src/gwy_mask-16.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=16 --export-height=16 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mask-16.png" "$(srcdir)/src/gwy_mask-16.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mask-16.png" "gwy_mask-16.crush.png"
	$(AM_V_at)if test -s "gwy_mask-16.crush.png"; then mv -f "gwy_mask-16.crush.png" "gwy_mask-16.png"; fi

gwy_mask-24.png: $(srcdir)/src/gwy_mask-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mask-24.png" "$(srcdir)/src/gwy_mask-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mask-24.png" "gwy_mask-24.crush.png"
	$(AM_V_at)if test -s "gwy_mask-24.crush.png"; then mv -f "gwy_mask-24.crush.png" "gwy_mask-24.png"; fi

gwy_mask_add-24.png: $(srcdir)/src/gwy_mask_add-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mask_add-24.png" "$(srcdir)/src/gwy_mask_add-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mask_add-24.png" "gwy_mask_add-24.crush.png"
	$(AM_V_at)if test -s "gwy_mask_add-24.crush.png"; then mv -f "gwy_mask_add-24.crush.png" "gwy_mask_add-24.png"; fi

gwy_mask_circle-24.png: $(srcdir)/src/gwy_mask_circle-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mask_circle-24.png" "$(srcdir)/src/gwy_mask_circle-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mask_circle-24.png" "gwy_mask_circle-24.crush.png"
	$(AM_V_at)if test -s "gwy_mask_circle-24.crush.png"; then mv -f "gwy_mask_circle-24.crush.png" "gwy_mask_circle-24.png"; fi

gwy_mask_circle_exclusive-24.png: $(srcdir)/src/gwy_mask_circle_exclusive-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mask_circle_exclusive-24.png" "$(srcdir)/src/gwy_mask_circle_exclusive-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mask_circle_exclusive-24.png" "gwy_mask_circle_exclusive-24.crush.png"
	$(AM_V_at)if test -s "gwy_mask_circle_exclusive-24.crush.png"; then mv -f "gwy_mask_circle_exclusive-24.crush.png" "gwy_mask_circle_exclusive-24.png"; fi

gwy_mask_circle_inclusive-24.png: $(srcdir)/src/gwy_mask_circle_inclusive-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mask_circle_inclusive-24.png" "$(srcdir)/src/gwy_mask_circle_inclusive-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mask_circle_inclusive-24.png" "gwy_mask_circle_inclusive-24.crush.png"
	$(AM_V_at)if test -s "gwy_mask_circle_inclusive-24.crush.png"; then mv -f "gwy_mask_circle_inclusive-24.crush.png" "gwy_mask_circle_inclusive-24.png"; fi

gwy_mask_distribute-24.png: $(srcdir)/src/gwy_mask_distribute-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mask_distribute-24.png" "$(srcdir)/src/gwy_mask_distribute-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mask_distribute-24.png" "gwy_mask_distribute-24.crush.png"
	$(AM_V_at)if test -s "gwy_mask_distribute-24.crush.png"; then mv -f "gwy_mask_distribute-24.crush.png" "gwy_mask_distribute-24.png"; fi

gwy_mask_editor-24.png: $(srcdir)/src/gwy_mask_editor-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mask_editor-24.png" "$(srcdir)/src/gwy_mask_editor-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mask_editor-24.png" "gwy_mask_editor-24.crush.png"
	$(AM_V_at)if test -s "gwy_mask_editor-24.crush.png"; then mv -f "gwy_mask_editor-24.crush.png" "gwy_mask_editor-24.png"; fi

gwy_mask_exclude-24.png: $(srcdir)/src/gwy_mask_exclude-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mask_exclude-24.png" "$(srcdir)/src/gwy_mask_exclude-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mask_exclude-24.png" "gwy_mask_exclude-24.crush.png"
	$(AM_V_at)if test -s "gwy_mask_exclude-24.crush.png"; then mv -f "gwy_mask_exclude-24.crush.png" "gwy_mask_exclude-24.png"; fi

gwy_mask_exclude_circle-24.png: $(srcdir)/src/gwy_mask_exclude_circle-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mask_exclude_circle-24.png" "$(srcdir)/src/gwy_mask_exclude_circle-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mask_exclude_circle-24.png" "gwy_mask_exclude_circle-24.crush.png"
	$(AM_V_at)if test -s "gwy_mask_exclude_circle-24.crush.png"; then mv -f "gwy_mask_exclude_circle-24.crush.png" "gwy_mask_exclude_circle-24.png"; fi

gwy_mask_extract-24.png: $(srcdir)/src/gwy_mask_extract-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mask_extract-24.png" "$(srcdir)/src/gwy_mask_extract-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mask_extract-24.png" "gwy_mask_extract-24.crush.png"
	$(AM_V_at)if test -s "gwy_mask_extract-24.crush.png"; then mv -f "gwy_mask_extract-24.crush.png" "gwy_mask_extract-24.png"; fi

gwy_mask_fill_draw-24.png: $(srcdir)/src/gwy_mask_fill_draw-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mask_fill_draw-24.png" "$(srcdir)/src/gwy_mask_fill_draw-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mask_fill_draw-24.png" "gwy_mask_fill_draw-24.crush.png"
	$(AM_V_at)if test -s "gwy_mask_fill_draw-24.crush.png"; then mv -f "gwy_mask_fill_draw-24.crush.png" "gwy_mask_fill_draw-24.png"; fi

gwy_mask_fill_erase-24.png: $(srcdir)/src/gwy_mask_fill_erase-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mask_fill_erase-24.png" "$(srcdir)/src/gwy_mask_fill_erase-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mask_fill_erase-24.png" "gwy_mask_fill_erase-24.crush.png"
	$(AM_V_at)if test -s "gwy_mask_fill_erase-24.crush.png"; then mv -f "gwy_mask_fill_erase-24.crush.png" "gwy_mask_fill_erase-24.png"; fi

gwy_mask_grow-24.png: $(srcdir)/src/gwy_mask_grow-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mask_grow-24.png" "$(srcdir)/src/gwy_mask_grow-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mask_grow-24.png" "gwy_mask_grow-24.crush.png"
	$(AM_V_at)if test -s "gwy_mask_grow-24.crush.png"; then mv -f "gwy_mask_grow-24.crush.png" "gwy_mask_grow-24.png"; fi

gwy_mask_intersect-24.png: $(srcdir)/src/gwy_mask_intersect-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mask_intersect-24.png" "$(srcdir)/src/gwy_mask_intersect-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mask_intersect-24.png" "gwy_mask_intersect-24.crush.png"
	$(AM_V_at)if test -s "gwy_mask_intersect-24.crush.png"; then mv -f "gwy_mask_intersect-24.crush.png" "gwy_mask_intersect-24.png"; fi

gwy_mask_invert-24.png: $(srcdir)/src/gwy_mask_invert-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mask_invert-24.png" "$(srcdir)/src/gwy_mask_invert-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mask_invert-24.png" "gwy_mask_invert-24.crush.png"
	$(AM_V_at)if test -s "gwy_mask_invert-24.crush.png"; then mv -f "gwy_mask_invert-24.crush.png" "gwy_mask_invert-24.png"; fi

gwy_mask_line-24.png: $(srcdir)/src/gwy_mask_line-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mask_line-24.png" "$(srcdir)/src/gwy_mask_line-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mask_line-24.png" "gwy_mask_line-24.crush.png"
	$(AM_V_at)if test -s "gwy_mask_line-24.crush.png"; then mv -f "gwy_mask_line-24.crush.png" "gwy_mask_line-24.png"; fi

gwy_mask_morph-24.png: $(srcdir)/src/gwy_mask_morph-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mask_morph-24.png" "$(srcdir)/src/gwy_mask_morph-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mask_morph-24.png" "gwy_mask_morph-24.crush.png"
	$(AM_V_at)if test -s "gwy_mask_morph-24.crush.png"; then mv -f "gwy_mask_morph-24.crush.png" "gwy_mask_morph-24.png"; fi

gwy_mask_noisify-24.png: $(srcdir)/src/gwy_mask_noisify-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mask_noisify-24.png" "$(srcdir)/src/gwy_mask_noisify-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mask_noisify-24.png" "gwy_mask_noisify-24.crush.png"
	$(AM_V_at)if test -s "gwy_mask_noisify-24.crush.png"; then mv -f "gwy_mask_noisify-24.crush.png" "gwy_mask_noisify-24.png"; fi

gwy_mask_paint_draw-24.png: $(srcdir)/src/gwy_mask_paint_draw-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mask_paint_draw-24.png" "$(srcdir)/src/gwy_mask_paint_draw-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mask_paint_draw-24.png" "gwy_mask_paint_draw-24.crush.png"
	$(AM_V_at)if test -s "gwy_mask_paint_draw-24.crush.png"; then mv -f "gwy_mask_paint_draw-24.crush.png" "gwy_mask_paint_draw-24.png"; fi

gwy_mask_paint_erase-24.png: $(srcdir)/src/gwy_mask_paint_erase-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mask_paint_erase-24.png" "$(srcdir)/src/gwy_mask_paint_erase-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mask_paint_erase-24.png" "gwy_mask_paint_erase-24.crush.png"
	$(AM_V_at)if test -s "gwy_mask_paint_erase-24.crush.png"; then mv -f "gwy_mask_paint_erase-24.crush.png" "gwy_mask_paint_erase-24.png"; fi

gwy_mask_rect_exclusive-24.png: $(srcdir)/src/gwy_mask_rect_exclusive-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mask_rect_exclusive-24.png" "$(srcdir)/src/gwy_mask_rect_exclusive-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mask_rect_exclusive-24.png" "gwy_mask_rect_exclusive-24.crush.png"
	$(AM_V_at)if test -s "gwy_mask_rect_exclusive-24.crush.png"; then mv -f "gwy_mask_rect_exclusive-24.crush.png" "gwy_mask_rect_exclusive-24.png"; fi

gwy_mask_rect_inclusive-24.png: $(srcdir)/src/gwy_mask_rect_inclusive-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mask_rect_inclusive-24.png" "$(srcdir)/src/gwy_mask_rect_inclusive-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mask_rect_inclusive-24.png" "gwy_mask_rect_inclusive-24.crush.png"
	$(AM_V_at)if test -s "gwy_mask_rect_inclusive-24.crush.png"; then mv -f "gwy_mask_rect_inclusive-24.crush.png" "gwy_mask_rect_inclusive-24.png"; fi

gwy_mask_remove-24.png: $(srcdir)/src/gwy_mask_remove-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mask_remove-24.png" "$(srcdir)/src/gwy_mask_remove-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mask_remove-24.png" "gwy_mask_remove-24.crush.png"
	$(AM_V_at)if test -s "gwy_mask_remove-24.crush.png"; then mv -f "gwy_mask_remove-24.crush.png" "gwy_mask_remove-24.png"; fi

gwy_mask_set-24.png: $(srcdir)/src/gwy_mask_set-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mask_set-24.png" "$(srcdir)/src/gwy_mask_set-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mask_set-24.png" "gwy_mask_set-24.crush.png"
	$(AM_V_at)if test -s "gwy_mask_set-24.crush.png"; then mv -f "gwy_mask_set-24.crush.png" "gwy_mask_set-24.png"; fi

gwy_mask_shift-24.png: $(srcdir)/src/gwy_mask_shift-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mask_shift-24.png" "$(srcdir)/src/gwy_mask_shift-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mask_shift-24.png" "gwy_mask_shift-24.crush.png"
	$(AM_V_at)if test -s "gwy_mask_shift-24.crush.png"; then mv -f "gwy_mask_shift-24.crush.png" "gwy_mask_shift-24.png"; fi

gwy_mask_shrink-24.png: $(srcdir)/src/gwy_mask_shrink-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mask_shrink-24.png" "$(srcdir)/src/gwy_mask_shrink-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mask_shrink-24.png" "gwy_mask_shrink-24.crush.png"
	$(AM_V_at)if test -s "gwy_mask_shrink-24.crush.png"; then mv -f "gwy_mask_shrink-24.crush.png" "gwy_mask_shrink-24.png"; fi

gwy_mask_subtract-24.png: $(srcdir)/src/gwy_mask_subtract-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mask_subtract-24.png" "$(srcdir)/src/gwy_mask_subtract-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mask_subtract-24.png" "gwy_mask_subtract-24.crush.png"
	$(AM_V_at)if test -s "gwy_mask_subtract-24.crush.png"; then mv -f "gwy_mask_subtract-24.crush.png" "gwy_mask_subtract-24.png"; fi

gwy_mask_thin-24.png: $(srcdir)/src/gwy_mask_thin-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mask_thin-24.png" "$(srcdir)/src/gwy_mask_thin-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mask_thin-24.png" "gwy_mask_thin-24.crush.png"
	$(AM_V_at)if test -s "gwy_mask_thin-24.crush.png"; then mv -f "gwy_mask_thin-24.crush.png" "gwy_mask_thin-24.png"; fi

gwy_measure_lattice-24.png: $(srcdir)/src/gwy_measure_lattice-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_measure_lattice-24.png" "$(srcdir)/src/gwy_measure_lattice-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_measure_lattice-24.png" "gwy_measure_lattice-24.crush.png"
	$(AM_V_at)if test -s "gwy_measure_lattice-24.crush.png"; then mv -f "gwy_measure_lattice-24.crush.png" "gwy_measure_lattice-24.png"; fi

gwy_merge-24.png: $(srcdir)/src/gwy_merge-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_merge-24.png" "$(srcdir)/src/gwy_merge-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_merge-24.png" "gwy_merge-24.crush.png"
	$(AM_V_at)if test -s "gwy_merge-24.crush.png"; then mv -f "gwy_merge-24.crush.png" "gwy_merge-24.png"; fi

gwy_mfm_convert_to_force-24.png: $(srcdir)/src/gwy_mfm_convert_to_force-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mfm_convert_to_force-24.png" "$(srcdir)/src/gwy_mfm_convert_to_force-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mfm_convert_to_force-24.png" "gwy_mfm_convert_to_force-24.crush.png"
	$(AM_V_at)if test -s "gwy_mfm_convert_to_force-24.crush.png"; then mv -f "gwy_mfm_convert_to_force-24.crush.png" "gwy_mfm_convert_to_force-24.png"; fi

gwy_mfm_current_line-24.png: $(srcdir)/src/gwy_mfm_current_line-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mfm_current_line-24.png" "$(srcdir)/src/gwy_mfm_current_line-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mfm_current_line-24.png" "gwy_mfm_current_line-24.crush.png"
	$(AM_V_at)if test -s "gwy_mfm_current_line-24.crush.png"; then mv -f "gwy_mfm_current_line-24.crush.png" "gwy_mfm_current_line-24.png"; fi

gwy_mfm_field_find_shift-24.png: $(srcdir)/src/gwy_mfm_field_find_shift-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mfm_field_find_shift-24.png" "$(srcdir)/src/gwy_mfm_field_find_shift-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mfm_field_find_shift-24.png" "gwy_mfm_field_find_shift-24.crush.png"
	$(AM_V_at)if test -s "gwy_mfm_field_find_shift-24.crush.png"; then mv -f "gwy_mfm_field_find_shift-24.crush.png" "gwy_mfm_field_find_shift-24.png"; fi

gwy_mfm_field_shift-24.png: $(srcdir)/src/gwy_mfm_field_shift-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mfm_field_shift-24.png" "$(srcdir)/src/gwy_mfm_field_shift-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mfm_field_shift-24.png" "gwy_mfm_field_shift-24.crush.png"
	$(AM_V_at)if test -s "gwy_mfm_field_shift-24.crush.png"; then mv -f "gwy_mfm_field_shift-24.crush.png" "gwy_mfm_field_shift-24.png"; fi

gwy_mfm_parallel-24.png: $(srcdir)/src/gwy_mfm_parallel-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mfm_parallel-24.png" "$(srcdir)/src/gwy_mfm_parallel-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mfm_parallel-24.png" "gwy_mfm_parallel-24.crush.png"
	$(AM_V_at)if test -s "gwy_mfm_parallel-24.crush.png"; then mv -f "gwy_mfm_parallel-24.crush.png" "gwy_mfm_parallel-24.png"; fi

gwy_mfm_perpendicular-24.png: $(srcdir)/src/gwy_mfm_perpendicular-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mfm_perpendicular-24.png" "$(srcdir)/src/gwy_mfm_perpendicular-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mfm_perpendicular-24.png" "gwy_mfm_perpendicular-24.crush.png"
	$(AM_V_at)if test -s "gwy_mfm_perpendicular-24.crush.png"; then mv -f "gwy_mfm_perpendicular-24.crush.png" "gwy_mfm_perpendicular-24.png"; fi

gwy_more-24.png: $(srcdir)/src/gwy_more-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_more-24.png" "$(srcdir)/src/gwy_more-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_more-24.png" "gwy_more-24.crush.png"
	$(AM_V_at)if test -s "gwy_more-24.crush.png"; then mv -f "gwy_more-24.crush.png" "gwy_more-24.png"; fi

gwy_mutual_crop-24.png: $(srcdir)/src/gwy_mutual_crop-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_mutual_crop-24.png" "$(srcdir)/src/gwy_mutual_crop-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_mutual_crop-24.png" "gwy_mutual_crop-24.crush.png"
	$(AM_V_at)if test -s "gwy_mutual_crop-24.crush.png"; then mv -f "gwy_mutual_crop-24.crush.png" "gwy_mutual_crop-24.png"; fi

gwy_neural_apply-24.png: $(srcdir)/src/gwy_neural_apply-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_neural_apply-24.png" "$(srcdir)/src/gwy_neural_apply-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_neural_apply-24.png" "gwy_neural_apply-24.crush.png"
	$(AM_V_at)if test -s "gwy_neural_apply-24.crush.png"; then mv -f "gwy_neural_apply-24.crush.png" "gwy_neural_apply-24.png"; fi

gwy_neural_train-24.png: $(srcdir)/src/gwy_neural_train-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_neural_train-24.png" "$(srcdir)/src/gwy_neural_train-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_neural_train-24.png" "gwy_neural_train-24.crush.png"
	$(AM_V_at)if test -s "gwy_neural_train-24.crush.png"; then mv -f "gwy_neural_train-24.crush.png" "gwy_neural_train-24.png"; fi

gwy_next-24.png: $(srcdir)/src/gwy_next-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_next-24.png" "$(srcdir)/src/gwy_next-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_next-24.png" "gwy_next-24.crush.png"
	$(AM_V_at)if test -s "gwy_next-24.crush.png"; then mv -f "gwy_next-24.crush.png" "gwy_next-24.png"; fi

gwy_null_offsets-24.png: $(srcdir)/src/gwy_null_offsets-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_null_offsets-24.png" "$(srcdir)/src/gwy_null_offsets-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_null_offsets-24.png" "gwy_null_offsets-24.crush.png"
	$(AM_V_at)if test -s "gwy_null_offsets-24.crush.png"; then mv -f "gwy_null_offsets-24.crush.png" "gwy_null_offsets-24.png"; fi

gwy_palettes-16.png: $(srcdir)/src/gwy_palettes-16.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=16 --export-height=16 $(INKSCAPE_EXPORT_PNGFILE)="gwy_palettes-16.png" "$(srcdir)/src/gwy_palettes-16.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_palettes-16.png" "gwy_palettes-16.crush.png"
	$(AM_V_at)if test -s "gwy_palettes-16.crush.png"; then mv -f "gwy_palettes-16.crush.png" "gwy_palettes-16.png"; fi

gwy_palettes-24.png: $(srcdir)/src/gwy_palettes-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_palettes-24.png" "$(srcdir)/src/gwy_palettes-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_palettes-24.png" "gwy_palettes-24.crush.png"
	$(AM_V_at)if test -s "gwy_palettes-24.crush.png"; then mv -f "gwy_palettes-24.crush.png" "gwy_palettes-24.png"; fi

gwy_path_level-24.png: $(srcdir)/src/gwy_path_level-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_path_level-24.png" "$(srcdir)/src/gwy_path_level-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_path_level-24.png" "gwy_path_level-24.crush.png"
	$(AM_V_at)if test -s "gwy_path_level-24.crush.png"; then mv -f "gwy_path_level-24.crush.png" "gwy_path_level-24.png"; fi

gwy_perspective_distort-24.png: $(srcdir)/src/gwy_perspective_distort-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_perspective_distort-24.png" "$(srcdir)/src/gwy_perspective_distort-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_perspective_distort-24.png" "gwy_perspective_distort-24.crush.png"
	$(AM_V_at)if test -s "gwy_perspective_distort-24.crush.png"; then mv -f "gwy_perspective_distort-24.crush.png" "gwy_perspective_distort-24.png"; fi

gwy_pointer_measure-24.png: $(srcdir)/src/gwy_pointer_measure-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_pointer_measure-24.png" "$(srcdir)/src/gwy_pointer_measure-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_pointer_measure-24.png" "gwy_pointer_measure-24.crush.png"
	$(AM_V_at)if test -s "gwy_pointer_measure-24.crush.png"; then mv -f "gwy_pointer_measure-24.crush.png" "gwy_pointer_measure-24.png"; fi

gwy_poly_distort-24.png: $(srcdir)/src/gwy_poly_distort-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_poly_distort-24.png" "$(srcdir)/src/gwy_poly_distort-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_poly_distort-24.png" "gwy_poly_distort-24.crush.png"
	$(AM_V_at)if test -s "gwy_poly_distort-24.crush.png"; then mv -f "gwy_poly_distort-24.crush.png" "gwy_poly_distort-24.png"; fi

gwy_polynom-24.png: $(srcdir)/src/gwy_polynom-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_polynom-24.png" "$(srcdir)/src/gwy_polynom-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_polynom-24.png" "gwy_polynom-24.crush.png"
	$(AM_V_at)if test -s "gwy_polynom-24.crush.png"; then mv -f "gwy_polynom-24.crush.png" "gwy_polynom-24.png"; fi

gwy_polynom_level-24.png: $(srcdir)/src/gwy_polynom_level-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_polynom_level-24.png" "$(srcdir)/src/gwy_polynom_level-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_polynom_level-24.png" "gwy_polynom_level-24.crush.png"
	$(AM_V_at)if test -s "gwy_polynom_level-24.crush.png"; then mv -f "gwy_polynom_level-24.crush.png" "gwy_polynom_level-24.png"; fi

gwy_previous-24.png: $(srcdir)/src/gwy_previous-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_previous-24.png" "$(srcdir)/src/gwy_previous-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_previous-24.png" "gwy_previous-24.crush.png"
	$(AM_V_at)if test -s "gwy_previous-24.crush.png"; then mv -f "gwy_previous-24.crush.png" "gwy_previous-24.png"; fi

gwy_profile-24.png: $(srcdir)/src/gwy_profile-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_profile-24.png" "$(srcdir)/src/gwy_profile-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_profile-24.png" "gwy_profile-24.crush.png"
	$(AM_V_at)if test -s "gwy_profile-24.crush.png"; then mv -f "gwy_profile-24.crush.png" "gwy_profile-24.png"; fi

gwy_profile_multiple-24.png: $(srcdir)/src/gwy_profile_multiple-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_profile_multiple-24.png" "$(srcdir)/src/gwy_profile_multiple-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_profile_multiple-24.png" "gwy_profile_multiple-24.crush.png"
	$(AM_V_at)if test -s "gwy_profile_multiple-24.crush.png"; then mv -f "gwy_profile_multiple-24.crush.png" "gwy_profile_multiple-24.png"; fi

gwy_psdf_log_phi-24.png: $(srcdir)/src/gwy_psdf_log_phi-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_psdf_log_phi-24.png" "$(srcdir)/src/gwy_psdf_log_phi-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_psdf_log_phi-24.png" "gwy_psdf_log_phi-24.crush.png"
	$(AM_V_at)if test -s "gwy_psdf_log_phi-24.crush.png"; then mv -f "gwy_psdf_log_phi-24.crush.png" "gwy_psdf_log_phi-24.png"; fi

gwy_pygwy-24.png: $(srcdir)/src/gwy_pygwy-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_pygwy-24.png" "$(srcdir)/src/gwy_pygwy-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_pygwy-24.png" "gwy_pygwy-24.crush.png"
	$(AM_V_at)if test -s "gwy_pygwy-24.crush.png"; then mv -f "gwy_pygwy-24.crush.png" "gwy_pygwy-24.png"; fi

gwy_radial_profile-24.png: $(srcdir)/src/gwy_radial_profile-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_radial_profile-24.png" "$(srcdir)/src/gwy_radial_profile-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_radial_profile-24.png" "gwy_radial_profile-24.crush.png"
	$(AM_V_at)if test -s "gwy_radial_profile-24.crush.png"; then mv -f "gwy_radial_profile-24.crush.png" "gwy_radial_profile-24.png"; fi

gwy_rank_filter-24.png: $(srcdir)/src/gwy_rank_filter-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_rank_filter-24.png" "$(srcdir)/src/gwy_rank_filter-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_rank_filter-24.png" "gwy_rank_filter-24.crush.png"
	$(AM_V_at)if test -s "gwy_rank_filter-24.crush.png"; then mv -f "gwy_rank_filter-24.crush.png" "gwy_rank_filter-24.png"; fi

gwy_rasterize-24.png: $(srcdir)/src/gwy_rasterize-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_rasterize-24.png" "$(srcdir)/src/gwy_rasterize-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_rasterize-24.png" "gwy_rasterize-24.crush.png"
	$(AM_V_at)if test -s "gwy_rasterize-24.crush.png"; then mv -f "gwy_rasterize-24.crush.png" "gwy_rasterize-24.png"; fi

gwy_remove_under_mask-24.png: $(srcdir)/src/gwy_remove_under_mask-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_remove_under_mask-24.png" "$(srcdir)/src/gwy_remove_under_mask-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_remove_under_mask-24.png" "gwy_remove_under_mask-24.crush.png"
	$(AM_V_at)if test -s "gwy_remove_under_mask-24.crush.png"; then mv -f "gwy_remove_under_mask-24.crush.png" "gwy_remove_under_mask-24.png"; fi

gwy_revolve_arc-24.png: $(srcdir)/src/gwy_revolve_arc-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_revolve_arc-24.png" "$(srcdir)/src/gwy_revolve_arc-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_revolve_arc-24.png" "gwy_revolve_arc-24.crush.png"
	$(AM_V_at)if test -s "gwy_revolve_arc-24.crush.png"; then mv -f "gwy_revolve_arc-24.crush.png" "gwy_revolve_arc-24.png"; fi

gwy_revolve_sphere-24.png: $(srcdir)/src/gwy_revolve_sphere-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_revolve_sphere-24.png" "$(srcdir)/src/gwy_revolve_sphere-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_revolve_sphere-24.png" "gwy_revolve_sphere-24.crush.png"
	$(AM_V_at)if test -s "gwy_revolve_sphere-24.crush.png"; then mv -f "gwy_revolve_sphere-24.crush.png" "gwy_revolve_sphere-24.png"; fi

gwy_rotate-24.png: $(srcdir)/src/gwy_rotate-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_rotate-24.png" "$(srcdir)/src/gwy_rotate-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_rotate-24.png" "gwy_rotate-24.crush.png"
	$(AM_V_at)if test -s "gwy_rotate-24.crush.png"; then mv -f "gwy_rotate-24.crush.png" "gwy_rotate-24.png"; fi

gwy_rotate_180-24.png: $(srcdir)/src/gwy_rotate_180-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_rotate_180-24.png" "$(srcdir)/src/gwy_rotate_180-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_rotate_180-24.png" "gwy_rotate_180-24.crush.png"
	$(AM_V_at)if test -s "gwy_rotate_180-24.crush.png"; then mv -f "gwy_rotate_180-24.crush.png" "gwy_rotate_180-24.png"; fi

gwy_rotate_3d-24.png: $(srcdir)/src/gwy_rotate_3d-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_rotate_3d-24.png" "$(srcdir)/src/gwy_rotate_3d-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_rotate_3d-24.png" "gwy_rotate_3d-24.crush.png"
	$(AM_V_at)if test -s "gwy_rotate_3d-24.crush.png"; then mv -f "gwy_rotate_3d-24.crush.png" "gwy_rotate_3d-24.png"; fi

gwy_rotate_90_ccw-24.png: $(srcdir)/src/gwy_rotate_90_ccw-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_rotate_90_ccw-24.png" "$(srcdir)/src/gwy_rotate_90_ccw-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_rotate_90_ccw-24.png" "gwy_rotate_90_ccw-24.crush.png"
	$(AM_V_at)if test -s "gwy_rotate_90_ccw-24.crush.png"; then mv -f "gwy_rotate_90_ccw-24.crush.png" "gwy_rotate_90_ccw-24.png"; fi

gwy_rotate_90_cw-24.png: $(srcdir)/src/gwy_rotate_90_cw-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_rotate_90_cw-24.png" "$(srcdir)/src/gwy_rotate_90_cw-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_rotate_90_cw-24.png" "gwy_rotate_90_cw-24.crush.png"
	$(AM_V_at)if test -s "gwy_rotate_90_cw-24.crush.png"; then mv -f "gwy_rotate_90_cw-24.crush.png" "gwy_rotate_90_cw-24.png"; fi

gwy_scale-24.png: $(srcdir)/src/gwy_scale-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_scale-24.png" "$(srcdir)/src/gwy_scale-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_scale-24.png" "gwy_scale-24.crush.png"
	$(AM_V_at)if test -s "gwy_scale-24.crush.png"; then mv -f "gwy_scale-24.crush.png" "gwy_scale-24.png"; fi

gwy_scale_horizontally-24.png: $(srcdir)/src/gwy_scale_horizontally-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_scale_horizontally-24.png" "$(srcdir)/src/gwy_scale_horizontally-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_scale_horizontally-24.png" "gwy_scale_horizontally-24.crush.png"
	$(AM_V_at)if test -s "gwy_scale_horizontally-24.crush.png"; then mv -f "gwy_scale_horizontally-24.crush.png" "gwy_scale_horizontally-24.png"; fi

gwy_scale_vertically-24.png: $(srcdir)/src/gwy_scale_vertically-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_scale_vertically-24.png" "$(srcdir)/src/gwy_scale_vertically-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_scale_vertically-24.png" "gwy_scale_vertically-24.crush.png"
	$(AM_V_at)if test -s "gwy_scale_vertically-24.crush.png"; then mv -f "gwy_scale_vertically-24.crush.png" "gwy_scale_vertically-24.png"; fi

gwy_scars-24.png: $(srcdir)/src/gwy_scars-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_scars-24.png" "$(srcdir)/src/gwy_scars-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_scars-24.png" "gwy_scars-24.crush.png"
	$(AM_V_at)if test -s "gwy_scars-24.crush.png"; then mv -f "gwy_scars-24.crush.png" "gwy_scars-24.png"; fi

gwy_scientific_number_format-18.png: $(srcdir)/src/gwy_scientific_number_format-18.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=18 --export-height=18 $(INKSCAPE_EXPORT_PNGFILE)="gwy_scientific_number_format-18.png" "$(srcdir)/src/gwy_scientific_number_format-18.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_scientific_number_format-18.png" "gwy_scientific_number_format-18.crush.png"
	$(AM_V_at)if test -s "gwy_scientific_number_format-18.crush.png"; then mv -f "gwy_scientific_number_format-18.crush.png" "gwy_scientific_number_format-18.png"; fi

gwy_selections-24.png: $(srcdir)/src/gwy_selections-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_selections-24.png" "$(srcdir)/src/gwy_selections-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_selections-24.png" "gwy_selections-24.crush.png"
	$(AM_V_at)if test -s "gwy_selections-24.crush.png"; then mv -f "gwy_selections-24.crush.png" "gwy_selections-24.png"; fi

gwy_shader-24.png: $(srcdir)/src/gwy_shader-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_shader-24.png" "$(srcdir)/src/gwy_shader-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_shader-24.png" "gwy_shader-24.crush.png"
	$(AM_V_at)if test -s "gwy_shader-24.crush.png"; then mv -f "gwy_shader-24.crush.png" "gwy_shader-24.png"; fi

gwy_spectrum-24.png: $(srcdir)/src/gwy_spectrum-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_spectrum-24.png" "$(srcdir)/src/gwy_spectrum-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_spectrum-24.png" "gwy_spectrum-24.crush.png"
	$(AM_V_at)if test -s "gwy_spectrum-24.crush.png"; then mv -f "gwy_spectrum-24.crush.png" "gwy_spectrum-24.png"; fi

gwy_spot_remove-24.png: $(srcdir)/src/gwy_spot_remove-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_spot_remove-24.png" "$(srcdir)/src/gwy_spot_remove-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_spot_remove-24.png" "gwy_spot_remove-24.crush.png"
	$(AM_V_at)if test -s "gwy_spot_remove-24.crush.png"; then mv -f "gwy_spot_remove-24.crush.png" "gwy_spot_remove-24.png"; fi

gwy_square_samples-24.png: $(srcdir)/src/gwy_square_samples-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_square_samples-24.png" "$(srcdir)/src/gwy_square_samples-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_square_samples-24.png" "gwy_square_samples-24.crush.png"
	$(AM_V_at)if test -s "gwy_square_samples-24.crush.png"; then mv -f "gwy_square_samples-24.crush.png" "gwy_square_samples-24.png"; fi

gwy_stat_quantities-24.png: $(srcdir)/src/gwy_stat_quantities-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_stat_quantities-24.png" "$(srcdir)/src/gwy_stat_quantities-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_stat_quantities-24.png" "gwy_stat_quantities-24.crush.png"
	$(AM_V_at)if test -s "gwy_stat_quantities-24.crush.png"; then mv -f "gwy_stat_quantities-24.crush.png" "gwy_stat_quantities-24.png"; fi

gwy_stitch-24.png: $(srcdir)/src/gwy_stitch-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_stitch-24.png" "$(srcdir)/src/gwy_stitch-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_stitch-24.png" "gwy_stitch-24.crush.png"
	$(AM_V_at)if test -s "gwy_stitch-24.crush.png"; then mv -f "gwy_stitch-24.crush.png" "gwy_stitch-24.png"; fi

gwy_straighten_path-24.png: $(srcdir)/src/gwy_straighten_path-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_straighten_path-24.png" "$(srcdir)/src/gwy_straighten_path-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_straighten_path-24.png" "gwy_straighten_path-24.crush.png"
	$(AM_V_at)if test -s "gwy_straighten_path-24.crush.png"; then mv -f "gwy_straighten_path-24.crush.png" "gwy_straighten_path-24.png"; fi

gwy_subscript-20.png: $(srcdir)/src/gwy_subscript-20.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=20 --export-height=20 $(INKSCAPE_EXPORT_PNGFILE)="gwy_subscript-20.png" "$(srcdir)/src/gwy_subscript-20.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_subscript-20.png" "gwy_subscript-20.crush.png"
	$(AM_V_at)if test -s "gwy_subscript-20.crush.png"; then mv -f "gwy_subscript-20.crush.png" "gwy_subscript-20.png"; fi

gwy_superscript-20.png: $(srcdir)/src/gwy_superscript-20.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=20 --export-height=20 $(INKSCAPE_EXPORT_PNGFILE)="gwy_superscript-20.png" "$(srcdir)/src/gwy_superscript-20.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_superscript-20.png" "gwy_superscript-20.crush.png"
	$(AM_V_at)if test -s "gwy_superscript-20.crush.png"; then mv -f "gwy_superscript-20.crush.png" "gwy_superscript-20.png"; fi

gwy_synthetic_anneal-24.png: $(srcdir)/src/gwy_synthetic_anneal-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_synthetic_anneal-24.png" "$(srcdir)/src/gwy_synthetic_anneal-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_synthetic_anneal-24.png" "gwy_synthetic_anneal-24.crush.png"
	$(AM_V_at)if test -s "gwy_synthetic_anneal-24.crush.png"; then mv -f "gwy_synthetic_anneal-24.crush.png" "gwy_synthetic_anneal-24.png"; fi

gwy_synthetic_ballistic_deposition-24.png: $(srcdir)/src/gwy_synthetic_ballistic_deposition-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_synthetic_ballistic_deposition-24.png" "$(srcdir)/src/gwy_synthetic_ballistic_deposition-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_synthetic_ballistic_deposition-24.png" "gwy_synthetic_ballistic_deposition-24.crush.png"
	$(AM_V_at)if test -s "gwy_synthetic_ballistic_deposition-24.crush.png"; then mv -f "gwy_synthetic_ballistic_deposition-24.crush.png" "gwy_synthetic_ballistic_deposition-24.png"; fi

gwy_synthetic_brownian_motion-24.png: $(srcdir)/src/gwy_synthetic_brownian_motion-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_synthetic_brownian_motion-24.png" "$(srcdir)/src/gwy_synthetic_brownian_motion-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_synthetic_brownian_motion-24.png" "gwy_synthetic_brownian_motion-24.crush.png"
	$(AM_V_at)if test -s "gwy_synthetic_brownian_motion-24.crush.png"; then mv -f "gwy_synthetic_brownian_motion-24.crush.png" "gwy_synthetic_brownian_motion-24.png"; fi

gwy_synthetic_columnar-24.png: $(srcdir)/src/gwy_synthetic_columnar-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_synthetic_columnar-24.png" "$(srcdir)/src/gwy_synthetic_columnar-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_synthetic_columnar-24.png" "gwy_synthetic_columnar-24.crush.png"
	$(AM_V_at)if test -s "gwy_synthetic_columnar-24.crush.png"; then mv -f "gwy_synthetic_columnar-24.crush.png" "gwy_synthetic_columnar-24.png"; fi

gwy_synthetic_diffusion-24.png: $(srcdir)/src/gwy_synthetic_diffusion-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_synthetic_diffusion-24.png" "$(srcdir)/src/gwy_synthetic_diffusion-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_synthetic_diffusion-24.png" "gwy_synthetic_diffusion-24.crush.png"
	$(AM_V_at)if test -s "gwy_synthetic_diffusion-24.crush.png"; then mv -f "gwy_synthetic_diffusion-24.crush.png" "gwy_synthetic_diffusion-24.png"; fi

gwy_synthetic_discs-24.png: $(srcdir)/src/gwy_synthetic_discs-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_synthetic_discs-24.png" "$(srcdir)/src/gwy_synthetic_discs-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_synthetic_discs-24.png" "gwy_synthetic_discs-24.crush.png"
	$(AM_V_at)if test -s "gwy_synthetic_discs-24.crush.png"; then mv -f "gwy_synthetic_discs-24.crush.png" "gwy_synthetic_discs-24.png"; fi

gwy_synthetic_domains-24.png: $(srcdir)/src/gwy_synthetic_domains-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_synthetic_domains-24.png" "$(srcdir)/src/gwy_synthetic_domains-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_synthetic_domains-24.png" "gwy_synthetic_domains-24.crush.png"
	$(AM_V_at)if test -s "gwy_synthetic_domains-24.crush.png"; then mv -f "gwy_synthetic_domains-24.crush.png" "gwy_synthetic_domains-24.png"; fi

gwy_synthetic_dunes-24.png: $(srcdir)/src/gwy_synthetic_dunes-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_synthetic_dunes-24.png" "$(srcdir)/src/gwy_synthetic_dunes-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_synthetic_dunes-24.png" "gwy_synthetic_dunes-24.crush.png"
	$(AM_V_at)if test -s "gwy_synthetic_dunes-24.crush.png"; then mv -f "gwy_synthetic_dunes-24.crush.png" "gwy_synthetic_dunes-24.png"; fi

gwy_synthetic_fibres-24.png: $(srcdir)/src/gwy_synthetic_fibres-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_synthetic_fibres-24.png" "$(srcdir)/src/gwy_synthetic_fibres-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_synthetic_fibres-24.png" "gwy_synthetic_fibres-24.crush.png"
	$(AM_V_at)if test -s "gwy_synthetic_fibres-24.crush.png"; then mv -f "gwy_synthetic_fibres-24.crush.png" "gwy_synthetic_fibres-24.png"; fi

gwy_synthetic_lattice-24.png: $(srcdir)/src/gwy_synthetic_lattice-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_synthetic_lattice-24.png" "$(srcdir)/src/gwy_synthetic_lattice-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_synthetic_lattice-24.png" "gwy_synthetic_lattice-24.crush.png"
	$(AM_V_at)if test -s "gwy_synthetic_lattice-24.crush.png"; then mv -f "gwy_synthetic_lattice-24.crush.png" "gwy_synthetic_lattice-24.png"; fi

gwy_synthetic_line_noise-24.png: $(srcdir)/src/gwy_synthetic_line_noise-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_synthetic_line_noise-24.png" "$(srcdir)/src/gwy_synthetic_line_noise-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_synthetic_line_noise-24.png" "gwy_synthetic_line_noise-24.crush.png"
	$(AM_V_at)if test -s "gwy_synthetic_line_noise-24.crush.png"; then mv -f "gwy_synthetic_line_noise-24.crush.png" "gwy_synthetic_line_noise-24.png"; fi

gwy_synthetic_noise-24.png: $(srcdir)/src/gwy_synthetic_noise-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_synthetic_noise-24.png" "$(srcdir)/src/gwy_synthetic_noise-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_synthetic_noise-24.png" "gwy_synthetic_noise-24.crush.png"
	$(AM_V_at)if test -s "gwy_synthetic_noise-24.crush.png"; then mv -f "gwy_synthetic_noise-24.crush.png" "gwy_synthetic_noise-24.png"; fi

gwy_synthetic_objects-24.png: $(srcdir)/src/gwy_synthetic_objects-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_synthetic_objects-24.png" "$(srcdir)/src/gwy_synthetic_objects-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_synthetic_objects-24.png" "gwy_synthetic_objects-24.crush.png"
	$(AM_V_at)if test -s "gwy_synthetic_objects-24.crush.png"; then mv -f "gwy_synthetic_objects-24.crush.png" "gwy_synthetic_objects-24.png"; fi

gwy_synthetic_particles-24.png: $(srcdir)/src/gwy_synthetic_particles-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_synthetic_particles-24.png" "$(srcdir)/src/gwy_synthetic_particles-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_synthetic_particles-24.png" "gwy_synthetic_particles-24.crush.png"
	$(AM_V_at)if test -s "gwy_synthetic_particles-24.crush.png"; then mv -f "gwy_synthetic_particles-24.crush.png" "gwy_synthetic_particles-24.png"; fi

gwy_synthetic_pattern-24.png: $(srcdir)/src/gwy_synthetic_pattern-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_synthetic_pattern-24.png" "$(srcdir)/src/gwy_synthetic_pattern-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_synthetic_pattern-24.png" "gwy_synthetic_pattern-24.crush.png"
	$(AM_V_at)if test -s "gwy_synthetic_pattern-24.crush.png"; then mv -f "gwy_synthetic_pattern-24.crush.png" "gwy_synthetic_pattern-24.png"; fi

gwy_synthetic_phases-24.png: $(srcdir)/src/gwy_synthetic_phases-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_synthetic_phases-24.png" "$(srcdir)/src/gwy_synthetic_phases-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_synthetic_phases-24.png" "gwy_synthetic_phases-24.crush.png"
	$(AM_V_at)if test -s "gwy_synthetic_phases-24.crush.png"; then mv -f "gwy_synthetic_phases-24.crush.png" "gwy_synthetic_phases-24.png"; fi

gwy_synthetic_pileup-24.png: $(srcdir)/src/gwy_synthetic_pileup-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_synthetic_pileup-24.png" "$(srcdir)/src/gwy_synthetic_pileup-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_synthetic_pileup-24.png" "gwy_synthetic_pileup-24.crush.png"
	$(AM_V_at)if test -s "gwy_synthetic_pileup-24.crush.png"; then mv -f "gwy_synthetic_pileup-24.crush.png" "gwy_synthetic_pileup-24.png"; fi

gwy_synthetic_plateaus-24.png: $(srcdir)/src/gwy_synthetic_plateaus-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_synthetic_plateaus-24.png" "$(srcdir)/src/gwy_synthetic_plateaus-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_synthetic_plateaus-24.png" "gwy_synthetic_plateaus-24.crush.png"
	$(AM_V_at)if test -s "gwy_synthetic_plateaus-24.crush.png"; then mv -f "gwy_synthetic_plateaus-24.crush.png" "gwy_synthetic_plateaus-24.png"; fi

gwy_synthetic_spectral-24.png: $(srcdir)/src/gwy_synthetic_spectral-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_synthetic_spectral-24.png" "$(srcdir)/src/gwy_synthetic_spectral-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_synthetic_spectral-24.png" "gwy_synthetic_spectral-24.crush.png"
	$(AM_V_at)if test -s "gwy_synthetic_spectral-24.crush.png"; then mv -f "gwy_synthetic_spectral-24.crush.png" "gwy_synthetic_spectral-24.png"; fi

gwy_synthetic_turing_pattern-24.png: $(srcdir)/src/gwy_synthetic_turing_pattern-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_synthetic_turing_pattern-24.png" "$(srcdir)/src/gwy_synthetic_turing_pattern-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_synthetic_turing_pattern-24.png" "gwy_synthetic_turing_pattern-24.crush.png"
	$(AM_V_at)if test -s "gwy_synthetic_turing_pattern-24.crush.png"; then mv -f "gwy_synthetic_turing_pattern-24.crush.png" "gwy_synthetic_turing_pattern-24.png"; fi

gwy_synthetic_waves-24.png: $(srcdir)/src/gwy_synthetic_waves-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_synthetic_waves-24.png" "$(srcdir)/src/gwy_synthetic_waves-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_synthetic_waves-24.png" "gwy_synthetic_waves-24.crush.png"
	$(AM_V_at)if test -s "gwy_synthetic_waves-24.crush.png"; then mv -f "gwy_synthetic_waves-24.crush.png" "gwy_synthetic_waves-24.png"; fi

gwy_terrace_measure-24.png: $(srcdir)/src/gwy_terrace_measure-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_terrace_measure-24.png" "$(srcdir)/src/gwy_terrace_measure-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_terrace_measure-24.png" "gwy_terrace_measure-24.crush.png"
	$(AM_V_at)if test -s "gwy_terrace_measure-24.crush.png"; then mv -f "gwy_terrace_measure-24.crush.png" "gwy_terrace_measure-24.png"; fi

gwy_tilt-24.png: $(srcdir)/src/gwy_tilt-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_tilt-24.png" "$(srcdir)/src/gwy_tilt-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_tilt-24.png" "gwy_tilt-24.crush.png"
	$(AM_V_at)if test -s "gwy_tilt-24.crush.png"; then mv -f "gwy_tilt-24.crush.png" "gwy_tilt-24.png"; fi

gwy_tip_dilation-24.png: $(srcdir)/src/gwy_tip_dilation-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_tip_dilation-24.png" "$(srcdir)/src/gwy_tip_dilation-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_tip_dilation-24.png" "gwy_tip_dilation-24.crush.png"
	$(AM_V_at)if test -s "gwy_tip_dilation-24.crush.png"; then mv -f "gwy_tip_dilation-24.crush.png" "gwy_tip_dilation-24.png"; fi

gwy_tip_erosion-24.png: $(srcdir)/src/gwy_tip_erosion-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_tip_erosion-24.png" "$(srcdir)/src/gwy_tip_erosion-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_tip_erosion-24.png" "gwy_tip_erosion-24.crush.png"
	$(AM_V_at)if test -s "gwy_tip_erosion-24.crush.png"; then mv -f "gwy_tip_erosion-24.crush.png" "gwy_tip_erosion-24.png"; fi

gwy_tip_estimation-24.png: $(srcdir)/src/gwy_tip_estimation-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_tip_estimation-24.png" "$(srcdir)/src/gwy_tip_estimation-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_tip_estimation-24.png" "gwy_tip_estimation-24.crush.png"
	$(AM_V_at)if test -s "gwy_tip_estimation-24.crush.png"; then mv -f "gwy_tip_estimation-24.crush.png" "gwy_tip_estimation-24.png"; fi

gwy_tip_indent_analyze-24.png: $(srcdir)/src/gwy_tip_indent_analyze-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_tip_indent_analyze-24.png" "$(srcdir)/src/gwy_tip_indent_analyze-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_tip_indent_analyze-24.png" "gwy_tip_indent_analyze-24.crush.png"
	$(AM_V_at)if test -s "gwy_tip_indent_analyze-24.crush.png"; then mv -f "gwy_tip_indent_analyze-24.crush.png" "gwy_tip_indent_analyze-24.png"; fi

gwy_tip_lateral_force-24.png: $(srcdir)/src/gwy_tip_lateral_force-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_tip_lateral_force-24.png" "$(srcdir)/src/gwy_tip_lateral_force-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_tip_lateral_force-24.png" "gwy_tip_lateral_force-24.crush.png"
	$(AM_V_at)if test -s "gwy_tip_lateral_force-24.crush.png"; then mv -f "gwy_tip_lateral_force-24.crush.png" "gwy_tip_lateral_force-24.png"; fi

gwy_tip_map-24.png: $(srcdir)/src/gwy_tip_map-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_tip_map-24.png" "$(srcdir)/src/gwy_tip_map-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_tip_map-24.png" "gwy_tip_map-24.crush.png"
	$(AM_V_at)if test -s "gwy_tip_map-24.crush.png"; then mv -f "gwy_tip_map-24.crush.png" "gwy_tip_map-24.png"; fi

gwy_tip_model-24.png: $(srcdir)/src/gwy_tip_model-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_tip_model-24.png" "$(srcdir)/src/gwy_tip_model-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_tip_model-24.png" "gwy_tip_model-24.crush.png"
	$(AM_V_at)if test -s "gwy_tip_model-24.crush.png"; then mv -f "gwy_tip_model-24.crush.png" "gwy_tip_model-24.png"; fi

gwy_tip_pid-24.png: $(srcdir)/src/gwy_tip_pid-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_tip_pid-24.png" "$(srcdir)/src/gwy_tip_pid-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_tip_pid-24.png" "gwy_tip_pid-24.crush.png"
	$(AM_V_at)if test -s "gwy_tip_pid-24.crush.png"; then mv -f "gwy_tip_pid-24.crush.png" "gwy_tip_pid-24.png"; fi

gwy_unrotate-24.png: $(srcdir)/src/gwy_unrotate-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_unrotate-24.png" "$(srcdir)/src/gwy_unrotate-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_unrotate-24.png" "gwy_unrotate-24.crush.png"
	$(AM_V_at)if test -s "gwy_unrotate-24.crush.png"; then mv -f "gwy_unrotate-24.crush.png" "gwy_unrotate-24.png"; fi

gwy_value_invert-24.png: $(srcdir)/src/gwy_value_invert-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_value_invert-24.png" "$(srcdir)/src/gwy_value_invert-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_value_invert-24.png" "gwy_value_invert-24.crush.png"
	$(AM_V_at)if test -s "gwy_value_invert-24.crush.png"; then mv -f "gwy_value_invert-24.crush.png" "gwy_value_invert-24.png"; fi

gwy_volume-24.png: $(srcdir)/src/gwy_volume-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_volume-24.png" "$(srcdir)/src/gwy_volume-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_volume-24.png" "gwy_volume-24.crush.png"
	$(AM_V_at)if test -s "gwy_volume-24.crush.png"; then mv -f "gwy_volume-24.crush.png" "gwy_volume-24.png"; fi

gwy_volume_arithmetic-24.png: $(srcdir)/src/gwy_volume_arithmetic-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_volume_arithmetic-24.png" "$(srcdir)/src/gwy_volume_arithmetic-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_volume_arithmetic-24.png" "gwy_volume_arithmetic-24.crush.png"
	$(AM_V_at)if test -s "gwy_volume_arithmetic-24.crush.png"; then mv -f "gwy_volume_arithmetic-24.crush.png" "gwy_volume_arithmetic-24.png"; fi

gwy_volume_calibrate-24.png: $(srcdir)/src/gwy_volume_calibrate-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_volume_calibrate-24.png" "$(srcdir)/src/gwy_volume_calibrate-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_volume_calibrate-24.png" "gwy_volume_calibrate-24.crush.png"
	$(AM_V_at)if test -s "gwy_volume_calibrate-24.crush.png"; then mv -f "gwy_volume_calibrate-24.crush.png" "gwy_volume_calibrate-24.png"; fi

gwy_volume_dimensions-24.png: $(srcdir)/src/gwy_volume_dimensions-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_volume_dimensions-24.png" "$(srcdir)/src/gwy_volume_dimensions-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_volume_dimensions-24.png" "gwy_volume_dimensions-24.crush.png"
	$(AM_V_at)if test -s "gwy_volume_dimensions-24.crush.png"; then mv -f "gwy_volume_dimensions-24.crush.png" "gwy_volume_dimensions-24.png"; fi

gwy_volume_fd-24.png: $(srcdir)/src/gwy_volume_fd-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_volume_fd-24.png" "$(srcdir)/src/gwy_volume_fd-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_volume_fd-24.png" "gwy_volume_fd-24.crush.png"
	$(AM_V_at)if test -s "gwy_volume_fd-24.crush.png"; then mv -f "gwy_volume_fd-24.crush.png" "gwy_volume_fd-24.png"; fi

gwy_volume_invert-24.png: $(srcdir)/src/gwy_volume_invert-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_volume_invert-24.png" "$(srcdir)/src/gwy_volume_invert-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_volume_invert-24.png" "gwy_volume_invert-24.crush.png"
	$(AM_V_at)if test -s "gwy_volume_invert-24.crush.png"; then mv -f "gwy_volume_invert-24.crush.png" "gwy_volume_invert-24.png"; fi

gwy_volume_kmeans-24.png: $(srcdir)/src/gwy_volume_kmeans-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_volume_kmeans-24.png" "$(srcdir)/src/gwy_volume_kmeans-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_volume_kmeans-24.png" "gwy_volume_kmeans-24.crush.png"
	$(AM_V_at)if test -s "gwy_volume_kmeans-24.crush.png"; then mv -f "gwy_volume_kmeans-24.crush.png" "gwy_volume_kmeans-24.png"; fi

gwy_volume_kmedians-24.png: $(srcdir)/src/gwy_volume_kmedians-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_volume_kmedians-24.png" "$(srcdir)/src/gwy_volume_kmedians-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_volume_kmedians-24.png" "gwy_volume_kmedians-24.crush.png"
	$(AM_V_at)if test -s "gwy_volume_kmedians-24.crush.png"; then mv -f "gwy_volume_kmedians-24.crush.png" "gwy_volume_kmedians-24.png"; fi

gwy_volume_line_stats-24.png: $(srcdir)/src/gwy_volume_line_stats-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_volume_line_stats-24.png" "$(srcdir)/src/gwy_volume_line_stats-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_volume_line_stats-24.png" "gwy_volume_line_stats-24.crush.png"
	$(AM_V_at)if test -s "gwy_volume_line_stats-24.crush.png"; then mv -f "gwy_volume_line_stats-24.crush.png" "gwy_volume_line_stats-24.png"; fi

gwy_volume_plane_stats-24.png: $(srcdir)/src/gwy_volume_plane_stats-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_volume_plane_stats-24.png" "$(srcdir)/src/gwy_volume_plane_stats-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_volume_plane_stats-24.png" "gwy_volume_plane_stats-24.crush.png"
	$(AM_V_at)if test -s "gwy_volume_plane_stats-24.crush.png"; then mv -f "gwy_volume_plane_stats-24.crush.png" "gwy_volume_plane_stats-24.png"; fi

gwy_volume_slice-24.png: $(srcdir)/src/gwy_volume_slice-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_volume_slice-24.png" "$(srcdir)/src/gwy_volume_slice-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_volume_slice-24.png" "gwy_volume_slice-24.crush.png"
	$(AM_V_at)if test -s "gwy_volume_slice-24.crush.png"; then mv -f "gwy_volume_slice-24.crush.png" "gwy_volume_slice-24.png"; fi

gwy_volume_swap_axes-24.png: $(srcdir)/src/gwy_volume_swap_axes-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_volume_swap_axes-24.png" "$(srcdir)/src/gwy_volume_swap_axes-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_volume_swap_axes-24.png" "gwy_volume_swap_axes-24.crush.png"
	$(AM_V_at)if test -s "gwy_volume_swap_axes-24.crush.png"; then mv -f "gwy_volume_swap_axes-24.crush.png" "gwy_volume_swap_axes-24.png"; fi

gwy_volumize-24.png: $(srcdir)/src/gwy_volumize-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_volumize-24.png" "$(srcdir)/src/gwy_volumize-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_volumize-24.png" "gwy_volumize-24.crush.png"
	$(AM_V_at)if test -s "gwy_volumize-24.crush.png"; then mv -f "gwy_volumize-24.crush.png" "gwy_volumize-24.png"; fi

gwy_volumize_layers-24.png: $(srcdir)/src/gwy_volumize_layers-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_volumize_layers-24.png" "$(srcdir)/src/gwy_volumize_layers-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_volumize_layers-24.png" "gwy_volumize_layers-24.crush.png"
	$(AM_V_at)if test -s "gwy_volumize_layers-24.crush.png"; then mv -f "gwy_volumize_layers-24.crush.png" "gwy_volumize_layers-24.png"; fi

gwy_wrap_value-24.png: $(srcdir)/src/gwy_wrap_value-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_wrap_value-24.png" "$(srcdir)/src/gwy_wrap_value-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_wrap_value-24.png" "gwy_wrap_value-24.crush.png"
	$(AM_V_at)if test -s "gwy_wrap_value-24.crush.png"; then mv -f "gwy_wrap_value-24.crush.png" "gwy_wrap_value-24.png"; fi

gwy_xy_denoise-24.png: $(srcdir)/src/gwy_xy_denoise-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_xy_denoise-24.png" "$(srcdir)/src/gwy_xy_denoise-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_xy_denoise-24.png" "gwy_xy_denoise-24.crush.png"
	$(AM_V_at)if test -s "gwy_xy_denoise-24.crush.png"; then mv -f "gwy_xy_denoise-24.crush.png" "gwy_xy_denoise-24.png"; fi

gwy_xyzize-24.png: $(srcdir)/src/gwy_xyzize-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_xyzize-24.png" "$(srcdir)/src/gwy_xyzize-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_xyzize-24.png" "gwy_xyzize-24.crush.png"
	$(AM_V_at)if test -s "gwy_xyzize-24.crush.png"; then mv -f "gwy_xyzize-24.crush.png" "gwy_xyzize-24.png"; fi

gwy_zero_maximum-24.png: $(srcdir)/src/gwy_zero_maximum-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_zero_maximum-24.png" "$(srcdir)/src/gwy_zero_maximum-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_zero_maximum-24.png" "gwy_zero_maximum-24.crush.png"
	$(AM_V_at)if test -s "gwy_zero_maximum-24.crush.png"; then mv -f "gwy_zero_maximum-24.crush.png" "gwy_zero_maximum-24.png"; fi

gwy_zero_mean-24.png: $(srcdir)/src/gwy_zero_mean-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_zero_mean-24.png" "$(srcdir)/src/gwy_zero_mean-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_zero_mean-24.png" "gwy_zero_mean-24.crush.png"
	$(AM_V_at)if test -s "gwy_zero_mean-24.crush.png"; then mv -f "gwy_zero_mean-24.crush.png" "gwy_zero_mean-24.png"; fi

gwy_zero_under_mask-24.png: $(srcdir)/src/gwy_zero_under_mask-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_zero_under_mask-24.png" "$(srcdir)/src/gwy_zero_under_mask-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_zero_under_mask-24.png" "gwy_zero_under_mask-24.crush.png"
	$(AM_V_at)if test -s "gwy_zero_under_mask-24.crush.png"; then mv -f "gwy_zero_under_mask-24.crush.png" "gwy_zero_under_mask-24.png"; fi

gwy_zoom_1_1-24.png: $(srcdir)/src/gwy_zoom_1_1-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_zoom_1_1-24.png" "$(srcdir)/src/gwy_zoom_1_1-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_zoom_1_1-24.png" "gwy_zoom_1_1-24.crush.png"
	$(AM_V_at)if test -s "gwy_zoom_1_1-24.crush.png"; then mv -f "gwy_zoom_1_1-24.crush.png" "gwy_zoom_1_1-24.png"; fi

gwy_zoom_fit-24.png: $(srcdir)/src/gwy_zoom_fit-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_zoom_fit-24.png" "$(srcdir)/src/gwy_zoom_fit-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_zoom_fit-24.png" "gwy_zoom_fit-24.crush.png"
	$(AM_V_at)if test -s "gwy_zoom_fit-24.crush.png"; then mv -f "gwy_zoom_fit-24.crush.png" "gwy_zoom_fit-24.png"; fi

gwy_zoom_in-24.png: $(srcdir)/src/gwy_zoom_in-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_zoom_in-24.png" "$(srcdir)/src/gwy_zoom_in-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_zoom_in-24.png" "gwy_zoom_in-24.crush.png"
	$(AM_V_at)if test -s "gwy_zoom_in-24.crush.png"; then mv -f "gwy_zoom_in-24.crush.png" "gwy_zoom_in-24.png"; fi

gwy_zoom_out-24.png: $(srcdir)/src/gwy_zoom_out-24.svg
	$(AM_V_GEN)DISPLAY=; unset DISPLAY; $(INKSCAPE_EXPORT) --export-width=24 --export-height=24 $(INKSCAPE_EXPORT_PNGFILE)="gwy_zoom_out-24.png" "$(srcdir)/src/gwy_zoom_out-24.svg"
	$(AM_V_at)$(PNGCRUSH) $(PNGCRUSH_SILENCE) -brute "gwy_zoom_out-24.png" "gwy_zoom_out-24.crush.png"
	$(AM_V_at)if test -s "gwy_zoom_out-24.crush.png"; then mv -f "gwy_zoom_out-24.crush.png" "gwy_zoom_out-24.png"; fi
# @@@ GENERATED STOCK LIST END @@@
endif

