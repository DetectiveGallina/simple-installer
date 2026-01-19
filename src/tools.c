/*
 * tools.c - Utility functions and callbacks for LOC-OS 24 Installer
 */

#include "installer.h"

/* ==================== UTILITY FUNCTIONS ==================== */

char* get_selected_timezone(InstallerApp *app) {
    const char *region_raw = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->region_combo));
    const char *city_raw = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->city_combo));

    gchar *region = g_utf8_make_valid(region_raw, -1);
    gchar *city = g_utf8_make_valid(city_raw, -1);

    if (!region) {
        if (city) g_free(city);
        return strdup("UTC");
    }

    // Si es UTC, GMT, etc.
    if (strcmp(region, "UTC") == 0 || strcmp(region, "GMT") == 0) {
        g_free(region);
        if (city) g_free(city);
        return strdup(region);
    }

    char *timezone = NULL;

    // Si no hay ciudad o está vacía
    if (!city || strlen(city) == 0) {
        // Usar ciudad por defecto
        if (strcmp(region, "America") == 0) {
            timezone = strdup("America/New_York");
        } else if (strcmp(region, "Europe") == 0) {
            timezone = strdup("Europe/London");
        } else if (strcmp(region, "Asia") == 0) {
            timezone = strdup("Asia/Tokyo");
        } else if (strcmp(region, "Australia") == 0) {
            timezone = strdup("Australia/Sydney");
        } else {
            timezone = strdup("UTC");
        }
    } else {
        // Construir zona horaria completa
        timezone = malloc(strlen(region) + strlen(city) + 2);
        if (timezone) {
            sprintf(timezone, "%s/%s", region, city);
        }
    }

    g_free(region);
    if (city) g_free(city);

    return timezone ? timezone : strdup("UTC");
}

void set_current_timezone(InstallerApp *app) {
    char *current_tz = get_current_timezone();
    if (!current_tz) {
        current_tz = strdup("UTC");
    }

    // Convertir a UTF-8 válido
    gchar *valid_tz = g_utf8_make_valid(current_tz, -1);
    free(current_tz);

    if (!valid_tz) {
        return;
    }

    // Parsear la zona horaria actual
    char *slash = strchr(valid_tz, '/');

    if (slash) {
        *slash = '\0';
        char *region = valid_tz;
        char *city = slash + 1;

        // Buscar región
        for (int i = 0; i < app->region_count; i++) {
            if (app->region_names[i] && strcmp(app->region_names[i], region) == 0) {
                gtk_combo_box_set_active(GTK_COMBO_BOX(app->region_combo), i);

                // Llenar ciudades para esta región
                on_region_changed(GTK_COMBO_BOX(app->region_combo), app);

                // Buscar ciudad
                char **cities = app->timezone_regions[i];
                if (cities) {
                    for (int j = 0; cities[j] != NULL; j++) {
                        if (strstr(city, cities[j]) == city) {
                            gtk_combo_box_set_active(GTK_COMBO_BOX(app->city_combo), j);
                            break;
                        }
                    }
                }
                break;
            }
        }
    } else {
        // Zona sin barra (UTC, GMT, etc.)
        for (int i = 0; i < app->region_count; i++) {
            if (app->region_names[i] && strcmp(app->region_names[i], valid_tz) == 0) {
                gtk_combo_box_set_active(GTK_COMBO_BOX(app->region_combo), i);
                on_region_changed(GTK_COMBO_BOX(app->region_combo), app);
                break;
            }
        }
    }

    g_free(valid_tz);
}

void load_timezones_hierarchical(InstallerApp *app) {
    // Cargar todas las zonas horarias
    int tz_count;
    char **timezones = get_timezones(&tz_count);

    if (!timezones || tz_count == 0) {
        // Fallback básico
        app->region_count = 1;
        app->region_names = malloc(sizeof(char*));
        app->region_names[0] = strdup("America");

        app->timezone_regions = malloc(sizeof(char**));
        app->timezone_regions[0] = malloc(6 * sizeof(char*));
        app->timezone_regions[0][0] = strdup("New_York");
        app->timezone_regions[0][1] = strdup("Los_Angeles");
        app->timezone_regions[0][2] = strdup("Chicago");
        app->timezone_regions[0][3] = strdup("Denver");
        app->timezone_regions[0][4] = strdup("Mexico_City");
        app->timezone_regions[0][5] = NULL;
        return;
    }

    // Estructura temporal para regiones
    typedef struct {
        char *name;
        char **cities;
        int city_count;
        int city_capacity;
    } TimezoneRegion;

    TimezoneRegion *temp_regions = malloc(tz_count * sizeof(TimezoneRegion));
    int region_count = 0;

    for (int i = 0; i < tz_count; i++) {
        char *tz = timezones[i];

        // Saltar especiales
        if (strcmp(tz, "leapseconds") == 0 ||
            strcmp(tz, "tzdata.zi") == 0 ||
            strcmp(tz, "Factory") == 0) {
            continue;
            }

            char *slash = strchr(tz, '/');
        char *region_name;

        if (slash) {
            // Extraer nombre de región (primera parte)
            int region_len = slash - tz;
            region_name = malloc(region_len + 1);
            strncpy(region_name, tz, region_len);
            region_name[region_len] = '\0';
        } else {
            // Zonas sin barra (UTC, GMT, etc.)
            region_name = strdup(tz);
        }

        // Buscar si ya existe esta región
        int region_idx = -1;
        for (int j = 0; j < region_count; j++) {
            if (strcmp(temp_regions[j].name, region_name) == 0) {
                region_idx = j;
                free(region_name);
                break;
            }
        }

        if (region_idx == -1) {
            // Nueva región
            region_idx = region_count;
            temp_regions[region_idx].name = region_name;
            temp_regions[region_idx].cities = malloc(20 * sizeof(char*));
            temp_regions[region_idx].city_count = 0;
            temp_regions[region_idx].city_capacity = 20;
            region_count++;
        }

        // Procesar ciudad/subregión
        if (slash) {
            char *city_part = slash + 1;
            char *second_slash = strchr(city_part, '/');

            if (second_slash) {
                // Formato: Region/Subregion/City
                char *city_name = strdup(city_part);

                // Verificar si ya existe
                int city_found = 0;
                for (int k = 0; k < temp_regions[region_idx].city_count; k++) {
                    if (strcmp(temp_regions[region_idx].cities[k], city_name) == 0) {
                        city_found = 1;
                        free(city_name);
                        break;
                    }
                }

                if (!city_found) {
                    // Añadir ciudad si necesitamos más capacidad
                    if (temp_regions[region_idx].city_count >= temp_regions[region_idx].city_capacity) {
                        temp_regions[region_idx].city_capacity *= 2;
                        temp_regions[region_idx].cities = realloc(temp_regions[region_idx].cities,
                                                                  temp_regions[region_idx].city_capacity * sizeof(char*));
                    }

                    temp_regions[region_idx].cities[temp_regions[region_idx].city_count] = city_name;
                    temp_regions[region_idx].city_count++;
                }
            } else {
                // Formato simple: Region/City
                char *city_name = strdup(city_part);

                // Verificar si ya existe
                int city_found = 0;
                for (int k = 0; k < temp_regions[region_idx].city_count; k++) {
                    if (strcmp(temp_regions[region_idx].cities[k], city_name) == 0) {
                        city_found = 1;
                        free(city_name);
                        break;
                    }
                }

                if (!city_found) {
                    // Añadir ciudad si necesitamos más capacidad
                    if (temp_regions[region_idx].city_count >= temp_regions[region_idx].city_capacity) {
                        temp_regions[region_idx].city_capacity *= 2;
                        temp_regions[region_idx].cities = realloc(temp_regions[region_idx].cities,
                                                                  temp_regions[region_idx].city_capacity * sizeof(char*));
                    }

                    temp_regions[region_idx].cities[temp_regions[region_idx].city_count] = city_name;
                    temp_regions[region_idx].city_count++;
                }
            }
        } else {
            // Para UTC/GMT, etc.
            if (temp_regions[region_idx].city_count == 0) {
                temp_regions[region_idx].cities[0] = strdup("");
                temp_regions[region_idx].city_count = 1;
            }
        }
    }

    // Ordenar regiones alfabéticamente
    for (int i = 0; i < region_count - 1; i++) {
        for (int j = i + 1; j < region_count; j++) {
            if (strcmp(temp_regions[i].name, temp_regions[j].name) > 0) {
                TimezoneRegion temp = temp_regions[i];
                temp_regions[i] = temp_regions[j];
                temp_regions[j] = temp;
            }
        }
    }

    // Ordenar ciudades dentro de cada región
    for (int i = 0; i < region_count; i++) {
        if (temp_regions[i].city_count > 1) {
            for (int j = 0; j < temp_regions[i].city_count - 1; j++) {
                for (int k = j + 1; k < temp_regions[i].city_count; k++) {
                    if (strcmp(temp_regions[i].cities[j], temp_regions[i].cities[k]) > 0) {
                        char *temp_city = temp_regions[i].cities[j];
                        temp_regions[i].cities[j] = temp_regions[i].cities[k];
                        temp_regions[i].cities[k] = temp_city;
                    }
                }
            }
        }
    }

    // Convertir a la estructura final
    app->region_count = region_count;
    app->region_names = malloc(region_count * sizeof(char*));
    app->timezone_regions = malloc(region_count * sizeof(char**));

    for (int i = 0; i < region_count; i++) {
        app->region_names[i] = strdup(temp_regions[i].name);

        // Añadir terminador NULL al array de ciudades
        temp_regions[i].city_count++;
        temp_regions[i].cities = realloc(temp_regions[i].cities,
                                         temp_regions[i].city_count * sizeof(char*));
        temp_regions[i].cities[temp_regions[i].city_count - 1] = NULL;

        app->timezone_regions[i] = temp_regions[i].cities;
    }

    // Liberar memoria temporal
    for (int i = 0; i < tz_count; i++) {
        free(timezones[i]);
    }
    free(timezones);

    // Liberar estructura temporal (pero no las ciudades, que fueron transferidas)
    for (int i = 0; i < region_count; i++) {
        free(temp_regions[i].name);
    }
    free(temp_regions);
}

