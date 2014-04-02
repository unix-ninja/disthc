struct hash_list
{
	std::string job_name;
	std::string hash;
	std::string salt;
};

string cmap[] = {		// map of commands to expand
			"attack",
			"chunk",
			"dictionary",
			"exit",
			"hashes",
			"mask",
			"msg",
			"mode",
			"shutdown",
			"slaves",
			"start",
			"status",
			"stop" };

#ifndef DROLE_MASTER
#define DROLE_MASTER
#endif