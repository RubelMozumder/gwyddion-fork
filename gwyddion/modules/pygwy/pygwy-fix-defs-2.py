#!/usr/bin/python2
# $Id: pygwy-fix-defs-2.py 25640 2023-09-07 12:25:09Z yeti-dn $
# alter defs, fix parameters and return types
import sys, re
from optparse import OptionParser

parser = OptionParser(usage='''\
pygwy-fix-defs.py --codegendir=DIR
Reads pygwy.defs.tmp.
Generates pygwy.defs and boxed.override.''')

parser.add_option('-c', '--codegendir', dest='codegendir', type='string',
                  default='', metavar='DIR',
                  help='Directory with codegen python modules')

(options, args) = parser.parse_args()
if options.codegendir:
    sys.path.insert(0, options.codegendir)
assert not args

import defsparser, definitions

# Arguments we need to fix for various functions.  The syntax is natural:
# in each fix-list we list the affected arguments as the function arguments.
# Note original C names (func.c_name) are used even for foo_pygwy functions.
GwyDoubleOut_args = '''
gwy_3d_view_get_scale_range(min_scale, max_scale)
gwy_cdline_get_value(value)
gwy_axis_get_range(min, max)
gwy_axis_get_requested_range(min, max)
gwy_color_axis_get_range(min, max)
gwy_container_gis_double(value)
gwy_data_field_area_fit_plane(pa, pbx, pby)
gwy_data_field_area_get_inclination(theta, phi)
gwy_data_field_area_get_min_max_mask(min, max)
gwy_data_field_area_get_min_max(min, max)
gwy_data_field_area_get_dispersion(xcenter, ycenter)
gwy_data_field_area_get_normal_coeffs(nx, ny, nz)
gwy_data_field_area_get_stats(avg, ra, rms, skew, kurtosis)
gwy_data_field_area_get_stats_mask(avg, ra, rms, skew, kurtosis)
gwy_data_field_correct_laplace_iteration(error)
gwy_data_field_fit_facet_plane(pa, pbx, pby)
gwy_data_field_fit_plane(pa, pbx, pby)
gwy_data_field_fractal_cubecounting_dim(a, b)
gwy_data_field_fractal_partitioning_dim(a, b)
gwy_data_field_fractal_psdf_dim(a, b)
gwy_data_field_fractal_triangulation_dim(a, b)
gwy_data_field_get_autorange(from_, to)
gwy_data_field_get_inclination(theta, phi)
gwy_data_field_get_min_max(min, max)
gwy_data_field_get_dispersion(xcenter, ycenter)
gwy_data_field_get_normal_coeffs(nx, ny, nz)
gwy_data_field_get_stats(avg, ra, rms, skew, kurtosis)
gwy_data_field_hough_datafield_line_to_polar(rho, theta)
gwy_data_field_local_maximum(x_out, y_out)
gwy_data_line_get_line_coeffs(av, bv)
gwy_data_line_get_min_max(min, max)
gwy_data_line_part_get_min_max(min, max)
gwy_data_view_coords_real_to_xy_float(xscr, yscr)
gwy_data_view_coords_xy_to_real(xreal, yreal)
gwy_data_view_get_real_data_offsets(xoffset, yoffset)
gwy_data_view_get_real_data_sizes(xreal, yreal)
gwy_data_view_get_real_data_sizes(xreal, yreal)
gwy_graph_area_get_cursor(x_cursor, y_cursor)
gwy_graph_curve_model_get_ranges(x_min, y_min, x_max, y_max)
gwy_graph_curve_model_get_x_range(x_min, x_max)
gwy_graph_curve_model_get_y_range(y_min, y_max)
gwy_graph_model_get_ranges(x_min, y_min, x_max, y_max)
gwy_graph_model_get_x_range(x_min, x_max)
gwy_graph_model_get_y_range(y_min, y_max)
gwy_layer_basic_get_range(min, max)
gwy_math_curvature(kappa1, kappa2, phi1, phi2, xc, yc, zc)
gwy_math_refine_maximum(x, y)
gwy_math_refine_maximum_2d(x, y)
gwy_math_refine_maximum_1d(x)
gwy_ruler_get_range(lower, upper, position, max_size)
gwy_ruler_get_range(lower, upper, position, max_size)
gwy_spectra_itoxy(x, y)
gwy_surface_get_min_max(min, max)
gwy_surface_get_xrange(min, max)
gwy_surface_get_yrange(min, max)
gwy_math_find_nearest_line(d2min)
gwy_math_find_nearest_point(d2min)
gwy_interpolation_get_dval_of_equidists(result)
gwy_interpolation_interpolate_1d(result)
gwy_interpolation_interpolate_2d(result)
'''

