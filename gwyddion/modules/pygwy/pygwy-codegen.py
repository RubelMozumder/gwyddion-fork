#!/usr/bin/python2
# $Id: pygwy-codegen.py 21886 2019-02-15 13:14:48Z yeti-dn $
# Runtime for pygtk module codegen.py and overriden functions for Gwyddion data
# types.
# Public domain

import sys, os, re, string

# Add codegen path to our import path.  NB: codegen does its own argument
# parsing so we must not an option parser.  Use environment to pass other info.
codegendir = os.environ['PYGTK_CODEGENDIR'].strip()
if codegendir:
    sys.path.insert(0, codegendir)

# Load it
from codegen import *

# Override function_wrapper to deal with output arguments.
class RetTupleHandler():
    def __init__(self, handler, returns):
        self.returns = returns
        self.handler = handler

    def write_return(self, ptype, caller_owns_return, info):
        def rewrite_fake_retvals(self, ptype, caller_owns_return, info):
            # Handle GwyArrayFuncStatus specially.  We need the check to happen
            # before anything else (so that an exception is raised and we do
            # not access the arrays that the callee freed for us).  But we do
            # not want the rest of the return code for GwyArrayFuncStatus.
            if ptype != 'GwyArrayFuncStatus':
                return

            n = len(info.codeafter)
            self.handler.write_return(ptype, caller_owns_return, info)
            added_lines = info.codeafter[n:]
            for i, text in enumerate(added_lines):
                # Kill the return code from GwyArrayFuncStatus
                text = re.sub(r'Py_RETURN_NONE;', r'', text)
                added_lines[i] = text
            # Move the remaining part (exception raising) before the actual
            # value returning code.
            del info.codeafter[n:]
            info.codeafter[0:0] = added_lines

        ignored_returns = ('none', 'GwyArrayFuncStatus')
        has_orig_retval = int(not (ptype is None or ptype in ignored_returns))
        actual_returns = self.returns + has_orig_retval
        # We should not get here when there are no out-arguments.
        # If there is exactly one out-argument we do not want to return a
        # single-item tuple.  Convert the tuple back to a plain return value.
        # We could do that more easily by checking the tuple length at the
        # end but this way we generate a direct code that avoid any tuple
        # construction completely.  This is most useful for struct
        # out-arguments such as GwyXYZ, GwyRGBA, ...
        assert actual_returns > 0
        if actual_returns == 1:
            assert not has_orig_retval
            for i, text in enumerate(info.codeafter):
                text = re.sub(r'PyTuple_SetItem\(py_tuple_ret,\s*py_tuple_index\+\+,\s*(.*)\);',
                              r'return \1;',
                              text)
                info.codeafter[i] = text
            rewrite_fake_retvals(self, ptype, caller_owns_return, info)
            return

        # The typical case why we get here, actual multiple return values.
        # Must distinguish the cases when we have a original retval and when
        # we do not.  Furthermore, if the original return value is
        # GwyArrayFuncStatus we must rewrite its code.
        info.varlist.add('PyObject', '*py_tuple_ret')
        info.varlist.add('int', 'py_tuple_index')

        info.codebefore.append('    py_tuple_ret = PyTuple_New(%d);\n' % actual_returns)
        info.codebefore.append('    py_tuple_index = %d;\n' % has_orig_retval)
        if has_orig_retval:
            self.handler.write_return(ptype, caller_owns_return, info)
            info.varlist.add("PyObject", '*py_original_ret')
            for i, text in enumerate(info.codeafter):
                info.codeafter[i] = re.sub(r'return\s+(?!NULL)(.*)',
                                           r'py_original_ret = \1',
                                           text) + '\n'
            info.codeafter.append('    PyTuple_SetItem(py_tuple_ret, 0, py_original_ret);\n')

        info.codeafter.append('    return py_tuple_ret;\n')
        rewrite_fake_retvals(self, ptype, caller_owns_return, info)

