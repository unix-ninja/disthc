// dTalk - a simple network protocol for disthc
#include "disthc.h"
#include "dtalk.h"
#include <iostream>
#include <sstream>

dTalk::dTalk(StreamSocket& socket) :
_socket(socket),
_pBuffer(new char[BUFFER_SIZE]) {
}

bool dTalk::rpc(int DCODE) {
	std::string buffer = format("%c%c%c", (char) DCODE, (char) 0, (char) 0);
	_socket.sendBytes(buffer.c_str(), buffer.size());
	return true;
}

bool dTalk::rpc(int DCODE, std::string data) {
	if(data.empty())
	{
		return rpc(DCODE);
	}
	int block_sz(10240);
	std::stringstream ss;
	char sbuffer[block_sz]; // stream buffer
	std::string buffer; // data buffer
	dnet_size size;

	// dump data into stream
	ss << data;
	
	// iterate through the stream and send the data
	while (ss.getline(sbuffer, block_sz)) {
		buffer = sbuffer;
		size.Int = buffer.size();

		// header must be sent as a word.
		buffer = format("%c%c%c%s", (char) DCODE, (char) size.Index[0], (char) size.Index[1], buffer);

		// send payload
		_socket.sendBytes(buffer.c_str(), buffer.size());
	}

	return true;
}

bool dTalk::rpc(int DCODE, int data) {
	dnet_size size;
	size.Int = format("%d", data).size();
	
	// header must be sent as a word.
	std::string buffer = format("%c%c%d", (char) DCODE, (char) size.Index[0], (char) size.Index[1], data);
	_socket.sendBytes(buffer.c_str(), buffer.size());
	return true;
}

bool dTalk::send(char* buffer, int size) {
	_socket.sendBytes(buffer, size);
	return true;
}

bool dTalk::send(std::string buffer) {
	_socket.sendBytes(buffer.c_str(), buffer.size());
	return true;
}

void dTalk::zero_char(char* buffer) {
	for(int i=0; i<BUFFER_SIZE; i++)
	{
		buffer[i] = 0;
	}
}

bool dTalk::send_file(std::string filename) {
	// This method is NOT binary safe
		
	// Make sure the file exists, otherwise don't send
	if(filename.empty() || !File(filename).exists())
	{
		return false;
	}
	
	// setup some vars
	FileInputStream fis(filename);
	Path file(filename);
	std::string buffer; // data buffer
	std::string line;
	dnet_size size;
		
	// get payload
	size.Int = file.getFileName().size();
	buffer = format("%c%c%c%s", (char) DCODE_FILE_NAME, (char) size.Index[0], (char) size.Index[1], file.getFileName());
	// send payload
	_socket.sendBytes(buffer.c_str(), buffer.size());
	
	// zero out _pBuffer before working
	zero_char(_pBuffer);
	
	// get file contents and send
	while(fis.read(_pBuffer, BUFFER_SIZE-1))
	{
		line = _pBuffer;
		size.Int = strlen(_pBuffer);
		//std::cout << "send size pB " << strlen(_pBuffer) << std::endl;
		//std::cout << "send size sI " << size.Int << std::endl;
		//std::cout << " buf size " << line.length() << std::endl;
		//buffer = format("%c%c%c%s", (char) DCODE_FILE_DATA, (char) size.Index[0], (char) size.Index[1], _pBuffer);
		buffer = format("%c%c%c%s", (char) DCODE_FILE_DATA, (char) size.Index[0], (char) size.Index[1], line);
		//std::cout << "DEBUG " << _pBuffer << std::endl;
		//std::cout << "DEBUG " << buffer.length() << std::endl;
		_socket.sendBytes(buffer.c_str(), buffer.size());
		// reset buffer before continuing
		zero_char(_pBuffer);
		line.clear();
	}
	// send last buffer
	size.Int = strlen(_pBuffer);
	line = _pBuffer;
	buffer = format("%c%c%c%s", (char) DCODE_FILE_DATA, (char) size.Index[0], (char) size.Index[1], line);
	_socket.sendBytes(buffer.c_str(), buffer.size());
	
	// close transfer
	rpc(DCODE_FILE_EOF);
	return true;
}

