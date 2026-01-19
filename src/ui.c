/*
 * ui.c - UI creation functions for LOC-OS 24 Installer
 */

#include "installer.h"

/* ==================== GTK IDLE FUNCTIONS ==================== */

gboolean update_progress_ui(gpointer data) {
    ProgressData *pdata = (ProgressData*)data;

    if (pdata->app && pdata->app->progress_bar) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pdata->app->progress_bar), pdata->percent / 100.0);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(pdata->app->progress_bar), pdata->message);
    }

    free(pdata->message);
    free(pdata);
    return G_SOURCE_REMOVE;
}

gboolean update_status_ui(gpointer data) {
    StatusData *sdata = (StatusData*)data;

    if (sdata->app && sdata->app->status_label) {
        gtk_label_set_text(GTK_LABEL(sdata->app->status_label), sdata->message);
    }

    free(sdata->message);
    free(sdata);
    return G_SOURCE_REMOVE;
}

gboolean show_error_dialog(ErrorData *edata) {
    if (edata->app) {
        edata->app->config.installation_started = false;
        update_navigation_buttons(edata->app);

        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(edata->app->window),
                                                   GTK_DIALOG_MODAL,
                                                   GTK_MESSAGE_ERROR,
                                                   GTK_BUTTONS_OK,
                                                   "%s", edata->message);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }

    free(edata->message);
    free(edata);
    return G_SOURCE_REMOVE;
}

gboolean show_success_dialog(InstallerApp *app) {
    if (app) {
        app->config.installation_complete = true;
        update_navigation_buttons(app);

        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                                   GTK_DIALOG_MODAL,
                                                   GTK_MESSAGE_INFO,
                                                   GTK_BUTTONS_OK,
                                                   _("Installation completed successfully!\n\n"
                                                   "Click 'Finish Installation' to complete."));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }

    return G_SOURCE_REMOVE;
}

gboolean enable_close_button(gpointer data) {
    GtkWidget *close_btn = (GtkWidget*)data;
    gtk_widget_set_sensitive(close_btn, TRUE);
    return G_SOURCE_REMOVE;
}

void copy_log_to_clipboard(InstallerApp *app) {
    if (!app->log_buffer) return;

    GtkTextIter start, end;
    gtk_text_buffer_get_start_iter(app->log_buffer, &start);
    gtk_text_buffer_get_end_iter(app->log_buffer, &end);

    gchar *text = gtk_text_buffer_get_text(app->log_buffer, &start, &end, FALSE);

    GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clipboard, text, -1);

    g_free(text);

    // Mostrar mensaje de confirmación
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                               GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_INFO,
                                               GTK_BUTTONS_OK,
                                               _("Installation log copied to clipboard"));
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

// Función para configurar el teclado actual en la UI
void setup_current_keyboard_in_ui(InstallerApp *app) {
    if (!app || !app->keyboard_combo) return;

    // Obtener teclado actual del sistema
    char *current_kb = get_current_keyboard();
    if (!current_kb) {
        current_kb = strdup("us");
    }

    printf("DEBUG: Current system keyboard: '%s'\n", current_kb);

    // Buscar y seleccionar el teclado actual en el combo
    GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(app->keyboard_combo));
    GtkTreeIter iter;
    int index = 0;
    int found_index = 0;

    if (gtk_tree_model_get_iter_first(model, &iter)) {
        do {
            gchar *item_text;
            gtk_tree_model_get(model, &iter, 0, &item_text, -1);

            if (item_text) {
                // Extraer código del formato "code - description"
                char *dash = strstr(item_text, " - ");
                if (dash) {
                    // Cortar temporalmente para comparar
                    *dash = '\0';

                    if (strcmp(current_kb, item_text) == 0) {
                        found_index = index;
                        // Restaurar
                        *dash = ' ';
                        g_free(item_text);
                        break;
                    }
                    // Restaurar
                    *dash = ' ';
                }
                g_free(item_text);
            }
            index++;
        } while (gtk_tree_model_iter_next(model, &iter));
    }

    // Establecer la selección
    gtk_combo_box_set_active(GTK_COMBO_BOX(app->keyboard_combo), found_index);

    // Ahora obtener el layout seleccionado para actualizar variantes
    const char *selected_text = gtk_combo_box_text_get_active_text(
        GTK_COMBO_BOX_TEXT(app->keyboard_combo));

    if (selected_text) {
        char *layout_code = extract_code(selected_text);
        if (layout_code) {
            printf("DEBUG: Selected layout for variant update: %s\n", layout_code);

            // Guardar en la configuración
            strncpy(app->config.keyboard, layout_code, sizeof(app->config.keyboard));

            // Actualizar variantes disponibles
            update_keyboard_variants(app, layout_code);

            free(layout_code);
        }
    } else {
        // Si no hay nada seleccionado, usar "us" por defecto
        strncpy(app->config.keyboard, "us", sizeof(app->config.keyboard));
    }

    printf("DEBUG: Set keyboard combo to index %d (code: %s)\n",
           found_index, app->config.keyboard);

    free(current_kb);
}

