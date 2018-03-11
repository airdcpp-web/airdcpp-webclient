/*
* Copyright (C) 2011-2018 AirDC++ Project
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

#include <api/ApiSettingItem.h>
#include <airdcpp/SettingsManager.h>

namespace webserver {
	static vector<CoreSettingItem> coreSettings = {
		//{ ResourceManager::SETTINGS_GENERAL },
		{ "setting_profile", SettingsManager::SETTINGS_PROFILE, ResourceManager::SETTINGS_PROFILE },
		{ "nick", SettingsManager::NICK, ResourceManager::NICK },
		{ "description", SettingsManager::DESCRIPTION, ResourceManager::DESCRIPTION },
		{ "email", SettingsManager::EMAIL, ResourceManager::EMAIL },

		{ "upload_speed", SettingsManager::UPLOAD_SPEED, ResourceManager::SETCZDC_UPLOAD_SPEED, ApiSettingItem::TYPE_LAST, ResourceManager::Strings::MBITS },
		{ "download_speed", SettingsManager::DOWNLOAD_SPEED, ResourceManager::SETCZDC_DOWNLOAD_SPEED, ApiSettingItem::TYPE_LAST, ResourceManager::Strings::MBITS },

		{ "away_message", SettingsManager::DEFAULT_AWAY_MESSAGE, ResourceManager::SETTINGS_DEFAULT_AWAY_MSG, ApiSettingItem::TYPE_TEXT },
		{ "away_idle_time", SettingsManager::AWAY_IDLE_TIME, ResourceManager::AWAY_IDLE_TIME_BEGIN, ApiSettingItem::TYPE_LAST, ResourceManager::Strings::MINUTES_LOWER },
		{ "away_no_bots", SettingsManager::NO_AWAYMSG_TO_BOTS, ResourceManager::SETTINGS_NO_AWAYMSG_TO_BOTS },

		//{ ResourceManager::SETTINGS_DOWNLOADS },
		{ "download_directory", SettingsManager::DOWNLOAD_DIRECTORY, ResourceManager::SETTINGS_DOWNLOAD_DIRECTORY, ApiSettingItem::TYPE_DIRECTORY_PATH },
		{ "segmented_downloads", SettingsManager::MULTI_CHUNK, ResourceManager::SETTINGS_SEGMENTED_DOWNLOADS },
		{ "min_segment_size", SettingsManager::MIN_SEGMENT_SIZE, ResourceManager::SETTINGS_AIRDOWNLOADS_SEGMENT_SIZE, ApiSettingItem::TYPE_LAST, ResourceManager::Strings::KiB },
		{ "allow_slow_overlap", SettingsManager::OVERLAP_SLOW_SOURCES, ResourceManager::SETTINGS_OVERLAP_SLOW_SOURCES },
		{ "finished_remove_exit", SettingsManager::REMOVE_FINISHED_BUNDLES, ResourceManager::BUNDLES_REMOVE_EXIT },
		{ "use_partial_sharing", SettingsManager::USE_PARTIAL_SHARING, ResourceManager::PARTIAL_SHARING },

		//{ ResourceManager::SETTINGS_SKIPPING_OPTIONS },
		{ "dont_download_shared", SettingsManager::DONT_DL_ALREADY_SHARED, ResourceManager::SETTINGS_DONT_DL_ALREADY_SHARED },
		{ "dont_download_queued", SettingsManager::DONT_DL_ALREADY_QUEUED, ResourceManager::SETTING_DONT_DL_ALREADY_QUEUED },
		{ "download_dupe_min_size", SettingsManager::MIN_DUPE_CHECK_SIZE, ResourceManager::MIN_DUPE_CHECK_SIZE, ApiSettingItem::TYPE_LAST, ResourceManager::Strings::KiB },
		{ "download_skiplist", SettingsManager::SKIPLIST_DOWNLOAD, ResourceManager::SETTINGS_SKIPLIST_DOWNLOAD },
		{ "download_skiplist_regex", SettingsManager::DOWNLOAD_SKIPLIST_USE_REGEXP, ResourceManager::USE_REGEXP },

		//{ ResourceManager::SETTINGS_SEARCH_MATCHING },
		{ "auto_add_sources", SettingsManager::AUTO_ADD_SOURCE, ResourceManager::AUTO_ADD_SOURCE },
		{ "alt_search_auto", SettingsManager::AUTO_SEARCH, ResourceManager::SETTINGS_AUTO_BUNDLE_SEARCH },
		{ "alt_search_max_sources", SettingsManager::AUTO_SEARCH_LIMIT, ResourceManager::SETTINGS_AUTO_SEARCH_LIMIT },
		{ "max_sources_match_queue", SettingsManager::MAX_AUTO_MATCH_SOURCES, ResourceManager::SETTINGS_SB_MAX_SOURCES },
		{ "allow_match_full_list", SettingsManager::ALLOW_MATCH_FULL_LIST, ResourceManager::SETTINGS_ALLOW_MATCH_FULL_LIST },

		//{ ResourceManager::PROXIES },
		{ "http_proxy", SettingsManager::HTTP_PROXY, ResourceManager::SETTINGS_HTTP_PROXY },
		{ "outgoing_mode", SettingsManager::OUTGOING_CONNECTIONS, ResourceManager::SETTINGS_OUTGOING },
		{ "socks_server", SettingsManager::SOCKS_SERVER, ResourceManager::SETTINGS_SOCKS5_IP },
		{ "socks_user", SettingsManager::SOCKS_USER, ResourceManager::SETTINGS_SOCKS5_USERNAME },
		{ "socks_password", SettingsManager::SOCKS_PASSWORD, ResourceManager::PASSWORD },
		{ "socks_port", SettingsManager::SOCKS_PORT, ResourceManager::PORT },
		{ "socks_resolve", SettingsManager::SOCKS_RESOLVE, ResourceManager::SETTINGS_SOCKS5_RESOLVE },

		//{ ResourceManager::IP_V4 },
		{ "connection_auto_v4", SettingsManager::AUTO_DETECT_CONNECTION, ResourceManager::ALLOW_AUTO_DETECT_V4 },
		{ "connection_bind_v4", SettingsManager::BIND_ADDRESS, ResourceManager::SETTINGS_BIND_ADDRESS },
		{ "connection_mode_v4", SettingsManager::INCOMING_CONNECTIONS, ResourceManager::CONNECTIVITY },
		{ "connection_ip_v4", SettingsManager::EXTERNAL_IP, ResourceManager::SETTINGS_EXTERNAL_IP },
		{ "connection_update_ip_v4", SettingsManager::IP_UPDATE, ResourceManager::UPDATE_IP },
		{ "connection_ip_override_v4", SettingsManager::NO_IP_OVERRIDE, ResourceManager::SETTINGS_OVERRIDE },

		//{ ResourceManager::IP_V6 },
		{ "connection_auto_v6", SettingsManager::AUTO_DETECT_CONNECTION6, ResourceManager::ALLOW_AUTO_DETECT_V6 },
		{ "connection_bind_v6", SettingsManager::BIND_ADDRESS6, ResourceManager::SETTINGS_BIND_ADDRESS },
		{ "connection_mode_v6", SettingsManager::INCOMING_CONNECTIONS6, ResourceManager::CONNECTIVITY },
		{ "connection_ip_v6", SettingsManager::EXTERNAL_IP6, ResourceManager::SETTINGS_EXTERNAL_IP },
		{ "connection_update_ip_v6", SettingsManager::IP_UPDATE6, ResourceManager::UPDATE_IP },
		{ "connection_ip_override_v6", SettingsManager::NO_IP_OVERRIDE6, ResourceManager::SETTINGS_OVERRIDE },

		//{ ResourceManager::SETTINGS_PORTS },
		{ "tcp_port", SettingsManager::TCP_PORT, ResourceManager::SETTINGS_TCP_PORT },
		{ "udp_port", SettingsManager::UDP_PORT, ResourceManager::SETTINGS_UDP_PORT },
		{ "tls_port", SettingsManager::TLS_PORT, ResourceManager::SETTINGS_TLS_PORT },
		{ "preferred_port_mapper", SettingsManager::MAPPER, ResourceManager::PREFERRED_MAPPER },

		//{ ResourceManager::DOWNLOAD_LIMITS },
		{ "download_auto_limits", SettingsManager::DL_AUTODETECT, ResourceManager::AUTODETECT },
		{ "download_slots", SettingsManager::DOWNLOAD_SLOTS, ResourceManager::SETTINGS_DOWNLOADS_MAX },
		{ "download_max_start_speed", SettingsManager::MAX_DOWNLOAD_SPEED, ResourceManager::SETTINGS_DOWNLOADS_SPEED_PAUSE, ApiSettingItem::TYPE_LAST, ResourceManager::Strings::KiBS },
		{ "download_highest_prio_slots", SettingsManager::EXTRA_DOWNLOAD_SLOTS, ResourceManager::SETTINGS_CZDC_EXTRA_DOWNLOADS },

		{ "prio_high_files", SettingsManager::HIGH_PRIO_FILES, ResourceManager::SETTINGS_HIGH_PRIO_FILES },
		{ "prio_high_files_regex", SettingsManager::HIGHEST_PRIORITY_USE_REGEXP, ResourceManager::USE_REGEXP },
		{ "prio_highest_size", SettingsManager::PRIO_HIGHEST_SIZE, ResourceManager::SETTINGS_PRIO_HIGHEST, ApiSettingItem::TYPE_LAST, ResourceManager::Strings::KiB },
		{ "prio_high_size", SettingsManager::PRIO_HIGH_SIZE, ResourceManager::SETTINGS_PRIO_HIGH, ApiSettingItem::TYPE_LAST, ResourceManager::Strings::KiB },
		{ "prio_normal_size", SettingsManager::PRIO_NORMAL_SIZE, ResourceManager::SETTINGS_PRIO_NORMAL, ApiSettingItem::TYPE_LAST, ResourceManager::Strings::KiB },
		{ "prio_high_files_to_highest", SettingsManager::PRIO_LIST_HIGHEST, ResourceManager::SETTINGS_USE_HIGHEST_LIST },
		{ "prio_auto_default", SettingsManager::AUTO_PRIORITY_DEFAULT, ResourceManager::SETTINGS_AUTO_PRIORITY_DEFAULT },

		//{ ResourceManager::UPLOAD_LIMITS },
		{ "upload_auto_limits", SettingsManager::UL_AUTODETECT, ResourceManager::AUTODETECT },
		{ "upload_auto_grant_speed", SettingsManager::MIN_UPLOAD_SPEED, ResourceManager::SETTINGS_UPLOADS_MIN_SPEED, ApiSettingItem::TYPE_LAST, ResourceManager::Strings::KiBS },
		{ "upload_max_granted", SettingsManager::AUTO_SLOTS, ResourceManager::SETTINGS_AUTO_SLOTS },
		{ "upload_slots", SettingsManager::UPLOAD_SLOTS, ResourceManager::SETTINGS_UPLOADS_SLOTS },
		{ "upload_minislot_size", SettingsManager::SET_MINISLOT_SIZE, ResourceManager::SETCZDC_SMALL_FILES, ApiSettingItem::TYPE_LAST, ResourceManager::Strings::KiB },
		{ "upload_minislot_ext", SettingsManager::FREE_SLOTS_EXTENSIONS, ResourceManager::ST_MINISLOTS_EXT },

		//{ ResourceManager::SETTINGS_MCNSLOTS },
		{ "mcn_auto_limits", SettingsManager::MCN_AUTODETECT, ResourceManager::AUTODETECT },
		{ "mcn_down", SettingsManager::MAX_MCN_DOWNLOADS, ResourceManager::SETTINGS_MAX_MCN_DL },
		{ "mcn_up", SettingsManager::MAX_MCN_UPLOADS, ResourceManager::SETTINGS_MAX_MCN_UL },

		//{ ResourceManager::TRASFER_RATE_LIMITING },
		{ "upload_limit_main", SettingsManager::MAX_UPLOAD_SPEED_MAIN, ResourceManager::UPLOAD_LIMIT, ApiSettingItem::TYPE_LAST, ResourceManager::Strings::KiBS },
		{ "download_limit_main", SettingsManager::MAX_DOWNLOAD_SPEED_MAIN, ResourceManager::DOWNLOAD_LIMIT, ApiSettingItem::TYPE_LAST, ResourceManager::Strings::KiBS },
		{ "limit_use_alt", SettingsManager::TIME_DEPENDENT_THROTTLE, ResourceManager::ALTERNATE_LIMITING },
		{ "limit_alt_start_hour", SettingsManager::BANDWIDTH_LIMIT_START, ResourceManager::SET_ALTERNATE_LIMITING },
		{ "limit_alt_end_hour", SettingsManager::BANDWIDTH_LIMIT_END, ResourceManager::SET_ALTERNATE_LIMITING },
		{ "limit_ul_alt_max", SettingsManager::MAX_UPLOAD_SPEED_ALTERNATE, ResourceManager::UPLOAD_LIMIT, ApiSettingItem::TYPE_LAST, ResourceManager::Strings::KiBS },
		{ "limit_dl_alt_max", SettingsManager::MAX_DOWNLOAD_SPEED_ALTERNATE, ResourceManager::DOWNLOAD_LIMIT, ApiSettingItem::TYPE_LAST, ResourceManager::Strings::KiBS },
		{ "limit_use_with_auto_values", SettingsManager::AUTO_DETECTION_USE_LIMITED, ResourceManager::DOWNLOAD_LIMIT },

		//{ ResourceManager::HASHING_OPTIONS },
		{ "max_hash_speed", SettingsManager::MAX_HASH_SPEED, ResourceManager::SETTINGS_MAX_HASHER_SPEED, ApiSettingItem::TYPE_LAST, ResourceManager::Strings::MBPS },
		{ "max_total_hashers", SettingsManager::MAX_HASHING_THREADS, ResourceManager::MAX_HASHING_THREADS },
		{ "max_volume_hashers", SettingsManager::HASHERS_PER_VOLUME, ResourceManager::MAX_VOL_HASHERS },

		//{ ResourceManager::REFRESH_OPTIONS },
		{ "refresh_time", SettingsManager::AUTO_REFRESH_TIME, ResourceManager::SETTINGS_AUTO_REFRESH_TIME, ApiSettingItem::TYPE_LAST, ResourceManager::Strings::MINUTES_LOWER },
		{ "refresh_time_incoming", SettingsManager::INCOMING_REFRESH_TIME, ResourceManager::SETTINGS_INCOMING_REFRESH_TIME, ApiSettingItem::TYPE_LAST, ResourceManager::Strings::MINUTES_LOWER },
		{ "refresh_startup", SettingsManager::STARTUP_REFRESH, ResourceManager::SETTINGS_STARTUP_REFRESH },
		{ "refresh_threading", SettingsManager::REFRESH_THREADING, ResourceManager::MULTITHREADED_REFRESH },

		//{ ResourceManager::SETTINGS_SHARING_OPTIONS },
		{ "share_skiplist", SettingsManager::SKIPLIST_SHARE, ResourceManager::ST_SKIPLIST_SHARE },
		{ "share_skiplist_regex", SettingsManager::SHARE_SKIPLIST_USE_REGEXP, ResourceManager::USE_REGEXP },
		{ "share_hidden", SettingsManager::SHARE_HIDDEN, ResourceManager::SETTINGS_SHARE_HIDDEN },
		{ "share_no_empty_dirs", SettingsManager::SKIP_EMPTY_DIRS_SHARE, ResourceManager::DONT_SHARE_EMPTY_DIRS },
		{ "share_no_zero_byte", SettingsManager::NO_ZERO_BYTE, ResourceManager::SETTINGS_NO_ZERO_BYTE },
		{ "share_max_size", SettingsManager::MAX_FILE_SIZE_SHARED, ResourceManager::DONT_SHARE_BIGGER_THAN, ApiSettingItem::TYPE_LAST, ResourceManager::Strings::MiB },
		{ "share_follow_symlinks", SettingsManager::SHARE_FOLLOW_SYMLINKS, ResourceManager::FOLLOW_SYMLINKS },

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

		// Events
		{ "report_uploads", SettingsManager::SYSTEM_SHOW_UPLOADS, ResourceManager::SYSTEM_SHOW_FINISHED_UPLOADS },
		{ "report_downloads", SettingsManager::SYSTEM_SHOW_DOWNLOADS, ResourceManager::SYSTEM_SHOW_FINISHED_DOWNLOADS },
		{ "report_search_alternates", SettingsManager::REPORT_ALTERNATES, ResourceManager::REPORT_ALTERNATES },
		{ "report_added_sources", SettingsManager::REPORT_ADDED_SOURCES, ResourceManager::SETTINGS_REPORT_ADDED_SOURCES },
		{ "report_blocked_share", SettingsManager::REPORT_BLOCKED_SHARE, ResourceManager::REPORT_BLOCKED_SHARE },
		{ "report_hashed_files", SettingsManager::LOG_HASHING, ResourceManager::LOG_HASHING },
		{ "report_scheduled_refreshes", SettingsManager::LOG_SCHEDULED_REFRESHES, ResourceManager::SETTINGS_LOG_SCHEDULED_REFRESHES },
		{ "report_filelist_dupes", SettingsManager::FL_REPORT_FILE_DUPES, ResourceManager::REPORT_DUPLICATE_FILES },
		{ "report_ignored_messages", SettingsManager::LOG_IGNORED, ResourceManager::REPORT_IGNORED },
		{ "report_crc_ok", SettingsManager::LOG_CRC_OK, ResourceManager::LOG_CRC_OK },

		//{ ResourceManager::HISTORIES },
		{ "history_search_max", SettingsManager::HISTORY_SEARCH_MAX, ResourceManager::SEARCH_STRINGS },
		{ "history_search_clear_exit", SettingsManager::HISTORY_SEARCH_CLEAR, ResourceManager::CLEAR_EXIT },
		{ "history_download_max", SettingsManager::HISTORY_DIR_MAX, ResourceManager::SETTINGS_DOWNLOAD_LOCATIONS },
		{ "history_download_clear_exit", SettingsManager::HISTORY_DIR_CLEAR, ResourceManager::CLEAR_EXIT },
		{ "history_chat_log_lines", SettingsManager::MAX_PM_HISTORY_LINES, ResourceManager::MAX_PM_HISTORY_LINES },

		{ "history_pm_messages", SettingsManager::PM_MESSAGE_CACHE, ResourceManager::PRIVATE_CHAT },
		{ "history_hub_messages", SettingsManager::HUB_MESSAGE_CACHE , ResourceManager::HUBS },
		{ "history_log_messages", SettingsManager::LOG_MESSAGE_CACHE , ResourceManager::SYSTEM_LOG },

		{ "history_hub_sessions", SettingsManager::MAX_RECENT_HUBS , ResourceManager::HUB },
		{ "history_pm_sessions", SettingsManager::MAX_RECENT_PRIVATE_CHATS , ResourceManager::PRIVATE_CHAT },
		{ "history_filelist_sessions", SettingsManager::MAX_RECENT_FILELISTS , ResourceManager::FILELIST },

		//{ ResourceManager::SETTINGS_ADVANCED },
		{ "socket_read_buffer", SettingsManager::SOCKET_IN_BUFFER, ResourceManager::SETTINGS_SOCKET_IN_BUFFER, ApiSettingItem::TYPE_LAST, ResourceManager::Strings::B },
		{ "socket_write_buffer", SettingsManager::SOCKET_OUT_BUFFER, ResourceManager::SETTINGS_SOCKET_OUT_BUFFER, ApiSettingItem::TYPE_LAST, ResourceManager::Strings::B },
		{ "buffer_size", SettingsManager::BUFFER_SIZE, ResourceManager::SETTINGS_WRITE_BUFFER, ApiSettingItem::TYPE_LAST, ResourceManager::Strings::KiBS },
		{ "compress_transfers", SettingsManager::COMPRESS_TRANSFERS, ResourceManager::SETTINGS_COMPRESS_TRANSFERS },
		{ "max_compression", SettingsManager::MAX_COMPRESSION, ResourceManager::SETTINGS_MAX_COMPRESS },
		{ "bloom_mode", SettingsManager::BLOOM_MODE, ResourceManager::BLOOM_MODE },

		{ "min_search_interval", SettingsManager::MINIMUM_SEARCH_INTERVAL, ResourceManager::MINIMUM_SEARCH_INTERVAL, ApiSettingItem::TYPE_LAST, ResourceManager::Strings::SECONDS_LOWER },
		{ "disconnect_offline_users", SettingsManager::AUTO_KICK, ResourceManager::SETTINGS_AUTO_KICK },
		{ "nmdc_encoding", SettingsManager::NMDC_ENCODING, ResourceManager::NMDC_ENCODING },
		{ "auto_follow_redirects", SettingsManager::AUTO_FOLLOW, ResourceManager::SETTINGS_AUTO_FOLLOW },
		{ "disconnect_hubs_noreg", SettingsManager::DISALLOW_CONNECTION_TO_PASSED_HUBS, ResourceManager::DISALLOW_CONNECTION_TO_PASSED_HUBS },

		{ "wizard_pending", SettingsManager::WIZARD_PENDING, ResourceManager::WIZARD_FINISHED_TITLE },

		{ "use_default_cert_paths", SettingsManager::USE_DEFAULT_CERT_PATHS, ResourceManager::USE_DEFAULT_CERT_PATHS },
		{ "tls_certificate_file", SettingsManager::TLS_CERTIFICATE_FILE, ResourceManager::OWN_CERTIFICATE, ApiSettingItem::TYPE_FILE_PATH },
		{ "tls_private_key_file", SettingsManager::TLS_PRIVATE_KEY_FILE, ResourceManager::PRIVATE_KEY_FILE, ApiSettingItem::TYPE_FILE_PATH },
		{ "always_ccpm", SettingsManager::ALWAYS_CCPM, ResourceManager::ALWAYS_CCPM },
		{ "tls_mode", SettingsManager::TLS_MODE, ResourceManager::TRANSFER_ENCRYPTION },
	};
}

#endif