bool dTalk::send_text_as_file(std::string filename, std::string text) {
	// This method is NOT binary safe
	//std::cout << filename << std::endl; return false;
	// setup some vars
	std::string buffer; // data buffer
	std::string line;
	dnet_size size;
	
	// setup input stream
	MemoryInputStream mis(text.data(), text.size());
	
	// get payload
	size.Int = filename.size();
	buffer = format("%c%c%c%s", (char) DCODE_FILE_NAME, (char) size.Index[0], (char) size.Index[1], filename);
	// send payload
	_socket.sendBytes(buffer.c_str(), buffer.size());
	
	// clear buffer before looping
	zero_char(_pBuffer);
	
	// get file contents and send
	while(mis.read(_pBuffer, BUFFER_SIZE-1))
	{
		line = _pBuffer;
		size.Int = strlen(_pBuffer);
		
		//std::cout << "pBuf " << _pBuffer << std::endl;
		//std::cout << "send size pB " << strlen(_pBuffer) << std::endl;
		//std::cout << "send size sI " << size.Int << std::endl;
		//std::cout << " buf size " << line.length() << std::endl;

		buffer = format("%c%c%c%s", (char) DCODE_FILE_DATA, (char) size.Index[0], (char) size.Index[1], line);
		//std::cout << "DEBUG " << buffer.length() << std::endl;
		//std::cout << "DEBUG " << buffer << std::endl << std::endl;
		
		_socket.sendBytes(buffer.c_str(), buffer.size());
		
		// reset buffer before continuing
		zero_char(_pBuffer);
		line.clear();
	}
	
	// send last buffer
	size.Int = strlen(_pBuffer);
	line = _pBuffer;
	buffer = format("%c%c%c%s", (char) DCODE_FILE_DATA, (char) size.Index[0], (char) size.Index[1], line);
	_socket.sendBytes(buffer.c_str(), buffer.size());
	
	// close transfer
	rpc(DCODE_FILE_EOF);
	return true;
}

bool dTalk::save_file(std::string savepath, std::string filename) {
	// This method is NOT binary safe

	Path file(savepath+'/'+filename);
	FileOutputStream fos(file.toString());
	if(!fos) return false;
	if(!receive())
	{
		std::cout << "|bad packet!" << std::endl;
		return false;
	}
	while (dcode() == DCODE_FILE_DATA)
	{
		fos << data();
		
		if(!receive())
		{
			std::cout << "|bad packet!" << std::endl;
			return false;
		}
	}
	fos.close();
	if(!file.isFile()) return false;
	
	return true;
}

bool dTalk::receive() {
	int n;
	dnet_size size;

	// clear the data buffer before getting new payload
	_data.clear();
	
	// get control data
	n = _socket.receiveBytes(_pBuffer, 3);
	
	// make sure data is sane
	if (n > 0) {
		_dcode = _pBuffer[0];
		size.Int = 0; // initialize the var
		size.Index[0] = _pBuffer[1];
		size.Index[1] = _pBuffer[2];

		// check for payload
		if (size.Int > 0)
		{
			// get payload
			n = _socket.receiveBytes(_pBuffer, size.Int);
			
			// assign payload to data container
			_data.assign(_pBuffer, _pBuffer + n);
		}
	} else return false;
	return true;
}

bool dTalk::receive(int size) {
	char* _pBuffer;
	int n = _socket.receiveBytes(_pBuffer, size);
	if (n == 0) return false;
	return true;
}

int dTalk::dcode() {
	return _dcode;
}

std::string dTalk::data() {
	//TODO make this binary safe
	// should return data vector
	return std::string(_data.begin(), _data.end());
}

int dTalk::size() {
	return _data.size();
}
