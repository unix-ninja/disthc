#include <cstdlib>
#include "../disthc.h"
#include "../djob.h"
#include "hashcat.h"


// ************************************************************************** //
// ************************************************************************** //
//                                Engine
// ************************************************************************** //
// ************************************************************************** //


void Engine::setName(string engine)
{
	_engine = engine;
}

string Engine::getName()
{
	return _engine;
}

int Engine::getAttackMode()
{
	return _attack;
}

void Engine::setAttackMode(int mode)
{
	_attack = mode;
}

int Engine::getHashType()
{
	return (int) _mode;
}

void Engine::setHashType(int mode)
{
	_mode = mode;
}

std::string Engine::getDictionary()
{
	return (std::string) _dict;
}

void Engine::setDictionary(std::string dictionary)
{
	_dict = dictionary;
}

std::string Engine::getRules()
{
	return (std::string) _rules;
}

void Engine::setRules(std::string rules)
{
	_rules = rules;
}

std::string Engine::getMask()
{
	return (std::string) _mask;
}

void Engine::setMask(std::string mask)
{
	_mask = mask;
}

string Engine::getBinaryPath(string key)
{
	return _map.find(key)->second;
}

void Engine::setBinaryPath(string key, string value)
{
	_map.insert(std::pair<string, string>(key, value));
}

string Engine::getPot()
{
	return _pot;
}

void Engine::setPot(std::string pot)
{
	_pot = pot;
	return;
}

string Engine::getHashFile()
{
	return _hashes;
}

void Engine::setHashFile(std::string hashes)
{
	_hashes = hashes;
	return;
}

string Engine::getFlags()
{
	return string("");
	return _flags;
}

void Engine::setFlags(std::string flags)
{
	_flags = flags;
	return;
}

string Engine::getConfig(string key)
{
	return _cfg.find(key)->second;
}

void Engine::setConfig(string key, string value)
{
	_cfg.insert(std::pair<string, string>(key, value));
}

bool Engine::remoteSync()
{
	return _sync;
}

void Engine::remoteSync(bool sync)
{
	_sync = sync;
}

bool Engine::isRunnable()
{
	Application& app = Application::instance();
	int hashes = 0;
	string hFile(getHashFile());
	string line;

	if (DEBUG) app.logger().information(format("%%Loading hash file %s...", hFile));
	// make sure the hash file exists
	Poco::File f(hFile);
	if (!f.exists())
	{
		app.logger().information("|Unable to find hash file");
		return false;
	}
	
	// open the has file
	FileInputStream fis(hFile);
	if (!fis.good())
	{
		app.logger().information("|Unable to load hash file");
		return false;
	}
	
	// count entries
	while(fis >> line) {
		if(!line.empty()) hashes++;
	}
	if(hashes>0) return true;
	
	if(DEBUG)
	{
		app.logger().information("%No hashes to load");
	}
	return false;
}





// ************************************************************************** //
// ************************************************************************** //
//                                 Hashcat
// ************************************************************************** //
// ************************************************************************** //


