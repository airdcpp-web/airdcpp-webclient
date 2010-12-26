#ifndef UPLOADMANAGERLISTENER_H_
#define UPLOADMANAGERLISTENER_H_

#include "forward.h"

namespace dcpp {

class UploadManagerListener {
	friend class UploadQueueItem; 
public:
	virtual ~UploadManagerListener() { }
	template<int I>	struct X { enum { TYPE = I }; };

	typedef X<0> Complete;
	typedef X<1> Failed;
	typedef X<2> Starting;
	typedef X<3> Tick;
	typedef X<4> QueueAdd;
	typedef X<5> QueueRemove;
	typedef X<6> QueueItemRemove;
	typedef X<7> QueueUpdate;

	virtual void on(Starting, const Upload*) throw() { }
	virtual void on(Tick, const UploadList&) throw() { }
	virtual void on(Complete, const Upload*) throw() { }
	virtual void on(Failed, const Upload*, const string&) throw() { }
	virtual void on(QueueAdd, UploadQueueItem*) throw() { }
	virtual void on(QueueRemove, const UserPtr&) throw() { }
	virtual void on(QueueItemRemove, UploadQueueItem*) throw() { }
	virtual void on(QueueUpdate) throw() { }

};

} // namespace dcpp

#endif /*UPLOADMANAGERLISTENER_H_*/
