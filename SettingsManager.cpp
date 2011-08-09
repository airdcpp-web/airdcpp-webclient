/*
 * Copyright (C) 2001-2011 Jacek Sieka, arnetheduck on gmail point com
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

#include "ResourceManager.h"
#include "SimpleXML.h"
#include "Util.h"
#include "File.h"
#include "version.h"
#include "AdcHub.h"
#include "CID.h"
#include "SearchManager.h"
#include "StringTokenizer.h"

namespace dcpp {

StringList SettingsManager::connectionSpeeds;
StringList SettingsManager::Languages;

const string SettingsManager::settingTags[] =
{
	// Strings
	"Nick", "UploadSpeed", "Description", "DownloadDirectory", "EMail", "ExternalIp",
	"Font", "MainFrameOrder", "MainFrameWidths", "HubFrameOrder", "HubFrameWidths", 
	"LanguageFile", "SearchFrameOrder", "SearchFrameWidths", "FavoritesFrameOrder", "FavoritesFrameWidths", 
	"HublistServers", "QueueFrameOrder", "QueueFrameWidths", "PublicHubsFrameOrder", "PublicHubsFrameWidths", 
	"UsersFrameOrder", "UsersFrameWidths", "HttpProxy", "LogDirectory", "LogFormatPostDownload", 
	"LogFormatPostUpload", "LogFormatMainChat", "LogFormatPrivateChat", "FinishedOrder", "FinishedWidths",	 
	"TempDownloadDirectory", "BindAddress", "SocksServer", "SocksUser", "SocksPassword", "ConfigVersion", 
	"DefaultAwayMessage", "TimeStampsFormat", "ADLSearchFrameOrder", "ADLSearchFrameWidths", 
	"FinishedULWidths", "FinishedULOrder", "CID", "SpyFrameWidths", "SpyFrameOrder", 
	"BeepFile", "BeginFile", "FinishedFile", "SourceFile", "UploadFile", "FakerFile", "ChatNameFile", "WinampFormat",
	"KickMsgRecent01", "KickMsgRecent02", "KickMsgRecent03", "KickMsgRecent04", "KickMsgRecent05", 
	"KickMsgRecent06", "KickMsgRecent07", "KickMsgRecent08", "KickMsgRecent09", "KickMsgRecent10", 
	"KickMsgRecent11", "KickMsgRecent12", "KickMsgRecent13", "KickMsgRecent14", "KickMsgRecent15", 
	"KickMsgRecent16", "KickMsgRecent17", "KickMsgRecent18", "KickMsgRecent19", "KickMsgRecent20",
	"Toolbar", "ToolbarImage", "ToolbarHot", "UserListImage", "UploadQueueFrameOrder", "UploadQueueFrameWidths",
	"SoundTTH", "SoundException", "SoundHubConnected", "SoundHubDisconnected", "SoundFavUserOnline", "SoundTypingNotify",
	"LogFileMainChat", 
	"LogFilePrivateChat", "LogFileStatus", "LogFileUpload", "LogFileDownload", "LogFileSystem", "LogFormatSystem", 
	"LogFormatStatus", "DirectoryListingFrameOrder", "DirectoryListingFrameWidths", 
	"MainFrameVisible", "SearchFrameVisible", "QueueFrameVisible", "HubFrameVisible", "UploadQueueFrameVisible", 
	"EmoticonsFile", "TLSPrivateKeyFile", "TLSCertificateFile", "TLSTrustedCertificatesPath",
	"FinishedVisible", "FinishedULVisible", "DirectoryListingFrameVisible",
	"RecentFrameOrder", "RecentFrameWidths", "Mapper",

	"BackgroundImage", "MPLAYERCformat", "ITUNESformat", "WMPformat", "Spotifyformat","WinampPath",
	"AntivirPath",
	"SkiplistShare", "FreeSlotsExtensions",
	"PopupFont", "PopupTitleFont", "PopupFile", "SkiplistDownload", "HighPrioFiles",
	"MediaToolbar", "password", "skiplistSearch", "skipMsg1", "skipMsg2", "skipMsg3", "DownloadSpeed",

	"SENTRY", 
	// Ints
	"IncomingConnections", "InPort", "Slots", "AutoFollow", "ClearSearch",
	"BackgroundColor", "TextColor", "ShareHidden", "FilterMessages", "MinimizeToTray",
	"AutoSearch", "TimeStamps", "ConfirmExit", "PopupHubPms", "PopupBotPms", "IgnoreHubPms", "IgnoreBotPms",
	"BufferSize", "DownloadSlots", "MaxDownloadSpeed", "LogMainChat", "LogPrivateChat",
	"LogDownloads", "LogUploads", "StatusInChat", "ShowJoins", "PrivateMessageBeep", "PrivateMessageBeepOpen",
	"UseSystemIcons", "PopupPMs", "MinUploadSpeed", "GetUserInfo", "UrlHandler", "MainWindowState", 
	"MainWindowSizeX", "MainWindowSizeY", "MainWindowPosX", "MainWindowPosY", "AutoAway",
	"SocksPort", "SocksResolve", "KeepLists", "AutoKick", "QueueFrameShowTree", 
	"CompressTransfers", "ShowProgressBars", "MaxTabRows",
	"MaxCompression", "AntiFragMethod", "MDIMaxmimized", "NoAwayMsgToBots",
	"SkipZeroByte", "AdlsBreakOnFirst",
	"HubUserCommands", "AutoSearchAutoMatch", "DownloadBarColor", "UploadBarColor", "LogSystem",
	"LogFilelistTransfers", "ShowStatusbar", "BandwidthSettingMode", "ShowToolbar", "ShowTransferview", 
	"SearchPassiveAlways", "SetMinislotSize", "ShutdownInterval", "DontAnnounceNewVersions", 
	"ExtraSlots", "ExtraPartialSlots",
	"TextGeneralBackColor", "TextGeneralForeColor", "TextGeneralBold", "TextGeneralItalic", 
	"TextMyOwnBackColor", "TextMyOwnForeColor", "TextMyOwnBold", "TextMyOwnItalic", 
	"TextPrivateBackColor", "TextPrivateForeColor", "TextPrivateBold", "TextPrivateItalic", 
	"TextSystemBackColor", "TextSystemForeColor", "TextSystemBold", "TextSystemItalic", 
	"TextServerBackColor", "TextServerForeColor", "TextServerBold", "TextServerItalic", 
	"TextTimestampBackColor", "TextTimestampForeColor", "TextTimestampBold", "TextTimestampItalic", 
	"TextMyNickBackColor", "TextMyNickForeColor", "TextMyNickBold", "TextMyNickItalic", 
	"TextFavBackColor", "TextFavForeColor", "TextFavBold", "TextFavItalic", 
	"TextOPBackColor", "TextOPForeColor", "TextOPBold", "TextOPItalic", 
	"TextURLBackColor", "TextURLForeColor", "TextURLBold", "TextURLItalic", 
	"HubSlots", 
	"RemoveForbidden", "ProgressTextDown", "ProgressTextUp", "ShowInfoTips", "ExtraDownloadSlots",
	"MinimizeOnStratup", "ConfirmDelete", "DefaultSearchFreeSlots", "SendUnknownCommands",
	"ErrorColor", "ExpandQueue", "TransferSplitSize",
	"DisconnectSpeed", "DisconnectFileSpeed", "DisconnectTime", "RemoveSpeed",
	"ProgressOverrideColors", "Progress3DDepth", "ProgressOverrideColors2",
	"MenubarTwoColors", "MenubarLeftColor", "MenubarRightColor", "MenubarBumped", 
	"DisconnectFileSize", "UploadQueueFrameShowTree",
	"SegmentsManual", "NumberOfSegments",
	"AutoUpdateIP", "MaxHashSpeed", "GetUserCountry", "DisableCZDiacritic",
	"UseAutoPriorityByDefault", "UseOldSharingUI",
	"FavShowJoins", "LogStatusMessages", "PMLogLines", "SearchAlternateColour", "SoundsDisabled",
	"ReportFoundAlternates",
	"SearchTime", "DontBeginSegment", "DontBeginSegmentSpeed", "PopunderPm", "PopunderFilelist",
	"DropMultiSourceOnly", "MagnetAsk", "MagnetAction", "MagnetRegister",
	"AddFinishedInstantly", "Away", "UseCTRLForLineHistory",
	"PopupHubConnected", "PopupHubDisconnected", "PopupFavoriteConnected", "PopupDownloadStart", 
	"PopupDownloadFailed", "PopupDownloadFinished", "PopupUploadFinished", "PopupPm", "PopupNewPM", 
	"PopupType", "ShutdownAction", "MinimumSearchInterval",
	"PopupAway", "PopupMinimized", "MaxAutoMatchSource",
    "ReservedSlotColor", "IgnoredColor", "FavoriteColor","NormalColour",
	"PasiveColor", "OpColor", "DontDLAlreadyShared",
	"ConfirmHubRemoval", "SuppressMainChat", "ProgressBackColor", "ProgressCompressColor", "ProgressSegmentColor",
	"UseVerticalView", "OpenNewWindow", "UDPPort", "MultiChunk",
 	"UserListDoubleClick", "TransferListDoubleClick", "ChatDoubleClick", "AdcDebug",
	"ToggleActiveWindow", "ProgressbaroDCStyle", "SearchHistory", 
	"OpenPublic", "OpenFavoriteHubs", "OpenFavoriteUsers", "OpenQueue", "OpenFinishedDownloads",
	"OpenFinishedUploads", "OpenSearchSpy", "OpenNetworkStatistics", "OpenNotepad", "OutgoingConnections",
	"NoIPOverride", "GroupSearchResults", "BoldFinishedDownloads", "BoldFinishedUploads", "BoldQueue", 
	"BoldHub", "BoldPm", "BoldSearch", "TabsOnTop", "SocketInBuffer", "SocketOutBuffer", 
	"ColorDownloaded", "ColorRunning", "ColorDone", "AutoRefreshTime", "UseTLS", "OpenWaitingUsers",
	"BoldWaitingUsers", "AutoSearchLimit", "AutoKickNoFavs", "PromptPassword", "SpyFrameIgnoreTthSearches",
 	"AllowUntrustedHubs", "AllowUntrustedClients", "TLSPort", "FastHash", "DownConnPerSec",
	"HighestPrioSize", "HighPrioSize", "NormalPrioSize", "LowPrioSize", "LowestPrio",
	"FilterEnter", "SortFavUsersFirst", "ShowShellMenu", 
	//AirDC
	"tabactivebg", "TabActiveText", "TabActiveBorder", "TabInactiveBg", "TabInactiveBgDisconnected", 
	"TabInactiveText", "TabInactiveBorder", "TabInactiveBgNotify", "TabDirtyBlend", "BlendTabs",
	"TabShowIcons", "TabSize", "HubBoldTabs", "showWinampControl", "MediaPlayer", "OpenWinampWindow",
	"IgnoreUseRegexpOrWc", "NatSort",
	"FavDownloadSpeed", "OpenFirstXHubs", "IPUpdate", "serverCommands", "ClientCommands",
	"PreviewPm", "PopupTime", "MaxMsgLength", "PopupBackColor", "PopupTextColor", "PopupTitleTextColor",
	"FlashWindowOnPm", "FlashWindowOnNewPm", "FlashWindowOnMyNick",
	"AutoSearchEvery", "AutoSearchEnabledTime", "AutoSearchEnabled", "AutoSearchRecheckTime",
	"TbImageSize", "TbImageSizeHot", "UseHighlight", "DupeColor", "ShowQueueBars", "SendBloom", 
	"LangSwitch", "ExpandDefault",
	"ShareSkiplistUseRegexp", "DownloadSkiplistUseRegexp", "HighestPriorityUseRegexp",
	"OverlapChunks", "MinSegmentSize", "OpenLogsInternal", "UcSubMenu", "AutoSlots", "Coral", "DupeText", "OpenSystemLog",
	"FirstRun", "LastSearchFiletype", "MaxResizeLines", "DontShareEmptyDirs", "OnlyShareFullDirs",
	"DupeSearch", "passwd_protect", "passwd_protect_tray",
	"DisAllowConnectionToPassedHubs", "BoldHubTabsOnKick", "searchSkiplist", "RefreshVnameOnSharePage",
	"AutoAddSource", "KeepFinishedFiles", "AllowNATTraversal", "UseExplorerTheme", "TestWrite", "IncomingRefreshTime", "UseAdls", "UseAdlsOwnList",
	"DontDlAlreadyQueued", "AutoDetectIncomingConnection", "DownloadsExpand", "TextNormBackColor", "TextNormForeColor", "TextNormBold", "TextNormItalic",
	"SystemShowUploads", "SystemShowDownloads", "SettingsProfile", "LanguageSwitch", "WizardRunNew", "FormatRelease", "ShareSFV", "LogLines",
	"CheckMissing", "CheckSfv", "CheckNfo", "CheckMp3Dir", "CheckExtraSfvNfo", "CheckExtraFiles", "CheckDupes", "SortDirs", "DecreaseRam", "MaxFileSizeShared",
	"CheckEmptyDirs","CheckEmptyReleases", "FavTop", "FavBottom", "FavLeft", "FavRight", "SyslogTop", "SyslogBottom", "SyslogLeft", "SyslogRight", "NotepadTop", "NotepadBottom",
	"NotepadLeft", "NotepadRight", "QueueTop", "QueueBottom", "QueueLeft", "QueueRight", "SearchTop", "SearchBottom", "SearchLeft", "SearchRight", "UsersTop", "UsersBottom",
	"UsersLeft", "UsersRight", "FinishedTop", "FinishedBottom", "FinishedLeft", "FinishedRight", "TextTop", "TextBottom", "TextLeft", "TextRight", "DirlistTop", "DirlistBottom",
	"DirlistLeft", "DirlistRight", "StatsTop", "StatsBottom", "StatsLeft", "StatsRight", "MaxMCNDownloads", "PartialMatchADC", "NoZeroByte", "MaxMCNUploads", "MCNAutoDetect",
	"DLAutoDetect", "ULAutoDetect", "CheckUseSkiplist", "CheckIgnoreZeroByte", "SubtractlistSkip", "TextDupeBackColor", "TextDupeBold", "TextDupeItalic",
	"SENTRY",
	// Int64
	"TotalUpload", "TotalDownload",
	"SENTRY"
};

SettingsManager::SettingsManager()
{
	fileEvents.resize(2);

	connectionSpeeds.push_back("0.1");
	connectionSpeeds.push_back("0.2");
	connectionSpeeds.push_back("0.5");
	connectionSpeeds.push_back("1");
	connectionSpeeds.push_back("2");
	connectionSpeeds.push_back("5");
	connectionSpeeds.push_back("8");
	connectionSpeeds.push_back("10");
	connectionSpeeds.push_back("20");
	connectionSpeeds.push_back("30");
	connectionSpeeds.push_back("40");
	connectionSpeeds.push_back("50");
	connectionSpeeds.push_back("60");
	connectionSpeeds.push_back("100");
	connectionSpeeds.push_back("200");
	connectionSpeeds.push_back("1000");

	Languages.push_back("English");   //0
	Languages.push_back("Swedish");   //1
	Languages.push_back("Finnish");   //2
	Languages.push_back("Italian");   //3
	Languages.push_back("Hungarian"); //4
	Languages.push_back("Romanian");  //5
	Languages.push_back("Danish");    //6
	Languages.push_back("Norwegian"); //7 
	Languages.push_back("Portuguese");//8
	Languages.push_back("Polish");    //9
	Languages.push_back("French");    //10
	Languages.push_back("Dutch");     //11
	Languages.push_back("Russian");   //12
	Languages.push_back("German");    //13

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
	
	setDefault(DOWNLOAD_DIRECTORY, Util::getPath(Util::PATH_DOWNLOADS));
	setDefault(TEMP_DOWNLOAD_DIRECTORY, Util::getPath(Util::PATH_USER_LOCAL) + "Incomplete" PATH_SEPARATOR_STR);
	setDefault(SLOTS, 2);
	setDefault(TCP_PORT, 0);
	setDefault(UDP_PORT, 0);
	setDefault(TLS_PORT, 0);
//	setDefault(INCOMING_CONNECTIONS, Util::isPrivateIp(Util::getLocalIp()) ? INCOMING_FIREWALL_PASSIVE : INCOMING_DIRECT);
	setDefault(INCOMING_CONNECTIONS, INCOMING_DIRECT);
	setDefault(OUTGOING_CONNECTIONS, OUTGOING_DIRECT);
	setDefault(AUTO_DETECT_CONNECTION, true);
	setDefault(AUTO_FOLLOW, true);
	setDefault(CLEAR_SEARCH, true);
	setDefault(SHARE_HIDDEN, false);
	setDefault(SHARE_SFV, false);
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
	setDefault(LOG_DIRECTORY, Util::getPath(Util::PATH_USER_LOCAL) + "Logs" PATH_SEPARATOR_STR);
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
	setDefault(BIND_ADDRESS, "0.0.0.0");
	setDefault(SOCKS_PORT, 1080);
	setDefault(SOCKS_RESOLVE, 1);
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
	setDefault(ANTI_FRAG, true);
	setDefault(NO_AWAYMSG_TO_BOTS, true);
	setDefault(SKIP_ZERO_BYTE, false);
	setDefault(ADLS_BREAK_ON_FIRST, false);
	setDefault(HUB_USER_COMMANDS, true);
	setDefault(AUTO_SEARCH_AUTO_MATCH, false);
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
	setDefault(MAGNET_REGISTER, true);
	setDefault(MAGNET_ASK, true);
	setDefault(MAGNET_ACTION, MAGNET_AUTO_SEARCH);
	setDefault(ADD_FINISHED_INSTANTLY, true);
	setDefault(DONT_DL_ALREADY_SHARED, false);
	setDefault(CONFIRM_HUB_REMOVAL, true);
	setDefault(USE_CTRL_FOR_LINE_HISTORY, true);
	setDefault(JOIN_OPEN_NEW_WINDOW, false);
	setDefault(SHOW_LAST_LINES_LOG, 10);
	setDefault(CONFIRM_DELETE, true);
	setDefault(ADC_DEBUG, false);
	setDefault(TOGGLE_ACTIVE_WINDOW, true);
	setDefault(SEARCH_HISTORY, 10);
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
	setDefault(AUTO_REFRESH_TIME, 60);
	setDefault(USE_TLS, true);
	setDefault(AUTO_SEARCH_LIMIT, 15);
	setDefault(AUTO_KICK_NO_FAVS, false);
	setDefault(PROMPT_PASSWORD, true);
	setDefault(SPY_FRAME_IGNORE_TTH_SEARCHES, false);
	setDefault(ALLOW_UNTRUSTED_HUBS, true);
	setDefault(ALLOW_UNTRUSTED_CLIENTS, true);		
	setDefault(FAST_HASH, true);
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
	setDefault(TOOLBAR, "0,-1,1,2,-1,3,4,5,-1,6,7,8,9,-1,10,11,12,13,-1,14,15,16,17,-1,19,20,21,22");
	setDefault(MEDIATOOLBAR, "0,-1,1,-1,2,3,4,5,6,7,8,9,-1");
	setDefault(SEARCH_ALTERNATE_COLOUR, RGB(255,200,0));
	setDefault(AUTO_PRIORITY_DEFAULT, false);
	setDefault(TOOLBARIMAGE,"");
	setDefault(TOOLBARHOTIMAGE,"");
	setDefault(REMOVE_FORBIDDEN, true);
	setDefault(EXTRA_DOWNLOAD_SLOTS, 3);

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
	setDefault(TEXT_URL_FORE_COLOR, RGB(0,0,255));
	setDefault(TEXT_URL_BOLD, false);
	setDefault(TEXT_URL_ITALIC, false);

	setDefault(TEXT_DUPE_BACK_COLOR, RGB(255, 255, 255));
	setDefault(DUPE_COLOR, RGB(255, 128, 255));
	setDefault(TEXT_DUPE_BOLD, false);
	setDefault(TEXT_DUPE_ITALIC, false);

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

	setDefault(CZCHARS_DISABLE, false);
	setDefault(REPORT_ALTERNATES, true);	

	setDefault(SOUNDS_DISABLED, false);
	setDefault(UPLOADQUEUEFRAME_SHOW_TREE, true);	
	setDefault(DONT_BEGIN_SEGMENT, true);
	setDefault(DONT_BEGIN_SEGMENT_SPEED, 512);

	setDefault(USE_VERTICAL_VIEW, true);
	setDefault(SEARCH_TIME, 15);
	setDefault(SUPPRESS_MAIN_CHAT, false);
	setDefault(AUTO_SLOTS, 5);	
	
	// default sounds
	setDefault(BEGINFILE, Util::emptyString);
	setDefault(BEEPFILE, Util::emptyString);
	setDefault(FINISHFILE, Util::emptyString);
	setDefault(SOURCEFILE, Util::emptyString);
	setDefault(UPLOADFILE, Util::emptyString);
	setDefault(FAKERFILE, Util::emptyString);
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
	setDefault(POPUP_DOWNLOAD_START, true);
	setDefault(POPUP_DOWNLOAD_FAILED, false);
	setDefault(POPUP_DOWNLOAD_FINISHED, true);
	setDefault(POPUP_UPLOAD_FINISHED, false);
	setDefault(POPUP_PM, false);
	setDefault(POPUP_NEW_PM, true);
	setDefault(POPUP_TYPE, 1);
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
	setDefault(NORMAL_COLOUR, RGB(0,0,0));
	setDefault(RESERVED_SLOT_COLOR, RGB(0,51,0));
	setDefault(IGNORED_COLOR, RGB(192,192,192));	
	setDefault(FAVORITE_COLOR, RGB(51,51,255));	
	setDefault(PASIVE_COLOR, RGB(132,132,132));
	setDefault(OP_COLOR, RGB(0,0,205));
	setDefault(HUBFRAME_VISIBLE, "1,1,0,1,0,1,0,0,0,0,0,0");
	setDefault(DIRECTORYLISTINGFRAME_VISIBLE, "1,1,0,1,1");	
	setDefault(FINISHED_VISIBLE, "1,1,1,1,1,1,1,1");
	setDefault(FINISHED_UL_VISIBLE, "1,1,1,1,1,1,1");
	setDefault(EMOTICONS_FILE, "RadoX");
	setDefault(GROUP_SEARCH_RESULTS, true);
	setDefault(TABS_ON_TOP, false);
	setDefault(DONT_ANNOUNCE_NEW_VERSIONS, false);
	setDefault(DOWNCONN_PER_SEC, 2);
	setDefault(FILTER_ENTER, false);
	setDefault(UC_SUBMENU, true);

	setDefault(DROP_MULTISOURCE_ONLY, true);
	setDefault(DISCONNECT_SPEED, 5);
	setDefault(DISCONNECT_FILE_SPEED, 15);
	setDefault(DISCONNECT_TIME, 40);
	setDefault(DISCONNECT_FILESIZE, 50);
    setDefault(REMOVE_SPEED, 2);


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
	
	setDefault(SHOW_WINAMP_CONTROL, false);
	setDefault(MEDIA_PLAYER, 0);
	setDefault(WMP_FORMAT, "/me playing: %[title] at %[bitrate] <Windows Media Player %[version]>");
	setDefault(ITUNES_FORMAT, "/me playing: %[title] at %[bitrate] <iTunes %[version]>");
	setDefault(MPLAYERC_FORMAT, "/me playing: %[title] <Media Player Classic>");
	setDefault(WINAMP_PATH, "C:\\Program Files\\Winamp\\winamp.exe");
	setDefault(IGNORE_USE_REGEXP_OR_WC, true);
	setDefault(NAT_SORT, true);
	setDefault(FAV_DL_SPEED, 0);
	setDefault(OPEN_FIRST_X_HUBS, 0);
	setDefault(IP_UPDATE, true);
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
	setDefault(POPUP_BACKCOLOR, RGB(58, 122, 180));
	setDefault(POPUP_TEXTCOLOR, RGB(0, 0, 0));
	setDefault(POPUP_TITLE_TEXTCOLOR, RGB(0, 0, 0));
	setDefault(SKIPLIST_DOWNLOAD, ".*|*All-Files-CRC-OK*|Descript.ion|thumbs.db|*.bad|*.missing|rushchk.log");
	setDefault(HIGH_PRIO_FILES, "*.sfv|*.nfo|*sample*|*subs*|*.jpg|*cover*|*.pls|*.m3u");
	setDefault(FLASH_WINDOW_ON_PM, false);
	setDefault(FLASH_WINDOW_ON_NEW_PM, false);
	setDefault(FLASH_WINDOW_ON_MYNICK, false);
	setDefault(AUTOSEARCH_EVERY, 15);
	setDefault(AUTOSEARCH_ENABLED_TIME, false);
	setDefault(AUTOSEARCH_ENABLED, false);
	setDefault(AUTOSEARCH_RECHECK_TIME, 30);
	setDefault(TB_IMAGE_SIZE, 22);
	setDefault(TB_IMAGE_SIZE_HOT, 22);
	setDefault(USE_HIGHLIGHT, false);
	setDefault(SHOW_QUEUE_BARS, true);
	setDefault(SEND_BLOOM, true);
	setDefault(LANG_SWITCH, 0);
	setDefault(EXPAND_DEFAULT, false);
	setDefault(SHARE_SKIPLIST_USE_REGEXP, true);
	setDefault(DOWNLOAD_SKIPLIST_USE_REGEXP, false);
	setDefault(HIGHEST_PRIORITY_USE_REGEXP, false);
	setDefault(OVERLAP_CHUNKS, true);
	setDefault(MIN_SEGMENT_SIZE, 1024);
	setDefault(OPEN_LOGS_INTERNAL, true);
	setDefault(DUPE_TEXT, true);
	setDefault(OPEN_SYSTEM_LOG, true);
	setDefault(FIRST_RUN, true);
	setDefault(USE_OLD_SHARING_UI, true);
	setDefault(LAST_SEARCH_FILETYPE, 0);
	setDefault(MAX_RESIZE_LINES, 2);
	setDefault(DONT_SHARE_EMPTY_DIRS, false);
	setDefault(ONLY_SHARE_FULL_DIRS, false);
	setDefault(DUPE_SEARCH, true);
	setDefault(PASSWD_PROTECT, false);
	setDefault(PASSWD_PROTECT_TRAY, false);
	setDefault(DISALLOW_CONNECTION_TO_PASSED_HUBS, false);
	setDefault(BOLD_HUB_TABS_ON_KICK, false);
	setDefault(SKIPLIST_SEARCH, "");
	setDefault(SEARCH_SKIPLIST, false);
	setDefault(SKIP_MSG_01, "*DISK2*|*cd2*");
	setDefault(SKIP_MSG_02, "*sample*");
	setDefault(SKIP_MSG_03, "*cover*");
	setDefault(REFRESH_VNAME_ON_SHAREPAGE, true);
	setDefault(AUTO_ADD_SOURCE, true);
	setDefault(KEEP_FINISHED_FILES, false);
	setDefault(ALLOW_NAT_TRAVERSAL, true);
	setDefault(USE_EXPLORER_THEME, true);
	setDefault(TESTWRITE, true);
	setDefault(INCOMING_REFRESH_TIME, 0);
	setDefault(USE_ADLS, true);
	setDefault(USE_ADLS_OWN_LIST, true);
	setDefault(DONT_DL_ALREADY_QUEUED, false);
	setDefault(DOWNLOADS_EXPAND, false);
	setDefault(SYSTEM_SHOW_UPLOADS, false);
	setDefault(SYSTEM_SHOW_DOWNLOADS, false);
	setDefault(SETTINGS_PROFILE, PROFILE_PUBLIC);
	setDefault(DOWNLOAD_SPEED, connectionSpeeds[0]);
	setDefault(LANGUAGE_SWITCH, 0);
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
	setDefault(PARTIAL_MATCH_ADC, true);
	setDefault(NO_ZERO_BYTE, false);
	setDefault(MCN_AUTODETECT, true);
	setDefault(DL_AUTODETECT, true);
	setDefault(UL_AUTODETECT, true);
	setDefault(MAX_MCN_UPLOADS, 1);
	setDefault(SKIP_SUBTRACT, 0);
#ifdef _WIN64
	setDefault(DECREASE_RAM, false);  
#else
	setDefault(DECREASE_RAM, true); //32 bit windows will most likely have less ram to spend (4gb max)
#endif
/*
#ifdef _WIN32
	OSVERSIONINFO ver;
	memzero(&ver, sizeof(OSVERSIONINFO));
	ver.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionEx((OSVERSIONINFO*)&ver);

	setDefault(USE_OLD_SHARING_UI, (ver.dwPlatformId == VER_PLATFORM_WIN32_NT) ? false : true);
#endif
	*/
	setSearchTypeDefaults();
}

