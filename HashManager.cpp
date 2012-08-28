/* 
 * Copyright (C) 2001-2010 Jacek Sieka, arnetheduck on gmail point com
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
#include "HashManager.h"

#include "ResourceManager.h"
#include "SimpleXML.h"
#include "LogManager.h"
#include "File.h"
#include "FileReader.h"
#include "ZUtils.h"
#include "SFVReader.h"
#include "ShareManager.h"


namespace dcpp {

#define HASH_FILE_VERSION_STRING "2"
static const uint32_t HASH_FILE_VERSION = 2;
const int64_t HashManager::MIN_BLOCK_SIZE = 64 * 1024;

bool HashManager::checkTTH(const string& aFileName, int64_t aSize, uint32_t aTimeStamp) {
	if (!store.checkTTH(Text::toLower(aFileName), aSize, aTimeStamp)) {
		hasher.hashFile(aFileName, aSize);
		return false;
	}
	return true;
}

TTHValue HashManager::getTTH(const string& aFileName, int64_t aSize) {
	const TTHValue* tth = store.getTTH(Text::toLower(aFileName));
	if (!tth) {
		hasher.hashFile(aFileName, aSize);
		throw HashException();
	}
	return *tth;
}

bool HashManager::getTree(const TTHValue& root, TigerTree& tt) {
	return store.getTree(root, tt);
}

size_t HashManager::getBlockSize(const TTHValue& root) {
	return store.getBlockSize(root);
}

void HashManager::hashDone(const string& aFileName, uint64_t aTimeStamp, const TigerTree& tth, int64_t speed, int64_t /*size*/) {
	try {
		store.addFile(Text::toLower(aFileName), aTimeStamp, tth, true);
	} catch (const Exception& e) {
		LogManager::getInstance()->message(STRING(HASHING_FAILED) + " " + e.getError(), LogManager::LOG_ERROR);
		return;
	}
	
	if(BOOLSETTING(LOG_HASHING)) {
			string fn = aFileName;
		if (count(fn.begin(), fn.end(), PATH_SEPARATOR) >= 2) {
			string::size_type i = fn.rfind(PATH_SEPARATOR);
			i = fn.rfind(PATH_SEPARATOR, i - 1);
			fn.erase(0, i);
			fn.insert(0, "...");
		}
	
		if (speed > 0) {
			LogManager::getInstance()->message(STRING(HASHING_FINISHED) + " " + fn + " (" + Util::formatBytes(speed) + "/s)", LogManager::LOG_INFO);
		} else {
			LogManager::getInstance()->message(STRING(HASHING_FINISHED) + " " + fn, LogManager::LOG_INFO);
		}
	}

	fire(HashManagerListener::TTHDone(), aFileName, tth.getRoot());
}

void HashManager::HashStore::addFile(const string&& aFileLower, uint64_t aTimeStamp, const TigerTree& tth, bool aUsed) {
	addTree(tth);

	WLock l(cs);
	FileInfoList& fileList = fileIndex[Util::getFilePath(aFileLower)];
	FileInfoIter j = find(fileList.begin(), fileList.end(), Util::getFileName(aFileLower));
	if (j != fileList.end()) {
		fileList.erase(j);
	}

	fileList.push_back(FileInfo(Util::getFileName(aFileLower), tth.getRoot(), aTimeStamp, aUsed));
	dirty = true;
}

void HashManager::HashStore::addTree(const TigerTree& tt) noexcept {
	WLock l(cs);
	if (treeIndex.find(tt.getRoot()) == treeIndex.end()) {
		try {
			File f(getDataFile(), File::READ | File::WRITE, File::OPEN | File::SHARED);
			int64_t index = saveTree(f, tt);
			treeIndex.insert(make_pair(tt.getRoot(), TreeInfo(tt.getFileSize(), index, tt.getBlockSize())));
			dirty = true;
		} catch (const FileException& e) {
			LogManager::getInstance()->message(STRING(ERROR_SAVING_HASH) + " " + e.getError(), LogManager::LOG_ERROR);
		}
	}
}

int64_t HashManager::HashStore::saveTree(File& f, const TigerTree& tt) {
	if (tt.getLeaves().size() == 1)
		return SMALL_TREE;

	f.setPos(0);
	int64_t pos = 0;
	size_t n = sizeof(pos);
	if (f.read(&pos, n) != sizeof(pos))
		throw HashException(STRING(HASH_READ_FAILED));

	// Check if we should grow the file, we grow by a meg at a time...
	int64_t datsz = f.getSize();
	if ((pos + (int64_t) (tt.getLeaves().size() * TTHValue::BYTES)) >= datsz) {
		f.setPos(datsz + 1024 * 1024);
		f.setEOF();
	}
	f.setPos(pos); dcassert(tt.getLeaves().size() > 1);
	f.write(tt.getLeaves()[0].data, (tt.getLeaves().size() * TTHValue::BYTES));
	int64_t p2 = f.getPos();
	f.setPos(0);
	f.write(&p2, sizeof(p2));
	return pos;
}

