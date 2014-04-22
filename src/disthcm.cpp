/*
	application: disthcm
	description: distributed hash-cracking master server
	written by Unix-Ninja
	May 10, 2013
*/
//#define DROLE_MASTER
#include "disthc.h"
#include "djob.h"
#include "dtalk.h"

#include "disthcm.h"

#include <Poco/Data/Common.h>
#include <Poco/Data/RecordSet.h>
#include <Poco/Data/SQLite/Connector.h>
#include <Poco/Data/SQLite/SQLiteException.h>
#include <Poco/Crypto/OpenSSLInitializer.h>
#include <Poco/Crypto/DigestEngine.h>
#include <Poco/Tuple.h>

using Poco::Data::into;
using Poco::Data::now;
using Poco::Data::range;
using Poco::Data::use;
using Poco::Crypto::DigestEngine;

// ************************************************************************** //
// Local globals
// ************************************************************************** //
bool DEBUG;
std::string authToken;
bool showResults;
bool showDcode;
bool dbRainbowOn;
Poco::Data::Session* db;


// ************************************************************************** //
// function to load hashes into db
// ************************************************************************** //

bool sendHashes()
{
	// sends to all slaves
	Application& app = Application::instance();
	DJob *job = DJob::Instance();
	string hash;
	string salt;
	string dump;
	Poco::Data::Statement select(*db);
	
	app.logger().information("Sending hashes...");
	select << "SELECT hash, salt FROM job_queue", into(hash), into(salt), range(0,1);
	while(!select.done())
	{
		select.execute();
		if(!salt.empty())
		{
			dump += string(hash + ":" + salt + "\n");
		}
		else
		{
			dump += string(hash + "\n");
		}
	}
	pool.sendFile(job->getHashFile(), &dump);
	return true;
}

bool sendHashes(StreamSocket socket)
{
	// sends to a particular socket
	Application& app = Application::instance();
	DJob *job = DJob::Instance();
	string hash;
	string salt;
	string dump;
	Poco::Data::Statement select(*db);
	
	app.logger().information("Sending hashes...");
	select << "SELECT hash, salt FROM job_queue", into(hash), into(salt), range(0,1);
	while(!select.done())
	{
		select.execute();
		if(!salt.empty())
		{
			dump += string(hash + ":" + salt + "\n");
		}
		else
		{
			dump += string(hash + "\n");
		}
	}
	dTalk *talk = new dTalk(socket);
	talk->send_text_as_file(job->getHashFile(), dump);
	return true;
}

bool loadHashes()
{
	Application& app = Application::instance();
	DJob *job = DJob::Instance();
	int count;
	int total(0);
	string line;
	string hash;
	string salt;
	
	//check for hash file
	File f(job->getHashFile());
	if(!f.exists()) return false;
	
	// open hash file
	FileInputStream fis(job->getHashFile());
	
	// clear job_queue
	app.logger().information("Cleaning job queue from db...");
	*db << "DELETE FROM job_queue", now;
	
	app.logger().information("Loading hashes into db...");
	//loop through hash file and add relevant hashes to job queue
	while(fis >> line) {
		// TODO parse for different hash types and salts
		StringTokenizer t(line,":");
		hash = t[0];
		salt = "";
		*db << "SELECT COUNT(hash) FROM rainbow WHERE hash=? AND salt=?", use(hash), use(salt), into(count), now;
		if(!count)
		{
			*db << "INSERT INTO job_queue (hash, salt) VALUES (?, ?)", use(hash), use(salt), now;
			total++;
		}
	}
	app.logger().information(format("  Added %d hashes.", total));
	job->setHashCount((unsigned int) total);
	return true;
}


// ************************************************************************** //
// Service handle for main app
// ************************************************************************** //
class DistServiceHandler
{
private:
	static const int BUFFER_SIZE = 1024;

	StreamSocket _socket;
	SocketReactor& _reactor;
	char* _pBuffer;
	string _authToken;
	string _clientType;
	dTalk _talk;
	long int _chunkSize;

public:
	DistServiceHandler(StreamSocket& socket, SocketReactor& reactor):
		_socket(socket),
		_reactor(reactor),
		_talk(_socket),
		_pBuffer(new char[BUFFER_SIZE])
	{
		Application& app = Application::instance();
		// Log a connection
		app.logger().information("+Node " + socket.peerAddress().toString());

		_reactor.addEventHandler(
			_socket,
			NObserver<DistServiceHandler,
			ReadableNotification>(*this, &DistServiceHandler::onReadable)
		);

		_reactor.addEventHandler(
			_socket,
			NObserver<DistServiceHandler,
			ShutdownNotification>(*this, &DistServiceHandler::onShutdown)
		);

		// Set default chunk size
		_chunkSize = DEFAULT_CHUNK_SIZE;

		// Send hello
		_talk.rpc(DCODE_HELO, "Hi");

	}

