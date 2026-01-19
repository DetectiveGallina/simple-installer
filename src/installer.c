/*
 * installer.c - Main installer functions for LOC-OS 24 Installer
 */

#include "installer.h"

/* ==================== SYSTEM INFO FUNCTIONS ==================== */

char** get_system_list(const char *command, int *count) {
    FILE *fp;
    char line[512];
    char **list = NULL;
    int capacity = 1000;
    int i = 0;

    list = malloc(capacity * sizeof(char*));
    if (!list) return NULL;

    fp = popen(command, "r");
    if (!fp) {
        free(list);
        return NULL;
    }

    while (fgets(line, sizeof(line), fp) && i < capacity) {
        line[strcspn(line, "\n")] = 0;
        if (strlen(line) > 0) {
            list[i] = strdup(line);
            i++;
        }
    }

    pclose(fp);
    *count = i;
    return list;
}

char** get_timezones(int *count) {
    return get_system_list(SYSINFO_SCRIPT " timezones", count);
}

char** get_keyboard_layouts(int *count) {
    return get_system_list(SYSINFO_SCRIPT " keyboards", count);
}

char** get_languages(int *count) {
    return get_system_list(SYSINFO_SCRIPT " languages", count);
}

char** get_disks(int *count) {
    FILE *fp;
    char line[256];
    char **disks = NULL;
    int capacity = 20;
    int i = 0;

    disks = malloc(capacity * sizeof(char*));
    if (!disks) return NULL;

    fp = popen(SYSINFO_SCRIPT " disks", "r");
    if (!fp) {
        free(disks);
        return NULL;
    }

    while (fgets(line, sizeof(line), fp) && i < capacity) {
        line[strcspn(line, "\n")] = 0;
        if (strlen(line) > 0) {
            disks[i] = strdup(line);
            i++;
        }
    }

    pclose(fp);
    *count = i;
    return disks;
}

char* get_current_timezone(void) {
    FILE *fp = popen(SYSINFO_SCRIPT " env | "
    "grep 'CURRENT_TIMEZONE' | cut -d'=' -f2 | tr -d '\"'", "r");
    if (!fp) return strdup("UTC");

    char buffer[64];
    if (fgets(buffer, sizeof(buffer), fp)) {
        buffer[strcspn(buffer, "\n")] = 0;
        pclose(fp);
        return strdup(buffer);
    }

    pclose(fp);
    return strdup("UTC");
}

char* get_current_keyboard(void) {
    FILE *fp = popen(SYSINFO_SCRIPT " env | "
    "grep 'CURRENT_KEYBOARD' | cut -d'=' -f2 | tr -d '\"'", "r");
    if (!fp) return strdup("us");

    char buffer[32];
    if (fgets(buffer, sizeof(buffer), fp)) {
        buffer[strcspn(buffer, "\n")] = 0;
        pclose(fp);
        return strdup(buffer);
    }

    pclose(fp);
    return strdup("us");
}

char* get_current_language(void) {
    FILE *fp = popen(SYSINFO_SCRIPT " env | "
    "grep 'CURRENT_LANGUAGE' | cut -d'=' -f2 | tr -d '\"'", "r");
    if (!fp) return strdup("en_US");

    char buffer[32];
    if (fgets(buffer, sizeof(buffer), fp)) {
        buffer[strcspn(buffer, "\n")] = 0;
        pclose(fp);
        return strdup(buffer);
    }

    pclose(fp);
    return strdup("en_US");
}

/* ==================== INSTALLATION FUNCTIONS ==================== */

