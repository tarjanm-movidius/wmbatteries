#!/bin/sh

. /sys/class/power_supply/BAT0/uevent 2>/dev/null
echo "BAT0 health: $(($POWER_SUPPLY_ENERGY_FULL * 100 / $POWER_SUPPLY_ENERGY_FULL_DESIGN))%"
