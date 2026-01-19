#!/bin/bash
# core-installer.sh - Installer script for the Loc-OS 24 installer

set -e
set -x

# ========== CONFIGURACIÓN ==========
TARGET="/mnt/installer"
LOG_FILE="/tmp/loc-installer.log"
ERROR_LOG="/tmp/loc-installer-error.log"
RSYNC_EXCLUDES="/tmp/installer-exclude.list"
CUSTOM_EXCLUDES="/etc/loc-installer/custom-excludes.list"
INSTALLER_CONFIG="/etc/loc-installer/loc-installer.conf"
DESKTOP_ENTRY_NAME="loc-installer.desktop"

# Cargar configuración personalizada si existe
if [ -f "$INSTALLER_CONFIG" ]; then
    echo "Loading configuration from $INSTALLER_CONFIG"
    source "$INSTALLER_CONFIG"
else
    echo "No custom configuration found at $INSTALLER_CONFIG, using defaults"
fi

# Inicializar logs
exec 2>"$ERROR_LOG"
echo "=== LOC-OS Installer Log - $(date) ===" > "$LOG_FILE"

log() {
    echo "[$(date '+%H:%M:%S')] $1" | tee -a "$LOG_FILE"
}

error() {
    echo "[$(date '+%H:%M:%S')] ERROR: $1" | tee -a "$LOG_FILE" "$ERROR_LOG"
    exit 1
}

warn() {
    echo "[$(date '+%H:%M:%S')] WARNING: $1" | tee -a "$LOG_FILE"
}

# ========== FUNCIONES DE VERIFICACIÓN ==========
check_requirements() {
    log "Checking requirements..."

    if [ "$(id -u)" -ne 0 ]; then
        error "This script must be run as root"
    fi

    local tools="rsync parted mkfs.ext4 mkfs.fat mount umount chroot grub-install"
    for tool in $tools; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            error "Required tool not found: $tool"
        fi
    done

    log "Requirements OK"
}

detect_boot_mode() {
    if [ -d /sys/firmware/efi ]; then
        echo "uefi"
    else
        echo "bios"
    fi
}

# ========== FUNCIONES DE PARTICIONADO ==========

# Función auxiliar para desmontar completamente un disco
force_unmount_disk() {
    local disk="$1"

    log "Force unmounting all partitions on $disk..."

    # 1. Desactivar cualquier swap
    for part in $(swapon -s | grep "^$disk" | awk '{print $1}'); do
        log "Deactivating swap: $part"
        swapoff "$part" 2>&1 | tee -a "$LOG_FILE" || true
    done

    # 2. Desmontar todas las particiones (incluyendo /mnt si está usando este disco)
    local parts=$(lsblk -lnpo NAME "$disk" 2>/dev/null | tail -n +2)

    for part in $parts; do
        # Verificar si está montado con mount (más confiable que mountpoint)
        if mount | grep -q "^$part "; then
            local mount_point=$(mount | grep "^$part " | awk '{print $3}')
            log "Unmounting $part from $mount_point"

            # Intentar desmontaje normal primero
            if ! umount "$part" 2>&1 | tee -a "$LOG_FILE"; then
                warn "Normal unmount failed for $part, trying force unmount..."

                # Intentar desmontaje forzado
                if ! umount -f "$part" 2>&1 | tee -a "$LOG_FILE"; then
                    warn "Force unmount failed for $part, trying lazy unmount..."

                    # Último recurso: lazy unmount
                    umount -l "$part" 2>&1 | tee -a "$LOG_FILE" || true
                fi
            fi

            sleep 1
        fi
    done

    # 3. Verificar que nada quedó montado
    if mount | grep -q "^$disk"; then
        log "WARNING: Some partitions may still be mounted:"
        mount | grep "^$disk" | tee -a "$LOG_FILE"
        return 1
    fi

    # 4. Limpiar device mapper si existe
    if command -v dmsetup >/dev/null 2>&1; then
        for part in $parts; do
            local dm_name=$(basename "$part")
            if dmsetup info "$dm_name" >/dev/null 2>&1; then
                log "Removing device mapper: $dm_name"
                dmsetup remove "$dm_name" 2>&1 | tee -a "$LOG_FILE" || true
            fi
        done
    fi

    log "Disk $disk is now clean and ready for partitioning"
    return 0
}