	~DistServiceHandler()
	{
		Application& app = Application::instance();
		try
		{
			// Unregister this client
			if(_clientType == "slave")
				pool.unregisterClient(_socket, NODE_SLAVE);
			else
				pool.unregisterClient(_socket, NODE_CONIO);

			// Log a disconnection
			app.logger().information("-Node " + _socket.peerAddress().toString());
			if(DEBUG) app.logger().information(format("%%Remaining slaves: %d", pool.count(NODE_SLAVE)));
		}
		catch (...) { }

		_reactor.removeEventHandler(
			_socket,
			NObserver<DistServiceHandler,
			ReadableNotification>(*this, &DistServiceHandler::onReadable)
		);

		_reactor.removeEventHandler(
			_socket,
			NObserver<DistServiceHandler,
			ShutdownNotification>(*this, &DistServiceHandler::onShutdown)
		);

		delete _pBuffer;
	}

	void onReadable(const AutoPtr<ReadableNotification>& pNf)
	{
		//receive data
		if (_talk.receive())
		{
			Application& app = Application::instance();
			DJob *job = DJob::Instance();
			
			if(DEBUG && showDcode) app.logger().information(format("%%DCODE(%d)",_talk.dcode()));
			if(!_authToken.empty())
			{
				if(_talk.dcode() == DCODE_RPC)
				{
					//TODO maybe client types should be type int?
					if(_clientType == "slave")
					{
						//slave is probably unsafe at this point. remove from pool
						pool.unregisterClient(_socket, NODE_SLAVE);
						return;
					}
					process_rpc();
				}
				else if(_talk.dcode() == DCODE_HOTKEY)
				{
					if(DEBUG) app.logger().information("%Hotkey triggered.");
					if (_talk.data().substr(0,1) == "\t")
					{
						_talk.rpc(DCODE_HOTKEY, tab_complete(_talk.data().substr(1)));
					}
				}
				else if(_talk.dcode() == DCODE_GET_CHUNK)
				{
					if(DEBUG) app.logger().information("%Chunk requested from " + _socket.peerAddress().toString());
				}
				else if(_talk.dcode() == DCODE_RESULTS)
				{
					if(DEBUG) app.logger().information("%Results received from " + _socket.peerAddress().toString());
					
					string results = _talk.data();		
					if(!results.empty())
					{
						if (showResults)
						{
							app.logger().information("==" + results);
						}
						StringTokenizer t(results,":");
						string hash = t[0];
						string salt = "";
						string plain = t[1];
						if(t.count()==3)
						{
							salt = t[1];
							plain = t[2];
						}
						*db << "INSERT INTO rainbow (hash, salt, plain) VALUES (?, ?, ?)", use(hash), use(salt), use(plain), now;
						*db << "DELETE FROM job_queue WHERE hash=? AND salt=?", use(hash), use(salt), now;
						job->setHashCount(job->getHashCount()-1); // decrement remaining hashes
						pool.zap(results);
						
						// check if all hashes cracked and (if so) stop job
						unsigned int count;
						*db << "SELECT COUNT(hash) FROM job_queue", into(count), now;
						if(!count)
						{
							job->stop();
							job->msgConsoles("All hashes have been found! Job stopped.");
						}
					}
					pool.ready(_socket);
				}
				else if(_talk.dcode() == DCODE_READY)
				{
					if(DEBUG) app.logger().information("%Client " + _socket.peerAddress().toString() + " is ready.");
					pool.ready(_socket);
				}
			}
			else
			{
				if(authorize())
				{
					// retreive client identity
					_talk.receive();
					string clientString = _talk.data();
					DigestEngine de("SHA256");
					de.update(clientString);
					string clientToken = DigestEngine::digestToHex(de.digest());
					string blacklist = "n";
					
					// register the client
					int node_t = NODE_SLAVE;
					
					if(_clientType == "conio")
					{
						node_t = NODE_CONIO;
					}
					
					// add client to pool
					int id = pool.registerClient(_socket, node_t, clientString, clientToken);
					ClientNode* node = pool.get(id, node_t);
					
					// is client in db?
					*db << "SELECT id, blacklist FROM clients WHERE token=?", use(clientToken), into(id), into(blacklist), now;
					if(!id)
					{
						// add client to db
						*db << "INSERT INTO clients (token, type, name, os, os_version, arch, cpu, mac, address, last_seen) VALUES (?, 'slave', ?, ?, ?, ?, ?, ?, ?, date('now'))", use(clientToken), use(node->name), use(node->os), use(node->osVersion), use(node->arch), use(node->cpu), use(node->mac), use(node->socket.peerAddress().toString()), now;
						*db << "SELECT id FROM clients WHERE token=?", use(clientToken), into(id), now;
					}
					
					// check for blacklist
					if (blacklist == "y")
					{
						// remove from pool
						pool.unregisterClient(_socket, node_t);
						// don't allow connect
						_talk.rpc(DCODE_PRINT, "Unknown Error");
						app.logger().information(format("|Client in blacklist (%s)", id));
						delete this;
						return;
					}

					// assign id to node object
					node->id = id;
						
					// additional setup/sync for slaves
					if(_clientType == "slave")
					{
						// set all params
						_talk.receive();
						if(_talk.data().substr(0,2) == "C:")
						{
							// set chunkSize if available
							unsigned int cs;
							if(NumberParser::tryParseUnsigned(_talk.data().substr(2), cs))
							{
								if (cs > DEFAULT_CHUNK_SIZE)
									pool.setChunkSize(_socket, cs);
							}
						}
						_talk.rpc(DCODE_SET_PARAM, format(string(PARAM_ATTACK)+":%d", job->getAttackMode()));
						_talk.rpc(DCODE_SET_PARAM, format(string(PARAM_MODE)+":%d", job->getHashType()));
						_talk.rpc(DCODE_SET_PARAM, format(string(PARAM_MASK)+":%s", job->getMask()));
						_talk.rpc(DCODE_SET_PARAM, format(string(PARAM_RULES)+":%s", job->getRules()));
						_talk.rpc(DCODE_SET_PARAM, format(string(PARAM_DICT)+":%s", job->getDictionary()));
						_talk.rpc(DCODE_SET_PARAM, format(string(PARAM_HASHES)+":%s", job->getHashFile()));
						
						_talk.receive();
						if(_talk.dcode() == DCODE_SYNC)
						{
							sync();
						}
						sendHashes(_socket); // sync hashes with just this client
					}
//					else
//					{
//						pool.registerClient(_socket, NODE_CONIO, clientString, clientToken);
//					}
					
					_talk.rpc(DCODE_READY);
				}
				else delete this;
			}
		}
		else delete this;
	}