/* ==================== UI CREATION ==================== */

GtkWidget* create_label_with_markup(const char *text) {
    GtkWidget *label = gtk_label_new(NULL);
    char *markup = g_markup_printf_escaped("<span size='large' weight='bold'>%s</span>", text);
    gtk_label_set_markup(GTK_LABEL(label), markup);
    g_free(markup);
    return label;
}

GtkWidget* create_regional_tab(InstallerApp *app) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 20);

    gtk_box_pack_start(GTK_BOX(vbox),
                       create_label_with_markup(_("Regional Settings")),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox),
                       gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),
                       FALSE, FALSE, 10);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 20);

    /* Language */
    GtkWidget *label = gtk_label_new(_("Language:"));
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);

    app->language_combo = gtk_combo_box_text_new();
    int lang_count;
    char **languages = get_languages(&lang_count);
    if (languages) {
        for (int i = 0; i < lang_count; i++) {
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->language_combo), languages[i]);
        }
        free_string_array(languages, lang_count);
    } else {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->language_combo), "en_US - English (United States)");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->language_combo), "es_ES - Spanish (Spain)");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->language_combo), "fr_FR - French (France)");
    }

    char *current_lang_code = get_current_language();
    int lang_pos = 0;
    int found_pos = 0;

    GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(app->language_combo));
    GtkTreeIter iter;

    if (gtk_tree_model_get_iter_first(model, &iter)) {
        do {
            gchar *item_text;
            gtk_tree_model_get(model, &iter, 0, &item_text, -1);

            char *code = extract_code(item_text);
            if (code && strcmp(code, current_lang_code) == 0) {
                found_pos = lang_pos;
                free(code);
                g_free(item_text);
                break;
            }

            free(code);
            g_free(item_text);
            lang_pos++;
        } while (gtk_tree_model_iter_next(model, &iter));
    }

    gtk_combo_box_set_active(GTK_COMBO_BOX(app->language_combo), found_pos);
    free(current_lang_code);
    gtk_grid_attach(GTK_GRID(grid), app->language_combo, 1, 0, 2, 1);

    /* Timezone */
    label = gtk_label_new(_("Timezone:"));
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_grid_attach(GTK_GRID(grid), label, 0, 1, 1, 1);

    GtkWidget *timezone_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

    app->region_combo = gtk_combo_box_text_new();
    gtk_widget_set_hexpand(app->region_combo, TRUE);

    app->city_combo = gtk_combo_box_text_new();
    gtk_widget_set_hexpand(app->city_combo, TRUE);

    gtk_box_pack_start(GTK_BOX(timezone_box), app->region_combo, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(timezone_box), gtk_label_new("/"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(timezone_box), app->city_combo, TRUE, TRUE, 0);

    gtk_grid_attach(GTK_GRID(grid), timezone_box, 1, 1, 2, 1);

    load_timezones_hierarchical(app);

    for (int i = 0; i < app->region_count; i++) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->region_combo),
                                       app->region_names[i]);
    }

    g_signal_connect(app->region_combo, "changed",
                     G_CALLBACK(on_region_changed), app);

    set_current_timezone(app);

    char *current_tz = get_current_timezone();
    char *default_region = "UTC";
    char *default_city = "";

    char *slash = strchr(current_tz, '/');
    if (slash) {
        *slash = '\0';
        default_region = current_tz;
        default_city = slash + 1;
    } else {
        default_region = current_tz;
    }

    int region_idx = find_combo_item(GTK_COMBO_BOX_TEXT(app->region_combo), default_region);
    if (region_idx >= 0) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(app->region_combo), region_idx);
        on_region_changed(GTK_COMBO_BOX(app->region_combo), app);

        if (default_city[0]) {
            int city_idx = find_combo_item(GTK_COMBO_BOX_TEXT(app->city_combo), default_city);
            if (city_idx >= 0) {
                gtk_combo_box_set_active(GTK_COMBO_BOX(app->city_combo), city_idx);
            }
        }
    }

    free(current_tz);

    /* Keyboard Layout */
    label = gtk_label_new(_("Keyboard Layout:"));
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_grid_attach(GTK_GRID(grid), label, 0, 2, 1, 1);

    app->keyboard_combo = gtk_combo_box_text_new();

    // Cargar layouts
    int kb_count;
    char **keyboards = get_keyboard_layouts(&kb_count);
    if (keyboards) {
        for (int i = 0; i < kb_count; i++) {
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->keyboard_combo),
                                           keyboards[i]);
        }
        free_string_array(keyboards, kb_count);
    } else {
        // Fallback
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->keyboard_combo),
                                       "us - English (US)");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->keyboard_combo),
                                       "es - Spanish");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->keyboard_combo),
                                       "latam - Spanish (Latin America)");
    }

    gtk_grid_attach(GTK_GRID(grid), app->keyboard_combo, 1, 2, 2, 1);

    /* Keyboard Variant */
    label = gtk_label_new(_("Keyboard Variant:"));
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_grid_attach(GTK_GRID(grid), label, 0, 3, 1, 1);

    app->keyboard_variant_combo = gtk_combo_box_text_new();
    // Inicialmente solo con "default"
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->keyboard_variant_combo),
                                   "default - Default variant");
    gtk_combo_box_set_active(GTK_COMBO_BOX(app->keyboard_variant_combo), 0);

    // Inicializar en la configuración
    strncpy(app->config.keyboard_variant, "default", sizeof(app->config.keyboard_variant));

    gtk_grid_attach(GTK_GRID(grid), app->keyboard_variant_combo, 1, 3, 2, 1);

    // Conectar señales
    g_signal_connect(app->keyboard_combo, "changed",
                     G_CALLBACK(on_keyboard_layout_changed), app);
    g_signal_connect(app->keyboard_variant_combo, "changed",
                     G_CALLBACK(on_keyboard_variant_changed), app);

    // Configurar teclado actual DESPUÉS de conectar las señales
    setup_current_keyboard_in_ui(app);

    gtk_box_pack_start(GTK_BOX(vbox), grid, FALSE, FALSE, 10);
    gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new(""), TRUE, TRUE, 0);

    return vbox;
}