def write_function_wrapper(self, function_obj, template,
                           handle_return=0, is_method=0, kwargs_needed=0,
                           substdict=None):
    '''This function is the guts of all functions that generate
    wrappers for functions, methods and constructors.'''

    if substdict is None:
        substdict = {}

    info = argtypes.WrapperInfo()
    returns = 0

    substdict.setdefault('errorreturn', 'NULL')

    # for methods, we want the leading comma
    if is_method:
        info.arglist.append('')

    if function_obj.varargs:
        raise argtypes.ArgTypeNotFoundError("varargs functions not supported")

    for param in function_obj.params:
        if param.pdflt != None and '|' not in info.parsestr:
            info.add_parselist('|', [], [])
        handler = argtypes.matcher.get(param.ptype)
        if (param.ptype.startswith('Gwy') and param.ptype.endswith('OutArg')
            or param.ptype.endswith('Value')):
            returns += 1
        handler.write_param(param.ptype, param.pname, param.pdflt,
                            param.pnull, info)

    substdict['setreturn'] = ''
    if handle_return:
        if function_obj.ret not in ('none', None):
            substdict['setreturn'] = 'ret = '
        handler = argtypes.matcher.get(function_obj.ret)
        if returns > 0:
            handler = RetTupleHandler(handler, returns)
        handler.write_return(function_obj.ret,
                             function_obj.caller_owns_return, info)

    if function_obj.deprecated != None:
        deprecated = self.deprecated_tmpl % {
            'deprecationmsg': function_obj.deprecated,
            'errorreturn': substdict['errorreturn']
        }
    else:
        deprecated = ''

    # if name isn't set, set it to function_obj.name
    substdict.setdefault('name', function_obj.name)

    if function_obj.unblock_threads:
        substdict['begin_allow_threads'] = 'pyg_begin_allow_threads;'
        substdict['end_allow_threads'] = 'pyg_end_allow_threads;'
    else:
        substdict['begin_allow_threads'] = ''
        substdict['end_allow_threads'] = ''

    if self.objinfo:
        substdict['typename'] = self.objinfo.c_name
    substdict.setdefault('cname',  function_obj.c_name)
    substdict['varlist'] = info.get_varlist()
    substdict['typecodes'] = info.parsestr
    substdict['parselist'] = info.get_parselist()
    substdict['arglist'] = info.get_arglist()
    substdict['codebefore'] = deprecated + (
        string.replace(info.get_codebefore(),
        'return NULL', 'return ' + substdict['errorreturn'])
    )
    substdict['codeafter'] = (
        string.replace(info.get_codeafter(),
                       'return NULL',
                       'return ' + substdict['errorreturn'])
    )

    if info.parsestr or kwargs_needed:
        substdict['parseargs'] = self.parse_tmpl % substdict
        substdict['extraparams'] = ', PyObject *args, PyObject *kwargs'
        flags = 'METH_VARARGS|METH_KEYWORDS'

        # prepend the keyword list to the variable list
        substdict['varlist'] = info.get_kwlist() + substdict['varlist']
    else:
        substdict['parseargs'] = ''
        substdict['extraparams'] = ''
        flags = 'METH_NOARGS'

    return template % substdict, flags

Wrapper.write_function_wrapper = write_function_wrapper

# Skipped arguments
# We just write the `default' value as the C function argument.
class GwySkipArg(argtypes.ArgType):
    def write_param(self, ptype, pname, pdflt, pnull, info):
        info.arglist.append(pdflt)

# Atomic (kind-of) output argument types
# The main handler write_function_wrapper() looks for things called
# Gwy.*OutArg and adds them to the list of out-args.  If there are any the
# RetTupleHandler() is invoked to produce a tuple for the return value.
class GwyBooleanOutArg(argtypes.ArgType):
    def write_param(self, ptype, pname, pdflt, pnull, info):
        info.varlist.add(ptype, pname)
        info.arglist.append('&'+pname)
        info.codebefore.append('    '+pname+' = FALSE;\n')
        info.codeafter.append('    PyTuple_SetItem(py_tuple_ret, py_tuple_index++, PyBool_FromLong('+pname+'));\n')

class GwyIntOutArg(argtypes.ArgType):
    def write_param(self, ptype, pname, pdflt, pnull, info):
        info.varlist.add(ptype, pname)
        info.arglist.append('&'+pname)
        info.codebefore.append('    '+pname+' = 0;\n')
        info.codeafter.append('    PyTuple_SetItem(py_tuple_ret, py_tuple_index++, PyInt_FromLong('+pname+'));\n')

class GwyDoubleOutArg(argtypes.ArgType):
    def write_param(self, ptype, pname, pdflt, pnull, info):
        info.varlist.add(ptype, pname)
        info.arglist.append('&'+pname)
        info.codebefore.append('    '+pname+' = 0.0;\n')
        info.codeafter.append('    PyTuple_SetItem(py_tuple_ret, py_tuple_index++, PyFloat_FromDouble('+pname+'));\n')