partition_disk() {
    local disk="$1"
    local uefi="$2"
    local add_swap="$3"
    local swap_size="$4"

    log "Partitioning $disk (UEFI: $uefi, Swap: $add_swap)"

    # Verificar que el disco existe
    if [ ! -b "$disk" ]; then
        error "Disk $disk not found"
    fi

    log "Unmounting all partitions on $disk..."

    # Desactivar swap si está en uso
    if swapon -s | grep -q "^$disk"; then
        log "Deactivating swap on $disk"
        swapoff "$disk"* 2>&1 | tee -a "$LOG_FILE" || true
    fi

    # Desmontar particiones existentes (múltiples intentos)
    local max_attempts=3
    local attempt=1

    while [ $attempt -le $max_attempts ]; do
        log "Unmount attempt $attempt/$max_attempts"
        local mounted_parts=$(lsblk -lnpo NAME "$disk" 2>/dev/null | tail -n +2)
        local all_unmounted=true

        for part in $mounted_parts; do
            if mount | grep -q "^$part "; then
                log "Unmounting $part"
                if ! umount -f "$part" 2>&1 | tee -a "$LOG_FILE"; then
                    warn "Failed to unmount $part on attempt $attempt"
                    all_unmounted=false

                    # Intentar lazy unmount como último recurso
                    if [ $attempt -eq $max_attempts ]; then
                        log "Trying lazy unmount on $part"
                        umount -l "$part" 2>&1 | tee -a "$LOG_FILE" || true
                    fi
                fi
            fi
        done

        if [ "$all_unmounted" = true ]; then
            log "All partitions unmounted successfully"
            break
        fi

        sleep 2
        attempt=$((attempt + 1))
    done

    # Verificar que no queden particiones montadas
    if mount | grep -q "^$disk"; then
        error "Could not unmount all partitions on $disk. Please unmount manually and try again."
    fi

    # Cerrar cualquier dispositivo LVM o RAID que pueda estar usando el disco
    if command -v dmsetup >/dev/null 2>&1; then
        log "Checking for device-mapper devices..."
        dmsetup remove_all 2>&1 | tee -a "$LOG_FILE" || true
    fi

    sleep 1

    # Limpiar tabla
    log "Wiping disk signature..."
    if ! wipefs -a "$disk" 2>&1 | tee -a "$LOG_FILE"; then
        warn "wipefs reported issues, continuing anyway"
    fi

    # Crear tabla de particiones
    log "Creating partition table..."
    if [ "$uefi" = "uefi" ]; then
        if ! parted -s "$disk" mklabel gpt 2>&1 | tee -a "$LOG_FILE"; then
            error "Failed to create GPT partition table"
        fi
    else
        if ! parted -s "$disk" mklabel msdos 2>&1 | tee -a "$LOG_FILE"; then
            error "Failed to create MSDOS partition table"
        fi
    fi

    sleep 2

    local start="1MiB"
    local part_num=1

    # Partición EFI para UEFI
    if [ "$uefi" = "uefi" ]; then
        log "Creating EFI partition (512MB)"
        if ! parted -s "$disk" mkpart primary fat32 "$start" 513MiB 2>&1 | tee -a "$LOG_FILE"; then
            error "Failed to create EFI partition"
        fi
        if ! parted -s "$disk" set "$part_num" esp on 2>&1 | tee -a "$LOG_FILE"; then
            error "Failed to set ESP flag on EFI partition"
        fi
        EFI_PART="${disk}$(if [[ "$disk" =~ [0-9]$ ]]; then echo "p$part_num"; else echo "$part_num"; fi)"
        log "EFI partition: $EFI_PART"
        start="513MiB"
        part_num=$((part_num + 1))
    fi

    # Partición swap (OPCIONAL)
    if [ "$add_swap" = "true" ] && [ "$swap_size" -gt 0 ]; then
        log "Creating swap partition (${swap_size}MB)"
        local swap_end="$(( ${start%MiB} + swap_size ))MiB"
        if ! parted -s "$disk" mkpart primary linux-swap "$start" "$swap_end" 2>&1 | tee -a "$LOG_FILE"; then
            error "Failed to create swap partition"
        fi
        SWAP_PART="${disk}$(if [[ "$disk" =~ [0-9]$ ]]; then echo "p$part_num"; else echo "$part_num"; fi)"
        log "Swap partition: $SWAP_PART"
        start="$swap_end"
        part_num=$((part_num + 1))
    else
        SWAP_PART=""
    fi

    # Partición raíz y home
    if [ "$sep_home" = "true" ]; then
        # Obtener tamaño total del disco
        log "Getting disk size..."
        local disk_size_mb=$(parted -s "$disk" unit MiB print 2>&1 | grep "^Disk /" | awk '{print $3}' | sed 's/MiB//')

        if [ -z "$disk_size_mb" ]; then
            error "Could not determine disk size"
        fi

        log "Disk size: ${disk_size_mb}MB"

        # Calcular tamaño raíz (60% del espacio restante)
        local remaining_mb=$((disk_size_mb - ${start%MiB}))
        local root_size=$((remaining_mb * 60 / 100))
        local root_end="$(( ${start%MiB} + root_size ))MiB"

        log "Creating root partition (${root_size}MB, from $start to $root_end)"
        if ! parted -s "$disk" mkpart primary ext4 "$start" "$root_end" 2>&1 | tee -a "$LOG_FILE"; then
            error "Failed to create root partition"
        fi
        ROOT_PART="${disk}$(if [[ "$disk" =~ [0-9]$ ]]; then echo "p$part_num"; else echo "$part_num"; fi)"
        log "Root partition: $ROOT_PART"
        part_num=$((part_num + 1))

        log "Creating home partition (from $root_end to 100%)"
        if ! parted -s "$disk" mkpart primary ext4 "$root_end" 100% 2>&1 | tee -a "$LOG_FILE"; then
            error "Failed to create home partition"
        fi
        HOME_PART="${disk}$(if [[ "$disk" =~ [0-9]$ ]]; then echo "p$part_num"; else echo "$part_num"; fi)"
        log "Home partition: $HOME_PART"
    else
        # Solo partición raíz
        log "Creating root partition (from $start to 100%)"
        if ! parted -s "$disk" mkpart primary ext4 "$start" 100% 2>&1 | tee -a "$LOG_FILE"; then
            error "Failed to create root partition"
        fi
        ROOT_PART="${disk}$(if [[ "$disk" =~ [0-9]$ ]]; then echo "p$part_num"; else echo "$part_num"; fi)"
        log "Root partition: $ROOT_PART"
        HOME_PART=""
    fi

    # Sincronizar y esperar que el kernel reconozca las particiones
    log "Syncing and probing partitions..."
    sync
    sleep 3

    if ! partprobe "$disk" 2>&1 | tee -a "$LOG_FILE"; then
        warn "partprobe reported issues, continuing anyway"
    fi

    sleep 2

    # Verificar que las particiones existen
    log "Verifying partitions exist..."
    if [ ! -b "$ROOT_PART" ]; then
        error "Root partition $ROOT_PART was not created"
    fi
    if [ -n "$HOME_PART" ] && [ ! -b "$HOME_PART" ]; then
        error "Home partition $HOME_PART was not created"
    fi
    if [ -n "$SWAP_PART" ] && [ ! -b "$SWAP_PART" ]; then
        error "Swap partition $SWAP_PART was not created"
    fi
    if [ -n "$EFI_PART" ] && [ ! -b "$EFI_PART" ]; then
        error "EFI partition $EFI_PART was not created"
    fi

    # Formatear particiones
    log "Formatting partitions..."

    if [ -n "$EFI_PART" ]; then
        log "Formatting EFI: $EFI_PART"
        if ! mkfs.fat -F 32 "$EFI_PART" 2>&1 | tee -a "$LOG_FILE"; then
            error "Failed to format EFI partition"
        fi
    fi

    if [ -n "$SWAP_PART" ]; then
        log "Formatting swap: $SWAP_PART"
        if ! mkswap "$SWAP_PART" 2>&1 | tee -a "$LOG_FILE"; then
            error "Failed to format swap partition"
        fi
    fi

    log "Formatting root: $ROOT_PART"
    if ! mkfs.ext4 -F "$ROOT_PART" 2>&1 | tee -a "$LOG_FILE"; then
        error "Failed to format root partition"
    fi

    if [ -n "$HOME_PART" ]; then
        log "Formatting home: $HOME_PART"
        if ! mkfs.ext4 -F "$HOME_PART" 2>&1 | tee -a "$LOG_FILE"; then
            error "Failed to format home partition"
        fi
    fi

    log "Partitioning completed successfully"
    log "ROOT_PART=$ROOT_PART"
    log "HOME_PART=$HOME_PART"
    log "SWAP_PART=$SWAP_PART"
    log "EFI_PART=$EFI_PART"
}

# ========== FUNCIONES DE COPIA ==========
create_exclude_list() {
    cat > "$RSYNC_EXCLUDES" << EOF

# System directories that should never be copied
- /dev/*
- /cdrom/*
- /media/*
- /target
- /mnt/*
- /sys/*
- /proc/*
- /tmp/*
- /run/*
- /live

# Boot configuration (will be regenerated)
- /boot/grub/grub.cfg
- /boot/grub/grubenv

# System configuration (will be regenerated)
- /etc/fstab
- /etc/mtab
- /var/lib/dbus/machine-id
- /etc/machine-id

# Live system specific
- /lib/live
- /usr/lib/live
- /run/live
- /bin/live-*
- /usr/bin/live-*
- /etc/live-*
- /usr/share/live

# Installer files
- /usr/share/applications/$DESKTOP_ENTRY_NAME
- /home/live/Desktop/$DESKTOP_ENTRY_NAME
- $CUSTOM_EXCLUDES
- $INSTALLER_CONFIG
- /usr/share/loc-installer/*
- /usr/bin/loc-installer

# Cache and temporary files
- /var/cache/apt/archives/*
- /var/cache/man/*
- /var/cache/man/*/
- /var/tmp/*
- /root/.cache/*
- /home/*/.cache/*
- /home/*/.thumbnails/*
- /home/*/.local/share/Trash/*

# User session files
- /home/*/.xsession-errors*
- /home/*/.Xauthority
- /home/*/.ICEauthority
- /home/*/.dbus/*
- /run/user/*
- /run/user/*/gvfs

# Log files (will start fresh)
- /var/log/*.log
- /var/log/*/*.log

# GVFS mounts (cause permission issues)
- /home/*/.gvfs
- /run/user/*/gvfs
EOF
    # Agregar excludes personalizados de la distro si existen
    if [ -f "$CUSTOM_EXCLUDES" ]; then
        log "Adding custom distro excludes from $CUSTOM_EXCLUDES"
        echo "" >> "$RSYNC_EXCLUDES"
        echo "# Custom distro excludes" >> "$RSYNC_EXCLUDES"
        echo "# ======================" >> "$RSYNC_EXCLUDES"
        cat "$CUSTOM_EXCLUDES" >> "$RSYNC_EXCLUDES"
    fi

    log "Created exclude list"
}