	void onShutdown(const AutoPtr<ShutdownNotification>& pNf)
	{
		delete this;
	}
	
	void sync()
	{
		Application& app = Application::instance();
		DJob *job = DJob::Instance();
		
		if(!job->getDictionary().empty() && !_talk.send_file(job->getDictionary()))
		{
			app.logger().information(format("|Unable to transfer file: %s",job->getDictionary()));
		}
		
		if(!job->getRules().empty() && !_talk.send_file(job->getRules()))
		{
			app.logger().information(format("|Unable to transfer file: %s",job->getRules()));
		}
	}

	bool authorize()
	{
		Application& app = Application::instance();
		// Make sure a helo is sent, or die
		if(_talk.dcode() != DCODE_HELO)
		{
			app.logger().information("|Protocol error!");
			_talk.rpc(DCODE_PRINT, "Unknown error!\n");
			return false;
		}

		// Make sure versions match, or die
		StringTokenizer t(_talk.data(),":"); //parse client introduction
		if(NumberParser::parse(t[2]) != APP_VERSION)
		{
			Application& app = Application::instance();
			app.logger().information(format("|Invalid client version: %d", NumberParser::parse(t[2])));
			_talk.receive(); // clear receive stream before sending DCODE
			_talk.rpc(DCODE_PRINT, "Invalid cient version!\n");
			return false;
		}
		
		// Make sure is authorized, or die
		_talk.receive();
		if(_talk.data() != authToken)
		{
			app.logger().information("|Invalid auth token");
			_talk.rpc(DCODE_PRINT, "Invalid auth token!\n");
			return false;
		}

		_authToken = _talk.data();
		_clientType = t[1];
		return true;
	}
	
	string expand_rpc(string rpc)
	{
		StringTokenizer rpct(rpc, " ");
		vector<string> cmd_map;
		
		for (int i=0; i<sizeof(cmap) / sizeof(string); i++)
		{
			if(cmap[i].find(rpc,0) == 0)
			{
				StringTokenizer mapt(cmap[i], " ");
				if(mapt[mapt.count()-1].find(rpct[rpct.count()-1],0) == 0)
				{
					cmd_map.push_back(mapt[mapt.count()-1]);
				}
			}
		}
		
		if(cmd_map.size() > 1) return " ";
		if(cmd_map.size() == 1) return cmd_map[0];
		return rpct[rpct.count()-1];
	}
	  
	string tab_complete(string needle)
	{
		bool listmode = false;
		vector<string> cmd_map;
		string map;
		string prefix;
		
		// detect tab complete when no chars have been entered for param
		if(needle == "" || needle.substr(needle.length()-1) == " ")
		{
			listmode = true;
		}
		
		{ // scope needle tokens and expand before offering tab complete
			StringTokenizer rpct(needle, " ");
			if(rpct.count() > 1)
			{
				for(int i=0; i<rpct.count()-1; i++)
				{
					if(prefix.length()) prefix += " ";
					prefix += expand_rpc(prefix + rpct[i]);
				}
			}

			// update needle with expanded prefixes
			if(prefix.length()) prefix += " ";
			needle = prefix + rpct[rpct.count()-1];
		}
		
		// let's use a new Token for updated needle
		StringTokenizer rpct(needle, " ");
		
		// now we can perform the mappings
		for (int i=0; i<sizeof(cmap) / sizeof(string); i++)
		{
			if(cmap[i].find(needle,0) == 0)
			{
				StringTokenizer mapt(cmap[i], " ");
				if(listmode)
				{
					// only grab matches for the next sublevel
					if(mapt.count() == rpct.count()+1)
					{
						cmd_map.push_back(mapt[mapt.count()-1]);
					}
				}
				else
				{
					if(mapt[mapt.count()-1].find(rpct[rpct.count()-1],0) == 0)
					{
						cmd_map.push_back(mapt[mapt.count()-1]);
					}
				}
			}
		}
		
		// return completion only the suffix if found
		if(cmd_map.size() == 1 && !listmode)
		{
			return "\t" + cmd_map[0].substr(rpct[rpct.count()-1].length()) + " ";
		}
		
		// return a list if ambiguous
		if(cmd_map.size() > 1 || (listmode && cmd_map.size() > 0))
		{
			map = " \n";
			for (int i=0; i<cmd_map.size(); i++)
			{
				map += cmd_map[i] + "\n";
			}
			map += format("%c", 4);
			return map;
		}
		
		// return nothing if no mappings
		return "";
	}