void free_timezones_hierarchical(InstallerApp *app) {
    if (!app->timezone_regions) return;

    for (int i = 0; i < app->region_count; i++) {
        if (app->timezone_regions[i]) {
            for (int j = 0; app->timezone_regions[i][j] != NULL; j++) {
                free(app->timezone_regions[i][j]);
            }
            free(app->timezone_regions[i]);
        }
    }
    free(app->timezone_regions);
    app->timezone_regions = NULL;
    app->region_count = 0;
}

char* extract_code(const char *text) {
    if (!text) return NULL;

    // Convertir a UTF-8 válido primero
    gchar *valid_text = g_utf8_make_valid(text, -1);
    if (!valid_text) return NULL;

    // Copiar el texto válido
    char *code = strdup(valid_text);
    g_free(valid_text);

    if (!code) return NULL;

    // Buscar el primer espacio o guión
    char *end = strpbrk(code, " -");
    if (end) {
        *end = '\0';
    }

    // También quitar cualquier texto entre paréntesis
    char *paren = strchr(code, '(');
    if (paren) {
        *paren = '\0';
    }

    // Trim espacios
    char *end_trim = code + strlen(code) - 1;
    while (end_trim > code && isspace((unsigned char)*end_trim)) {
        *end_trim = '\0';
        end_trim--;
    }

    return code;
}

bool is_uefi_boot(void) {
    return access("/sys/firmware/efi", F_OK) == 0;
}

bool is_valid_username(const char *username) {
    if (!username || strlen(username) < 2 || strlen(username) > 32) {
        return false;
    }

    for (const char *p = username; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') ||
            (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') ||
            *p == '_' || *p == '-')) {
            return false;
            }
    }

    return !(username[0] >= '0' && username[0] <= '9');
}

bool is_valid_hostname(const char *hostname) {
    if (!hostname || strlen(hostname) < 1 || strlen(hostname) > 63) {
        return false;
    }

    for (const char *p = hostname; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') ||
            (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') ||
            *p == '-')) {
            return false;
            }
    }

    return !(hostname[0] == '-' || hostname[strlen(hostname)-1] == '-');
}

bool is_valid_password(const char *password) {
    if (!password || strlen(password) < 1) {
        return false;
    }
    return true;
}

char* run_command(const char *cmd) {
    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;

    char buffer[4096];
    char *result = malloc(4096);
    result[0] = '\0';

    while (fgets(buffer, sizeof(buffer), fp)) {
        strcat(result, buffer);
    }

    pclose(fp);
    return result;
}

char** list_partitions(int *count) {
    FILE *fp = popen(SYSINFO_SCRIPT " partitions", "r");
    if (!fp) return NULL;

    char **partitions = malloc(50 * sizeof(char*));
    char line[256];
    *count = 0;

    while (fgets(line, sizeof(line), fp) && *count < 50) {
        line[strcspn(line, "\n")] = 0;
        if (strlen(line) > 0) {
            // Parsear: /dev/├─sda1|6.2G|ext4|
            // O: /dev/└─sda2|3.8G|ext4|
            char device[64], size[32], fstype[32];
            char *pipe1 = strchr(line, '|');
            char *pipe2 = pipe1 ? strchr(pipe1 + 1, '|') : NULL;
            char *pipe3 = pipe2 ? strchr(pipe2 + 1, '|') : NULL;

            if (pipe1 && pipe2) {
                // Marcar los fin de campos
                *pipe1 = '\0';
                *pipe2 = '\0';
                if (pipe3) {
                    *pipe3 = '\0';
                }

                // Copiar los campos con límites seguros
                strncpy(device, line, sizeof(device)-1);
                device[sizeof(device)-1] = '\0';  // Asegurar terminación

                strncpy(size, pipe1 + 1, sizeof(size)-1);
                size[sizeof(size)-1] = '\0';

                strncpy(fstype, pipe2 + 1, sizeof(fstype)-1);
                fstype[sizeof(fstype)-1] = '\0';

                // Construir string para mostrar (manteniendo el formato del árbol si está presente)
                char *display = malloc(256);

                // Verificar si la línea contiene caracteres de árbol
                char *tree_chars = strstr(device, "├─");
                if (!tree_chars) {
                    tree_chars = strstr(device, "└─");
                }

                if (tree_chars) {
                    // Separar el prefijo (/dev/) de la partición (├─sda1)
                    char prefix[32];
                    char part_name[32];

                    // Copiar hasta el caracter de árbol
                    size_t prefix_len = tree_chars - device;
                    strncpy(prefix, device, prefix_len);
                    prefix[prefix_len] = '\0';

                    // Copiar la parte después del árbol
                    snprintf(part_name, sizeof(part_name), "%s", tree_chars);

                    // Mostrar sin los caracteres especiales del árbol
                    char clean_part[32];
                    // Saltar los caracteres UTF-8 del árbol (3 bytes cada uno: ├─ o └─)
                    snprintf(clean_part, sizeof(clean_part), "%s", part_name + 6); // 6 bytes para "├─" o "└─"

                    snprintf(display, 256, "%s%s - %s %s",
                             prefix, clean_part, size, fstype);
                } else {
                    // Formato normal
                    snprintf(display, 256, "%s - %s %s", device, size, fstype);
                }

                partitions[*count] = display;
                (*count)++;

                // Restaurar los pipes para mantener el buffer consistente
                // (aunque no es estrictamente necesario)
                *pipe1 = '|';
                *pipe2 = '|';
                if (pipe3) {
                    *pipe3 = '|';
                }
            } else {
                // Formato simple si no hay pipes
                char *display = malloc(256);
                snprintf(display, 256, "%s", line);
                partitions[*count] = display;
                (*count)++;
            }
        }
    }

    pclose(fp);
    return partitions;
}