mount_partitions() {
    local root_part="$1"
    local home_part="$2"
    local boot_part="$3"
    local efi_part="$4"

    log "Mounting partitions..."

    # Crear directorio de montaje
    mkdir -p "$TARGET"

    # Montar raíz
    mount "$root_part" "$TARGET" || error "Failed to mount root"

    # Montar home si existe
    if [ -n "$home_part" ]; then
        mkdir -p "$TARGET/home"
        mount "$home_part" "$TARGET/home" || error "Failed to mount home"
    fi

    # Montar boot si existe
    if [ -n "$boot_part" ]; then
        mkdir -p "$TARGET/boot"
        mount "$boot_part" "$TARGET/boot" || error "Failed to mount boot"
    fi

    # Montar EFI si existe
    if [ -n "$efi_part" ]; then
        mkdir -p "$TARGET/boot/efi"
        mount "$efi_part" "$TARGET/boot/efi" || error "Failed to mount EFI"
    fi

    # IMPORTANTE: Crear directorios para sistemas virtuales ANTES de montarlos
    mkdir -p "$TARGET/proc"
    mkdir -p "$TARGET/sys"
    mkdir -p "$TARGET/dev"
    mkdir -p "$TARGET/dev/pts"
    mkdir -p "$TARGET/tmp"

    # Montar sistemas virtuales
    mount --bind /proc "$TARGET/proc"
    mount --bind /sys "$TARGET/sys"
    mount --bind /dev "$TARGET/dev"
    mount --bind /dev/pts "$TARGET/dev/pts" 2>/dev/null || true

    log "Partitions mounted"
}

