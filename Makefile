# Makefile for LOC-OS 24 Installer

CC = gcc
CFLAGS = -Wall -Wextra -O2 `pkg-config --cflags gtk+-3.0`
LIBS = `pkg-config --libs gtk+-3.0` -lpthread
PREFIX = /usr
BINDIR = $(PREFIX)/bin
DATADIR = $(PREFIX)/share/loc-installer
LOCALEDIR = $(PREFIX)/share/locale
POT_FILE = po/loc-installer.pot
DESKTOP_ENTRY_NAME="loc-installer.desktop"

# Source files - TODOS los archivos .c
SRC = src/installer.c src/tools.c src/ui.c src/main.c
OBJ = $(SRC:.c=.o)
TARGET = loc-installer

# Translation files
PO_FILES = $(wildcard po/*.po)
MO_FILES = $(PO_FILES:.po=.mo)

# Reglas principales
all: $(TARGET) translations

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ) $(LIBS)

%.o: %.c src/installer.h
	$(CC) $(CFLAGS) -c $< -o $@

# Reglas para traducciones
translations: $(MO_FILES)

%.mo: %.po
	msgfmt -c -o $@ $<

# Generar archivo POT (plantilla de traducción)
pot: $(POT_FILE)

$(POT_FILE): $(SRC) src/installer.h
	# Extraer cadenas de todos los archivos fuente
	xgettext --from-code=UTF-8 \
	         --keyword=_ \
	         --keyword=N_ \
	         --keyword=gettext \
	         --keyword=ngettext:1,2 \
	         --sort-output \
	         --package-name="LOC-OS 24 Installer" \
	         --package-version="1.0" \
	         --msgid-bugs-address="loc-os-dev@example.com" \
	         -o $(POT_FILE) \
	         $(SRC) src/installer.h
	@echo "POT file generated at: $(POT_FILE)"

# Actualizar archivos PO existentes con nuevas cadenas
update-po: $(POT_FILE)
	@for po in $(PO_FILES); do \
		echo "Updating $$po..."; \
		msgmerge -U $$po $(POT_FILE); \
	done

# Instalación
install: all
	# Create directories
	install -d $(DESTDIR)$(BINDIR)
	install -d $(DESTDIR)$(DATADIR)
	install -d $(DESTDIR)$(LOCALEDIR)
	install -d $(DESTDIR)$(DATADIR)/scripts
	install -d $(DESTDIR)/etc/sudoers.d

	# Install binary
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)

	# Install scripts
	install -m 755 src/scripts/core-installer.sh $(DESTDIR)$(DATADIR)/scripts
	install -m 755 src/scripts/get-system-info.sh $(DESTDIR)$(DATADIR)/scripts

	# Install sudoers file
	install -d $(DESTDIR)/etc/sudoers.d
	echo "ALL ALL=(ALL) NOPASSWD: $(DATADIR)/scripts/core-installer.sh" > $(DESTDIR)/etc/sudoers.d/loc-installer
	chmod 440 $(DESTDIR)/etc/sudoers.d/loc-installer

	# Install translations
	for mo in $(MO_FILES); do \
		lang=$$(basename $$mo .mo); \
		install -d $(DESTDIR)$(LOCALEDIR)/$$lang/LC_MESSAGES; \
		install -m 644 $$mo $(DESTDIR)$(LOCALEDIR)/$$lang/LC_MESSAGES/loc-installer.mo; \
	done

	# Create desktop entry
	install -d $(DESTDIR)$(PREFIX)/share/applications
	echo "[Desktop Entry]" > $(DESTDIR)$(PREFIX)/share/applications/$(DESKTOP_ENTRY_NAME)
	echo "Name=LOC-OS 24 Installer" >> $(DESTDIR)$(PREFIX)/share/applications/$(DESKTOP_ENTRY_NAME)
	echo "Comment=Install LOC-OS 24 to your hard drive" >> $(DESTDIR)$(PREFIX)/share/applications/$(DESKTOP_ENTRY_NAME)
	echo "Exec=loc-installer" >> $(DESTDIR)$(PREFIX)/share/applications/$(DESKTOP_ENTRY_NAME)
	echo "Icon=/usr/share/icons/hicolor/48x48/apps/installer-loc-os.png" >> $(DESTDIR)$(PREFIX)/share/applications/$(DESKTOP_ENTRY_NAME)
	echo "Terminal=false" >> $(DESTDIR)$(PREFIX)/share/applications/$(DESKTOP_ENTRY_NAME)
	echo "Type=Application" >> $(DESTDIR)$(PREFIX)/share/applications/$(DESKTOP_ENTRY_NAME)
	echo "Categories=System;" >> $(DESTDIR)$(PREFIX)/share/applications/$(DESKTOP_ENTRY_NAME)
	echo "NoDisplay=false" >> $(DESTDIR)$(PREFIX)/share/applications/$(DESKTOP_ENTRY_NAME)

	ln -s "$(DESTDIR)$(PREFIX)/share/applications/$(DESKTOP_ENTRY_NAME)" "/home/live/Desktop/$(DESKTOP_ENTRY_NAME)"
	chmod +x "/home/live/Desktop/$(DESKTOP_ENTRY_NAME)"

# Reglas de desinstalación
uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -rf $(DESTDIR)$(DATADIR)
	rm -f $(DESTDIR)$(PREFIX)/share/applications/$(DESKTOP_ENTRY_NAME)
	for mo in $(MO_FILES); do \
		lang=$$(basename $$mo .mo); \
		rm -f $(DESTDIR)$(LOCALEDIR)/$$lang/LC_MESSAGES/loc-installer.mo; \
	done

# Limpieza
clean:
	rm -f $(OBJ) $(TARGET) $(MO_FILES)

distclean: clean
	rm -f $(POT_FILE)

# Regla para debugging
debug: CFLAGS = -Wall -Wextra -g -DDEBUG `pkg-config --cflags gtk+-3.0`
debug: clean all

.PHONY: all translations pot update-po install uninstall clean distclean debug
