/*
	application: disthcm
	description: distributed hash-cracking console node
	written by Unix-Ninja
	May 10, 2013
*/

#include "disthc.h"
#include "dtalk.h"
#include "tinycon.h"

using std::string;
using std::vector;

// ************************************************************************** //
// Local globals
// ************************************************************************** //
std::string clientString;
std::string authToken;
bool DEBUG;
bool READY(false);
Thread thread_con;
dTalk *pTalk;
bool wait_for_receive(false);

// ************************************************************************** //
// Console Class
// ************************************************************************** //
class tcon : public Poco::Runnable, public tinyConsole
{
public:
	// run parent's constructor
	tcon (std::string s): tinyConsole(s) {}

	int trigger (std::string s)
	{
		if (s == "exit")
		{
			// SIGINT the current process
			Poco::Process::requestTermination(Poco::Process::id());
			// flag console for termination
			quit();
		}
		else
		{
			// the string buffer should probably be sent out to the server. for now, just echo it.
			//std::cout << " " << s << std::endl;
			if(!s.empty()) {
				pTalk->rpc(DCODE_RPC, s);
				wait_for_receive = true;
			}
		}

		while(wait_for_receive) {
            Poco::Thread::sleep(100);
		}
		return 0;
	}

	int hotkeys(char c)
	{
		if(c == TAB)
		{
			pTalk->rpc(DCODE_HOTKEY, "\t" + getBuffer());
		}
		return 0;
	}

	void run()
	{
		tinyConsole::run();
	}
} console(APP_PROMPT);

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

public:
	DistClientHandler(StreamSocket& socket, SocketReactor& reactor):
		_socket(socket),
		_reactor(reactor),
		_talk(_socket),
		_pBuffer(new char[BUFFER_SIZE])
	{
		Application& app = Application::instance();
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
		// initialize global pointer
		pTalk = &_talk;

		//greet server
		_talk.rpc(DCODE_HELO, format("-dhc:conio:%d", APP_VERSION));
		//authorize to server
		_talk.rpc(DCODE_TOKEN, authToken);
		// identify to server
		_talk.rpc(DCODE_SET_PARAM, clientString);
	}

	~DistClientHandler()
	{
		Application& app = Application::instance();
		try
		{
			_reactor.stop();
			app.logger().information("Disconnected from " + _socket.peerAddress().toString());
			// send quit to console
			console.quit();
			// SIGINT the current process
			Poco::Process::requestTermination(Poco::Process::id());
			//exit(1);
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
		//receive data or close on end
		//_talk.receive() || delete this;
		if ( !_talk.receive() ){ delete this; }

		if(_talk.dcode() == DCODE_READY)
		{
			READY = true;
		}
		else if(_talk.dcode() == DCODE_HOTKEY)
		{
			//std::cout << "[" << _talk.data().size() << "] " << std::flush;
			if (_talk.data().substr(0,1) == "\t")
			{
				console.setBuffer(console.getBuffer() + _talk.data().substr(1));
				std::cout << _talk.data().substr(1) << std::flush;
			}
			else if (_talk.data().size())
			{
				if(_talk.data() == format("%c", 4))
				{
					// end of output, so show prompt again
					console.showPrompt();
					std::cout << console.getBuffer() << std::flush;
				} else std::cout << _talk.data() << std::endl;
			}
		}
		if(_talk.dcode() == DCODE_PRINT)
		{
			std::cout << _talk.data() << std::endl;
			std::cout << std::flush; // force output to screen
		}
		wait_for_receive = false;
	}

	void onShutdown(const AutoPtr<ShutdownNotification>& pNf)
	{
		delete this;
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
		//gsocket = socket();
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
		exit(2); // Unable to connect to server
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

// ************************************************************************** //
// The main application class
// ************************************************************************** //
class DistClient : public Poco::Util::ServerApplication
{
public:

	DistClient() : _helpRequested(false),
		_cfg("console.properties")
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
		self.logger().information(format("Disthc Console Build [%d]", APP_VERSION));
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
		
		Application& app = Application::instance();
		
		// create client string
		clientString = format("%s %s %s %u %s %s",
			Poco::Environment::nodeName(),
			Poco::Environment::osName(),
			Poco::Environment::osVersion(),
			Poco::Environment::processorCount(),
			Poco::Environment::nodeId(),
			Poco::Environment::osArchitecture()
			);

		// get parameters from configuration file
		string host = (string) config().getString("cfg.server.address","localhost");
		unsigned short port = (unsigned short) config().getInt("cfg.server.port", 4000);
		authToken = (string) config().getString("cfg.server.auth.token", "*");
		DEBUG = config().getBool("cfg.server.debug", false);

		// set-up a stream socket
		SocketAddress sa(host, port);
		// set-up a SocketReactor
		SocketReactor reactor;

		// Connect to the server
		mySocketConnector connector(sa, reactor);

		// run the reactor in its own thread so that we can wait for a termination request
		Thread thread;
		thread.start(reactor);

		app.logger().information("----------------------------------------");

		while(!READY); // loop until ready
		// start console
		thread_con.start(console);

		waitForTerminationRequest();
		// Stop the SocketReactor
		reactor.stop();
		thread.join();
		thread_con.join();

		return Application::EXIT_OK;
	}
};

int main(int argc, char** argv)
{
	DistClient app;
	return app.run(argc, argv);
}