GtkWidget* create_partition_tab(InstallerApp *app) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 20);

    gtk_box_pack_start(GTK_BOX(vbox),
                       create_label_with_markup(_("Disk Partitioning")),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox),
                       gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),
                       FALSE, FALSE, 10);

    /* Installation Mode */
    GtkWidget *frame = gtk_frame_new(_("Installation Mode"));
    GtkWidget *frame_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(frame_box), 10);

    app->auto_radio = gtk_radio_button_new_with_label(NULL, _("Automatic (Recommended)"));
    app->manual_radio = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(app->auto_radio), _("Manual"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->auto_radio), TRUE);

    g_signal_connect(app->auto_radio, "toggled",
                     G_CALLBACK(on_partition_mode_toggled), app);
    g_signal_connect(app->manual_radio, "toggled",
                     G_CALLBACK(on_partition_mode_toggled), app);

    gtk_box_pack_start(GTK_BOX(frame_box), app->auto_radio, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(frame_box), app->manual_radio, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(frame), frame_box);
    gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 10);

    /* Notebook interno para las dos vistas */
    app->partition_notebook = gtk_notebook_new();
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(app->partition_notebook), FALSE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(app->partition_notebook), FALSE);

    /* ====== PESTAÑA AUTOMÁTICA ====== */
    GtkWidget *auto_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(auto_vbox), 5);

    GtkWidget *auto_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(auto_grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(auto_grid), 20);

    int row = 0;

    /* Disk */
    GtkWidget *label = gtk_label_new(_("Disk:"));
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_grid_attach(GTK_GRID(auto_grid), label, 0, row, 1, 1);

    app->disk_combo = gtk_combo_box_text_new();
    int disk_count;
    char **disks = get_disks(&disk_count);
    if (disks) {
        printf("DEBUG: Found %d disks\n", disk_count);

        for (int i = 0; i < disk_count; i++) {
            printf("DEBUG: Raw disk string [%d]: '%s'\n", i, disks[i]);

            // Hacer una copia para no modificar el original
            char disk_copy[512];
            strncpy(disk_copy, disks[i], sizeof(disk_copy) - 1);
            disk_copy[sizeof(disk_copy) - 1] = '\0';

            char *pipe1 = strchr(disk_copy, '|');
            if (pipe1) {
                *pipe1 = '\0';  // Ahora disk_copy es solo "/dev/sda"
                char *device_name = disk_copy;  // "/dev/sda"

                printf("  DEBUG: After first pipe cut, device_name = '%s'\n", device_name);

                // Construir display_text con toda la información
                gchar *display_text;

                // DECLARAR LAS VARIABLES AQUÍ
                char *size_start = pipe1 + 1;  // DECLARAR size_start
                char size_info[64] = "";       // DECLARAR size_info
                char *pipe2 = strchr(size_start, '|');  // DECLARAR pipe2

                if (pipe2) {
                    *pipe2 = '\0';
                    strncpy(size_info, size_start, sizeof(size_info) - 1);
                    size_info[sizeof(size_info) - 1] = '\0';

                    char *model_start = pipe2 + 1;
                    if (strlen(model_start) > 0) {
                        display_text = g_strdup_printf("%s - %s - %s",
                                                       device_name, size_info, model_start);
                    } else {
                        display_text = g_strdup_printf("%s - %s", device_name, size_info);
                    }
                    *pipe2 = '|';
                } else {
                    if (strlen(size_start) > 0) {
                        display_text = g_strdup_printf("%s - %s", device_name, size_start);
                    } else {
                        display_text = g_strdup_printf("%s", device_name);
                    }
                }

                printf("  DEBUG: Appending - ID='%s', Text='%s'\n",
                       device_name, display_text);

                gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->disk_combo),
                                          device_name,      // ID: "/dev/sda"
                                          display_text);    // Texto: "/dev/sda - 10G - QEMU HARDDISK disk ata"

                                          g_free(display_text);
            } else {
                printf("  DEBUG: No pipe found, using whole string: '%s'\n", disks[i]);
                gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->disk_combo),
                                          disks[i], disks[i]);
            }
        }

        free_string_array(disks, disk_count);
    } else {
        printf("DEBUG: No disks detected!\n");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->disk_combo), "");
        gtk_label_new(_("Sorry, no disks detected"));
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(app->disk_combo), 0);
    g_signal_connect(app->disk_combo, "changed", G_CALLBACK(on_disk_changed), app);
    gtk_grid_attach(GTK_GRID(auto_grid), app->disk_combo, 1, row, 2, 1);
    row++;

    /* UEFI detection info */
    GtkWidget *uefi_label;
    if (app->config.uefi_mode) {
        uefi_label = gtk_label_new(_("System detected: UEFI Mode"));
    } else {
        uefi_label = gtk_label_new(_("System detected: BIOS/Legacy Mode"));
    }
    gtk_label_set_xalign(GTK_LABEL(uefi_label), 0);
    gtk_grid_attach(GTK_GRID(auto_grid), uefi_label, 0, row, 3, 1);
    row++;


    /* Swap options frame */
    GtkWidget *swap_frame = gtk_frame_new(_("Swap Options"));
    GtkWidget *swap_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(swap_vbox), 10);

    app->add_swap_check = gtk_check_button_new_with_label(_("Enable swap space"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->add_swap_check), FALSE);
    g_signal_connect(app->add_swap_check, "toggled",
                     G_CALLBACK(on_add_swap_toggled), app);

    /* Contenedor para opciones de swap */
    app->swap_options_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_start(app->swap_options_container, 20);

    app->swap_partition_radio = gtk_radio_button_new_with_label(NULL, _("Swap partition"));
    app->swap_file_radio = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(app->swap_partition_radio), _("Swap file"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->swap_partition_radio), TRUE);

    GtkWidget *swap_size_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *swap_size_label = gtk_label_new(_("Size:"));
    gtk_label_set_xalign(GTK_LABEL(swap_size_label), 0);

    app->swap_spin = gtk_spin_button_new_with_range(512, 32768, 512);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->swap_spin), 2048);

    GtkWidget *mb_label = gtk_label_new(_("MB"));

    gtk_box_pack_start(GTK_BOX(swap_size_hbox), swap_size_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(swap_size_hbox), app->swap_spin, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(swap_size_hbox), mb_label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(app->swap_options_container), app->swap_partition_radio, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(app->swap_options_container), app->swap_file_radio, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(app->swap_options_container), swap_size_hbox, FALSE, FALSE, 0);

    /* Empaquetar todo en swap_vbox */
    gtk_box_pack_start(GTK_BOX(swap_vbox), app->add_swap_check, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(swap_vbox), app->swap_options_container, FALSE, FALSE, 0);

    /* Añadir swap_vbox al frame y el frame al grid */
    gtk_container_add(GTK_CONTAINER(swap_frame), swap_vbox);
    gtk_grid_attach(GTK_GRID(auto_grid), swap_frame, 0, row, 3, 1);
    row++;

    gtk_box_pack_start(GTK_BOX(auto_vbox), auto_grid, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(auto_vbox), gtk_label_new(""), TRUE, TRUE, 0); // Espaciador

    /* ====== PESTAÑA MANUAL ====== */
    GtkWidget *manual_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(manual_vbox), 5);

    GtkWidget *manual_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(manual_grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(manual_grid), 20);

    row = 0;

    /* UEFI info en manual también */
    if (app->config.uefi_mode) {
        uefi_label = gtk_label_new(_("System detected: UEFI Mode, remember to do a EFI partition at the start of around 200 MB"));
    } else {
        uefi_label = gtk_label_new(_("System detected: BIOS/Legacy Mode"));
    }
    gtk_label_set_xalign(GTK_LABEL(uefi_label), 0);
    gtk_grid_attach(GTK_GRID(manual_grid), uefi_label, 0, row, 3, 1);
    row++;

    /* GParted button */
    GtkWidget *gparted_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    app->open_gparted_btn = gtk_button_new_with_label(_("Open GParted"));
    g_signal_connect(app->open_gparted_btn, "clicked", G_CALLBACK(on_open_gparted), app);

    gtk_box_pack_start(GTK_BOX(gparted_hbox), app->open_gparted_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(gparted_hbox),
                       gtk_label_new(_("For manual partitioning")),
                       FALSE, FALSE, 0);

    gtk_grid_attach(GTK_GRID(manual_grid), gparted_hbox, 0, row, 3, 1);
    row++;

    /* EFI si UEFI - PRIMERO si es UEFI */
    if (app->config.uefi_mode) {
        label = gtk_label_new(_("EFI partition:"));
        gtk_label_set_xalign(GTK_LABEL(label), 0);
        gtk_grid_attach(GTK_GRID(manual_grid), label, 0, row, 1, 1);
        app->efi_combo = gtk_combo_box_text_new();
        // SOLO crear el combo aquí, poblarlo después
        gtk_grid_attach(GTK_GRID(manual_grid), app->efi_combo, 1, row, 2, 1);
        row++;
    }

    /* Root - SIEMPRE presente */
    label = gtk_label_new(_("Root partition:"));
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_grid_attach(GTK_GRID(manual_grid), label, 0, row, 1, 1);
    app->root_combo = gtk_combo_box_text_new();
    // SOLO crear el combo aquí, poblarlo después
    gtk_grid_attach(GTK_GRID(manual_grid), app->root_combo, 1, row, 2, 1);
    row++;

    /* Checkboxes para particiones adicionales */
    GtkWidget *manual_checkboxes_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);

    app->separate_home_check_manual = gtk_check_button_new_with_label(_("Separate /home"));
    g_signal_connect(app->separate_home_check_manual, "toggled",
                     G_CALLBACK(on_separate_home_manual_toggled), app);

    app->separate_boot_check = gtk_check_button_new_with_label(_("Separate /boot"));
    g_signal_connect(app->separate_boot_check, "toggled",
                     G_CALLBACK(on_separate_boot_toggled), app);

    // AGREGAR CHECKBOX PARA SWAP
    app->add_swap_check_manual = gtk_check_button_new_with_label(_("Add swap partition"));
    g_signal_connect(app->add_swap_check_manual, "toggled",
                     G_CALLBACK(on_add_swap_manual_toggled), app);

    gtk_box_pack_start(GTK_BOX(manual_checkboxes_hbox), app->separate_home_check_manual, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(manual_checkboxes_hbox), app->separate_boot_check, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(manual_checkboxes_hbox), app->add_swap_check_manual, FALSE, FALSE, 0);
    gtk_grid_attach(GTK_GRID(manual_grid), manual_checkboxes_hbox, 0, row, 3, 1);
    row++;

    /* Home partition - VISIBLE SOLO SI ESTÁ MARCADO */
    app->home_combo_container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    label = gtk_label_new(_("Home partition:"));
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_box_pack_start(GTK_BOX(app->home_combo_container), label, FALSE, FALSE, 0);
    app->home_combo = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(app->home_combo_container), app->home_combo, TRUE, TRUE, 0);
    gtk_grid_attach(GTK_GRID(manual_grid), app->home_combo_container, 0, row, 3, 1);
    row++;

    /* Boot partition - VISIBLE SOLO SI ESTÁ MARCADO */
    app->boot_combo_container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    label = gtk_label_new(_("Boot partition:"));
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_box_pack_start(GTK_BOX(app->boot_combo_container), label, FALSE, FALSE, 0);
    app->boot_combo = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(app->boot_combo_container), app->boot_combo, TRUE, TRUE, 0);
    gtk_grid_attach(GTK_GRID(manual_grid), app->boot_combo_container, 0, row, 3, 1);
    row++;

    /* Swap partition - VISIBLE SOLO SI ESTÁ MARCADO */
    app->swap_combo_container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    label = gtk_label_new(_("Swap partition:"));
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_box_pack_start(GTK_BOX(app->swap_combo_container), label, FALSE, FALSE, 0);
    app->swap_combo = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(app->swap_combo_container), app->swap_combo, TRUE, TRUE, 0);
    gtk_grid_attach(GTK_GRID(manual_grid), app->swap_combo_container, 0, row, 3, 1);
    row++;

    // FALTA ESTA LÍNEA CRÍTICA: Agregar manual_grid a manual_vbox
    gtk_box_pack_start(GTK_BOX(manual_vbox), manual_grid, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(manual_vbox), gtk_label_new(""), TRUE, TRUE, 0); // Espaciador

    // ====== POBLAR COMBOS DESPUÉS DE CREARLOS TODOS ======
    // Primero poblar root (tiene prioridad)
    populate_partition_combo(GTK_COMBO_BOX_TEXT(app->root_combo), app);

    // Luego EFI si existe
    if (app->config.uefi_mode && app->efi_combo) {
        populate_partition_combo(GTK_COMBO_BOX_TEXT(app->efi_combo), app);
    }

    // Los demás combos (home, boot, swap) se poblarán cuando se activen
    // porque inicialmente están ocultos

    // ====== CONECTAR SEÑALES ======
    g_signal_connect(app->root_combo, "changed",
                     G_CALLBACK(on_partition_combo_changed), app);

    if (app->efi_combo) {
        g_signal_connect(app->efi_combo, "changed",
                         G_CALLBACK(on_partition_combo_changed), app);
    }

    g_signal_connect(app->home_combo, "changed",
                     G_CALLBACK(on_partition_combo_changed), app);

    g_signal_connect(app->boot_combo, "changed",
                     G_CALLBACK(on_partition_combo_changed), app);

    g_signal_connect(app->swap_combo, "changed",
                     G_CALLBACK(on_partition_combo_changed), app);

    /* Añadir pestañas al notebook interno */
    gtk_notebook_append_page(GTK_NOTEBOOK(app->partition_notebook), auto_vbox, NULL);
    gtk_notebook_append_page(GTK_NOTEBOOK(app->partition_notebook), manual_vbox, NULL);

    /* Inicialmente mostrar automático */
    gtk_notebook_set_current_page(GTK_NOTEBOOK(app->partition_notebook), 0);

    /* Agregar notebook a la vista principal */
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scrolled), 400);
    gtk_container_add(GTK_CONTAINER(scrolled), app->partition_notebook);

    gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 10);

    return vbox;
}