int Ngn_Hashcat::run ()
{
	Application& app = Application::instance();
	DJob *job = DJob::Instance();
	string cmd;
	string hashcat(dengine->getBinaryPath("hashcat"));
	int eCode(-1);
	
	// clean results
	_results = string("");
	
	// grab settings from job
	setAttackMode(job->getAttackMode());
	setHashType(job->getHashType());
	setMask(job->getMask());
	setRules(job->getRules());
	setDictionary(job->getDictionary());
	setPot("disthc.pot");
	
	// clean pot before working
	File f(getPot());
	if(f.exists()) {
		f.remove();
	}
	
	// setup command prefix (format command takes 7 args max)
	cmd = format("%s -o %s -s %lu -l %u",
		hashcat,
		getPot(),
		job->getChunk(),
		job->getChunkSize()
	);
	
	// Attack modes:
	// 0 = Straight
	// 1 = Combination
	// 2 = Toggle-Case
	// 3 = Brute-force
	// 4 = Permutation
	// 5 = Table-Lookup
	
	// if mask minimum set, apply it
	if(job->getMaskMin())
	{
		cmd = format("%s --pw-min %d",
			cmd,
			job->getMaskMin()
		);
	}
	
	// discover attack mode and create command to execute
	switch(getAttackMode())
	{
		case 3:
			cmd = format("%s -a3 -m %d %s %s %s",
				cmd,
				getHashType(),
				getFlags(),
				getHashFile(),
				getMask()
			);
			break;
		default:
			// default command uses attack mode 0
			cmd = format("%s -m %d %s %s %s %s",
				cmd,
				getHashType(),
				getFlags(),
				getHashFile(),
				getDictionary(),
				getRules()
			);
	}
	
	if(DEBUG) app.logger().information(format("%%Running command: %s", cmd));

	// check for ghosts, and run as appropriate
	if(isGhost())
	{
		app.logger().information("~~~ A ghost is loose! ~~~");
		app.logger().information("      .-.");
		app.logger().information("     (o o) boo!");
		app.logger().information("    \\| O \\/");
		app.logger().information("      \\   \\ ");
		app.logger().information("       `~~~' ");
	}
	else
	{
		// run hashcat!  :)
		// TODO change this over to use Poco Processes
		eCode = system(cmd.c_str());
		
		// check for results
		if(f.exists()) {
			FileInputStream fis(getPot());
			//std::ifstream in(pot,std::ios::in);
			string line;
			while(fis >> line) {
				_results.append(line + "\n");
			}
		}
		
		// TODO might take this out?
		// see if it's worth it to just display hashcout output during
		// execution
		// if enabled, print pot to screen
//		if(false) {
//			app.logger().information("\n=== Recovered Hashes ===");
//			if(!_results.empty()) app.logger().information(_results);
//			app.logger().information("========================");
//		}
	}
	return 0;
}

std::string Ngn_Hashcat::results ()
{
	return _results;
}

bool Ngn_Hashcat::isGhost()
{
	int e(0);
	
	// setup pipe to catch output
	Pipe outPipe;
	
	// get exit code for hashcat
	std::vector<std::string> args;
	args.push_back("--help");
	Poco::ProcessHandle ph = Process::launch(dengine->getBinaryPath("hashcat"), args, NULL, &outPipe, NULL);
	e = ph.wait();
	
	// check to see if hashcat ran and exited cleanly
	if(e==0 || e==255) return false;
	
	// hashcat failed or is not present
	return true;
}

void Ngn_Hashcat::zapHashes(string data)
{
	int i;
	string line;
	string hash;
	bool write, move(false);
	
	// clean data
	data = trim(data);
	if(data.empty() || isGhost()) return;
	
	Application& app = Application::instance();
	
	if(DEBUG) {	
		app.logger().information("%Zap!");
	}
	
	// parse hashes
	StringTokenizer hashes(data, "\n", 
		StringTokenizer::TOK_IGNORE_EMPTY | StringTokenizer::TOK_TRIM);
	
	string oldFile(getHashFile());
	string newFile(string(getHashFile()) + ".new");
	
	FileInputStream fis(oldFile);
	FileOutputStream fos(newFile);
	
	// remove cracked hashes
	while(fis >> line) {
		write = true;
		for(i=0;i<hashes.count();i++) {
			StringTokenizer hash(hashes[i], ":",
				StringTokenizer::TOK_IGNORE_EMPTY | StringTokenizer::TOK_TRIM);
			if(hash[0] == line) write = false;
		}
		if(write) {
			fos << line << std::endl;
			move = true;
		}
	}
	
	// cleanup
	fis.close();
	fos.close();
	
	if(move || hashes.count()) {
		// remove garbage
		File g(oldFile);
		g.remove();
		// copy new file to it's place
		File f(newFile);
		f.moveTo(oldFile);
	} else {
		// remove garbage
		File g(newFile);
		g.remove();
	}
	return;
}







// ************************************************************************** //
// ************************************************************************** //
//                                oclHashcat
// ************************************************************************** //
// ************************************************************************** //