copy_system() {
    local username="$1"
    log "Starting system copy..."

    create_exclude_list

    # Variables de exclusión para particiones separadas
    local sep_home_opt=""
    local sep_boot_opt=""

    if [ -n "$HOME_PART" ]; then
        sep_home_opt="--exclude=/home/*"
        log "Separate /home partition detected, will copy separately"
    fi

    if [ -n "$BOOT_PART" ]; then
        sep_boot_opt="--exclude=/boot/*"
        log "Separate /boot partition detected, will copy separately"
    fi

    # PASO 1: Copiar sistema raíz
    log "Running rsync for root filesystem (this may take several minutes)..."

    # Usar un buffer para procesar la salida de rsync
    rsync -aAXH \
        --numeric-ids \
        --info=progress2 \
        --no-inc-recursive \
        --filter='P lost+found' \
        --filter='H lost+found' \
        --exclude-from="$RSYNC_EXCLUDES" \
        $sep_home_opt \
        $sep_boot_opt \
        / "$TARGET/" 2>&1 | \
    while IFS= read -r line_raw; do
        # Eliminar retorno de carro y espacios extra al principio
        line=$(echo "$line_raw" | sed 's/\r//g' | sed 's/^[[:space:]]*//')

        # Detectar si es línea de progreso (contiene porcentaje y MB/s)
        if echo "$line" | grep -qE '[0-9]+%.*[0-9]+\.[0-9]+[MKGB]/s'; then
            # Extraer información clave
            percent=$(echo "$line" | grep -oE '[0-9]+%' | head -1)
            speed=$(echo "$line" | grep -oE '[0-9]+\.[0-9]+[MKG]B/s' | head -1 || echo "0.0MB/s")
            eta=$(echo "$line" | grep -oE '[0-9]+:[0-9]+:[0-9]+' | head -1 || echo "0:00:00")

            # Enviar línea formateada
            echo "RSYNC_PROGRESS: $percent $speed $eta"
        else
            # Otras líneas normales
            echo "$line"
        fi
    done | tee -a "$LOG_FILE"

    local rsync_exit=${PIPESTATUS[0]}

    # PASO 2: Copiar /home por separado si existe partición separada
    if [ -n "$HOME_PART" ]; then
        log "Copying /home to separate partition..."

        # IMPORTANTE: Crear el directorio home del nuevo usuario si no existe
        local new_username_home="$TARGET/home/$USERNAME"
        if [ ! -d "$new_username_home" ] && [ -n "$USERNAME" ]; then
            log "Creating home directory for $USERNAME in separate partition"
            mkdir -p "$new_username_home"
            chmod 755 "$new_username_home"
        fi

        # Crear lista de excludes para home (más permisiva)
        local home_excludes="/tmp/home-excludes.list"
        cat > "$home_excludes" << EOF
# Excludes para /home
- .cache/*
- Desktop/$DESKTOP_ENTRY_NAME
- .thumbnails/*
- .local/share/Trash/*
- .xsession-errors*
- .gvfs
EOF

        if rsync -aAX \
            --info=progress2 \
            --filter='P lost+found' \
            --filter='H lost+found' \
            --exclude-from="$home_excludes" \
            /home/ "$TARGET/home/" 2>&1 | tee -a "$LOG_FILE"; then

            log "Home directory copy completed"

            # Cambiar propietario del home copiado si el usuario cambió
            if [ -n "$USERNAME" ] && [ -d "$TARGET/home/live" ]; then
                log "Setting ownership for copied home directories..."
                chroot "$TARGET" chown -R "$USERNAME:$USERNAME" "/home/live" 2>/dev/null || true
            fi
        else
            warn "Home copy had issues (exit code $?), but continuing..."
        fi

        rm -f "$home_excludes"
    fi

    # PASO 3: Copiar /boot por separado si existe partición separada
    if [ -n "$BOOT_PART" ]; then
        log "Copying /boot to separate partition..."

        if rsync -aAX \
            --info=progress2 \
            --filter='P lost+found' \
            --filter='H lost+found' \
            /boot/ "$TARGET/boot/" 2>&1 | tee -a "$LOG_FILE"; then

            log "Boot directory copy completed"
        else
            warn "Boot copy had issues (exit code $?), but continuing..."
        fi
    fi

    # PASO 4: Crear directorios esenciales
    log "Creating essential directories..."
    mkdir -p "$TARGET"/{proc,sys,dev,tmp,run,mnt,media}
    mkdir -p "$TARGET/var/log"
    mkdir -p "$TARGET/var/tmp"
    mkdir -p "$TARGET/dev/pts"
    mkdir -p "$TARGET/run/shm"

    # Establecer permisos correctos
    chmod 1777 "$TARGET/tmp" "$TARGET/var/tmp"
    chmod 755 "$TARGET"/{proc,sys,dev,run,mnt,media}

    # PASO 5: Crear nodos de dispositivos básicos en /dev
    log "Creating basic device nodes..."

    # Solo crear si no existen (para no sobrescribir si ya están)
    [ ! -e "$TARGET/dev/console" ] && mknod -m 622 "$TARGET/dev/console" c 5 1
    [ ! -e "$TARGET/dev/null" ] && mknod -m 666 "$TARGET/dev/null" c 1 3
    [ ! -e "$TARGET/dev/zero" ] && mknod -m 666 "$TARGET/dev/zero" c 1 5
    [ ! -e "$TARGET/dev/ptmx" ] && mknod -m 666 "$TARGET/dev/ptmx" c 5 2
    [ ! -e "$TARGET/dev/tty" ] && mknod -m 666 "$TARGET/dev/tty" c 5 0
    [ ! -e "$TARGET/dev/random" ] && mknod -m 444 "$TARGET/dev/random" c 1 8
    [ ! -e "$TARGET/dev/urandom" ] && mknod -m 444 "$TARGET/dev/urandom" c 1 9

    # Establecer propietarios correctos
    chown root:tty "$TARGET/dev/console" "$TARGET/dev/ptmx" "$TARGET/dev/tty" 2>/dev/null || true

    # Crear enlaces simbólicos
    ln -sf /proc/self/fd "$TARGET/dev/fd" 2>/dev/null || true
    ln -sf /proc/self/fd/0 "$TARGET/dev/stdin" 2>/dev/null || true
    ln -sf /proc/self/fd/1 "$TARGET/dev/stdout" 2>/dev/null || true
    ln -sf /proc/self/fd/2 "$TARGET/dev/stderr" 2>/dev/null || true
    ln -sf /proc/kcore "$TARGET/dev/core" 2>/dev/null || true
    ln -sf /run/shm "$TARGET/dev/shm" 2>/dev/null || true

    log "Device nodes created"

    # PASO 6: Habilitar updatedb si estaba deshabilitado
    if [ -e "$TARGET/usr/bin/updatedb.mlocate" ]; then
        if [ ! -x "$TARGET/usr/bin/updatedb.mlocate" ]; then
            log "Re-enabling updatedb.mlocate"
            chmod +x "$TARGET/usr/bin/updatedb.mlocate"
        fi
    fi

    # PASO 7: Configurar pmount (si está instalado)
    if [ -f "$TARGET/etc/pmount.allow" ]; then
        log "Disabling automount of fixed drives in pmount"
        sed -i 's:^/dev/sd\[a-z\]:#/dev/sd\[a-z\]:' "$TARGET/etc/pmount.allow" 2>/dev/null || true
    fi

    log "System copy stage completed"
}

create_swapfile() {
    local swapfile_size="$1"

    [ -z "$swapfile_size" ] || [ "$swapfile_size" -eq 0 ] && return 0

    log "Creating ${swapfile_size}MB swapfile..."

    # 1. Verificar espacio
    local free_mb=$(df -m "$TARGET" | tail -1 | awk '{print $4}')
    [ $free_mb -lt $swapfile_size ] && {
        warn "Need ${swapfile_size}MB, only ${free_mb}MB free"
        return 1
    }

    # 2. Decidir método basado en RAM
    local ram_free_mb=$(($(grep MemFree /proc/meminfo | awk '{print $2}') / 1024))
    local ram_total_mb=$(($(grep MemTotal /proc/meminfo | awk '{print $2}') / 1024))

    # REGLA: Si RAM libre > swapfile_size + 512MB, podemos ser rápidos
    if [ $ram_free_mb -gt $((swapfile_size + 512)) ] || [ $ram_total_mb -gt $((swapfile_size * 4)) ]; then
        log "Fast method (RAM: ${ram_free_mb}MB free)"

        # Intentar fallocate (mágico)
        if command -v fallocate >/dev/null 2>&1; then
            if fallocate -l ${swapfile_size}M "$TARGET/swapfile" 2>/dev/null; then
                log "Instant swapfile with fallocate"
            else
                # fallocate falló, dd normal
                dd if=/dev/zero of="$TARGET/swapfile" bs=1M count="$swapfile_size" status=none
            fi
        else
            # dd normal sin fallocate
            dd if=/dev/zero of="$TARGET/swapfile" bs=1M count="$swapfile_size" status=none
        fi
    else
        # Poca RAM: método seguro
        log "Safe method (RAM: ${ram_free_mb}MB free)"

        local chunk
        if [ $ram_free_mb -lt 256 ]; then
            chunk=32  # Muy poca RAM
        elif [ $ram_free_mb -lt 512 ]; then
            chunk=64  # Poca RAM
        else
            chunk=128 # RAM moderada
        fi

        log "Using ${chunk}MB chunks"

        > "$TARGET/swapfile"
        local created=0

        while [ $created -lt $swapfile_size ]; do
            local remaining=$((swapfile_size - created))
            local this_chunk=$((remaining < chunk ? remaining : chunk))

            dd if=/dev/zero bs=1M count=$this_chunk 2>/dev/null >> "$TARGET/swapfile"
            created=$((created + this_chunk))

            # Sincronizar cada 512MB
            [ $((created % 512)) -eq 0 ] && sync
        done
    fi

    # 3. Formatear (si se creó algo)
    if [ -f "$TARGET/swapfile" ] && [ $(stat -c%s "$TARGET/swapfile" 2>/dev/null || echo 0) -gt 0 ]; then
        if mkswap "$TARGET/swapfile" >/dev/null 2>&1; then
            chmod 600 "$TARGET/swapfile"
            log "Swapfile created successfully"
            return 0
        fi
    fi

    warn "Failed to create swapfile"
    rm -f "$TARGET/swapfile" 2>/dev/null
    return 1
}

# ========== FUNCIONES DE CONFIGURACIÓN REGIONAL ==========
configure_locales() {
    local timezone="$1"
    local language="$2"
    local keyboard="$3"
    local keyboard_variant="${4:-}"  # Opcional, puede estar vacío

    log "Configuring locales: timezone=$timezone, language=$language, keyboard=$keyboard, variant=$keyboard_variant"

    # 1. TIMEZONE
    log "Setting timezone to: $timezone"
    # Verificar que la zona horaria existe en el sistema destino
    if [ -f "$TARGET/usr/share/zoneinfo/$timezone" ]; then
        # Método 1: Symlink (preferido en systemd)
        rm -f "$TARGET/etc/localtime"
        ln -sf "/usr/share/zoneinfo/$timezone" "$TARGET/etc/localtime"

        # Método 2: Escribir en /etc/timezone (para compatibilidad)
        echo "$timezone" > "$TARGET/etc/timezone"

        # Método 3: Usar timedatectl dentro del chroot si está disponible
        if chroot "$TARGET" command -v timedatectl >/dev/null 2>&1; then
            chroot "$TARGET" timedatectl set-timezone "$timezone" 2>/dev/null || true
        fi

        log "Timezone set to: $timezone"
    else
        warn "Timezone $timezone not found in target system, using UTC"
        rm -f "$TARGET/etc/localtime"
        ln -sf "/usr/share/zoneinfo/UTC" "$TARGET/etc/localtime"
        echo "UTC" > "$TARGET/etc/timezone"
    fi

    # 2. LOCALES
    # Configurar locale.gen
    if [ -f "$TARGET/etc/locale.gen" ]; then
        # Comentar todos los locales
        sed -i '/^[a-z][a-z]_/s/^/# /' "$TARGET/etc/locale.gen" 2>/dev/null || true

        # Agregar locale seleccionado
        if ! grep -q "^$language.UTF-8 UTF-8" "$TARGET/etc/locale.gen" 2>/dev/null; then
            echo "$language.UTF-8 UTF-8" >> "$TARGET/etc/locale.gen"
        fi

        # Descomentar locale seleccionado
        sed -i "s/^#.*$language.UTF-8 UTF-8/$language.UTF-8 UTF-8/" "$TARGET/etc/locale.gen" 2>/dev/null || true
    fi

    # Configurar locale.conf
    cat > "$TARGET/etc/locale.conf" << EOF
LANG=$language.UTF-8
LC_ALL=$language.UTF-8
EOF

    # Configurar default/locale también
    echo "LANG=$language.UTF-8" > "$TARGET/etc/default/locale"
    echo "LC_ALL=$language.UTF-8" >> "$TARGET/etc/default/locale"

    # Generar locales
    chroot "$TARGET" locale-gen 2>/dev/null || warn "Locale generation may have warnings"

    # 3. KEYBOARD
    # Configurar para consola virtual
    if [ -n "$keyboard_variant" ] && [ "$keyboard_variant" != "default" ]; then
        # Con variante específica
        echo "KEYMAP=$keyboard-$keyboard_variant" > "$TARGET/etc/vconsole.conf"
        log "Console keyboard set to: $keyboard-$keyboard_variant"
    else
        # Sin variante o variante default
        echo "KEYMAP=$keyboard" > "$TARGET/etc/vconsole.conf"
        log "Console keyboard set to: $keyboard"
    fi

    # Configurar para X11
    if [ -d "$TARGET/etc/X11" ]; then
        # Determinar variante para X11
        local xkb_variant=""
        if [ -n "$keyboard_variant" ] && [ "$keyboard_variant" != "default" ]; then
            xkb_variant="$keyboard_variant"
        fi

        cat > "$TARGET/etc/default/keyboard" << EOF
# KEYBOARD CONFIGURATION FILE
# Consult the keyboard(5) manual page.

XKBMODEL="pc105"
XKBLAYOUT="$keyboard"
XKBVARIANT="$xkb_variant"
XKBOPTIONS=""

BACKSPACE="guess"
EOF

        log "X11 keyboard set to: layout=$keyboard, variant=$xkb_variant"

        # Aplicar configuración
        chroot "$TARGET" dpkg-reconfigure -f noninteractive keyboard-configuration 2>/dev/null || \
            warn "Keyboard configuration may have warnings"
    fi

    # 4. Configurar para loadkeys (consola) si está disponible
    if chroot "$TARGET" command -v loadkeys >/dev/null 2>&1; then
        if [ -n "$keyboard_variant" ] && [ "$keyboard_variant" != "default" ]; then
            chroot "$TARGET" loadkeys "$keyboard-$keyboard_variant" 2>/dev/null || \
                chroot "$TARGET" loadkeys "$keyboard" 2>/dev/null || true
        else
            chroot "$TARGET" loadkeys "$keyboard" 2>/dev/null || true
        fi
    fi

    log "Locales configuration completed"
}

# ========== FUNCIONES DE CONFIGURACIÓN DEL SISTEMA ==========
create_fstab() {
    log "Creating fstab..."

    cat > "$TARGET/etc/fstab" << EOF
# /etc/fstab: static file system information.
# Generated by LOC-OS Installer
#
# <file system> <mount point>   <type>  <options>       <dump>  <pass>
EOF

    # Función para obtener UUID
    get_uuid() {
        blkid -s UUID -o value "$1" 2>/dev/null || echo ""
    }

    # Raíz
    if [ -n "$ROOT_PART" ]; then
        uuid=$(get_uuid "$ROOT_PART")
        if [ -n "$uuid" ]; then
            echo "UUID=$uuid / ext4 defaults,noatime,errors=remount-ro 0 1" >> "$TARGET/etc/fstab"
        else
            echo "$ROOT_PART / ext4 defaults,noatime,errors=remount-ro 0 1" >> "$TARGET/etc/fstab"
        fi
    fi

    # Home
    if [ -n "$HOME_PART" ]; then
        uuid=$(get_uuid "$HOME_PART")
        if [ -n "$uuid" ]; then
            echo "UUID=$uuid /home ext4 defaults,noatime 0 2" >> "$TARGET/etc/fstab"
        fi
    fi

    # Boot
    if [ -n "$BOOT_PART" ]; then
        uuid=$(get_uuid "$BOOT_PART")
        if [ -n "$uuid" ]; then
            echo "UUID=$uuid /boot ext4 defaults,noatime 0 1" >> "$TARGET/etc/fstab"
        fi
    fi

    # Swap
    if [ -n "$SWAP_PART" ]; then
        uuid=$(get_uuid "$SWAP_PART")
        if [ -n "$uuid" ]; then
            echo "UUID=$uuid none swap sw 0 0" >> "$TARGET/etc/fstab"
        fi
    fi

    # Swapfile
    if [ "$CREATE_SWAPFILE" = "true" ]; then
        echo "/swapfile none swap sw 0 0" >> "$TARGET/etc/fstab"
        log "Added swapfile to fstab"
    fi

    # EFI
    if [ -n "$EFI_PART" ]; then
        uuid=$(get_uuid "$EFI_PART")
        if [ -n "$uuid" ]; then
            echo "UUID=$uuid /boot/efi vfat umask=0077 0 1" >> "$TARGET/etc/fstab"
        fi
    fi

    # tmpfs para /tmp
    echo "tmpfs /tmp tmpfs defaults,noatime,mode=1777 0 0" >> "$TARGET/etc/fstab"

    log "fstab created"
}

configure_autologin() {
    local username="$1"
    local enable="$2"  # true/false

    if [ "$enable" = "true" ]; then
        log "Enabling autologin for user: $username"
    else
        log "Disabling autologin"
    fi

    # Cargar comandos personalizados desde archivo de configuración
    if [ -f "$INSTALLER_CONFIG" ]; then
        log "Loading autologin configuration from $INSTALLER_CONFIG"

        # Source del archivo de configuración
        source "$INSTALLER_CONFIG"

        # Verificar si hay comandos personalizados definidos
        if [ -n "$AUTOLOGIN_ENABLE_CMD" ] || [ -n "$AUTOLOGIN_DISABLE_CMD" ]; then
            log "Using custom autologin commands from config"

            if [ "$enable" = "true" ]; then
                if [ -n "$AUTOLOGIN_ENABLE_CMD" ]; then
                    log "Executing custom enable command..."
                    # Reemplazar placeholders
                    local cmd="${AUTOLOGIN_ENABLE_CMD//\{USERNAME\}/$username}"
                    cmd="${cmd//\{TARGET\}/$TARGET}"

                    # Ejecutar comando
                    eval "$cmd" 2>&1 | tee -a "$LOG_FILE"

                    if [ $? -eq 0 ]; then
                        log "Custom autologin enable command executed successfully"
                    else
                        warn "Custom autologin enable command had issues"
                    fi
                else
                    warn "AUTOLOGIN_ENABLE_CMD not defined in config, falling back to default"
                    configure_lxdm_autologin "$username" "true"
                fi
            else
                if [ -n "$AUTOLOGIN_DISABLE_CMD" ]; then
                    log "Executing custom disable command..."
                    local cmd="${AUTOLOGIN_DISABLE_CMD//\{TARGET\}/$TARGET}"

                    eval "$cmd" 2>&1 | tee -a "$LOG_FILE"

                    if [ $? -eq 0 ]; then
                        log "Custom autologin disable command executed successfully"
                    else
                        warn "Custom autologin disable command had issues"
                    fi
                else
                    warn "AUTOLOGIN_DISABLE_CMD not defined in config, falling back to default"
                    configure_lxdm_autologin "$username" "false"
                fi
            fi

            return 0
        fi
    fi

    # Comportamiento por defecto: LXDM
    log "Using default LXDM autologin configuration"
    configure_lxdm_autologin "$username" "$enable"
}

configure_lxdm_autologin() {
    local username="$1"
    local enable="$2"

    if [ ! -f "$TARGET/etc/lxdm/lxdm.conf" ]; then
        warn "LXDM configuration file not found at $TARGET/etc/lxdm/lxdm.conf"
        return 1
    fi

    log "Configuring LXDM autologin..."

    if [ "$enable" = "true" ]; then
        # Habilitar autologin en LXDM
        log "Enabling LXDM autologin for user: $username"

        # Backup del archivo original
        cp "$TARGET/etc/lxdm/lxdm.conf" "$TARGET/etc/lxdm/lxdm.conf.bak" 2>/dev/null || true

        # True comando papu pro
        sed -i "s/autologin=[^ ]*/autologin=$username/g" "$TARGET/etc/lxdm/default.conf"

        log "LXDM autologin enabled successfully"

    else
        # Deshabilitar autologin en LXDM
        log "Disabling LXDM autologin"

        # Comentar autologin
        sed -i 's/^autologin=/#autologin=/' "$TARGET/etc/lxdm/lxdm.conf"

        log "LXDM autologin disabled successfully"
    fi

    return 0
}