void parse_installation_output(const char *line, InstallerApp *app) {
    if (!line || !app) return;
    printf("PARSING OUTPUT: %s\n", line);  // DEBUG

    // Detectar líneas de progreso de rsync (con o sin RSYNC_PROGRESS:)
    if (strstr(line, "RSYNC_PROGRESS:") != NULL) {
        // Formato: "RSYNC_PROGRESS: 12% 10.5MB/s 0:01:23"
        const char *progress_info = line + strlen("RSYNC_PROGRESS:");

        // Buscar porcentaje
        char percent_str[10] = "0%";
        char speed_str[20] = "0.0MB/s";
        char time_str[10] = "0:00:00";

        if (sscanf(progress_info, " %9[^ ] %19[^ ] %9[^ \n]",
            percent_str, speed_str, time_str) >= 1) {

            // Actualizar la última línea del log
            char clean_progress[256];
        snprintf(clean_progress, sizeof(clean_progress), "Copying files: %s %s %s",
                 percent_str, speed_str, time_str);

        LogData *log_data = create_log_data(app, clean_progress);
        if (log_data) {
            g_idle_add(update_last_log_line_idle, log_data);
        }

        // También actualizar el status
        StatusData *sdata = malloc(sizeof(StatusData));
        if (sdata) {
            sdata->app = app;
            sdata->message = g_strdup_printf("Copying system files... %s %s",
                                             percent_str, speed_str);
            g_idle_add(update_status_ui, sdata);
        }
        return;
            }
    }
    // Detectar líneas de progreso de rsync directas (sin RSYNC_PROGRESS:)
    else if (strstr(line, "MB/s") != NULL || strstr(line, "KB/s") != NULL ||
        strstr(line, "GB/s") != NULL) {
        // Intentar extraer información
        char percent_str[10] = "0%";
    char speed_str[20] = "0.0MB/s";

    // Buscar porcentaje (patrón: número seguido de %)
    const char *p = line;
    while (*p && !isdigit(*p)) p++;
    if (isdigit(*p)) {
        char *end;
        long percent_num = strtol(p, &end, 10);
        if (end > p && *end == '%') {
            snprintf(percent_str, sizeof(percent_str), "%ld%%", percent_num);
        }
    }

    // Buscar velocidad (MB/s, KB/s, GB/s)
    const char *speed_pos = strstr(line, "B/s");
    if (speed_pos) {
        // Retroceder para encontrar el número
        const char *speed_start = speed_pos;
        while (speed_start > line && (isdigit(*(speed_start-1)) ||
            *(speed_start-1) == '.' || *(speed_start-1) == 'M' ||
            *(speed_start-1) == 'K' || *(speed_start-1) == 'G')) {
            speed_start--;
            }
            size_t speed_len = speed_pos - speed_start + 3; // +3 para "B/s"
            if (speed_len < sizeof(speed_str)) {
                strncpy(speed_str, speed_start, speed_len);
                speed_str[speed_len] = '\0';
            }
    }

    // Actualizar última línea
    char clean_progress[256];
    snprintf(clean_progress, sizeof(clean_progress), "Copying: %s %s",
             percent_str, speed_str);

    LogData *log_data = create_log_data(app, clean_progress);
    if (log_data) {
        g_idle_add(update_last_log_line_idle, log_data);
    }
    return;
        }

        // Para TODAS las otras líneas, añadir al log normalmente
        if (strlen(line) > 0) {
            LogData *log_data = create_log_data(app, line);
            if (log_data) {
                g_idle_add(append_to_log_idle, log_data);
            }
        }

        // Luego procesar comandos especiales para actualizar UI
        if (strncmp(line, "PROGRESS:", 9) == 0) {
            char *percent_str = strdup(line + 9);
            if (!percent_str) return;
            char *message = strchr(percent_str, ':');
            if (message) {
                *message = '\0';
                message++;
                char *endptr;
                long percent = strtol(percent_str, &endptr, 10);
                if (endptr != percent_str && percent >= 0 && percent <= 100) {
                    ProgressData *pdata = malloc(sizeof(ProgressData));
                    if (pdata) {
                        pdata->app = app;
                        pdata->percent = (int)percent;
                        pdata->message = g_strdup(message);
                        g_idle_add(update_progress_ui, pdata);
                    }
                }
            }
            free(percent_str);
        }
        else if (strncmp(line, "ERROR:", 6) == 0) {
            ErrorData *edata = malloc(sizeof(ErrorData));
            if (edata) {
                edata->app = app;
                edata->message = g_strdup(line + 6);
                printf("ERROR DETECTED: %s\n", edata->message);  // DEBUG
                g_idle_add((GSourceFunc)show_error_dialog, edata);
            }
        }
        else if (strncmp(line, "SUCCESS:", 8) == 0) {
            printf("SUCCESS DETECTED\n");  // DEBUG
            g_idle_add((GSourceFunc)show_success_dialog, app);
            app->config.installation_complete = true;
            app->config.installation_started = false;
            g_idle_add((GSourceFunc)update_navigation_buttons, app);

            ProgressData *final_progress = malloc(sizeof(ProgressData));
            if (final_progress) {
                final_progress->app = app;
                final_progress->percent = 100;
                final_progress->message = g_strdup("Installation complete!");
                g_idle_add(update_progress_ui, final_progress);
            }

            StatusData *sdata = malloc(sizeof(StatusData));
            if (sdata) {
                sdata->app = app;
                sdata->message = g_strdup("Installation completed successfully!");
                g_idle_add(update_status_ui, sdata);
            }
        }
        else if (strncmp(line, "INFO:", 5) == 0) {
            StatusData *sdata = malloc(sizeof(StatusData));
            if (sdata) {
                sdata->app = app;
                sdata->message = g_strdup(line + 5);
                g_idle_add(update_status_ui, sdata);
            }
        }
        else if (strlen(line) > 0) {
            if (strchr(line, ':') &&
                strncmp(line, "PROGRESS:", 9) != 0 &&
                strncmp(line, "ERROR:", 6) != 0 &&
                strncmp(line, "SUCCESS:", 8) != 0 &&
                strncmp(line, "INFO:", 5) != 0) {
                StatusData *sdata = malloc(sizeof(StatusData));
            if (sdata) {
                sdata->app = app;
                sdata->message = g_strdup(line);
                g_idle_add(update_status_ui, sdata);
            }
                }
                else if (strlen(line) < 100) {
                    StatusData *sdata = malloc(sizeof(StatusData));
                    if (sdata) {
                        sdata->app = app;
                        sdata->message = g_strdup(line);
                        g_idle_add(update_status_ui, sdata);
                    }
                }
        }
}