void SettingsManager::load(string const& aFileName)
{
	try {
		SimpleXML xml;
		
		xml.fromXML(File(aFileName, File::READ, File::OPEN).read());
		
		xml.resetCurrentChild();
		
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
		if(xml.findChild("SearchTypes")) {
			try {
				searchTypes.clear();
				xml.stepIn();
				while(xml.findChild("SearchType")) {
					const string& extensions = xml.getChildData();
					if(extensions.empty()) {
						continue;
					}
					const string& name = xml.getChildAttrib("Id");
					if(name.empty()) {
						continue;
					}
					searchTypes[name] = StringTokenizer<string>(extensions, ';').getTokens();
				}
				xml.stepOut();
			} catch(const SimpleXMLException&) {
				setSearchTypeDefaults();
			}
		}

		xml.resetCurrentChild();
		if(xml.findChild("SearchHistory")) {
			xml.stepIn();
			while(xml.findChild("Search")) {
				addSearchToHistory(Text::toT(xml.getChildData()));
			}
			xml.stepOut();
		}


		if(SETTING(PRIVATE_ID).length() != 39 || CID(SETTING(PRIVATE_ID)).isZero()) {
			set(PRIVATE_ID, CID::generate().toBase32());
		}

		double v = Util::toDouble(SETTING(CONFIG_VERSION));
		// if(v < 0.x) { // Fix old settings here }

		if(v <= 0.674) {

			// Formats changed, might as well remove these...
			set(LOG_FORMAT_POST_DOWNLOAD, Util::emptyString);
			set(LOG_FORMAT_POST_UPLOAD, Util::emptyString);
			set(LOG_FORMAT_MAIN_CHAT, Util::emptyString);
			set(LOG_FORMAT_PRIVATE_CHAT, Util::emptyString);
			set(LOG_FORMAT_STATUS, Util::emptyString);
			set(LOG_FORMAT_SYSTEM, Util::emptyString);
			set(LOG_FILE_MAIN_CHAT, Util::emptyString);
			set(LOG_FILE_STATUS, Util::emptyString);
			set(LOG_FILE_PRIVATE_CHAT, Util::emptyString);
			set(LOG_FILE_UPLOAD, Util::emptyString);
			set(LOG_FILE_DOWNLOAD, Util::emptyString);
			set(LOG_FILE_SYSTEM, Util::emptyString);
		}

		//Convert the old lang_switch to new one with correct counts... Oh Zinden why why..
		//Zinden had 0,1,2 switch for english
		if(v <= 2.08) {
			if(SETTING(LANG_SWITCH) == 0 || SETTING(LANG_SWITCH) == 1 || SETTING(LANG_SWITCH) == 2) {
			      set(LANGUAGE_SWITCH, 0); 
			} else {
				set(LANGUAGE_SWITCH, (LANG_SWITCH - 2));
				 
			}
		}

		if(v <= 2.07 && SETTING(INCOMING_CONNECTIONS) != INCOMING_FIREWALL_PASSIVE) {
			set(AUTO_DETECT_CONNECTION, false); //Don't touch if it works
		}


		setDefault(UDP_PORT, SETTING(TCP_PORT));

		File::ensureDirectory(SETTING(TLS_TRUSTED_CERTIFICATES_PATH));
		
		fire(SettingsManagerListener::Load(), xml);

		xml.stepOut();

	} catch(const Exception&) {
		if(CID(SETTING(PRIVATE_ID)).isZero())
			set(PRIVATE_ID, CID::generate().toBase32());
	}

	if(SETTING(INCOMING_CONNECTIONS) == INCOMING_DIRECT || INCOMING_FIREWALL_UPNP || INCOMING_FIREWALL_NAT) {
	if(SETTING(TLS_PORT) == 0) {
		set(TLS_PORT, (int)Util::rand(10000, 32000));
		}
	}

	if(SETTING(INCOMING_CONNECTIONS) == INCOMING_DIRECT) {
		set(TCP_PORT, (int)Util::rand(10000, 32000));
		set(UDP_PORT, (int)Util::rand(10000, 32000));
		set(TLS_PORT, (int)Util::rand(10000, 32000));
	}
}