	void process_rpc()
	{
		Application& app = Application::instance();
		StringTokenizer t(_talk.data()," ", StringTokenizer::TOK_IGNORE_EMPTY | StringTokenizer::TOK_TRIM);
		vector<string> param;
		string rpc;
		string cmd_tree = "";

		// expand all params before processing
		for(int i=0; i< t.count(); i++)
		{
			string cmd_branch;
			if(cmd_tree.length())
			{
				cmd_branch = expand_rpc(cmd_tree + " " + t[i]);
			}
			else
			{
				cmd_branch = expand_rpc(t[i]);
			}
			
			// expanded params will be whitespace if ambiguous
			if(cmd_branch == " ")
			{
				if(DEBUG) app.logger().information(format("|Ambiguous command: %s", t[i]));
				_talk.rpc(DCODE_PRINT, "Ambiguous command.\n");
				return;
			}
			
			// pad tree string
			if(cmd_tree.length()) cmd_tree += " ";
			
			// only apply branch if a match is found
			if(cmd_branch.length())
			{
				param.push_back(cmd_branch);
				cmd_tree += cmd_branch;
			} else {
				param.push_back(t[i]);
				cmd_tree += t[i];
			}
			
		}
		
		// alias for rpc path
		rpc = param[0];
		
		// Set pointer for DJob
		DJob *job = DJob::Instance();
		
		if(rpc == "attack")
		{
			if(param.size()>1)
			{
				job->setAttackMode(NumberParser::parse(param[1].c_str()));
				_talk.rpc(DCODE_READY); // send to console
				pool.sendParam(PARAM_ATTACK, format("%d", job->getAttackMode())); // send to slaves
				if(DEBUG)
				{
					app.logger().information(format("%%Setting attack mode to %s", param[1]));
				}
			} else {
				
			_talk.rpc(DCODE_PRINT, format("Attack mode: %d\n", job->getAttackMode()));
			}
		}
		else if(rpc == "chunk")
		{
			if(param.size()>1)
			{
				//job->setChunk(NumberParser::parseUnsigned(param[1]));
				if(param[1] == "reset")
				{
					if(DEBUG)
					{
						app.logger().information("%Resetting chunk to 0...");
					}
					_talk.rpc(DCODE_PRINT, "Resetting chunk to 0...");
					job->setChunk(0);
				}
			} else {
				_talk.rpc(DCODE_PRINT, format("Chunk: %lu\n", job->showChunk()));
			}
		}
		else if(rpc == "clients")
		{
			if(param.size()>1)
			{
				int node_t = 0;
				int pi = 1; // set the param index to check
				
				if(param[1] == "blacklist" )
				{
					if(param[2].substr(0,1) == "-")
					{
						int node_id = Poco::NumberParser::parse(param[2].substr(1));
						*db << "UPDATE clients SET blacklist='n' WHERE id=?", use(node_id), now;
						_talk.rpc(DCODE_PRINT, format("Unblacklisted node %d", node_id));
					} else {
						int node_id = Poco::NumberParser::parse(param[2]);
						pool.blacklist(node_id);
						*db << "UPDATE clients SET blacklist='y' WHERE id=?", use(node_id), now;
						_talk.rpc(DCODE_PRINT, format("Blacklisted node %d", node_id));
					}
					return;
				}
				
				if(param[1] == "details" )
				{
					pi = 2; // advance param index
				}
				if(param.size()>(pi))
				{
					if(param[pi] == "conio" )
					{
						node_t = NODE_CONIO;
					}
					else if (param[pi] == "slave")
					{
						node_t = NODE_SLAVE;
					}
					else
					{
						_talk.rpc(DCODE_PRINT, "Unknown option: " + param[pi]);
						return;
					}
				}
				if(node_t > 0)
				{
					_talk.rpc(DCODE_PRINT, format("Clients: %d",pool.count(node_t)));
				}
				else
				{
					_talk.rpc(DCODE_PRINT, format("Clients: %d",pool.count()));
				}
				if(param[1] == "details" )
				{
					_talk.rpc(DCODE_PRINT, "Client details:\n");
					clientDetails(node_t);
				}
			} else _talk.rpc(DCODE_PRINT, format("Clients: %d",pool.count()));
		}
		else if(rpc == "debug")
		{
			debugCmd(&param);
		}
		else if(rpc == "dictionary")
		{
			if(param.size()>1)
			{
				job->setDictionary(param[1]);
				_talk.rpc(DCODE_READY);
				pool.sendParam(PARAM_DICT, job->getDictionary()); // send to slaves
				if(DEBUG)
				{
					app.logger().information(format("%%Setting dictionary to %s", param[1]));
				}
			} else {
				_talk.rpc(DCODE_PRINT, format("Dictionary: %s\n", job->getDictionary()));
			}
		}
		else if(rpc == "hashes")
		{
			if(param.size()>1)
			{
				if(job->getHashFile() != param[1])
				{
					File f(param[1]);
					if(f.exists())
					{
						_talk.rpc(DCODE_READY);
						if(DEBUG)
						{
							app.logger().information(format("%%Setting hash file to %s", param[1]));
						}
						job->setHashFile(param[1]);
						// TODO probably need to audit some of this code for tansfering files
						pool.sendParam(PARAM_HASHES, job->getHashFile()); // send to slaves
						loadHashes();
						sendHashes();
					} else {
						app.logger().information(format("|Cannot find hash file %s", param[1]));
						_talk.rpc(DCODE_PRINT, format("Cannot find hash file %s", param[1]));
					}
				}
			} else {
				_talk.rpc(DCODE_PRINT, format("Hash File: %s\n", job->getHashFile()));
			}
		}
		else if(rpc == "help")
		{
			if(param.size()>1)
			{
				moreHelp(param[1]);
			} else {
				if(DEBUG) app.logger().debug("%Sending help data...");
				_talk.rpc(DCODE_PRINT, string("Supported commands (type help [command] to get more info):\n") +
					"  attack\n" +
					"  chunk\n" +
					"  clients\n" +
					"  dictionary\n" +
					"  exit\n" +
					"  hashes\n" +
					"  help\n" +
					"  mask\n" +
					"  msg\n" +
					"  mode\n" +
					"  show\n" +
					"  shutdown\n" +
					"  start\n" +
					"  status\n" +
					"  stop\n");
			}
		}
		else if(rpc == "mask")
		{
			if(param.size()>1)
			{
				if(param[1] == "minimum")
				{
					if(param.size() > 2)
					{
						// allow "-" to clear (zero value)
						int val = 0;
						if(param[2] != "-")
						{
							val =NumberParser::parse(param[2]);
						}
						// set value
						job->setMaskMin(val);
						_talk.rpc(DCODE_READY);
					}
					else
					{
						_talk.rpc(DCODE_PRINT, format("Mask minimum: %d\n", job->getMaskMin()));
					}
				}
				else if(param[1] == "maximum")
				{
					if(param.size() > 2)
					{
						// allow "-" to clear (zero value)
						int val = 0;
						if(param[2] != "-")
						{
							val =NumberParser::parse(param[2]);
						}
						// set value
						job->setMaskMax(val);
						_talk.rpc(DCODE_READY);
					}
					else
					{
						_talk.rpc(DCODE_PRINT, format("Mask maximum: %d\n", job->getMaskMax()));
					}
				}
				else
				{
					if(job->setMask(param[1]))
					{
						_talk.rpc(DCODE_READY);
						pool.sendParam(PARAM_MASK, job->getMask()); // send to slaves
						if(DEBUG)
						{
							app.logger().information(format("%%Setting mask to %s", param[1]));
						}
					} else {
						_talk.rpc(DCODE_PRINT, "Unable to set mask!");
						app.logger().information(format("|Unable to set mask: %s", param[1]));
					}
				}
			} else {
				_talk.rpc(DCODE_PRINT, format("Mask: %s\n", job->getMask()));
			}
		}
		else if(rpc == "mode")
		{
			if(param.size()>1)
			{
				job->setHashType(atoi(param[1].c_str()));
				_talk.rpc(DCODE_READY);
				pool.sendParam(PARAM_MODE, format("%d", job->getHashType())); // send to slaves
				if(DEBUG)
				{
					app.logger().information(format("%%Setting hash mode to %s", param[1]));
				}
			} else {
				_talk.rpc(DCODE_PRINT, format("Hash mode: %d\n", job->getHashType()));
			}
		}
		else if(rpc == "msg")
		{
			if(param.size()>1)
			{
				// make sure to grab the leading whitespace
				int mbuf = _talk.data().find(param[0]);
				// send just the message (do not send leading whitespace, 'msg', or trailing space)
				pool.sendMessage(NODE_SLAVE, "*MSG* " + _talk.data().substr(param[0].length()+mbuf+1));	
				_talk.rpc(DCODE_PRINT, "Message sent.\n");
			} else {
				_talk.rpc(DCODE_PRINT, "No message to send.\n");
			}
		}
		else if(rpc == "rules")
		{
			if(param.size()>1)
			{
				job->setRules(param[1]);
				_talk.rpc(DCODE_READY);
				pool.sendParam(PARAM_RULES, job->getRules()); // send to slaves
				if(DEBUG)
				{
					app.logger().information(format("%%Setting rules to %s", param[1]));
				}
			} else {
				_talk.rpc(DCODE_PRINT, format("Rules: %s", job->getRules()));
			}
		}
		else if(rpc == "show")
		{
			if(param.size()>1)
			{
				_talk.rpc(DCODE_PRINT, "Plain text: \n");
				app.logger().information("Showing rainbow results...");
			}
		}
		else if(rpc == "shutdown")
		{
			_talk.rpc(DCODE_PRINT, "Shutting down server...\n");
			app.logger().information("Shutting down...");
			// hard shutdown; this should probably be improved
			exit(0);
		}
		else if(rpc == "start")
		{
			if(job->isRunning())
			{
				_talk.rpc(DCODE_PRINT, "A job is already running.\n");
			} else if (job->getHashCount() == 0) {
				_talk.rpc(DCODE_PRINT, "There are no hashes to process. The job will not start.\n");
			} else {
				_talk.rpc(DCODE_PRINT, "Job starting...\n");
				app.logger().information("Job starting...");
				if(!job->start())
				{
					_talk.rpc(DCODE_PRINT, "Unable to start job.\n");
					app.logger().information("|Unable to start job.");
				}
			}
		}
		else if(rpc == "status")
		{
			string msg;
			string mmin;
			string mmax;
			
			if(job->isRunning()) {
				msg = "A job is currently running.";
			} else {
				msg = "No jobs are running.";
			}
			
			// calculate mask min string
			if(job->getMaskMin())
			{
				mmin = format("%d", job->getMaskMin());
			}
			else
			{
				mmin = "none";
			}
			
			// calculate mask max string
			if(job->getMaskMax())
			{
				mmax = format("%d", job->getMaskMax());
			}
			else
			{
				mmax = "none";
			}
			
			// format can only take 7 args at a time. maybe we should use another method.
			msg = format("%s\n-- Stats --\n  attack: %d\n  mode: %d\n  hashes: %s\n  dictionary: %s\n  mask: %s\n", msg, job->getAttackMode(), job->getHashType(), job->getHashFile(), job->getDictionary(), job->getMask());
			msg = format ("%s  mask min: %s\n  mask max: %s\n  remaining hashes: %u\n", msg, mmin, mmax, job->getHashCount());
			
			// send output
			_talk.rpc(DCODE_PRINT, msg);
		
		}
		else if(rpc == "stop")
		{
			if(job->stop())
			{
				_talk.rpc(DCODE_PRINT, "Job stopping...\n");
				if(DEBUG) app.logger().information("Job stopping...");
			} else {
				_talk.rpc(DCODE_PRINT, "No job running to stop.\n");
			}
		}
		else
		{
			if(DEBUG) app.logger().information(format("|Unknown command: %s", _talk.data()));
			_talk.rpc(DCODE_PRINT, "Unknown command.\n");
		}
	}
	
