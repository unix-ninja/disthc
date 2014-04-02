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
#include <Poco/Tuple.h>

using Poco::Data::into;
using Poco::Data::now;
using Poco::Data::range;
using Poco::Data::use;

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
	//loop through hash file
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
						//TODO slave is probably unsafe at this point. maybe remove from pool
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
					// register the client
					if(_clientType == "slave")
					{
						pool.registerClient(_socket, NODE_SLAVE);
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
					else
					{
						pool.registerClient(_socket, NODE_CONIO);
					}
					
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

		_talk.receive();

		// Make sure is authorized, or die
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
		vector<string> cmd_map;
		
		for (int i=0; i<sizeof(cmap) / sizeof(string); i++)
		{
			if(cmap[i].find(rpc,0) == 0)
			{
				cmd_map.push_back(cmap[i]);
			}
			
		}
		
		if(cmd_map.size() > 1) return " ";
		if(cmd_map.size() == 1) return cmd_map[0];
		return rpc;
	}
	
	string tab_complete(string needle)
	{
		vector<string> cmd_map;
		string map;
		
		for (int i=0; i<sizeof(cmap) / sizeof(string); i++)
		{
			if(cmap[i].find(needle,0) == 0)
			{
				cmd_map.push_back(cmap[i]);
			}
		}
		
		// return only the suffix if found
		if(cmd_map.size() == 1) return "\t" + cmd_map[0].substr(needle.length()) + " ";
		
		if(cmd_map.size() > 1)
		{
			map = "\n";
			for (int i=0; i<cmd_map.size(); i++)
			{
				map += cmd_map[i] + "\n";
			}
			map += format("%c", 4);
			return map;
		}
		
		// return nothing
		return "";
	}

	void process_rpc()
	{
		Application& app = Application::instance();
		StringTokenizer param(_talk.data()," ", StringTokenizer::TOK_IGNORE_EMPTY | StringTokenizer::TOK_TRIM); 
		string rpc = expand_rpc(param[0]);
		
		if(rpc == " ")
		{
			if(DEBUG) app.logger().information(format("|Ambiguous command: %s", _talk.data()));
			_talk.rpc(DCODE_PRINT, "Ambiguous command.\n");
			return;
		}
		// Set pointer for DJob
		DJob *job = DJob::Instance();
		
		if(rpc == "attack")
		{
			if(param.count()>1)
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
			if(param.count()>1)
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
		else if(rpc == "debug")
		{
			debugCmd();
		}
		else if(rpc == "dictionary")
		{
			if(param.count()>1)
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
			if(param.count()>1)
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
						// TODO we can probably remove the sendParam and just send the relevant list
						//      or... we can have the client compare local hash files and request missing ones.
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
			if(param.count()>1)
			{
				moreHelp(param[1]);
			} else {
				if(DEBUG) app.logger().debug("%Sending help data...");
				_talk.rpc(DCODE_PRINT, string("Supported commands (type help [command] to get more info):\n") +
					  "  attack\n" +
					  "  chunk\n" +
					  "  dictionary\n" +
					  "  exit\n" +
					  "  hashes\n" +
					  "  help\n" +
					  "  mask\n" +
					  "  msg\n" +
					  "  mode\n" +
					  "  show\n" +
					  "  shutdown\n" +
					  "  slaves\n" +
					  "  start\n" +
					  "  status\n" +
					  "  stop\n");
			}
		}
		else if(rpc == "mask")
		{
			if(param.count()>1)
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
				
			} else {
				_talk.rpc(DCODE_PRINT, format("Mask: %s\n", job->getMask()));
			}
		}
		else if(rpc == "mode")
		{
			if(param.count()>1)
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
			if(param.count()>1)
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
			if(param.count()>1)
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
			if(param.count()>1)
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
		else if(rpc == "slaves")
		{
			_talk.rpc(DCODE_PRINT, format("Slaves: %d",pool.count(NODE_SLAVE)));
		}
		else if(rpc == "start")
		{
			if(job->isRunning())
			{
				_talk.rpc(DCODE_PRINT, "A job is already running.\n");
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
			if(job->isRunning()) {
				msg = "A job is currently running.";
			} else {
				msg = "No jobs are running.";
			}
			// format can only take 7 args at a time. maybe we should use another method.
			msg = format("%s\n-- Stats --\n  attack: %d\n  mode: %d\n  hashes: %s\n  dictionary: %s\n  mask: %s\n", msg, job->getAttackMode(), job->getHashType(), job->getHashFile(), job->getDictionary(), job->getMask());
			msg = format ("%s  remaining hashes: %u\n", msg, job->getHashCount());
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
				(string) "    mask           view the current mask\n"+
				(string) "    mask <string>  set the mask to <string>\n"+
				(string) "    mask -         clear the mask and do not use it in jobs";
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
		} else if(cmd == "slaves") {
			msg = "(slaves) view the current number of connected slaves.";
		} else if(cmd == "start") {
			msg = "(start)  use this to start the processing of a job.";
		} else if(cmd == "status") {
			msg = "(status)  use this to view the status of a job.";
		} else if(cmd == "stop") {
			msg = "(stop)  use this to stop the processing of a job.";
		} else {
			msg = "No information is available for '" + cmd + "'\n";
		}
		_talk.rpc(DCODE_PRINT, msg + "\n");
	}
	
	void debugCmd()
	{
		string ts = " An application uses instances of the Logger class to generate its log messages and send them on their way to their final destination. Logger instances are organized in a hierarchical, tree-like manner and are maintained by the framework. Every Logger object has exactly one direct ancestor, with the exception of the root logger. A newly created logger inherits its properties - channel and level - from its direct ancestor. Every logger is connected to a channel, to which it passes on its messages. Furthermore, every logger has a log level, which is used for filtering messages based on their priority. Only messages with a priority equal to or higher than the specified level are passed on. For example, if the level of a logger is set to three (PRIO_ERROR), only messages with priority PRIO_ERROR, PRIO_CRITICAL and PRIO_FATAL will propagate. If the level is set to zero, the logger is effectively disabled.\n The name of a logger determines the logger's place within the logger hierarchy. The name of the root logger is always "", the empty string. For all other loggers, the name is made up of one or more components, separated by a period. For example, the loggers with the name HTTPServer.RequestHandler and HTTPServer.Listener are descendants of the logger HTTPServer, which itself is a descendant of the root logger. There is not limit as to how deep the logger hierarchy can become. Once a logger has been created and it has inherited the channel and level from its ancestor, it loses the connection to it. So changes to the level or channel of a logger do not affect its descendants. This greatly simplifies the implementation of the framework and is no real restriction, because almost always levels and channels are set up at application startup and never changed afterwards. Nevertheless, there are methods to simultaneously change the level and channel of all loggers in a certain hierarchy.\n There are also convenience macros available that wrap the actual logging statement into a check whether the Logger's log level is sufficient to actually log the message. This allows to increase the application performance if many complex log statements are used. The macros also add the source file path and line number into the log message so that it is available to formatters. Variants of these macros that allow message formatting with Poco::format() are also available. Up to four arguments are supported.";
		
		_talk.rpc(DCODE_PRINT, ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + ts + "\n");
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

	DistServer() : _helpRequested(false) { }

	~DistServer() { }

	void shutdown()
	{
		terminate();
	}
private:

	bool _helpRequested;

protected:

	void initialize(Application& self)
	{
		string cfg = "master.properties";
		File f(cfg);
		if(f.exists())
		{
			loadConfiguration(cfg); // load default configuration files, if present
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
	}

	void handleOption(const std::string& name, const std::string& value)
	{
		ServerApplication::handleOption(name, value);

		if (name == "help")
		{
			_helpRequested = true;
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
		*db << "CREATE TABLE IF NOT EXISTS job_queue (id INT AUTO_INCREMENT PRIMARY KEY, job_name VARCHAR, hash VARCHAR NOT NULL, salt VARCHAR)", now;
		*db << "CREATE TABLE IF NOT EXISTS rainbow (id INT AUTO_INCREMENT PRIMARY KEY, hash_type VARCHAR, hash VARCHAR NOT NULL, salt VARCHAR, plain VARCHAR NOT NULL)", now;
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
	DistServer app;
	return app.run(argc, argv);
}