GwyIntOut_args = '''
gwy_container_gis_enum(value)
gwy_container_gis_int32(value)
gwy_container_gis_int64(value)
gwy_data_chooser_get_active(id)
gwy_data_field_area_count_in_range(nbelow, nabove)
gwy_data_field_grains_autocrop(left, right, up, down)
gwy_data_field_hough_polar_line_to_datafield(px1, px2, py1, py2)
gwy_data_view_coords_real_to_xy(xscr, yscr)
gwy_data_view_coords_xy_clamp(xscr, yscr)
gwy_data_view_get_pixel_data_sizes(xres, yres)
gwy_enum_combo_box_update_int(integer)
gwy_file_detect_with_score(score)
gwy_gradient_get_points(npoints)
gwy_gradient_get_samples(nsamples)
gwy_math_curvature(dimen)
gwy_math_humanize_numbers(precision)
gwy_si_unit_new_parse(power10)
gwy_si_unit_set_from_string_parse(power10)
gwy_tip_estimate_full(count)
gwy_tip_estimate_partial(count)
gwy_peaks_analyze(npeaks)
gwy_math_find_nearest_line(idx)
gwy_math_find_nearest_point(idx)
'''

GwyBooleanOut_args = '''
gwy_cdline_get_value(fres)
gwy_nlfit_preset_guess(fres)
gwy_math_is_in_polygon(is_inside)
gwy_math_refine_maximum(refined)
gwy_math_refine_maximum_2d(refined)
gwy_math_refine_maximum_1d(refined)
gwy_data_field_measure_lattice_acf(succeeded)
gwy_data_field_measure_lattice_psdf(succeeded)
'''

GwyRGBAOut_args = '''
gwy_color_button_get_color(color)
'''

GwyIntInOut_args = '''
gwy_data_view_coords_xy_clamp(xscr, yscr)
gwy_data_view_coords_xy_cut_line(x0scr, y0scr, x1scr, y1scr)
'''

GObject_args = '''
gwy_container_set_object(value)
gwy_container_get_object(return)
'''

constgchar_args = '''
gwy_container_get_string(return)
gwy_container_set_const_string(value)
gwy_container_set_const_string_by_name(value)
'''

