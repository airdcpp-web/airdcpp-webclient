
#include "stdinc.h"
#include "forward.h"
#include "typedefs.h"

#include "AutoSearch.h"
#include "PrioritySearchQueue.h"

namespace dcpp {

	class Searches : public PrioritySearchQueue<AutoSearchPtr> {
	public:
		Searches() {}
		~Searches() {}

		void addItem(AutoSearchPtr& as) {
			addSearchPrio(as);
			searches.emplace(as->getToken(), as);
		}

		void removeItem(AutoSearchPtr& as) noexcept {
			removeSearchPrio(as);
			searches.erase(as->getToken());
		}

		bool hasItem(AutoSearchPtr& as) {
			return searches.find(as->getToken()) != searches.end();
		}

		AutoSearchPtr getItem(const ProfileToken& aToken) const {
			auto ret = searches.find(aToken);
			return ret != searches.end() ? ret->second : nullptr;
		}

		AutoSearchPtr getItem(void* aSearch) const {
			auto i = find_if(searches | map_values, [&](const AutoSearchPtr& s) {
				return s.get() == aSearch;
			});
			return i.base() != searches.end() ? *i : nullptr;
		}

		AutoSearchMap& getItems() { return searches; }
		const AutoSearchMap& getItems() const { return searches; }
	private:
		/** Bundles by token */
		AutoSearchMap searches;
	};
}
