#!/bin/bash
# get-system-info.sh - Get system information for installer

set -e

MODE="${1:-list}"

get_timezones() {
    if command -v timedatectl &> /dev/null; then
        timedatectl list-timezones
    else
        # Show all timezones
        if [ -d "/usr/share/zoneinfo" ]; then
            find /usr/share/zoneinfo -type f ! -name "*.tab" ! -name "*.list" ! -name "+VERSION" | \
                sed 's|/usr/share/zoneinfo/||' | sort
        else
            echo "UTC"
            echo "America/New_York"
            echo "Europe/London"
            echo "Asia/Tokyo"
            echo "Australia/Sydney"
        fi
    fi
}

get_keyboard_layouts() {
    # Try to get layouts with descriptions from XKB rules
    local found=0

    # Try base.lst first, then evdev.lst
    for rules_file in /usr/share/X11/xkb/rules/base.lst /usr/share/X11/xkb/rules/evdev.lst; do
        if [ -f "$rules_file" ]; then
            # Extract layout section with code and description
            awk '
                /^! layout/ { in_layout=1; next }
                /^!/ { in_layout=0 }
                in_layout && NF >= 2 {
                    code = $1
                    $1 = ""
                    desc = $0
                    gsub(/^[[:space:]]+/, "", desc)
                    printf "%s - %s\n", code, desc
                }
            ' "$rules_file" | sort -u
            found=1
            break
        fi
    done

    # Fallback if XKB rules not found
    if [ $found -eq 0 ]; then
        if [ -f "/usr/share/kbd/keymaps/i386/qwerty" ]; then
            find /usr/share/kbd/keymaps/i386 -name "*.map.gz" | \
                sed 's|.*/||;s|\.map\.gz$||' | \
                awk '{printf "%s - %s\n", $0, $0}' | sort
        elif command -v localectl &> /dev/null; then
            localectl list-keymaps | awk '{printf "%s - %s\n", $0, $0}'
        else
            # Fallback common layouts with descriptions
            echo "us - English (US)"
            echo "es - Spanish"
            echo "fr - French"
            echo "de - German"
            echo "it - Italian"
            echo "pt - Portuguese"
            echo "ru - Russian"
            echo "uk - English (UK)"
            echo "latam - Spanish (Latin America)"
            echo "br - Portuguese (Brazil)"
        fi
    fi
}

get_keyboard_variants() {
    local layout="$1"

    if [ -z "$layout" ]; then
        echo "Error: Layout code required" >&2
        return 1
    fi

    # Try base.lst first, then evdev.lst
    for rules_file in /usr/share/X11/xkb/rules/base.lst /usr/share/X11/xkb/rules/evdev.lst; do
        if [ -f "$rules_file" ]; then
            # Extract variant section for the specified layout
            awk -v layout="$layout" '
                /^! variant/ { in_variant=1; next }
                /^!/ { in_variant=0 }
                in_variant && NF >= 2 {
                    # Check if this line belongs to our layout
                    if ($0 ~ layout ":") {
                        # Extract variant code (before the colon or at start of line)
                        if ($1 ~ /:/) {
                            # Format: "layout: description"
                            # This is the base variant (no specific code)
                            print "default - " substr($0, index($0, ":") + 2)
                        } else {
                            # Format: "variant_code  layout: description"
                            variant = $1
                            rest = substr($0, index($0, layout ":") + length(layout) + 2)
                            gsub(/^[[:space:]]+/, "", rest)
                            print variant " - " rest
                        }
                    }
                }
            ' "$rules_file"
            return 0
        fi
    done

    # Fallback if XKB rules not found
    echo "default - Default variant"
    return 1
}

get_languages() {
    # Method 1: Use /usr/share/i18n/SUPPORTED (best)
    if [ -f "/usr/share/i18n/SUPPORTED" ]; then
        grep "UTF-8" /usr/share/i18n/SUPPORTED | \
            cut -d' ' -f1 | \
            sed 's/\.UTF-8//' | \
            sort -u
        return 0
    fi

    # Method 2: Show all possible locales from locale.gen (commented or not)
    if [ -f "/etc/locale.gen" ]; then
        # Extract all locale patterns, remove .UTF-8, get unique
        grep -oE '[a-z]{2}_[A-Z]{2}(\.[A-Z0-9-]+)?' /etc/locale.gen | \
            sed 's/\.[^.]*$//' | \
            sort -u
        return 0
    fi

    # Method 3: Check installed locales
    if command -v locale &> /dev/null; then
        locale -a 2>/dev/null | grep -E '^[a-z]{2}_[A-Z]{2}' | sort -u
        return 0
    fi

    # Final fallback
    echo "en_US"
    echo "es_ES"
    echo "fr_FR"
    echo "de_DE"
    echo "it_IT"
    echo "pt_BR"
    echo "ru_RU"
}