bool extract_device(const char *display_text, char *buffer, size_t buffer_size) {
    if (!display_text || strncmp(display_text, "(None)", 6) == 0) {
        if (buffer_size > 0) {
            buffer[0] = '\0';
        }
        return false;
    }

    // Buscar el primer pipe o guión
    // Formato: "/dev/sda - 10G - Modelo" o "/dev/sda|10G|Modelo"
    const char *separator = strchr(display_text, '|');
    if (!separator) {
        separator = strstr(display_text, " - ");
    }

    if (separator) {
        size_t len = separator - display_text;
        if (len < buffer_size) {
            strncpy(buffer, display_text, len);
            buffer[len] = '\0';

            // Trim espacios al final
            char *end = buffer + strlen(buffer) - 1;
            while (end > buffer && isspace((unsigned char)*end)) {
                *end = '\0';
                end--;
            }
            return true;
        }
    }

    // Si no hay separador, usar todo
    g_strlcpy(buffer, display_text, buffer_size);
    return true;
}

void free_string_array(char **array, int count) {
    if (!array) return;
    for (int i = 0; i < count; i++) free(array[i]);
    free(array);
}

int get_disk_size_gb(const char *device) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "lsblk -b -dn -o SIZE %s 2>/dev/null | awk '{print int($1/1073741824)}'",
             device);

    char *result = run_command(cmd);
    int size = result ? atoi(result) : 0;
    free(result);

    return size > 0 ? size : 100;
}

static char* get_combo_selected_device(GtkComboBoxText *combo) {
    if (!combo) return NULL;

    const char *text = gtk_combo_box_text_get_active_text(combo);
    if (!text) return NULL;

    // Si es "(None)", ignorar
    int index = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
    if (index <= 0) {
        return NULL;
    }

    // Extraer el dispositivo del texto
    char device[64];
    if (extract_device(text, device, sizeof(device))) {
        return strdup(device); // Devuelve una copia
    }

    return NULL;
}

void on_partition_combo_changed(GtkComboBox *combo, InstallerApp *app) {
    if (app->updating_partition_combos) return;

    app->updating_partition_combos = true;
    // Convertir combo a GtkComboBoxText* para comparar
    GtkComboBoxText *changed_combo = GTK_COMBO_BOX_TEXT(combo);

    // Lista de todos los combos
    GtkComboBoxText *all_combos[] = {
        GTK_COMBO_BOX_TEXT(app->root_combo),
        GTK_COMBO_BOX_TEXT(app->efi_combo),
        GTK_COMBO_BOX_TEXT(app->home_combo),
        GTK_COMBO_BOX_TEXT(app->boot_combo),
        GTK_COMBO_BOX_TEXT(app->swap_combo)
    };

    // Para cada combo excepto el que cambió
    for (int i = 0; i < 5; i++) {
        if (all_combos[i] && all_combos[i] != changed_combo) {
            // Guardar selección actual
            int current_index = gtk_combo_box_get_active(GTK_COMBO_BOX(all_combos[i]));

            // Solo repoblar si tenía algo seleccionado (índice > 0)
            if (current_index > 0) {
                const char *current_text = gtk_combo_box_text_get_active_text(all_combos[i]);
                if (current_text) {
                    // Guardar el texto actual
                    char saved_text[256];
                    strncpy(saved_text, current_text, sizeof(saved_text)-1);
                    saved_text[sizeof(saved_text)-1] = '\0';

                    // Repoblar el combo
                    populate_partition_combo(all_combos[i], app);

                    // Intentar restaurar la selección
                    GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(all_combos[i]));
                    GtkTreeIter iter;
                    int new_index = -1;
                    int idx = 0;

                    if (gtk_tree_model_get_iter_first(model, &iter)) {
                        do {
                            gchar *item_text;
                            gtk_tree_model_get(model, &iter, 0, &item_text, -1);

                            if (item_text && strcmp(item_text, saved_text) == 0) {
                                new_index = idx;
                                g_free(item_text);
                                break;
                            }

                            if (item_text) g_free(item_text);
                            idx++;
                        } while (gtk_tree_model_iter_next(model, &iter));
                    }

                    if (new_index >= 0) {
                        gtk_combo_box_set_active(GTK_COMBO_BOX(all_combos[i]), new_index);
                    } else {
                        gtk_combo_box_set_active(GTK_COMBO_BOX(all_combos[i]), 0);
                    }
                }
            }
        }
    }
    app->updating_partition_combos = false;
}

int populate_partition_combo(GtkComboBoxText *combo, InstallerApp *app) {
    gtk_combo_box_text_remove_all(combo);

    const char *none_text = _("(None)");
    gtk_combo_box_text_append_text(combo, none_text);

    int count;
    char **partitions = list_partitions(&count);

    if (!partitions) {
        return 0;
    }

    // Crear lista de dispositivos ya seleccionados en otros combos
    char *excluded_devices[10] = {NULL};
    int excluded_count = 0;

    // Obtener dispositivo seleccionado en cada combo relevante
    // CASTEAR a GtkComboBoxText* para comparar
    if ((GtkComboBoxText*)combo != (GtkComboBoxText*)app->root_combo) {
        char *root_dev = get_combo_selected_device(GTK_COMBO_BOX_TEXT(app->root_combo));
        if (root_dev) excluded_devices[excluded_count++] = root_dev;
    }

    if (app->efi_combo && (GtkComboBoxText*)combo != (GtkComboBoxText*)app->efi_combo) {
        char *efi_dev = get_combo_selected_device(GTK_COMBO_BOX_TEXT(app->efi_combo));
        if (efi_dev) excluded_devices[excluded_count++] = efi_dev;
    }

    if (app->home_combo && (GtkComboBoxText*)combo != (GtkComboBoxText*)app->home_combo) {
        char *home_dev = get_combo_selected_device(GTK_COMBO_BOX_TEXT(app->home_combo));
        if (home_dev) excluded_devices[excluded_count++] = home_dev;
    }

    if (app->boot_combo && (GtkComboBoxText*)combo != (GtkComboBoxText*)app->boot_combo) {
        char *boot_dev = get_combo_selected_device(GTK_COMBO_BOX_TEXT(app->boot_combo));
        if (boot_dev) excluded_devices[excluded_count++] = boot_dev;
    }

    if (app->swap_combo && (GtkComboBoxText*)combo != (GtkComboBoxText*)app->swap_combo) {
        char *swap_dev = get_combo_selected_device(GTK_COMBO_BOX_TEXT(app->swap_combo));
        if (swap_dev) excluded_devices[excluded_count++] = swap_dev;
    }

    // Agregar particiones que no estén en la lista de excluidas
    int added_count = 0;
    for (int i = 0; i < count; i++) {
        char device[64];
        if (extract_device(partitions[i], device, sizeof(device))) {
            bool excluded = false;

            // Verificar si este dispositivo ya está seleccionado
            for (int j = 0; j < excluded_count; j++) {
                if (excluded_devices[j] && strcmp(device, excluded_devices[j]) == 0) {
                    excluded = true;
                    break;
                }
            }

            if (!excluded) {
                gtk_combo_box_text_append_text(combo, partitions[i]);
                added_count++;
            }
        } else {
            // Si no podemos extraer dispositivo, agregar de todos modos
            gtk_combo_box_text_append_text(combo, partitions[i]);
            added_count++;
        }
    }

    // Limpiar memoria de dispositivos excluidos
    for (int i = 0; i < excluded_count; i++) {
        free(excluded_devices[i]);
    }

    free_string_array(partitions, count);

    return added_count;
}

void refresh_partition_combos(InstallerApp *app) {
    if (app->root_combo) populate_partition_combo(GTK_COMBO_BOX_TEXT(app->root_combo), app);
    if (app->home_combo) populate_partition_combo(GTK_COMBO_BOX_TEXT(app->home_combo), app);
    if (app->swap_combo) populate_partition_combo(GTK_COMBO_BOX_TEXT(app->swap_combo), app);
    if (app->efi_combo) populate_partition_combo(GTK_COMBO_BOX_TEXT(app->efi_combo), app);
}