	void moreHelp(string cmd)
	{
		string msg;
		if (cmd == "attack") {
			msg = (string) "(attack)  use this to view or manipulate hashcat attack mode settings.\n" +
				(string) "    attack        view current attack mode\n"+
				(string) "    attack <int>  set the attack mode to <int>";
		} else if(cmd == "chunk") {
			msg = (string) "(chunk)  view or reset the current chunk position\n" +
				(string) "    chunk         view the current chunk offset\n" +
				(string) "    chunk reset   set chunk position to 0";
		} else if(cmd == "clients") {
			msg = (string) "(clients)  view information about disthc clients\n\n" +
				(string) "           You can specify an optional \"node\" parameter to get more info about\n" +
				(string) "           a node type. For example, 'clients conio' will show info for only the\n" +
				(string) "           connected consoles\n\n" +
				(string) "    clients                 view the number of connected clients\n" +
				(string) "    clients details         view client details for all connected clients\n" +
				(string) "    clients blacklist <id>  add a client with <id> to the blacklist (you can use\n" +
				(string) "                            the negative complement of the <id> ro remove a client\n" +
				(string) "                            from the blacklist)\n";;
		} else if(cmd == "dictionary" || cmd == "dict") {
			msg = (string) "(dictionary)  view or set the current dictionary file\n" +
				(string) "    dictionary             view the current dictionary filename\n" +
				(string) "    dictionary <filename>  set a new dictionary filename to use";
		} else if (cmd == "exit") {
			msg = "(exit)  closes the console client.";
		} else if(cmd == "hashes") {
			msg = (string) "(hashes)  view or set the current hashes file\n" +
				(string) "    hashes             view the current hashes filename\n" +
				(string) "    hashes <filename>  set a new hashes filename to use";
		} else if(cmd == "help") {
			msg = "(help)  prints information on available commands.\n";
		} else if(cmd == "mask") {
			msg = (string) "(mask)  view or manipulate the hashcat mask.\n"+
				(string) "    mask            view the current mask\n"+
				(string) "    mask <string>   set the mask to <string>\n"+
				(string) "    mask min <int>  set the pw-min flag to <int>\n"+
				(string) "    mask max <int>  set the pw-max flag to <int>\n"+
				(string) "    mask -          clear the mask and do not use it in jobs";
		} else if(cmd == "msg") {
			msg = (string) "(msg)  send a message to all slave screens.\n"+
				(string) "    msg <string>   sends <string> to each slave to be printed on the screen";
		} else if(cmd == "mode") {
			msg = (string) "(mode)  use this command to view or manipulate the hash mode.\n"+
				(string) "    mode          view current hash mode\n"+
				(string) "    mode <int>    set the hash mode to <int>";
		} else if(cmd == "rules") {
			msg = (string) "(rules)  view, enable, or disable use of a rules file.\n"+
				(string) "    rules         view the current rules param\n"+
				(string) "    rules on      use rules when processing jobs\n"+
				(string) "    rules -       clear rules (do not use rules in jobs)";
		} else if(cmd == "show") {
			msg = (string) "(show)  view details on the various options.\n"+
				(string) "    show rt <string>    show the plain for <string> if found";
				//(string) "    show pot      view the results in your pot";
		}else if(cmd == "shutdown" || cmd == "shut") {
			msg = "(shutdown)  this will shutdown the server and close all client connections made\n to the server.";
		} else if(cmd == "start") {
			msg = "(start)  use this to start the processing of a job.";
		} else if(cmd == "status") {
			msg = "(status)  use this to view the status of a job.";
		} else if(cmd == "stop") {
			msg = "(stop)  use this to stop the processing of a job.";
		} else {
			msg = "No information is available for '" + cmd + "'";
		}
		_talk.rpc(DCODE_PRINT, msg + "\n");
	}
	
