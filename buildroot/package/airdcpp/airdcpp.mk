################################################################################
#
# airdcpp
#
################################################################################

AIRDCPP_VERSION = develop
AIRDCPP_SITE = https://github.com/airdcpp-web/airdcpp-webclient.git
AIRDCPP_SITE_METHOD = git

AIRDCPP_LICENSE = GPL
AIRDCPP_INSTALL_STAGING = YES
AIRDCPP_DEPENDENCIES = \
	boost \
	bzip2 \
	geoip \
	leveldb \
	libminiupnpc \
	openssl \
	websocketpp \
	zlib

$(eval $(cmake-package))