GtkWidget* create_user_tab(InstallerApp *app) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 20);

    gtk_box_pack_start(GTK_BOX(vbox),
                       create_label_with_markup(_("User Configuration")),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox),
                       gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),
                       FALSE, FALSE, 10);

    /* Fields grid - SOLO para campos de usuario */
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 20);

    int row = 0;

    /* Username */
    GtkWidget *label = gtk_label_new(_("Username:"));
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
    app->username_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->username_entry), _("Enter username"));
    gtk_grid_attach(GTK_GRID(grid), app->username_entry, 1, row, 2, 1);
    row++;

    /* Real name */
    label = gtk_label_new(_("Real name:"));
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
    app->realname_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->realname_entry), _("Your full name"));
    gtk_grid_attach(GTK_GRID(grid), app->realname_entry, 1, row, 2, 1);
    row++;

    /* Hostname */
    label = gtk_label_new(_("Hostname:"));
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
    app->hostname_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->hostname_entry), "loc-os-pc");
    gtk_entry_set_text(GTK_ENTRY(app->hostname_entry), "loc-os-pc");
    gtk_grid_attach(GTK_GRID(grid), app->hostname_entry, 1, row, 2, 1);
    row++;

    /* Password */
    label = gtk_label_new(_("Password:"));
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
    app->password_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(app->password_entry), FALSE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->password_entry), _("Enter password"));
    gtk_grid_attach(GTK_GRID(grid), app->password_entry, 1, row, 2, 1);
    row++;

    /* Confirm password */
    label = gtk_label_new(_("Confirm password:"));
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
    app->password_confirm_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(app->password_confirm_entry), FALSE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->password_confirm_entry),
                                   _("Confirm password"));
    gtk_grid_attach(GTK_GRID(grid), app->password_confirm_entry, 1, row, 2, 1);
    row++;

    /* Agregar el grid principal de campos de usuario */
    gtk_box_pack_start(GTK_BOX(vbox), grid, FALSE, FALSE, 10);

    /* ====== CHECKBOXES ====== */
    GtkWidget *checkboxes_frame = gtk_frame_new(NULL);
    GtkWidget *checkboxes_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(checkboxes_box), 10);

    app->autologin_check = gtk_check_button_new_with_label(_("Enable automatic login"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->autologin_check), FALSE);
    g_signal_connect(app->autologin_check, "toggled",
                     G_CALLBACK(on_autologin_toggled), app);

    app->root_password_check = gtk_check_button_new_with_label(_("Use same password for root"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->root_password_check), TRUE);
    g_signal_connect(app->root_password_check, "toggled",
                     G_CALLBACK(on_root_password_check_toggled), app);

    gtk_box_pack_start(GTK_BOX(checkboxes_box), app->autologin_check, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(checkboxes_box), app->root_password_check, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(checkboxes_frame), checkboxes_box);
    gtk_box_pack_start(GTK_BOX(vbox), checkboxes_frame, FALSE, FALSE, 10);

    /* ====== CAMPOS DE CONTRASEÑA ROOT (inicialmente ocultos) ====== */
    app->root_password_container = gtk_frame_new(_("Root Password Settings"));
    GtkWidget *root_pass_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(root_pass_vbox), 10);

    GtkWidget *root_pass_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(root_pass_grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(root_pass_grid), 20);

    /* Root password */
    label = gtk_label_new(_("Root password:"));
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_grid_attach(GTK_GRID(root_pass_grid), label, 0, 0, 1, 1);
    app->root_password_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(app->root_password_entry), FALSE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->root_password_entry),
                                   _("Enter root password"));
    gtk_grid_attach(GTK_GRID(root_pass_grid), app->root_password_entry, 1, 0, 2, 1);

    /* Confirm root password */
    label = gtk_label_new(_("Confirm root password:"));
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_grid_attach(GTK_GRID(root_pass_grid), label, 0, 1, 1, 1);
    app->root_password_confirm_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(app->root_password_confirm_entry), FALSE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->root_password_confirm_entry),
                                   _("Confirm root password"));
    gtk_grid_attach(GTK_GRID(root_pass_grid), app->root_password_confirm_entry, 1, 1, 2, 1);

    gtk_box_pack_start(GTK_BOX(root_pass_vbox), root_pass_grid, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(app->root_password_container), root_pass_vbox);
    gtk_box_pack_start(GTK_BOX(vbox), app->root_password_container, FALSE, FALSE, 10);

    /* Espaciador final */
    gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new(""), TRUE, TRUE, 0);

    return vbox;
}

