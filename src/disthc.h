#include <Poco/Environment.h>
#include <Poco/Exception.h>
#include <Poco/File.h>
#include <Poco/FileStream.h>
#include <Poco/Logger.h>
#include <Poco/MemoryStream.h>
#include <Poco/Net/SecureServerSocket.h>	// SSL
#include <Poco/Net/SecureStreamSocket.h>	// SSL
#include <Poco/Net/SocketReactor.h>
#include <Poco/Net/SocketAcceptor.h>
#include <Poco/Net/SocketConnector.h>       // Client Connections
#include <Poco/Net/SocketAddress.h>         // Client Connections
#include <Poco/Net/SocketNotification.h>
#include <Poco/Net/StreamSocket.h>
#include <Poco/Net/ServerSocket.h>
#include <Poco/NObserver.h>
#include <Poco/NumberParser.h>
#include <Poco/Observer.h>                  // Client Connections
#include <Poco/Path.h>						// used by dTalk
#include <Poco/Pipe.h>
#include <Poco/Process.h>
#include <Poco/String.h>
#include <Poco/StringTokenizer.h>
#include <Poco/Thread.h>
#include <Poco/Util/Application.h>
#include <Poco/Util/HelpFormatter.h>
#include <Poco/Util/Option.h>
#include <Poco/Util/OptionSet.h>
#include <Poco/Util/ServerApplication.h>
#include <cstring>
#include <deque>
#include <iostream>
#include <string>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
#include <direct.h>
#else
#include <unistd.h>
#endif

using Poco::AutoPtr;
using Poco::File;
using Poco::FileInputStream;
using Poco::FileOutputStream;
using Poco::format;
using Poco::MemoryInputStream;
using Poco::Net::SecureServerSocket;
using Poco::Net::SecureStreamSocket;
using Poco::Net::SocketAcceptor;
using Poco::Net::SocketAddress;
using Poco::Net::SocketConnector;
using Poco::Net::SocketReactor;
using Poco::Net::ReadableNotification;
using Poco::Net::ShutdownNotification;
using Poco::Net::ServerSocket;
using Poco::Net::StreamSocket;
using Poco::NObserver;
using Poco::NumberParser;
using Poco::Observer;
using Poco::Path;
using Poco::Pipe;
using Poco::Process;
using Poco::StringTokenizer;
using Poco::Thread;
using Poco::trim;
using Poco::trimInPlace;
using Poco::Util::Application;
using Poco::Util::HelpFormatter;
using Poco::Util::Option;
using Poco::Util::OptionSet;

using std::deque;
using std::string;
using std::vector;


// Application constants
#define APP_VERSION 39
#define APP_PROMPT string("disthc>")
#define NODE_SLAVE 1
#define NODE_CONIO 2
#define DEFAULT_CHUNK_SIZE 1000000
#define SYNC_AUTO 1
#define NO_SYNC_AUTO 0

// Application parameters
#define PARAM_ATTACK "A"
#define PARAM_MODE "T"
#define PARAM_MASK "M"
#define PARAM_RULES "R"
#define PARAM_DICT "D"
#define PARAM_HASHES "H"
#define PARAM_CHUNK_SIZE "C"
#define PARAM_GHOST "G"

// Exit codes
#define EXIT_BAD_DICT 2
#define EXIT_BIND_FAILED 3
#define EXIT_BAD_HASHCAT 5
#define EXIT_BAD_ENGINE 6

// DCODE definitions
#define DCODE_HELO 1
#define DCODE_READY 2
#define DCODE_PRINT 3
#define DCODE_SET_PARAM 4
#define DCODE_START_COPY 5
#define DCODE_END_COPY 6
#define DCODE_GET_CHUNK 7
#define DCODE_SET_CHUNK 8
#define DCODE_RESULTS 9
#define DCODE_HOTKEY 10
#define DCODE_SYNC 11
#define DCODE_ZAP 12		// removes a list of hashes
#define DCODE_FILE_NAME 14	// starts transmission of a file
#define DCODE_FILE_DATA 15
#define DCODE_FILE_EOF 16	// stops transmission of a file
#define DCODE_RPC 33
#define DCODE_TOKEN 88

// Readability shortcuts
#define match !strcmp

// Globals
extern bool DEBUG;
extern bool GHOST;