	void debugCmd(vector<string> *param)
	{
		DJob *job = DJob::Instance();
		
		if(param->size() < 2)
		{
			_talk.rpc(DCODE_READY);
			return;
		}
		if((*param)[1] == "womp")
		{
			string ts = (string) (*param)[0] + ": womp!";
			_talk.rpc(DCODE_PRINT, ts + "\n");
		}
		else if((*param)[1] == "ghost")
		{
			if(param->size() > 2 && (*param)[2] == "on")
			{
				pool.sendParam(PARAM_GHOST, "on");
				_talk.rpc(DCODE_PRINT, "Ghost mode enabled.");
				return;
			}
			else if(param->size() > 2 && (*param)[2] == "off")
			{
				pool.sendParam(PARAM_GHOST, "off");
				_talk.rpc(DCODE_PRINT, "Ghost mode disabled.");
				return;
			}
		}
		
		// cleanup, just in-case
		_talk.rpc(DCODE_READY);
		return;
		
	}
	
	void clientDetails(int node_t)
	{
		ClientNode* cn;
		string out;
		
		if (node_t == 0 || node_t == NODE_CONIO)
		{
			for(int i=0; i<pool.count(NODE_CONIO); i++)
			{
				cn = pool.get(i, NODE_CONIO);
				out.append(format("C  [%u] %s %s %s %s  %s\n", cn->id, cn->name, cn->os, cn->osVersion, cn->arch, cn->socket.peerAddress().toString()));
			}
		}
		if (node_t == 0 || node_t == NODE_SLAVE)
		{
			for(int i=0; i<pool.count(NODE_SLAVE); i++)
			{
				cn = pool.get(i, NODE_SLAVE);
				out.append(format("S  [%u] %s %s %s %s  %s\n", cn->id, cn->name, cn->os, cn->osVersion, cn->arch, cn->socket.peerAddress().toString()));
			}
		}
		_talk.rpc(DCODE_PRINT, out);
	}

};