configure_user() {
    local hostname="$1"
    local new_username="$2"
    local password="$3"
    local autologin="$4"
    local root_password="${5:-$password}"  # Por defecto usa la misma contraseña del usuario

    log "Configuring user: $new_username"

    # 1. Hostname
    echo "$hostname" > "$TARGET/etc/hostname"
    cat > "$TARGET/etc/hosts" << EOF
127.0.0.1	localhost
127.0.1.1	$hostname
::1		localhost ip6-localhost ip6-loopback
ff02::1		ip6-allnodes
ff02::2		ip6-allrouters
EOF

    # 2. Buscar usuario existente (UID 1000)
    local old_username=$(chroot "$TARGET" awk -F: '$3 == 1000 {print $1}' /etc/passwd 2>/dev/null)

    if [ -z "$old_username" ]; then
        # Crear usuario nuevo
        log "Creating new user: $new_username"
        chroot "$TARGET" useradd -m \
            -G sudo,users,audio,video,disk,cdrom,dip,plugdev \
            -s /bin/bash \
            "$new_username"
    elif [ "$old_username" != "$new_username" ]; then
        # ===========================================================
        # CASO ESPECIAL: Home en partición separada vs sistema raíz
        # ===========================================================

        # Verificar si estamos usando partición de home separada
        if [ -n "$HOME_PART" ]; then
            log "Separate /home partition detected - special handling for user rename"

            # Las rutas pueden estar en diferentes particiones
            local old_home_root="/home/$old_username"    # En partición separada
            local new_home_root="/home/$new_username"    # En partición separada

            # 1. Cambiar nombre de grupo si existe
            log "Changing group name from $old_username to $new_username"
            if chroot "$TARGET" grep -q "^$old_username:" /etc/group 2>/dev/null; then
                chroot "$TARGET" groupmod -n "$new_username" "$old_username" 2>&1 | tee -a "$LOG_FILE" || {
                    warn "Could not rename group $old_username"
                    # Crear grupo nuevo si falla
                    chroot "$TARGET" groupadd "$new_username" 2>/dev/null || true
                }
            else
                chroot "$TARGET" groupadd "$new_username" 2>/dev/null || true
            fi

            # 2. Cambiar nombre de usuario
            log "Changing username from $old_username to $new_username"
            chroot "$TARGET" usermod -l "$new_username" "$old_username" 2>&1 | tee -a "$LOG_FILE" || {
                error "Failed to change username from $old_username to $new_username"
            }

            # 3. Configurar directorio home en /etc/passwd
            log "Updating home directory in /etc/passwd to $new_home_root"
            chroot "$TARGET" usermod -d "$new_home_root" "$new_username" 2>&1 | tee -a "$LOG_FILE" || {
                warn "Could not update home directory in /etc/passwd"
                # Manual update
                sed -i "s|^$new_username:[^:]*:[^:]*:[^:]*:[^:]*:\K.*|$new_home_root|" "$TARGET/etc/passwd" 2>/dev/null || true
            }

            # 4. Manejar directorios de home físicos
            if [ -d "$TARGET/home/live" ] && [ ! -d "$TARGET/home/$new_username" ]; then
                log "Renaming home directory from /home/live to /home/$new_username"
                mv "$TARGET/home/live" "$TARGET/home/$new_username" 2>&1 | tee -a "$LOG_FILE" || {
                    warn "Could not rename directory, copying instead"
                    cp -a "$TARGET/home/live/." "$TARGET/home/$new_username/" 2>/dev/null || true
                    rm -rf "$TARGET/home/live" 2>/dev/null || true
                }
            elif [ -d "$TARGET/home/live" ] && [ -d "$TARGET/home/$new_username" ]; then
                log "Both /home/live and /home/$new_username exist, merging..."
                cp -a "$TARGET/home/live/." "$TARGET/home/$new_username/" 2>/dev/null || true
                rm -rf "$TARGET/home/live" 2>/dev/null || true
            fi

            # 5. Establecer permisos correctos
            if [ -d "$TARGET/home/$new_username" ]; then
                log "Setting ownership for /home/$new_username"
                chroot "$TARGET" chown -R "$new_username:$new_username" "/home/$new_username" 2>/dev/null || true
            fi

            # 6. Actualizar registros de usuario en /etc/shadow, /etc/gshadow, etc.
            log "Updating user references in system files..."
            sed -i "s/^$old_username:/$new_username:/" "$TARGET/etc/shadow" 2>/dev/null || true
            sed -i "s/^$old_username:/$new_username:/" "$TARGET/etc/gshadow" 2>/dev/null || true
            sed -i "s/:[^:]*:$old_username:/:$new_username:/" "$TARGET/etc/group" 2>/dev/null || true
            sed -i "s/,$old_username,/,$new_username,/" "$TARGET/etc/group" 2>/dev/null || true
            sed -i "s/,$old_username$/,$new_username/" "$TARGET/etc/group" 2>/dev/null || true

        else
            # Home NO separado - usar método normal
            log "Renaming user $old_username to $new_username (home in root partition)"

            # Cambiar nombre de grupo
            chroot "$TARGET" groupmod -n "$new_username" "$old_username" 2>/dev/null || true

            # Cambiar nombre de usuario
            chroot "$TARGET" usermod -l "$new_username" "$old_username" 2>/dev/null || true

            # Cambiar directorio home (con -m para mover archivos)
            chroot "$TARGET" usermod -d /home/"$new_username" -m "$new_username" 2>&1 | tee -a "$LOG_FILE" || {
                warn "Could not move home directory, creating new one"
                chroot "$TARGET" mkdir -p "/home/$new_username"
                chroot "$TARGET" chown "$new_username:$new_username" "/home/$new_username"
                if chroot "$TARGET" [ -d "/home/$old_username" ]; then
                    chroot "$TARGET" cp -a "/home/$old_username/." "/home/$new_username/" 2>/dev/null || true
                fi
            }

            # Limpiar directorio antiguo si quedó vacío
            if chroot "$TARGET" [ -d "/home/$old_username" ]; then
                if [ -z "$(chroot "$TARGET" ls -A "/home/$old_username" 2>/dev/null)" ]; then
                    chroot "$TARGET" rmdir "/home/$old_username" 2>/dev/null || true
                else
                    log "Old home directory not empty, keeping: /home/$old_username"
                    chroot "$TARGET" chown -R "$new_username:$new_username" "/home/$old_username" 2>/dev/null || true
                fi
            fi
        fi
    else
        log "Username unchanged: $new_username"
    fi

    # 3. Configurar contraseñas
    log "Setting user password"
    echo "$new_username:$password" | chroot "$TARGET" chpasswd 2>/dev/null

    # Configurar contraseña de root (puede ser diferente o igual)
    if [ "$root_password" != "$password" ]; then
        log "Setting custom root password"
    else
        log "Setting root password (same as user)"
    fi
    echo "root:$root_password" | chroot "$TARGET" chpasswd 2>/dev/null

    # 4. Configurar sudo
    log "Configuring sudo..."
    chroot "$TARGET" usermod -a -G sudo "$new_username" 2>/dev/null

    # Asegurar configuración de sudo
    if ! grep -q "^%sudo" "$TARGET/etc/sudoers" 2>/dev/null; then
        echo "%sudo ALL=(ALL:ALL) ALL" >> "$TARGET/etc/sudoers"
    fi

    # 5. Configurar autologin usando el sistema configurable
    configure_autologin "$new_username" "$autologin"

    log "User configuration completed"
}