nullable_args = '''
gwy_app_data_browser_add_brick(data, preview)
gwy_app_data_browser_add_data_field(data)
gwy_app_data_browser_add_spectra(data)
gwy_app_data_browser_add_graph_model(data)
gwy_app_data_browser_add_surface(data)
gwy_math_find_nearest_line(metric)
gwy_math_find_nearest_point(metric)
gwy_app_file_load(name)
gwy_app_file_write(name)
gwy_data_line_fft(isrc)
gwy_data_line_fft_raw(isrc)
gwy_data_line_part_fft(isrc)
gwy_data_field_1dfft(iin)
gwy_data_field_1dfft_raw(iin)
gwy_data_field_area_1dfft(iin)
gwy_data_field_2dfft(iin)
gwy_data_field_2dfft_raw(iin)
gwy_data_field_area_2dfft(iin)
gwy_data_field_area_count_in_range(mask)
gwy_data_field_area_fit_plane(mask)
gwy_data_field_area_row_acf(weights, mask)
gwy_data_field_area_row_hhcf(weights, mask)
gwy_data_field_area_get_line_stats(mask)
gwy_data_field_new_rotated(exterior_mask)
gwy_surface_set_from_data_field_mask(mask)
gwy_si_unit_get_format(format)
gwy_si_unit_get_format_for_power10(format)
gwy_si_unit_get_format_with_digits(format)
gwy_si_unit_get_format_with_resolution(format)
gwy_surface_get_value_format_xy(format)
gwy_surface_get_value_format_z(format)
gwy_data_field_get_value_format_xy(format)
gwy_data_field_get_value_format_z(format)
gwy_data_line_get_value_format_x(format)
gwy_data_line_get_value_format_y(format)
gwy_brick_get_value_format_x(format)
gwy_brick_get_value_format_y(format)
gwy_brick_get_value_format_z(format)
gwy_brick_get_value_format_w(format)
gwy_lawn_get_value_format_xy(format)
gwy_lawn_get_value_format_curve(format)
gwy_brick_set_zcalibration(calibration)
gwy_data_field_crosscorrelate_init(x_dist,y_dist,score)
gwy_data_field_correlation_search(kernel_weight)
gwy_data_field_area_fill_mask(mask)
gwy_data_field_average_xyz(density_map)
gwy_data_field_filter_slope(xder,yder)
gwy_data_field_area_get_stats(mask)
gwy_data_field_area_get_stats_mask(mask)
gwy_data_field_area_dh(mask)
gwy_data_field_area_cdh(mask)
gwy_data_field_area_da_mask(mask)
gwy_data_field_area_cda_mask(mask)
gwy_data_field_area_get_median(mask)
gwy_data_field_area_get_median_mask(mask)
gwy_data_field_get_line_stats_mask(mask,weights)
gwy_data_field_area_get_line_stats(mask)
gwy_data_field_area_get_surface_area(mask)
gwy_data_field_area_get_surface_area_mask(mask)
gwy_data_field_area_get_variation(mask)
gwy_data_field_area_get_volume(mask,basis)
gwy_data_field_area_2dacf_mask(mask,weights)
gwy_data_field_angular_average(mask)
gwy_data_field_area_row_acf(mask,weights)
gwy_data_field_area_row_hhcf(mask,weights)
gwy_data_field_area_row_psdf(mask)
gwy_data_field_area_row_asg(mask)
gwy_data_field_get_profile_mask(mask)
'''

skip_args = '''
gwy_app_file_load(filename_sys=NULL)
gwy_app_file_write(filename_sys=NULL)
gwy_tip_estimate_partial(set_fraction=NULL, set_message=NULL)
gwy_tip_estimate_full(set_fraction=NULL, set_message=NULL)
gwy_graph_export_postscript(str=NULL)
gwy_graph_model_export_ascii(string=NULL)
gwy_data_field_get_profile(data_line=NULL)
gwy_data_field_area_filter_kth_rank(set_fraction=NULL)
gwy_data_field_area_filter_trimmed_mean(set_fraction=NULL)
gwy_brick_get_value_format_x(format=NULL)
gwy_brick_get_value_format_y(format=NULL)
gwy_brick_get_value_format_z(format=NULL)
gwy_brick_get_value_format_w(format=NULL)
gwy_data_field_get_value_format_xy(format=NULL)
gwy_data_field_get_value_format_z(format=NULL)
gwy_data_line_get_value_format_x(format=NULL)
gwy_data_line_get_value_format_y(format=NULL)
gwy_surface_get_value_format_xy(format=NULL)
gwy_surface_get_value_format_z(format=NULL)
gwy_lawn_get_value_format_xy(format=NULL)
gwy_lawn_get_value_format_curve(format=NULL)
'''