int find_combo_item(GtkComboBoxText *combo, const char *search_text) {
    GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(combo));
    GtkTreeIter iter;
    int index = 0;

    if (gtk_tree_model_get_iter_first(model, &iter)) {
        do {
            gchar *item_text;
            gtk_tree_model_get(model, &iter, 0, &item_text, -1);

            if (item_text && strcmp(item_text, search_text) == 0) {
                g_free(item_text);
                return index;
            }

            g_free(item_text);
            index++;
        } while (gtk_tree_model_iter_next(model, &iter));
    }

    return 0; // Default to first item if not found
}

LogData* create_log_data(InstallerApp *app, const char *text) {
    LogData *data = malloc(sizeof(LogData));
    if (data) {
        data->app = app;
        data->text = text ? g_strdup(text) : NULL;
    }
    return data;
}

void free_log_data(LogData *data) {
    if (data) {
        g_free(data->text);
        free(data);
    }
}

void update_last_log_line(InstallerApp *app, const char *text) {
    if (!app || !app->log_buffer || !text) return;

    GtkTextIter start, end;

    // Obtener la última línea
    gtk_text_buffer_get_end_iter(app->log_buffer, &end);
    start = end;

    // Mover al inicio de la última línea
    if (gtk_text_iter_backward_line(&start)) {
        gtk_text_iter_forward_to_line_end(&start);
        if (!gtk_text_iter_is_end(&start)) {
            gtk_text_iter_forward_line(&start);
        }
    } else {
        // Si no hay línea anterior, empezar desde el inicio
        gtk_text_buffer_get_start_iter(app->log_buffer, &start);
    }

    // Borrar la última línea
    gtk_text_buffer_delete(app->log_buffer, &start, &end);

    // Insertar el nuevo texto
    gtk_text_buffer_get_end_iter(app->log_buffer, &end);
    gtk_text_buffer_insert(app->log_buffer, &end, text, -1);
    gtk_text_buffer_insert(app->log_buffer, &end, "\n", -1);

    // Auto-scroll
    if (app->log_text_view) {
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(app->log_text_view),
                                     &end, 0.0, FALSE, 0.0, 1.0);
    }
}

gboolean update_last_log_line_idle(gpointer data) {
    LogData *ldata = (LogData*)data;
    if (!ldata || !ldata->app || !ldata->text) {
        free_log_data(ldata);
        return G_SOURCE_REMOVE;
    }

    if (ldata->app->log_buffer) {
        GtkTextIter start, end;

        // Obtener la última línea
        gtk_text_buffer_get_end_iter(ldata->app->log_buffer, &end);
        start = end;

        // Mover al inicio de la última línea
        if (gtk_text_iter_backward_line(&start)) {
            gtk_text_iter_forward_to_line_end(&start);
            if (!gtk_text_iter_is_end(&start)) {
                gtk_text_iter_forward_line(&start);
            }
        } else {
            gtk_text_buffer_get_start_iter(ldata->app->log_buffer, &start);
        }

        // Borrar la última línea
        gtk_text_buffer_delete(ldata->app->log_buffer, &start, &end);

        // Insertar el nuevo texto
        gtk_text_buffer_get_end_iter(ldata->app->log_buffer, &end);
        gtk_text_buffer_insert(ldata->app->log_buffer, &end, ldata->text, -1);
        gtk_text_buffer_insert(ldata->app->log_buffer, &end, "\n", -1);

        // Auto-scroll
        if (ldata->app->log_text_view) {
            gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(ldata->app->log_text_view),
                                         &end, 0.0, FALSE, 0.0, 1.0);
        }
    }

    free_log_data(ldata);
    return G_SOURCE_REMOVE;
}

gboolean append_to_log_idle(gpointer data) {
    LogData *ldata = (LogData*)data;
    if (!ldata || !ldata->app || !ldata->text) {
        free_log_data(ldata);
        return G_SOURCE_REMOVE;
    }

    if (ldata->app->log_buffer) {
        GtkTextIter iter;
        gtk_text_buffer_get_end_iter(ldata->app->log_buffer, &iter);
        gtk_text_buffer_insert(ldata->app->log_buffer, &iter, ldata->text, -1);
        gtk_text_buffer_insert(ldata->app->log_buffer, &iter, "\n", -1);

        // Auto-scroll al final
        if (ldata->app->log_text_view) {
            gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(ldata->app->log_text_view),
                                         &iter, 0.0, FALSE, 0.0, 1.0);
        }
    }

    free_log_data(ldata);
    return G_SOURCE_REMOVE;
}

gboolean clear_log_idle(gpointer app_ptr) {
    InstallerApp *app = (InstallerApp*)app_ptr;
    if (app && app->log_buffer) {
        gtk_text_buffer_set_text(app->log_buffer, "", -1);
    }
    return G_SOURCE_REMOVE;
}

/* ==================== CALLBACKS ==================== */

gboolean update_navigation_buttons(InstallerApp *app) {
    if (!app || !app->notebook) return G_SOURCE_REMOVE;

    int page = app->last_page;

    // Botones Previous/Next/Install deben existir
    if (!app->prev_btn || !app->next_btn || !app->install_btn) {
        printf("ERROR: Navigation buttons not initialized!\n");
        return G_SOURCE_REMOVE;
    }

    // Usar constantes definidas
    gboolean show_prev = (page > TAB_REGIONAL && page <= TAB_USER);
    gboolean show_next = (page >= TAB_REGIONAL && page < TAB_USER);
    gboolean show_install = (page == TAB_USER && !app->config.installation_started);


    // Botón Previous
    if (app->prev_btn) {
        gtk_widget_set_visible(app->prev_btn, show_prev);
        gtk_widget_set_sensitive(app->prev_btn, show_prev);
    }

    // Botón Next
    if (app->next_btn) {
        gtk_widget_set_visible(app->next_btn, show_next);
        gtk_widget_set_sensitive(app->next_btn, show_next);
    }

    // Botón Install
    if (app->install_btn) {
        gtk_widget_set_visible(app->install_btn, show_install);
        gtk_widget_set_sensitive(app->install_btn, show_install);
    }

    // Botón Finish - solo visible en TAB_PROGRESS y si está completo
    if (app->finish_btn) {
        gboolean show_finish = (page == TAB_PROGRESS && app->config.installation_complete);
        gtk_widget_set_visible(app->finish_btn, show_finish);
        gtk_widget_set_sensitive(app->finish_btn, show_finish);
    }

    // Actualizar status label
    if (app->status_label) {
        const char *status_text = "";
        switch (page) {
            case TAB_REGIONAL:
                status_text = _("Regional settings");
                break;
            case TAB_PARTITIONING:
                status_text = _("Disk partitioning");
                break;
            case TAB_USER:
                status_text = _("User configuration");
                break;
            case TAB_PROGRESS:
                if (app->config.installation_started) {
                    status_text = _("Installation in progress...");
                } else if (app->config.installation_complete) {
                    status_text = _("Installation complete!");
                } else {
                    status_text = _("Ready to install");
                }
                break;
        }
        gtk_label_set_text(GTK_LABEL(app->status_label), status_text);
    }

    return G_SOURCE_REMOVE;
}

void on_page_switched(GtkNotebook *notebook, GtkWidget *page_widget, guint page_num, InstallerApp *app) {
    (void)notebook;
    (void)page_widget;

    int current_page = page_num;

    app->last_page = current_page;

    update_navigation_buttons(app);
}

void on_previous_clicked(GtkButton *btn, InstallerApp *app) {
    (void)btn;
    int page = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook));
    if (page > TAB_REGIONAL) {
        gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), page - 1);
    }
}

