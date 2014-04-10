/*
	application: disthcs
	description: distributed hash-cracking slave node
	written by Unix-Ninja
	May 10, 2013
*/

#include <map>
//#include <stdlib.h>	/* malloc, free, rand */
#include "disthc.h"
#include "djob.h"
#include "dtalk.h"
#include "engines/hashcat.h"

// ************************************************************************** //
// Local globals
// ************************************************************************** //
std::string clientString;
std::string authToken;
std::string dataPath;
bool DEBUG;
bool GHOST;
bool READY(false);
unsigned int auto_reconnect;
Engine *dengine;

//Ngn_oclHashcat dengine;

// ************************************************************************** //
// Service handle for main app
// ************************************************************************** //
class DistClientHandler
{
private:
	static const int BUFFER_SIZE = 1024;

	StreamSocket _socket;
	SocketReactor& _reactor;
	dTalk _talk;
	char* _pBuffer;
	string _dataPath;

public:
	DistClientHandler(StreamSocket& socket, SocketReactor& reactor):
		_socket(socket),
		_reactor(reactor),
		_talk(_socket),
		_pBuffer(new char[BUFFER_SIZE])
	{
		Application& app = Application::instance();
		DJob *job = DJob::Instance();
		app.logger().information("Connected to " + socket.peerAddress().toString());

		_reactor.addEventHandler(
			_socket,
			NObserver<DistClientHandler,
			ReadableNotification>(*this, &DistClientHandler::onReadable)
		);

		_reactor.addEventHandler(
			_socket,
			NObserver<DistClientHandler,
			ShutdownNotification>(*this, &DistClientHandler::onShutdown)
		);
		// greet server
		_talk.rpc(DCODE_HELO, format("-dhc:slave:%d", APP_VERSION));
		// authorize to server
		_talk.rpc(DCODE_TOKEN, authToken);
		// identify to server
		_talk.rpc(DCODE_SET_PARAM, clientString);
		// report chunk capability
		_talk.rpc(DCODE_SET_PARAM, format(string(PARAM_CHUNK_SIZE)+":%u", job->getChunkSize()));
		// sync with server
		if(dengine->remoteSync())
		{
			_talk.rpc(DCODE_SYNC);
			app.logger().information("Syncing...");
			sync();
		}
		else
		{
			READY = true;
			_talk.rpc(DCODE_READY);
			app.logger().information("Ready.");
		}
	}	

	~DistClientHandler()
	{
		Application& app = Application::instance();
		try
		{
			_reactor.stop();
			app.logger().information("Disconnected from " + _socket.peerAddress().toString());
			// SIGINT the current process
			Poco::Process::requestTermination(Poco::Process::id());
		}
		catch (...)
		{
		}

		_reactor.removeEventHandler(
			_socket,
			NObserver<DistClientHandler,
			ReadableNotification>(*this, &DistClientHandler::onReadable)
		);

		_reactor.removeEventHandler(
			_socket,
			NObserver<DistClientHandler,
			ShutdownNotification>(*this, &DistClientHandler::onShutdown)
		);
		delete [] _pBuffer;
	}

