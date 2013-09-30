/*
 * Copyright (C) 2001-2013 Jacek Sieka, arnetheduck on gmail point com
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

#include "stdinc.h"
#include "SettingsManager.h"

#include "CID.h"
#include "ConnectivityManager.h"
#include "ResourceManager.h"
#include "SimpleXML.h"
#include "Util.h"
#include "AirUtil.h"
#include "File.h"
#include "version.h"

namespace dcpp {

#define CONFIG_NAME "DCPlusPlus.xml"
#define CONFIG_DIR Util::PATH_USER_CONFIG

bool SettingsManager::lanMode = false;

StringList SettingsManager::connectionSpeeds = { "0.1", "0.2", "0.5", "1", "2", "5", "8", "10", "20", "30", "40", "50", "60", "100", "200", "1000" };


const ResourceManager::Strings SettingsManager::encryptionStrings[TLS_LAST] { ResourceManager::DISABLED, ResourceManager::ENABLED, ResourceManager::ENCRYPTION_FORCED };
const ResourceManager::Strings SettingsManager::delayStrings[DELAY_LAST] { ResourceManager::MONITOR_DELAY_DIR, ResourceManager::MONITOR_DELAY_VOLUME, ResourceManager::MONITOR_DELAY_ANY };
const ResourceManager::Strings SettingsManager::bloomStrings[BLOOM_LAST] { ResourceManager::DISABLED, ResourceManager::ENABLED, ResourceManager::AUTO };
const ResourceManager::Strings SettingsManager::profileStrings[PROFILE_LAST] { ResourceManager::NORMAL, ResourceManager::RAR_HUBS, ResourceManager::LAN_HUBS };
const ResourceManager::Strings SettingsManager::refreshStrings[MULTITHREAD_LAST] { ResourceManager::NEVER, ResourceManager::MANUAL_REFRESHES, ResourceManager::ALWAYS };
const ResourceManager::Strings SettingsManager::prioStrings[PRIO_LAST] { ResourceManager::DISABLED, ResourceManager::PRIOPAGE_ORDER_BALANCED, ResourceManager::PRIOPAGE_ORDER_PROGRESS };
const ResourceManager::Strings SettingsManager::incomingStrings[INCOMING_LAST] { ResourceManager::DISABLED, ResourceManager::ACTIVE_MODE, ResourceManager::SETTINGS_ACTIVE_UPNP, ResourceManager::PASSIVE_MODE };
const ResourceManager::Strings SettingsManager::outgoingStrings[OUTGOING_LAST] { ResourceManager::SETTINGS_DIRECT, ResourceManager::SETTINGS_SOCKS5 };
const ResourceManager::Strings SettingsManager::monitoringStrings[MONITORING_LAST] { ResourceManager::DISABLED, ResourceManager::INCOMING_ONLY, ResourceManager::ALL_DIRS };
const ResourceManager::Strings SettingsManager::dropStrings[QUEUE_LAST] { ResourceManager::FILE, ResourceManager::BUNDLE, ResourceManager::ALL };

const ProfileSettingItem SettingsManager::profileSettings[SettingsManager::PROFILE_LAST][9] = {

{ 
	// profile normal
	{ SettingsManager::MULTI_CHUNK, true, ResourceManager::SEGMENTS },
	{ SettingsManager::CHECK_SFV, false, ResourceManager::CHECK_SFV },
	{ SettingsManager::CHECK_NFO, false, ResourceManager::CHECK_NFO },
	{ SettingsManager::CHECK_EXTRA_SFV_NFO, false, ResourceManager::CHECK_EXTRA_SFV_NFO },
	{ SettingsManager::CHECK_EXTRA_FILES, false, ResourceManager::CHECK_EXTRA_FILES },
	{ SettingsManager::CHECK_DUPES, false, ResourceManager::CHECK_DUPES },
	{ SettingsManager::MAX_FILE_SIZE_SHARED, 0, ResourceManager::DONT_SHARE_BIGGER_THAN },
	{ SettingsManager::SEARCH_TIME, 10, ResourceManager::MINIMUM_SEARCH_INTERVAL },
	//{ SettingsManager::AUTO_SEARCH_LIMIT, 5 },
	{ SettingsManager::AUTO_FOLLOW, true, ResourceManager::SETTINGS_AUTO_FOLLOW },
}, {
	// profile RAR
	{ SettingsManager::MULTI_CHUNK, false, ResourceManager::SEGMENTS },
	{ SettingsManager::CHECK_SFV, true, ResourceManager::CHECK_SFV },
	{ SettingsManager::CHECK_NFO, true, ResourceManager::CHECK_NFO },
	{ SettingsManager::CHECK_EXTRA_SFV_NFO, true, ResourceManager::CHECK_EXTRA_SFV_NFO },
	{ SettingsManager::CHECK_EXTRA_FILES, true, ResourceManager::CHECK_EXTRA_FILES },
	{ SettingsManager::CHECK_DUPES, true, ResourceManager::CHECK_DUPES },
	{ SettingsManager::MAX_FILE_SIZE_SHARED, 600, ResourceManager::DONT_SHARE_BIGGER_THAN },
	{ SettingsManager::SEARCH_TIME, 5, ResourceManager::MINIMUM_SEARCH_INTERVAL },
	//{ SettingsManager::AUTO_SEARCH_LIMIT, 5 },
	{ SettingsManager::AUTO_FOLLOW, false, ResourceManager::SETTINGS_AUTO_FOLLOW },
}, {
	// profile LAN
	{ SettingsManager::MULTI_CHUNK, true, ResourceManager::SEGMENTS },
	{ SettingsManager::CHECK_SFV, false, ResourceManager::CHECK_SFV },
	{ SettingsManager::CHECK_NFO, false, ResourceManager::CHECK_NFO },
	{ SettingsManager::CHECK_EXTRA_SFV_NFO, false, ResourceManager::CHECK_EXTRA_SFV_NFO },
	{ SettingsManager::CHECK_EXTRA_FILES, false, ResourceManager::CHECK_EXTRA_FILES },
	{ SettingsManager::CHECK_DUPES, false, ResourceManager::CHECK_DUPES },
	{ SettingsManager::MAX_FILE_SIZE_SHARED, 0, ResourceManager::DONT_SHARE_BIGGER_THAN },
	{ SettingsManager::SEARCH_TIME, 5, ResourceManager::MINIMUM_SEARCH_INTERVAL },
	//{ SettingsManager::AUTO_SEARCH_LIMIT, 5 },
	{ SettingsManager::AUTO_FOLLOW, true, ResourceManager::SETTINGS_AUTO_FOLLOW },
} 

};

const string SettingsManager::settingTags[] =
{
	// Strings
	"Nick", "UploadSpeed", "Description", "DownloadDirectory", "EMail", "ExternalIp", "ExternalIp6",
	"Font", "TransferViewOrder", "TransferViewWidths", "HubFrameOrder", "HubFrameWidths", 
	"LanguageFile", "SearchFrameOrder", "SearchFrameWidths", "FavoritesFrameOrder", "FavoritesFrameWidths", 
	"HublistServers", "QueueFrameOrder", "QueueFrameWidths", "PublicHubsFrameOrder", "PublicHubsFrameWidths", 
	"UsersFrmOrder2", "UsersFrmWidths2", "HttpProxy", "LogDirectory", "LogFormatPostDownload", 
	"LogFormatPostUpload", "LogFormatMainChat", "LogFormatPrivateChat", "FinishedOrder", "FinishedWidths",	 
	"TempDownloadDirectory", "BindAddress", "BindAddress6", "SocksServer", "SocksUser", "SocksPassword", "ConfigVersion", 
	"DefaultAwayMessage", "TimeStampsFormat", "ADLSearchFrameOrder", "ADLSearchFrameWidths", 
	"FinishedULWidths", "FinishedULOrder", "CID", "SpyFrameWidths", "SpyFrameOrder", 
	"BeepFile", "BeginFile", "FinishedFile", "SourceFile", "UploadFile", "ChatNameFile", "WinampFormat",
	"KickMsgRecent01", "KickMsgRecent02", "KickMsgRecent03", "KickMsgRecent04", "KickMsgRecent05", 
	"KickMsgRecent06", "KickMsgRecent07", "KickMsgRecent08", "KickMsgRecent09", "KickMsgRecent10", 
	"KickMsgRecent11", "KickMsgRecent12", "KickMsgRecent13", "KickMsgRecent14", "KickMsgRecent15", 
	"KickMsgRecent16", "KickMsgRecent17", "KickMsgRecent18", "KickMsgRecent19", "KickMsgRecent20",
	"Toolbar", "ToolbarImage", "ToolbarHot", "UploadQueueFrameOrder", "UploadQueueFrameWidths",
	"SoundTTH", "SoundException", "SoundHubConnected", "SoundHubDisconnected", "SoundFavUserOnline", "SoundTypingNotify",
	"LogFileMainChat", 
	"LogFilePrivateChat", "LogFileStatus", "LogFileUpload", "LogFileDownload", "LogFileSystem", "LogFormatSystem", 
	"LogFormatStatus", "DirectoryListingFrameOrder", "DirectoryListingFrameWidths", 
	"MainFrameVisible", "SearchFrameVisible", "QueueFrameVisible", "HubFrameVisible", "UploadQueueFrameVisible", 
	"EmoticonsFile", "TLSPrivateKeyFile", "TLSCertificateFile", "TLSTrustedCertificatesPath",
	"FinishedVisible", "FinishedULVisible", "DirectoryListingFrameVisible",
	"RecentFrameOrder", "RecentFrameWidths", "Mapper", "CountryFormat", "DateFormat",

	"BackgroundImage", "MPLAYERCformat", "ITUNESformat", "WMPformat", "Spotifyformat","WinampPath",
	"SkiplistShare", "FreeSlotsExtensions",
	"PopupFont", "PopupTitleFont", "PopupFile", "SkiplistDownload", "HighPrioFiles",
	"MediaToolbar", "password", "DownloadSpeed", "HighlightList", "IconPath",
	"AutoSearchFrame2Order", "AutoSearchFrame2Widths", "ToolbarPos", "TBProgressFont", "LastSearchFiletype", "LastSearchDisabledHubs", "LastASFiletype", "LastSearchExcluded",
	"UsersFrmVisible2", "ListViewFont", "LogShareScanPath", "LastFilelistFiletype",

	"SENTRY", 
	// Ints
	"IncomingConnections", "IncomingConnections6", "InPort", "Slots", "BackgroundColor", "TextColor", "BufferSize", "DownloadSlots", "MaxDownloadSpeed", "MinUploadSpeed", "MainWindowState", 
	"MainWindowSizeX", "MainWindowSizeY", "MainWindowPosX", "MainWindowPosY", "SocksPort", "MaxTabRows",
	"MaxCompression", "DownloadBarColor", "UploadBarColor", "SetMinislotSize", "ShutdownInterval", "ExtraSlots", "ExtraPartialSlots",
	"TextGeneralBackColor", "TextGeneralForeColor",
	"TextMyOwnBackColor", "TextMyOwnForeColor",
	"TextPrivateBackColor", "TextPrivateForeColor", 
	"TextSystemBackColor", "TextSystemForeColor",
	"TextServerBackColor", "TextServerForeColor",
	"TextTimestampBackColor", "TextTimestampForeColor",
	"TextMyNickBackColor", "TextMyNickForeColor",
	"TextFavBackColor", "TextFavForeColor", 
	"TextOPBackColor", "TextOPForeColor",
	"TextURLBackColor", "TextURLForeColor",
	"Progress3DDepth", 
	"HubSlots",	"ProgressTextDown", "ProgressTextUp", "ExtraDownloadSlots", "ErrorColor", "TransferSplitSize",
	"DisconnectSpeed", "DisconnectFileSpeed", "DisconnectTime", "RemoveSpeed", "MenubarLeftColor", 
	"MenubarRightColor", "DisconnectFileSize", "NumberOfSegments", "MaxHashSpeed", "PMLogLines", "SearchAlternateColour", "SearchTime", "DontBeginSegmentSpeed",
	"MagnetAction",  "PopupType", "ShutdownAction", "MinimumSearchInterval", "MaxAutoMatchSource", "ReservedSlotColor", "IgnoredColor", "FavoriteColor","NormalColour",
	"PasiveColor", "OpColor", "ProgressBackColor", "ProgressCompressColor", "ProgressSegmentColor", "UDPPort",
 	"UserListDoubleClick", "TransferListDoubleClick", "ChatDoubleClick", "OutgoingConnections","SocketInBuffer", "SocketOutBuffer", 
	"ColorDownloaded", "ColorRunning", "ColorDone", "AutoRefreshTime", "AutoSearchLimit",
 	"MaxCommandLength", "TLSPort", "DownConnPerSec", "HighestPrioSize", "HighPrioSize", "NormalPrioSize", "LowPrioSize",

	"BandwidthLimitStart", "BandwidthLimitEnd", "MaxDownloadSpeedRealTime",
	"MaxUploadSpeedTime", "MaxDownloadSpeedPrimary", "MaxUploadSpeedPrimary",
	"SlotsAlternateLimiting", "SlotsPrimaryLimiting",

	//AirDC
	"tabactivebg", "TabActiveText", "TabActiveBorder", "TabInactiveBg", "TabInactiveBgDisconnected", "TabInactiveText", "TabInactiveBorder", "TabInactiveBgNotify", "TabDirtyBlend", "TabSize", "MediaPlayer",
	"FavDownloadSpeed", "PopupTime", "MaxMsgLength", "PopupBackColor", "PopupTextColor", "PopupTitleTextColor", "AutoSearchEvery", "AutoSearchRecheckTime", "TbImageSize", "TbImageSizeHot", 
	"DupeColor", "TextDupeBackColor", "MinSegmentSize", "AutoSlots", "MaxResizeLines", "IncomingRefreshTime", "TextNormBackColor", "TextNormForeColor", "SettingsProfile", "LogLines",
	"MaxFileSizeShared", "FavTop", "FavBottom", "FavLeft", "FavRight", "SyslogTop", "SyslogBottom", "SyslogLeft", "SyslogRight", "NotepadTop", "NotepadBottom",
	"NotepadLeft", "NotepadRight", "QueueTop", "QueueBottom", "QueueLeft", "QueueRight", "SearchTop", "SearchBottom", "SearchLeft", "SearchRight", "UsersTop", "UsersBottom",
	"UsersLeft", "UsersRight", "FinishedTop", "FinishedBottom", "FinishedLeft", "FinishedRight", "TextTop", "TextBottom", "TextLeft", "TextRight", "DirlistTop", "DirlistBottom",
	"DirlistLeft", "DirlistRight", "StatsTop", "StatsBottom", "StatsLeft", "StatsRight", "MaxMCNDownloads", "MaxMCNUploads", "ListHighlightBackColor", "ListHighlightColor", "QueueColor", "TextQueueBackColor",
	"RecentBundleHours","DisconnectMinSources", "AutoprioType", "AutoprioInterval", "AutosearchExpireDays", "DLAutoSelectMethod", "WinampBarIconSize", "TBProgressTextColor", "TLSMode", "UpdateMethod", 
	"QueueSplitterPos", "FullListDLLimit", "ASDelayHours", "LastListProfile", "MaxHashingThreads", "HashersPerVolume", "SubtractlistSkip", "BloomMode", "FavUsersSplitterPos", "AwayIdleTime",
	"SearchHistoryMax", "ExcludeHistoryMax", "DirectoryHistoryMax", "MinDupeCheckSize", "DbCacheSize", "DLAutoDisconnectMode", "RemovedTrees", "RemovedFiles", "MultithreadedRefresh", "MonitoringMode", 
	"MonitoringDelay", "DelayCountMode", "MaxRunningBundles", "DefaultShareProfile",
	"SENTRY",

	// Bools
	"AddFinishedInstantly", "AdlsBreakOnFirst",
	"AllowUntrustedClients", "AllowUntrustedHubs", "AutoAway",
	"AutoDetectIncomingConnection", "AutoDetectIncomingConnection6", "AutoFollow", "AutoKick", "AutoKickNoFavs", "AutoSearch",
	"AwayCompLock", "BoldFinishedDownloads", "BoldFinishedUploads", "BoldHub", "BoldPm",
	"BoldQueue", "BoldSearch", "BoldSystemLog", "ClearSearch",
	"CompressTransfers", "ConfirmADLSRemoval", "ConfirmExit",
	"ConfirmHubRemoval", "ConfirmUserRemoval", "Coral",
	"DontDlAlreadyQueued", "DontDLAlreadyShared", "FavShowJoins", "FilterMessages",
	"FollowLinks", "GetUserCountry", "GetUserInfo", "HubUserCommands",
	"IgnoreBotPms", "IgnoreHubPms", "OpenNewWindow", "KeepFinishedFiles", "KeepLists",
	"LogDownloads", "LogFilelistTransfers", "LogFinishedDownloads", "LogMainChat",
	"LogPrivateChat", "LogStatusMessages", "LogSystem", "LogUploads", "MagnetAsk",
	"MagnetRegister", "MinimizeToTray", "NoAwayMsgToBots", "NoIpOverride",
	"PopupBotPms", "PopupHubPms", "PopupPMs", "PopunderFilelist", "PopunderPm",
	"LowestPrio", "PromptPassword", "QueueFrameShowTree", "SearchFilterShared",
	"SearchOnlyFreeSlots", "SendUnknownCommands",
	"ShareHidden", "ShowJoins", "ShowMenuBar", "ShowStatusbar", "ShowToolbar",
	"ShowTransferview", "SkipZeroByte", "SocksResolve", "SortFavUsersFirst",
	"StatusInChat", "TimeDependentThrottle", "TimeStamps",
	"ToggleActiveTab", "UrlHandler", "UseCTRLForLineHistory", "UseSystemIcons",
	"UsersFilterFavorite", "UsersFilterOnline", "UsersFilterQueue", "UsersFilterWaiting",
	"AwayTimeStamp",

	"PrivateMessageBeep", "PrivateMessageBeepOpen", "ShowProgressBars", "MDIMaxmimized", "SearchPassiveAlways", "RemoveForbidden", "ShowInfoTips", "MinimizeOnStratup", "ConfirmDelete", "ExpandQueue",
	"FilterEnter", "ShowShellMenu", "SpyFrameIgnoreTthSearches", "OpenWaitingUsers", "BoldWaitingUsers", "GroupSearchResults", "TabsOnTop", "OpenPublic", "OpenFavoriteHubs", "OpenFavoriteUsers", "OpenQueue", "OpenFinishedDownloads",
	"OpenFinishedUploads", "OpenSearchSpy", "OpenNetworkStatistics", "OpenNotepad", "SuppressMainChat", "ProgressbaroDCStyle", "MultiChunk", "PopupAway", "PopupMinimized", "Away", "PopupHubConnected", "PopupHubDisconnected", "PopupFavoriteConnected", 
	"PopupDownloadStart", "PopupDownloadFailed", "PopupDownloadFinished", "PopupUploadFinished", "PopupPm", "PopupNewPM", "UploadQueueFrameShowTree", "SegmentsManual", "SoundsDisabled", "ReportFoundAlternates",
	"UseAutoPriorityByDefault", "UseOldSharingUI", "DefaultSearchFreeSlots", 

	"TextGeneralBold", "TextGeneralItalic", "TextMyOwnBold", "TextMyOwnItalic", "TextPrivateBold", "TextPrivateItalic", "TextSystemBold", "TextSystemItalic", "TextServerBold", "TextServerItalic", "TextTimestampBold", "TextTimestampItalic", 
	"TextMyNickBold", "TextMyNickItalic", "TextFavBold", "TextFavItalic", "TextOPBold", "TextOPItalic", "TextURLBold", "TextURLItalic", "ProgressOverrideColors", "ProgressOverrideColors2", "MenubarTwoColors", "MenubarBumped", "DontBeginSegment", 

	"AutoDetectionUseLimited", "LogScheduledRefreshes", "AutoCompleteBundles", "SearchSaveHubsState", "ConfirmHubExit", "ConfirmASRemove", "EnableSUDP", "NmdcMagnetWarn",
	"UpdateIPHourly", "OpenTextOnBackground", "LockTB", "PopunderPartialList", "ShowTBStatusBar", "HorizontalQueue", "UseSlowDisconnectingDefault", "PrioListHighest", 
	"UseFTPLogger", "QIAutoPrio", "ShowSharedDirsFav", "ReportAddedSources", "ExpandBundles", "OverlapSlowUser", "FormatDirRemoteTime", "TextQueueBold", "TextQueueItalic", "UnderlineQueue", "LogHashedFiles",
	"UsePartialSharing", "PopupBundleDLs", "PopupBundleULs", "ListHighlightBold", "ListHighlightItalic", "ReportSkiplist", "ScanDLBundles", "MCNAutoDetect", "DLAutoDetect", "ULAutoDetect", "CheckUseSkiplist", "CheckIgnoreZeroByte", 
	"TextDupeBold", "TextDupeItalic", "UnderlineLinks", "UnderlineDupes", "DupesInFilelists", "DupesInChat", "NoZeroByte", "CheckEmptyDirs","CheckEmptyReleases",  "CheckMissing", "CheckSfv", 
	"CheckNfo", "CheckMp3Dir", "CheckExtraSfvNfo", "CheckExtraFiles", "CheckDupes", "SortDirs", "DecreaseRam", "WizardRunNew", "FormatRelease", "TextNormBold", "TextNormItalic", "SystemShowUploads", "SystemShowDownloads", 
	"UseAdls", "DupeSearch", "passwd_protect", "passwd_protect_tray", "DisAllowConnectionToPassedHubs", "BoldHubTabsOnKick",
	"AutoAddSource", "UseExplorerTheme", "TestWrite", "OpenSystemLog", "OpenLogsInternal", "UcSubMenu", "ShowQueueBars", "ExpandDefault",
	"ShareSkiplistUseRegexp", "DownloadSkiplistUseRegexp", "HighestPriorityUseRegexp", "UseHighlight", "FlashWindowOnPm", "FlashWindowOnNewPm", "FlashWindowOnMyNick", "IPUpdate", "serverCommands", "ClientCommands", 
	"PreviewPm", "IgnoreUseRegexpOrWc", "NatSort", "HubBoldTabs", "showWinampControl", "BlendTabs", "TabShowIcons", "AllowMatchFullList", "ShowChatNotify", "FreeSpaceWarn", "FavUsersShowInfo", "LogAlreadyShared",
	"ClearDirectoryHistory", "ClearExcludeHistory", "ClearDirHistory", "NoIpOverride6", "IPUpdate6", "SearchUseExcluded", "AutoSearchBold", "ShowEmoticon", "ShowMultiline", "ShowMagnet", "WarnElevated", "SkipEmptyDirsShare", "LogShareScans",
	"AcceptFailoversFavs", "RemoveExpiredAs", "AdcLogGroupCID", "ShareFollowSymlinks", "ScanMonitoredFolders", "FinishedNoHash", "ConfirmFileDeletions", "UseDefaultCertPaths", "StartupRefresh", "DctmpStoreDestination", "FLReportDupeFiles",
	"FilterFLShared", "FilterFLQueued", "FilterFLInversed", "FilterFLTop", "FilterFLPartialDupes", "FilterFLResetChange", "FilterSearchShared", "FilterSearchQueued", "FilterSearchInversed", "FilterSearchTop", "FilterSearchPartialDupes", "FilterSearchResetChange",
	"SearchAschOnlyMan", "IgnoreIndirectSR", "UseUploadBundles",
	"SENTRY",
	// Int64
	"TotalUpload", "TotalDownload",
	"SENTRY"
};

SettingsManager::SettingsManager()
{
	//make sure it can fit our events without using push_back since
	//that might cause them to be in the wrong position.
	fileEvents.resize(2);

	for(int i=0; i<SETTINGS_LAST; i++)
		isSet[i] = false;

	for(int i=0; i<INT_LAST-INT_FIRST; i++) {
		intDefaults[i] = 0;
		intSettings[i] = 0;
	}
	for(int i=0; i<INT64_LAST-INT64_FIRST; i++) {
		int64Defaults[i] = 0;
		int64Settings[i] = 0;
	}

	setDefault(MAX_UPLOAD_SPEED_MAIN, 0);
	setDefault(MAX_DOWNLOAD_SPEED_MAIN, 0);
	setDefault(TIME_DEPENDENT_THROTTLE, false);
	setDefault(MAX_DOWNLOAD_SPEED_ALTERNATE, 0);
	setDefault(MAX_UPLOAD_SPEED_ALTERNATE, 0);
	setDefault(BANDWIDTH_LIMIT_START, 1);
	setDefault(BANDWIDTH_LIMIT_END, 1);
	setDefault(SLOTS_ALTERNATE_LIMITING, 1);
	
	setDefault(DOWNLOAD_DIRECTORY, Util::getPath(Util::PATH_DOWNLOADS));
	setDefault(TEMP_DOWNLOAD_DIRECTORY, Util::getPath(Util::PATH_USER_CONFIG) + "Incomplete" PATH_SEPARATOR_STR);
	setDefault(SLOTS, 2);
	setDefault(MAX_COMMAND_LENGTH, 512*1024); // 512 KiB
	setDefault(BIND_ADDRESS, "0.0.0.0");
	setDefault(BIND_ADDRESS6, "::");
	setDefault(TCP_PORT, 0);
	setDefault(UDP_PORT, 0);
	setDefault(TLS_PORT, 0);
	setDefault(INCOMING_CONNECTIONS, INCOMING_ACTIVE);

	//TODO: check whether we have ipv6 available
	setDefault(INCOMING_CONNECTIONS6, INCOMING_ACTIVE);

	setDefault(OUTGOING_CONNECTIONS, OUTGOING_DIRECT);
	setDefault(AUTO_DETECT_CONNECTION, true);
	setDefault(AUTO_DETECT_CONNECTION6, true);

	setDefault(AUTO_FOLLOW, true);
	setDefault(CLEAR_SEARCH, true);
	setDefault(SHARE_HIDDEN, false);
	setDefault(FILTER_MESSAGES, true);
	setDefault(MINIMIZE_TRAY, false);
	setDefault(AUTO_SEARCH, true);
	setDefault(TIME_STAMPS, true);
	setDefault(CONFIRM_EXIT, true);
	setDefault(POPUP_HUB_PMS, true);
	setDefault(POPUP_BOT_PMS, true);
	setDefault(IGNORE_HUB_PMS, false);
	setDefault(IGNORE_BOT_PMS, false);
	setDefault(BUFFER_SIZE, 64);
	setDefault(HUBLIST_SERVERS, "http://dchublist.com/hublist.xml.bz2;http://www.hublista.hu/hublist.xml.bz2;http://hublist.openhublist.org/hublist.xml.bz2;");
	setDefault(DOWNLOAD_SLOTS, 50);
	setDefault(MAX_DOWNLOAD_SPEED, 0);
	setDefault(LOG_DIRECTORY, Util::getPath(Util::PATH_USER_CONFIG) + "Logs" PATH_SEPARATOR_STR);
	setDefault(LOG_UPLOADS, false);
	setDefault(LOG_DOWNLOADS, false);
	setDefault(LOG_PRIVATE_CHAT, false);
	setDefault(LOG_MAIN_CHAT, false);
	setDefault(STATUS_IN_CHAT, true);
	setDefault(SHOW_JOINS, false);
	setDefault(UPLOAD_SPEED, connectionSpeeds[0]);
	setDefault(PRIVATE_MESSAGE_BEEP, false);
	setDefault(PRIVATE_MESSAGE_BEEP_OPEN, false);
	setDefault(USE_SYSTEM_ICONS, true);
	setDefault(POPUP_PMS, true);
	setDefault(MIN_UPLOAD_SPEED, 0);
	setDefault(LOG_FORMAT_POST_DOWNLOAD, "%Y-%m-%d %H:%M: %[target] " + STRING(DOWNLOADED_FROM) + " %[userNI] (%[userCID]), %[fileSI] (%[fileSIchunk]), %[speed], %[time]");
	setDefault(LOG_FORMAT_POST_UPLOAD, "%Y-%m-%d %H:%M: %[source] " + STRING(UPLOADED_TO) + " %[userNI] (%[userCID]), %[fileSI] (%[fileSIchunk]), %[speed], %[time]");
	setDefault(LOG_FORMAT_MAIN_CHAT, "[%Y-%m-%d %H:%M] %[message]");
	setDefault(LOG_FORMAT_PRIVATE_CHAT, "[%Y-%m-%d %H:%M] %[message]");
	setDefault(LOG_FORMAT_STATUS, "[%Y-%m-%d %H:%M] %[message]");
	setDefault(LOG_FORMAT_SYSTEM, "[%Y-%m-%d %H:%M] %[message]");
	setDefault(LOG_FILE_MAIN_CHAT, "%[hubURL].log");
	setDefault(LOG_FILE_STATUS, "%[hubURL]_status.log");
	setDefault(LOG_FILE_PRIVATE_CHAT, "PM\\%B - %Y\\%[userNI].log");
	setDefault(LOG_FILE_UPLOAD, "Uploads.log");
	setDefault(LOG_FILE_DOWNLOAD, "Downloads.log");
	setDefault(LOG_FILE_SYSTEM, "system.log");
	setDefault(GET_USER_INFO, true);
	setDefault(URL_HANDLER, true);
	setDefault(AUTO_AWAY, false);
	setDefault(SOCKS_PORT, 1080);
	setDefault(SOCKS_RESOLVE, true);
	setDefault(CONFIG_VERSION, "0.181");		// 0.181 is the last version missing configversion
	setDefault(KEEP_LISTS, false);
	setDefault(AUTO_KICK, false);
	setDefault(QUEUEFRAME_SHOW_TREE, true);
	setDefault(COMPRESS_TRANSFERS, true);
	setDefault(SHOW_PROGRESS_BARS, true);
	setDefault(DEFAULT_AWAY_MESSAGE, "I'm away. State your business and I might answer later if you're lucky.");
	setDefault(TIME_STAMPS_FORMAT, "%H:%M:%S");
	setDefault(MAX_TAB_ROWS, 4);
	setDefault(MAX_COMPRESSION, 6);
	setDefault(NO_AWAYMSG_TO_BOTS, true);
	setDefault(SKIP_ZERO_BYTE, false);
	setDefault(ADLS_BREAK_ON_FIRST, false);
	setDefault(HUB_USER_COMMANDS, true);
	setDefault(LOG_FILELIST_TRANSFERS, false);
	setDefault(LOG_SYSTEM, true);
	setDefault(SEND_UNKNOWN_COMMANDS, false);
	setDefault(MAX_HASH_SPEED, 0);
	setDefault(GET_USER_COUNTRY, true);
	setDefault(FAV_SHOW_JOINS, false);
	setDefault(LOG_STATUS_MESSAGES, false);
	setDefault(SHOW_TRANSFERVIEW, true);
	setDefault(SHOW_STATUSBAR, true);
	setDefault(SHOW_TOOLBAR, true);
	setDefault(POPUNDER_PM, false);
	setDefault(POPUNDER_FILELIST, false);
	setDefault(MAGNET_REGISTER, false);
	setDefault(MAGNET_ASK, true);
	setDefault(MAGNET_ACTION, MAGNET_DOWNLOAD);
	setDefault(ADD_FINISHED_INSTANTLY, true);
	setDefault(DONT_DL_ALREADY_SHARED, false);
	setDefault(CONFIRM_HUB_REMOVAL, true);
	setDefault(USE_CTRL_FOR_LINE_HISTORY, true);
	setDefault(JOIN_OPEN_NEW_WINDOW, false);
	setDefault(SHOW_LAST_LINES_LOG, 10);
	setDefault(CONFIRM_QUEUE_REMOVAL, true);
	setDefault(TOGGLE_ACTIVE_WINDOW, true);
	setDefault(SET_MINISLOT_SIZE, 512);
	setDefault(PRIO_HIGHEST_SIZE, 64);
	setDefault(PRIO_HIGH_SIZE, 0);
	setDefault(PRIO_NORMAL_SIZE, 0);
	setDefault(PRIO_LOW_SIZE, 0);
	setDefault(PRIO_LOWEST, false);
	setDefault(OPEN_PUBLIC, false);
	setDefault(OPEN_FAVORITE_HUBS, false);
	setDefault(OPEN_FAVORITE_USERS, false);
	//setDefault(OPEN_RECENT_HUBS, false);
	setDefault(OPEN_QUEUE, false);
	setDefault(OPEN_FINISHED_DOWNLOADS, false);
	setDefault(OPEN_FINISHED_UPLOADS, false);
	setDefault(OPEN_SEARCH_SPY, false);
	setDefault(OPEN_NETWORK_STATISTICS, false);
	setDefault(OPEN_NOTEPAD, false);
	setDefault(NO_IP_OVERRIDE, false);
	setDefault(NO_IP_OVERRIDE6, false);
	setDefault(SOCKET_IN_BUFFER, 64*1024);
	setDefault(SOCKET_OUT_BUFFER, 64*1024);
	setDefault(OPEN_WAITING_USERS, false);
	setDefault(TLS_TRUSTED_CERTIFICATES_PATH, Util::getPath(Util::PATH_USER_CONFIG) + "Certificates" PATH_SEPARATOR_STR);
	setDefault(TLS_PRIVATE_KEY_FILE, Util::getPath(Util::PATH_USER_CONFIG) + "Certificates" PATH_SEPARATOR_STR "client.key");
	setDefault(TLS_CERTIFICATE_FILE, Util::getPath(Util::PATH_USER_CONFIG) + "Certificates" PATH_SEPARATOR_STR "client.crt");
	setDefault(BOLD_FINISHED_DOWNLOADS, true);
	setDefault(BOLD_FINISHED_UPLOADS, true);
	setDefault(BOLD_QUEUE, true);
	setDefault(BOLD_HUB, true);
	setDefault(BOLD_PM, true);
	setDefault(BOLD_SEARCH, true);
	setDefault(BOLD_WAITING_USERS, true);
	setDefault(AUTO_REFRESH_TIME, 0);
	setDefault(AUTO_SEARCH_LIMIT, 15);
	setDefault(AUTO_KICK_NO_FAVS, false);
	setDefault(PROMPT_PASSWORD, true);
	setDefault(SPY_FRAME_IGNORE_TTH_SEARCHES, false);
	setDefault(ALLOW_UNTRUSTED_HUBS, true);
	setDefault(ALLOW_UNTRUSTED_CLIENTS, true);		
	setDefault(SORT_FAVUSERS_FIRST, false);
	setDefault(SHOW_SHELL_MENU, true);	
	setDefault(CORAL, true);	
	setDefault(NUMBER_OF_SEGMENTS, 3);
	setDefault(SEGMENTS_MANUAL, false);
	setDefault(HUB_SLOTS, 0);
	setDefault(TEXT_FONT, "Tahoma,-11,400,0");
	setDefault(EXTRA_SLOTS, 3);
	setDefault(EXTRA_PARTIAL_SLOTS, 1);
	setDefault(SHUTDOWN_TIMEOUT, 150);
	setDefault(SEARCH_PASSIVE, false);
	setDefault(TOOLBAR, "0,-1,1,2,-1,3,4,5,-1,6,7,8,9,-1,10,11,12,13,-1,14,15,16,17,-1,19,20,21,22,23");
	setDefault(MEDIATOOLBAR, "0,-1,1,-1,2,3,4,5,6,7,8,9,-1");
	setDefault(AUTO_PRIORITY_DEFAULT, false);
	setDefault(TOOLBARIMAGE,"");
	setDefault(TOOLBARHOTIMAGE,"");
	setDefault(REMOVE_FORBIDDEN, true);
	setDefault(EXTRA_DOWNLOAD_SLOTS, 3);

#ifdef _WIN32
	setDefault(SEARCH_ALTERNATE_COLOUR, RGB(255, 200, 0));

	setDefault(BACKGROUND_COLOR, RGB(255, 255, 255));
	setDefault(TEXT_COLOR, RGB(0,0,0));

	setDefault(TEXT_GENERAL_BACK_COLOR, RGB(255, 255, 255));
	setDefault(TEXT_GENERAL_FORE_COLOR, RGB(0,0,0));
	setDefault(TEXT_GENERAL_BOLD, false);
	setDefault(TEXT_GENERAL_ITALIC, false);

	setDefault(TEXT_MYOWN_BACK_COLOR, RGB(255, 255, 255));
	setDefault(TEXT_MYOWN_FORE_COLOR, RGB(0,0,0));
	setDefault(TEXT_MYOWN_BOLD, false);
	setDefault(TEXT_MYOWN_ITALIC, false);

	setDefault(TEXT_PRIVATE_BACK_COLOR, RGB(255,255,255));
	setDefault(TEXT_PRIVATE_FORE_COLOR, RGB(0,0,0));
	setDefault(TEXT_PRIVATE_BOLD, false);
	setDefault(TEXT_PRIVATE_ITALIC, false);

	setDefault(TEXT_SYSTEM_BACK_COLOR, RGB(255, 255, 255));
	setDefault(TEXT_SYSTEM_FORE_COLOR, RGB(255, 102, 0));
	setDefault(TEXT_SYSTEM_BOLD, false);
	setDefault(TEXT_SYSTEM_ITALIC, true);

	setDefault(TEXT_SERVER_BACK_COLOR, RGB(255,255,255));
	setDefault(TEXT_SERVER_FORE_COLOR, RGB(255, 153, 204));
	setDefault(TEXT_SERVER_BOLD, false);
	setDefault(TEXT_SERVER_ITALIC, false);

	setDefault(TEXT_TIMESTAMP_BACK_COLOR, RGB(255,255,255));
	setDefault(TEXT_TIMESTAMP_FORE_COLOR, RGB(255,0,0));
	setDefault(TEXT_TIMESTAMP_BOLD, false);
	setDefault(TEXT_TIMESTAMP_ITALIC, false);

	setDefault(TEXT_MYNICK_BACK_COLOR, RGB(255,255,255));
	setDefault(TEXT_MYNICK_FORE_COLOR, RGB(0,180,0));
	setDefault(TEXT_MYNICK_BOLD, true);
	setDefault(TEXT_MYNICK_ITALIC, false);

	setDefault(TEXT_FAV_BACK_COLOR, RGB(255,255,255));
	setDefault(TEXT_FAV_FORE_COLOR, RGB(0,0,0));
	setDefault(TEXT_FAV_BOLD, true);
	setDefault(TEXT_FAV_ITALIC, true);

	setDefault(TEXT_OP_BACK_COLOR, RGB(255,255,255));
	setDefault(TEXT_OP_FORE_COLOR, RGB(0,0,0));
	setDefault(TEXT_OP_BOLD, true);
	setDefault(TEXT_OP_ITALIC, false);

	setDefault(TEXT_NORM_BACK_COLOR, RGB(255,255,255));
	setDefault(TEXT_NORM_FORE_COLOR, RGB(0,0,0));
	setDefault(TEXT_NORM_BOLD, true);
	setDefault(TEXT_NORM_ITALIC, false);

	setDefault(TEXT_URL_BACK_COLOR, RGB(255,255,255));
	setDefault(TEXT_URL_FORE_COLOR, RGB(0,102,204));
	setDefault(TEXT_URL_BOLD, false);
	setDefault(TEXT_URL_ITALIC, false);
	setDefault(UNDERLINE_LINKS, true);

	setDefault(TEXT_DUPE_BACK_COLOR, RGB(255, 255, 255));
	setDefault(DUPE_COLOR, RGB(255, 128, 255));
	setDefault(TEXT_DUPE_BOLD, false);
	setDefault(TEXT_DUPE_ITALIC, false);
	setDefault(UNDERLINE_DUPES, true);

	setDefault(TEXT_QUEUE_BACK_COLOR, RGB(255, 255, 255));
	setDefault(QUEUE_COLOR, RGB(255,200,0));
	setDefault(TEXT_QUEUE_BOLD, false);
	setDefault(TEXT_QUEUE_ITALIC, false);
	setDefault(UNDERLINE_QUEUE, true);

	setDefault(LIST_HL_BG_COLOR, RGB(255, 255, 255));
	setDefault(LIST_HL_COLOR, RGB(126, 189, 202));
	setDefault(LIST_HL_BOLD, false);
	setDefault(LIST_HL_ITALIC, false);

	setDefault(KICK_MSG_RECENT_01, "");
	setDefault(KICK_MSG_RECENT_02, "");
	setDefault(KICK_MSG_RECENT_03, "");
	setDefault(KICK_MSG_RECENT_04, "");
	setDefault(KICK_MSG_RECENT_05, "");
	setDefault(KICK_MSG_RECENT_06, "");
	setDefault(KICK_MSG_RECENT_07, "");
	setDefault(KICK_MSG_RECENT_08, "");
	setDefault(KICK_MSG_RECENT_09, "");
	setDefault(KICK_MSG_RECENT_10, "");
	setDefault(KICK_MSG_RECENT_11, "");
	setDefault(KICK_MSG_RECENT_12, "");
	setDefault(KICK_MSG_RECENT_13, "");
	setDefault(KICK_MSG_RECENT_14, "");
	setDefault(KICK_MSG_RECENT_15, "");
	setDefault(KICK_MSG_RECENT_16, "");
	setDefault(KICK_MSG_RECENT_17, "");
	setDefault(KICK_MSG_RECENT_18, "");
	setDefault(KICK_MSG_RECENT_19, "");
	setDefault(KICK_MSG_RECENT_20, "");
	setDefault(WINAMP_FORMAT, "winamp(%[version]) %[state](%[title]) stats(%[percent] of %[length] %[bar])");
	setDefault(SPOTIFY_FORMAT, "/me playing: %[title]     %[link]");
	setDefault(PROGRESS_TEXT_COLOR_DOWN, RGB(255, 255, 255));
	setDefault(PROGRESS_TEXT_COLOR_UP, RGB(255, 255, 255));
	setDefault(SHOW_INFOTIPS, true);
	setDefault(MINIMIZE_ON_STARTUP, false);
	setDefault(FREE_SLOTS_DEFAULT, false);
	setDefault(ERROR_COLOR, RGB(255, 0, 0));
	setDefault(EXPAND_QUEUE, true);
	setDefault(TRANSFER_SPLIT_SIZE, 8000);
	setDefault(MENUBAR_TWO_COLORS, true);
	setDefault(MENUBAR_LEFT_COLOR, RGB(255, 64, 64));
	setDefault(MENUBAR_RIGHT_COLOR, RGB(0, 34, 102));
	setDefault(MENUBAR_BUMPED, true);

	setDefault(NORMAL_COLOUR, RGB(0, 0, 0));
	setDefault(RESERVED_SLOT_COLOR, RGB(0, 51, 0));
	setDefault(IGNORED_COLOR, RGB(192, 192, 192));
	setDefault(FAVORITE_COLOR, RGB(51, 51, 255));
	setDefault(PASIVE_COLOR, RGB(132, 132, 132));
	setDefault(OP_COLOR, RGB(0, 0, 205));

	setDefault(MAIN_WINDOW_STATE, SW_SHOWNORMAL);
	setDefault(MAIN_WINDOW_SIZE_X, CW_USEDEFAULT);
	setDefault(MAIN_WINDOW_SIZE_Y, CW_USEDEFAULT);
	setDefault(MAIN_WINDOW_POS_X, CW_USEDEFAULT);
	setDefault(MAIN_WINDOW_POS_Y, CW_USEDEFAULT);
	setDefault(MDI_MAXIMIZED, true);
	setDefault(UPLOAD_BAR_COLOR, RGB(205, 60, 55));
	setDefault(DOWNLOAD_BAR_COLOR, RGB(55, 170, 85));
	setDefault(PROGRESS_BACK_COLOR, RGB(95, 95, 95));
	setDefault(PROGRESS_COMPRESS_COLOR, RGB(222, 160, 0));
	setDefault(PROGRESS_SEGMENT_COLOR, RGB(49, 106, 197));
	setDefault(COLOR_RUNNING, RGB(0, 150, 0));
	setDefault(COLOR_DOWNLOADED, RGB(255, 255, 100));
	setDefault(COLOR_DONE, RGB(222, 160, 0));

	//AirDC   
	setDefault(TAB_ACTIVE_BG, RGB(130, 211, 244));
	setDefault(TAB_ACTIVE_TEXT, RGB(0, 0, 0));
	setDefault(TAB_ACTIVE_BORDER, RGB(0, 0, 0));
	setDefault(TAB_INACTIVE_BG, RGB(255, 255, 255));
	setDefault(TAB_INACTIVE_BG_DISCONNECTED, RGB(126, 154, 194));
	setDefault(TAB_INACTIVE_TEXT, RGB(82, 82, 82));
	setDefault(TAB_INACTIVE_BORDER, RGB(157, 157, 161));
	setDefault(TAB_INACTIVE_BG_NOTIFY, RGB(176, 169, 185));
	setDefault(TAB_DIRTY_BLEND, 10);
	setDefault(BLEND_TABS, true);
	setDefault(BACKGROUND_IMAGE, "airdc.jpg");
	setDefault(TAB_SHOW_ICONS, true);
	setDefault(TAB_SIZE, 20);
	setDefault(HUB_BOLD_TABS, true);
	setDefault(TB_PROGRESS_TEXT_COLOR, RGB(255, 0, 0));

	setDefault(POPUP_BACKCOLOR, RGB(58, 122, 180));
	setDefault(POPUP_TEXTCOLOR, RGB(0, 0, 0));
	setDefault(POPUP_TITLE_TEXTCOLOR, RGB(0, 0, 0));
#endif

	setDefault(REPORT_ALTERNATES, true);	

	setDefault(SOUNDS_DISABLED, false);
	setDefault(UPLOADQUEUEFRAME_SHOW_TREE, true);	
	setDefault(DONT_BEGIN_SEGMENT, true);
	setDefault(DONT_BEGIN_SEGMENT_SPEED, 512);

	setDefault(SEARCH_TIME, 15);
	setDefault(SUPPRESS_MAIN_CHAT, false);
	setDefault(AUTO_SLOTS, 5);	
	
	// default sounds
	setDefault(BEGINFILE, Util::emptyString);
	setDefault(BEEPFILE, Util::emptyString);
	setDefault(FINISHFILE, Util::emptyString);
	setDefault(SOURCEFILE, Util::emptyString);
	setDefault(UPLOADFILE, Util::emptyString);
	setDefault(CHATNAMEFILE, Util::emptyString);
	setDefault(SOUND_TTH, Util::emptyString);
	setDefault(SOUND_EXC, Util::emptyString);
	setDefault(SOUND_HUBCON, Util::emptyString);
	setDefault(SOUND_HUBDISCON, Util::emptyString);
	setDefault(SOUND_FAVUSER, Util::emptyString);
	setDefault(SOUND_TYPING_NOTIFY, Util::emptyString);

	setDefault(POPUP_HUB_CONNECTED, false);
	setDefault(POPUP_HUB_DISCONNECTED, false);
	setDefault(POPUP_FAVORITE_CONNECTED, true);
	setDefault(POPUP_DOWNLOAD_START, false);
	setDefault(POPUP_DOWNLOAD_FAILED, false);
	setDefault(POPUP_DOWNLOAD_FINISHED, false);
	setDefault(POPUP_UPLOAD_FINISHED, false);
	setDefault(POPUP_PM, false);
	setDefault(POPUP_NEW_PM, true);
	setDefault(POPUP_TYPE, 0);
	setDefault(POPUP_AWAY, false);
	setDefault(POPUP_MINIMIZED, true);

	setDefault(AWAY, false);
	setDefault(SHUTDOWN_ACTION, 0);
	setDefault(MINIMUM_SEARCH_INTERVAL, 5);
	setDefault(PROGRESSBAR_ODC_STYLE, true);

	setDefault(PROGRESS_3DDEPTH, 4);
	setDefault(PROGRESS_OVERRIDE_COLORS, true);
	setDefault(MAX_AUTO_MATCH_SOURCES, 5);
	setDefault(MULTI_CHUNK, true);
	setDefault(USERLIST_DBLCLICK, 0);
	setDefault(TRANSFERLIST_DBLCLICK, 0);
	setDefault(CHAT_DBLCLICK, 0);	
	setDefault(HUBFRAME_VISIBLE, "1,1,0,1,0,1,0,0,0,0,0,0");
	setDefault(DIRECTORYLISTINGFRAME_VISIBLE, "1,1,0,1,1");	
	setDefault(FINISHED_VISIBLE, "1,1,1,1,1,1,1,1");
	setDefault(FINISHED_UL_VISIBLE, "1,1,1,1,1,1,1");
	setDefault(EMOTICONS_FILE, "RadoX");
	setDefault(GROUP_SEARCH_RESULTS, true);
	setDefault(TABS_ON_TOP, false);
	setDefault(DOWNCONN_PER_SEC, 2);
	setDefault(FILTER_ENTER, false);
	setDefault(UC_SUBMENU, true);

	setDefault(DISCONNECT_SPEED, 5);
	setDefault(DISCONNECT_FILE_SPEED, 15);
	setDefault(DISCONNECT_TIME, 40);
	setDefault(DISCONNECT_FILESIZE, 50);
    setDefault(REMOVE_SPEED, 2);
	
	setDefault(SHOW_WINAMP_CONTROL, false);
	setDefault(MEDIA_PLAYER, 0);
	setDefault(WMP_FORMAT, "/me playing: %[title] at %[bitrate] <Windows Media Player %[version]>");
	setDefault(ITUNES_FORMAT, "/me playing: %[title] at %[bitrate] <iTunes %[version]>");
	setDefault(MPLAYERC_FORMAT, "/me playing: %[title] <Media Player Classic>");
	setDefault(WINAMP_PATH, "C:\\Program Files\\Winamp\\winamp.exe");
	setDefault(IGNORE_USE_REGEXP_OR_WC, true);
	setDefault(NAT_SORT, true);
	setDefault(FAV_DL_SPEED, 0);
	setDefault(IP_UPDATE, true);
	setDefault(IP_UPDATE6, false);
	setDefault(SERVER_COMMANDS, true);
	setDefault(CLIENT_COMMANDS, true);
	setDefault(SKIPLIST_SHARE, "(.*\\.(scn|asd|lnk|url|log|crc|dat|sfk|mxm))$|(rushchk.log)");
	setDefault(FREE_SLOTS_EXTENSIONS, "*.nfo|*.sfv");
	setDefault(POPUP_FONT, "MS Shell Dlg,-11,400,0");
	setDefault(POPUP_TITLE_FONT, "MS Shell Dlg,-11,400,0");
	setDefault(POPUPFILE, Util::getPath(Util::PATH_GLOBAL_CONFIG) + "popup.bmp");
	setDefault(PM_PREVIEW, true);
	setDefault(POPUP_TIME, 5);
	setDefault(MAX_MSG_LENGTH, 120);
	setDefault(SKIPLIST_DOWNLOAD, ".*|*All-Files-CRC-OK*|Descript.ion|thumbs.db|*.bad|*.missing|rushchk.log");
	setDefault(HIGH_PRIO_FILES, "*.sfv|*.nfo|*sample*|*subs*|*.jpg|*cover*|*.pls|*.m3u");
	setDefault(FLASH_WINDOW_ON_PM, false);
	setDefault(FLASH_WINDOW_ON_NEW_PM, false);
	setDefault(FLASH_WINDOW_ON_MYNICK, false);
	setDefault(AUTOSEARCH_EVERY, 15);
	setDefault(AUTOSEARCH_RECHECK_TIME, 360);
	setDefault(TB_IMAGE_SIZE, 24);
	setDefault(TB_IMAGE_SIZE_HOT, 24);
	setDefault(USE_HIGHLIGHT, false);
	setDefault(SHOW_QUEUE_BARS, true);
	setDefault(BLOOM_MODE, BLOOM_DISABLED);
	setDefault(EXPAND_DEFAULT, false);
	setDefault(SHARE_SKIPLIST_USE_REGEXP, true);
	setDefault(DOWNLOAD_SKIPLIST_USE_REGEXP, false);
	setDefault(HIGHEST_PRIORITY_USE_REGEXP, false);
	setDefault(OVERLAP_SLOW_SOURCES, true);
	setDefault(MIN_SEGMENT_SIZE, 1024);
	setDefault(OPEN_LOGS_INTERNAL, true);
	setDefault(OPEN_SYSTEM_LOG, true);
	setDefault(USE_OLD_SHARING_UI, false);
	setDefault(LAST_SEARCH_FILETYPE, "0");
	setDefault(LAST_AS_FILETYPE, "7");
	setDefault(MAX_RESIZE_LINES, 4);
	setDefault(DUPE_SEARCH, true);
	setDefault(PASSWD_PROTECT, false);
	setDefault(PASSWD_PROTECT_TRAY, false);
	setDefault(DISALLOW_CONNECTION_TO_PASSED_HUBS, false);
	setDefault(BOLD_HUB_TABS_ON_KICK, false);
	setDefault(SEARCH_USE_EXCLUDED, false);
	setDefault(AUTO_ADD_SOURCE, true);
	setDefault(KEEP_FINISHED_FILES, false);
	setDefault(USE_EXPLORER_THEME, true);
	setDefault(TESTWRITE, true);
	setDefault(INCOMING_REFRESH_TIME, 0);
	setDefault(USE_ADLS, true);
	setDefault(DONT_DL_ALREADY_QUEUED, false);
	setDefault(SYSTEM_SHOW_UPLOADS, false);
	setDefault(SYSTEM_SHOW_DOWNLOADS, false);
	setDefault(SETTINGS_PROFILE, PROFILE_NORMAL);
	setDefault(DOWNLOAD_SPEED, connectionSpeeds[0]);
	setDefault(WIZARD_RUN_NEW, true); // run wizard on startup
	setDefault(FORMAT_RELEASE, true);
	setDefault(LOG_LINES, 500);
	setDefault(CHECK_MISSING, true);
	setDefault(CHECK_SFV, false);
	setDefault(CHECK_NFO, false);
	setDefault(CHECK_MP3_DIR, false);
	setDefault(CHECK_EXTRA_SFV_NFO, false);
	setDefault(CHECK_EXTRA_FILES, false);
	setDefault(CHECK_DUPES, false);
	setDefault(CHECK_EMPTY_DIRS, true);
	setDefault(CHECK_EMPTY_RELEASES, true);
	setDefault(CHECK_USE_SKIPLIST, false);
	setDefault(CHECK_IGNORE_ZERO_BYTE, false);
	setDefault(SORT_DIRS, false);
	setDefault(MAX_FILE_SIZE_SHARED, 0);
	setDefault(MAX_MCN_DOWNLOADS, 1);
	setDefault(NO_ZERO_BYTE, false);
	setDefault(MCN_AUTODETECT, true);
	setDefault(DL_AUTODETECT, true);
	setDefault(UL_AUTODETECT, true);
	setDefault(MAX_MCN_UPLOADS, 1);
	setDefault(SKIP_SUBTRACT, 0);
	setDefault(DUPES_IN_FILELIST, true);
	setDefault(DUPES_IN_CHAT, true);
	setDefault(HIGHLIGHT_LIST, "");
	setDefault(REPORT_SKIPLIST, true);


	setDefault(SCAN_DL_BUNDLES, true);
	setDefault(USE_PARTIAL_SHARING, true);
	setDefault(POPUP_BUNDLE_DLS, true);
	setDefault(POPUP_BUNDLE_ULS, false);
	setDefault(LOG_HASHING, false);
	setDefault(RECENT_BUNDLE_HOURS, 24);
	setDefault(USE_FTP_LOGGER, false);
	setDefault(ICON_PATH, "");
	setDefault(QI_AUTOPRIO, true);
	setDefault(SHOW_SHARED_DIRS_FAV, true);
	setDefault(ALLOW_MATCH_FULL_LIST, false);
	setDefault(REPORT_ADDED_SOURCES, false);
	setDefault(EXPAND_BUNDLES, false);
	setDefault(COUNTRY_FORMAT, "%[2code] - %[name]");
	setDefault(FORMAT_DIR_REMOTE_TIME, false);
	setDefault(DISCONNECT_MIN_SOURCES, 2);
	setDefault(USE_SLOW_DISCONNECTING_DEFAULT, true);
	setDefault(PRIO_LIST_HIGHEST, false);
	setDefault(AUTOPRIO_TYPE, PRIO_BALANCED);
	setDefault(AUTOPRIO_INTERVAL, 10);
	setDefault(AUTOSEARCH_EXPIRE_DAYS, 5);
	setDefault(HORIZONTAL_QUEUE, false);
	setDefault(DL_AUTOSELECT_METHOD, 0);
	setDefault(WTB_IMAGE_SIZE, 16);
	setDefault(SHOW_TBSTATUS, true);

	setDefault(TB_PROGRESS_FONT, "Arial,-11,400,0");
	setDefault(LOCK_TB, false);
	setDefault(POPUNDER_PARTIAL_LIST, false);
	setDefault(TLS_MODE, 1);
	setDefault(LAST_SEARCH_DISABLED_HUBS, Util::emptyString);
	setDefault(UPDATE_METHOD, 2);
	setDefault(QUEUE_SPLITTER_POS, 3000);
	setDefault(UPDATE_IP_HOURLY, false);
	setDefault(POPUNDER_TEXT, false);
	setDefault(FULL_LIST_DL_LIMIT, 30000);
	setDefault(SEARCH_SAVE_HUBS_STATE, false);
	setDefault(CONFIRM_HUB_CLOSING, true);
	setDefault(CONFIRM_AS_REMOVAL, true);
	setDefault(ENABLE_SUDP, false);
	setDefault(NMDC_MAGNET_WARN, true);
	setDefault(AUTO_COMPLETE_BUNDLES, false);
	setDefault(LOG_SCHEDULED_REFRESHES, true);
	setDefault(AUTO_DETECTION_USE_LIMITED, true);
	setDefault(AS_DELAY_HOURS, 12);
	setDefault(LAST_LIST_PROFILE, 0);
	setDefault(SHOW_CHAT_NOTIFY, false);
	setDefault(FAV_USERS_SPLITTER_POS, 7500);
	setDefault(AWAY_IDLE_TIME, 5);
	setDefault(FREE_SPACE_WARN, true);
	setDefault(FAV_USERS_SHOW_INFO, true);
	setDefault(USERS_FILTER_FAVORITE, false);
	setDefault(USERS_FILTER_QUEUE, false);
	setDefault(USERS_FILTER_ONLINE, false);

	setDefault(HISTORY_SEARCH_MAX, 10);
	setDefault(HISTORY_EXCLUDE_MAX, 10);
	setDefault(HISTORY_DIR_MAX, 10);

	setDefault(HISTORY_SEARCH_CLEAR, false);
	setDefault(HISTORY_EXCLUDE_CLEAR, false);
	setDefault(HISTORY_DIR_CLEAR, false);
	setDefault(AUTOSEARCH_BOLD, true);
	setDefault(LIST_VIEW_FONT, "");
	setDefault(SHOW_EMOTICON, true);
	setDefault(SHOW_MULTILINE, true);
	setDefault(SHOW_MAGNET, true);

	//set depending on the cpu count
	setDefault(MAX_HASHING_THREADS, std::thread::hardware_concurrency());

	setDefault(HASHERS_PER_VOLUME, 1);

	setDefault(MIN_DUPE_CHECK_SIZE, 512);
	setDefault(WARN_ELEVATED, true);
	setDefault(SKIP_EMPTY_DIRS_SHARE, true);

	setDefault(LOG_SHARE_SCANS, false);
	setDefault(LOG_SHARE_SCAN_PATH, "Scan Results\\Scan %Y-%m-%d %H:%M.log");

	setDefault(LAST_FL_FILETYPE, "0");
	setDefault(ACCEPT_FAILOVERS, true);

	setDefault(DB_CACHE_SIZE, 8);
	setDefault(CUR_REMOVED_TREES, 0);
	setDefault(CUR_REMOVED_FILES, 0);

	setDefault(DL_AUTO_DISCONNECT_MODE, QUEUE_FILE);
	setDefault(REFRESH_THREADING, MULTITHREAD_MANUAL);

	setDefault(REMOVE_EXPIRED_AS, false);

	setDefault(PM_LOG_GROUP_CID, true);
	setDefault(SHARE_FOLLOW_SYMLINKS, true);
	setDefault(SCAN_MONITORED_FOLDERS, true);

#ifdef _WIN32
	setDefault(MONITORING_MODE, MONITORING_ALL);
#else
	// TODO: implement monitoring
	setDefault(MONITORING_MODE, MONITORING_DISABLED);
#endif

	setDefault(FINISHED_NO_HASH, false);
	setDefault(MONITORING_DELAY, 30);
	setDefault(DELAY_COUNT_MODE, DELAY_VOLUME);

	setDefault(CONFIRM_FILE_DELETIONS, true);
	setDefault(USE_DEFAULT_CERT_PATHS, true);
	setDefault(DCTMP_STORE_DESTINATION, true);

	setDefault(MAX_RUNNING_BUNDLES, 0);
	setDefault(DEFAULT_SP, 0);
	setDefault(STARTUP_REFRESH, true);
	setDefault(FL_REPORT_FILE_DUPES, true);
	setDefault(DATE_FORMAT, "%Y-%m-%d %H:%M");
	setDefault(SEARCH_ASCH_ONLY, false);

	setDefault(FILTER_FL_SHARED, true);
	setDefault(FILTER_FL_QUEUED, true);
	setDefault(FILTER_FL_INVERSED, false);
	setDefault(FILTER_FL_TOP, true);
	setDefault(FILTER_FL_PARTIAL_DUPES, false);
	setDefault(FILTER_FL_RESET_CHANGE, true);

	setDefault(FILTER_SEARCH_SHARED, true);
	setDefault(FILTER_SEARCH_QUEUED, true);
	setDefault(FILTER_SEARCH_INVERSED, false);
	setDefault(FILTER_SEARCH_TOP, false);
	setDefault(FILTER_SEARCH_PARTIAL_DUPES, false);
	setDefault(FILTER_SEARCH_RESET_CHANGE, true);

	// not in GUI
	setDefault(IGNORE_INDIRECT_SR, false);
	setDefault(USE_UPLOAD_BUNDLES, true);
#ifdef _WIN64
	setDefault(DECREASE_RAM, false);  
#else
	setDefault(DECREASE_RAM, true); //32 bit windows will most likely have less ram to spend (4gb max)
#endif
}

void SettingsManager::applyProfileDefaults() {
	for (const auto& newSetting: profileSettings[get(SETTINGS_PROFILE)]) {
		newSetting.setProfileToDefault(false);
	}
}

void SettingsManager::setProfile(int aProfile, const ProfileSettingItem::List& conflicts) {
	set(SettingsManager::SETTINGS_PROFILE, aProfile);
	applyProfileDefaults();

	for (const auto& setting: conflicts) {
		setting.setProfileToDefault(true);
	}
}

string SettingsManager::getProfileName(int profile) {
	switch(profile) {
		case PROFILE_NORMAL: return STRING(NORMAL);
		case PROFILE_RAR: return STRING(RAR_HUBS);
		case PROFILE_LAN: return STRING(LAN_HUBS);
		default: return STRING(NORMAL);
	}
}

void SettingsManager::load(function<bool (const string& /*Message*/, bool /*isQuestion*/, bool /*isError*/)> messageF) {
	try {
		SimpleXML xml;
		xml.loadSettingFile(CONFIG_DIR, CONFIG_NAME);
		
		xml.resetCurrentChild();
		
		if(xml.findChild("DCPlusPlus"))
		{
			xml.stepIn();
		
			if(xml.findChild("Settings"))
			{
				xml.stepIn();

				int i;
			
				for(i=STR_FIRST; i<STR_LAST; i++)
				{
					const string& attr = settingTags[i];
					dcassert(attr.find("SENTRY") == string::npos);
				
					if(xml.findChild(attr))
						set(StrSetting(i), xml.getChildData());
					xml.resetCurrentChild();
				}
				for(i=INT_FIRST; i<INT_LAST; i++)
				{
					const string& attr = settingTags[i];
					dcassert(attr.find("SENTRY") == string::npos);
				
					if(xml.findChild(attr))
						set(IntSetting(i), Util::toInt(xml.getChildData()));
					xml.resetCurrentChild();
				}

				for(i=BOOL_FIRST; i<BOOL_LAST; i++)
				{
					const string& attr = settingTags[i];
					dcassert(attr.find("SENTRY") == string::npos);

					if(xml.findChild(attr)) {
						auto val = Util::toInt(xml.getChildData());
						dcassert(val == 0 || val == 1);
						set(BoolSetting(i), val ? true : false);
					}
					xml.resetCurrentChild();
				}

				for(i=INT64_FIRST; i<INT64_LAST; i++)
				{
					const string& attr = settingTags[i];
					dcassert(attr.find("SENTRY") == string::npos);
				
					if(xml.findChild(attr))
						set(Int64Setting(i), Util::toInt64(xml.getChildData()));
					xml.resetCurrentChild();
				}
			
				xml.stepOut();
			}

			xml.resetCurrentChild();


			//load history lists
			for(int i = 0; i < HISTORY_LAST; ++i) {
				if (xml.findChild(historyTags[i])) {
					xml.stepIn();
					while(xml.findChild("HistoryItem")) {
						addToHistory(Text::toT(xml.getChildData()), (HistoryType)i);
					}
					xml.stepOut();
				}
				xml.resetCurrentChild();
			}
		
			if(xml.findChild("FileEvents")) {
				xml.stepIn();
				if(xml.findChild("OnFileComplete")) {
					StringPair sp;
					sp.first = xml.getChildAttrib("Command");
					sp.second = xml.getChildAttrib("CommandLine");
					fileEvents[ON_FILE_COMPLETE] = sp;
				}
				xml.resetCurrentChild();
				if(xml.findChild("OnDirCreated")) {
					StringPair sp;
					sp.first = xml.getChildAttrib("Command");
					sp.second = xml.getChildAttrib("CommandLine");
					fileEvents[ON_DIR_CREATED] = sp;
				}
				xml.stepOut();
			}
			xml.resetCurrentChild();

			double prevVersion = Util::toDouble(SETTING(CONFIG_VERSION));
			if (prevVersion < 2.41) {
				//previous versions have saved two settings in a wrong order... fix the dupe color here (queuebars don't matter)
				if(xml.findChild("Settings")) {
					xml.stepIn();
					const string& attr = settingTags[SHOW_QUEUE_BARS];
					if(xml.findChild(attr)) {
						set(IntSetting(DUPE_COLOR), Util::toInt(xml.getChildData()));
					}
					xml.resetCurrentChild();
					xml.stepOut();
				}

				// port previous conn settings
				enum { OLD_INCOMING_DIRECT, OLD_INCOMING_UPNP, OLD_INCOMING_NAT, OLD_INCOMING_PASSIVE };
				switch(SETTING(INCOMING_CONNECTIONS)) {
					case OLD_INCOMING_UPNP: set(INCOMING_CONNECTIONS, INCOMING_ACTIVE_UPNP); break;
					case OLD_INCOMING_PASSIVE: set(INCOMING_CONNECTIONS, INCOMING_PASSIVE); break;
					default: set(INCOMING_CONNECTIONS, INCOMING_ACTIVE); break;
				}

				/*SettingItem restoredSettings [] {
					{ SettingsManager::SHOW_JOINS, ResourceManager::FAV_SHOW_JOIN },
					{ SettingsManager::LOG_MAIN_CHAT, ResourceManager::FAV_LOG_CHAT },
					{ SettingsManager::MINIMUM_SEARCH_INTERVAL, ResourceManager::MINIMUM_SEARCH_INTERVAL },
					{ SettingsManager::SHOW_CHAT_NOTIFY, ResourceManager::CHAT_NOTIFY },
					{ SettingsManager::EXTERNAL_IP, ResourceManager::SETTINGS_EXTERNAL_IP }
				};*/

				//notify about changed fav hub settings
				auto getSettingTextStr = [this] (const string& aSettingCaption, SettingsManager::StrSetting aSetting) -> string {
					auto val = get(aSetting);
					return aSettingCaption + "\t(globally " + (!val.empty() ? val : "not set") + ")";
				};

				auto getSettingTextBool = [this] (const string& aSettingCaption, SettingsManager::BoolSetting aSetting) -> string {
					return aSettingCaption + "\t(globally " + (get(aSetting) ? "Enabled" : "Disabled") + ")";
				};

				auto getSettingTextInt = [this] (const string& aSettingCaption, SettingsManager::IntSetting aSetting) -> string {
					return aSettingCaption + "\t(globally " + Util::toString(get(aSetting)) + " seconds)";
				};

				auto getConnection = [this] {
					string text;

					if (SETTING(AUTO_DETECT_CONNECTION)) {
						text = "Auto detect";
					} else {
						switch(SETTING(INCOMING_CONNECTIONS)) {
							case SettingsManager::INCOMING_ACTIVE: text = "Active (no router or manual config)"; break;
							case SettingsManager::INCOMING_ACTIVE_UPNP: text = "Active (UPnP/NAT-PMP)"; break;
							case SettingsManager::INCOMING_PASSIVE: text = "Passive"; break;
						}
					}

					return "Incoming connection\t(globally " + text + ")";
				};

				string msg = boost::str(boost::format(
					"\rAll favorite hubs have been reset to use the global values for the following settings:\r\n\r\n\
	%s\r\n\
	%s\r\n\
	%s\r\n\
	%s\r\n\r\n\
	%s\r\n\
	%s\r\n\r\n\
	You can customize those settings for each favorite hub if needed")

					% getSettingTextBool(STRING(FAV_SHOW_JOIN) + "\t", SettingsManager::SHOW_JOINS)
					% getSettingTextBool(STRING(FAV_LOG_CHAT) + "\t\t", SettingsManager::LOG_MAIN_CHAT)
					% getSettingTextInt(STRING(MINIMUM_SEARCH_INTERVAL) + "\t", SettingsManager::MINIMUM_SEARCH_INTERVAL)
					% getSettingTextBool(STRING(CHAT_NOTIFY), SettingsManager::SHOW_CHAT_NOTIFY)
					% getSettingTextStr(STRING(SETTINGS_EXTERNAL_IP) + "\t", SettingsManager::EXTERNAL_IP)
					% getConnection());

				messageF(msg, false, false);
			}

			if(prevVersion <= 2.30 && SETTING(POPUP_TYPE) == 1)
				set(POPUP_TYPE, 0);

			//reset the old private hub profile to normal
			if(prevVersion < 2.50 && SETTING(SETTINGS_PROFILE) == PROFILE_LAN)
				unset(SETTINGS_PROFILE);

			if (prevVersion <= 2.50 && SETTING(MONITORING_MODE) != MONITORING_DISABLED) {
				set(MONITORING_MODE, MONITORING_ALL);
				set(INCOMING_REFRESH_TIME, 0);
				set(AUTO_REFRESH_TIME, 0);
			}
		
			fire(SettingsManagerListener::Load(), xml);

			xml.stepOut();
		}

	} catch(const Exception&) { 
		//...
	}

	setDefault(UDP_PORT, SETTING(TCP_PORT));

	File::ensureDirectory(SETTING(TLS_TRUSTED_CERTIFICATES_PATH));

	if(SETTING(PRIVATE_ID).length() != 39 || !CID(SETTING(PRIVATE_ID))) {
		set(SettingsManager::PRIVATE_ID, CID::generate().toBase32());
	}

	//lanMode = SETTING(SETTINGS_PROFILE) == PROFILE_LAN;
	if(SETTING(INCOMING_CONNECTIONS) == INCOMING_ACTIVE || INCOMING_ACTIVE_UPNP) {
		if(SETTING(TLS_PORT) == 0) {
			set(TLS_PORT, (int)Util::rand(10000, 32000));
		}
	}

	//check the bind address
	auto checkBind = [&] (SettingsManager::StrSetting aSetting, bool v6) {
		if (!isDefault(aSetting)) {
			AirUtil::IpList addresses;
			AirUtil::getIpAddresses(addresses, v6);
			auto p = boost::find_if(addresses, [this, aSetting](const AirUtil::AddressInfo& aInfo) { return aInfo.ip == get(aSetting); });
			if (p == addresses.end() && messageF(STRING_F(BIND_ADDRESS_MISSING, (v6 ? "IPv6" : "IPv4") % get(aSetting)), true, false)) {
				set(aSetting, Util::emptyString);
			}
		}
	};

	checkBind(BIND_ADDRESS, false);
	checkBind(BIND_ADDRESS6, true);

	applyProfileDefaults();
}

