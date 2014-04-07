#include <string>
#include "engine.h"

class Ngn_Hashcat : public Engine
{
public:
	int			run();
	string		results();
	bool		isGhost();
	void		zapHashes(string);
};

class Ngn_oclHashcat : public Engine
{
public:
	Ngn_oclHashcat () { return; }
	~Ngn_oclHashcat () { }
	int			run();
	string		results();
	bool		isGhost();
	void		zapHashes(string);
};