# ========== FUNCIONES DE BOOTLOADER ==========
install_bootloader() {
    local disk="$1"
    local efi_part="$2"

    log "Installing bootloader..."

    if [ -n "$BOOT_PART" ]; then
        log "Mounting separate /boot in chroot..."
        chroot "$TARGET" mount $BOOT_PART /boot 2>/dev/null
    fi


    if [ -n "$efi_part" ]; then
        # UEFI
        log "Installing GRUB for UEFI"

        # Montar EFI si no está montado
        if ! mountpoint -q "$TARGET/boot/efi" 2>/dev/null; then
            mkdir -p "$TARGET/boot/efi"
            mount "$efi_part" "$TARGET/boot/efi" || error "Failed to mount EFI for GRUB"
        fi

        chroot "$TARGET" grub-install \
            --target=x86_64-efi \
            --efi-directory=/boot/efi \
            --bootloader-id=LOC-OS \
            --recheck 2>&1 | tee -a "$LOG_FILE" || warn "GRUB install may have warnings"
    else
        # BIOS
        log "Installing GRUB for BIOS"
        chroot "$TARGET" grub-install \
            --target=i386-pc \
            --recheck \
            "$disk" 2>&1 | tee -a "$LOG_FILE" || warn "GRUB install may have warnings"
    fi

    # Actualizar configuración GRUB
    log "Updating GRUB configuration"
    chroot "$TARGET" update-grub 2>&1 | tee -a "$LOG_FILE" || warn "GRUB update may have warnings"

    log "Bootloader installed"
}