default_args = '''
gwy_data_field_affine(fill_value=0.0)
gwy_interpolation_shift_block_1d(fill_value=0.0)
gwy_data_line_new(nullme=TRUE)
gwy_data_line_new_alike(nullme=TRUE)
gwy_data_field_new(nullme=TRUE)
gwy_data_field_new_alike(nullme=TRUE)
gwy_brick_new(nullme=TRUE)
gwy_brick_new_alike(nullme=TRUE)
gwy_data_line_fft(interpolation=GWY_INTERPOLATION_LINEAR)
gwy_data_line_part_fft(interpolation=GWY_INTERPOLATION_LINEAR)
gwy_data_field_1dfft(interpolation=GWY_INTERPOLATION_LINEAR)
gwy_data_field_area_1dfft(interpolation=GWY_INTERPOLATION_LINEAR)
gwy_data_field_2dfft(interpolation=GWY_INTERPOLATION_LINEAR)
gwy_data_field_area_2dfft(interpolation=GWY_INTERPOLATION_LINEAR)
gwy_data_field_area_row_acf(masking=GWY_MASK_INCLUDE)
gwy_data_field_area_row_hhcf(masking=GWY_MASK_INCLUDE)
gwy_data_field_area_row_psdf(masking=GWY_MASK_INCLUDE)
gwy_data_field_area_row_asg(masking=GWY_MASK_INCLUDE)
gwy_file_load(mode=GWY_RUN_NONINTERACTIVE)
gwy_file_save(mode=GWY_RUN_NONINTERACTIVE)
'''

# Lists of functions that should have various attributes
caller_owns_return_funcs = '''
gwy_brick_duplicate
gwy_brick_new_alike
gwy_brick_new_part
gwy_construct_grain_quantity_units
gwy_container_duplicate
gwy_container_duplicate_by_prefix
gwy_data_chooser_new_channels
gwy_data_chooser_new_graphs
gwy_data_chooser_new_volumes
gwy_data_chooser_new_xyzs
gwy_data_field_area_extract
gwy_data_field_area_row_acf
gwy_data_field_area_row_hhcf
gwy_data_field_area_row_psdf
gwy_data_field_area_row_asg
gwy_data_field_duplicate
gwy_data_field_find_row_shifts_trimmed_diff
gwy_data_field_find_row_shifts_trimmed_mean
gwy_data_field_new_alike
gwy_data_field_new_binned
gwy_data_field_new_resampled
gwy_data_field_new_rotated
gwy_data_field_new_rotated_90
gwy_data_line_duplicate
gwy_data_line_new_alike
gwy_data_line_new_resampled
gwy_lawn_new_alike
gwy_lawn_new_part
gwy_file_load
gwy_graph_curve_model_duplicate
gwy_graph_curve_model_new_alike
gwy_graph_model_duplicate
gwy_graph_model_new_alike
gwy_si_unit_duplicate
gwy_si_unit_new_parse
gwy_si_unit_get_format
gwy_si_unit_get_format_for_power10
gwy_si_unit_get_format_with_digits
gwy_si_unit_get_format_with_resolution
gwy_spectra_duplicate
gwy_spectra_new_alike
gwy_spline_new_from_points
gwy_surface_duplicate
gwy_surface_new_alike
gwy_surface_new_part
gwy_surface_reduce_points
gwy_nlfit_preset_get_param_units
gwy_nlfit_preset_create_fitter
gwy_cdline_get_param_units
gwy_shape_fit_preset_get_param_units
gwy_shape_fit_preset_get_secondary_units
gwy_shape_fit_preset_create_fitter
gwy_brick_get_value_format_x
gwy_brick_get_value_format_y
gwy_brick_get_value_format_z
gwy_brick_get_value_format_w
gwy_data_field_get_value_format_xy
gwy_data_field_get_value_format_z
gwy_data_line_get_value_format_x
gwy_data_line_get_value_format_y
gwy_surface_get_value_format_xy
gwy_surface_get_value_format_z
gwy_lawn_get_value_format_xy
gwy_lawn_get_value_format_curve
gwy_tip_dilation
gwy_tip_erosion
gwy_tip_cmap
gwy_tip_estimate_full
gwy_tip_estimate_partial
'''

