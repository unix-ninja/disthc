struct hash_list
{
	std::string job_name;
	std::string hash;
	std::string salt;
};

string cmap[] = {		// map of commands to expand
			"attack",
			"chunk",
			"clients",
			"clients details",
			"clients details conio",
			"clients details slave",
			"dictionary",
			"exit",
			"hashes",
			"mask",
			"mask maximum",
			"mask minimum",
			"msg",
			"mode",
			"shutdown",
			"start",
			"status",
			"stop" };

#ifndef DROLE_MASTER
#define DROLE_MASTER
#endif