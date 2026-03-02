/*
 * This is not a real header.
 *
 * It contains prototypes for `functions' that are actually macros in C but are
 * used as functions and we want to have them in Python.
 */
gboolean     gwy_container_contains_by_name   (GwyContainer *container,
                                               const gchar *n);
gboolean     gwy_container_remove_by_name     (GwyContainer *container,
                                               const gchar *n);
gboolean     gwy_container_rename_by_name     (GwyContainer *container,
                                               const gchar *n,
                                               const gchar *new_name,
                                               gboolean force);
void         gwy_container_set_boolean_by_name(GwyContainer *container,
                                               const gchar *n,
                                               gboolean v);
void         gwy_container_set_double_by_name (GwyContainer *container,
                                               const gchar *n,
                                               gdouble v);
void         gwy_container_set_enum_by_name   (GwyContainer *container,
                                               const gchar *n,
                                               guint v);
void         gwy_container_set_int32_by_name  (GwyContainer *container,
                                               const gchar *n,
                                               gint32 v);
void         gwy_container_set_int64_by_name  (GwyContainer *container,
                                               const gchar *n,
                                               gint64 v);
void         gwy_container_set_object_by_name (GwyContainer *container,
                                               const gchar *n,
                                               GObject *v);
void         gwy_container_set_uchar_by_name  (GwyContainer *container,
                                               const gchar *n,
                                               guchar v);
gboolean     gwy_container_get_boolean_by_name(GwyContainer *container,
                                               const gchar *n);
gdouble      gwy_container_get_double_by_name (GwyContainer *container,
                                               const gchar *n);
guint        gwy_container_get_enum_by_name   (GwyContainer *container,
                                               const gchar *n);
gint32       gwy_container_get_int32_by_name  (GwyContainer *container,
                                               const gchar *n);
gint64       gwy_container_get_int64_by_name  (GwyContainer *container,
                                               const gchar *n);
guchar       gwy_container_get_uchar_by_name  (GwyContainer *container,
                                               const gchar *n);
GObject*     gwy_container_get_object_by_name (GwyContainer *container,
                                               const gchar *n);
const gchar* gwy_container_get_string_by_name (GwyContainer *container,
                                               const gchar *n);

GwyBrick*           gwy_brick_duplicate            (GwyBrick *brick);
GwyContainer*       gwy_container_duplicate        (GwyContainer *container);
GwyDataField*       gwy_data_field_duplicate       (GwyDataField *data_field);
gdouble             gwy_data_field_get_xmeasure    (GwyDataField *data_field);
gdouble             gwy_data_field_get_ymeasure    (GwyDataField *data_field);
GwyDataLine*        gwy_data_line_duplicate        (GwyDataLine *data_line);
GwySpectra*         gwy_spectra_duplicate          (GwySpectra *spectra);
GwySurface*         gwy_surface_duplicate          (GwySurface *surface);
GwySIUnit*          gwy_si_unit_duplicate          (GwySIUnit *siunit);
GwyGraphModel*      gwy_graph_model_duplicate      (GwyGraphModel *graph_model);
GwyGraphCurveModel* gwy_graph_curve_model_duplicate(GwyGraphCurveModel *graph_curve_model);
GwySelection*       gwy_selection_duplicate        (GwySelection *selection);
GwyStringList*      gwy_string_list_duplicate      (GwyStringList *string_list);

GType gwy_vector_layer_get_selection_type(GwyVectorLayer *layer);

GType             gwy_si_value_format_get_type (void);
GwySIValueFormat* gwy_si_value_format_new      (gdouble magnitude,
                                                gint precision,
                                                const gchar *units);
GwySIValueFormat* gwy_si_value_format_copy     (GwySIValueFormat *format);
void              gwy_si_value_format_free     (GwySIValueFormat *format);
GwySIValueFormat* gwy_si_value_format_clone    (GwySIValueFormat *source,
                                                GwySIValueFormat *dest);
void              gwy_si_value_format_set_units(GwySIValueFormat *format,
                                                const gchar *units);