remove_funcs = '''
gwy_math_lin_solve_rewrite
gwy_gradient_get_samples
gwy_gradient_sample
gwy_vector_layer_button_press
gwy_vector_layer_button_release
gwy_vector_layer_motion_notify
gwy_vector_layer_key_press
gwy_vector_layer_key_release
gwy_md5_get_digest
gwy_gstring_replace
gwy_gstring_to_native_eol
gwy_append_doubles_to_gstring
gwy_app_data_browser_get_current
gwy_container_set_string
gwy_container_get_value
gwy_container_set_value
gwy_container_set_value_by_name
gwy_container_pass_object
gwy_si_value_format_set_units
gwy_si_unit_value_format_set_units
gwy_tip_model_get_preset_params
gwy_tip_model_get_preset_nparams
'''

pygwy_added_funcs = '''
gwy_key_from_name
gwy_name_from_key
gwy_data_line_set_data
gwy_data_field_set_data
gwy_brick_set_data
gwy_data_field_create_full_mask
gwy_get_grain_quantity_needs_same_units
gwy_construct_grain_quantity_units
gwy_tip_model_preset_get_params
'''

boxed_sequence_likes = '''
XY
XYZ
RGBA
AppDataId
'''

# This is a hack; functions that are renamed to other functions and we want to
# remove the originals because they would be generated too.
remove_methods = '''
GwyContainer.set_const_string
GwyContainer.set_const_string_by_name
'''

def unwrap_name(fname, fail_if_not_wrapped=False):
    if fname.endswith('_pygwy'):
        return fname[:-6]
    if fail_if_not_wrapped:
        assert fname.endswith('_pygwy')
    return fname

def get_python_name(c_name):
    py_name = c_name.capitalize()
    letters = list(re.findall(r'_[a-z]', c_name))
    for match in letters:
        py_name = py_name.replace(match, match.upper()[1])
    return py_name.replace('_', '')

def parse_fix_args_spec(ffargs, spec, argattr):
    re_fnmatch = re.compile(r'(?m)^(?P<cname>[a-zA-Z0-9_]+)'
                            r'\((?P<args>[^)]+)\)$')
    re_argsplit = re.compile(r'\s*,\s*').split
    for m in re_fnmatch.finditer(spec):
        name = m.group('cname')
        if name not in ffargs:
            ffargs[name] = {}
        for arg in re_argsplit(m.group('args')):
            # skip(foobar=NULL), default(foobar=1) or whatever
            argval = None
            if '=' in arg:
                arg, argval = arg.split('=')

            if arg not in ffargs[name]:
                ffargs[name][arg] = set()

            if argval is None:
                ffargs[name][arg].add(argattr)
            else:
                ffargs[name][arg].add(argattr + argval)

def parse_fix_func_props(ffprops, spec, argattr):
    for m in re.finditer(r'\w+', spec):
        name = m.group()
        if name not in ffprops:
            ffprops[name] = set()
        ffprops[name].add(argattr)

fix_function_args = {}
parse_fix_args_spec(fix_function_args, GwyBooleanOut_args, 'type:GwyBooleanOutArg')
parse_fix_args_spec(fix_function_args, GwyIntOut_args, 'type:GwyIntOutArg')
parse_fix_args_spec(fix_function_args, GwyDoubleOut_args, 'type:GwyDoubleOutArg')
parse_fix_args_spec(fix_function_args, GwyRGBAOut_args, 'type:GwyRGBAOutArg')
parse_fix_args_spec(fix_function_args, GwyIntInOut_args, 'type:GwyIntInOutArg')
parse_fix_args_spec(fix_function_args, GObject_args, 'type:GObject*')
parse_fix_args_spec(fix_function_args, constgchar_args, 'type:const-gchar*')
parse_fix_args_spec(fix_function_args, skip_args, 'skip:')
parse_fix_args_spec(fix_function_args, default_args, 'default:')
parse_fix_args_spec(fix_function_args, nullable_args, 'nullable')