	void onReadable(const AutoPtr<ReadableNotification>& pNf)
	{
		Application& app = Application::instance();
		DJob *job = DJob::Instance();

		//receive data or close on end
		if(!_talk.receive()) delete this;
		
		//app.logger().information(format("[%d|%s]", _talk.dcode(), (string) _talk.data())); //debug line
		
		if(_talk.dcode() == DCODE_READY)
		{
			READY = true;
			_talk.rpc(DCODE_READY);
			app.logger().information("Ready.");
		}
		else if(_talk.dcode() == DCODE_SET_CHUNK)
		{
			if (DEBUG) app.logger().information("%Received chunk from master.");
			job->setChunk(NumberParser::parseUnsigned(_talk.data()));
			// process chunk!
			if(dengine->isRunnable())
			{
				dengine->run();

				if (DEBUG) app.logger().information("%Sending results to master.");

				// send results back to master
				_talk.rpc(DCODE_RESULTS, dengine->results());
			} else {
				_talk.rpc(DCODE_RESULTS, "");
			}
		}
		else if(_talk.dcode() == DCODE_SET_PARAM)
		{
			StringTokenizer t(_talk.data(),":");
			const char* key = t[0].c_str();
			
			if (match(key, PARAM_ATTACK))
			{
				int value;
				if(t.count()>1)
				{
					value = atoi(t[1].c_str());
				}
				else
				{
					value = 0;
				}
				job->setAttackMode(value);
				if(DEBUG) app.logger().information(format("%%Set param: attack[%d]", value));
			}
			else if (match(key, PARAM_MODE))
			{
				int value;
				if(t.count()>1)
				{
					value = atoi(t[1].c_str());
				}
				else
				{
					value = 0;
				}
				job->setHashType(value);
				if(DEBUG) app.logger().information(format("%%Set param: mode[%d]", value));
			}
			else if (match(key, PARAM_MASK))
			{
				string value;
				if(t.count()>1)
				{
					value = string(t[1].c_str());
				}
				else
				{
					value = "";
				}
				job->setMask(value);
				if(DEBUG) app.logger().information(format("%%Set param: mask[%s]", value));
			}
			else if (match(key, PARAM_RULES))
			{
				string value;
				if(t.count()>1)
				{
					value = string(t[1].c_str());
				}
				else
				{
					value = "";
				}
				job->setRules(value);
				if(DEBUG) app.logger().information(format("%%Set param: rules[%s]", value));
			}
			else if (match(key, PARAM_HASHES))
			{
				string value;
				if(t.count()>1)
				{
					value = string(t[1].c_str());
				}
				else
				{
					value = "";
				}
				// set job and engine hash values
				job->setHashFile(value);
				dengine->setHashFile(value);
				if(DEBUG) app.logger().information(format("%%Set param: hash file[%s]", value));
			}
			else if (match(key, PARAM_DICT))
			{
				string value;
				if(t.count()>1)
				{
					value = string(t[1].c_str());
				}
				else
				{
					value = "";
				}
				job->setDictionary(value);
				if(DEBUG) app.logger().information(format("%%Set param: dictionary[%s]", value));
			}
			else
			{
				app.logger().information("|Invalid param to set!");
				if(DEBUG) app.logger().information(format("++[%s]", (string) key));
			}
		}
		else if (_talk.dcode() == DCODE_ZAP)
		{
			if(DEBUG) app.logger().information(format("Zapping: %s", _talk.data()));
			if(dengine->getName() == "hashcat" || dengine->getName() == "oclhashcat")
			{
				dengine->zapHashes(trim(_talk.data()));
			}
		}
		else if(_talk.dcode() == DCODE_FILE_NAME)
		{
			//app.logger().information("|ugh syncing...");
			syncFile();
		}
		else if(_talk.dcode() == DCODE_PRINT)
		{
			app.logger().information(_talk.data());
		}
	}

	void onShutdown(const AutoPtr<ShutdownNotification>& pNf)
	{
		delete this;
	}
	
	void syncFile()
	{
		Application& app = Application::instance();
		
		//if(!_talk.save_file(dataPath,_talk.data()))
		if(!_talk.save_file("./",_talk.data()))
		{
			app.logger().information(format("|Unable to save file: %s", _talk.data()));
		}
	}

};

class mySocketConnector : public SocketConnector<DistClientHandler>
{
private:
	bool _failed;
	bool _shutdown;

public:

	mySocketConnector(SocketAddress& address, SocketReactor& reactor) :
		SocketConnector<DistClientHandler>(address, reactor),
		_failed(false),
		_shutdown(false)
	{
		reactor.addEventHandler(socket(), Observer<mySocketConnector, ShutdownNotification > (*this, &mySocketConnector::onShutdown));
	}

	void onShutdown(ShutdownNotification* pNf)
	{
		pNf->release();
		_shutdown = true;
	}

	void onError(int error)
	{
		Application& app = Application::instance();
		app.logger().information("Error: Unable to connect to disthc server!");
		_failed = true;
		reactor()->stop();
		Poco::Process::requestTermination(Poco::Process::id());
	}

	bool failed() const
	{
		return _failed;
	}

	bool shutdown() const
	{
		return _shutdown;
	}

};

// The main application class.
class DistClient : public Poco::Util::ServerApplication
{
public:

	DistClient() : _helpRequested(false),
		_syncRequested(-1),
		_cfg ("slave.properties")
	{
	}

	~DistClient()
	{
	}

	void shutdown()
	{
		terminate();
	}
private:

	bool _helpRequested;
	int _syncRequested;
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
		self.logger().information(format("Disthc Slave Build [%d]", APP_VERSION));
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
			.repeatable(false)
		);
		options.addOption(
			Option("sync", "S", "automatuically sync data files from master (true|false)")
			.required(false)
			.repeatable(false)
			.argument("true|false", true)
		);
		
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
		else if (name == "sync")
		{
			if (Poco::toLower(value) == "true")
			{
				_syncRequested = SYNC_AUTO;
			}
			else
			{
				_syncRequested = NO_SYNC_AUTO;
			}
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
		helpFormatter.setHeader("A distributed hash-cracking slave.");
		helpFormatter.format(std::cout);
	}

	int main(const std::vector<std::string>& args)
	{
		if (_helpRequested)
		{
			displayHelp();
			return Application::EXIT_OK;
		}
		
		Application& app = Application::instance();
		DJob *job = DJob::Instance();

		// get parameters from configuration file
		auto_reconnect = (unsigned int) config().getInt("cfg.reconnect.interval", 0);
		string host = (string) config().getString("cfg.server.address","localhost");
		unsigned short port = (unsigned short) config().getInt("cfg.server.port", 4000);
		authToken = (string) config().getString("cfg.server.auth.token", "*");
		dataPath = (string) config().getString("cfg.data.path", ".");
		DEBUG = config().getBool("cfg.debug", false);
		GHOST = config().getBool("cfg.ghost.allow", false);
		job->setPot((string) config().getString("cfg.pot.path","disthc.pot"));
		job->setChunkSize(config().getInt("cfg.chunk.size", DEFAULT_CHUNK_SIZE));
		string engine_name = (string) config().getString("cfg.engine","oclhashcat");
		
		// change to working directory
		chdir(dataPath.c_str());
		
		// setup correct engine object
		if(engine_name == "hashcat") {
			dengine = new Ngn_Hashcat();
		}
		else if (engine_name == "oclhashcat")
		{
			dengine = new Ngn_oclHashcat();
			
			// set gpu-temp option
			if((string) config().getString("cfg.hashcat.gputemp","true") == "true")
			{
				dengine->setConfig("gpuTempDisable", "false");
			}
			else
			{
				dengine->setConfig("gpuTempDisable", "true");
			}
		}
		else
		{
			app.logger().information("Invalid option for cfg.engine!");
			return EXIT_BAD_ENGINE;
		}
		
		
		// set engine configs
		dengine->setBinaryPath("hashcat", (string) config().getString("cfg.hashcat.path",""));
		dengine->setBinaryPath("oclhashcat", (string) config().getString("cfg.oclhashcat.path",""));
		dengine->setName(engine_name);
		dengine->setHashFile((string) config().getString("cfg.hashes.path","disthc.hashes"));
		
		// verify sync requests
		if(_syncRequested != -1)
		{
			if(_syncRequested == SYNC_AUTO)
			{
				dengine->remoteSync(true);
			}
			else
			{
				dengine->remoteSync(false);
			}
		}
		else
		{
			dengine->remoteSync((bool) config().getBool("cfg.server.sync","true"));
		}

		// set-up a stream socket
		SocketAddress sa(host, port);
		// set-up a SocketReactor
		SocketReactor reactor;

		// Connect to the server
		mySocketConnector connector(sa, reactor);

		if(DEBUG) app.logger().information("DEBUG mode enabled");
		
		if(dengine->remoteSync())
		{
			app.logger().information("SYNC from remote enabled");
		}
		
		app.logger().information("----------------------------------------");
		
		// Check for ghosts here
		if(dengine->isGhost())
		{
			if(DEBUG) app.logger().information("%Ghost detected!");
			if(!GHOST)
			{
				app.logger().information("Ghosts are not allowed. Exiting.");
				return EXIT_BAD_HASHCAT;
			}
		}
		
		// run the reactor in its own thread so that we can wait for a termination request
		Thread thread;
		thread.start(reactor);

		waitForTerminationRequest();
		// Stop the SocketReactor
		reactor.stop();
		thread.join();

		return Application::EXIT_OK;
	}
};

int run_app(int argc, char** argv)
{
	DistClient app;
	return app.run(argc, argv);
}

int main(int argc, char** argv)
{
	int ec;
	string cwd = Poco::Path::current();
	
	// create client string
	clientString = format("%s %s %s %u %s %s",
		Poco::Environment::nodeName(),
		Poco::Environment::osName(),
		Poco::Environment::osVersion(),
		Poco::Environment::processorCount(),
		Poco::Environment::nodeId(),
		Poco::Environment::osArchitecture()
		);
		
	// run slave
	ec =run_app(argc, argv);
	if(ec != Application::EXIT_OK)
	{
		return ec;
	}
	
	// auto-reconnect code
	if(auto_reconnect>0)
	while(1)
	{
		chdir(cwd.c_str()); // reset cwd before looping so we can find configs
		Thread::sleep(auto_reconnect * 1000); // wait before trying to reconnect
		std::cout << std::endl;
		ec = run_app(argc, argv);
		if(ec != Application::EXIT_OK)
		{
			return ec;
		}
	}
	return 0;
}
