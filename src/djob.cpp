#include <Poco/NumberParser.h>

#include "disthc.h"
#include "djob.h"
#include "dtalk.h"

DJob::DJob()
{
	_running = false;
	_chunk = 0;
	_ceiling = 0;
	_maskMin = 0;
	_maskMax = 0;
}

DJob* DJob::Instance()
{
	if (!_pInstance)   // Only allow one instance of class to be generated.
		_pInstance = new DJob;
	return _pInstance;
}

bool DJob::start()
{
	_running = true;
	return true;
	
	// more checks needed?
	if(pool.slavesAvailable())
	{
		
		return true;
	}
	return false;
}

bool DJob::stop()
{
	if(isRunning())
	{
		_running = false;
		return true;
	}
	return false;
}

bool DJob::isRunning()
{
	return (bool) _running;
}

void DJob::msgConsoles(string msg)
{
	pool.sendMessage(NODE_CONIO, msg);
	return;
}

void DJob::msgSlave(StreamSocket socket, string msg)
{
	//dTalk talk(socket);
	//talk.rpc(DCODE_PRINT, msg);
	return;
}

bool DJob::slavesAvailable()
{
	return pool.slavesAvailable() ? true : false;
}

unsigned long DJob::showChunk()
{
	return _chunk;
}

unsigned long DJob::getChunk()
{
	unsigned long chunk = _chunk;
	_chunk += _chunkSize;
	return chunk;
}

unsigned long DJob::getChunk(unsigned int chunkSize)
{
	unsigned long chunk = _chunk;
	_chunk += chunkSize;
	return chunk;
}

void DJob::setChunk(unsigned int chunk)
{
	_chunk = chunk;
}

unsigned int DJob::getChunkSize()
{
	return _chunkSize;
}

void DJob::setChunkSize(unsigned int chunkSize)
{
	_chunkSize = chunkSize;
}

int DJob::getAttackMode()
{
	return (int) _attack;
}

void DJob::setAttackMode(int mode)
{
	_attack = mode;
}

int DJob::getHashType()
{
	return (int) _mode;
}

void DJob::setHashType(int mode)
{
	_mode = mode;
}

unsigned int DJob::getHashCount()
{
	return _count;
}

void DJob::setHashCount(unsigned int count)
{
	_count = count;
}

std::string DJob::getHashFile()
{
	return (std::string) _hashes;
}

void DJob::setHashFile(std::string file)
{
	_hashes = file;
}

std::string DJob::getDictionary()
{
	return (std::string) _dict;
}

void DJob::setDictionary(std::string dictionary)
{
	_dict = dictionary;
}

std::string DJob::getDb()
{
	return (std::string) _db;
}

void DJob::setDb(std::string db)
{
	_db = db;
}

std::string DJob::getRules()
{
	return (std::string) _rules;
}

void DJob::setRules(std::string rules)
{
	_rules = rules;
}

std::string DJob::getMask()
{
	return (std::string) _mask;
}

bool DJob::setMask(std::string mask)
{
	if(_attack == 3)
	{
		// reset ceiling
		_ceiling = 1;

		// calculate ceiling
		for (int i=0; i< mask.length(); i+=2)
		{
			if(mask[i] != '?')
			{
				_ceiling = 0;
				return false;
			}
			switch(mask[i+1])
			{
				case 'l':
					_ceiling *= 26;
					break;
				case 'u':
					_ceiling *= 26;
					break;
				case 'd':
					_ceiling *= 10;
					break;
				case 's':
					_ceiling *= 32;
					break;
				case 'a':
					_ceiling *= (26 + 26 + 10 + 32);
					break;
				default:
					_ceiling = 0;
					return false;
			}
		}
	}
	_mask = mask;
	return true;
}

int DJob::getMaskMin()
{
	return _maskMin;
}

bool DJob::setMaskMin(int min)
{
	if(min < 0) min = 0;
	_maskMin = min;
	return true;
}

int DJob::getMaskMax()
{
	return _maskMax;
}

bool DJob::setMaskMax(int max)
{
	if(max < 0) max = 0;
	_maskMax = max;
	return true;
}

std::string DJob::getPot()
{
	return (std::string) _pot;
}

void DJob::setPot(std::string pot)
{
	_pot = pot;
}

DJob* DJob::_pInstance = NULL; // this MUST be set for Instance() to recognize the reference


// ************************************************************************** //
// Client Pool
// ************************************************************************** //
bool ClientPool::registerClient(StreamSocket socket, int node)
{
	ClientNode cn;
	cn.socket = socket;
	cn.type = node;
	
	if(node == NODE_SLAVE)
	{
		_slaves.push_back(cn);
		_chunkMap.push_back(DEFAULT_CHUNK_SIZE);
	} else
		//_conio.push_back(socket);
		_conio.push_back(cn);
	return true;
}