void on_next_clicked(GtkButton *btn, InstallerApp *app) {
    (void)btn;
    int page = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook));

    // Si estamos en la pestaña de particionado (TAB_PARTITIONING = 1)
    if (page == TAB_PARTITIONING) {
        // Primero verificar si estamos en modo manual
        bool is_manual_mode = gtk_toggle_button_get_active(
            GTK_TOGGLE_BUTTON(app->manual_radio));

        if (is_manual_mode) {
            // === VALIDACIÓN DE PARTICIONES MANUALES ===

            // Verificar root partition por índice
            int root_index = gtk_combo_box_get_active(GTK_COMBO_BOX(app->root_combo));
            if (root_index <= 0) { // 0 o menos significa "(None)" o no seleccionado
                GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                                           GTK_DIALOG_MODAL,
                                                           GTK_MESSAGE_ERROR,
                                                           GTK_BUTTONS_OK,
                                                           _("Please select a partition for root (/)"));
                gtk_dialog_run(GTK_DIALOG(dialog));
                gtk_widget_destroy(dialog);
                return;
            }

            // Validar EFI en modo UEFI por índice
            if (app->config.uefi_mode) {
                int efi_index = gtk_combo_box_get_active(GTK_COMBO_BOX(app->efi_combo));
                if (efi_index <= 0) {
                    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                                               GTK_DIALOG_MODAL,
                                                               GTK_MESSAGE_ERROR,
                                                               GTK_BUTTONS_OK,
                                                               _("UEFI mode detected. Please select a partition for EFI"));
                    gtk_dialog_run(GTK_DIALOG(dialog));
                    gtk_widget_destroy(dialog);
                    return;
                }
            }

            // Verificar si está marcado separate /home
            bool separate_home = gtk_toggle_button_get_active(
                GTK_TOGGLE_BUTTON(app->separate_home_check_manual));

            if (separate_home) {
                int home_index = gtk_combo_box_get_active(GTK_COMBO_BOX(app->home_combo));
                if (home_index <= 0) {
                    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                                               GTK_DIALOG_MODAL,
                                                               GTK_MESSAGE_ERROR,
                                                               GTK_BUTTONS_OK,
                                                               _("You selected separate /home but didn't select a partition for it"));
                    gtk_dialog_run(GTK_DIALOG(dialog));
                    gtk_widget_destroy(dialog);
                    return;
                }
            }

            // Verificar si está marcado separate /boot
            bool separate_boot = gtk_toggle_button_get_active(
                GTK_TOGGLE_BUTTON(app->separate_boot_check));

            if (separate_boot) {
                int boot_index = gtk_combo_box_get_active(GTK_COMBO_BOX(app->boot_combo));
                if (boot_index <= 0) {
                    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                                               GTK_DIALOG_MODAL,
                                                               GTK_MESSAGE_ERROR,
                                                               GTK_BUTTONS_OK,
                                                               _("You selected separate /boot but didn't select a partition for it"));
                    gtk_dialog_run(GTK_DIALOG(dialog));
                    gtk_widget_destroy(dialog);
                    return;
                }
            }

            // Verificar si está marcado add swap
            bool add_swap = gtk_toggle_button_get_active(
                GTK_TOGGLE_BUTTON(app->add_swap_check_manual));

            if (add_swap) {
                int swap_index = gtk_combo_box_get_active(GTK_COMBO_BOX(app->swap_combo));
                if (swap_index <= 0) {
                    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                                               GTK_DIALOG_MODAL,
                                                               GTK_MESSAGE_ERROR,
                                                               GTK_BUTTONS_OK,
                                                               _("You selected to add swap but didn't select a partition for it"));
                    gtk_dialog_run(GTK_DIALOG(dialog));
                    gtk_widget_destroy(dialog);
                    return;
                }
            }
            // ====== GUARDAR CONFIGURACIÓN TEMPORAL ======
            // Solo guardar si tenemos índices válidos
            if (root_index > 0) {
                const char *root_text = gtk_combo_box_text_get_active_text(
                    GTK_COMBO_BOX_TEXT(app->root_combo));
                if (root_text) {
                    extract_device(root_text, app->config.root_partition,
                                   sizeof(app->config.root_partition));
                }
            }

            // EFI partition (si es UEFI)
            if (app->config.uefi_mode) {
                int efi_index = gtk_combo_box_get_active(GTK_COMBO_BOX(app->efi_combo));
                if (efi_index > 0) {
                    const char *efi_text = gtk_combo_box_text_get_active_text(
                        GTK_COMBO_BOX_TEXT(app->efi_combo));
                    if (efi_text) {
                        extract_device(efi_text, app->config.efi_partition,
                                       sizeof(app->config.efi_partition));
                    }
                }
            }

            // Home partition (si está marcado)
            app->config.separate_home = separate_home;
            if (separate_home) {
                int home_index = gtk_combo_box_get_active(GTK_COMBO_BOX(app->home_combo));
                if (home_index > 0) {
                    const char *home_text = gtk_combo_box_text_get_active_text(
                        GTK_COMBO_BOX_TEXT(app->home_combo));
                    if (home_text) {
                        extract_device(home_text, app->config.home_partition,
                                       sizeof(app->config.home_partition));
                    }
                }
            } else {
                app->config.home_partition[0] = '\0';
            }

            // Boot partition (si está marcado)
            app->config.separate_boot = separate_boot;
            if (separate_boot) {
                int boot_index = gtk_combo_box_get_active(GTK_COMBO_BOX(app->boot_combo));
                if (boot_index > 0) {
                    const char *boot_text = gtk_combo_box_text_get_active_text(
                        GTK_COMBO_BOX_TEXT(app->boot_combo));
                    if (boot_text) {
                        extract_device(boot_text, app->config.boot_partition,
                                       sizeof(app->config.boot_partition));
                    }
                }
            } else {
                app->config.boot_partition[0] = '\0';
            }

            // Swap partition (si está marcado)
            app->config.add_swap = add_swap;
            if (add_swap) {
                int swap_index = gtk_combo_box_get_active(GTK_COMBO_BOX(app->swap_combo));
                if (swap_index > 0) {
                    const char *swap_text = gtk_combo_box_text_get_active_text(
                        GTK_COMBO_BOX_TEXT(app->swap_combo));
                    char swap_device[64];
                    if (swap_text && extract_device(swap_text, swap_device, sizeof(swap_device)) &&
                        strlen(swap_device) > 0) {
                        g_strlcpy(app->config.swap_partition, swap_device,
                                  sizeof(app->config.swap_partition));
                        } else {
                            app->config.swap_partition[0] = '\0';
                        }
                }
            } else {
                app->config.swap_partition[0] = '\0';
            }

            // Marcar que estamos en modo manual
            app->config.auto_partition = false;

            printf("DEBUG: Manual partitions validated and saved:\n");
            printf("  Root: %s\n", app->config.root_partition);
            printf("  Separate /home: %s\n", app->config.separate_home ? "Yes" : "No");
            if (app->config.separate_home) {
                printf("  Home: %s\n", app->config.home_partition);
            }
            printf("  Separate /boot: %s\n", app->config.separate_boot ? "Yes" : "No");
            if (app->config.separate_boot) {
                printf("  Boot: %s\n", app->config.boot_partition);
            }
            printf("  Swap enabled: %s\n", app->config.add_swap ? "Yes" : "No");
            if (app->config.add_swap) {
                printf("  Swap: %s\n", app->config.swap_partition);
            }
            if (app->config.uefi_mode) {
                printf("  EFI: %s\n", app->config.efi_partition);
            }
        }else {
            // === MODO AUTOMÁTICO ===
            const char *device_id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(app->disk_combo));
            printf("DEBUG: Got device ID directly from combo: '%s'\n", device_id);
            g_strlcpy(app->config.disk_device, device_id, sizeof(app->config.disk_device));

            // Guardar otras opciones del modo automático
            app->config.add_swap = gtk_toggle_button_get_active(
                GTK_TOGGLE_BUTTON(app->add_swap_check));
            app->config.create_swapfile = gtk_toggle_button_get_active(
                GTK_TOGGLE_BUTTON(app->swap_file_radio));
            app->config.swap_size_mb = gtk_spin_button_get_value_as_int(
                GTK_SPIN_BUTTON(app->swap_spin));

            // Marcar que estamos en modo automático
            app->config.auto_partition = true;

            printf("DEBUG: Auto partition configuration saved:\n");
            printf("  Disk: %s\n", app->config.disk_device);
            printf("  Swap enabled: %s\n", app->config.add_swap ? "Yes" : "No");
            if (app->config.add_swap) {
                printf("  Swap type: %s\n", app->config.create_swapfile ? "File" : "Partition");
                printf("  Swap size: %dMB\n", app->config.swap_size_mb);
            }
        }
    }

    // Avanzar a la siguiente pestaña si pasa todas las validaciones
    if (page < TAB_USER) {
        gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), page + 1);
    }

    // Actualizar botones de navegación
    update_navigation_buttons(app);
}

