#ifndef DCPLUSPLUS_DCPP_DOWNLOAD_H_
#define DCPLUSPLUS_DCPP_DOWNLOAD_H_

#include <string>
#include <memory>

#include "forward.h"
#include "noexcept.h"
#include "Transfer.h"
#include "MerkleTree.h"
#include "Flags.h"
#include "Bundle.h"
#include "GetSet.h"

namespace dcpp {

using std::string;
using std::unique_ptr;

/**
 * Comes as an argument in the DownloadManagerListener functions.
 * Use it to retrieve information about the ongoing transfer.
 */
class Download : public Transfer, public Flags {
public:

	enum {
		FLAG_ZDOWNLOAD			= 0x01,
		FLAG_CHUNKED			= 0x02,
		FLAG_TTH_CHECK			= 0x04,
		FLAG_SLOWUSER			= 0x08,
		FLAG_XML_BZ_LIST		= 0x10,
		FLAG_PARTIAL			= 0x40,
		FLAG_OVERLAP			= 0x80,
		FLAG_VIEW				= 0x100,
		FLAG_RECURSIVE			= 0x200,
		FLAG_QUEUE				= 0x400,
		FLAG_NFO				= 0x800,
		FLAG_TTHLIST            = 0x1000,
		FLAG_TTHLIST_BUNDLE		= 0x2000,
		FLAG_HIGHEST_PRIO		= 0x4000
	};

	bool operator==(const Download* d) const {
		return compare(getToken(), d->getToken()) == 0;
	}

	Download(UserConnection& conn, QueueItem& qi) noexcept;

	void getParams(const UserConnection& aSource, ParamMap& params);

	~Download();

	bool isFileList();

	/** @return Target filename without path. */
	string getTargetFileName() const;

	/** Open the target output for writing */
	void open(int64_t bytes, bool z, bool hasDownloadedBytes);

	/** Release the target output */
	void close();

	/** @internal */
	TigerTree& getTigerTree() { return tt; }
	const string& getPFS() const { return pfs; }

	/** @internal */
	AdcCommand getCommand(bool zlib, const string& mySID) const;
	const unique_ptr<OutputStream>& getOutput() const { return output; }

	GETSET(string, tempTarget, TempTarget);
	//GETSET(string, remotePath, RemotePath);

	IGETSET(uint64_t, lastTick, LastTick, GET_TICK());
	IGETSET(bool, treeValid, TreeValid, false);
	IGETSET(BundlePtr, bundle, Bundle, nullptr);
private:
	Download(const Download&);
	Download& operator=(const Download&);

	const string& getDownloadTarget() const;

	unique_ptr<OutputStream> output;
	TigerTree tt;
	string pfs;
};

} // namespace dcpp

#endif /*DOWNLOAD_H_*/
