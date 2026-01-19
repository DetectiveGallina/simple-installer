#ifndef INSTALLER_H
#define INSTALLER_H

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>
#include <locale.h>
#include <ctype.h>

/* ==================== PATHS ==================== */
#define SCRIPTS_DIR     "/usr/share/loc-installer/scripts/"
#define SYSINFO_SCRIPT  SCRIPTS_DIR "get-system-info.sh"
#define CORE_INSTALLER  SCRIPTS_DIR "core-installer.sh"

/* ==================== CONSTANTS ==================== */
#define TAB_REGIONAL     0
#define TAB_PARTITIONING 1
#define TAB_USER         2
#define TAB_PROGRESS     3

/* ==================== STRUCTURES ==================== */

typedef struct {
    char language[32];
    char timezone[64];
    char keyboard[32];
    char keyboard_variant[32];

    char disk_device[64];
    bool uefi_mode;
    bool auto_partition;
    bool separate_home;
    bool separate_boot;
    bool add_swap;
    bool create_swapfile;
    int swap_size_mb;

    char username[32];
    char realname[64];
    char hostname[64];
    char password[64];
    char root_password[64];
    bool same_root_password;
    bool autologin;

    bool installation_started;
    bool installation_complete;

    // Manual partition fields
    char root_partition[64];
    char home_partition[64];
    char boot_partition[64];
    char swap_partition[64];
    char efi_partition[64];
} InstallConfig;

typedef struct {
    GtkWidget *window;
    GtkWidget *notebook;
    GtkWidget *progress_bar;
    GtkWidget *status_label;

    /* Navigation buttons */
    GtkWidget *prev_btn;
    GtkWidget *next_btn;
    GtkWidget *install_btn;
    GtkWidget *finish_btn;

    /* Regional */
    GtkWidget *region_combo;
    GtkWidget *city_combo;
    GtkWidget *language_combo;
    GtkWidget *timezone_combo;
    GtkWidget *keyboard_combo;
    GtkWidget *keyboard_variant_combo;

    /* Partitioning */
    GtkWidget *disk_combo;
    GtkWidget *partition_notebook;
    GtkWidget *auto_radio;
    GtkWidget *manual_radio;
    GtkWidget *swap_options_container;
    GtkWidget *add_swap_check;
    GtkWidget *swap_spin;
    GtkWidget *swap_partition_radio;
    GtkWidget *swap_file_radio;
    GtkWidget *disk_info_label;
    GtkWidget *gparted_hbox;

    /* Manual partition frame */
    GtkWidget *manual_frame;
    GtkWidget *open_gparted_btn;
    GtkWidget *root_combo;
    GtkWidget *separate_home_check;
    GtkWidget *separate_home_check_manual;
    GtkWidget *home_combo;
    GtkWidget *home_combo_container;
    GtkWidget *separate_boot_check;
    GtkWidget *boot_combo;
    GtkWidget *boot_combo_container;
    GtkWidget *swap_combo;
    GtkWidget *swap_combo_container;
    GtkWidget *add_swap_check_manual;
    GtkWidget *efi_combo;

    /* User */
    GtkWidget *username_entry;
    GtkWidget *realname_entry;
    GtkWidget *hostname_entry;
    GtkWidget *password_entry;
    GtkWidget *password_confirm_entry;
    GtkWidget *root_password_check;
    GtkWidget *autologin_check;
    GtkWidget *root_password_container;
    GtkWidget *root_password_entry;
    GtkWidget *root_password_confirm_entry;

    /* Installation */
    GtkWidget *log_text_view;
    GtkTextBuffer *log_buffer;
    GtkWidget *log_scrolled_window;
    GtkWidget *copy_log_btn;
    GtkWidget *progress_label;

    InstallConfig config;
    pthread_t install_thread;
    bool updating_partition_combos;
    bool thread_running;
    pthread_mutex_t mutex;
    char **region_names;
    char ***timezone_regions;
    char *current_keyboard_layout;
    int region_count;
    int last_page;
} InstallerApp;


typedef struct {
    InstallerApp *app;
    char *text;
} LogData;

typedef struct {
    InstallerApp *app;
    char *message;
    int percent;
} ProgressData;

typedef struct {
    char *message;
    InstallerApp *app;
} StatusData;

typedef struct {
    char *message;
    InstallerApp *app;
} ErrorData;

/* ==================== SYSTEM INFO FUNCTIONS ==================== */
char** get_system_list(const char *command, int *count);
char** get_timezones(int *count);
char** get_keyboard_layouts(int *count);
char** get_languages(int *count);
char** get_disks(int *count);
char* get_current_timezone(void);
char* get_current_keyboard(void);
char* get_current_language(void);