# ========== FUNCIONES DE LIMPIEZA ==========
cleanup_post_install() {
    log "Running post-install cleanup..."

    # 1. Eliminar icono del instalador del escritorio (todas las variantes)
    for user_home in "$TARGET"/home/*/ "$TARGET"/root/; do
        if [ -d "$user_home" ]; then
            # Varios nombres posibles para el directorio de escritorio
            rm -f "$user_home/Desktop/$DESKTOP_ENTRY_NAME" 2>/dev/null || true
            rm -f "$user_home/Escritorio/$DESKTOP_ENTRY_NAME" 2>/dev/null || true
            rm -f "$user_home/Desktop/loc-installer.desktop" 2>/dev/null || true
            rm -f "$user_home/Escritorio/loc-installer.desktop" 2>/dev/null || true

            # También eliminar del menú de aplicaciones (localizado)
            local config_dir="$user_home/.config/autostart"
            if [ -d "$config_dir" ]; then
                rm -f "$config_dir/$DESKTOP_ENTRY_NAME" 2>/dev/null || true
            fi
        fi
    done

    # 2. Eliminar entrada del menú global (GTK/desktop standard)
    rm -f "$TARGET/usr/share/applications/$DESKTOP_ENTRY_NAME" 2>/dev/null || true

    # 3. Limpiar caché de paquetes
    if [ -d "$TARGET/var/cache/apt/archives" ]; then
        rm -rf "$TARGET/var/cache/apt/archives"/* 2>/dev/null || true
    fi

    # 4. Limpiar logs
    find "$TARGET/var/log" -name "*.log" -type f -exec truncate -s 0 {} \; 2>/dev/null || true

    # 5. Regenerar initramfs (importante para arranque)
    log "Updating initramfs..."
    chroot "$TARGET" update-initramfs -u -k all 2>/dev/null || warn "Initramfs update may have warnings"

    # 6. Sincronizar
    sync

    log "Cleanup completed"
}
unmount_all() {
    log "Unmounting partitions (non-critical operation)..."

    # Lista de posibles puntos de montaje (algunos pueden no existir)
    local mount_points=(
        "$TARGET/proc"
        "$TARGET/sys"
        "$TARGET/dev/pts"
        "$TARGET/dev"
        "$TARGET/boot/efi"
        "$TARGET/boot"
        "$TARGET/home"
        "$TARGET"
    )

    # PRIMERA RONDA: Intento normal
    for mp in "${mount_points[@]}"; do
        if mountpoint -q "$mp" 2>/dev/null; then
            log "Unmounting: $mp"
            umount "$mp" 2>/dev/null && log "  ✓ Success" || {
                log "  ✗ Failed, will retry later"
            }
        fi
    done

    sleep 1  # Pequeña pausa

    # SEGUNDA RONDA: Intento forzado/lazy para lo que quedó
    for mp in "${mount_points[@]}"; do
        if mountpoint -q "$mp" 2>/dev/null; then
            log "Retrying: $mp"
            # Intentar en orden: normal -> forzado -> lazy
            umount "$mp" 2>/dev/null || \
            umount -f "$mp" 2/dev/null || \
            umount -l "$mp" 2>/dev/null || \
            log "  Could not unmount $mp (non-critical)"
        fi
    done

    # Reporte final
    local still_mounted=$(mount | grep -c "$TARGET" 2>/dev/null || echo 0)
    if [ "$still_mounted" -gt 0 ]; then
        warn "$still_mounted mount(s) still active under $TARGET"
        mount | grep "$TARGET" 2>&1 | tee -a "$LOG_FILE" || true
    else
        log "All mounts cleaned up"
    fi

    log "Unmount completed (installation can continue regardless)"
}

# ========== FUNCIÓN PRINCIPAL ==========
main_installation() {
    # Variables
    local DISK="" USERNAME="" HOSTNAME="" PASSWORD=""
    local TIMEZONE="" LANGUAGE="" KEYBOARD=""
    local AUTOLOGIN="true" SEP_HOME="false" SEP_BOOT="false"
    local AUTO_PARTITION="true" UEFI_MODE="auto"
    local ADD_SWAP="false" SWAP_SIZE="2048"
    local CREATE_SWAPFILE="false" SWAPFILE_SIZE="2048"

    # Parsear argumentos
    while [[ $# -gt 0 ]]; do
        case $1 in
            # Required parameters
            --disk=*) DISK="${1#*=}"; shift ;;
            --disk) DISK="$2"; shift 2 ;;
            --username=*) USERNAME="${1#*=}"; shift ;;
            --username) USERNAME="$2"; shift 2 ;;
            --hostname=*) HOSTNAME="${1#*=}"; shift ;;
            --hostname) HOSTNAME="$2"; shift 2 ;;
            --password=*) PASSWORD="${1#*=}"; shift ;;
            --password) PASSWORD="$2"; shift 2 ;;

            # Regional settings
            --timezone=*) TIMEZONE="${1#*=}"; shift ;;
            --timezone) TIMEZONE="$2"; shift 2 ;;
            --language=*) LANGUAGE="${1#*=}"; shift ;;
            --language) LANGUAGE="$2"; shift 2 ;;
            --keyboard=*) KEYBOARD="${1#*=}"; shift ;;
            --keyboard) KEYBOARD="$2"; shift 2 ;;
            --keyboard-variant=*) KEYBOARD_VARIANT="${1#*=}"; shift ;;
            --keyboard-variant) KEYBOARD_VARIANT="$2"; shift 2 ;;

            # User passwords
            --root-password=*) ROOT_PASSWORD="${1#*=}"; shift ;;
            --root-password) ROOT_PASSWORD="$2"; shift 2 ;;

            # Optional settings
            --autologin=*) AUTOLOGIN="${1#*=}"; shift ;;
            --autologin) AUTOLOGIN="$2"; shift 2 ;;
            --sep-home=*) SEP_HOME="${1#*=}"; shift ;;
            --sep-home) SEP_HOME="$2"; shift 2 ;;
            --sep-boot=*) SEP_BOOT="${1#*=}"; shift ;;
            --sep-boot) SEP_BOOT="$2"; shift 2 ;;
            --uefi-mode=*) UEFI_MODE="${1#*=}"; shift ;;
            --uefi-mode) UEFI_MODE="$2"; shift 2 ;;
            --auto-partition=*) AUTO_PARTITION="${1#*=}"; shift ;;
            --auto-partition) AUTO_PARTITION="$2"; shift 2 ;;

            # Swap options
            --add-swap=*) ADD_SWAP="${1#*=}"; shift ;;
            --add-swap) ADD_SWAP="$2"; shift 2 ;;
            --swap-size=*) SWAP_SIZE="${1#*=}"; shift ;;
            --swap-size) SWAP_SIZE="$2"; shift 2 ;;
            --create-swapfile=*) CREATE_SWAPFILE="${1#*=}"; shift ;;
            --create-swapfile) CREATE_SWAPFILE="$2"; shift 2 ;;
            --swapfile-size=*) SWAPFILE_SIZE="${1#*=}"; shift ;;
            --swapfile-size) SWAPFILE_SIZE="$2"; shift 2 ;;

            # Manual partitions
            --root-part=*) ROOT_PART="${1#*=}"; shift ;;
            --root-part) ROOT_PART="$2"; shift 2 ;;
            --home-part=*) HOME_PART="${1#*=}"; shift ;;
            --home-part) HOME_PART="$2"; shift 2 ;;
            --boot-part=*) BOOT_PART="${1#*=}"; shift ;;
            --boot-part) BOOT_PART="$2"; shift 2 ;;
            --swap-part=*) SWAP_PART="${1#*=}"; shift ;;
            --swap-part) SWAP_PART="$2"; shift 2 ;;
            --efi-part=*) EFI_PART="${1#*=}"; shift ;;
            --efi-part) EFI_PART="$2"; shift 2 ;;

            *) shift ;;
        esac
    done

    # Validar parámetros requeridos SIEMPRE necesarios
    [ -z "$USERNAME" ] && error "Username not specified (--username)"
    [ -z "$HOSTNAME" ] && error "Hostname not specified (--hostname)"
    [ -z "$PASSWORD" ] && error "Password not specified (--password)"
    [ -z "$TIMEZONE" ] && error "Timezone not specified (--timezone)"
    [ -z "$LANGUAGE" ] && error "Language not specified (--language)"
    [ -z "$KEYBOARD" ] && error "Keyboard not specified (--keyboard)"

    # Validar parámetros según el modo de particionado
    if [ "$AUTO_PARTITION" = "true" ]; then
        # En modo automático, el disco es OBLIGATORIO
        [ -z "$DISK" ] && error "Disk not specified (--disk) - required for automatic partitioning"
    else
        # En modo manual, la partición root es OBLIGATORIA
        [ -z "$ROOT_PART" ] && error "Root partition not specified (--root-part) - required for manual partitioning"
    fi

    # Valores por defecto
    : ${AUTOLOGIN:=true}
    : ${SEP_HOME:=false}
    : ${SEP_BOOT:=false}
    : ${AUTO_PARTITION:=true}
    : ${UEFI_MODE:=auto}
    : ${ADD_SWAP:=false}
    : ${SWAP_SIZE:=2048}
    : ${CREATE_SWAPFILE:=false}
    : ${SWAPFILE_SIZE:=0}

    log "=== Starting LOC-OS Installation ==="
    log "Installation mode: $([ "$AUTO_PARTITION" = "true" ] && echo "Automatic" || echo "Manual")"
    [ -n "$DISK" ] && log "Disk: $DISK"
    [ "$AUTO_PARTITION" = "false" ] && log "Root partition: $ROOT_PART"
    log "Username: $USERNAME"
    log "Hostname: $HOSTNAME"
    log "Timezone: $TIMEZONE"
    log "Language: $LANGUAGE"
    log "Keyboard: $KEYBOARD"
    log "Autologin: $AUTOLOGIN"
    log "Separate /home: $SEP_HOME"
    log "Separate /boot: $SEP_BOOT"
    log "Add swap partition: $ADD_SWAP (${SWAP_SIZE}MB)"
    log "Create swapfile: $CREATE_SWAPFILE (${SWAPFILE_SIZE}MB)"

    # Registrar cleanup para ejecutar al final
    trap 'log "Installation interrupted"; exit 1' INT TERM

    # Total de pasos (puedes ajustar según tu instalador real)
    TOTAL_STEPS=13

    # Paso 1: Verificar requisitos
    echo "PROGRESS:5:Checking system requirements..."
    check_requirements

    # Paso 2: Detectar modo boot
    echo "PROGRESS:10:Detecting boot mode..."
    if [ "$UEFI_MODE" = "auto" ]; then
        UEFI_MODE=$(detect_boot_mode)
        log "Detected boot mode: $UEFI_MODE"
    fi

    # Paso 3: Particionado
    if [ "$AUTO_PARTITION" = "true" ]; then
        echo "PROGRESS:15:Auto-partitioning disk $DISK..."
        log "Auto-partitioning disk $DISK"

        # Forzar desmontaje antes de particionar
        if ! force_unmount_disk "$DISK"; then
            error "Cannot proceed: disk $DISK has partitions that could not be unmounted"
        fi

        sleep 2

        # Particionar (actualizar función para soportar /boot separado)
        partition_disk "$DISK" "$UEFI_MODE" "$ADD_SWAP" "$SWAP_SIZE"
    else
        echo "PROGRESS:15:Using manual partitions..."
        log "Using manual partitions"

        # Verificar particiones manuales existen
        for part in $ROOT_PART $HOME_PART $BOOT_PART $SWAP_PART $EFI_PART; do
            if [ -n "$part" ] && [ ! -b "$part" ]; then
                error "Partition $part not found"
            fi
        done

        # En modo manual, verificar que al menos ROOT_PART esté disponible
        if [ ! -b "$ROOT_PART" ]; then
            error "Root partition $ROOT_PART does not exist or is not a block device"
        fi
        DISK=$(echo "$ROOT_PART" | sed -E 's/([0-9]+|p[0-9]+)$//')
    fi

    # Paso 4: Montar particiones
    echo "PROGRESS:25:Mounting partitions..."
    mount_partitions "$ROOT_PART" "$HOME_PART" "$BOOT_PART" "$EFI_PART"

    # Paso 5: Copiar sistema
    echo "PROGRESS:30:Copying system files..."
    copy_system "$USERNAME"

    # Paso 6: Crear swapfile si se solicitó
    if [ "$CREATE_SWAPFILE" = "true" ] && [ "$SWAPFILE_SIZE" -gt 0 ]; then
        echo "PROGRESS:45:Creating swapfile..."
        create_swapfile "$SWAPFILE_SIZE"
    fi

    # Paso 7: Configurar locales
    echo "PROGRESS:55:Configuring locales..."
    configure_locales "$TIMEZONE" "$LANGUAGE" "$KEYBOARD" "${KEYBOARD_VARIANT:-}"

    # Paso 8: Crear fstab
    echo "PROGRESS:65:Creating fstab..."
    create_fstab

    # Paso 9: Configurar usuario
    echo "PROGRESS:75:Configuring user..."
    configure_user "$HOSTNAME" "$USERNAME" "$PASSWORD" "$AUTOLOGIN" "${ROOT_PASSWORD:-$PASSWORD}"

    # Paso 10: Instalar bootloader
    echo "PROGRESS:85:Installing bootloader..."
    install_bootloader "$DISK" "$EFI_PART"

    # Paso 11: Limpieza post-instalación
    echo "PROGRESS:90:Performing post-installation cleanup..."
    cleanup_post_install

    # Paso 12: Desmontar todo
    echo "PROGRESS:95:Unmounting partitions..."
    unmount_all

    # Paso 13: Limpiar archivos temporales
    rm -f "$RSYNC_EXCLUDES" 2>/dev/null || true

    echo "PROGRESS:100:Installation complete!"
    log "=== Installation completed successfully ==="
    echo "SUCCESS: LOC-OS has been installed"
    echo "You can now reboot and remove the installation media."
}

# ========== PUNTO DE ENTRADA ==========
case "${1:-install}" in
    "install")
        shift
        main_installation "$@"
        ;;
    "help"|"--help"|"-h")
        cat << EOF
LOC-OS Installer
Usage: $0 install [OPTIONS]

Required options:
  --disk=DEVICE          Installation disk (e.g., /dev/sda)
  --username=NAME        Username for the new system
  --hostname=NAME        Hostname for the new system
  --password=PASS        Password for the user
  --timezone=TZ          Timezone (e.g., America/New_York, Europe/Madrid)
  --language=LANG        System language (e.g., en_US, es_ES, fr_FR)
  --keyboard=LAYOUT      Keyboard layout (e.g., us, es, fr, de)

Optional options:
  --autologin=BOOL       Enable autologin (true/false, default: true)
  --sep-home=BOOL        Separate /home partition (true/false, default: false)
  --uefi-mode=MODE       UEFI mode: auto, uefi, bios (default: auto)
  --auto-partition=BOOL  Auto partition disk (true/false, default: true)
  --add-swap=BOOL        Add swap partition (true/false, default: false)
  --swap-size=MB         Swap size in MB when add-swap=true (default: 2048)

Manual partition options (when auto-partition=false):
  --root-part=DEV        Root partition (required)
  --home-part=DEV        Home partition
  --swap-part=DEV        Swap partition
  --efi-part=DEV         EFI partition

Examples:
  # Auto install with UEFI detection, no swap
  $0 install --disk=/dev/sda --username=john --hostname=mypc \
    --password=secret --timezone=America/New_York \
    --language=en_US --keyboard=us

  # With swap and separate home
  $0 install --disk=/dev/sda --username=john --hostname=mypc \
    --password=secret --timezone=Europe/Madrid \
    --language=es_ES --keyboard=es \
    --add-swap=true --sep-home=true

  # Manual partitions
  $0 install --disk=/dev/sda --auto-partition=false \
    --root-part=/dev/sda1 --swap-part=/dev/sda2 --home-part=/dev/sda3 \
    --username=john --hostname=mypc --password=secret \
    --timezone=UTC --language=en_US --keyboard=us
EOF
        ;;
    *)
        echo "Unknown command: $1"
        echo "Use: $0 install --help"
        exit 1
        ;;
esac