class GwyRGBAOutArg(argtypes.ArgType):
    def write_param(self, ptype, pname, pdflt, pnull, info):
        info.varlist.add(ptype, pname)
        info.arglist.append('&'+pname)
        info.codebefore.append('    gwy_clear(&'+pname+', 1);\n')
        info.codeafter.append('    PyTuple_SetItem(py_tuple_ret, py_tuple_index++, pyg_boxed_new(GWY_TYPE_RGBA, (GwyRGBA*)&'+pname+', TRUE, TRUE));\n')

class GwyIntInOutArg(argtypes.ArgType):
    def write_param(self, ptype, pname, pdflt, pnull, info):
        info.varlist.add(ptype, pname)
        info.arglist.append('&'+pname)
        info.add_parselist('i', ['&'+pname], [pname])
        info.codeafter.append('    PyTuple_SetItem(py_tuple_ret, py_tuple_index++, PyInt_FromLong('+pname+'));\n')

class GwyDoubleInOutArg(argtypes.ArgType):
    def write_param(self, ptype, pname, pdflt, pnull, info):
        info.varlist.add(ptype, pname)
        info.arglist.append('&'+pname)
        info.add_parselist('i', ['&'+pname], [pname])
        info.codeafter.append('    PyTuple_SetItem(py_tuple_ret, py_tuple_index++, PyFloat_FromDouble('+pname+'));\n')

# Strings
class GwyGString(argtypes.ArgType):
    def write_return(self, ptype, ownsreturn, info):
        info.varlist.add('GString', '*ret')
        info.codeafter.append('    return create_string_consume_gstring(ret);\n')

# Fake argument types
class GwyArrayFuncStatus(argtypes.ArgType):
    "Boolean indicating if the arrays passed to the function had sane sizes."
    def write_return(self, ptype, ownsreturn, info):
        # This is the simple case when there are no out-args.
        # When there are out-args RetTupleHandler.write_return() has to rewrite
        # this code.
        info.varlist.add('gboolean', 'ret')
        info.codeafter.append('    if (!ret) {\n')
        info.codeafter.append('        PyErr_SetString(PyExc_ValueError, "Incompatible sequence length (not a multiple or does not match other arguments)");\n')
        info.codeafter.append('        return NULL;\n')
        info.codeafter.append('    }\n')
        info.codeafter.append('    Py_RETURN_NONE;\n')

# Array argument types
class GwyDoubleArray(argtypes.ArgType):
    "Array of doubles passed by value."
    def write_param(self, ptype, pname, pdflt, pnull, info):
        # NB: We do not free the GArray.  The callee must always do it.  This
        # makes memory leaks less likely when we raise exceptions and have
        # multiple arrays.
        info.varlist.add('GArray', '*'+pname + ' = NULL');
        info.varlist.add('PyObject', '*'+pname+'_pyobj')
        if pnull:
            info.add_parselist('|O', ['&'+pname+'_pyobj'], [pname])
        else:
            info.add_parselist('O', ['&'+pname+'_pyobj'], [pname])
        info.arglist.append(pname)
        xnull = pname + '_pyobj && ' if pnull else ''
        info.codebefore.append('    if ('+xnull+'!('+pname+' = create_double_garray_from_sequence('+pname+'_pyobj))) {\n')
        info.codebefore.append('        PyErr_SetString(PyExc_TypeError, "Parameter \''+pname+'\' must be a sequence of floats");\n')
        info.codebefore.append('        return NULL;\n')
        info.codebefore.append('    }\n')

    def write_return(self, ptype, ownsreturn, info):
        info.varlist.add('GArray', '*ret')
        info.codeafter.append('    return create_list_consume_double_garray(ret);\n')

class GwyDoubleArrayOutArg(argtypes.ArgType):
    def write_param(self, ptype, pname, pdflt, pnull, info):
        info.varlist.add('GArray', '*'+pname)
        info.arglist.append(pname)
        info.codebefore.append('    '+pname+' = g_array_new(FALSE, FALSE, sizeof(gdouble));\n')
        info.codeafter.append('    PyTuple_SetItem(py_tuple_ret, py_tuple_index++, create_list_consume_double_garray('+pname+'));\n')