fix_function_props = {}
parse_fix_func_props(fix_function_props, caller_owns_return_funcs, 'caller-owns-return')
parse_fix_func_props(fix_function_props, remove_funcs, 'remove')

added_functions_in_wrap_calls = {}
parse_fix_func_props(added_functions_in_wrap_calls, pygwy_added_funcs, 'pygwy-added')

remove_methods = set([x for x in remove_methods.split() if x])
boxed_sequence_likes = set([x for x in boxed_sequence_likes.split() if x])

parser = defsparser.DefsParser("pygwy.defs.tmp")
parser.startParsing()
parser.include('extra.defs')

# We want to discard the originals of wrapped functions so that we do not see
# `could-not-write-method messages' in the log for functions we did wrap.
wrapped_funcs = {}
for f in parser.functions:
    assert not f.name[0].isdigit()
    if f.name.endswith('_pygwy'):
        wrapped_funcs[unwrap_name(f.c_name, True)] = None

for i, f in enumerate(parser.functions):
    if f.c_name in wrapped_funcs:
        assert wrapped_funcs[f.c_name] is None
        wrapped_funcs[f.c_name] = i
        continue

    f.name = unwrap_name(f.name)
    c_name = unwrap_name(f.c_name)
    if c_name in fix_function_props:
        for funcprop in fix_function_props[c_name]:
            if funcprop == 'caller-owns-return':
                f.caller_owns_return = True
            elif funcprop == 'remove':
                wrapped_funcs[f.c_name] = i
            else:
                assert not 'Reached'

        del fix_function_props[c_name]

    if hasattr(f, 'of_object'):
        qname = f.of_object + '.' + f.name
        if qname in remove_methods:
            wrapped_funcs[qname] = i

    if c_name in fix_function_args:
        to_fix = fix_function_args[c_name]
        for p in f.params:
            if p.pname not in to_fix:
                continue
            for argattr in to_fix[p.pname]:
                if argattr == 'nullable':
                    p.pnull = True
                    p.pdflt = 'NULL'
                elif argattr.startswith('default:'):
                    p.pdflt = argattr.split(':')[1]
                elif argattr.startswith('skip:'):
                    # To skip a parameter completely we define it to be of
                    # GwySkipArg and provide a default value.  The codegen
                    # handler for that type then just passed this value without
                    # putting any correspding arg into the Python protptype.
                    p.ptype = 'GwySkipArg'
                    p.pdflt = argattr.split(':')[1]
                elif argattr.startswith('type:'):
                    p.ptype = argattr.split(':')[1]
                else:
                    assert not 'Reached'
            del to_fix[p.pname]
        if 'return' in to_fix:
            for argattr in to_fix['return']:
                if argattr.startswith('type:'):
                    f.ret = argattr.split(':')[1]
                else:
                    assert not 'Reached'
            del to_fix['return']

        if not fix_function_args[c_name]:
            del fix_function_args[c_name]

# Unused fix items mean potential errors.
for f in fix_function_args.items():
    sys.stderr.write('Warning: unused fixarg %s: %s\n' % (f[0], str(f[1])))
for f in fix_function_props.items():
    sys.stderr.write('Warning: unused fixfunc %s: %s\n' % (f[0], str(f[1])))
for f, i in wrapped_funcs.items():
    if f in added_functions_in_wrap_calls:
        continue
    if i is None:
        sys.stderr.write('Warning: wrapper for %s does not wrap existing function\n' % f)

for i in reversed(sorted(wrapped_funcs.values())):
    if i is not None:
        del parser.functions[i]

parser.write_defs()