bool HashManager::HashStore::loadTree(File& f, const TreeInfo& ti, const TTHValue& root, TigerTree& tt) {
	if (ti.getIndex() == SMALL_TREE) {
		tt = TigerTree(ti.getSize(), ti.getBlockSize(), root);
		return true;
	}
	try {
		f.setPos(ti.getIndex());
		size_t datalen = TigerTree::calcBlocks(ti.getSize(), ti.getBlockSize()) * TTHValue::BYTES;
		boost::scoped_array<uint8_t> buf(new uint8_t[datalen]);
		f.read(&buf[0], datalen);
		tt = TigerTree(ti.getSize(), ti.getBlockSize(), &buf[0]);
		if (!(tt.getRoot() == root))
			return false;
	} catch (const Exception&) {
		return false;
	}

	return true;
}

bool HashManager::HashStore::getTree(const TTHValue& root, TigerTree& tt) {
	RLock l(cs);
	TreeIterC i = treeIndex.find(root);
	if (i == treeIndex.end())
		return false;
	try {
		File f(getDataFile(), File::READ, File::OPEN | File::SHARED | File::RANDOM_ACCESS);
		return loadTree(f, i->second, root, tt);
	} catch (const Exception&) {
		return false;
	}
}

size_t HashManager::HashStore::getBlockSize(const TTHValue& root) const {
	RLock l(cs);
	TreeMap::const_iterator i = treeIndex.find(root);
	return i == treeIndex.end() ? 0 : static_cast<size_t>(i->second.getBlockSize());
}

bool HashManager::HashStore::checkTTH(const string&& aFileLower, int64_t aSize, uint32_t aTimeStamp) {
	RLock l(cs);
	DirIter i = fileIndex.find(Util::getFilePath(aFileLower));
	if (i != fileIndex.end()) {
		FileInfoIter j = find(i->second.begin(), i->second.end(), Util::getFileName(aFileLower));
		if (j != i->second.end()) {
			FileInfo& fi = *j;
			TreeIterC ti = treeIndex.find(fi.getRoot());
			if (ti == treeIndex.end() || ti->second.getSize() != aSize || fi.getTimeStamp() != aTimeStamp) {
				i->second.erase(j);
				dirty = true;
				return false;
			}
			return true;
		}
	} 
	return false;
}

const TTHValue* HashManager::HashStore::getTTH(const string&& aFileLower) {
	RLock l(cs);
	DirIter i = fileIndex.find(Util::getFilePath(aFileLower));
	if (i != fileIndex.end()) {
		FileInfoIter j = find(i->second.begin(), i->second.end(), Util::getFileName(aFileLower));
		if (j != i->second.end()) {
			j->setUsed(true);
			return &(j->getRoot());
		}
	}
	return nullptr;
}

void HashManager::HashStore::rebuild() {
	try {
		DirMap newFileIndex;
		TreeMap newTreeIndex;

		{
			RLock l(cs);
			for (DirIterC i = fileIndex.begin(); i != fileIndex.end(); ++i) {
				for (FileInfoIterC j = i->second.begin(); j != i->second.end(); ++j) {
					if (!j->getUsed())
						continue;

					TreeIterC k = treeIndex.find(j->getRoot());
					if (k != treeIndex.end()) {
						newTreeIndex[j->getRoot()] = k->second;
					}
				}
			}
		} //unlock

		string tmpName = getDataFile() + ".tmp";
		string origName = getDataFile();

		createDataFile(tmpName);

		{
			File in(origName, File::READ, File::OPEN | File::SHARED | File::RANDOM_ACCESS);
			File out(tmpName, File::READ | File::WRITE, File::OPEN | File::RANDOM_ACCESS);

			for (TreeIter i = newTreeIndex.begin(); i != newTreeIndex.end();) {
				TigerTree tree;
				bool loaded;
			{
				//check this again, trying to minimize the locking time but locking in a loop, plus allowing shared access to the datafile..
				RLock l(cs);
				loaded = loadTree(in, i->second, i->first, tree);
			}
				if (loaded) {
					i->second.setIndex(saveTree(out, tree));
					++i;
				} else {
					newTreeIndex.erase(i++);
				}
			}
		}
	{
		RLock l(cs);
		for (DirIterC i = fileIndex.begin(); i != fileIndex.end(); ++i) {
			DirIter fi = newFileIndex.insert(make_pair(i->first, FileInfoList())).first;
			for (FileInfoIterC j = i->second.begin(); j != i->second.end(); ++j) {
				if (newTreeIndex.find(j->getRoot()) != newTreeIndex.end()) {
					fi->second.push_back(*j);
				}
			}
			if (fi->second.empty())
				newFileIndex.erase(fi);
		}
	}//unlock
	
		{
			WLock l(cs); //need to lock, we wont be able to rename if the file is in use...
			File::deleteFile(origName);
			File::renameFile(tmpName, origName);
			treeIndex = newTreeIndex;
			fileIndex = newFileIndex;
		}

		dirty = true;
		save();
	} catch (const Exception& e) {
		LogManager::getInstance()->message(STRING(HASHING_FAILED) + " " + e.getError(), LogManager::LOG_ERROR);
	}
}

