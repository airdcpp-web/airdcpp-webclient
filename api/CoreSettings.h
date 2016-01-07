/*
* Copyright (C) 2011-2015 AirDC++ Project
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#ifndef DCPLUSPLUS_DCPP_CORESETTINGS_H
#define DCPLUSPLUS_DCPP_CORESETTINGS_H

#include <web-server/stdinc.h>

#include <api/ApiSettingItem.h>
#include <airdcpp/SettingsManager.h>

namespace webserver {
	static const vector<ApiSettingItem> coreSettings = {
		//{ ResourceManager::SETTINGS_GENERAL },
		{ "setting_profile", SettingsManager::SETTINGS_PROFILE, ResourceManager::SETTINGS_PROFILE },
		{ "nick", SettingsManager::NICK, ResourceManager::NICK },
		{ "description", SettingsManager::DESCRIPTION, ResourceManager::DESCRIPTION },
		{ "email", SettingsManager::EMAIL, ResourceManager::EMAIL },
		{ "upload_speed", SettingsManager::UPLOAD_SPEED, ResourceManager::SETCZDC_UPLOAD_SPEED, ApiSettingItem::TYPE_GENERAL, { ResourceManager::Strings::MBITS, false } },
		{ "download_speed", SettingsManager::DOWNLOAD_SPEED, ResourceManager::SETCZDC_DOWNLOAD_SPEED, ApiSettingItem::TYPE_GENERAL, { ResourceManager::Strings::MBITS, false } },
		{ "away_message", SettingsManager::DEFAULT_AWAY_MESSAGE, ResourceManager::SETTINGS_DEFAULT_AWAY_MSG, ApiSettingItem::TYPE_LONG_TEXT },
		{ "away_idle_time", SettingsManager::AWAY_IDLE_TIME, ResourceManager::AWAY_IDLE_TIME_BEGIN, ApiSettingItem::TYPE_GENERAL, { ResourceManager::Strings::MINUTES, false } },
		{ "away_no_bots", SettingsManager::NO_AWAYMSG_TO_BOTS, ResourceManager::SETTINGS_NO_AWAYMSG_TO_BOTS },
		{ "nmdc_encoding", SettingsManager::NMDC_ENCODING, ResourceManager::INVALID_ENCODING },

		//{ ResourceManager::SETTINGS_DOWNLOADS },
		{ "download_directory", SettingsManager::DOWNLOAD_DIRECTORY, ResourceManager::SETTINGS_DOWNLOAD_DIRECTORY, ApiSettingItem::TYPE_DIRECTORY_PATH },
		//{ "temp_dir", SettingsManager::TEMP_DOWNLOAD_DIRECTORY, ResourceManager::SETTINGS_UNFINISHED_DOWNLOAD_DIRECTORY },
		//{ "temp_use_dest", SettingsManager::DCTMP_STORE_DESTINATION, ResourceManager::UNFINISHED_STORE_DESTINATION },
		{ "segmented_downloads", SettingsManager::MULTI_CHUNK, ResourceManager::SETTINGS_SEGMENTED_DOWNLOADS },
		{ "min_segment_size", SettingsManager::MIN_SEGMENT_SIZE, ResourceManager::SETTINGS_AIRDOWNLOADS_SEGMENT_SIZE, ApiSettingItem::TYPE_GENERAL, { ResourceManager::Strings::KiB, false } },
		{ "new_segment_min_speed", SettingsManager::DONT_BEGIN_SEGMENT_SPEED, ResourceManager::DONT_ADD_SEGMENT_TEXT, ApiSettingItem::TYPE_GENERAL, { ResourceManager::Strings::KiB, true } },
		{ "allow_slow_overlap", SettingsManager::OVERLAP_SLOW_SOURCES, ResourceManager::SETTINGS_OVERLAP_SLOW_SOURCES },
		{ "share_finished_bundles", SettingsManager::ADD_FINISHED_INSTANTLY, ResourceManager::ADD_FINISHED_INSTANTLY },
		{ "finished_no_hash", SettingsManager::FINISHED_NO_HASH, ResourceManager::SETTINGS_FINISHED_NO_HASH },
		{ "finished_remove_exit", SettingsManager::REMOVE_FINISHED_BUNDLES, ResourceManager::BUNDLES_REMOVE_EXIT },

		//{ ResourceManager::SETTINGS_SKIPPING_OPTIONS },
		{ "dont_download_shared", SettingsManager::DONT_DL_ALREADY_SHARED, ResourceManager::SETTINGS_DONT_DL_ALREADY_SHARED },
		{ "dont_download_queued", SettingsManager::DONT_DL_ALREADY_QUEUED, ResourceManager::SETTING_DONT_DL_ALREADY_QUEUED },
		{ "download_dupe_min_size", SettingsManager::MIN_DUPE_CHECK_SIZE, ResourceManager::MIN_DUPE_CHECK_SIZE, ApiSettingItem::TYPE_GENERAL, { ResourceManager::Strings::KiB, false } },
		{ "download_skip_zero_byte", SettingsManager::SKIP_ZERO_BYTE, ResourceManager::SETTINGS_SKIP_ZERO_BYTE },
		{ "download_skiplist", SettingsManager::SKIPLIST_DOWNLOAD, ResourceManager::SETTINGS_SKIPLIST_DOWNLOAD },
		{ "download_skiplist_regex", SettingsManager::DOWNLOAD_SKIPLIST_USE_REGEXP, ResourceManager::USE_REGEXP },

		//{ ResourceManager::SETTINGS_SEARCH_MATCHING },
		{ "auto_add_sources", SettingsManager::AUTO_ADD_SOURCE, ResourceManager::AUTO_ADD_SOURCE },
		{ "alt_search_auto", SettingsManager::AUTO_SEARCH, ResourceManager::SETTINGS_AUTO_BUNDLE_SEARCH },
		{ "alt_search_max_sources", SettingsManager::AUTO_SEARCH_LIMIT, ResourceManager::SETTINGS_AUTO_SEARCH_LIMIT },
		{ "max_sources_match_queue", SettingsManager::MAX_AUTO_MATCH_SOURCES, ResourceManager::SETTINGS_SB_MAX_SOURCES },

		//{ ResourceManager::PROXIES },
		{ "http_proxy", SettingsManager::HTTP_PROXY, ResourceManager::SETTINGS_HTTP_PROXY },
		{ "outgoing_mode", SettingsManager::OUTGOING_CONNECTIONS, ResourceManager::SETTINGS_OUTGOING },
		{ "socks_server", SettingsManager::SOCKS_SERVER, ResourceManager::SETTINGS_SOCKS5_IP },
		{ "socks_user", SettingsManager::SOCKS_USER, ResourceManager::SETTINGS_SOCKS5_RESOLVE },
		{ "socks_password", SettingsManager::SOCKS_PASSWORD, ResourceManager::PASSWORD },
		{ "socks_port", SettingsManager::SOCKS_PORT, ResourceManager::SETTINGS_SOCKS5_PORT },
		{ "socks_resolve", SettingsManager::SOCKS_RESOLVE, ResourceManager::SETTINGS_SOCKS5_RESOLVE },

		//{ ResourceManager::IP_V4 },
		{ "connection_auto_v4", SettingsManager::AUTO_DETECT_CONNECTION, ResourceManager::ALLOW_AUTO_DETECT_V4 },
		{ "connection_bind_v4", SettingsManager::BIND_ADDRESS, ResourceManager::SETTINGS_BIND_ADDRESS, ApiSettingItem::TYPE_CONN_V4 },
		{ "connection_mode_v4", SettingsManager::INCOMING_CONNECTIONS, ResourceManager::CONNECTIVITY, ApiSettingItem::TYPE_CONN_V4 },
		{ "connection_ip_v4", SettingsManager::EXTERNAL_IP, ResourceManager::SETTINGS_EXTERNAL_IP, ApiSettingItem::TYPE_CONN_V4 },
		{ "connection_update_ip_v4", SettingsManager::IP_UPDATE, ResourceManager::UPDATE_IP, ApiSettingItem::TYPE_CONN_V4 },
		{ "connection_ip_override_v4", SettingsManager::NO_IP_OVERRIDE, ResourceManager::SETTINGS_OVERRIDE, ApiSettingItem::TYPE_CONN_V4 },

		//{ ResourceManager::IP_V6 },
		{ "connection_auto_v6", SettingsManager::AUTO_DETECT_CONNECTION6, ResourceManager::ALLOW_AUTO_DETECT_V6 },
		{ "connection_bind_v6", SettingsManager::BIND_ADDRESS6, ResourceManager::SETTINGS_BIND_ADDRESS, ApiSettingItem::TYPE_CONN_V6 },
		{ "connection_mode_v6", SettingsManager::INCOMING_CONNECTIONS6, ResourceManager::CONNECTIVITY, ApiSettingItem::TYPE_CONN_V6 },
		{ "connection_ip_v6", SettingsManager::EXTERNAL_IP6, ResourceManager::SETTINGS_EXTERNAL_IP, ApiSettingItem::TYPE_CONN_V6 },
		{ "connection_update_ip_v6", SettingsManager::IP_UPDATE6, ResourceManager::UPDATE_IP, ApiSettingItem::TYPE_CONN_V6 },
		{ "connection_ip_override_v6", SettingsManager::NO_IP_OVERRIDE6, ResourceManager::SETTINGS_OVERRIDE, ApiSettingItem::TYPE_CONN_V6 },

		//{ ResourceManager::SETTINGS_PORTS },
		{ "tcp_port", SettingsManager::TCP_PORT, ResourceManager::SETTINGS_TCP_PORT, ApiSettingItem::TYPE_CONN_GEN },
		{ "udp_port", SettingsManager::UDP_PORT, ResourceManager::SETTINGS_UDP_PORT, ApiSettingItem::TYPE_CONN_GEN },
		{ "tls_port", SettingsManager::TLS_PORT, ResourceManager::SETTINGS_TLS_PORT, ApiSettingItem::TYPE_CONN_GEN },
		{ "preferred_port_mapper", SettingsManager::MAPPER, ResourceManager::PREFERRED_MAPPER , ApiSettingItem::TYPE_CONN_GEN },

		//{ ResourceManager::DOWNLOAD_LIMITS },
		{ "download_auto_limits", SettingsManager::DL_AUTODETECT, ResourceManager::AUTODETECT },
		{ "download_slots", SettingsManager::DOWNLOAD_SLOTS, ResourceManager::SETTINGS_DOWNLOADS_MAX, ApiSettingItem::TYPE_LIMITS_DL },
		{ "download_max_start_speed", SettingsManager::MAX_DOWNLOAD_SPEED, ResourceManager::SETTINGS_DOWNLOADS_SPEED_PAUSE, ApiSettingItem::TYPE_LIMITS_DL, { ResourceManager::Strings::KiB, true } },
		{ "download_highest_prio_slots", SettingsManager::EXTRA_DOWNLOAD_SLOTS, ResourceManager::SETTINGS_CZDC_EXTRA_DOWNLOADS },

		//{ ResourceManager::UPLOAD_LIMITS },
		{ "upload_auto_limits", SettingsManager::UL_AUTODETECT, ResourceManager::AUTODETECT },
		{ "upload_auto_grant_speed", SettingsManager::MIN_UPLOAD_SPEED, ResourceManager::SETTINGS_UPLOADS_MIN_SPEED, ApiSettingItem::TYPE_LIMITS_UL, { ResourceManager::Strings::KiB, true } },
		{ "upload_max_granted", SettingsManager::AUTO_SLOTS, ResourceManager::SETTINGS_AUTO_SLOTS, ApiSettingItem::TYPE_LIMITS_UL },
		{ "upload_slots", SettingsManager::SLOTS, ResourceManager::SETTINGS_UPLOADS_SLOTS, ApiSettingItem::TYPE_LIMITS_UL },
		{ "upload_minislot_size", SettingsManager::SET_MINISLOT_SIZE, ResourceManager::SETCZDC_SMALL_FILES, ApiSettingItem::TYPE_GENERAL, { ResourceManager::Strings::KiB, false } },
		{ "upload_minislot_ext", SettingsManager::FREE_SLOTS_EXTENSIONS, ResourceManager::ST_MINISLOTS_EXT },

		//{ ResourceManager::SETTINGS_MCNSLOTS },
		{ "mcn_auto_limits", SettingsManager::MCN_AUTODETECT , ResourceManager::AUTODETECT },
		{ "mcn_down", SettingsManager::MAX_MCN_DOWNLOADS, ResourceManager::SETTINGS_MAX_MCN_DL, ApiSettingItem::TYPE_LIMITS_MCN },
		{ "mcn_up", SettingsManager::MAX_MCN_UPLOADS, ResourceManager::SETTINGS_MAX_MCN_UL, ApiSettingItem::TYPE_LIMITS_MCN },

		//{ ResourceManager::TRASFER_RATE_LIMITING },
		{ "upload_limit_main", SettingsManager::MAX_UPLOAD_SPEED_MAIN, ResourceManager::UPLOAD_LIMIT, ApiSettingItem::TYPE_GENERAL, { ResourceManager::Strings::KiB, true } },
		{ "download_limit_main", SettingsManager::MAX_DOWNLOAD_SPEED_MAIN, ResourceManager::DOWNLOAD_LIMIT, ApiSettingItem::TYPE_GENERAL, { ResourceManager::Strings::KiB, true } },
		{ "limit_use_alt", SettingsManager::TIME_DEPENDENT_THROTTLE, ResourceManager::ALTERNATE_LIMITING },
		{ "limit_alt_start_hour", SettingsManager::BANDWIDTH_LIMIT_START, ResourceManager::SET_ALTERNATE_LIMITING },
		{ "limit_alt_end_hour", SettingsManager::BANDWIDTH_LIMIT_END, ResourceManager::SET_ALTERNATE_LIMITING },
		{ "limit_ul_alt_max", SettingsManager::MAX_UPLOAD_SPEED_ALTERNATE, ResourceManager::UPLOAD_LIMIT, ApiSettingItem::TYPE_GENERAL, { ResourceManager::Strings::KiB, true } },
		{ "limit_dl_alt_max", SettingsManager::MAX_UPLOAD_SPEED_ALTERNATE, ResourceManager::DOWNLOAD_LIMIT, ApiSettingItem::TYPE_GENERAL, { ResourceManager::Strings::KiB, true } },
		{ "limit_use_with_auto_values", SettingsManager::AUTO_DETECTION_USE_LIMITED, ResourceManager::DOWNLOAD_LIMIT },

		//{ ResourceManager::HASHING_OPTIONS },
		{ "max_hash_speed", SettingsManager::MAX_HASH_SPEED, ResourceManager::SETTINGS_MAX_HASHER_SPEED, ApiSettingItem::TYPE_GENERAL, { ResourceManager::Strings::MiB, true } },
		{ "max_total_hashers", SettingsManager::MAX_HASHING_THREADS, ResourceManager::MAX_HASHING_THREADS },
		{ "max_volume_hashers", SettingsManager::HASHERS_PER_VOLUME, ResourceManager::MAX_VOL_HASHERS },
		{ "report_each_hashed_file", SettingsManager::LOG_HASHING, ResourceManager::LOG_HASHING },

		//{ ResourceManager::REFRESH_OPTIONS },
		{ "refresh_time", SettingsManager::AUTO_REFRESH_TIME, ResourceManager::SETTINGS_AUTO_REFRESH_TIME, ApiSettingItem::TYPE_GENERAL, { ResourceManager::Strings::MINUTES, false } },
		{ "refresh_time_incoming", SettingsManager::INCOMING_REFRESH_TIME, ResourceManager::SETTINGS_INCOMING_REFRESH_TIME, ApiSettingItem::TYPE_GENERAL, { ResourceManager::Strings::MINUTES, false } },
		{ "refresh_startup", SettingsManager::STARTUP_REFRESH, ResourceManager::SETTINGS_STARTUP_REFRESH },
		{ "refresh_report_scheduled_refreshes", SettingsManager::LOG_SCHEDULED_REFRESHES, ResourceManager::SETTINGS_LOG_SCHEDULED_REFRESHES },

		//{ ResourceManager::SETTINGS_SHARING_OPTIONS },
		{ "share_skiplist", SettingsManager::SKIPLIST_SHARE, ResourceManager::ST_SKIPLIST_SHARE },
		{ "share_skiplist_regex", SettingsManager::SHARE_SKIPLIST_USE_REGEXP, ResourceManager::USE_REGEXP },
		{ "share_hidden", SettingsManager::SHARE_HIDDEN, ResourceManager::SETTINGS_SHARE_HIDDEN },
		{ "share_no_empty_dirs", SettingsManager::SKIP_EMPTY_DIRS_SHARE, ResourceManager::DONT_SHARE_EMPTY_DIRS },
		{ "share_no_zero_byte", SettingsManager::NO_ZERO_BYTE, ResourceManager::SETTINGS_NO_ZERO_BYTE },
		{ "share_max_size", SettingsManager::MAX_FILE_SIZE_SHARED, ResourceManager::DONT_SHARE_BIGGER_THAN, ApiSettingItem::TYPE_GENERAL, { ResourceManager::Strings::MiB, false } },
		{ "share_follow_symlinks", SettingsManager::SHARE_FOLLOW_SYMLINKS, ResourceManager::FOLLOW_SYMLINKS },
		{ "share_report_duplicates", SettingsManager::FL_REPORT_FILE_DUPES, ResourceManager::REPORT_DUPLICATE_FILES },
		{ "share_report_skiplist", SettingsManager::REPORT_SKIPLIST, ResourceManager::REPORT_SKIPLIST },

		//{ ResourceManager::SETTINGS_LOGGING },
		{ "log_directory", SettingsManager::LOG_DIRECTORY, ResourceManager::SETTINGS_LOG_DIR, ApiSettingItem::TYPE_DIRECTORY_PATH },
		{ "log_main", SettingsManager::LOG_MAIN_CHAT, ResourceManager::SETTINGS_LOG_MAIN_CHAT },
		{ "log_main_file", SettingsManager::LOG_FILE_MAIN_CHAT, ResourceManager::FILENAME },
		{ "log_main_format", SettingsManager::LOG_FORMAT_MAIN_CHAT, ResourceManager::SETTINGS_FORMAT },
		{ "log_pm", SettingsManager::LOG_PRIVATE_CHAT, ResourceManager::SETTINGS_LOG_PRIVATE_CHAT },
		{ "log_pm_file", SettingsManager::LOG_FILE_PRIVATE_CHAT, ResourceManager::FILENAME },
		{ "log_pm_format", SettingsManager::LOG_FORMAT_PRIVATE_CHAT, ResourceManager::SETTINGS_FORMAT },
		{ "log_downloads", SettingsManager::LOG_DOWNLOADS, ResourceManager::SETTINGS_LOG_DOWNLOADS },
		{ "log_downloads_file", SettingsManager::LOG_FILE_DOWNLOAD, ResourceManager::FILENAME },
		{ "log_downloads_format", SettingsManager::LOG_FORMAT_POST_DOWNLOAD, ResourceManager::SETTINGS_FORMAT },
		{ "log_uploads", SettingsManager::LOG_UPLOADS, ResourceManager::SETTINGS_LOG_UPLOADS },
		{ "log_uploads_file", SettingsManager::LOG_FILE_UPLOAD, ResourceManager::FILENAME },
		{ "log_uploads_format", SettingsManager::LOG_FORMAT_POST_UPLOAD, ResourceManager::SETTINGS_FORMAT },
		{ "log_syslog", SettingsManager::LOG_SYSTEM, ResourceManager::SETTINGS_LOG_SYSTEM_MESSAGES },
		{ "log_syslog_file", SettingsManager::LOG_FILE_SYSTEM, ResourceManager::FILENAME },
		{ "log_syslog_format", SettingsManager::LOG_FORMAT_SYSTEM, ResourceManager::SETTINGS_FORMAT },
		{ "log_status", SettingsManager::LOG_STATUS_MESSAGES, ResourceManager::SETTINGS_LOG_STATUS_MESSAGES },
		{ "log_status_file", SettingsManager::LOG_FILE_STATUS, ResourceManager::FILENAME },
		{ "log_status_format", SettingsManager::LOG_FORMAT_STATUS, ResourceManager::SETTINGS_FORMAT },
		{ "log_list_transfers", SettingsManager::LOG_FILELIST_TRANSFERS, ResourceManager::SETTINGS_LOG_FILELIST_TRANSFERS },
		{ "single_log_per_cid", SettingsManager::PM_LOG_GROUP_CID, ResourceManager::LOG_COMBINE_ADC_PM },

		//{ ResourceManager::HISTORIES },
		{ "history_search_max", SettingsManager::HISTORY_SEARCH_MAX, ResourceManager::SEARCH_STRINGS },
		{ "history_search_clear_exit", SettingsManager::HISTORY_SEARCH_CLEAR, ResourceManager::CLEAR_EXIT },
		{ "history_download_max", SettingsManager::HISTORY_DIR_MAX, ResourceManager::SETTINGS_DOWNLOAD_LOCATIONS },
		{ "history_download_clear_exit", SettingsManager::HISTORY_DIR_CLEAR, ResourceManager::CLEAR_EXIT },
		//{ "history_last_pm_lines", SettingsManager::SHOW_LAST_LINES_LOG, ResourceManager::MAX_LOG_LINES },
		{ "history_pm_messages", SettingsManager::PM_MESSAGE_CACHE, ResourceManager::PRIVATE_CHAT },
		{ "history_hub_messages", SettingsManager::HUB_MESSAGE_CACHE , ResourceManager::HUBS },
		{ "history_log_messages", SettingsManager::LOG_MESSAGE_CACHE , ResourceManager::SYSTEM_LOG },

		//{ ResourceManager::SETTINGS_AIR_TABSPAGE },
		{ "open_transfers", SettingsManager::SHOW_TRANSFERVIEW, ResourceManager::MENU_TRANSFERS },
		{ "open_hublist", SettingsManager::OPEN_PUBLIC, ResourceManager::MAX_HASHING_THREADS },
		{ "open_favorites", SettingsManager::OPEN_FAVORITE_HUBS, ResourceManager::MAX_VOL_HASHERS },
		{ "open_queue", SettingsManager::OPEN_QUEUE, ResourceManager::MAX_VOL_HASHERS },

		//{ ResourceManager::SETTINGS_ADVANCED },
		{ "socket_read_buffer", SettingsManager::SOCKET_IN_BUFFER, ResourceManager::SETTINGS_SOCKET_IN_BUFFER },
		{ "socket_write_buffer", SettingsManager::SOCKET_OUT_BUFFER, ResourceManager::SETTINGS_SOCKET_OUT_BUFFER },
		{ "buffer_size", SettingsManager::BUFFER_SIZE, ResourceManager::SETTINGS_WRITE_BUFFER },
		{ "compress_transfers", SettingsManager::COMPRESS_TRANSFERS, ResourceManager::SETTINGS_COMPRESS_TRANSFERS },
		{ "max_compression", SettingsManager::MAX_COMPRESSION, ResourceManager::SETTINGS_MAX_COMPRESS },
		{ "bloom_mode", SettingsManager::BLOOM_MODE, ResourceManager::BLOOM_MODE },
		{ "disconnect_offline_users", SettingsManager::AUTO_KICK, ResourceManager::SETTINGS_AUTO_KICK },

		{ "always_ccpm", SettingsManager::ALWAYS_CCPM, ResourceManager::ALWAYS_CCPM },
		{ "tls_mode", SettingsManager::TLS_MODE, ResourceManager::TRANSFER_ENCRYPTION },
	};
}

#endif