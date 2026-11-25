################################################################################
#
# y2-mpv-compat: mpv auto-enables DVB input, which needs kernel headers newer than the 3.4 ones
# this toolchain uses. Not a package: only extra options for mpv (inert when mpv is not selected).
#
################################################################################

MPV_CONF_OPTS += -Ddvbin=disabled