void HashManager::HashStore::save() {
	if (dirty) {
		try {
			File ff(getIndexFile() + ".tmp", File::WRITE, File::CREATE | File::TRUNCATE, false);
			BufferedOutputStream<false> f(&ff);

			string tmp;
			string b32tmp;

			f.write(SimpleXML::utf8Header);
			f.write(LIT("<HashStore Version=\"" HASH_FILE_VERSION_STRING "\">\r\n"));

			f.write(LIT("\t<Trees>\r\n"));
			{
				RLock l(cs);
				for (TreeIterC i = treeIndex.begin(); i != treeIndex.end(); ++i) {
					const TreeInfo& ti = i->second;
					f.write(LIT("\t\t<Hash Type=\"TTH\" Index=\""));
					f.write(Util::toString(ti.getIndex()));
					f.write(LIT("\" BlockSize=\""));
					f.write(Util::toString(ti.getBlockSize()));
					f.write(LIT("\" Size=\""));
					f.write(Util::toString(ti.getSize()));
					f.write(LIT("\" Root=\""));
					b32tmp.clear();
					f.write(i->first.toBase32(b32tmp));
					f.write(LIT("\"/>\r\n"));
				}
		
				f.write(LIT("\t</Trees>\r\n\t<Files>\r\n"));

				for (DirIterC i = fileIndex.begin(); i != fileIndex.end(); ++i) {
					const string& dir = i->first;
					for (FileInfoIterC j = i->second.begin(); j != i->second.end(); ++j) {
						const FileInfo& fi = *j;
						f.write(LIT("\t\t<File Name=\""));
						f.write(SimpleXML::escape(dir + fi.getFileName(), tmp, true));
						f.write(LIT("\" TimeStamp=\""));
						f.write(Util::toString(fi.getTimeStamp()));
						f.write(LIT("\" Root=\""));
						b32tmp.clear();
						f.write(fi.getRoot().toBase32(b32tmp));
						f.write(LIT("\"/>\r\n"));
					}
				}
			} //unlock
			f.write(LIT("\t</Files>\r\n</HashStore>"));
			f.flush();
			ff.close();
			File::deleteFile( getIndexFile());
			File::renameFile(getIndexFile() + ".tmp", getIndexFile());
			dirty = false;
		} catch (const FileException& e) {
			LogManager::getInstance()->message(STRING(ERROR_SAVING_HASH) + " " + e.getError(), LogManager::LOG_ERROR);
		}
	}
}

string HashManager::HashStore::getIndexFile() { return Util::getPath(Util::PATH_USER_CONFIG) + "HashIndex.xml"; }
string HashManager::HashStore::getDataFile() { return Util::getPath(Util::PATH_USER_CONFIG) + "HashData.dat"; }

class HashLoader: public SimpleXMLReader::CallBack {
public:
	HashLoader(HashManager::HashStore& s) : 
		store(s), size(0), timeStamp(0), version(HASH_FILE_VERSION), inTrees(false), inFiles(false), inHashStore(false) {
	}
	void startTag(const string& name, StringPairList& attribs, bool simple);
	void endTag(const string& name, const string& data);
	
private:
	HashManager::HashStore& store;

	string file;
	int64_t size;
	uint32_t timeStamp;
	int version;

	bool inTrees;
	bool inFiles;
	bool inHashStore;
};

void HashManager::HashStore::load() {
	try {
		Util::migrate(getIndexFile());

		HashLoader loader(*this);
		File f(getIndexFile(), File::READ, File::OPEN);
		WLock l(cs);
		SimpleXMLReader(&loader).parse(f);
	} catch (const Exception&) {
		// ...
	}
}