class GwyIntArray(argtypes.ArgType):
    def write_param(self, ptype, pname, pdflt, pnull, info):
        # NB: We do not free the GArray.  The callee must always do it.  This
        # makes memory leaks less likely when we raise exceptions and have
        # multiple arrays.
        info.varlist.add('GArray', '*'+pname + ' = NULL');
        info.varlist.add('PyObject', '*'+pname+'_pyobj')
        if pnull:
            info.add_parselist('|O', ['&'+pname+'_pyobj'], [pname])
        else:
            info.add_parselist('O', ['&'+pname+'_pyobj'], [pname])
        info.arglist.append(pname)
        xnull = pname + '_pyobj && ' if pnull else ''
        info.codebefore.append('    if ('+xnull+'!('+pname+' = create_int_garray_from_sequence('+pname+'_pyobj))) {\n')
        info.codebefore.append('        PyErr_SetString(PyExc_TypeError, "Parameter \''+pname+'\' must be a sequence of integers");\n')
        info.codebefore.append('        return NULL;\n')
        info.codebefore.append('    }\n')

    def write_return(self, ptype, ownsreturn, info):
        info.varlist.add('GArray', '*ret')
        info.codeafter.append('    return create_list_consume_int_garray(ret);\n')

class GwyIntArrayOutArg(argtypes.ArgType):
    "Array of integers passed by value."
    def write_param(self, ptype, pname, pdflt, pnull, info):
        info.varlist.add('GArray', '*'+pname)
        info.arglist.append(pname)
        info.codebefore.append('    '+pname+' = g_array_new(FALSE, FALSE, sizeof(gint));\n')
        info.codeafter.append('    PyTuple_SetItem(py_tuple_ret, py_tuple_index++, create_list_consume_int_garray('+pname+'));\n')

class GwyStringArray(argtypes.ArgType):
    def write_param(self, ptype, pname, pdflt, pnull, info):
        # NB: We do not free the GArray.  The callee must always do it.  This
        # makes memory leaks less likely when we raise exceptions and have
        # multiple arrays.
        info.varlist.add('GArray', '*'+pname + ' = NULL');
        info.varlist.add('PyObject', '*'+pname+'_pyobj')
        if pnull:
            info.add_parselist('|O', ['&'+pname+'_pyobj'], [pname])
        else:
            info.add_parselist('O', ['&'+pname+'_pyobj'], [pname])
        info.arglist.append(pname)
        xnull = pname + '_pyobj && ' if pnull else ''
        info.codebefore.append('    if ('+xnull+'!('+pname+' = create_string_garray_from_sequence('+pname+'_pyobj))) {\n')
        info.codebefore.append('        PyErr_SetString(PyExc_TypeError, "Parameter \''+pname+'\' must be a sequence of strings");\n')
        info.codebefore.append('        return NULL;\n')
        info.codebefore.append('    }\n')

    def write_return(self, ptype, ownsreturn, info):
        info.varlist.add('GArray', '*ret')
        info.codeafter.append('    return create_list_consume_string_garray(ret);\n')

class GwyConstStringArray(argtypes.ArgType):
    def write_param(self, ptype, pname, pdflt, pnull, info):
        # We cannot get input const array because strings from Python are
        # duplicated to handle the string/unicode duality.
        assert not 'Reached'

    def write_return(self, ptype, ownsreturn, info):
        info.varlist.add('GArray', '*ret')
        info.codeafter.append('    return create_list_consume_const_string_garray(ret);\n')

# Array argument types
class GwyDataFieldArray(argtypes.ArgType):
    def write_param(self, ptype, pname, pdflt, pnull, info):
        # Input arrays of DataFields are not implemented because they do not
        # occur.
        assert not 'Reached'

    def write_return(self, ptype, ownsreturn, info):
        info.varlist.add('GArray', '*ret')
        info.codeafter.append('    return create_list_consume_data_field_garray(ret);\n')

# Pointer/boxed argument types
class GwyConstPointerArg(argtypes.PointerArg):
    "Pointers that have const (like presets)."
    def write_return(self, ptype, ownsreturn, info):
        assert ptype[-1] == '*'
        info.varlist.add('const '+self.typename, '*ret')
        info.codeafter.append('    return pyg_pointer_new('+self.typecode+', (gpointer)ret);\n')

# GTK+ types we need because they appear as arguments?
# We certainly get `No ArgType' messages for types we do not list here.
argtypes.matcher.register_object('GdkGC', None, 'GDK_TYPE_GC')
argtypes.matcher.register_object('GdkEvent', None, 'GDK_TYPE_EVENT')
argtypes.matcher.register_object('GdkGLConfig', None, 'GDK_TYPE_GL_CONFIG')
argtypes.matcher.register_object('GdkPixbuf', None, 'GDK_TYPE_PIXBUF')
argtypes.matcher.register_object('GdkDrawable', None, 'GDK_TYPE_DRAWABLE')

