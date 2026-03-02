// vim: set ft=cocci :
@@
expression f1;
expression f2;
@@
- g_free(*f1);
- *f1 = g_strdup(f2);
+ gwy_assign_string(f1, f2);

@@
expression f1;
expression f2;
@@
- g_free(f1);
- f1 = g_strdup(f2);
+ gwy_assign_string(&f1, f2);

