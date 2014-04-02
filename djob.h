#ifndef DJOB_H
#define DJOB_H

#include <tgmath.h>

using std::string;

class DJob
{
public:
	static DJob*	Instance();
	unsigned int	getChunkSize();
	void			setChunkSize(unsigned int);
	unsigned long	showChunk();			// get chunk, but do not advance position
	unsigned long	getChunk();				// get chunk and advance position
	unsigned long	getChunk(unsigned int);	// get chunk and advance position
	void			setChunk(unsigned int); // set offset for chunk (on slaves)
	int				getAttackMode();
	void			setAttackMode(int);
	int				getHashType();
	void			setHashType(int);
	string			getMask();
	bool			setMask(string);
	string			getRules();
	void			setRules(string);
	string			getDictionary();
	void			setDictionary(string);
	string			getDb();
	void			setDb(string);
	string			getPot();
	void			setPot(string);
	unsigned int	getHashCount();
	void			setHashCount(unsigned int);
	string			getHashFile();
	void			setHashFile(string);
	string			getBinaryPath();
	void			setBinaryPath(string);
	bool			slavesAvailable();
	bool			isRunning();
	void			zapHashes(string);
	void			msgConsoles(string);
	void			msgSlave(StreamSocket, string);
	bool			start();
	bool			stop();
	bool			ready(StreamSocket); // set a slave in the ready queue
	bool			unready(StreamSocket); // remove a slave from the ready queue

protected:
					DJob();
					~DJob();
	static DJob* 	_pInstance;
	int				_attack;
	int				_mode;
	unsigned int	_chunkSize;
	unsigned long	_chunk;
	string			_dict; // dictionary file
	string			_hashes; // hashlist to be used for attack
	string			_rules; // rules file
	string			_mask; // mask to use
	string			_pot; // pot file
	string			_db; // disthc master db file
	unsigned long long _ceiling;
	unsigned int	_count; // number of hashes
	
	// Master-specific properties
	bool			_running;
	deque<StreamSocket>	_ready; // queue for slaves ready to process
};

// Client node object for use in the client pool
class ClientNode
{
public:
	StreamSocket	socket;
	int				type;	// client node type (NODE_SLAVE|NODE_CONIO)
	string			os;
	//unsigned int	chunk; // TODO replace chunkMap with this param
};


// Client nodes will be registered in the Client Pool.
// Communications and stats to nodes will be tunneled through
// this object
class ClientPool
{
private:
	deque<StreamSocket>	_conio;  // registered console clients
	deque<StreamSocket>	_slaves; // registered slave clients
	deque<StreamSocket>	_ready; // slaves available for processing
	deque<unsigned int> _chunkMap;
	
public:
	bool			registerClient(StreamSocket, int node_type);
	bool			unregisterClient(StreamSocket, int node_type);
	int				count();
	int				count(int type);
	StreamSocket*	get(int index);
	bool			slavesAvailable();
	void			sendMessage(int, std::string);
	void			sendParam(string, string);
	void			sendFile(string, string*);
	void			sendFile(string);
	bool			ready(StreamSocket); // set a slave in the ready queue
	bool			unready(StreamSocket); // remove a slave from the ready queue
	StreamSocket*	getSlave(); // get first available slave in the ready queue
	void			zap(string);
	void			setChunkSize(StreamSocket, unsigned int); // sets the chunk size for a slave node
	unsigned int	getChunkSize(StreamSocket); // get the chunk size for a slave node
};

extern ClientPool pool;

#endif // DJOB_H