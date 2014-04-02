#include <string>

class Engine
{
public:
	Engine() {}
	
	~Engine() {}
	
	virtual int			run() { return 0; }
	virtual string		results() { return string(""); }
	virtual bool		isGhost() { return false; }
	virtual void		zapHashes(string) { return; }
	//virtual void		setConfig(string, string);
	
	string			getName();
	void			setName(string);
	int				getAttackMode();
	void			setAttackMode(int);
	int				getHashType();
	void			setHashType(int);
	string			getMask();
	void			setMask(string);
	string			getRules();
	void			setRules(string);
	string			getDictionary();
	void			setDictionary(string);
	string			getBinaryPath(string);
	void			setBinaryPath(string, string);
	string			getPot();
	void			setPot(string);
	string			getHashFile();
	void			setHashFile(string);
	string			getFlags();
	void			setFlags(string);
	string			getConfig(string);
	void			setConfig(string, string);
	
	bool			isRunnable();
	bool			remoteSync();
	void			remoteSync(bool);
	
	
protected:
	std::map<string, string> _map;
	std::map<string, string> _cfg;
	string _engine;
	
	int				_attack;
	int				_mode;
	int				_chunkSize;
	unsigned long	_chunk;
	string			_dict; // dictionary file
	string			_hashes; // hashlist to be used for attack
	string			_rules; // rules file
	string			_mask; // mask to use
	string			_pot; // pot file
	string			_binaryPath;
	string			_flags; // additional parameters for hashcat
	string			_results;
	bool			_sync;
};

extern Engine *dengine;