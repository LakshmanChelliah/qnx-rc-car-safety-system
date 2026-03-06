#!/bin/ksh

APP_DIR="/fs/apps"
TIMEOUT_SECS=20

# --- ARGUMENT PARSING ---
# Flags and positional args are collected in any order.
# Usage: reload_pi_car_apps.sh [--boot-mode] [--skip-slay] [--logs] [app1 app2 ...]
BOOT_MODE=0
SKIP_SLAY=0
LOGS=0

while [ $# -gt 0 ]; do
    case "$1" in
        --boot-mode)  BOOT_MODE=1;  shift ;;
        --skip-slay)  SKIP_SLAY=1;  shift ;;
        --logs)       LOGS=1;       shift ;;
        --)           shift;        break ;;
        -*)           echo "[ERROR] Unknown option: $1"; exit 1 ;;
        *)            break ;;
    esac
done

# Remaining positional arguments are the app names to manage
APPS_TO_MANAGE="$@"

# --- LOGGING SETUP ---
sleep 3
if [ $LOGS -eq 1 ]; then
    mkdir -p /var/logs
    exec >> /var/logs/reload_pi_car_apps.sh.log 2>&1
else
    echo "[INFO] Running without --logs. Console output enabled."
fi

echo "======================================================="
echo "[$(date '+%Y-%m-%d %H:%M:%S')] Pi Car Launcher Invoked"
echo "======================================================="
echo "  --boot-mode : $BOOT_MODE"
echo "  --skip-slay : $SKIP_SLAY"
echo "  --logs      : $LOGS"
echo "  apps        : $@"
echo "-------------------------------------------------------"

# ==============================================================================
# BOOTSTRAP LOGIC
# ==============================================================================
if [ $BOOT_MODE -eq 1 ]; then
    echo "[BOOT] Initializing persistent environment..."

    # 1. Wait for and mount the persistent QNX6 partition
    echo "[BOOT] Waiting for QNX6 partition..."
    waitfor /dev/sd0t77 5
    if mount -v -t qnx6 /dev/sd0t77 /fs/apps; then
        chown -R qnxuser:qnxuser /fs/apps
        echo "[BOOT] Success: Persistent storage mounted at /fs/apps."
    else
        echo "[BOOT] Notice: No QNX6 partition found. /fs/apps is using volatile RAM."
    fi

    # 2. Check for script updates from FAT32 and copy them to QNX6
    if [ ! -f /fs/apps/reload_pi_car_apps.sh ]; then
        echo "[BOOT] Copying orchestrator to /fs/apps..."
        cp /fs/boot/reload_pi_car_apps.sh /fs/apps/
        chmod +x /fs/apps/reload_pi_car_apps.sh
    fi

    # 3. Trigger the C++ Launcher
    if [ -x /fs/apps/reload_pi_car_apps.aarch64le.bin ]; then
        echo "[BOOT] Starting Pi Car Apps Launcher..."
        /fs/apps/reload_pi_car_apps.aarch64le.bin --drive --skip-slay --logs > /var/logs/reload_pi_car_apps.aarch64le.bin.log 2>&1 &
    else
        echo "[BOOT] Notice: C++ Launcher not found or not executable."
    fi

    echo "[BOOT] Bootstrap complete. Exiting boot thread."
    exit 0
fi
# ==============================================================================

if [ -z "$APPS_TO_MANAGE" ]; then
    echo "[ERROR] No applications provided to manage. Check launcher logic."
    exit 1
fi

# --- 1. ARCHITECTURE SAFETY CHECK ---
ARCH=$(uname -m)
if [ "$ARCH" != "RaspberryPi4B" ]; then
    echo "[FATAL ERROR] Architecture mismatch!"
    echo "This script is designed for the Raspberry Pi 4B, but detected: $ARCH"
    echo "Aborting to protect host machine."
    exit 1
fi

echo "--- Initiating App Reload for Amazon Pi Car ---"

# --- SHUTDOWN FUNCTION ---
shutdown_app() {
    APP_NAME=$1
    echo "Attempting to stop $APP_NAME..."
    
    slay -s SIGTERM "$APP_NAME" >/dev/null 2>&1
    
    elapsed=0
    while [ $elapsed -lt $TIMEOUT_SECS ]; do
        if ! pidin | grep -q "$APP_NAME"; then
            echo "[OK] $APP_NAME shut down cleanly."
            return 0
        fi
        sleep 1
        elapsed=$((elapsed + 1))
    done
    
    echo "[ERROR] $APP_NAME did not shut down after $TIMEOUT_SECS seconds!"
    echo "Aborting system restart to prevent hardware conflicts."
    exit 1 
}

# --- STARTUP FUNCTION ---
start_app() {
    APP_PATH=$1
    APP_NAME=${APP_PATH##*/} 
    
    echo "Starting $APP_NAME..."
    $APP_PATH > /dev/null 2>&1 &
    APP_PID=$! 
    
    sleep 1 
    
    if kill -0 $APP_PID >/dev/null 2>&1; then
        echo "[OK] $APP_NAME is running (PID: $APP_PID)."
    else
        echo "[ERROR] $APP_NAME crashed or failed to start!"
        echo "Aborting orchestration."
        exit 1
    fi
}

# --- 2. SHUTDOWN PHASE ---
if [ $SKIP_SLAY -eq 0 ]; then
    for app in $APPS_TO_MANAGE; do
        if [ -n "$app" ]; then
            shutdown_app "$app"
        fi
    done
else
    echo "Skipping shutdown phase (--skip-slay engaged)."
fi

# --- 3. FILE PREP ---
echo "Applying execute permissions..."
chmod +x $APP_DIR/*.bin

# --- 4. STARTUP PHASE ---
for app in $APPS_TO_MANAGE; do
    if [ -n "$app" ]; then
        start_app "$APP_DIR/$app"
    fi
done

echo "--- Car App Reload Complete ---"
exit 0