# Generate setters and sequence-like wrappers for boxed types.  Generally, if
# we declare something as boxed type with some fields we want it work as a
# plain C struct.
boxed_setter_template = '''\
%%%%
override-attr %(cname)s.%(attr)s
static int
_wrap_%(lcname)s__set_%(attr)s(PyGBoxed *self, PyObject *value, void *closure)
{
    return assign_%(convertor)s(value, &pyg_boxed_get(self, %(cname)s)->%(attr)s, "%(pyname)s.%(attr)s");
}
'''

boxed_sequence_template = '''\
%%%%
override-slot %(cname)s.tp_as_sequence

static ssize_t
_sq_%(lcname)s_length(G_GNUC_UNUSED PyGObject *self)
{
    return %(nfields)u;
}

static PyObject*
_sq_%(lcname)s_item(PyGObject *self, Py_ssize_t i)
{
    const %(cname)s *%(name)s = pyg_boxed_get(self, %(cname)s);

%(get_fields)s
    PyErr_SetString(PyExc_IndexError, "%(pyname)s index out of range");
    return NULL;
}

static int
_sq_%(lcname)s_ass_item(PyGObject *self, Py_ssize_t i, PyObject *item)
{
    %(cname)s *%(name)s = pyg_boxed_get(self, %(cname)s);

%(set_fields)s
    PyErr_SetString(PyExc_IndexError, "%(pyname)s index out of range");
    return -1;
}

static const PySequenceMethods _wrap_%(lcname)s_tp_as_sequence = {
    (lenfunc)_sq_%(lcname)s_length,
    NULL,
    NULL,
    (ssizeargfunc)_sq_%(lcname)s_item,
    NULL,
    (ssizeobjargproc)_sq_%(lcname)s_ass_item,
    NULL,
    NULL,
    NULL,
    NULL,
};
'''

boxed_sequence_get_field_template = '''\
    if (i == %(i)u)
        return %(convertor)s(%(name)s->%(attr)s);
'''

boxed_sequence_set_field_template = '''\
    if (i == %(i)u)
        return assign_%(convertor)s(item, &%(name)s->%(attr)s, "%(pyname)s %(attr)s item");
'''

extract_convertors = {
    'gdouble': 'PyFloat_FromDouble',
    'gint': 'PyInt_FromLong',
}

assign_convertors = {
    'gdouble': 'number_as_double',
    'gint': 'number_as_int',
}

boxed_overrides = ['/* This is a %s file */' % 'GENERATED']
for b in parser.boxes:
    if not b.fields:
        continue

    substdict = {
        'cname': b.c_name,
        'pyname': b.name,
        'lcname': b.typecode.lower().replace('_type_', '_', 1),
        'name': b.name.lower(),
    }
    for typ, attr in b.fields:
        if typ not in assign_convertors:
            continue

        substdict['attr'] = attr
        substdict['convertor'] = assign_convertors[typ]
        setter = boxed_setter_template % substdict
        boxed_overrides.append(setter)

    if b.name not in boxed_sequence_likes:
        continue

    sq_getters = []
    sq_setters = []
    i = 0
    for typ, attr in b.fields:
        if typ not in extract_convertors or typ not in assign_convertors:
            continue

        substdict['attr'] = attr
        substdict['i'] = i
        substdict['convertor'] = extract_convertors[typ]
        sq_getters.append(boxed_sequence_get_field_template % substdict)
        substdict['convertor'] = assign_convertors[typ]
        sq_setters.append(boxed_sequence_set_field_template % substdict)
        boxed_overrides.append(setter)
        i += 1

    if not i:
        continue

    del substdict['attr']
    del substdict['convertor']
    del substdict['i']
    substdict['get_fields'] = '\n'.join(sq_getters)
    substdict['set_fields'] = '\n'.join(sq_setters)
    substdict['nfields'] = i
    sq_override = boxed_sequence_template % substdict
    boxed_overrides.append(sq_override)

file('boxed.override', 'w').write('\n'.join(boxed_overrides))