static const string sHashStore = "HashStore";
static const string sversion = "version";		// Oops, v1 was like this
static const string sVersion = "Version";
static const string sTrees = "Trees";
static const string sFiles = "Files";
static const string sFile = "File";
static const string sName = "Name";
static const string sSize = "Size";
static const string sHash = "Hash";
static const string sType = "Type";
static const string sTTH = "TTH";
static const string sIndex = "Index";
static const string sBlockSize = "BlockSize";
static const string sTimeStamp = "TimeStamp";
static const string sRoot = "Root";

void HashLoader::startTag(const string& name, StringPairList& attribs, bool simple) {
	if (!inHashStore && name == sHashStore) {
		version = Util::toInt(getAttrib(attribs, sVersion, 0));
		if (version == 0) {
			version = Util::toInt(getAttrib(attribs, sversion, 0));
		}
		inHashStore = !simple;
	} else if (inHashStore && version == 2) {
		if (inTrees && name == sHash) {
			const string& type = getAttrib(attribs, sType, 0);
			int64_t index = Util::toInt64(getAttrib(attribs, sIndex, 1));
			int64_t blockSize = Util::toInt64(getAttrib(attribs, sBlockSize, 2));
			int64_t size = Util::toInt64(getAttrib(attribs, sSize, 3));
			const string& root = getAttrib(attribs, sRoot, 4);
			if (!root.empty() && type == sTTH && (index >= 8 || index == HashManager::SMALL_TREE) && blockSize >= 1024) {
				store.treeIndex[TTHValue(root)] = HashManager::HashStore::TreeInfo(size, index, blockSize);
			}
		} else if (inFiles && name == sFile) {
			file = getAttrib(attribs, sName, 0);
			timeStamp = Util::toUInt32(getAttrib(attribs, sTimeStamp, 1));
			const string& root = getAttrib(attribs, sRoot, 2);

			if (!file.empty() && size >= 0 && timeStamp > 0 && !root.empty()) {
				string fileLower = Text::toLower(file);
				store.fileIndex[Util::getFilePath(fileLower)].push_back(HashManager::HashStore::FileInfo(Util::getFileName(fileLower), TTHValue(root), timeStamp,
					false));
			}
		} else if (name == sTrees) {
			inTrees = !simple;
		} else if (name == sFiles) {
			inFiles = !simple;
		}
	}
}

void HashLoader::endTag(const string& name, const string&) {
	if (name == sFile) {
		file.clear();
	}
}

HashManager::HashStore::HashStore() :
	dirty(false) {

	Util::migrate(getDataFile());

	if (File::getSize(getDataFile()) <= static_cast<int64_t> (sizeof(int64_t))) {
		try {
			createDataFile( getDataFile());
		} catch (const FileException&) {
			// ?
		}
	}
}

/**
 * Creates the data files for storing hash values.
 * The data file is very simple in its format. The first 8 bytes
 * are filled with an int64_t (little endian) of the next write position
 * in the file counting from the start (so that file can be grown in chunks).
 * We start with a 1 mb file, and then grow it as needed to avoid fragmentation.
 * To find data inside the file, use the corresponding index file.
 * Since file is never deleted, space will eventually be wasted, so a rebuild
 * should occasionally be done.
 */
void HashManager::HashStore::createDataFile(const string& name) {
	try {
		File dat(name, File::WRITE, File::CREATE | File::TRUNCATE);
		dat.setPos(1024 * 1024);
		dat.setEOF();
		dat.setPos(0);
		int64_t start = sizeof(start);
		dat.write(&start, sizeof(start));

	} catch (const FileException& e) {
		LogManager::getInstance()->message(STRING(ERROR_CREATING_HASH_DATA_FILE) + " " + e.getError(), LogManager::LOG_ERROR);
	}
}

void HashManager::Hasher::hashFile(const string& fileName, int64_t size) {
	Lock l(hcs);
	if (w.insert(make_pair(fileName, size)).second) {
			totalBytesLeft += size;
			s.signal();
	}
}

bool HashManager::Hasher::pause() {
	paused = true;
	return paused;
}

void HashManager::Hasher::resume() {
	paused = false;
	t_resume();
}

bool HashManager::Hasher::isPaused() const {
	return paused;
}

void HashManager::Hasher::stopHashing(const string& baseDir) {
	Lock l(hcs);
	for (WorkIter i = w.begin(); i != w.end();) {
		if (strnicmp(baseDir, i->first, baseDir.length()) == 0) {
			totalBytesLeft -= i->second;
			w.erase(i++);
		} else {
			++i;
		}
	}
}