void on_install_clicked(GtkButton *btn, InstallerApp *app) {
    (void)btn;

    // Validaciones básicas
    const char *username_raw = gtk_entry_get_text(GTK_ENTRY(app->username_entry));
    const char *realname_raw = gtk_entry_get_text(GTK_ENTRY(app->realname_entry));
    const char *hostname_raw = gtk_entry_get_text(GTK_ENTRY(app->hostname_entry));
    const char *password_raw = gtk_entry_get_text(GTK_ENTRY(app->password_entry));
    const char *confirm_raw = gtk_entry_get_text(GTK_ENTRY(app->password_confirm_entry));

    // Convertir a UTF-8 válido
    gchar *username = g_utf8_make_valid(username_raw, -1);
    gchar *realname = g_utf8_make_valid(realname_raw, -1);
    gchar *hostname = g_utf8_make_valid(hostname_raw, -1);
    gchar *password = g_utf8_make_valid(password_raw, -1);
    gchar *confirm = g_utf8_make_valid(confirm_raw, -1);

    // Validar campos requeridos
    if (!username || strlen(username) < 2) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                                   GTK_DIALOG_MODAL,
                                                   GTK_MESSAGE_ERROR,
                                                   GTK_BUTTONS_OK,
                                                   _("Please enter a valid username (minimum 2 characters)"));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);

        g_free(username); g_free(realname); g_free(hostname);
        g_free(password); g_free(confirm);
        return;
    }

    if (!password || strlen(password) < 1) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                                   GTK_DIALOG_MODAL,
                                                   GTK_MESSAGE_ERROR,
                                                   GTK_BUTTONS_OK,
                                                   _("Please enter a password"));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);

        g_free(username); g_free(realname); g_free(hostname);
        g_free(password); g_free(confirm);
        return;
    }

    if (strcmp(password, confirm) != 0) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                                   GTK_DIALOG_MODAL,
                                                   GTK_MESSAGE_ERROR,
                                                   GTK_BUTTONS_OK,
                                                   _("Passwords do not match"));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);

        g_free(username); g_free(realname); g_free(hostname);
        g_free(password); g_free(confirm);
        return;
    }

    // Validaciones adicionales
    if (!is_valid_username(username)) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                                   GTK_DIALOG_MODAL,
                                                   GTK_MESSAGE_ERROR,
                                                   GTK_BUTTONS_OK,
                                                   _("Invalid username. Use 2-32 characters, "
                                                   "letters, numbers, underscores and hyphens only."));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);

        g_free(username); g_free(realname); g_free(hostname);
        g_free(password); g_free(confirm);
        return;
    }

    if (!is_valid_hostname(hostname)) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                                   GTK_DIALOG_MODAL,
                                                   GTK_MESSAGE_ERROR,
                                                   GTK_BUTTONS_OK,
                                                   _("Invalid hostname. Use 1-63 characters, "
                                                   "letters, numbers and hyphens only."));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);

        g_free(username); g_free(realname); g_free(hostname);
        g_free(password); g_free(confirm);
        return;
    }

    if (!is_valid_password(password)) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                                   GTK_DIALOG_MODAL,
                                                   GTK_MESSAGE_ERROR,
                                                   GTK_BUTTONS_OK,
                                                   _("Invalid password. Use at least 1 character"));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);

        g_free(username); g_free(realname); g_free(hostname);
        g_free(password); g_free(confirm);
        return;
    }

    // ====== VALIDACIÓN DE CONTRASEÑA DE ROOT ======
    app->config.same_root_password = gtk_toggle_button_get_active(
        GTK_TOGGLE_BUTTON(app->root_password_check));

    // ====== VALIDACIÓN DE CONTRASEÑA DE ROOT ======
    app->config.same_root_password = gtk_toggle_button_get_active(
        GTK_TOGGLE_BUTTON(app->root_password_check));

    // Inicializar root_password para evitar problemas
    app->config.root_password[0] = '\0';

    if (!app->config.same_root_password) {
        // Validar contraseña de root
        const char *root_password_raw = gtk_entry_get_text(GTK_ENTRY(app->root_password_entry));
        const char *root_confirm_raw = gtk_entry_get_text(GTK_ENTRY(app->root_password_confirm_entry));

        gchar *root_password = g_utf8_make_valid(root_password_raw, -1);
        gchar *root_confirm = g_utf8_make_valid(root_confirm_raw, -1);

        if (!root_password || strlen(root_password) < 1) {
            GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                                       GTK_DIALOG_MODAL,
                                                       GTK_MESSAGE_ERROR,
                                                       GTK_BUTTONS_OK,
                                                       _("Please enter a password for root"));
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);

            g_free(username); g_free(realname); g_free(hostname);
            g_free(password); g_free(confirm);
            g_free(root_password); g_free(root_confirm);
            return;
        }

        if (strcmp(root_password, root_confirm) != 0) {
            GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                                       GTK_DIALOG_MODAL,
                                                       GTK_MESSAGE_ERROR,
                                                       GTK_BUTTONS_OK,
                                                       _("Root passwords do not match"));
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);

            g_free(username); g_free(realname); g_free(hostname);
            g_free(password); g_free(confirm);
            g_free(root_password); g_free(root_confirm);
            return;
        }

        if (!is_valid_password(root_password)) {
            GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                                       GTK_DIALOG_MODAL,
                                                       GTK_MESSAGE_ERROR,
                                                       GTK_BUTTONS_OK,
                                                       _("Invalid root password. Use at least 1 character"));
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);

            g_free(username); g_free(realname); g_free(hostname);
            g_free(password); g_free(confirm);
            g_free(root_password); g_free(root_confirm);
            return;
        }

        // Guardar contraseña de root
        g_strlcpy(app->config.root_password, root_password, sizeof(app->config.root_password));

        g_free(root_password);
        g_free(root_confirm);

        printf("DEBUG: Root password set to: %s\n", app->config.root_password);
    } else {
        // Usar misma contraseña que usuario
        printf("DEBUG: Using same password for root as user\n");
        g_strlcpy(app->config.root_password, password, sizeof(app->config.root_password));
    }


    // Guardar configuración básica
    g_strlcpy(app->config.username, username, sizeof(app->config.username));
    g_strlcpy(app->config.realname, realname ? realname : username, sizeof(app->config.realname));
    g_strlcpy(app->config.hostname, hostname ? hostname : "loc-os-pc", sizeof(app->config.hostname));
    g_strlcpy(app->config.password, password, sizeof(app->config.password));

    // Guardar configuración adicional
    app->config.autologin = gtk_toggle_button_get_active(
        GTK_TOGGLE_BUTTON(app->autologin_check));

    // ====== CONFIGURACIÓN DE PARTICIONADO ======
    app->config.auto_partition = gtk_toggle_button_get_active(
        GTK_TOGGLE_BUTTON(app->auto_radio));

    // ====== VALIDACIÓN DE PARTICIÓN MANUAL ======
    if (!app->config.auto_partition) {

        if (strlen(app->config.root_partition) == 0) {
            GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                                       GTK_DIALOG_MODAL,
                                                       GTK_MESSAGE_ERROR,
                                                       GTK_BUTTONS_OK,
                                                       _("Root partition not configured. Please go back to partition tab."));
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            return;
        }

        if (app->config.uefi_mode && strlen(app->config.efi_partition) == 0) {
            GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                                       GTK_DIALOG_MODAL,
                                                       GTK_MESSAGE_ERROR,
                                                       GTK_BUTTONS_OK,
                                                       _("EFI partition not configured for UEFI mode. Please go back to partition tab."));
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            return;
        }

        // Las demás validaciones ya se hicieron en on_next_clicked
        // Solo mostramos debug para confirmar
        printf("DEBUG: Using saved manual partition configuration:\n");
        printf("  Root: %s\n", app->config.root_partition);
        printf("  Separate /home: %s\n", app->config.separate_home ? "Yes" : "No");
        if (app->config.separate_home) {
            printf("  Home: %s\n", app->config.home_partition);
        }
        printf("  Separate /boot: %s\n", app->config.separate_boot ? "Yes" : "No");
        if (app->config.separate_boot) {
            printf("  Boot: %s\n", app->config.boot_partition);
        }
        printf("  Swap enabled: %s\n", app->config.add_swap ? "Yes" : "No");
        if (app->config.add_swap) {
            printf("  Swap: %s\n", app->config.swap_partition);
        }
        if (app->config.uefi_mode) {
            printf("  EFI: %s\n", app->config.efi_partition);
        }
    }

    // Obtener zona horaria
    char *selected_timezone = get_selected_timezone(app);
    if (selected_timezone) {
        g_strlcpy(app->config.timezone, selected_timezone, sizeof(app->config.timezone));
        free(selected_timezone);
    } else {
        g_strlcpy(app->config.timezone, "UTC", sizeof(app->config.timezone));
    }

    // Obtener teclado
    const char *keyboard_text_raw = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->keyboard_combo));
    if (keyboard_text_raw) {
        char *keyboard_code = extract_code(keyboard_text_raw);
        if (keyboard_code) {
            g_strlcpy(app->config.keyboard, keyboard_code, sizeof(app->config.keyboard));
            free(keyboard_code);
        } else {
            g_strlcpy(app->config.keyboard, "us", sizeof(app->config.keyboard));
        }
    }

    // Obtener idioma
    const char *language_text_raw = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->language_combo));
    if (language_text_raw) {
        char *language_code = extract_code(language_text_raw);
        if (language_code) {
            g_strlcpy(app->config.language, language_code, sizeof(app->config.language));
            free(language_code);
        } else {
            g_strlcpy(app->config.language, "en_US", sizeof(app->config.language));
        }
    }

    // ====== CONFIGURACIÓN DE MODO AUTOMÁTICO ======
    if (app->config.auto_partition) {
        // === MODO AUTOMÁTICO ===
        const char *disk_text_raw = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->disk_combo));
        if (disk_text_raw) {
            // Extraer solo el dispositivo
            char disk_device[128] = "";
            const char *separator = strchr(disk_text_raw, '|');
            if (!separator) separator = strstr(disk_text_raw, " - ");

            if (separator) {
                int len = separator - disk_text_raw;
                strncpy(disk_device, disk_text_raw, len);
                disk_device[len] = '\0';
            } else {
                g_strlcpy(disk_device, disk_text_raw, sizeof(disk_device));
            }

            // Trim espacios
            char *end = disk_device + strlen(disk_device) - 1;
            while (end > disk_device && isspace((unsigned char)*end)) {
                *end = '\0';
                end--;
            }

            g_strlcpy(app->config.disk_device, disk_device, sizeof(app->config.disk_device));
        }

        app->config.separate_home = gtk_toggle_button_get_active(
            GTK_TOGGLE_BUTTON(app->separate_home_check));
        app->config.add_swap = gtk_toggle_button_get_active(
            GTK_TOGGLE_BUTTON(app->add_swap_check));
        app->config.create_swapfile = gtk_toggle_button_get_active(
            GTK_TOGGLE_BUTTON(app->swap_file_radio));
        app->config.swap_size_mb = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->swap_spin));
    }

    printf("DEBUG: Final configuration:\n");
    printf("  Mode: %s\n", app->config.auto_partition ? "Auto" : "Manual");
    printf("  Username: %s\n", app->config.username);
    printf("  Hostname: %s\n", app->config.hostname);
    printf("  Timezone: %s\n", app->config.timezone);
    printf("  Keyboard: %s\n", app->config.keyboard);
    printf("  Variant Keyboard: %s\n", app->config.keyboard_variant);
    printf("  Language: %s\n", app->config.language);
    printf("  Same root password: %s\n", app->config.same_root_password ? "Yes" : "No");

    if (app->config.auto_partition) {
        printf("  Disk: %s\n", app->config.disk_device);
        printf("  Separate /home: %s\n", app->config.separate_home ? "Yes" : "No");
        printf("  Swap enabled: %s\n", app->config.add_swap ? "Yes" : "No");
        if (app->config.add_swap) {
            printf("  Swap type: %s\n", app->config.create_swapfile ? "File" : "Partition");
            printf("  Swap size: %dMB\n", app->config.swap_size_mb);
        }
    } else {
        printf("  Root partition: %s\n", app->config.root_partition);
        printf("  Separate /home: %s\n", app->config.separate_home ? "Yes" : "No");
        if (app->config.separate_home) {
            printf("  Home partition: %s\n", app->config.home_partition);
        }
        printf("  Separate /boot: %s\n", app->config.separate_boot ? "Yes" : "No");
        if (app->config.separate_boot) {
            printf("  Boot partition: %s\n", app->config.boot_partition);
        }
        printf("  Swap enabled: %s\n", app->config.add_swap ? "Yes" : "No");
        if (app->config.add_swap) {
            printf("  Swap partition: %s\n", app->config.swap_partition);
        }
        if (app->config.uefi_mode) {
            printf("  EFI partition: %s\n", app->config.efi_partition);
        }
    }

    // Liberar strings temporales
    g_free(username);
    g_free(realname);
    g_free(hostname);
    g_free(password);
    g_free(confirm);

    // DIÁLOGO DE CONFIRMACIÓN
    GtkWidget *dialog;
    if (app->config.auto_partition) {
        dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                        GTK_DIALOG_MODAL,
                                        GTK_MESSAGE_QUESTION,
                                        GTK_BUTTONS_YES_NO,
                                        _("This will erase all data on %s and install LOC-OS 24.\n\n"
                                        "Do you want to continue?"),
                                        app->config.disk_device);
    } else {
        dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                        GTK_DIALOG_MODAL,
                                        GTK_MESSAGE_QUESTION,
                                        GTK_BUTTONS_YES_NO,
                                        _("This will install LOC-OS 24 using the selected partitions.\n\n"
                                        "Do you want to continue?"));
    }

    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (response != GTK_RESPONSE_YES) {
        printf("DEBUG: User cancelled installation\n");
        return;
    }

    printf("DEBUG: User confirmed installation\n");

    // Ir a pestaña de progreso
    gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), TAB_PROGRESS);

    // Habilitar la pestaña de progreso
    gtk_widget_set_sensitive(gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), TAB_PROGRESS), TRUE);

    // Limpiar log anterior
    if (app->log_buffer) {
        gtk_text_buffer_set_text(app->log_buffer, "", -1);
    }

    // Actualizar UI
    update_navigation_buttons(app);

    // Iniciar instalación en segundo plano
    start_installation(app);
}