/* ==================== UTILITY FUNCTIONS ==================== */
bool is_uefi_boot(void);
bool is_valid_username(const char *username);
bool is_valid_hostname(const char *hostname);
bool is_valid_password(const char *password);
char* run_command(const char *cmd);
char** list_partitions(int *count);
void free_string_array(char **array, int count);
int get_disk_size_gb(const char *device);
bool extract_device(const char *display_text, char *buffer, size_t buffer_size);
int populate_partition_combo(GtkComboBoxText *combo, InstallerApp *app);
void refresh_partition_combos(InstallerApp *app);
int find_combo_item(GtkComboBoxText *combo, const char *search_text);
char* extract_code(const char *text);

/* Timezone functions */
void load_timezones_hierarchical(InstallerApp *app);
void free_timezones_hierarchical(InstallerApp *app);
void set_current_timezone(InstallerApp *app);
char* get_selected_timezone(InstallerApp *app);

void copy_log_to_clipboard(InstallerApp *app);
void append_to_log(InstallerApp *app, const char *text);

LogData* create_log_data(InstallerApp *app, const char *text);
void free_log_data(LogData *data);
gboolean append_to_log_idle(gpointer data);
gboolean clear_log_idle(gpointer app);

/* ==================== CALLBACKS ==================== */
gboolean update_navigation_buttons(InstallerApp *app);
void on_page_switched(GtkNotebook *notebook, GtkWidget *page_widget, guint page_num, InstallerApp *app);
void on_region_changed(GtkComboBox *combo, InstallerApp *app);
void on_disk_changed(GtkComboBox *combo, InstallerApp *app);
void on_partition_mode_toggled(GtkToggleButton *btn, InstallerApp *app);
void on_add_swap_toggled(GtkToggleButton *btn, InstallerApp *app);
void on_swap_type_toggled(GtkToggleButton *btn, InstallerApp *app);
void on_add_swap_manual_toggled(GtkToggleButton *btn, InstallerApp *app);
void on_separate_home_manual_toggled(GtkToggleButton *btn, InstallerApp *app);
void on_separate_boot_toggled(GtkToggleButton *btn, InstallerApp *app);
void on_open_gparted(GtkButton *btn, InstallerApp *app);
void on_autologin_toggled(GtkToggleButton *btn, InstallerApp *app);
void on_root_password_check_toggled(GtkToggleButton *btn, InstallerApp *app);
void on_install_clicked(GtkButton *btn, InstallerApp *app);
void on_finish_clicked(GtkButton *btn, InstallerApp *app);
void on_previous_clicked(GtkButton *btn, InstallerApp *app);
void on_next_clicked(GtkButton *btn, InstallerApp *app);
void on_window_destroy(GtkWidget *widget, InstallerApp *app);
void on_partition_combo_changed(GtkComboBox *combo, InstallerApp *app);

/* ==================== KEYBOARD FUNCTIONS ==================== */
void setup_current_keyboard_in_ui(InstallerApp *app);
void on_keyboard_layout_changed(GtkComboBox *combo, InstallerApp *app);
void on_keyboard_variant_changed(GtkComboBox *combo, InstallerApp *app);
void update_keyboard_variants(InstallerApp *app, const char *layout_code);

/* ==================== UI FUNCTIONS ==================== */
GtkWidget* create_label_with_markup(const char *text);
GtkWidget* create_regional_tab(InstallerApp *app);
GtkWidget* create_partition_tab(InstallerApp *app);
GtkWidget* create_user_tab(InstallerApp *app);
GtkWidget* create_progress_tab(InstallerApp *app);
void create_main_window(InstallerApp *app);
void update_last_log_line(InstallerApp *app, const char *text);

/* ==================== GTK IDLE FUNCTIONS ==================== */
/* Actualiza las declaraciones */
gboolean update_progress_ui(gpointer data);
gboolean update_status_ui(gpointer data);
gboolean show_error_dialog(ErrorData *edata);
gboolean show_success_dialog(InstallerApp *app);
gboolean enable_close_button(gpointer data);
gboolean update_last_log_line_idle(gpointer data);

/* ==================== INSTALLATION FUNCTIONS ==================== */
void start_installation(InstallerApp *app);
void* run_installation_thread(void *data);
void parse_installation_output(const char *line, InstallerApp *app);

/* ==================== ENTRY POINT ==================== */
int installer_run(int argc, char *argv[]);

#endif /* INSTALLER_H */