const SettingsManager::BoolSetting clearSettings[SettingsManager::HISTORY_LAST] = {
	SettingsManager::HISTORY_SEARCH_CLEAR,
	SettingsManager::HISTORY_EXCLUDE_CLEAR,
	SettingsManager::HISTORY_DIR_CLEAR
};

const SettingsManager::IntSetting maxLimits[SettingsManager::HISTORY_LAST] = {
	SettingsManager::HISTORY_SEARCH_MAX,
	SettingsManager::HISTORY_EXCLUDE_MAX,
	SettingsManager::HISTORY_DIR_MAX
};

const string SettingsManager::historyTags[] = {
	"SearchHistory",
	"ExcludeHistory",
	"DirectoryHistory"
};

bool SettingsManager::addToHistory(const tstring& aString, HistoryType aType) {
	auto& hist = history[aType];

	if(aString.empty() || get(maxLimits[aType]) == 0)
		return false;

	auto s = boost::find(hist, aString);
	if(s != hist.end()) {
		hist.erase(s);
	}

	if(static_cast<int>(hist.size()) == get(maxLimits[aType])) {
		hist.pop_front();
	}

	hist.push_back(aString);
	return true;
}

void SettingsManager::clearHistory(HistoryType aType) {
	history[aType].clear();
}

