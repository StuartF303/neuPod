################################################################################
#
# y2-fbtool
#
################################################################################

Y2_FBTOOL_VERSION = 1.0
Y2_FBTOOL_SITE = $(BR2_EXTERNAL_Y2_PATH)/package/y2-fbtool/src
Y2_FBTOOL_SITE_METHOD = local
Y2_FBTOOL_LICENSE = MIT

define Y2_FBTOOL_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) -static -Wall -Wextra \
		-o $(@D)/fbtool $(@D)/fbtool.c
endef

define Y2_FBTOOL_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/fbtool $(TARGET_DIR)/usr/bin/fbtool
endef

$(eval $(generic-package))