void on_finish_clicked(GtkButton *btn, InstallerApp *app) {
    (void)btn;

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        _("Installation Complete"),
                                                    GTK_WINDOW(app->window),
                                                    GTK_DIALOG_MODAL,
                                                    _("Close"), GTK_RESPONSE_CLOSE,
                                                    _("Reboot"), GTK_RESPONSE_ACCEPT,
                                                    NULL
    );

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    GtkWidget *message = gtk_label_new(_("Installation completed successfully!"));
    gtk_widget_set_margin_start(message, 20);
    gtk_widget_set_margin_end(message, 20);
    gtk_widget_set_margin_top(message, 20);
    gtk_widget_set_margin_bottom(message, 20);

    gtk_box_pack_start(GTK_BOX(content), message, TRUE, TRUE, 0);

    gtk_widget_show_all(dialog);

    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (response == GTK_RESPONSE_ACCEPT) {
        sync();
        system("sudo reboot");
    }

    gtk_main_quit();
}

void on_region_changed(GtkComboBox *combo, InstallerApp *app) {
    int region_index = gtk_combo_box_get_active(combo);

    if (region_index < 0 || region_index >= app->region_count) return;

    // Limpiar combo de ciudades
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(app->city_combo));

    // Llenar ciudades para la región seleccionada
    char **cities = app->timezone_regions[region_index];

    if (cities[0]) {
        for (int i = 0; cities[i] != NULL; i++) {
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->city_combo), cities[i]);
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(app->city_combo), 0);
    }
}

