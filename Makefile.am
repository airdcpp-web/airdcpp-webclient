lib_LTLIBRARIES = libdcppclient.la

LIBDCPP_CURRENT=0
LIBDCPP_REVISION=0
LIBDCPP_AGE=0

libdcppclient_la_LDFLAGS = -no-undefined -export-dynamic -version-info $(LIBDCPP_CURRENT):$(LIBDCPP_REVISION):$(LIBDCPP_AGE)

libdcppclient_la_CXXFLAGS = -Wall -Winvalid-pch -pedantic -ansi -Wno-long-long -I$(top_srcdir)/yassl/include

libdcppclient_la_SOURCES= \
AdcCommand.cpp \
AdcHub.cpp \
ADLSearch.cpp \
BufferedSocket.cpp \
BZUtils.cpp \
Client.cpp \
ClientManager.cpp \
ConnectionManager.cpp \
CryptoManager.cpp \
DCPlusPlus.cpp \
DirectoryListing.cpp \
DownloadManager.cpp \
Encoder.cpp \
Exception.cpp \
FavoriteManager.cpp \
FinishedManager.cpp \
HashManager.cpp \
HttpConnection.cpp \
LogManager.cpp \
NmdcHub.cpp \
QueueManager.cpp \
ResourceManager.cpp \
SearchManager.cpp \
ServerSocket.cpp \
SettingsManager.cpp \
SFVReader.cpp \
ShareManager.cpp \
SimpleXML.cpp \
Socket.cpp \
SSLSocket.cpp \
stdinc.cpp \
StringDefs.cpp \
StringTokenizer.cpp \
Text.cpp \
Thread.cpp \
TigerHash.cpp \
TimerManager.cpp \
UploadManager.cpp \
UserConnection.cpp \
User.cpp \
Util.cpp \
ZUtils.cpp \
AdcCommand.h \
AdcHub.h \
ADLSearch.h \
BitInputStream.h \
BitOutputStream.h \
BloomFilter.h \
BufferedSocket.h \
BZUtils.h \
CID.h \
Client.h \
ClientManager.h \
ClientManagerListener.h \
config.h \
ConnectionManager.h \
ConnectionManagerListener.h \
CriticalSection.h \
CryptoManager.h \
DCPlusPlus.h \
DirectoryListing.h \
DownloadManager.h \
Encoder.h \
Exception.h \
FastAlloc.h \
FavoriteManager.h \
FavoriteUser.h \
File.h \
FilteredFile.h \
FinishedManager.h \
HashManager.h \
HashValue.h \
HttpConnection.h \
LogManager.h \
MerkleCheckOutputStream.h \
MerkleTree.h \
NmdcHub.h \
Pointer.h \
QueueItem.h \
QueueManager.h \
QueueManagerListener.h \
ResourceManager.h \
SearchManager.h \
SearchManagerListener.h \
Semaphore.h \
ServerSocket.h \
SettingsManager.h \
SFVReader.h \
ShareManager.h \
SimpleXML.h \
Singleton.h \
Socket.h \
Speaker.h \
SSLSocket.h \
stdinc.h \
Streams.h \
StringDefs.h \
StringSearch.h \
StringTokenizer.h \
Text.h \
Thread.h \
TigerHash.h \
TimerManager.h \
UploadManager.h \
UserCommand.h \
UserConnection.h \
User.h \
Util.h \
version.h \
ZUtils.h 

pch: stdinc.h.gch

BUILT_SOURCES = stdinc.h.gch

stdinc.h.gch: stdinc.h
	$(CXXCOMPILE) $?
	
