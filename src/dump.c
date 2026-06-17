#include "gmdb.h"
#include <stdio.h>

extern MdbHandle *mdb;

static gchar *
dump_table_definitions(void)
{
    FILE *tmp;
    char buf[4096];
    size_t nread;
    GString *result;

    if (!mdb) return g_strdup("");

    tmp = tmpfile();
    if (!tmp) return g_strdup("");

    mdb_set_default_backend(mdb, "access");
    mdb_print_schema(mdb, tmp, NULL, NULL, MDB_SHEXP_DEFAULT);
    rewind(tmp);

    result = g_string_new("");
    while ((nread = fread(buf, 1, sizeof(buf), tmp)) > 0)
        g_string_append_len(result, buf, nread);

    fclose(tmp);
    return g_string_free(result, FALSE);
}

static gchar *
dump_sample_rows(int max_rows)
{
    GString *result;
    int i, j, row;
    MdbCatalogEntry *entry;
    MdbTableDef *table;
    MdbColumn *col;
    gchar *bound_data[256];
    int bound_lens[256];
    gchar *value;
    size_t length;
    FILE *tmp;
    char line_buf[4096];
    size_t nread;

    if (!mdb) return g_strdup("");

    result = g_string_new("");

    for (i = 0; i < mdb->num_catalog; i++) {
        entry = g_ptr_array_index(mdb->catalog, i);
        if (!mdb_is_user_table(entry)) continue;

        table = mdb_read_table(entry);
        if (!table) continue;
        mdb_read_columns(table);
        mdb_rewind_table(table);

        g_string_append_printf(result, "-- Table: %s\n", entry->object_name);

        for (j = 0; j < table->num_cols; j++) {
            if (j > 0) g_string_append(result, " | ");
            col = g_ptr_array_index(table->columns, j);
            g_string_append(result, col->name);
        }
        g_string_append(result, "\n");

        for (j = 0; j < table->num_cols; j++) {
            if (j > 0) g_string_append(result, "-+-");
            g_string_append_printf(result, "--------");
        }
        g_string_append(result, "\n");

        for (j = 0; j < table->num_cols; j++) {
            bound_data[j] = g_malloc0(MDB_BIND_SIZE);
            mdb_bind_column(table, j + 1, bound_data[j], &bound_lens[j]);
        }

        row = 0;
        while (mdb_fetch_row(table) && (max_rows <= 0 || row < max_rows)) {
            for (j = 0; j < table->num_cols; j++) {
                if (j > 0) g_string_append(result, " | ");
                col = g_ptr_array_index(table->columns, j);
                if (bound_lens[j]) {
                    tmp = tmpfile();
                    if (tmp) {
                        if (col->col_type == MDB_OLE) {
                            value = mdb_ole_read_full(mdb, col, &length);
                        } else {
                            value = bound_data[j];
                            length = bound_lens[j];
                        }
                        mdb_print_col(tmp, value, FALSE, col->col_type, length, "", "", MDB_BINEXPORT_RAW);
                        if (col->col_type == MDB_OLE)
                            free(value);
                        rewind(tmp);
                        while ((nread = fread(line_buf, 1, sizeof(line_buf), tmp)) > 0)
                            g_string_append_len(result, line_buf, nread);
                        fclose(tmp);
                    }
                }
            }
            g_string_append(result, "\n");
            row++;
        }

        for (j = 0; j < table->num_cols; j++)
            g_free(bound_data[j]);

        g_string_append(result, "\n");
    }

    return g_string_free(result, FALSE);
}

void
gmdb_dump_refresh_cb(GtkWidget *w, gpointer data)
{
    GtkBuilder *xml = GTK_BUILDER(data);
    GtkWidget *defs_check, *data_check;
    GtkWidget *defs_text, *data_text;
    GtkWidget *defs_scroll, *data_scroll;
    GtkTextBuffer *buffer;
    gboolean dump_defs, dump_data;
    int row_count;
    gchar *text;

    defs_check = GTK_WIDGET(gtk_builder_get_object(xml, "dump_defs_check"));
    data_check = GTK_WIDGET(gtk_builder_get_object(xml, "dump_data_check"));
    dump_defs = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(defs_check));
    dump_data = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(data_check));

    row_count = (int)gtk_spin_button_get_value(
        GTK_SPIN_BUTTON(gtk_builder_get_object(xml, "dump_rowcount_spin")));

    defs_text = GTK_WIDGET(gtk_builder_get_object(xml, "dump_defs_text"));
    data_text = GTK_WIDGET(gtk_builder_get_object(xml, "dump_data_text"));
    defs_scroll = GTK_WIDGET(gtk_builder_get_object(xml, "dump_defs_scroll"));
    data_scroll = GTK_WIDGET(gtk_builder_get_object(xml, "dump_data_scroll"));

    if (dump_defs && dump_data) {
        gtk_widget_show(defs_scroll);
        gtk_widget_show(data_scroll);
    } else if (dump_defs) {
        gtk_widget_show(defs_scroll);
        gtk_widget_hide(data_scroll);
    } else if (dump_data) {
        gtk_widget_hide(defs_scroll);
        gtk_widget_show(data_scroll);
    } else {
        gtk_widget_hide(defs_scroll);
        gtk_widget_hide(data_scroll);
        return;
    }

    if (dump_defs) {
        text = dump_table_definitions();
        buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(defs_text));
        gtk_text_buffer_set_text(buffer, text, -1);
        g_free(text);
    }

    if (dump_data) {
        text = dump_sample_rows(row_count);
        buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(data_text));
        gtk_text_buffer_set_text(buffer, text, -1);
        g_free(text);
    }
}

static void
gmdb_dump_destroy_cb(GtkWidget *w, gpointer data)
{
    GtkBuilder *xml = GTK_BUILDER(data);
    g_object_unref(xml);
}

void
gmdb_dump_new_cb(GtkWidget *w, gpointer data)
{
    GtkBuilder *xml;
    GtkWidget *win;
    GtkWidget *defs_text, *data_text;
    PangoFontDescription *font_desc;
    GError *error = NULL;

    xml = gtk_builder_new();
    if (!gtk_builder_add_from_file(xml, GMDB_UIDIR "gmdb-dump.ui", &error)) {
        g_warning("Error adding " GMDB_UIDIR "gmdb-dump.ui: %s", error->message);
        g_error_free(error);
        g_object_unref(xml);
        return;
    }
    gtk_builder_connect_signals(xml, NULL);

    win = GTK_WIDGET(gtk_builder_get_object(xml, "dump_window"));
    g_object_ref(xml);
    g_signal_connect(win, "destroy", G_CALLBACK(gmdb_dump_destroy_cb), xml);

    g_signal_connect(GTK_WIDGET(gtk_builder_get_object(xml, "dump_refresh")), "clicked",
        G_CALLBACK(gmdb_dump_refresh_cb), xml);

    font_desc = pango_font_description_from_string("Monospace");
    defs_text = GTK_WIDGET(gtk_builder_get_object(xml, "dump_defs_text"));
    gtk_widget_override_font(defs_text, font_desc);
    data_text = GTK_WIDGET(gtk_builder_get_object(xml, "dump_data_text"));
    gtk_widget_override_font(data_text, font_desc);
    pango_font_description_free(font_desc);

    gtk_widget_show(win);

    gmdb_dump_refresh_cb(NULL, xml);
}
