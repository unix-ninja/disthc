// Requires PocoLibs

class dTalk {
private:
    static const int BUFFER_SIZE = 1024;
	StreamSocket _socket;
	char* _pBuffer;
	int _dcode;
	std::vector<char> _data;
	void zero_char(char* buffer);

public:
	dTalk(StreamSocket& socket);
	bool rpc(int DCODE);
	bool rpc(int DCODE, std::string);
	bool rpc(int DCODE, int);
	bool send(char* buffer, int size);
	bool send(std::string buffer);
	bool send_file(std::string filename);
	bool send_text_as_file(std::string filename, std::string text);
	bool save_file(std::string savepath, std::string filename);

	bool receive();				// receive an rpc request
	bool receive(int size);			// receive a raw request
	int dcode();				// DCODE from last received
	std::string data();			// data from last received
	int size();				// size of last received data
};

union dnet_size {
	int32_t Int;
	//char Index[4];	// use 32 bits
	char Index[2];		// use 16 bits
};