void SettingsManager::save(string const& aFileName) {

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

	xml.addTag("SearchHistory");
	xml.stepIn();
	{
		Lock l(cs);
		for(TStringIter i = searchHistory.begin(); i != searchHistory.end(); ++i) {
			string tmp = Text::fromT(*i);
			xml.addTag("Search", tmp);
		}
	}
	xml.stepOut();
	
	/*xml.addTag("SearchTypes");
	xml.stepIn();
	for(SearchTypesIterC i = searchTypes.begin(); i != searchTypes.end(); ++i) {
		xml.addTag("SearchType", Util::toString(";", i->second));
		xml.addChildAttrib("Id", i->first);
	}
	xml.stepOut();*/

	fire(SettingsManagerListener::Save(), xml);

	try {
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
	}
}

void SettingsManager::validateSearchTypeName(const string& name) const {
	if(name.empty() || (name.size() == 1 && name[0] >= '1' && name[0] <= '6')) {
		throw SearchTypeException("Invalid search type name"); // TODO: localize
	}
	for(int type = SearchManager::TYPE_ANY; type != SearchManager::TYPE_LAST; ++type) {
		if(SearchManager::getTypeStr(type) == name) {
			throw SearchTypeException("This search type already exists"); // TODO: localize
		}
	}
}