void HashManager::Hasher::getStats(string& curFile, int64_t& bytesLeft, size_t& filesLeft, int64_t& speed) {
	Lock l(hcs);
	curFile = currentFile;
	filesLeft = w.size();
	if (running)
		filesLeft++;
	bytesLeft = totalBytesLeft;
	speed = lastSpeed;
}

void HashManager::Hasher::instantPause() {
	if(paused) {
		t_suspend();
	}
}

int HashManager::Hasher::run() {
	setThreadPriority(Thread::IDLE);

	string fname;
	for(;;) {
		s.wait();
		instantPause(); //suspend the thread...
		if(stop)
			break;

		if(saveData) {
			HashManager::getInstance()->SaveData();
			saveData = false;
			continue;
		}

		if(rebuild) {
			HashManager::getInstance()->doRebuild();
			rebuild = false;
			LogManager::getInstance()->message(STRING(HASH_REBUILT), LogManager::LOG_INFO);
			continue;
		}
		
		{
			Lock l(hcs);
			if(!w.empty()) {
				currentFile = fname = w.begin()->first;
				currentSize = w.begin()->second;
				w.erase(w.begin());
			} else {
				fname.clear();
			}
		}
		running = true;

		if(!fname.empty()) {
			try {
				uint64_t start = GET_TICK();
				File f(fname, File::READ, File::OPEN);
				int64_t size = f.getSize();
				int64_t bs = max(TigerTree::calcBlockSize(size, 10), MIN_BLOCK_SIZE);
				uint64_t timestamp = f.getLastModified();
				int64_t sizeLeft = size;
				TigerTree tt(bs);

				CRC32Filter crc32;
				FileSFVReader sfv(fname);
				CRC32Filter* xcrc32 = 0;

				if (sfv.hasCRC())
					xcrc32 = &crc32;

				uint64_t lastRead = GET_TICK();
 
                FileReader fr(true);
				fr.read(fname, [&](const void* buf, size_t n) -> bool {
					uint64_t now = GET_TICK();
					if(SETTING(MAX_HASH_SPEED)> 0) {
						uint64_t minTime = n * 1000LL / (SETTING(MAX_HASH_SPEED) * 1024LL * 1024LL);
 
						if(lastRead + minTime> now) {
							Thread::sleep(minTime - (now - lastRead));
						}
					lastRead = lastRead + minTime;
					} else {
						lastRead = GET_TICK();
					}
					tt.update(buf, n);
				
				if(xcrc32)
					(*xcrc32)(buf, n);

					sizeLeft -= n;
				{
					Lock l(hcs);
					currentSize = max(static_cast<uint64_t>(currentSize - n), static_cast<uint64_t>(0));
										
					if(totalBytesLeft > 0)
						totalBytesLeft -= n;
					if(now > start)
						lastSpeed = (size - sizeLeft)*1000 / (now -start);

				}

					return !stop;
				});

				f.close();
				tt.finalize();
				uint64_t end = GET_TICK();
				int64_t averageSpeed = 0;
				if(end > start) {
					averageSpeed = size * 1000 / (end - start);
				}

				if(xcrc32 && xcrc32->getValue() != sfv.getCRC()) {
					LogManager::getInstance()->message(STRING(ERROR_HASHING) + fname + ": " + STRING(ERROR_HASHING_CRC32), LogManager::LOG_ERROR);
					HashManager::getInstance()->fire(HashManagerListener::HashFailed(), fname);
				} else {
					HashManager::getInstance()->hashDone(fname, timestamp, tt, averageSpeed, size);
				}
			} catch(const FileException& e) {
				LogManager::getInstance()->message(STRING(ERROR_HASHING) + " " + fname + ": " + e.getError(), LogManager::LOG_ERROR);
				HashManager::getInstance()->fire(HashManagerListener::HashFailed(), fname);
			}
		
		}
		{
			Lock l(hcs);
			currentFile.clear();
			currentSize = 0;
		}
		running = false;
	}		

	return 0;
}

HashManager::HashPauser::HashPauser() {
	resume = !HashManager::getInstance()->isHashingPaused();
	HashManager::getInstance()->pauseHashing();
}

HashManager::HashPauser::~HashPauser() {
	if(resume)
		HashManager::getInstance()->resumeHashing();
}

bool HashManager::pauseHashing() {
	//Lock l(cs);
	return hasher.pause();
}

void HashManager::resumeHashing() {
	//Lock l(cs);
	hasher.resume();
}

bool HashManager::isHashingPaused() const {
	//Lock l(cs);
	return hasher.isPaused();
}

} // namespace dcpp

/**
 * @file
 * $Id: HashManager.cpp 568 2011-07-24 18:28:43Z bigmuscle $
 */
