// vim: set ft=cocci :
@@
expression c, k, s;
@@
-gwy_container_set_string(c, k, g_strdup(s));
+gwy_container_set_const_string(c, k, s);

@@
expression c, k, s;
@@
-gwy_container_set_string_by_name(c, k, g_strdup(s));
+gwy_container_set_const_string_by_name(c, k, s);

