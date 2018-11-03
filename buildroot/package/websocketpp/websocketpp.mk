################################################################################
#
## websocketpp
#
#################################################################################

WEBSOCKETPP_VERSION = 0.8.1
WEBSOCKETPP_SITE = $(call github,zaphoyd,websocketpp,$(WEBSOCKETPP_VERSION))
WEBSOCKETPP_INSTALL_STAGING = YES
WEBSOCKETPP_LICENSE = BSD or MIT
WEBSOCKETPP_LICENSE_FILES = COPYING

WEBSOCKETPP_VERSION_CONF_OPTS = \
	CC="$(TARGET_CC)" CFLAGS="$(TARGET_CFLAGS)" \
	LDFLAGS="$(TARGET_LDFLAGS)"

define WEBSOCKETPP_INSTALL_STAGING_CMDS
	cp -vR $(@D)/websocketpp $(STAGING_DIR)/usr/include/websocketpp
endef

$(eval $(generic-package))

