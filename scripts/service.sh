#!/usr/bin/env bash
# Manage the tour-player service: it starts the tour automatically when the
# board powers on (no SSH needed), and restarts it if it ever crashes.
#
#   scripts/service.sh install     install and enable it (starts now and at every boot)
#   scripts/service.sh uninstall   remove it (the tour no longer starts at boot)
#   scripts/service.sh start | stop | restart | status
#   scripts/service.sh log         follow the tour's messages live (Ctrl-C to stop)
#
# Run scripts/deploy.sh first, so the programs and audio are on the board.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

action="${1:-}"
load_config
check_board_reachable

case "$action" in
    install)
        # Absolute path of BOARD_DIR on the board (it may be relative to the home folder).
        # shellcheck disable=SC2029
        dir="$(ssh "${SSH_OPTS[@]}" "$TARGET" "cd '$BOARD_DIR' && pwd")"
        if ! ssh "${SSH_OPTS[@]}" "$TARGET" "test -x '$dir/tour_player'"; then
            echo "No $dir/tour_player on the board yet - run scripts/deploy.sh first."
            exit 1
        fi
        unit="[Unit]
Description=SFU Burnaby campus tour player
After=sound.target

[Service]
Type=simple
WorkingDirectory=$dir
# Give the USB audio adapter a few seconds to appear after power-on.
ExecStartPre=/bin/sleep 5
ExecStart=$dir/tour_player --log $dir/tour.log
StandardInput=null
Restart=on-failure
RestartSec=3

[Install]
WantedBy=multi-user.target
"
        echo "Installing /etc/systemd/system/$SERVICE_NAME.service (runs $dir/tour_player) ..."
        # shellcheck disable=SC2029
        printf '%s' "$unit" | ssh "${SSH_OPTS[@]}" "$TARGET" "cat > /tmp/$SERVICE_NAME.service"
        ssh -t "${SSH_OPTS[@]}" "$TARGET" \
            "sudo mv /tmp/$SERVICE_NAME.service /etc/systemd/system/ && \
             sudo systemctl daemon-reload && \
             sudo systemctl enable --now $SERVICE_NAME"
        echo "Done. The tour now starts at every power-on."
        echo "  Messages:  scripts/service.sh log       Stop it:  scripts/service.sh stop"
        ;;
    uninstall)
        ssh -t "${SSH_OPTS[@]}" "$TARGET" \
            "sudo systemctl disable --now $SERVICE_NAME; \
             sudo rm -f /etc/systemd/system/$SERVICE_NAME.service; \
             sudo systemctl daemon-reload"
        echo "Removed. The tour no longer starts at boot."
        ;;
    start|stop|restart)
        ssh -t "${SSH_OPTS[@]}" "$TARGET" "sudo systemctl $action $SERVICE_NAME"
        ;;
    status)
        ssh -t "${SSH_OPTS[@]}" "$TARGET" "systemctl status --no-pager $SERVICE_NAME || true"
        ;;
    log)
        ssh -t "${SSH_OPTS[@]}" "$TARGET" "sudo journalctl -u $SERVICE_NAME -f -n 50"
        ;;
    *)
        echo "Usage: $0 install|uninstall|start|stop|restart|status|log"
        exit 1
        ;;
esac
