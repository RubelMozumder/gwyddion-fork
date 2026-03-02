/*
 *  $Id: pygwy.h 20677 2017-12-18 18:22:52Z yeti-dn $
 *  Copyright (C) 2008 Jan Horak
 *  E-mail: xhorak@gmail.com
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 *
 *  Description: This file contains pygwy console module.
 */
#ifndef __PYGWY_H__
#define __PYGWY_H__

#define pygwy_module_dir_name "pygwy"

#define pygwy_stderr_redirect_setup_code \
    "import sys, tempfile\n" \
    "_pygwy_output_redir = tempfile.TemporaryFile()\n" \
    "_pygwy_stderr_orig = sys.stderr\n" \
    "_pygwy_stdout_orig = sys.stdout\n" \
    "sys.stderr = _pygwy_output_redir\n" \
    "sys.stdout = _pygwy_output_redir\n"

#define pygwy_stderr_redirect_restore_code \
    "_pygwy_output_redir.seek(0)\n" \
    "_pygwy_stderr_string = _pygwy_output_redir.read()\n" \
    "_pygwy_output_redir.close()\n" \
    "sys.stderr = _pygwy_stderr_orig\n" \
    "sys.stdout = _pygwy_stdout_orig\n"

#define pygwy_stderr_redirect_readstr_code \
    "_pygwy_output_redir.flush()\n" \
    "_pygwy_stderr_pos = _pygwy_output_redir.tell()\n" \
    "_pygwy_output_redir.seek(0)\n" \
    "_pygwy_stderr_string = _pygwy_output_redir.read(_pygwy_stderr_pos)\n" \
    "_pygwy_output_redir.seek(0)"

PyObject* pygwy_create_environment (const gchar *filename,
                                    gboolean show_errors);
void      pygwy_initialize         (void);

G_GNUC_UNUSED
static inline void
pygwy_run_string(const char *cmd, int type, PyObject *globals, PyObject *locals)
{
    PyObject *ret = PyRun_String(cmd, type, globals, locals);
    if (!ret)
        PyErr_Print();
    else
        Py_DECREF(ret);
}

#endif
