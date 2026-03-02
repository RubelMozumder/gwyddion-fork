// vim: set ft=cocci :
@@
GwyDataField *f1;
expression f2;
@@
-gwy_serializable_clone(G_OBJECT(f1), G_OBJECT(f2));
+gwy_data_field_assign(f2, f1);

@@
expression f1;
GwyDataField *f2;
@@
-gwy_serializable_clone(G_OBJECT(f1), G_OBJECT(f2));
+gwy_data_field_assign(f2, f1);

@@
GwyDataLine *l1;
expression l2;
@@
-gwy_serializable_clone(G_OBJECT(l1), G_OBJECT(l2));
+gwy_data_line_assign(l2, l1);

@@
expression l1;
GwyDataLine *l2;
@@
-gwy_serializable_clone(G_OBJECT(l1), G_OBJECT(l2));
+gwy_data_line_assign(l2, l1);

@@
GwyBrick *b1;
expression b2;
@@
-gwy_serializable_clone(G_OBJECT(b1), G_OBJECT(b2));
+gwy_brick_assign(b2, b1);

@@
expression b1;
GwyBrick *b2;
@@
-gwy_serializable_clone(G_OBJECT(b1), G_OBJECT(b2));
+gwy_brick_assign(b2, b1);

@@
GwySelection *s1;
expression s2;
@@
-gwy_serializable_clone(G_OBJECT(s1), G_OBJECT(s2));
+gwy_selection_assign(s2, s1);

@@
expression s1;
GwySelection *s2;
@@
-gwy_serializable_clone(G_OBJECT(s1), G_OBJECT(s2));
+gwy_selection_assign(s2, s1);

