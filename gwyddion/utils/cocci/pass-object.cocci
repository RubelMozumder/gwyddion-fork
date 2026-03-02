// vim: set ft=cocci :
@@
identifier i;
expression c, k;
@@
-gwy_container_set_object(c, k, i);
-g_object_unref(i);
+gwy_container_pass_object(c, k, i);

@@
identifier i;
expression c, k;
@@
-gwy_container_set_object_by_name(c, k, i);
-g_object_unref(i);
+gwy_container_pass_object_by_name(c, k, i);

