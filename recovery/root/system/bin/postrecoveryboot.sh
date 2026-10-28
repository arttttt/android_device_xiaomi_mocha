#!/system/bin/sh
#
# OrangeFox runs this late in its start-up, after OrangeFox_Startup().
#
# That function writes 0 to /proc/touchpanel/capacitive_keys_enable on every
# device which has the node, a habit kept from RedWolf. Here the node is
# enable_keys of the atmel touch controller, and at 0 the driver drops the
# back, home and menu keys under the panel. The GUI handles all three, so
# they are turned back on. The write has to come from here: runatboot.sh
# runs before OrangeFox_Startup() and would be undone by it.
echo 1 > /proc/touchpanel/capacitive_keys_enable