GtkWidget* create_progress_tab(InstallerApp *app) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 20);

    gtk_box_pack_start(GTK_BOX(vbox),
                       create_label_with_markup(_("Installation Progress")),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox),
                       gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),
                       FALSE, FALSE, 10);

    /* Progress bar */
    GtkWidget *progress_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);

    app->progress_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(app->progress_bar), TRUE);
    gtk_widget_set_size_request(app->progress_bar, -1, 30);
    gtk_box_pack_start(GTK_BOX(progress_box), app->progress_bar, FALSE, FALSE, 5);

    gtk_box_pack_start(GTK_BOX(vbox), progress_box, FALSE, FALSE, 15);

    /* Log area */
    GtkWidget *log_frame = gtk_frame_new(_("Installation Log"));
    GtkWidget *log_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(log_box), 10);

    app->log_scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(app->log_scrolled_window),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(app->log_scrolled_window), 300);

    app->log_text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(app->log_text_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(app->log_text_view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->log_text_view), GTK_WRAP_WORD_CHAR);

    app->log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->log_text_view));

    gtk_container_add(GTK_CONTAINER(app->log_scrolled_window), app->log_text_view);
    gtk_box_pack_start(GTK_BOX(log_box), app->log_scrolled_window, TRUE, TRUE, 0);

    /* Copy log button */
    app->copy_log_btn = gtk_button_new_with_label(_("Copy Log to Clipboard"));
    g_signal_connect_swapped(app->copy_log_btn, "clicked",
                             G_CALLBACK(copy_log_to_clipboard), app);
    gtk_box_pack_start(GTK_BOX(log_box), app->copy_log_btn, FALSE, FALSE, 5);

    gtk_container_add(GTK_CONTAINER(log_frame), log_box);
    gtk_box_pack_start(GTK_BOX(vbox), log_frame, TRUE, TRUE, 10);

    /* Finish button */
    GtkWidget *finish_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    app->finish_btn = gtk_button_new_with_label(_("Finish Installation"));
    gtk_widget_set_sensitive(app->finish_btn, FALSE);
    gtk_widget_set_halign(app->finish_btn, GTK_ALIGN_END);
    gtk_widget_set_valign(app->finish_btn, GTK_ALIGN_END);
    g_signal_connect(app->finish_btn, "clicked", G_CALLBACK(on_finish_clicked), app);

    gtk_box_pack_end(GTK_BOX(finish_box), app->finish_btn, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(vbox), finish_box, FALSE, FALSE, 10);

    return vbox;
}

