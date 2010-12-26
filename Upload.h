#ifndef UPLOAD_H_
#define UPLOAD_H_

#include "forward.h"
#include "Transfer.h"
#include "Flags.h"

namespace dcpp {

class Upload : public Transfer, public Flags {
public:
	enum Flags {
		FLAG_ZUPLOAD = 0x01,
		FLAG_PENDING_KICK = 0x02,
		FLAG_RESUMED = 0x04,
		FLAG_CHUNKED = 0x08,
		FLAG_PARTIAL = 0x10
	};

	Upload(UserConnection& conn, const string& path, const TTHValue& tth);
	~Upload();

	void getParams(const UserConnection& aSource, StringMap& params) const;

	GETSET(int64_t, fileSize, FileSize);
	GETSET(InputStream*, stream, Stream);

	uint8_t delayTime;
};

} // namespace dcpp

#endif /*UPLOAD_H_*/