// Disthc Worker class
// This will assign jobs to slaves
class DistWorker : public Poco::Runnable
{
public:
	DistWorker() : _shutdown(false) { }

	virtual void run()
	{
		DJob *job = DJob::Instance();
		Application& app = Application::instance();
		StreamSocket *socket;
		dTalk *talk;
		unsigned long chunk;

		while(_shutdown == false)
		{
			Thread::sleep(1000);
			if((bool) job->isRunning())
			{
				// get next available slave
				socket = pool.getSlave();
				if(socket != NULL)
				{
					app.logger().information(format("Sending chunk to %s...", socket->peerAddress().toString()));
					// send chunk data
					talk = new dTalk(*socket);
					chunk = job->getChunk(pool.getChunkSize(*socket));
					talk->rpc(DCODE_SET_CHUNK, format("%lu", chunk));
					delete talk;
					pool.unready(*socket);
				}
				if(job->getHashCount() == 0)
				{
					job->stop();
					app.logger().information("The job has completed.");
					pool.sendMessage(NODE_CONIO, "The job has completed.");
				}
			}
		}
	}

	void shutdown()
	{
		_shutdown = true;
	}

private:
	bool _shutdown;
};

// The main application class.
class DistServer : public Poco::Util::ServerApplication
{
public:

	DistServer() : _helpRequested(false),
		_cfg("master.properties")
	{
	}

	~DistServer() { }

	void shutdown()
	{
		terminate();
	}
private:

	bool _helpRequested;
	string _cfg;

protected:

	void initialize(Application& self)
	{
		File f(_cfg);
		if(f.exists())
		{
			loadConfiguration(_cfg); // load default configuration files, if present
		}
		ServerApplication::initialize(self);
		self.logger().information("----------------------------------------");
		self.logger().information(format("Disthc Server Build [%d]", APP_VERSION));
	}

	void uninitialize()
	{
		ServerApplication::uninitialize();
	}

	void defineOptions(OptionSet& options)
	{
		ServerApplication::defineOptions(options);

		options.addOption(
			Option("help", "h", "display help information on command line arguments")
			.required(false)
			.repeatable(false));
		
		options.addOption(
			Option("config", "c", "specify where the .properties config file is located")
			.required(false)
			.repeatable(false)
			.argument("CONFIG"));
	}

	void handleOption(const std::string& name, const std::string& value)
	{
		ServerApplication::handleOption(name, value);
		
		if (name == "help")
		{
			_helpRequested = true;
		}
		else if (name == "config")
		{
			_cfg = value;
		}
	}