void* run_installation_thread(void *data) {
    InstallerApp *app = (InstallerApp*)data;

    printf("=== INSTALLATION THREAD STARTED ===\n");

    // Asegurarnos de que estamos en la pestaña de progreso
    gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), TAB_PROGRESS);
    g_idle_add((GSourceFunc)update_navigation_buttons, app);

    // Limpiar log anterior
    g_idle_add(clear_log_idle, app);

    // Mensaje inicial al log
    LogData *init_log = create_log_data(app, "=== Starting LOC-OS Installation ===");
    if (init_log) g_idle_add(append_to_log_idle, init_log);

    // Mensaje de estado inicial
    StatusData *initial_status = malloc(sizeof(StatusData));
    if (initial_status) {
        initial_status->app = app;
        initial_status->message = g_strdup("Preparing installation...");
        g_idle_add(update_status_ui, initial_status);
    }

    // Progreso inicial
    ProgressData *initial_progress = malloc(sizeof(ProgressData));
    if (initial_progress) {
        initial_progress->app = app;
        initial_progress->percent = 0;
        initial_progress->message = g_strdup("Starting...");
        g_idle_add(update_progress_ui, initial_progress);
    }

    // Construir comando de manera segura
    char cmd[8192];
    printf("Building installation command...\n");

    // Escapar todos los parámetros para shell
    char *escaped_disk = g_shell_quote(app->config.disk_device);
    char *escaped_username = g_shell_quote(app->config.username);
    char *escaped_realname = g_shell_quote(app->config.realname);
    char *escaped_hostname = g_shell_quote(app->config.hostname);
    char *escaped_password = g_shell_quote(app->config.password);
    char *escaped_timezone = g_shell_quote(app->config.timezone);
    char *escaped_keyboard = g_shell_quote(app->config.keyboard);
    char *escaped_language = g_shell_quote(app->config.language);
    char *escaped_keyboard_variant = g_shell_quote(app->config.keyboard_variant);

    // IMPORTANTE: Escapar también la contraseña de root
    char *escaped_root_password = g_shell_quote(app->config.root_password);

    // Construir la parte de la contraseña de root
    char root_params[256] = "";

    // SIEMPRE enviar root-password (ya sea misma que usuario o diferente)
    if (strlen(app->config.root_password) > 0) {
        snprintf(root_params, sizeof(root_params),
                 "--root-password=%s ", escaped_root_password);
        printf("DEBUG: Sending root password: %s\n", app->config.root_password);
    }

    if (app->config.auto_partition) {
        printf("Using AUTO partitioning\n");

        // Construir la parte de swap según el tipo
        char swap_params[256] = "";

        if (app->config.add_swap) {
            if (app->config.create_swapfile) {
                snprintf(swap_params, sizeof(swap_params),
                         "--add-swap=true "
                         "--create-swapfile=true "
                         "--swapfile-size=%d ",
                         app->config.swap_size_mb);
            } else {
                snprintf(swap_params, sizeof(swap_params),
                         "--add-swap=true "
                         "--create-swapfile=false "
                         "--swap-size=%d ",
                         app->config.swap_size_mb);
            }
        } else {
            snprintf(swap_params, sizeof(swap_params),
                     "--add-swap=false ");
        }

        // En el comando para modo auto:
        snprintf(cmd, sizeof(cmd),
                 "sudo " CORE_INSTALLER " install "
                 "--disk=%s "
                 "--auto-partition=true "
                 "--uefi-mode=%s "
                 "--sep-home=%s "
                 "%s"  // Swap params
                 "--username=%s "
                 "--realname=%s "
                 "--hostname=%s "
                 "--password=%s "
                 "%s"  // Root password params (SIEMPRE enviarlo)
        "--autologin=%s "
        "--timezone=%s "
        "--keyboard=%s "
        "--keyboard-variant=%s "
        "--language=%s "
        "2>&1",
        escaped_disk,
        app->config.uefi_mode ? "true" : "false",
        app->config.separate_home ? "true" : "false",
        swap_params,
        escaped_username,
        escaped_realname,
        escaped_hostname,
        escaped_password,
        root_params,  // <-- ESTO es lo que faltaba
        app->config.autologin ? "true" : "false",
        escaped_timezone,
        escaped_keyboard,
        escaped_keyboard_variant,
        escaped_language);
    } else {
        printf("Using MANUAL partitioning\n");

        // Construir parámetros de particiones
        char partitions_params[1024] = "";

        if (strlen(app->config.root_partition) > 0) {
            char *escaped_root = g_shell_quote(app->config.root_partition);
            snprintf(partitions_params + strlen(partitions_params),
                     sizeof(partitions_params) - strlen(partitions_params),
                     "--root-part=%s ", escaped_root);
            g_free(escaped_root);
        }

        if (app->config.separate_home && strlen(app->config.home_partition) > 0) {
            char *escaped_home = g_shell_quote(app->config.home_partition);
            snprintf(partitions_params + strlen(partitions_params),
                     sizeof(partitions_params) - strlen(partitions_params),
                     "--home-part=%s ", escaped_home);
            g_free(escaped_home);
        }

        if (app->config.separate_boot && strlen(app->config.boot_partition) > 0) {
            char *escaped_boot = g_shell_quote(app->config.boot_partition);
            snprintf(partitions_params + strlen(partitions_params),
                     sizeof(partitions_params) - strlen(partitions_params),
                     "--boot-part=%s ", escaped_boot);
            g_free(escaped_boot);
        }

        if (app->config.add_swap && strlen(app->config.swap_partition) > 0) {
            char *escaped_swap = g_shell_quote(app->config.swap_partition);
            snprintf(partitions_params + strlen(partitions_params),
                     sizeof(partitions_params) - strlen(partitions_params),
                     "--swap-part=%s ", escaped_swap);
            g_free(escaped_swap);
        }

        if (app->config.uefi_mode && strlen(app->config.efi_partition) > 0) {
            char *escaped_efi = g_shell_quote(app->config.efi_partition);
            snprintf(partitions_params + strlen(partitions_params),
                     sizeof(partitions_params) - strlen(partitions_params),
                     "--efi-part=%s ", escaped_efi);
            g_free(escaped_efi);
        }

        snprintf(cmd, sizeof(cmd),
                 "sudo " CORE_INSTALLER " install "
                 "--auto-partition=false "
                 "--uefi-mode=%s "
                 "%s"  // Parámetros de particiones
                 "--username=%s "
                 "--realname=%s "
                 "--hostname=%s "
                 "--password=%s "
                 "%s"  // Root password params (SIEMPRE enviarlo)
        "--autologin=%s "
        "--timezone=%s "
        "--keyboard=%s "
        "--keyboard-variant=%s "
        "--language=%s "
        "2>&1",
        app->config.uefi_mode ? "true" : "false",
        partitions_params,
        escaped_username,
        escaped_realname,
        escaped_hostname,
        escaped_password,
        root_params,  // <-- ESTO es lo que faltaba
        app->config.autologin ? "true" : "false",
        escaped_timezone,
        escaped_keyboard,
        escaped_keyboard_variant,
        escaped_language);
    }

    // Liberar strings escapados
    g_free(escaped_disk);
    g_free(escaped_username);
    g_free(escaped_realname);
    g_free(escaped_hostname);
    g_free(escaped_password);
    g_free(escaped_timezone);
    g_free(escaped_keyboard);
    g_free(escaped_language);
    g_free(escaped_keyboard_variant);
    g_free(escaped_root_password);  // Liberar la nueva

    printf("DEBUG: Executing command (root password included): %s\n", cmd);

    // Añadir comando al log (version segura sin contraseñas)
    LogData *cmd_log = create_log_data(app, "=== Installation Command (passwords hidden) ===");
    if (cmd_log) g_idle_add(append_to_log_idle, cmd_log);

    // Mostrar comando truncado por seguridad (sin contraseña)
    char safe_cmd[1024];
    if (app->config.auto_partition) {
        snprintf(safe_cmd, sizeof(safe_cmd),
                 "Command: sudo " CORE_INSTALLER " install "
                 "--disk=%s --username=%s --hostname=%s "
                 "(passwords hidden) ...",
                 app->config.disk_device,
                 app->config.username,
                 app->config.hostname);
    } else {
        snprintf(safe_cmd, sizeof(safe_cmd),
                 "Command: sudo " CORE_INSTALLER " install "
                 "--root-part=%s --username=%s --hostname=%s "
                 "(passwords hidden) ...",
                 app->config.root_partition,
                 app->config.username,
                 app->config.hostname);
    }
    LogData *safe_cmd_log = create_log_data(app, safe_cmd);
    if (safe_cmd_log) g_idle_add(append_to_log_idle, safe_cmd_log);

    // Ejecutar comando
    printf("Opening pipe to installation command...\n");
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        printf("ERROR: Failed to popen command\n");
        char *error_msg = g_strdup_printf(_("Failed to start installation process: %s"), strerror(errno));
        LogData *error_log = create_log_data(app, error_msg);
        if (error_log) g_idle_add(append_to_log_idle, error_log);

        // Crear ErrorData y pasar a show_error_dialog
        ErrorData *edata = malloc(sizeof(ErrorData));
        if (edata) {
            edata->app = app;
            edata->message = error_msg;
            g_idle_add((GSourceFunc)show_error_dialog, edata);
        } else {
            free(error_msg);
        }

        app->config.installation_started = false;
        app->config.installation_complete = false;
        g_idle_add((GSourceFunc)update_navigation_buttons, app);

        return NULL;
    }

    printf("Command started, reading output...\n");
    char line_buffer[2048];  // Buffer más grande
    int line_count = 0;

    // Actualizar estado a "en progreso"
    StatusData *running_status = malloc(sizeof(StatusData));
    if (running_status) {
        running_status->app = app;
        running_status->message = g_strdup("Installation in progress...");
        g_idle_add(update_status_ui, running_status);
    }

    while (fgets(line_buffer, sizeof(line_buffer), fp)) {
        line_count++;

        // Eliminar el \n al final si existe
        line_buffer[strcspn(line_buffer, "\n")] = '\0';

        // IMPORTANTE: Manejar los \r (retorno de carro) de rsync
        // rsync usa \r para sobreescribir la misma línea
        char *clean_line = line_buffer;

        // Buscar el último \r en la línea
        char *last_cr = strrchr(line_buffer, '\r');
        if (last_cr) {
            // Si hay un \r, usar solo lo que viene después del último \r
            clean_line = last_cr + 1;

            // Si después del \r solo hay espacios o está vacío, ignorar esta línea
            if (strlen(clean_line) == 0 || clean_line[0] == ' ') {
                continue; // Saltar esta línea, es solo una actualización
            }
        }

        // DEBUG: Mostrar en consola también
        printf("[INSTALLER:%03d] %s\n", line_count, clean_line);
        fflush(stdout);  // Forzar salida inmediata

        parse_installation_output(clean_line, app);
    }

    int exit_code = pclose(fp);
    printf("Command finished with exit code: %d\n", exit_code);

    // Añadir código de salida al log
    char exit_msg[256];
    snprintf(exit_msg, sizeof(exit_msg), "=== Installation process finished with exit code: %d ===", exit_code);
    LogData *exit_log = create_log_data(app, exit_msg);
    if (exit_log) g_idle_add(append_to_log_idle, exit_log);

    // Marcar instalación como completada
    app->config.installation_complete = true;
    app->config.installation_started = false;

    printf("Installation marked as complete\n");

    // Actualizar navegación (mostrar botón Finish)
    g_idle_add((GSourceFunc)update_navigation_buttons, app);

    // Si la instalación fue exitosa, mostrar diálogo de éxito
    if (exit_code == 0 || exit_code == 23 || exit_code == 24) {
        // Códigos de rsync aceptables (0=éxito, 23/24=advertencias)
        printf("Installation SUCCESSFUL (exit code: %d)\n", exit_code);

        app->config.installation_complete = true;
        app->config.installation_started = false;
        g_idle_add((GSourceFunc)update_navigation_buttons, app);

        // Mensaje de éxito al log
        LogData *success_log = create_log_data(app, "=== Installation completed successfully! ===");
        if (success_log) g_idle_add(append_to_log_idle, success_log);

        // Actualizar barra de progreso a 100%
        ProgressData *final_progress = malloc(sizeof(ProgressData));
        if (final_progress) {
            final_progress->app = app;
            final_progress->percent = 100;
            final_progress->message = g_strdup("Installation complete!");
            g_idle_add(update_progress_ui, final_progress);
        }
    } else {
        printf("Installation FAILED (exit code: %d)\n", exit_code);
        char *error_msg = g_strdup_printf(_("Installation failed with exit code %d"), exit_code);
        LogData *error_log = create_log_data(app, error_msg);
        if (error_log) g_idle_add(append_to_log_idle, error_log);

        // Crear ErrorData y pasar a show_error_dialog
        ErrorData *edata = malloc(sizeof(ErrorData));
        if (edata) {
            edata->app = app;
            edata->message = error_msg;
            g_idle_add((GSourceFunc)show_error_dialog, edata);
        } else {
            free(error_msg);
        }
    }

    printf("=== INSTALLATION THREAD FINISHED ===\n");
    return NULL;
}

