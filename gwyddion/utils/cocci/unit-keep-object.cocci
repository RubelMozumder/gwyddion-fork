// vim: set ft=cocci :
@@
GwySIUnit *u;
expression o, s;
@@
-u = gwy_si_unit_new(s);
-gwy_data_field_set_si_unit_xy(o, u);
-g_object_unref(u);
+gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(o), s);

@@
GwySIUnit *u;
expression o, s;
@@
-u = gwy_si_unit_new(s);
-gwy_data_field_set_si_unit_z(o, u);
-g_object_unref(u);
+gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(o), s);

@@
GwySIUnit *u;
expression o, s;
@@
-u = gwy_si_unit_new(s);
-gwy_data_line_set_si_unit_x(o, u);
-g_object_unref(u);
+gwy_si_unit_set_from_string(gwy_data_line_get_si_unit_x(o), s);

@@
GwySIUnit *u;
expression o, s;
@@
-u = gwy_si_unit_new(s);
-gwy_data_line_set_si_unit_y(o, u);
-g_object_unref(u);
+gwy_si_unit_set_from_string(gwy_data_line_get_si_unit_y(o), s);

@@
GwySIUnit *u;
expression o, s;
@@
-u = gwy_si_unit_new(s);
-gwy_lawn_set_si_unit_xy(o, u);
-g_object_unref(u);
+gwy_si_unit_set_from_string(gwy_lawn_get_si_unit_xy(o), s);

@@
GwySIUnit *u;
expression o, s;
@@
-u = gwy_si_unit_new(s);
-gwy_brick_set_si_unit_x(o, u);
-g_object_unref(u);
+gwy_si_unit_set_from_string(gwy_brick_get_si_unit_x(o), s);

@@
GwySIUnit *u;
expression o, s;
@@
-u = gwy_si_unit_new(s);
-gwy_brick_set_si_unit_y(o, u);
-g_object_unref(u);
+gwy_si_unit_set_from_string(gwy_brick_get_si_unit_y(o), s);

@@
GwySIUnit *u;
expression o, s;
@@
-u = gwy_si_unit_new(s);
-gwy_brick_set_si_unit_z(o, u);
-g_object_unref(u);
+gwy_si_unit_set_from_string(gwy_brick_get_si_unit_z(o), s);

@@
GwySIUnit *u;
expression o, s;
@@
-u = gwy_si_unit_new(s);
-gwy_brick_set_si_unit_w(o, u);
-g_object_unref(u);
+gwy_si_unit_set_from_string(gwy_brick_get_si_unit_w(o), s);

@@
GwySIUnit *u;
expression o, s;
@@
-u = gwy_si_unit_new(s);
-gwy_surface_set_si_unit_xy(o, u);
-g_object_unref(u);
+gwy_si_unit_set_from_string(gwy_surface_get_si_unit_xy(o), s);

@@
GwySIUnit *u;
expression o, s;
@@
-u = gwy_si_unit_new(s);
-gwy_surface_set_si_unit_z(o, u);
-g_object_unref(u);
+gwy_si_unit_set_from_string(gwy_surface_get_si_unit_z(o), s);