	void displayHelp()
	{
		HelpFormatter helpFormatter(options());
		helpFormatter.setCommand(commandName());
		helpFormatter.setUsage("OPTIONS");
		helpFormatter.setHeader("A distributed hash-cracking server.");
		helpFormatter.format(std::cout);
	}

	int main(const std::vector<std::string>& args)
	{
		if (_helpRequested)
		{
			displayHelp();
			return Application::EXIT_OK;
		}

		// Set pointer for DJob
		DJob *job = DJob::Instance();

		// get parameters from configuration file
		unsigned short port = (unsigned short) config().getInt("cfg.server.port", 4000);
		authToken = (string) config().getString("cfg.server.auth.token", "*");
		showResults = config().getBool("cfg.results.show", false);
		showDcode = config().getString("cfg.debug.dcode", "") == "show" ? true : false;
		DEBUG = config().getBool("cfg.debug", false);
		job->setAttackMode(config().getInt("cfg.job.attack", 1));
		job->setHashType(config().getInt("cfg.job.mode", 100));
		job->setDictionary(config().getString("cfg.job.dict.file", "disthc.dict"));
		job->setHashFile(config().getString("cfg.job.hashes.file", "disthc.hashes"));
		dbRainbowOn = config().getBool("cfg.server.db.rainbow", true);
		job->setDb(config().getString("cfg.server.db.file", "disthc.db"));
		// make sure db is not empty
		if(job->getDb().empty())
		{
			job->setDb("disthc.db");
		}
		job->setRules(config().getString("cfg.job.rules.file", ""));
		job->setMask(config().getString("cfg.job.mask", ""));

		Application& app = Application::instance();

		// set-up a server socket
		ServerSocket svs(port);
		// set-up a SocketReactor
		SocketReactor reactor;
		// ... and a SocketAcceptor
		SocketAcceptor<DistServiceHandler> acceptor(svs, reactor);
		// run the reactor in its own thread so that we can wait for
		// a termination request
		Thread net_thread;
		net_thread.start(reactor);

		app.logger().information(format("Listening on port %d",(int) port));
		if(DEBUG) app.logger().information("DEBUG mode enabled");
		app.logger().information("----------------------------------------");
		
		// Check for dictionary file
		app.logger().information("Scanning dictionary...");
		string dictionary = job->getDictionary();
		if(dictionary.empty())
		{
			// dictionary string must be valid, otherwise die
			app.logger().error("|Invalid dictionary file name.");
			return (EXIT_BAD_DICT);
		}
		File f(dictionary);
		if(!f.exists())
		{
			app.logger().error(format("|Unable to load dictionary: %s", dictionary));
			return (EXIT_BAD_DICT);
		}
		
		// Start db
		app.logger().information("Enabling master DB...");
		// Check for SQLite db
		Poco::Data::SQLite::Connector::registerConnector();
		db = new Poco::Data::Session(Poco::Data::SessionFactory::instance().create(Poco::Data::SQLite::Connector::KEY, job->getDb()));

		// Make sure tables are present
		*db << "CREATE TABLE IF NOT EXISTS job_queue (job_name VARCHAR, hash VARCHAR NOT NULL, salt VARCHAR)", now;
		*db << "CREATE TABLE IF NOT EXISTS rainbow (hash_type VARCHAR, hash VARCHAR NOT NULL, salt VARCHAR, plain VARCHAR NOT NULL)", now;
		*db << "CREATE TABLE IF NOT EXISTS clients (id INTEGER PRIMARY KEY AUTOINCREMENT, token VARCHAR NOT NULL, type VARCHAR(8) NOT NULL, name VARCHAR(128) NOT NULL, os VARCHAR(16) NOT NULL, os_version VARCHAR(16) NOT NULL, arch VARCHAR(8) NOT NULL, cpu INT NOT NULL DEFAULT 1, mac VARCHAR(32) NOT NULL, address VARCHAR(32) NOT NULL, last_seen DATETIME NOT NULL, blacklist VARCHAR(1) DEFAULT 'n')", now;
		loadHashes();
		//sendHashes(); // send on connect

		// Launch worker thread
		if(DEBUG)
		{
			app.logger().information("Launching worker thread...");
		}
		DistWorker dwork;
		Thread worker_thread;
		worker_thread.start(dwork);

		// Ready to work!
		app.logger().information("Ready.");

		// if autostart is set, start now
		//if(config().getBool("cfg.job.autostart", false)) {
		//	std::cout << "Starting job..." << std::endl;
		//	job->start();
		//}

		// Pause until finished
		waitForTerminationRequest();

		// Stop the SocketReactor and Worker
		reactor.stop();
		dwork.shutdown();

		// Join spawned threads
		net_thread.join();
		worker_thread.join();

		// Stop db and cleanup
		db->close();
		delete db;
		Poco::Data::SQLite::Connector::unregisterConnector();
		
		// Exit program
		app.logger().information("Bye.");
		return Application::EXIT_OK;
	}
};

int main(int argc, char** argv)
{
	Poco::Crypto::OpenSSLInitializer::initialize(); // needed to use DigestEngines
	DistServer app;
	return app.run(argc, argv);
}