void start_installation(InstallerApp *app) {
    if (app->config.installation_started) {
        printf("DEBUG: Installation already started\n");
        return;
    }

    printf("DEBUG: Starting installation process...\n");

    // Iniciar instalación
    app->config.installation_started = true;

    // Deshabilitar botones de navegación
    if (app->prev_btn) gtk_widget_set_sensitive(app->prev_btn, FALSE);
    if (app->next_btn) gtk_widget_set_sensitive(app->next_btn, FALSE);
    if (app->install_btn) gtk_widget_set_sensitive(app->install_btn, FALSE);

    // También deshabilitar el botón de copiar log hasta que haya algo
    if (app->copy_log_btn) gtk_widget_set_sensitive(app->copy_log_btn, FALSE);

    // Actualizar botones de navegación
    update_navigation_buttons(app);

    gtk_label_set_text(GTK_LABEL(app->status_label), _("Starting installation..."));

    // Crear hilo de instalación
    int thread_result = pthread_create(&app->install_thread, NULL, run_installation_thread, app);
    if (thread_result != 0) {
        GtkWidget *error_dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                                         GTK_DIALOG_MODAL,
                                                         GTK_MESSAGE_ERROR,
                                                         GTK_BUTTONS_OK,
                                                         _("Failed to create installation thread: %s"),
                                                         strerror(thread_result));
        gtk_dialog_run(GTK_DIALOG(error_dialog));
        gtk_widget_destroy(error_dialog);

        app->config.installation_started = false;
        // Re-habilitar botones
        if (app->prev_btn) gtk_widget_set_sensitive(app->prev_btn, TRUE);
        if (app->next_btn) gtk_widget_set_sensitive(app->next_btn, TRUE);
        if (app->install_btn) gtk_widget_set_sensitive(app->install_btn, TRUE);

        update_navigation_buttons(app);
    } else {
        app->thread_running = true;
    }
}