void append_to_log(InstallerApp *app, const char *text) {
    if (!app || !app->log_buffer) return;

    GtkTextIter iter;
    gtk_text_buffer_get_end_iter(app->log_buffer, &iter);
    gtk_text_buffer_insert(app->log_buffer, &iter, text, -1);
    gtk_text_buffer_insert(app->log_buffer, &iter, "\n", -1);

    // Auto-scroll al final
    if (app->log_text_view) {
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(app->log_text_view),
                                     &iter, 0.0, FALSE, 0.0, 1.0);
    }
}

void create_main_window(InstallerApp *app) {
    app->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app->window), _("LOC-OS 24 Installer"));
    gtk_window_set_default_size(GTK_WINDOW(app->window), 800, 650);
    gtk_window_set_position(GTK_WINDOW(app->window), GTK_WIN_POS_CENTER);
    g_signal_connect(app->window, "destroy", G_CALLBACK(on_window_destroy), app);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 5);

    /* Scrolled window para notebook */
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scrolled), 400);

    /* Notebook - SIN PESTAÑAS VISIBLES */
    app->notebook = gtk_notebook_new();
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(app->notebook), GTK_POS_TOP);
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(app->notebook), TRUE);

    /* DESHABILITAR COMPLETAMENTE LAS PESTAÑAS VISIBLES */
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(app->notebook), FALSE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(app->notebook), FALSE);

    /* Añadir pestañas al notebook usando NULL como etiqueta ya que no se muestran */
    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook),
                             create_regional_tab(app),
                             NULL);
    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook),
                             create_partition_tab(app),
                             NULL);
    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook),
                             create_user_tab(app),
                             NULL);
    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook),
                             create_progress_tab(app),
                             NULL);

    /* IMPORTANTE: Conectar señal de cambio de página */
    g_signal_connect(app->notebook, "switch-page",
                     G_CALLBACK(on_page_switched), app);

    gtk_container_add(GTK_CONTAINER(scrolled), app->notebook);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

    /* Navigation buttons */
    GtkWidget *btn_box = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(btn_box), GTK_BUTTONBOX_END);
    gtk_box_set_spacing(GTK_BOX(btn_box), 10);

    app->prev_btn = gtk_button_new_with_label(_("Previous"));
    app->next_btn = gtk_button_new_with_label(_("Next"));
    app->install_btn = gtk_button_new_with_label(_("Install"));

    g_signal_connect(app->prev_btn, "clicked", G_CALLBACK(on_previous_clicked), app);
    g_signal_connect(app->next_btn, "clicked", G_CALLBACK(on_next_clicked), app);
    g_signal_connect(app->install_btn, "clicked", G_CALLBACK(on_install_clicked), app);

    gtk_box_pack_start(GTK_BOX(btn_box), app->prev_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btn_box), app->next_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btn_box), app->install_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), btn_box, FALSE, FALSE, 5);

    /* Agregar vbox a la ventana */
    gtk_container_add(GTK_CONTAINER(app->window), vbox);

    /* Inicializar botones según la pestaña actual */
    update_navigation_buttons(app);
}