get_disks() {
    # Get disks with better info
    if command -v lsblk &> /dev/null; then
        lsblk -d -n -o NAME,SIZE,MODEL,TYPE,TRAN 2>/dev/null | grep -E '^(sd|nvme|mmcblk|vd)' | \
        while read -r line; do
            name=$(echo "$line" | awk '{print $1}')
            size=$(echo "$line" | awk '{print $2}')
            model=$(echo "$line" | awk '{$1=$2=""; print $0}' | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')
            [ -z "$model" ] && model="Unknown"
            echo "/dev/$name|$size|$model"
        done
    else
        # Fallback using /proc/partitions
        cat /proc/partitions | awk 'NR>2 && $4 !~ /[0-9]$/ {print "/dev/" $4 "|" $3 "|Unknown"}' | grep -E '^(/dev/sd|/dev/nvme|/dev/mmcblk|/dev/vd)'
    fi
}

get_partitions() {
    if command -v lsblk &> /dev/null; then
        lsblk -n -o NAME,TYPE,SIZE,FSTYPE,MOUNTPOINT | grep 'part' | \
        while read -r line; do
            name=$(echo "$line" | awk '{print $1}')
            type=$(echo "$line" | awk '{print $2}')
            size=$(echo "$line" | awk '{print $3}')
            fstype=$(echo "$line" | awk '{print $4}')
            mount=$(echo "$line" | awk '{$1=$2=$3=$4=""; print $0}' | sed -e 's/^[[:space:]]*//')

            if [ -n "$mount" ] && [ "$mount" != "" ]; then
                echo "/dev/$name|$size|$fstype|$mount"
            else
                echo "/dev/$name|$size|$fstype|"
            fi
        done
    else
        # Fallback usando /proc/partitions
        cat /proc/partitions | awk 'NR>2 && $4 ~ /[0-9]$/ {print "/dev/" $4 "|" $3 " blocks|unknown|"}' | \
        grep -E '^(/dev/sd|/dev/nvme|/dev/mmcblk|/dev/vd)'
    fi
}

get_current_timezone() {
    if [ -f "/etc/timezone" ]; then
        cat /etc/timezone
    elif command -v timedatectl &> /dev/null; then
        timedatectl show --property=Timezone --value 2>/dev/null || echo "UTC"
    else
        if [ -f "/etc/localtime" ]; then
            ls -l /etc/localtime | sed 's|.*/zoneinfo/||'
        else
            echo "UTC"
        fi
    fi
}

get_current_keyboard() {
    if command -v localectl &> /dev/null; then
        localectl status 2>/dev/null | grep "Layout" | awk -F': ' '{print $2}' | tr -d ' ' || echo "us"
    elif [ -f "/etc/default/keyboard" ]; then
        . /etc/default/keyboard 2>/dev/null
        echo "${XKBLAYOUT:-us}"
    else
        echo "us"
    fi
}

get_current_language() {
    if [ -n "$LANG" ]; then
        echo "$LANG" | cut -d. -f1
    else
        echo "en_US"
    fi
}

case "$MODE" in
    "env")
        echo "CURRENT_TIMEZONE=\"$(get_current_timezone)\""
        echo "CURRENT_KEYBOARD=\"$(get_current_keyboard)\""
        echo "CURRENT_LANGUAGE=\"$(get_current_language)\""
        echo "UEFI_BOOT=\"$([ -d /sys/firmware/efi ] && echo 1 || echo 0)\""
        ;;
    "timezones")
        get_timezones
        ;;
    "keyboards")
        get_keyboard_layouts
        ;;
    "variants")
        if [ -z "$2" ]; then
            echo "Usage: $0 variants <layout_code>"
            echo "Example: $0 variants latam"
            exit 1
        fi
        get_keyboard_variants "$2"
        ;;
    "languages")
        get_languages
        ;;
    "disks")
        get_disks
        ;;
    "partitions")
        get_partitions
        ;;
    "list")
        echo "=== Timezones ==="
        get_timezones
        echo ""
        echo "=== Keyboard Layouts ==="
        get_keyboard_layouts
        echo ""
        echo "=== Languages ==="
        get_languages
        echo ""
        echo "=== Disks ==="
        get_disks
        ;;
    "test")
        echo "=== System Information Test ==="
        echo "Current timezone: $(get_current_timezone)"
        echo "Current keyboard: $(get_current_keyboard)"
        echo "Current language: $(get_current_language)"
        echo "UEFI boot: $([ -d /sys/firmware/efi ] && echo "Yes" || echo "No")"
        echo ""
        echo "Timezones count: $(get_timezones | wc -l)"
        echo "Keyboard layouts count: $(get_keyboard_layouts | wc -l)"
        echo "Languages count: $(get_languages | wc -l)"
        echo "Disks count: $(get_disks | wc -l)"
        echo "Partitions: $(get_partitions | wc -l)"
        ;;
    *)
        echo "Usage: $0 {env|list|test|timezones|keyboards|variants|languages|disks|partitions}"
        echo "  env        - Output as environment variables"
        echo "  list       - Output lists for installer (default)"
        echo "  test       - Test and show counts"
        echo "  timezones  - Output only timezones"
        echo "  keyboards  - Output only keyboard layouts"
        echo "  variants   - Output variants for a specific layout (requires layout code)"
        echo "  languages  - Output only languages"
        echo "  disks      - Output only disks"
        echo "  partitions      - Output only partitions"
        exit 1
        ;;
esac