/* ==================== ENTRY POINT ==================== */

int installer_run(int argc, char *argv[]) {
    InstallerApp *app;

    setlocale(LC_ALL, "");
    bindtextdomain("loc-installer", "/usr/share/locale");
    textdomain("loc-installer");

    gtk_init(&argc, &argv);

    app = malloc(sizeof(InstallerApp));
    if (!app) {
        fprintf(stderr, _("Failed to allocate memory\n"));
        return 1;
    }

    memset(app, 0, sizeof(InstallerApp));
    pthread_mutex_init(&app->mutex, NULL);

    /* Default configuration */
    app->config.uefi_mode = is_uefi_boot();
    app->config.auto_partition = true;
    app->config.separate_home = false;
    app->config.separate_boot = false;
    app->config.add_swap = false;
    app->config.create_swapfile = false;
    app->config.swap_size_mb = 2048;
    app->config.autologin = false;
    app->config.same_root_password = true;
    app->config.installation_started = false;
    app->config.installation_complete = false;
    app->last_page = TAB_REGIONAL;

    create_main_window(app);

    gtk_widget_show_all(app->window);  // Mostrar TODO primero

    // Ocultar los contenedores específicos
    gtk_widget_hide(app->home_combo_container);
    gtk_widget_hide(app->boot_combo_container);
    gtk_widget_hide(app->swap_combo_container);
    gtk_widget_hide(app->swap_options_container);
    gtk_widget_hide(app->root_password_container);

    // Establecer pestaña inicial usando constante
    gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), TAB_REGIONAL);

    // Actualizar last_page
    app->last_page = TAB_REGIONAL;

    // Forzar actualización de botones
    update_navigation_buttons(app);

    gtk_main();

    /* Cleanup */
    if (app->thread_running) {
        pthread_join(app->install_thread, NULL);
    }

    pthread_mutex_destroy(&app->mutex);
    free(app);

    return 0;
}