int ClientPool::registerClient(StreamSocket socket, int node, string clientString, string clientToken)
{
	// create client node object
	ClientNode cn;
	cn.socket = socket;
	cn.type = node;
	
	StringTokenizer cs(clientString," "); //parse client string
	// add client information to node
	cn.name = cs[0];
	cn.os = cs[1];
	cn.osVersion = cs[2];
	cn.cpu = NumberParser::parseUnsigned(cs[3]);
	cn.mac = cs[4];
	cn.arch = cs[5];
	cn.token = clientToken;
	cn.id = 0;
	
	// add to queue
	if(node == NODE_SLAVE)
	{
		_slaves.push_back(cn);
		_chunkMap.push_back(DEFAULT_CHUNK_SIZE);
		return _slaves.size()-1;
	} else {
		//_conio.push_back(socket);
		_conio.push_back(cn);
		return _conio.size()-1;
	}
}

bool ClientPool::unregisterClient(StreamSocket socket, int node)
{
	deque<ClientNode> *q;
	if(node == NODE_SLAVE)
		q = &_slaves;
	else
		q = &_conio;

	for(int i=0; i<q->size();i++)
	{
		if((*q)[i].socket == socket) {
			//(*q).erase((*q).begin()+i, (*q).begin()+i+1);
			(*q).erase((*q).begin()+i);
			if(node == NODE_SLAVE)
			{
				_chunkMap.erase(_chunkMap.begin()+i);
			}
			break;
		}
	}
	unready(socket);
	
	return true;
}

int ClientPool::count(int node)
{
	if(node == NODE_SLAVE)
		return _slaves.size();
	else
		return _conio.size();
}

int ClientPool::count()
{
	return _slaves.size() + _conio.size();
}

bool ClientPool::slavesAvailable()
{
	return _slaves.size() ? true : false;
}

void ClientPool::sendMessage(int node, string msg)
{
	dTalk *talk;
	deque<ClientNode> *q;
	if(node == NODE_SLAVE)
		q = &_slaves;
	else
		q = &_conio;

	for(int i=0; i<q->size();i++)
	{
		talk = new dTalk((*q)[i].socket);
		talk->rpc(DCODE_PRINT, msg);
		delete talk;
	}
}

bool ClientPool::ready(StreamSocket socket)
{
	// make sure socket isn't already ready
	for(int i=0; i<_ready.size(); i++) {
		if(_ready[i] == socket) return true;
	}
	// note the ready socket
	_ready.push_back(socket);
	return true;
}

bool ClientPool::unready(StreamSocket socket)
{
	for(int i=0; i<_ready.size(); i++)
	{
		if(_ready[i] == socket)
		{
			_ready.erase(_ready.begin()+i);
			return true;
		}
	}
	return false;
}

ClientNode* ClientPool::get(unsigned int index, int node)
{
	deque<ClientNode> *q;
	if(node == NODE_SLAVE)
		q = &_slaves;
	else
		q = &_conio;
	
	return &((*q)[index]);
}
StreamSocket* ClientPool::getSlave()
{
	if(DEBUG) {
		Application& app = Application::instance();
		app.logger().information(format("%%Slaves ready: %d", (int) _ready.size()));
	}
	if (_ready.size()>0)
	{
		StreamSocket *s;
		s = &(_ready.front());
		//_ready.pop_front();
		return s;
	}
	return NULL;
}

void ClientPool::sendParam(std::string key, std::string value)
{
	dTalk *talk;
	deque<ClientNode> *q;
	q = &_slaves;

	for(int i=0; i<q->size();i++)
	{
		talk = new dTalk((*q)[i].socket);
		talk->rpc(DCODE_SET_PARAM, format("%s:%s", key, value));
		delete talk;
	}
}

void ClientPool::sendFile(std::string filename, std::string *content)
{
	dTalk *talk;
	deque<ClientNode> *q;
	q = &_slaves;

	for(int i=0; i<q->size();i++)
	{
		talk = new dTalk((*q)[i].socket);
		talk->send_text_as_file(filename, *content);
		delete talk;
	}
}

void ClientPool::zap(std::string hashes)
{
	if(hashes.empty()) return;
	if(DEBUG) {
		Application& app = Application::instance();
		app.logger().information("%Zap!");
	}
	dTalk *talk;
	deque<ClientNode> *q;
	q = &_slaves;

	for(int i=0; i<q->size();i++)
	{
		talk = new dTalk((*q)[i].socket);
		talk->rpc(DCODE_ZAP, hashes);
		delete talk;
	}
}

void ClientPool::setChunkSize(StreamSocket socket, unsigned int chunkSize)
{
	deque<ClientNode> *q;
	q = &_slaves;
	for(int i=0; i<q->size();i++)
	{
		if((*q)[i].socket == socket) {
			_chunkMap[i] = chunkSize;
			break;
		}
	}
}

unsigned int ClientPool::getChunkSize(StreamSocket socket)
{
	deque<ClientNode> *q;
	q = &_slaves;
	for(int i=0; i<q->size();i++)
	{
		if((*q)[i].socket == socket) {
			return _chunkMap[i];
			break;
		}
	}
	return 0; // chunk size should NEVER be zero
}

void ClientPool::blacklist(int node_id)
{

	deque<ClientNode> *q;
	for(int loop=1; loop<=2; loop++)
	{
		if(loop == 1) q = &_slaves;
		if(loop == 2) q = &_conio;
		for(int i=0; i<q->size();i++)
		{
			if((*q)[i].id == node_id) {
				// disconnect node
				(*q)[i].socket.close();
			}
		}
	}
}

ClientPool pool;