argtypes.matcher.register_object('GtkWidget', None, 'GTK_TYPE_WIDGET')
argtypes.matcher.register_object('GtkDialog', None, 'GTK_TYPE_DIALOG')
argtypes.matcher.register_object('GtkObject', None, 'GTK_TYPE_OBJECT')
argtypes.matcher.register_object('GtkListStore', None, 'GTK_TYPE_LIST_STORE')
argtypes.matcher.register_object('GtkWindow', None, 'GTK_TYPE_WINDOW')
argtypes.matcher.register_object('GtkTooltips', None, 'GTK_TYPE_TOOLTIPS')
argtypes.matcher.register_object('GtkComboBox', None, 'GTK_TYPE_COMBO_BOX')
argtypes.matcher.register_object('GtkTreeView', None, 'GTK_TYPE_TREE_VIEW')
argtypes.matcher.register_object('GtkTable', None, 'GTK_TYPE_TABLE')
argtypes.matcher.register_object('GtkButton', None, 'GTK_TYPE_BUTTON')
argtypes.matcher.register_object('GtkTextBuffer', None, 'GTK_TYPE_TEXT_BUFFER')
argtypes.matcher.register_object('GtkAccelGroup', None, 'GTK_TYPE_ACCEL_GROUP')

def preregister_plain(argfunc, mixed_name, uppercase_type_name):
    argtypes.matcher.register(mixed_name,
                              argfunc(mixed_name, uppercase_type_name))

preregister_plain(argtypes.EnumArg, 'GdkLineStyle', 'GDK_TYPE_LINE_STYLE')
preregister_plain(argtypes.EnumArg, 'GtkOrientation', 'GTK_TYPE_ORIENTATION')
preregister_plain(argtypes.EnumArg, 'GtkPositionType', 'GTK_TYPE_POSITION_TYPE')
preregister_plain(argtypes.EnumArg, 'GtkUpdateType', 'GTK_TYPE_UPDATE_TYPE')
preregister_plain(argtypes.BoxedArg, 'GtkTreePath', 'GTK_TYPE_TREE_PATH')
preregister_plain(argtypes.BoxedArg, 'GtkTreeIter', 'GTK_TYPE_TREE_ITER')
preregister_plain(argtypes.BoxedArg, 'GString', 'G_TYPE_STRING')
del preregister_plain

# Gwyddion arg types
argtypes.matcher.register('GwySkipArg', GwySkipArg())
argtypes.matcher.register('GQuark', argtypes.IntArg())

# Simple out-arguments and in-out-arguments
argtypes.matcher.register('GwyDoubleOutArg', GwyDoubleOutArg())
argtypes.matcher.register('GwyIntOutArg', GwyIntOutArg())
argtypes.matcher.register('GwyBooleanOutArg', GwyBooleanOutArg())
argtypes.matcher.register('GwyRGBAOutArg', GwyRGBAOutArg())
argtypes.matcher.register('GwyDoubleInOutArg', GwyDoubleInOutArg())
argtypes.matcher.register('GwyIntInOutArg', GwyIntInOutArg())

# Strings
argtypes.matcher.register('GString*', GwyGString())

# Arrays
argtypes.matcher.register('GwyArrayFuncStatus', GwyArrayFuncStatus())
argtypes.matcher.register('GwyDoubleArray*', GwyDoubleArray())
argtypes.matcher.register('GwyDoubleArrayOutArg', GwyDoubleArrayOutArg())
argtypes.matcher.register('GwyIntArray*', GwyIntArray())
argtypes.matcher.register('GwyIntArrayOutArg', GwyIntArrayOutArg())
argtypes.matcher.register('GwyStringArray*', GwyStringArray())
argtypes.matcher.register('GwyConstStringArray*', GwyConstStringArray())
argtypes.matcher.register('GwyDataFieldArray*', GwyDataFieldArray())

# Pointer/boxed stuff
argtypes.matcher.register('const-GwyTipModelPreset*', GwyConstPointerArg('GwyTipModelPreset', 'GWY_TYPE_TIP_MODEL_PRESET'))

# Run codegen
sys.exit(main(sys.argv))
