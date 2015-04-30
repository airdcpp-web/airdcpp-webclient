/*
 * Copyright (C) 2001-2015 Jacek Sieka, arnetheduck on gmail point com
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
#include "Download.h"

#include "File.h"
#include "FilteredFile.h"
#include "HashManager.h"
#include "MerkleCheckOutputStream.h"
#include "MerkleTreeOutputStream.h"
#include "QueueItem.h"
#include "SharedFileStream.h"
#include "UserConnection.h"
#include "ZUtils.h"

namespace dcpp {

Download::Download(UserConnection& conn, QueueItem& qi) noexcept : Transfer(conn, qi.getTarget(), qi.getTTH()),
	tempTarget(qi.getTempTarget())
{
	conn.setDownload(this);
	
	QueueItem::SourceConstIter source = qi.getSource(getUser());

	if(qi.isSet(QueueItem::FLAG_PARTIAL_LIST)) {
		setType(TYPE_PARTIAL_LIST);
	} else if(qi.isSet(QueueItem::FLAG_USER_LIST)) {
		setType(TYPE_FULL_LIST);
	}

	if(source->isSet(QueueItem::Source::FLAG_PARTIAL))
		setFlag(FLAG_PARTIAL);
	if(qi.isSet(QueueItem::FLAG_CLIENT_VIEW))
		setFlag(FLAG_VIEW);
	if(qi.isSet(QueueItem::FLAG_MATCH_QUEUE))
		setFlag(FLAG_QUEUE);
	if(qi.isSet(QueueItem::FLAG_VIEW_NFO))
		setFlag(FLAG_NFO);
	if(qi.isSet(QueueItem::FLAG_RECURSIVE_LIST))
		setFlag(FLAG_RECURSIVE);
	if(qi.isSet(QueueItem::FLAG_TTHLIST_BUNDLE))
		setFlag(FLAG_TTHLIST_BUNDLE);
	if (qi.getPriority() == QueueItemBase::HIGHEST)
		setFlag(FLAG_HIGHEST_PRIO);

	if (qi.getBundle()) {
		dcassert(!qi.isSet(QueueItem::FLAG_USER_LIST));
		dcassert(!qi.isSet(QueueItem::FLAG_TEXT));
		setBundle(qi.getBundle());
	}
	
	if(getType() == TYPE_FILE && qi.getSize() != -1) {
		if(HashManager::getInstance()->getTree(getTTH(), getTigerTree())) {
			setTreeValid(true);
			setSegment(qi.getNextSegment(getTigerTree().getBlockSize(), conn.getChunkSize(), conn.getSpeed(), source->getPartialSource(), true));
			qi.setBlockSize(getTigerTree().getBlockSize());
		} else if(conn.isSet(UserConnection::FLAG_SUPPORTS_TTHL) && !source->isSet(QueueItem::Source::FLAG_NO_TREE) && qi.getSize() > HashManager::MIN_BLOCK_SIZE) {
			// Get the tree unless the file is small (for small files, we'd probably only get the root anyway)
			setType(TYPE_TREE);
			getTigerTree().setFileSize(qi.getSize());
			setSegment(Segment(0, -1));
		} else {
			// Use the root as tree to get some sort of validation at least...
			getTigerTree() = TigerTree(qi.getSize(), qi.getSize(), getTTH());
			setTreeValid(true);
			setSegment(qi.getNextSegment(getTigerTree().getBlockSize(), 0, 0, source->getPartialSource(), true));
		}
		
		if ((getStartPos() + getSegmentSize()) != qi.getSize() || (conn.getDownload() && conn.getDownload()->isSet(FLAG_CHUNKED))) {
			setFlag(FLAG_CHUNKED);
		}

		if(getSegment().getOverlapped()) {
			setFlag(FLAG_OVERLAP);

			// set overlapped flag to original segment
			for(auto d: qi.getDownloads()) {
				if(d->getSegment().contains(getSegment())) {
					d->setOverlapped(true);
					break;
				}
			}
		}
	}
}

Download::~Download() {
	getUserConnection().setDownload(0);
}

AdcCommand Download::getCommand(bool zlib, const string& mySID) const {
	AdcCommand cmd(AdcCommand::CMD_GET);
	
	cmd.addParam(Transfer::names[getType()]);

	if(getType() == TYPE_PARTIAL_LIST) {
		if (isSet(Download::FLAG_TTHLIST_BUNDLE)) {
			//these must be converted to adc file when adding (if needed, no slash for bundle requests)
			cmd.addParam(getTempTarget());
		} else {
			cmd.addParam(Util::toAdcFile(getTempTarget()));
		}
	} else if(getType() == TYPE_FULL_LIST) {
		if(isSet(Download::FLAG_XML_BZ_LIST)) {
			cmd.addParam(USER_LIST_NAME_BZ);
		} else {
			cmd.addParam(USER_LIST_NAME);
		}
	} else {
		cmd.addParam("TTH/" + getTTH().toBase32());
	}

	cmd.addParam(Util::toString(getStartPos()));
	cmd.addParam(Util::toString(getSegmentSize()));
	if(!mySID.empty()) //add requester's SID (mySID) to the filelist request, so he can find the hub we are calling from.
		cmd.addParam("ID", mySID); 

	if(zlib && SETTING(COMPRESS_TRANSFERS)) {
		cmd.addParam("ZL1");
	}

	if(isSet(Download::FLAG_RECURSIVE) && getType() == TYPE_PARTIAL_LIST) {
		cmd.addParam("RE1");
	}
	
	if(isSet(Download::FLAG_QUEUE) && getType() == TYPE_PARTIAL_LIST) {	 
		cmd.addParam("TL1");	 
	}

	return cmd;
}

void Download::getParams(const UserConnection& aSource, ParamMap& params) {
	Transfer::getParams(aSource, params);
	params["target"] = getPath();
}

bool Download::isFileList() {
	return getType() == Transfer::TYPE_FULL_LIST || getType() == Transfer::TYPE_PARTIAL_LIST;
}

string Download::getTargetFileName() const {
	return Util::getFileName(getPath());
}

const string& Download::getDownloadTarget() const {
	return (getTempTarget().empty() ? getPath() : getTempTarget());
}

void Download::open(int64_t bytes, bool z, bool hasDownloadedBytes) {
	if(getType() == Transfer::TYPE_FILE) {
		auto target = getDownloadTarget();
		auto fullSize = tt.getFileSize();

		if(getOverlapped() && bundle) {
			setOverlapped(false);
 	 
			bool found = false;
			// ok, we got a fast slot, so it's possible to disconnect original user now
			for(auto d: bundle->getDownloads()) {
				if(d != this && compare(d->getPath(), getPath()) == 0 && d->getSegment().contains(getSegment())) {
 	 
					// overlapping has no sense if segment is going to finish
					if(d->getSecondsLeft() < 10)
						break;
 	 
					found = true;
 	 
					// disconnect slow chunk
					d->getUserConnection().disconnect();
					break;
				}
			}

			if(!found) {
				// slow chunk already finished ???
				throw Exception(STRING(DOWNLOAD_FINISHED_IDLE));
			}
		}

		if(hasDownloadedBytes) {
			if(File::getSize(target) != fullSize) {
				// When trying the download the next time, the resume pos will be reset
				throw Exception(CSTRING(TARGET_FILE_MISSING));
			}
		} else {
			File::ensureDirectory(target);
		}

		int fileFlags = File::OPEN | File::CREATE | File::SHARED_WRITE;
		if (getSegment().getEnd() != fullSize) {
			//segmented download, let Windows decide the buffering
			fileFlags |= File::BUFFER_AUTO;
		}

		unique_ptr<SharedFileStream> f(new SharedFileStream(target, File::WRITE, fileFlags));

		if(f->getSize() != fullSize) {
			f->setSize(fullSize);
		}

		f->setPos(getSegment().getStart());
		output = move(f);
		tempTarget = target;
	} else if(getType() == Transfer::TYPE_FULL_LIST) {
		auto target = getPath();
		File::ensureDirectory(target);

		if(isSet(Download::FLAG_XML_BZ_LIST)) {
			target += ".xml.bz2";
		} else {
			target += ".xml";
		}

		output.reset(new File(target, File::WRITE, File::OPEN | File::TRUNCATE | File::CREATE));
		tempTarget = target;
	} else if(getType() == Transfer::TYPE_PARTIAL_LIST) {
		output.reset(new StringOutputStream(pfs));
	} else if(getType() == Transfer::TYPE_TREE) {
		output.reset(new MerkleTreeOutputStream<TigerTree>(tt));
	}

	if((getType() == Transfer::TYPE_FILE || getType() == Transfer::TYPE_FULL_LIST) && SETTING(BUFFER_SIZE) > 0 ) {
		output.reset(new BufferedOutputStream<true>(output.release()));
	}

	if(getType() == Transfer::TYPE_FILE && !SettingsManager::lanMode) {
		typedef MerkleCheckOutputStream<TigerTree, true> MerkleStream;

		output.reset(new MerkleStream(tt, output.release(), getStartPos()));
		setFlag(Download::FLAG_TTH_CHECK);
	}

	// Check that we don't get too many bytes
	output.reset(new LimitedOutputStream<true>(output.release(), bytes));

	if(z) {
		setFlag(Download::FLAG_ZDOWNLOAD);
		output.reset(new FilteredOutputStream<UnZFilter, true>(output.release()));
	}
}

void Download::close()
{
	output.reset();
}

} // namespace dcpp