int Ngn_oclHashcat::run ()
{
	Application& app = Application::instance();
	DJob *job = DJob::Instance();
	string cmd;
	string hashcat(dengine->getBinaryPath("oclhashcat"));
	string gputemp("");
	int eCode(-1);
	
	// clean results
	_results = string("");
	
	// grab settings from job
	setAttackMode(job->getAttackMode());
	setHashType(job->getHashType());
	setMask(job->getMask());
	setRules(job->getRules());
	setDictionary(job->getDictionary());
	setPot("disthc.pot");
	
	// clean pot before working
	File f(getPot());
	if(f.exists()) {
		f.remove();
	}
	
	if(getConfig("gpuTempDisable") == "true")
	{
		gputemp = "--gpu-temp-disable";
	}
	// setup command prefix (format command takes 7 args max)
	cmd = format("%s %s -o %s -s %lu -l %u",
		hashcat,
		gputemp,
		getPot(),
		job->getChunk(),
		job->getChunkSize()
	);
	
	// Attack modes:
	// 0 = Straight
	// 1 = Combination
	// 2 = Toggle-Case
	// 3 = Brute-force
	// 4 = Permutation
	// 5 = Table-Lookup
	
	// discover attack mode and create command to execute
	switch(getAttackMode())
	{
		case 3:
			cmd = format("%s -a3 -m %d %s %s %s",
				cmd,
				getHashType(),
				getFlags(),
				getHashFile(),
				getMask()
			);
			break;
		default:
			// default command uses attack mode 0
			cmd = format("%s -m %d %s %s %s %s",
				cmd,
				getHashType(),
				getFlags(),
				getHashFile(),
				getDictionary(),
				getRules()
			);
	}
	
	if(DEBUG) app.logger().information(format("%%Running command: %s", cmd));
	
	// check for ghosts, and run as appropriate
	if(isGhost())
	{
		app.logger().information("~~~ A ghost is loose! ~~~");
		app.logger().information("      |\\___   - boo!");
		app.logger().information("    (:o ___(");
		app.logger().information("      |/");
	}
	else
	{
		// run hashcat!  :)
		// TODO change this over to use Poco Processes
		eCode = system(cmd.c_str());
		
		// check for results
		if(f.exists()) {
			FileInputStream fis(getPot());
			//std::ifstream in(pot,std::ios::in);
			string line;
			while(fis >> line) {
				_results.append(line + "\n");
			}
		}
		
		// TODO might take this out?
		// see if it's worth it to just display hashcout output during
		// execution
		// if enabled, print pot to screen
	}
	return 0;
}

std::string Ngn_oclHashcat::results ()
{
	return _results;
}

bool Ngn_oclHashcat::isGhost()
{
	int e(0);
	
	// setup pipe to catch output
	Pipe outPipe;
	
	// get exit code for hashcat
	std::vector<std::string> args;
	args.push_back("--help");
	Poco::ProcessHandle ph = Process::launch(dengine->getBinaryPath("oclhashcat"), args, NULL, &outPipe, NULL);
	e = ph.wait();
	
	// check to see if hashcat ran and exited cleanly
	if(e==0 || e==255) return false;
	
	// hashcat failed or is not present
	return true;
}

void Ngn_oclHashcat::zapHashes(string data)
{
	int i;
	string line;
	string hash;
	bool write, move(false);
	
	// clean data
	data = trim(data);
	if(data.empty() || isGhost()) return;
	
	Application& app = Application::instance();
	
	if(DEBUG) {	
		app.logger().information("%Zap!");
	}
	
	// parse hashes
	StringTokenizer hashes(data, "\n", 
		StringTokenizer::TOK_IGNORE_EMPTY | StringTokenizer::TOK_TRIM);
	
	string oldFile(getHashFile());
	string newFile(string(getHashFile()) + ".new");
	
	FileInputStream fis(oldFile);
	FileOutputStream fos(newFile);
	
	// remove cracked hashes
	while(fis >> line) {
		write = true;
		for(i=0;i<hashes.count();i++) {
			StringTokenizer hash(hashes[i], ":",
				StringTokenizer::TOK_IGNORE_EMPTY | StringTokenizer::TOK_TRIM);
			if(hash[0] == line) write = false;
		}
		if(write) {
			fos << line << std::endl;
			move = true;
		}
	}
	
	// cleanup
	fis.close();
	fos.close();
	
	if(move || hashes.count()) {
		// remove garbage
		File g(oldFile);
		g.remove();
		// copy new file to it's place
		File f(newFile);
		f.moveTo(oldFile);
	} else {
		// remove garbage
		File g(newFile);
		g.remove();
	}
	return;
}