const SettingsManager::HistoryList& SettingsManager::getHistory(HistoryType aType) const {
	return history[aType];
}

void SettingsManager::save() {

	SimpleXML xml;
	xml.addTag("DCPlusPlus");
	xml.stepIn();
	xml.addTag("Settings");
	xml.stepIn();

	int i;
	string type("type"), curType("string");
	
	for(i=STR_FIRST; i<STR_LAST; i++)
	{
		if(i == CONFIG_VERSION) {
			xml.addTag(settingTags[i], VERSIONSTRING);
			xml.addChildAttrib(type, curType);
		} else if(isSet[i]) {
			xml.addTag(settingTags[i], get(StrSetting(i), false));
			xml.addChildAttrib(type, curType);
		}
	}

	curType = "int";
	for(i=INT_FIRST; i<INT_LAST; i++)
	{
		if(isSet[i]) {
			xml.addTag(settingTags[i], get(IntSetting(i), false));
			xml.addChildAttrib(type, curType);
		}
	}

	for(i=BOOL_FIRST; i<BOOL_LAST; i++)
	{
		if(isSet[i]) {
			xml.addTag(settingTags[i], get(BoolSetting(i), false));
			xml.addChildAttrib(type, curType);
		}
	}

	curType = "int64";
	for(i=INT64_FIRST; i<INT64_LAST; i++)
	{
		if(isSet[i])
		{
			xml.addTag(settingTags[i], get(Int64Setting(i), false));
			xml.addChildAttrib(type, curType);
		}
	}
	xml.stepOut();

	for(int i = 0; i < HISTORY_LAST; ++i) {
		const auto& hist = history[i];
		if (!hist.empty() && !get(clearSettings[i])) {
			xml.addTag(historyTags[i]);
			xml.stepIn();
			const auto& hist = history[i];
			for (auto& hi: hist) {
				xml.addTag("HistoryItem", Text::fromT(hi));
			}
			xml.stepOut();
		}
	}

	xml.addTag("FileEvents");
	xml.stepIn();
	xml.addTag("OnFileComplete");
	xml.addChildAttrib("Command", fileEvents[ON_FILE_COMPLETE].first);
	xml.addChildAttrib("CommandLine", fileEvents[ON_FILE_COMPLETE].second);
	xml.addTag("OnDirCreated");
	xml.addChildAttrib("Command", fileEvents[ON_DIR_CREATED].first);
	xml.addChildAttrib("CommandLine", fileEvents[ON_DIR_CREATED].second);
	xml.stepOut();


	fire(SettingsManagerListener::Save(), xml);

	try {
		xml.saveSettingFile(CONFIG_DIR, CONFIG_NAME);
	} catch(const Exception& e) {
		LogManager::getInstance()->message("Failed to save the setting file: " + e.getError(), LogManager::LOG_ERROR);
	}
	/*try {
		File out(aFileName + ".tmp", File::WRITE, File::CREATE | File::TRUNCATE);
		BufferedOutputStream<false> f(&out);
		f.write(SimpleXML::utf8Header);
		xml.toXML(&f);
		f.flush();
		out.close();
		File::deleteFile(aFileName);
		File::renameFile(aFileName + ".tmp", aFileName);
	} catch(...) {
		// ...
	}*/
}

HubSettings SettingsManager::getHubSettings() const {
	HubSettings ret;
	ret.get(HubSettings::Nick) = get(NICK);
	ret.get(HubSettings::Description) = get(DESCRIPTION);
	ret.get(HubSettings::Email) = get(EMAIL);
	ret.get(HubSettings::ShowJoins) = get(SHOW_JOINS);
	ret.get(HubSettings::FavShowJoins) = get(FAV_SHOW_JOINS);
	ret.get(HubSettings::LogMainChat) = get(LOG_MAIN_CHAT);
	ret.get(HubSettings::SearchInterval) = get(MINIMUM_SEARCH_INTERVAL);
	ret.get(HubSettings::Connection) = CONNSETTING(INCOMING_CONNECTIONS);
	ret.get(HubSettings::Connection6) = CONNSETTING(INCOMING_CONNECTIONS6);
	ret.get(HubSettings::ChatNotify) = get(SHOW_CHAT_NOTIFY);
	ret.get(HubSettings::AwayMsg) = get(DEFAULT_AWAY_MESSAGE);
	ret.get(HubSettings::AcceptFailovers) = get(ACCEPT_FAILOVERS);
	return ret;
}

} // namespace dcpp
