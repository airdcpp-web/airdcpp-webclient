/*
 * Copyright (C) 2001-2024 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
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
#include "DCPlusPlus.h"
#include "Exception.h"
#include "File.h"
#include "HubSettings.h"
#include "LogManager.h"
#include "Mapper_MiniUPnPc.h"
#include "NetworkUtil.h"
#include "PathUtil.h"
#include "ResourceManager.h"
#include "SimpleXML.h"
#include "StringTokenizer.h"
#include "SystemUtil.h"
#include "Util.h"
#include "version.h"

#include <thread>

namespace dcpp {

#define CONFIG_NAME "DCPlusPlus.xml"
#define CONFIG_DIR AppUtil::PATH_USER_CONFIG

StringList SettingsManager::connectionSpeeds = { "0.1", "0.2", "0.5", "1", "2", "5", "8", "10", "20", "30", "40", "50", "60", "100", "200", "1000" };


const ResourceManager::Strings SettingsManager::encryptionStrings[TLS_LAST] { ResourceManager::DISABLED, ResourceManager::ENABLED, ResourceManager::ENCRYPTION_FORCED };
const ResourceManager::Strings SettingsManager::bloomStrings[BLOOM_LAST] { ResourceManager::DISABLED, ResourceManager::ENABLED, ResourceManager::AUTO };
const ResourceManager::Strings SettingsManager::profileStrings[PROFILE_LAST] { ResourceManager::NORMAL, ResourceManager::RAR_HUBS, ResourceManager::LAN_HUBS };
const ResourceManager::Strings SettingsManager::refreshStrings[MULTITHREAD_LAST] { ResourceManager::NEVER, ResourceManager::MANUAL_REFRESHES, ResourceManager::ALWAYS };
const ResourceManager::Strings SettingsManager::prioStrings[PRIO_LAST] { ResourceManager::DISABLED, ResourceManager::PRIOPAGE_ORDER_BALANCED, ResourceManager::PRIOPAGE_ORDER_PROGRESS };
const ResourceManager::Strings SettingsManager::incomingStrings[INCOMING_LAST] { ResourceManager::DISABLED, ResourceManager::SETTINGS_ACTIVE, ResourceManager::SETTINGS_ACTIVE_UPNP, ResourceManager::SETTINGS_PASSIVE };
const ResourceManager::Strings SettingsManager::outgoingStrings[OUTGOING_LAST] { ResourceManager::SETTINGS_DIRECT, ResourceManager::SETTINGS_SOCKS5 };
const ResourceManager::Strings SettingsManager::dropStrings[QUEUE_LAST] { ResourceManager::FILE, ResourceManager::BUNDLE, ResourceManager::ALL };
const ResourceManager::Strings SettingsManager::updateStrings[VERSION_LAST] { ResourceManager::CHANNEL_STABLE, ResourceManager::CHANNEL_BETA, ResourceManager::CHANNEL_NIGHTLY };


void SettingsManager::registerChangeHandler(const SettingKeyList& aKeys, SettingChangeHandler::OnSettingChangedF&& changeF) noexcept {
	settingChangeHandlers.push_back(SettingChangeHandler({ changeF, aKeys }));
}

SettingsManager::SettingValue SettingsManager::getSettingValue(int key, bool useDefault) const noexcept {
	if (key >= SettingsManager::STR_FIRST && key < SettingsManager::STR_LAST) {
		return SettingsManager::getInstance()->get(static_cast<SettingsManager::StrSetting>(key), useDefault);
	} else if (key >= SettingsManager::INT_FIRST && key < SettingsManager::INT_LAST) {
		return SettingsManager::getInstance()->get(static_cast<SettingsManager::IntSetting>(key), useDefault);
	} else if (key >= SettingsManager::BOOL_FIRST && key < SettingsManager::BOOL_LAST) {
		return SettingsManager::getInstance()->get(static_cast<SettingsManager::BoolSetting>(key), useDefault);
	} else {
		dcassert(0);
	}

	return 0;
}

SettingsManager::EnumStringMap SettingsManager::getEnumStrings(int aKey, bool aValidateCurrentValue) noexcept {
	EnumStringMap ret;

	auto insertStrings = [&](const ResourceManager::Strings* aStrings, int aMax, int aMin = 0) {
		auto cur = getInstance()->get(static_cast<IntSetting>(aKey));
		if (!aValidateCurrentValue || (cur >= aMin && cur < aMax)) {
			// The string array indexing always starts from 0
			for (int i = 0; i < aMax; i++) {
				ret.emplace(i + aMin, aStrings[i]);
			}
		}
	};

	if ((aKey == INCOMING_CONNECTIONS || aKey == INCOMING_CONNECTIONS6)) {
		insertStrings(incomingStrings, INCOMING_LAST, -1);
	}

	if (aKey == REFRESH_THREADING) {
		insertStrings(refreshStrings, MULTITHREAD_LAST);
	}

	if (aKey == TLS_MODE) {
		insertStrings(encryptionStrings, TLS_LAST);
	}

	if (aKey == OUTGOING_CONNECTIONS) {
		insertStrings(outgoingStrings, OUTGOING_LAST);
	}

	if (aKey == DL_AUTO_DISCONNECT_MODE) {
		insertStrings(dropStrings, QUEUE_LAST);
	}

	if (aKey == BLOOM_MODE) {
		insertStrings(bloomStrings, BLOOM_LAST);
	}

	if (aKey == AUTOPRIO_TYPE) {
		insertStrings(prioStrings, PRIO_LAST);
	}

	if (aKey == SETTINGS_PROFILE) {
		insertStrings(profileStrings, PROFILE_LAST);
	}

	return ret;
}

// Every profile should contain the same setting keys
const ProfileSettingItem::List SettingsManager::profileSettings[SettingsManager::PROFILE_LAST] = {

{
	// profile normal
	{ SettingsManager::MULTI_CHUNK, true, ResourceManager::SEGMENTS },
	{ SettingsManager::MINIMUM_SEARCH_INTERVAL, 10, ResourceManager::MINIMUM_SEARCH_INTERVAL },
	{ SettingsManager::AUTO_FOLLOW, true, ResourceManager::SETTINGS_AUTO_FOLLOW },
#ifdef HAVE_GUI
	{ SettingsManager::TOOLBAR_ORDER, SettingsManager::buildToolbarOrder(SettingsManager::getDefaultToolbarOrder()), ResourceManager::TOOLBAR_ORDER },
#endif
}, {
	// profile RAR
	{ SettingsManager::MULTI_CHUNK, false, ResourceManager::SEGMENTS },
	{ SettingsManager::MINIMUM_SEARCH_INTERVAL, 5, ResourceManager::MINIMUM_SEARCH_INTERVAL },
	{ SettingsManager::AUTO_FOLLOW, false, ResourceManager::SETTINGS_AUTO_FOLLOW },
#ifdef HAVE_GUI
	{ SettingsManager::TOOLBAR_ORDER, SettingsManager::buildToolbarOrder({
		ToolbarIconEnum::RECONNECT,
		ToolbarIconEnum::DIVIDER,

		ToolbarIconEnum::FAVORITE_HUBS,
		ToolbarIconEnum::USERS,
		ToolbarIconEnum::DIVIDER,

		ToolbarIconEnum::QUEUE,
		ToolbarIconEnum::UPLOAD_QUEUE,
		ToolbarIconEnum::FINISHED_UPLOADS,
		ToolbarIconEnum::DIVIDER,

		ToolbarIconEnum::SEARCH,
		ToolbarIconEnum::ADL_SEARCH,
		ToolbarIconEnum::AUTO_SEARCH,
		ToolbarIconEnum::DIVIDER,

		ToolbarIconEnum::NOTEPAD,
		ToolbarIconEnum::SYSTEM_LOG,
		ToolbarIconEnum::DIVIDER,

		ToolbarIconEnum::REFRESH_FILELIST,
		ToolbarIconEnum::EXTENSIONS,
		ToolbarIconEnum::DIVIDER,

		ToolbarIconEnum::OPEN_FILELIST,
		ToolbarIconEnum::OPEN_DOWNLOADS,
		ToolbarIconEnum::DIVIDER,

		ToolbarIconEnum::SETTINGS
	}), ResourceManager::TOOLBAR_ORDER },
#endif
}, {
	// profile LAN
	{ SettingsManager::MULTI_CHUNK, true, ResourceManager::SEGMENTS },
	{ SettingsManager::MINIMUM_SEARCH_INTERVAL, 5, ResourceManager::MINIMUM_SEARCH_INTERVAL },
	{ SettingsManager::AUTO_FOLLOW, true, ResourceManager::SETTINGS_AUTO_FOLLOW },
#ifdef HAVE_GUI
	{ SettingsManager::TOOLBAR_ORDER, SettingsManager::buildToolbarOrder(SettingsManager::getDefaultToolbarOrder()), ResourceManager::TOOLBAR_ORDER },
#endif
} 

};

const string SettingsManager::settingTags[] =
{
	// Strings

	// Generic
	"Nick", "UploadSpeed", "DownloadSpeed", "Description", "DownloadDirectory", "EMail", "ExternalIp", "ExternalIp6",
	"LanguageFile", "HublistServers",  "HttpProxy", "Mapper",
	"BindAddress", "BindAddress6", "SocksServer", "SocksUser", "SocksPassword", "ConfigVersion", "ConfigName",
	"DefaultAwayMessage", "TimeStampsFormat", "CID", "NmdcEncoding",

	"LogDirectory", "LogFormatPostDownload", "LogFormatPostUpload", "LogFormatMainChat", "LogFormatPrivateChat",
	"LogFileMainChat","LogFilePrivateChat", "LogFileStatus", "LogFileUpload", 
	"LogFileDownload", "LogFileSystem", "LogFormatSystem", "LogFormatStatus", 
	"TLSPrivateKeyFile", "TLSCertificateFile", "TLSTrustedCertificatesPath",
	"CountryFormat", "DateFormat", "SkiplistShare", "FreeSlotsExtensions", "SkiplistDownload", "HighPrioFiles",
	"AsDefaultFailedGroup",

#ifdef HAVE_GUI
	// Windows GUI
	"Font", "TransferViewOrder", "TransferViewWidths", "HubFrameOrder", "HubFrameWidths",
	"SearchFrameOrder", "SearchFrameWidths", "FavoritesFrameOrder", "FavoritesFrameWidths",
	"QueueFrmOrder", "QueueFrmWidths", "PublicHubsFrameOrder", "PublicHubsFrameWidths",
	"UsersFrmOrder2", "UsersFrmWidths2", "FinishedOrder", "FinishedWidths", "ADLSearchFrameOrder", "ADLSearchFrameWidths",
	"FinishedULWidths", "FinishedULOrder", "SpyFrameWidths", "SpyFrameOrder",
	"FinishedVisible", "FinishedULVisible", "DirectoryListingFrameVisible",
	"RecentFrameOrder", "RecentFrameWidths", "DirectoryListingFrameOrder", "DirectoryListingFrameWidths",
	"MainFrameVisible", "SearchFrameVisible", "QueueFrameVisible", "HubFrameVisible", "UploadQueueFrameVisible",
	"EmoticonsFile",

	"BeepFile", "BeginFile", "FinishedFile", "SourceFile", "UploadFile", "ChatNameFile", "WinampFormat",
	"KickMsgRecent01", "KickMsgRecent02", "KickMsgRecent03", "KickMsgRecent04", "KickMsgRecent05",
	"KickMsgRecent06", "KickMsgRecent07", "KickMsgRecent08", "KickMsgRecent09", "KickMsgRecent10",
	"KickMsgRecent11", "KickMsgRecent12", "KickMsgRecent13", "KickMsgRecent14", "KickMsgRecent15",
	"KickMsgRecent16", "KickMsgRecent17", "KickMsgRecent18", "KickMsgRecent19", "KickMsgRecent20",
	"ToolbarOrder", "UploadQueueFrameOrder", "UploadQueueFrameWidths",
	"SoundException", "SoundHubConnected", "SoundHubDisconnected", "SoundFavUserOnline", "SoundTypingNotify",

	"BackgroundImage", "MPLAYERCformat", "ITUNESformat", "WMPformat", "Spotifyformat", "WinampPath",
	"PopupFont", "PopupTitleFont", "PopupFile", 
	"MediaToolbar", "password", "HighlightList", "IconPath",
	"AutoSearchFrame2Order", "AutoSearchFrame2Widths", "ToolbarPos", "TBProgressFont", "LastSearchFiletype", "LastSearchDisabledHubs", "LastASFiletype", "LastSearchExcluded",
	"UsersFrmVisible2", "ListViewFont", "LastFilelistFiletype", "AutosearchFrmVisible",
	"RssFrameOrder", "RssFrameWidths", "RssFrameVisible",
#endif
	"SENTRY",

	// Ints

	// Generic
	"IncomingConnections", "IncomingConnections6", "InPort", "Slots", 
	"BufferSize", "DownloadSlots", "MaxDownloadSpeed", "MinUploadSpeed", "SocksPort",
	"MaxCompression", "SetMinislotSize", "ShutdownInterval", "ExtraSlots", "ExtraPartialSlots", "ExtraDownloadSlots",

	"DisconnectSpeed", "DisconnectFileSpeed", "DisconnectTime", "RemoveSpeed", 
	"DisconnectFileSize", "NumberOfSegments", "MaxHashSpeed", "PMLogLines", "SearchTime",
	"MinimumSearchInterval", "MaxAutoMatchSource", 
	"UDPPort", "OutgoingConnections", "SocketInBuffer", "SocketOutBuffer",
	"AutoRefreshTime", "AutoSearchLimit", "MaxCommandLength", "TLSPort", "DownConnPerSec", 
	"HighestPrioSize", "HighPrioSize", "NormalPrioSize", "LowPrioSize",

	"BandwidthLimitStart", "BandwidthLimitEnd", "MaxDownloadSpeedRealTime",
	"MaxUploadSpeedTime", "MaxDownloadSpeedPrimary", "MaxUploadSpeedPrimary",
	"SlotsAlternateLimiting", "SlotsPrimaryLimiting",

	"MaxFileSizeShared", "MinSegmentSize", "AutoSlots",  "IncomingRefreshTime", 
	"ConfigBuildNumber", "PmMessageCache", "HubMessageCache", "LogMessageCache", "MaxRecentHubs", "MaxRecentPrivateChats", "MaxRecentFilelists",


	"FavDownloadSpeed", "SettingsProfile", "LogLines", "MaxMCNDownloads", "MaxMCNUploads",
	"RecentBundleHours", "DisconnectMinSources", "AutoprioType", "AutoprioInterval", "AutosearchExpireDays",  "TLSMode", "UpdateMethod",

	"FullListDLLimit", "LastListProfile", "MaxHashingThreads", "HashersPerVolume", "SubtractlistSkip", "BloomMode", "AwayIdleTime",
	"SearchHistoryMax", "ExcludeHistoryMax", "DirectoryHistoryMax", "MinDupeCheckSize", "DbCacheSize", "DLAutoDisconnectMode", 
	"RemovedTrees", "RemovedFiles", "MultithreadedRefresh",
	"MaxRunningBundles", "DefaultShareProfile", "UpdateChannel",

	"AutoSearchEvery", "ASDelayHours",

#ifdef HAVE_GUI
	// Windows GUI
	"BackgroundColor", "TextColor", "MainWindowState",
	"MainWindowSizeX", "MainWindowSizeY", "MainWindowPosX", "MainWindowPosY", "MaxTabRows",
	"DownloadBarColor", "UploadBarColor", "MenubarLeftColor", "MenubarRightColor", "SearchAlternateColour",
	"ReservedSlotColor", "IgnoredColor", "FavoriteColor", "NormalColour",
	"PasiveColor", "OpColor", "ProgressBackColor", "ProgressSegmentColor", "ColorDone",

	"MagnetAction", "PopupType", "ShutdownAction",
	"UserListDoubleClick", "TransferListDoubleClick", "ChatDoubleClick",

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
	"ProgressTextDown", "ProgressTextUp", "ErrorColor", "TransferSplitSize",

	"tabactivebg", "TabActiveText", "TabActiveBorder", "TabInactiveBg", "TabInactiveBgDisconnected", "TabInactiveText", 
	"TabInactiveBorder", "TabInactiveBgNotify", "TabDirtyBlend", "TabSize", "MediaPlayer",
	"PopupTime", "MaxMsgLength", "PopupBackColor", "PopupTextColor", "PopupTitleTextColor", 
	"TbImageSize", "TbImageSizeHot", "MaxResizeLines",
	"DupeColor", "TextDupeBackColor", 
	"TextNormBackColor", "TextNormForeColor", 
	"FavTop", "FavBottom", "FavLeft", "FavRight", "SyslogTop", "SyslogBottom", "SyslogLeft", "SyslogRight", "NotepadTop", "NotepadBottom",
	"NotepadLeft", "NotepadRight", "QueueTop", "QueueBottom", "QueueLeft", "QueueRight", "SearchTop", "SearchBottom", "SearchLeft", "SearchRight", "UsersTop", "UsersBottom",
	"UsersLeft", "UsersRight", "FinishedTop", "FinishedBottom", "FinishedLeft", "FinishedRight", "TextTop", "TextBottom", "TextLeft", "TextRight", "DirlistTop", "DirlistBottom",
	"DirlistLeft", "DirlistRight", "StatsTop", "StatsBottom", "StatsLeft", "StatsRight", 

	"ListHighlightBackColor", "ListHighlightColor", "QueueColor", "TextQueueBackColor", "QueueSplitterPosition",
	"WinampBarIconSize", "TBProgressTextColor",
	"ColorStatusFinished", "ColorStatusShared", "ProgressLighten", "FavUsersSplitterPos",
#endif
	"SENTRY",

	// Bools

	// Generic
	"AdlsBreakOnFirst",
	"AllowUntrustedClients", "AllowUntrustedHubs",
	"AutoDetectIncomingConnection", "AutoDetectIncomingConnection6", "AutoFollow", "AutoKick", "AutoKickNoFavs", "AutoSearch",
	"CompressTransfers",

	"DontDlAlreadyQueued", "DontDLAlreadyShared", "FavShowJoins", "FilterMessages",
	"GetUserCountry", "GetUserInfo", "HubUserCommands", "KeepLists",
	"LogDownloads", "LogFilelistTransfers", "LogFinishedDownloads", "LogMainChat",
	"LogPrivateChat", "LogStatusMessages", "LogSystem", "LogUploads",

	"SocksResolve", "NoAwayMsgToBots", "NoIpOverride", "LowestPrio", "ShareHidden", "ShowJoins",
	"TimeDependentThrottle", "TimeStamps",
	"SearchPassiveAlways", "RemoveForbidden", "MultiChunk", "Away",
		
	"SegmentsManual", "ReportFoundAlternates", "UseAutoPriorityByDefault",

	"AutoDetectionUseLimited", "LogScheduledRefreshes", "AutoCompleteBundles",
	"EnableSUDP", "NmdcMagnetWarn", "UpdateIPHourly",
	"UseSlowDisconnectingDefault", "PrioListHighest",
	"QIAutoPrio", "ReportAddedSources", "OverlapSlowUser", "FormatDirRemoteTime",
	"LogHashedFiles", "UsePartialSharing",
	"ReportBlockedShare", "MCNAutoDetect", "DLAutoDetect", "ULAutoDetect",
	"DupesInFilelists", "DupesInChat", "NoZeroByte",

	"SystemShowUploads", "SystemShowDownloads", "WizardRunNew", "FormatRelease",
	"UseAdls", "DupeSearch", "DisAllowConnectionToPassedHubs", "AutoAddSource",
	"ShareSkiplistUseRegexp", "DownloadSkiplistUseRegexp", "HighestPriorityUseRegexp", "UseHighlight",
	"IPUpdate",

	"IgnoreUseRegexpOrWc", "AllowMatchFullList", "ShowChatNotify", "FreeSpaceWarn",
	"ClearDirectoryHistory", "ClearExcludeHistory", "ClearDirHistory", "NoIpOverride6", "IPUpdate6",
	"SkipEmptyDirsShare", "RemoveExpiredAs", "AdcLogGroupCID", "ShareFollowSymlinks", "UseDefaultCertPaths", "StartupRefresh",
	"FLReportDupeFiles", "UseUploadBundles", "LogIgnored", "RemoveFinishedBundles", "AlwaysCCPM",

	"PopupBotPms", "PopupHubPms", "SortFavUsersFirst",
#ifdef HAVE_GUI
	// Windows GUI
	"BoldFinishedDownloads", "BoldFinishedUploads", "BoldHub", "BoldPm",
	"BoldQueue", "BoldSearch", "BoldSystemLog", "ClearSearch", "DefaultSearchFreeSlots",
	"ConfirmADLSRemoval", "ConfirmExit", "ConfirmHubRemoval", "ConfirmUserRemoval",

	"MagnetAsk", "MagnetRegister", "MinimizeToTray",
	"PopunderFilelist", "PopunderPm", "PromptPassword",
	"ShowMenuBar", "ShowStatusbar", "ShowToolbar",
	"ShowTransferview", "StatusInChat", "ShowIpCountryChat",

	"ToggleActiveTab", "UrlHandler", "UseCTRLForLineHistory", "UseSystemIcons",
	"UsersFilterFavorite", "UsersFilterOnline", "UsersFilterQueue", "UsersFilterWaiting",
	"PrivateMessageBeep", "PrivateMessageBeepOpen", "ShowProgressBars", "MDIMaxmimized",

	"ShowInfoTips", "MinimizeOnStratup", "ConfirmDelete",
	"SpyFrameIgnoreTthSearches", "OpenWaitingUsers", "BoldWaitingUsers", "TabsOnTop",
	"OpenPublic", "OpenFavoriteHubs", "OpenFavoriteUsers", "OpenQueue",
	"OpenFinishedUploads", "OpenSearchSpy", "OpenNotepad", "ProgressbaroDCStyle",

	"PopupAway", "PopupMinimized", "PopupHubConnected", "PopupHubDisconnected", "PopupFavoriteConnected",
	"PopupDownloadStart", "PopupDownloadFailed", "PopupDownloadFinished", "PopupUploadFinished", "PopupPm", "PopupNewPM",

	"UploadQueueFrameShowTree", "SoundsDisabled", "UseOldSharingUI",
	"TextGeneralBold", "TextGeneralItalic", "TextMyOwnBold", "TextMyOwnItalic", "TextPrivateBold", "TextPrivateItalic", "TextSystemBold",
	"TextSystemItalic", "TextServerBold", "TextServerItalic", "TextTimestampBold", "TextTimestampItalic",
	"TextMyNickBold", "TextMyNickItalic", "TextFavBold", "TextFavItalic", "TextOPBold", "TextOPItalic", "TextURLBold", "TextURLItalic",
	"ProgressOverrideColors", "ProgressOverrideColors2", "MenubarTwoColors", "MenubarBumped",

	"SearchSaveHubsState", "ConfirmHubExit", "ConfirmASRemove", 
	"OpenTextOnBackground", "LockTB", "PopunderPartialList", "ShowTBStatusBar", "ShowSharedDirsFav", "ExpandBundles",
		
	"TextQueueBold", "TextQueueItalic", "UnderlineQueue", 
	"PopupBundleDLs", "PopupBundleULs", "ListHighlightBold", "ListHighlightItalic",
	"TextDupeBold", "TextDupeItalic", "UnderlineLinks", "UnderlineDupes", 

	"SortDirs", "TextNormBold", "TextNormItalic", 
	"passwd_protect", "passwd_protect_tray", "BoldHubTabsOnKick",
	"UseExplorerTheme", "TestWrite", "OpenSystemLog", "OpenLogsInternal", "UcSubMenu", "ShowQueueBars", "ExpandDefault",
	"FlashWindowOnPm", "FlashWindowOnNewPm", "FlashWindowOnMyNick",

	"serverCommands", "ClientCommands", "PreviewPm", 
		
	"HubBoldTabs", "showWinampControl", "BlendTabs", "TabShowIcons", 
	"FavUsersShowInfo", "SearchUseExcluded", "AutoSearchBold", "ShowEmoticon", "ShowMultiline", "ShowMagnet", "ShowSendMessage", "WarnElevated",
	"ConfirmFileDeletions", "CloseMinimize",

	"FilterFLShared", "FilterFLQueued", "FilterFLInversed", "FilterFLTop", "FilterFLPartialDupes", "FilterFLResetChange", "FilterSearchShared", 
	"FilterSearchQueued", "FilterSearchInversed", "FilterSearchTop", "FilterSearchPartialDupes", "FilterSearchResetChange", "SearchAschOnlyMan", 
		
	"UsersFilterIgnore", "NfoExternal", "SingleClickTray", "QueueShowFinished", 
	"FilterQueueInverse", "FilterQueueTop", "FilterQueueReset", "OpenAutoSearch", "SaveLastState",
#endif
	"SENTRY",

	// Int64
	"TotalUpload", "TotalDownload",
	"SENTRY"
};

SettingsManager::SettingsManager() : connectionRegex("(\\d+(\\.\\d+)?)")
{
	setDefault(NICK, SystemUtil::getSystemUsername());

	setDefault(MAX_UPLOAD_SPEED_MAIN, 0);
	setDefault(MAX_DOWNLOAD_SPEED_MAIN, 0);
	setDefault(TIME_DEPENDENT_THROTTLE, false);
	setDefault(MAX_DOWNLOAD_SPEED_ALTERNATE, 0);
	setDefault(MAX_UPLOAD_SPEED_ALTERNATE, 0);
	setDefault(BANDWIDTH_LIMIT_START, 1);
	setDefault(BANDWIDTH_LIMIT_END, 1);
	setDefault(SLOTS_ALTERNATE_LIMITING, 1);
	
	setDefault(DOWNLOAD_DIRECTORY, AppUtil::getPath(AppUtil::PATH_DOWNLOADS));
	setDefault(UPLOAD_SLOTS, 2);
	setDefault(MAX_COMMAND_LENGTH, 512*1024); // 512 KiB

	setDefault(BIND_ADDRESS, "0.0.0.0");
	setDefault(BIND_ADDRESS6, "::");

	setDefault(TCP_PORT, 0);
	setDefault(UDP_PORT, 0);
	setDefault(TLS_PORT, 0);

	setDefault(MAPPER, Mapper_MiniUPnPc::name);
	setDefault(INCOMING_CONNECTIONS, INCOMING_ACTIVE);

	//TODO: check whether we have ipv6 available
	setDefault(INCOMING_CONNECTIONS6, INCOMING_ACTIVE);

	setDefault(OUTGOING_CONNECTIONS, OUTGOING_DIRECT);
	setDefault(AUTO_DETECT_CONNECTION, true);
	setDefault(AUTO_DETECT_CONNECTION6, true);

	setDefault(AUTO_FOLLOW, true);
	setDefault(SHARE_HIDDEN, false);
	setDefault(FILTER_MESSAGES, true);
	setDefault(AUTO_SEARCH, true);
	setDefault(TIME_STAMPS, true);
	setDefault(BUFFER_SIZE, 256);
	setDefault(HUBLIST_SERVERS, "https://www.te-home.net/?do=hublist&get=hublist.xml.bz2;https://dchublist.org/hublist.xml.bz2;https://dchublist.ru/hublist.xml.bz2;https://dcnf.github.io/Hublist/hublist.xml.bz2;https://hublist.pwiam.com/hublist.xml.bz2;");
	setDefault(DOWNLOAD_SLOTS, 50);
	setDefault(MAX_DOWNLOAD_SPEED, 0);
	setDefault(LOG_DIRECTORY, AppUtil::getPath(AppUtil::PATH_USER_CONFIG) + "Logs" PATH_SEPARATOR_STR);
	setDefault(LOG_UPLOADS, false);
	setDefault(LOG_DOWNLOADS, false);
	setDefault(LOG_PRIVATE_CHAT, false);
	setDefault(LOG_MAIN_CHAT, false);
	setDefault(SHOW_JOINS, false);
	setDefault(UPLOAD_SPEED, connectionSpeeds[0]);
	setDefault(MIN_UPLOAD_SPEED, 0);
	setDefault(LOG_FORMAT_POST_DOWNLOAD, "%Y-%m-%d %H:%M: %[target] " + STRING(DOWNLOADED_FROM) + " %[userNI] (%[userCID]), %[fileSI] (%[fileSIchunk]), %[speed], %[time]");
	setDefault(LOG_FORMAT_POST_UPLOAD, "%Y-%m-%d %H:%M: %[source] " + STRING(UPLOADED_TO) + " %[userNI] (%[userCID]), %[fileSI] (%[fileSIchunk]), %[speed], %[time]");
	setDefault(LOG_FORMAT_MAIN_CHAT, "[%Y-%m-%d %H:%M] %[message]");
	setDefault(LOG_FORMAT_PRIVATE_CHAT, "[%Y-%m-%d %H:%M] %[message]");
	setDefault(LOG_FORMAT_STATUS, "[%Y-%m-%d %H:%M] %[message]");
	setDefault(LOG_FORMAT_SYSTEM, "[%Y-%m-%d %H:%M] %[message]");
	setDefault(LOG_FILE_MAIN_CHAT, "%[hubURL].log");
	setDefault(LOG_FILE_STATUS, "%[hubURL]_status.log");
	setDefault(LOG_FILE_PRIVATE_CHAT, "PM" + string(PATH_SEPARATOR_STR) + "%B - %Y" + string(PATH_SEPARATOR_STR) + "%[userNI].log");
	setDefault(LOG_FILE_UPLOAD, "Uploads.log");
	setDefault(LOG_FILE_DOWNLOAD, "Downloads.log");
	setDefault(LOG_FILE_SYSTEM, "%Y-%m-system.log");
	setDefault(GET_USER_INFO, true);
	setDefault(SOCKS_PORT, 1080);
	setDefault(SOCKS_RESOLVE, true);
	setDefault(CONFIG_VERSION, "0.181");		// 0.181 is the last version missing configversion
	setDefault(KEEP_LISTS, false);
	setDefault(AUTO_KICK, false);
	setDefault(COMPRESS_TRANSFERS, true);
	setDefault(DEFAULT_AWAY_MESSAGE, "I'm away. State your business and I might answer later if you're lucky.");
	setDefault(TIME_STAMPS_FORMAT, "%H:%M:%S");
	setDefault(MAX_COMPRESSION, 6);
	setDefault(NO_AWAYMSG_TO_BOTS, true);
	setDefault(ADLS_BREAK_ON_FIRST, false);
	setDefault(HUB_USER_COMMANDS, true);
	setDefault(LOG_FILELIST_TRANSFERS, false);
	setDefault(LOG_SYSTEM, true);
	setDefault(MAX_HASH_SPEED, 0);
	setDefault(GET_USER_COUNTRY, true);
	setDefault(FAV_SHOW_JOINS, false);
	setDefault(LOG_STATUS_MESSAGES, false);

	setDefault(DONT_DL_ALREADY_SHARED, false);
	setDefault(MAX_PM_HISTORY_LINES, 10);
	setDefault(SET_MINISLOT_SIZE, 512);
	setDefault(PRIO_HIGHEST_SIZE, 64);
	setDefault(PRIO_HIGH_SIZE, 0);
	setDefault(PRIO_NORMAL_SIZE, 0);
	setDefault(PRIO_LOW_SIZE, 0);
	setDefault(PRIO_LOWEST, false);
	setDefault(NO_IP_OVERRIDE, false);
	setDefault(NO_IP_OVERRIDE6, false);
	setDefault(SOCKET_IN_BUFFER, 0); // OS default
	setDefault(SOCKET_OUT_BUFFER, 0); // OS default
	setDefault(TLS_TRUSTED_CERTIFICATES_PATH, AppUtil::getPath(AppUtil::PATH_USER_CONFIG) + "Certificates" PATH_SEPARATOR_STR);
	setDefault(TLS_PRIVATE_KEY_FILE, AppUtil::getPath(AppUtil::PATH_USER_CONFIG) + "Certificates" PATH_SEPARATOR_STR "client.key");
	setDefault(TLS_CERTIFICATE_FILE, AppUtil::getPath(AppUtil::PATH_USER_CONFIG) + "Certificates" PATH_SEPARATOR_STR "client.crt");
	setDefault(AUTO_REFRESH_TIME, 60);
	setDefault(AUTO_SEARCH_LIMIT, 15);
	setDefault(AUTO_KICK_NO_FAVS, false);
	setDefault(ALLOW_UNTRUSTED_HUBS, true);
	setDefault(ALLOW_UNTRUSTED_CLIENTS, true);
	setDefault(NUMBER_OF_SEGMENTS, 3);
	setDefault(SEGMENTS_MANUAL, false);
	setDefault(EXTRA_SLOTS, 3);
	setDefault(EXTRA_PARTIAL_SLOTS, 1);
	setDefault(SHUTDOWN_TIMEOUT, 150);
	setDefault(SEARCH_PASSIVE, false);
	setDefault(AUTO_PRIORITY_DEFAULT, false);
	setDefault(REMOVE_FORBIDDEN, true);
	setDefault(EXTRA_DOWNLOAD_SLOTS, 3);


	setDefault(MAX_AUTO_MATCH_SOURCES, 5);
	setDefault(MULTI_CHUNK, true);
	setDefault(DOWNCONN_PER_SEC, 2);
	setDefault(REPORT_ALTERNATES, true);

	setDefault(BUNDLE_SEARCH_TIME, 15);
	setDefault(AUTO_SLOTS, 5);
	setDefault(MINIMUM_SEARCH_INTERVAL, 5);
	setDefault(AWAY, false);

	setDefault(DISCONNECT_SPEED, 5);
	setDefault(DISCONNECT_FILE_SPEED, 15);
	setDefault(DISCONNECT_TIME, 40);
	setDefault(DISCONNECT_FILESIZE, 50);
	setDefault(REMOVE_SPEED, 2);

	setDefault(IGNORE_USE_REGEXP_OR_WC, true);
	setDefault(FAV_DL_SPEED, 0);
	setDefault(IP_UPDATE, true);
	setDefault(IP_UPDATE6, false);
	setDefault(SKIPLIST_SHARE, "(.*\\.(scn|asd|lnk|url|log|crc|dat|sfk|mxm))$|(rushchk.log)");
	setDefault(FREE_SLOTS_EXTENSIONS, "*.nfo|*.sfv");
	setDefault(SKIPLIST_DOWNLOAD, ".*|*All-Files-CRC-OK*|Descript.ion|thumbs.db|*.bad|*.missing|rushchk.log");
	setDefault(HIGH_PRIO_FILES, "*.sfv|*.nfo|*sample*|*subs*|*.jpg|*cover*|*.pls|*.m3u");
	setDefault(AUTOSEARCH_EVERY, 5);
	setDefault(USE_HIGHLIGHT, false);
	setDefault(BLOOM_MODE, BLOOM_DISABLED);
	setDefault(SHARE_SKIPLIST_USE_REGEXP, true);
	setDefault(DOWNLOAD_SKIPLIST_USE_REGEXP, false);
	setDefault(HIGHEST_PRIORITY_USE_REGEXP, false);
	setDefault(OVERLAP_SLOW_SOURCES, true);
	setDefault(MIN_SEGMENT_SIZE, 1024);
	setDefault(DUPE_SEARCH, true);
	setDefault(DISALLOW_CONNECTION_TO_PASSED_HUBS, false);
	setDefault(AUTO_ADD_SOURCE, true);
	setDefault(INCOMING_REFRESH_TIME, 60);
	setDefault(USE_ADLS, true);
	setDefault(DONT_DL_ALREADY_QUEUED, false);
	setDefault(SYSTEM_SHOW_UPLOADS, false);
	setDefault(SYSTEM_SHOW_DOWNLOADS, false);
	setDefault(SETTINGS_PROFILE, PROFILE_NORMAL);
	setDefault(DOWNLOAD_SPEED, connectionSpeeds[0]);
	setDefault(WIZARD_PENDING, true); // run wizard on startup
	setDefault(FORMAT_RELEASE, true);
	setDefault(LOG_LINES, 500);

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
	setDefault(REPORT_BLOCKED_SHARE, true);


	setDefault(USE_PARTIAL_SHARING, true);
	setDefault(LOG_HASHING, false);
	setDefault(RECENT_BUNDLE_HOURS, 24);
	setDefault(QI_AUTOPRIO, true);
	setDefault(ALLOW_MATCH_FULL_LIST, true);
	setDefault(REPORT_ADDED_SOURCES, false);
	setDefault(COUNTRY_FORMAT, "%[2code]");
	setDefault(FORMAT_DIR_REMOTE_TIME, false);
	setDefault(DISCONNECT_MIN_SOURCES, 2);
	setDefault(USE_SLOW_DISCONNECTING_DEFAULT, true);
	setDefault(PRIO_LIST_HIGHEST, false);
	setDefault(AUTOPRIO_TYPE, PRIO_BALANCED);
	setDefault(AUTOPRIO_INTERVAL, 10);
	setDefault(AUTOSEARCH_EXPIRE_DAYS, 5);
	setDefault(TLS_MODE, 1);
	setDefault(UPDATE_METHOD, 2);
	setDefault(UPDATE_IP_HOURLY, false);
	setDefault(FULL_LIST_DL_LIMIT, 30000);

	setDefault(ENABLE_SUDP, false);
	setDefault(NMDC_MAGNET_WARN, true);
	setDefault(AUTO_COMPLETE_BUNDLES, false);
	setDefault(LOG_SCHEDULED_REFRESHES, true);
	setDefault(AUTO_DETECTION_USE_LIMITED, true);
	setDefault(AS_DELAY_HOURS, 12);
	setDefault(LAST_LIST_PROFILE, 0);
	setDefault(SHOW_CHAT_NOTIFY, false);
	setDefault(AWAY_IDLE_TIME, 5);
	setDefault(FREE_SPACE_WARN, true);

	setDefault(HISTORY_SEARCH_MAX, 10);
	setDefault(HISTORY_EXCLUDE_MAX, 10);
	setDefault(HISTORY_DIR_MAX, 10);

	setDefault(HISTORY_SEARCH_CLEAR, false);
	setDefault(HISTORY_EXCLUDE_CLEAR, false);
	setDefault(HISTORY_DIR_CLEAR, false);

	//set depending on the cpu count
	setDefault(MAX_HASHING_THREADS, std::thread::hardware_concurrency());

	setDefault(HASHERS_PER_VOLUME, 1);

	setDefault(MIN_DUPE_CHECK_SIZE, 512);
	setDefault(SKIP_EMPTY_DIRS_SHARE, true);

	setDefault(DB_CACHE_SIZE, 8);
	setDefault(CUR_REMOVED_TREES, 0);
	setDefault(CUR_REMOVED_FILES, 0);

	setDefault(DL_AUTO_DISCONNECT_MODE, QUEUE_FILE);
	setDefault(REFRESH_THREADING, MULTITHREAD_MANUAL);

	setDefault(REMOVE_EXPIRED_AS, false);

	setDefault(PM_LOG_GROUP_CID, true);
	setDefault(SHARE_FOLLOW_SYMLINKS, true);
	setDefault(AS_FAILED_DEFAULT_GROUP, "Failed Bundles");

	setDefault(USE_DEFAULT_CERT_PATHS, true);

	setDefault(MAX_RUNNING_BUNDLES, 0);
	setDefault(DEFAULT_SP, 0);
	setDefault(STARTUP_REFRESH, true);
	setDefault(FL_REPORT_FILE_DUPES, true);
	setDefault(DATE_FORMAT, "%Y-%m-%d %H:%M");

	setDefault(UPDATE_CHANNEL, VERSION_STABLE);
	setDefault(LOG_IGNORED, true);
	setDefault(REMOVE_FINISHED_BUNDLES, false);
	setDefault(ALWAYS_CCPM, false);

	setDefault(MAX_RECENT_HUBS, 30);
	setDefault(MAX_RECENT_PRIVATE_CHATS, 15);
	setDefault(MAX_RECENT_FILELISTS, 15);

	// not in GUI
	setDefault(USE_UPLOAD_BUNDLES, true);
	setDefault(CONFIG_BUILD_NUMBER, 2029);

	setDefault(PM_MESSAGE_CACHE, 20); // Just so that we won't lose messages while the tab is being created
	setDefault(HUB_MESSAGE_CACHE, 0);
	setDefault(LOG_MESSAGE_CACHE, 100);

	setDefault(POPUP_HUB_PMS, true);
	setDefault(POPUP_BOT_PMS, true);
	setDefault(SORT_FAVUSERS_FIRST, false);

#ifdef _WIN32
	setDefault(NMDC_ENCODING, Text::systemCharset);
#else
	setDefault(NMDC_ENCODING, "CP1252");
#endif

	// GUI SETTINGS
#ifdef HAVE_GUI
	setDefault(CONFIRM_EXIT, true);
	setDefault(MINIMIZE_TRAY, false);
	setDefault(CLEAR_SEARCH, true);
	setDefault(STATUS_IN_CHAT, true);
	setDefault(SHOW_IP_COUNTRY_CHAT, false);
	setDefault(PRIVATE_MESSAGE_BEEP, false);
	setDefault(SHOW_PROGRESS_BARS, true);
	setDefault(PRIVATE_MESSAGE_BEEP_OPEN, false);
	setDefault(USE_SYSTEM_ICONS, true);
	setDefault(MAX_TAB_ROWS, 4);
	setDefault(URL_HANDLER, true);
	setDefault(SHOW_TRANSFERVIEW, true);
	setDefault(SHOW_STATUSBAR, true);
	setDefault(SHOW_TOOLBAR, true);
	setDefault(POPUNDER_PM, false);
	setDefault(POPUNDER_FILELIST, false);
	setDefault(MAGNET_REGISTER, false);
	setDefault(MAGNET_ASK, true);
	setDefault(MAGNET_ACTION, MAGNET_DOWNLOAD);
	setDefault(CONFIRM_HUB_REMOVAL, true);
	setDefault(USE_CTRL_FOR_LINE_HISTORY, true);
	setDefault(CONFIRM_QUEUE_REMOVAL, true);
	setDefault(TOGGLE_ACTIVE_WINDOW, true);

	setDefault(OPEN_PUBLIC, false);
	setDefault(OPEN_FAVORITE_HUBS, false);
	setDefault(OPEN_FAVORITE_USERS, false);
	setDefault(OPEN_AUTOSEARCH, false);
	//setDefault(OPEN_RECENT_HUBS, false);
	setDefault(OPEN_QUEUE, false);
	setDefault(OPEN_FINISHED_UPLOADS, false);
	setDefault(OPEN_SEARCH_SPY, false);
	setDefault(OPEN_NOTEPAD, false);

	setDefault(OPEN_WAITING_USERS, false);
	setDefault(BOLD_FINISHED_DOWNLOADS, true);
	setDefault(BOLD_FINISHED_UPLOADS, true);
	setDefault(BOLD_QUEUE, true);
	setDefault(BOLD_HUB, true);
	setDefault(BOLD_PM, true);
	setDefault(BOLD_SEARCH, true);
	setDefault(BOLD_WAITING_USERS, true);
	setDefault(PROMPT_PASSWORD, true);
	setDefault(SPY_FRAME_IGNORE_TTH_SEARCHES, false);
	setDefault(TEXT_FONT, "Tahoma,-11,400,0");
	setDefault(TOOLBAR_ORDER, SettingsManager::buildToolbarOrder(SettingsManager::getDefaultToolbarOrder()));
	setDefault(MEDIATOOLBAR, "0,-1,1,-1,2,3,4,5,6,7,8,9,-1");

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
	setDefault(PROGRESS_SEGMENT_COLOR, RGB(49, 106, 197));
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

	setDefault(COLOR_STATUS_FINISHED, RGB(145, 183, 4));
	setDefault(COLOR_STATUS_SHARED, RGB(102, 158, 18));

	setDefault(SOUNDS_DISABLED, false);
	setDefault(UPLOADQUEUEFRAME_SHOW_TREE, true);

	// default sounds
	setDefault(BEGINFILE, Util::emptyString);
	setDefault(BEEPFILE, Util::emptyString);
	setDefault(FINISHFILE, Util::emptyString);
	setDefault(SOURCEFILE, Util::emptyString);
	setDefault(UPLOADFILE, Util::emptyString);
	setDefault(CHATNAMEFILE, Util::emptyString);
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

	setDefault(SHUTDOWN_ACTION, 0);
	setDefault(PROGRESSBAR_ODC_STYLE, true);

	setDefault(PROGRESS_3DDEPTH, 4);
	setDefault(PROGRESS_OVERRIDE_COLORS, true);
	setDefault(USERLIST_DBLCLICK, 0);
	setDefault(TRANSFERLIST_DBLCLICK, 0);
	setDefault(CHAT_DBLCLICK, 0);	
	setDefault(HUBFRAME_VISIBLE, "1,1,0,1,0,1,0,0,0,0,0,0");
	setDefault(DIRECTORYLISTINGFRAME_VISIBLE, "1,1,0,1,1");	
	setDefault(FINISHED_VISIBLE, "1,1,1,1,1,1,1,1");
	setDefault(FINISHED_UL_VISIBLE, "1,1,1,1,1,1,1");
	setDefault(QUEUEFRAME_VISIBLE, "1,1,1,1,1,1,1,0,1,1,1");
	setDefault(EMOTICONS_FILE, "Atlantis");
	setDefault(TABS_ON_TOP, false);
	setDefault(UC_SUBMENU, true);

	setDefault(SHOW_WINAMP_CONTROL, false);
	setDefault(MEDIA_PLAYER, 0);
	setDefault(WMP_FORMAT, "/me playing: %[title] at %[bitrate] <Windows Media Player %[version]>");
	setDefault(ITUNES_FORMAT, "/me playing: %[title] at %[bitrate] <iTunes %[version]>");
	setDefault(MPLAYERC_FORMAT, "/me playing: %[title] <Media Player Classic>");
	setDefault(WINAMP_PATH, "C:\\Program Files\\Winamp\\winamp.exe");

	setDefault(SERVER_COMMANDS, true);
	setDefault(CLIENT_COMMANDS, true);
	setDefault(POPUP_FONT, "MS Shell Dlg,-11,400,0");
	setDefault(POPUP_TITLE_FONT, "MS Shell Dlg,-11,400,0");
	setDefault(POPUPFILE, AppUtil::getPath(AppUtil::PATH_RESOURCES) + "popup.bmp");
	setDefault(PM_PREVIEW, true);
	setDefault(POPUP_TIME, 5);
	setDefault(MAX_MSG_LENGTH, 120);
	setDefault(FLASH_WINDOW_ON_PM, false);
	setDefault(FLASH_WINDOW_ON_NEW_PM, false);
	setDefault(FLASH_WINDOW_ON_MYNICK, false);
	setDefault(TB_IMAGE_SIZE, 24);
	setDefault(TB_IMAGE_SIZE_HOT, 24);
	setDefault(SHOW_QUEUE_BARS, true);
	setDefault(EXPAND_DEFAULT, false);

	setDefault(OPEN_LOGS_INTERNAL, true);
	setDefault(OPEN_SYSTEM_LOG, true);
	setDefault(USE_OLD_SHARING_UI, false);
	setDefault(LAST_SEARCH_FILETYPE, "0");
	setDefault(LAST_AS_FILETYPE, "7");
	setDefault(MAX_RESIZE_LINES, 4);
	setDefault(PASSWD_PROTECT, false);
	setDefault(PASSWD_PROTECT_TRAY, false);
	setDefault(BOLD_HUB_TABS_ON_KICK, false);
	setDefault(SEARCH_USE_EXCLUDED, false);
	setDefault(USE_EXPLORER_THEME, true);
	setDefault(TESTWRITE, true);

	setDefault(SORT_DIRS, false);
	setDefault(HIGHLIGHT_LIST, "");

	setDefault(POPUP_BUNDLE_DLS, true);
	setDefault(POPUP_BUNDLE_ULS, false);
	setDefault(ICON_PATH, "");
	setDefault(SHOW_SHARED_DIRS_DL, true);
	setDefault(EXPAND_BUNDLES, false);

	setDefault(WTB_IMAGE_SIZE, 16);
	setDefault(SHOW_TBSTATUS, true);
	setDefault(TB_PROGRESS_FONT, "Arial,-11,400,0");
	setDefault(LOCK_TB, false);
	setDefault(POPUNDER_PARTIAL_LIST, false);
	setDefault(LAST_SEARCH_DISABLED_HUBS, Util::emptyString);
	setDefault(QUEUE_SPLITTER_POS, 750);
	setDefault(POPUNDER_TEXT, false);
	setDefault(SEARCH_SAVE_HUBS_STATE, false);
	setDefault(CONFIRM_HUB_CLOSING, true);
	setDefault(CONFIRM_AS_REMOVAL, true);

	setDefault(FAV_USERS_SPLITTER_POS, 7500);
	setDefault(FAV_USERS_SHOW_INFO, true);
	setDefault(USERS_FILTER_FAVORITE, false);
	setDefault(USERS_FILTER_QUEUE, false);
	setDefault(USERS_FILTER_ONLINE, false);

	setDefault(AUTOSEARCH_BOLD, true);
	setDefault(LIST_VIEW_FONT, "");
	setDefault(SHOW_EMOTICON, true);
	setDefault(SHOW_MULTILINE, true);
	setDefault(SHOW_MAGNET, true);
	setDefault(SHOW_SEND_MESSAGE, true);

	setDefault(WARN_ELEVATED, true);
	setDefault(LAST_FL_FILETYPE, "0");
	setDefault(CONFIRM_FILE_DELETIONS, true);
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

	setDefault(FILTER_QUEUE_INVERSED, false);
	setDefault(FILTER_QUEUE_TOP, true);
	setDefault(FILTER_QUEUE_RESET_CHANGE, true);

	setDefault(CLOSE_USE_MINIMIZE, false);
	setDefault(USERS_FILTER_IGNORE, false);
	setDefault(NFO_EXTERNAL, false);
	setDefault(SINGLE_CLICK_TRAY, false);
	setDefault(QUEUE_SHOW_FINISHED, true);
	setDefault(PROGRESS_LIGHTEN, 25);
	setDefault(AUTOSEARCHFRAME_VISIBLE, "1,1,1,1,1,1,1,1,1,1,1");
	setDefault(SAVE_LAST_STATE, true);
#endif
}

void SettingsManager::applyProfileDefaults() noexcept {
	for (const auto& newSetting: profileSettings[get(SETTINGS_PROFILE)]) {
		newSetting.setProfileToDefault(false);
	}
}

void SettingsManager::setProfile(int aProfile, const ProfileSettingItem::List& conflicts) noexcept {
	set(SettingsManager::SETTINGS_PROFILE, aProfile);
	applyProfileDefaults();

	for (const auto& setting: conflicts) {
		setting.setProfileToDefault(true);
	}
}

string SettingsManager::getProfileName(int profile) const noexcept {
	switch(profile) {
		case PROFILE_NORMAL: return STRING(NORMAL);
		case PROFILE_RAR: return STRING(RAR_HUBS);
		case PROFILE_LAN: return STRING(LAN_HUBS);
		default: return STRING(NORMAL);
	}
}

void SettingsManager::load(StartupLoader& aLoader) noexcept {
	auto fileLoaded = loadSettingFile(CONFIG_DIR, CONFIG_NAME, [this](SimpleXML& xml) {
		if (xml.findChild("DCPlusPlus")) {
			xml.stepIn();

			if (xml.findChild("Settings")) {
				xml.stepIn();

				int i;

				for (i = STR_FIRST; i < STR_LAST; i++) {
					const string& attr = settingTags[i];
					dcassert(attr.find("SENTRY") == string::npos);

					if (xml.findChild(attr))
						set(StrSetting(i), xml.getChildData(), true);
					xml.resetCurrentChild();
				}

				for (i = INT_FIRST; i < INT_LAST; i++) {
					const string& attr = settingTags[i];
					dcassert(attr.find("SENTRY") == string::npos);

					if (xml.findChild(attr))
						set(IntSetting(i), Util::toInt(xml.getChildData()), true);
					xml.resetCurrentChild();
				}

				for (i = BOOL_FIRST; i < BOOL_LAST; i++) {
					const string& attr = settingTags[i];
					dcassert(attr.find("SENTRY") == string::npos);

					if (xml.findChild(attr)) {
						auto val = Util::toInt(xml.getChildData());
						dcassert(val == 0 || val == 1);
						set(BoolSetting(i), val ? true : false, true);
					}
					xml.resetCurrentChild();
				}

				for (i = INT64_FIRST; i < INT64_LAST; i++) {
					const string& attr = settingTags[i];
					dcassert(attr.find("SENTRY") == string::npos);

					if (xml.findChild(attr))
						set(Int64Setting(i), Util::toInt64(xml.getChildData()), true);
					xml.resetCurrentChild();
				}

				xml.stepOut();
			}

			xml.resetCurrentChild();


			//load history lists
			for (int i = 0; i < HISTORY_LAST; ++i) {
				if (xml.findChild(historyTags[i])) {
					xml.stepIn();
					while (xml.findChild("HistoryItem")) {
						addToHistory(xml.getChildData(), static_cast<HistoryType>(i));
					}
					xml.stepOut();
				}
				xml.resetCurrentChild();
			}

			fire(SettingsManagerListener::Load(), xml);

			xml.stepOut();
		}
	});

	setDefault(UDP_PORT, SETTING(TCP_PORT));

	File::ensureDirectory(SETTING(TLS_TRUSTED_CERTIFICATES_PATH));

	if(SETTING(PRIVATE_ID).length() != 39 || !CID(SETTING(PRIVATE_ID))) {
		set(SettingsManager::PRIVATE_ID, CID::generate().toBase32());
	}

	//check the bind address
	auto checkBind = [&] (SettingsManager::StrSetting aSetting, bool v6) {
		if (!isDefault(aSetting)) {
			auto adapters = NetworkUtil::getNetworkAdapters(v6);
			auto p = ranges::find_if(adapters, [this, aSetting](const AdapterInfo& aInfo) { return aInfo.ip == get(aSetting); });
			if (p == adapters.end() && aLoader.messageF(STRING_F(BIND_ADDRESS_MISSING, (v6 ? "IPv6" : "IPv4") % get(aSetting)), true, false)) {
				unsetKey(aSetting);
			}
		}
	};

	checkBind(BIND_ADDRESS, false);
	checkBind(BIND_ADDRESS6, true);

	applyProfileDefaults();

	fire(SettingsManagerListener::LoadCompleted(), fileLoaded);
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

bool SettingsManager::addToHistory(const string& aString, HistoryType aType) noexcept {
	if(aString.empty() || get(maxLimits[aType]) == 0)
		return false;

	WLock l(cs);
	StringList& hist = history[aType];

	// Remove existing matching item
	auto s = ranges::find(hist, aString);
	if(s != hist.end()) {
		hist.erase(s);
	}

	// Count exceed?
	if(static_cast<int>(hist.size()) == get(maxLimits[aType])) {
		hist.erase(hist.begin());
	}

	hist.push_back(aString);
	return true;
}

void SettingsManager::clearHistory(HistoryType aType) noexcept {
	WLock l(cs);
	history[aType].clear();
}

SettingsManager::HistoryList SettingsManager::getHistory(HistoryType aType) const noexcept {
	RLock l(cs);
	return history[aType];
}



void SettingsManager::set(StrSetting key, string const& value, bool aForceSet) noexcept {
	if ((key == NICK) && (value.size() > 35)) {
		strSettings[key - STR_FIRST] = value.substr(0, 35);
	} else if ((key == DESCRIPTION) && (value.size() > 50)) {
		strSettings[key - STR_FIRST] = value.substr(0, 50);
	} else if ((key == EMAIL) && (value.size() > 64)) {
		strSettings[key - STR_FIRST] = value.substr(0, 64);
	} else if (key == UPLOAD_SPEED || key == DOWNLOAD_SPEED) {
		if (!regex_match(value, connectionRegex)) {
			strSettings[key - STR_FIRST] = connectionSpeeds[0];
		} else {
			strSettings[key - STR_FIRST] = value;
		}
	} else {
		strSettings[key - STR_FIRST] = value;
	}

	if (value.empty()) {
		isSet[key] = false;
	} else if (!isSet[key]) {
		isSet[key] = aForceSet || value != getDefault(key);
	}
}

void SettingsManager::set(IntSetting key, int value, bool aForceSet) noexcept {
	if (key == UPLOAD_SLOTS && value <= 0) {
		value = 1;
	} else if (key == EXTRA_SLOTS && value < 1) {
		value = 1;
	} else if (key == AUTOSEARCH_EVERY && value < 1) {
		value = 1;
	} else if (key == SET_MINISLOT_SIZE && value < 64) {
		value = 64;
	} else if (key == NUMBER_OF_SEGMENTS && value > 10) {
		value = 10;
	} else if (key == BUNDLE_SEARCH_TIME && value < 5) {
		value = 5;
	} else if (key == MINIMUM_SEARCH_INTERVAL && value < 5) {
		value = 5;
#ifdef HAVE_GUI
	} else if (key == MAX_RESIZE_LINES && value < 1) {
		value = 1;
#endif
	} else if (key == DISCONNECT_SPEED && value < 1) {
		value = 1;
	}

	intSettings[key - INT_FIRST] = value;
	updateValueSet(key, value, aForceSet);
}

void SettingsManager::set(BoolSetting key, bool value, bool aForceSet) noexcept {
	boolSettings[key - BOOL_FIRST] = value;
	updateValueSet(key, value, aForceSet);
}

void SettingsManager::set(Int64Setting key, int64_t value, bool aForceSet) noexcept {
	int64Settings[key - INT64_FIRST] = value;
	updateValueSet(key, value, aForceSet);
}

void SettingsManager::set(IntSetting key, const string& value) noexcept {
	if (value.empty()) {
		intSettings[key - INT_FIRST] = 0;
		isSet[key] = false;
	} else {
		set(key, Util::toInt(value));
	}
}

void SettingsManager::set(BoolSetting key, const string& value) noexcept {
	if (value.empty()) {
		boolSettings[key - BOOL_FIRST] = 0;
		isSet[key] = false;
	} else {
		set(key, Util::toInt(value) > 0 ? true : false);
	}
}

void SettingsManager::set(Int64Setting key, const string& value) noexcept {
	if (value.empty()) {
		int64Settings[key - INT64_FIRST] = 0;
		isSet[key] = false;
	} else {
		set(key, Util::toInt64(value));
	}
}

void SettingsManager::save() noexcept {

	SimpleXML xml;
	xml.addTag("DCPlusPlus");
	xml.stepIn();
	xml.addTag("Settings");
	xml.stepIn();

	int i;
	string type("type"), curType("string");
	
	for(i=STR_FIRST; i<STR_LAST; i++)
	{
		if (i == CONFIG_VERSION) {
			xml.addTag(settingTags[i], VERSIONSTRING);
			xml.addChildAttrib(type, curType);
		} else if (i == CONFIG_APP) {
			xml.addTag(settingTags[i], APPID);
			xml.addChildAttrib(type, curType);
		} else if(isSet[i]) {
			xml.addTag(settingTags[i], get(StrSetting(i), false));
			xml.addChildAttrib(type, curType);
		}
	}

	curType = "int";
	for(i=INT_FIRST; i<INT_LAST; i++)
	{
	
		if (i == CONFIG_BUILD_NUMBER) {
			xml.addTag(settingTags[i], BUILD_NUMBER);
			xml.addChildAttrib(type, curType);
		} else if (isSet[i]) {
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

	for(i = 0; i < HISTORY_LAST; ++i) {
		const auto& hist = history[i];
		if (!hist.empty() && !get(clearSettings[i])) {
			xml.addTag(historyTags[i]);
			xml.stepIn();
			for (auto& hi: hist) {
				xml.addTag("HistoryItem", hi);
			}
			xml.stepOut();
		}
	}

	fire(SettingsManagerListener::Save(), xml);
	saveSettingFile(xml, CONFIG_DIR, CONFIG_NAME);
}

HubSettings SettingsManager::getHubSettings() const noexcept {
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
	ret.get(HubSettings::NmdcEncoding) = get(NMDC_ENCODING);
	ret.get(HubSettings::ShareProfile) = get(DEFAULT_SP);
	return ret;
}

void settingXmlMessage(const string& aMessage, LogMessage::Severity aSeverity, const MessageCallback& aCustomErrorF) noexcept {
	if (!aCustomErrorF) {
		LogManager::getInstance()->message(aMessage, aSeverity, STRING(SETTINGS));
	} else {
		aCustomErrorF(aMessage);
	}
}

string SettingsManager::buildToolbarOrder(const vector<ToolbarIconEnum>& aIcons) noexcept {
	string ret;
	for (const auto& i: aIcons) {
		if (!ret.empty()) {
			ret += ',';
		}

		ret += Util::toString(static_cast<int>(i));
	}

	return ret;
}

vector<ToolbarIconEnum> SettingsManager::getDefaultToolbarOrder() noexcept {
	return vector<ToolbarIconEnum>({
		ToolbarIconEnum::PUBLIC_HUBS,
		ToolbarIconEnum::DIVIDER,

		ToolbarIconEnum::RECONNECT,
		ToolbarIconEnum::FOLLOW_REDIRECT,
		ToolbarIconEnum::DIVIDER,

		ToolbarIconEnum::FAVORITE_HUBS,
		ToolbarIconEnum::USERS,
		ToolbarIconEnum::RECENT_HUBS,
		ToolbarIconEnum::DIVIDER,

		ToolbarIconEnum::QUEUE,
		ToolbarIconEnum::UPLOAD_QUEUE,
		ToolbarIconEnum::FINISHED_UPLOADS,
		ToolbarIconEnum::DIVIDER,

		ToolbarIconEnum::SEARCH,
		ToolbarIconEnum::ADL_SEARCH,
		ToolbarIconEnum::AUTO_SEARCH,
		ToolbarIconEnum::DIVIDER,

		ToolbarIconEnum::NOTEPAD,
		ToolbarIconEnum::SYSTEM_LOG,
		ToolbarIconEnum::DIVIDER,

		ToolbarIconEnum::REFRESH_FILELIST,
		ToolbarIconEnum::EXTENSIONS,
		ToolbarIconEnum::DIVIDER,

		ToolbarIconEnum::OPEN_FILELIST,
		ToolbarIconEnum::OPEN_DOWNLOADS,
		ToolbarIconEnum::DIVIDER,

		ToolbarIconEnum::SETTINGS
	});
}

bool SettingsManager::loadSettingFile(AppUtil::Paths aPath, const string& aFileName, XMLParseCallback&& aParseCallback, const MessageCallback& aCustomReportF) noexcept {
	const auto parseXmlFile = [&](const string& aPath) {
		SimpleXML xml;
		try {
			// Some legacy config files (such as favorites and recent hubs) may contain invalid UTF-8 data
			// so don't throw in case of validation errors
			xml.fromXML(File(aPath, File::READ, File::OPEN).read(), SimpleXMLReader::FLAG_REPLACE_INVALID_UTF8);

			aParseCallback(xml);
		} catch (const Exception& e) {
			settingXmlMessage(STRING_F(LOAD_FAILED_X, aPath % e.getError()), LogMessage::SEV_ERROR, aCustomReportF);
			return false;
		}

		return true;
	};

	return loadSettingFile(aPath, aFileName, parseXmlFile, aCustomReportF);
}

bool SettingsManager::loadSettingFile(AppUtil::Paths aPath, const string& aFileName, PathParseCallback&& aParseCallback, const MessageCallback& aCustomReportF) noexcept {
	const auto fullPath = AppUtil::getPath(aPath) + aFileName;

	AppUtil::migrate(fullPath);

	if (!PathUtil::fileExists(fullPath)) {
		return false;
	}

	const auto backupPath = fullPath + ".bak";
	if (!aParseCallback(fullPath)) {
		// Try to load the file that was previously loaded succesfully
		if (!PathUtil::fileExists(backupPath) || !aParseCallback(backupPath)) {
			return false;
		}

		auto corruptedCopyPath = fullPath + Util::formatTime(".CORRUPTED_%Y-%m-%d_%H-%M-%S", time(NULL));

		// Replace the main setting file with the backup
		try {
			File::renameFile(fullPath, corruptedCopyPath);
			File::copyFile(backupPath, fullPath);
		} catch (const Exception& e) {
			settingXmlMessage(STRING_F(UNABLE_TO_RENAME, fullPath % e.getError()), LogMessage::SEV_ERROR, aCustomReportF);
			return false;
		}

		settingXmlMessage(STRING_F(SETTING_FILE_RECOVERED, backupPath % Util::formatTime("%Y-%m-%d %H:%M", File::getLastModified(backupPath)) % corruptedCopyPath), LogMessage::SEV_INFO, aCustomReportF);
	} else {
		// Succeeded, save the backup
		File::deleteFile(backupPath);
		try {
			File::copyFile(fullPath, backupPath);
		} catch (const Exception& e) {
			settingXmlMessage(STRING_F(SAVE_FAILED_X, backupPath % e.getError()), LogMessage::SEV_ERROR, aCustomReportF);
		}
	}

	return true;
}

bool SettingsManager::saveSettingFile(SimpleXML& aXML, AppUtil::Paths aPath, const string& aFileName, const MessageCallback& aCustomErrorF) noexcept {
	return saveSettingFile(SimpleXML::utf8Header + aXML.toXML(), aPath, aFileName, aCustomErrorF);
}

bool SettingsManager::saveSettingFile(const string& aContent, AppUtil::Paths aPath, const string& aFileName, const MessageCallback& aCustomErrorF) noexcept {
	auto fname = AppUtil::getPath(aPath) + aFileName;
	try {
		{
			File f(fname + ".tmp", File::WRITE, File::CREATE | File::TRUNCATE, File::BUFFER_WRITE_THROUGH);
			f.write(aContent);
		}

		// Dont overwrite with empty file.
		if (File::getSize(fname + ".tmp") > 0) {
			File::deleteFile(fname);
			File::renameFile(fname + ".tmp", fname);
		}
	} catch (const FileException& e) {
		settingXmlMessage(STRING_F(SAVE_FAILED_X, fname % e.getError()), LogMessage::SEV_ERROR, aCustomErrorF);
		return false;
	}

	return true;
}

} // namespace dcpp
