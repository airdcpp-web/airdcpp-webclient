#ifndef DCPLUSPLUS_DCPP_FLAGS_H_
#define DCPLUSPLUS_DCPP_FLAGS_H_

#include <stdint.h>

namespace dcpp {

class Flags {
	public:
		typedef uint32_t MaskType;

		Flags() : flags(0) { }
		Flags(MaskType f) : flags(f) { }
		bool isSet(MaskType aFlag) const { return (flags & aFlag) == aFlag; }
		bool isAnySet(MaskType aFlag) const { return (flags & aFlag) != 0; }
		void setFlag(MaskType aFlag) { flags |= aFlag; }
		void unsetFlag(MaskType aFlag) { flags &= ~aFlag; }
		MaskType getFlags() const { return flags; }
private:
	MaskType flags;
};

} // namespace dcpp

#endif /*FLAGS_H_*/