void SettingsManager::setSearchTypeDefaults() {
	searchTypes.clear();

	// for conveniency, the default search exts will be the same as the ones defined by SEGA.
	const auto& searchExts = AdcHub::getSearchExts();
	for(size_t i = 0, n = searchExts.size(); i < n; ++i)
		searchTypes[string(1, '1' + i)] = searchExts[i];

	fire(SettingsManagerListener::SearchTypesChanged());
}

void SettingsManager::addSearchType(const string& name, const StringList& extensions, bool validated) {
	if(!validated) {
		validateSearchTypeName(name);
	}

	if(searchTypes.find(name) != searchTypes.end()) {
		throw SearchTypeException("This search type already exists"); // TODO: localize
	}

	searchTypes[name] = extensions;
	fire(SettingsManagerListener::SearchTypesChanged());
}

void SettingsManager::delSearchType(const string& name) {
	validateSearchTypeName(name);
	searchTypes.erase(name);
	fire(SettingsManagerListener::SearchTypesChanged());
}

void SettingsManager::renameSearchType(const string& oldName, const string& newName) {
	validateSearchTypeName(newName);
	StringList exts = getSearchType(oldName)->second;
	addSearchType(newName, exts, true);
	searchTypes.erase(oldName);
}

void SettingsManager::modSearchType(const string& name, const StringList& extensions) {
	getSearchType(name)->second = extensions;
	fire(SettingsManagerListener::SearchTypesChanged());
}

const StringList& SettingsManager::getExtensions(const string& name) {
	return getSearchType(name)->second;
}

SettingsManager::SearchTypesIter SettingsManager::getSearchType(const string& name) {
	SearchTypesIter ret = searchTypes.find(name);
	if(ret == searchTypes.end()) {
		throw SearchTypeException("No such search type"); // TODO: localize
	}
	return ret;
}

} // namespace dcpp

/**
 * @file
 * $Id: SettingsManager.cpp 551 2010-12-18 12:14:16Z bigmuscle $
 */