// Callback cuando cambia el layout principal
void on_keyboard_layout_changed(GtkComboBox *combo, InstallerApp *app) {
    (void)combo;
    const char *selected_text = gtk_combo_box_text_get_active_text(
        GTK_COMBO_BOX_TEXT(app->keyboard_combo));

    if (!selected_text) return;

    // Extraer código del layout
    char *layout_code = extract_code(selected_text);
    if (!layout_code) return;

    printf("DEBUG: Keyboard layout changed to: %s\n", layout_code);

    // Actualizar las variantes disponibles
    update_keyboard_variants(app, layout_code);

    free(layout_code);
}

// Callback cuando cambia la variante
void on_keyboard_variant_changed(GtkComboBox *combo, InstallerApp *app) {
    (void)combo;
    const char *selected_text = gtk_combo_box_text_get_active_text(
        GTK_COMBO_BOX_TEXT(app->keyboard_variant_combo));

    if (!selected_text) return;

    // Extraer código de la variante
    char *variant_code = extract_code(selected_text);
    if (variant_code) {
        strncpy(app->config.keyboard_variant, variant_code,
                sizeof(app->config.keyboard_variant) - 1);
        app->config.keyboard_variant[sizeof(app->config.keyboard_variant) - 1] = '\0';
        printf("DEBUG: Keyboard variant changed to: %s\n", app->config.keyboard_variant);
        free(variant_code);
    }
}

// Función para actualizar variantes basadas en el layout
void update_keyboard_variants(InstallerApp *app, const char *layout_code) {
    if (!app || !layout_code || !app->keyboard_variant_combo) return;

    printf("DEBUG: Updating variants for layout: %s\n", layout_code);

    // Limpiar el combo de variantes
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(app->keyboard_variant_combo));

    // SIEMPRE agregar "default" como primera opción
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->keyboard_variant_combo),
                                   "default - Default (no variant)");

    // Obtener variantes para este layout desde el script
    char command[256];
    snprintf(command, sizeof(command), "%s variants %s", SYSINFO_SCRIPT, layout_code);

    int variant_count;
    char **variants = get_system_list(command, &variant_count);

    if (variants && variant_count > 0) {
        printf("DEBUG: Found %d variants for layout %s\n", variant_count, layout_code);

        // Añadir todas las variantes obtenidas
        for (int i = 0; i < variant_count; i++) {
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->keyboard_variant_combo),
                                           variants[i]);
        }

        free_string_array(variants, variant_count);
    } else {
        printf("DEBUG: No variants found for layout %s (only default available)\n", layout_code);
    }

    // Seleccionar "default" por defecto
    gtk_combo_box_set_active(GTK_COMBO_BOX(app->keyboard_variant_combo), 0);
    strncpy(app->config.keyboard_variant, "default", sizeof(app->config.keyboard_variant));
}

/* Partitioning things */

void on_disk_changed(GtkComboBox *combo, InstallerApp *app) {
    const char *device_id = gtk_combo_box_get_active_id(combo);

    if (device_id && strlen(device_id) > 0) {
        printf("DEBUG: Disk selected (from ID): '%s'\n", device_id);
        g_strlcpy(app->config.disk_device, device_id, sizeof(app->config.disk_device));
    } else {
        app->config.disk_device[0] = '\0';
        printf("DEBUG: No disk selected or no ID found\n");
    }
}

void on_partition_mode_toggled(GtkToggleButton *btn, InstallerApp *app) {
    (void)btn;
    bool manual = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->manual_radio));

    printf("DEBUG: Switching to %s mode\n", manual ? "manual" : "auto");

    // Cambiar entre pestañas del notebook interno
    if (app->partition_notebook) {
        gtk_notebook_set_current_page(GTK_NOTEBOOK(app->partition_notebook),
                                      manual ? 1 : 0);
    }

    app->config.auto_partition = !manual;
}

void on_swap_type_toggled(GtkToggleButton *btn, InstallerApp *app) {
    (void)btn;
    app->config.create_swapfile = gtk_toggle_button_get_active(
        GTK_TOGGLE_BUTTON(app->swap_file_radio));

    printf("DEBUG: Swap type changed: %s\n",
           app->config.create_swapfile ? "Swap file" : "Swap partition");
}

void on_add_swap_toggled(GtkToggleButton *btn, InstallerApp *app) {
    bool active = gtk_toggle_button_get_active(btn);

    if (active) {
        gtk_widget_show_all(app->swap_options_container);
    } else {
        gtk_widget_hide(app->swap_options_container);
    }

    app->config.add_swap = active;
    printf("DEBUG: Swap enabled: %s\n", active ? "Yes" : "No");
}

void on_separate_home_manual_toggled(GtkToggleButton *btn, InstallerApp *app) {
    bool active = gtk_toggle_button_get_active(btn);
    printf("DEBUG: Separate /home toggled: %s\n", active ? "Yes" : "No");

    if (active) {
        populate_partition_combo(GTK_COMBO_BOX_TEXT(app->home_combo), app);
        gtk_widget_show_all(app->home_combo_container); // Cambiar a show_all
    } else {
        gtk_widget_hide(app->home_combo_container);
    }

    app->config.separate_home = active;
}

void on_separate_boot_toggled(GtkToggleButton *btn, InstallerApp *app) {
    bool active = gtk_toggle_button_get_active(btn);
    printf("DEBUG: Separate /boot toggled: %s\n", active ? "Yes" : "No");

    if (active) {
        populate_partition_combo(GTK_COMBO_BOX_TEXT(app->boot_combo), app);
        gtk_widget_show_all(app->boot_combo_container); // Cambiar a show_all
    } else {
        gtk_widget_hide(app->boot_combo_container);
    }

    app->config.separate_boot = active;
}

void on_add_swap_manual_toggled(GtkToggleButton *btn, InstallerApp *app) {
    bool active = gtk_toggle_button_get_active(btn);
    printf("DEBUG: Add swap partition toggled: %s\n", active ? "Yes" : "No");

    if (active) {
        populate_partition_combo(GTK_COMBO_BOX_TEXT(app->swap_combo), app);
        gtk_widget_show_all(app->swap_combo_container);
    } else {
        gtk_widget_hide(app->swap_combo_container);
    }

    app->config.add_swap = active;
}

void on_open_gparted(GtkButton *btn, InstallerApp *app) {
    (void)btn;

    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                               GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_INFO,
                                               GTK_BUTTONS_OK,
                                               _("GParted will open in a new window. "
                                               "Close it when done to refresh the partition list."));
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    pid_t pid = fork();
    if (pid == 0) {
        execlp("gparted", "gparted", NULL);
        exit(1);
    } else if (pid > 0) {
        waitpid(pid, NULL, 0);
        refresh_partition_combos(app);

        dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                        GTK_DIALOG_MODAL,
                                        GTK_MESSAGE_INFO,
                                        GTK_BUTTONS_OK,
                                        _("Partition list refreshed."));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
}

/* User thing */

void on_autologin_toggled(GtkToggleButton *btn, InstallerApp *app) {
    app->config.autologin = gtk_toggle_button_get_active(btn);
}

void on_root_password_check_toggled(GtkToggleButton *btn, InstallerApp *app) {
    bool active = gtk_toggle_button_get_active(btn);

    if (active) {
        // Usar misma contraseña: ocultar campos de root
        gtk_widget_hide(app->root_password_container);
        // Limpiar los campos
        gtk_entry_set_text(GTK_ENTRY(app->root_password_entry), "");
        gtk_entry_set_text(GTK_ENTRY(app->root_password_confirm_entry), "");
    } else {
        // Usar contraseña separada: mostrar campos de root
        gtk_widget_show_all(app->root_password_container);
    }

    app->config.same_root_password = active;
}

void on_window_destroy(GtkWidget *widget, InstallerApp *app) {
    (void)widget;
    if (app->thread_running) {
        pthread_cancel(app->install_thread);
        pthread_join(app->install_thread, NULL);
    }

    // Liberar zonas horarias
    free_timezones_hierarchical(app);

    pthread_mutex_destroy(&app->mutex);
    free(app);
    gtk_main_quit();
}
