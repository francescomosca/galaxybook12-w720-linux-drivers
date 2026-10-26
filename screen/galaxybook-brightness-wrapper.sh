#!/usr/bin/env bash
CONFIG_FILE="/etc/galaxybook-colorprofile.conf"
PROFILE="3"
if [ -f "$CONFIG_FILE" ]; then
    PROFILE=$(cat "$CONFIG_FILE" | tr -d ' \n')
fi
exec /usr/local/bin/galaxybook-brightness watch "$PROFILE"
