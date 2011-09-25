#ifndef DCPLUSPLUS_DCPP_DOWNLOAD_H_
#define DCPLUSPLUS_DCPP_DOWNLOAD_H_

#include "forward.h"
#include "noexcept.h"
#include "Transfer.h"
#include "MerkleTree.h"
#include "Flags.h"
#include "Streams.h"
#include "QueueItem.h"

namespace dcpp {

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
		FLAG_TTHLIST            = 0x1000
	};

	Download(UserConnection& conn, QueueItem& qi, const string& path) noexcept;

	void getParams(const UserConnection& aSource, StringMap& params);

	~Download();

	/** @return Target filename without path. */
	string getTargetFileName() const {
		return Util::getFileName(getPath());
	}

	/** @internal */
	const string& getDownloadTarget() const {
		return (getTempTarget().empty() ? getPath() : getTempTarget());
	}

	/** @internal */
	TigerTree& getTigerTree() { return tt; }
	string& getPFS() { return pfs; }
	
	const TigerTree& getTigerTree() const { return tt; }
	const string& getPFS() const { return pfs; }

	/** @internal */
	AdcCommand getCommand(bool zlib) const;

	GETSET(string, tempTarget, TempTarget);
	GETSET(uint64_t, lastTick, LastTick);
	GETSET(OutputStream*, file, File);
	GETSET(bool, treeValid, TreeValid);
private:
	Download(const Download&);
	Download& operator=(const Download&);

	TigerTree tt;
	string pfs;
};

} // namespace dcpp

#endif /*DOWNLOAD_H_*/
