#include <windows.h>
#include <process.h>
#include <stdio.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <iterator>
#include <map>
using namespace std;

#pragma comment(lib, "Ws2_32.lib")


// Function to prepare to receive connections for a service
static int passiveTCP(string service, int qlen);

// Function to read the contents of the file containing username and passwords
bool fetchFileData(string filename);

// Function to authenticate user based on username and password
bool Authenticate(string userPass);

// Function to parse username from username=password
string parseUsername(string user_and_pass);

// Function to find client username
string findUser(SOCKET s);


//buffer to hold username & passwords
stringstream buffer;

//to hold online connected client [socket, username] pairs
map<int, string> usernames;

/**/
/*
NAME

		server - to receive connections from clients and share their messages with all other clients in a chatroom

SYNOPSIS

		server [service]

DESCRIPTION

		The server process will receive connections from client processes, authenticates clients
		using username and password, and adds them to a chatroom where they can communicate with
		all other connected authenticated clients
*/
/**/
int main()
{
	
	// Make sure that we can connect:
	WORD wVersionRequested;
	WSADATA wsaData;
	int err;

	// A macro to create a word with low order and high order bytes as specified.
	wVersionRequested = MAKEWORD(1, 1);
	err = WSAStartup(wVersionRequested, &wsaData);

	if (err != 0)
	{
		// Tell the user that we couldn't find a useable winsock.dll.
		cerr << "Could not find useable DLL" << endl;
		return 1;
	}

	// Confirm that the Windows Sockets DLL supports version 1.1.
	if (LOBYTE(wsaData.wVersion) != 1 || HIBYTE(wsaData.wVersion) != 1)
	{
		// Tell the user that we couldn't find a useable winsock DLL.
		cerr << "Could not find useable DLL" << endl;

		// Terminates the use of the library.
		WSACleanup();

		return 1;
	}


	// The default port number for the service to be provided.
	string service = "7007";

	//the name of file where username and passwords are stored
	string filename = "duck.txt";

	//opening the file where user & passes are stored every time someone tries to log in is expensive
	//so instead we read file data to stringstream object just once and before starting up the sever

	//read in all username & passwords
	if (!fetchFileData(filename))
		return 1;

	//connect to the server process
	SOCKET ls = passiveTCP(service, 5);

	//to hold connected client connections
	vector<SOCKET> soc;

	//to hold authenticated client connections
	vector<SOCKET> authen_soc;

	//the maximum number of characters for a username/pass combination (i.e. username=password)
	const int user_pass_MAXSIZE = 20;

	string welcome_msg = "Welcome to the Chatroom Server!\n";
	const int welcome_msg_size = (int)welcome_msg.length();
	char welcome_buff[32 + 1];
	strcpy_s(welcome_buff, welcome_msg.c_str());

	string authen_msg = "Enter your username and password in the following format: username=password\n";
	const int authen_msg_size = (int)authen_msg.length();
	char auth_buff[76 + 1];
	strcpy_s(auth_buff, authen_msg.c_str());

	string login_sucess = "You successfully logged in! Have fun chatting...\n";
	const int login_sucess_size = (int)login_sucess.length();
	char success_buff[49 + 1];
	strcpy_s(success_buff, login_sucess.c_str());

	string login_fail = "Log in failed! Try again...\n";
	const int login_fail_msg_size = (int)login_fail.length();
	char login_fail_buff[28 + 1];
	strcpy_s(login_fail_buff, login_fail.c_str());

	/* Wait for connections from clients. */
	while (true)
	{
		// Address of the client.
		struct sockaddr fsin;

		// Length of client address.
		int alen = sizeof(sockaddr_in);

		fd_set readmap; //all the sockets waiting for data
		FD_ZERO(&readmap); //setting read map to zero
		FD_SET(ls, &readmap); //putting listening socket in readmap
		SOCKET bigSoc = ls; //include the listening socket when computing largest socket

		for (int is = 0; is < (int)soc.size(); is++)
		{
			//when you have all data needed, it gets set to zero
			if (soc[is] == 0)
				continue;

			//take the value of the socket into the read map
			FD_SET(soc[is], &readmap);

			//we need the biggest value of the socket
			if ((int)soc[is] > bigSoc)
				bigSoc = soc[is];
		}

		int nsoc = select(bigSoc + 1, &readmap, NULL, NULL, NULL);

		if (nsoc == SOCKET_ERROR)
		{
			cerr << "Error in select" << endl;
			return 1;
		}

		//if we have a pending connection
		if (FD_ISSET(ls, &readmap))
		{
			//accepting pending connection
			SOCKET s = accept(ls, &fsin, &alen);

			if (s == INVALID_SOCKET)
			{
				int errorcode = WSAGetLastError();
				cerr << "Accept failed, error = " << errorcode << endl;
				continue;
			}

			//send welcome message
			if (send(s, welcome_buff, welcome_msg_size, 0) == INVALID_SOCKET)
			{
				int errorcode = WSAGetLastError();
				cerr << "send to client failed: " << errorcode << endl;
				closesocket(s);
				continue;
			}

			//ask client for username & password
			if (send(s, auth_buff, authen_msg_size, 0) == INVALID_SOCKET)
			{
				int errorcode = WSAGetLastError();
				cerr << "send to client failed: " << errorcode << endl;
				closesocket(s);
				continue;
			}

			//record the socket number of the client connection
			soc.push_back(s);

			continue;
		}


		for (int is = 0; is < (int)soc.size(); is++)
		{
			//if socket is in the readmap (if we recieve an action from a socket)
			if (FD_ISSET(soc[is], &readmap))
			{
				//if new user --> authenticate
				//else --> skip authentication

				bool authenicated_socket = false;
				for (int i = 0; i < (int)authen_soc.size(); i++)
				{
					if (authen_soc[i] == soc[is])
						authenicated_socket = true;
				}

				//if we have a new client
				if (!authenicated_socket)
				{
					//to read user & pass from client
					char user_pass_buff[user_pass_MAXSIZE + 1];

					size_t nb1 = recv(soc[is], user_pass_buff, user_pass_MAXSIZE, 0);

					//if there was a disconnect (nb = 0) or there is an error (nb < 0)
					if (nb1 <= 0)
					{
						if (nb1 < 0)
							cerr << "recv error for server soc: " << soc[is] << endl;

						closesocket(soc[is]);

						soc.erase(soc.begin() + is);
						is--;

						continue;
					}

					//holds string format of "username=password"
					string user_and_pass;

					//we do nb1-2 because the last two bytes are empty characters
					for (int i = 0; i < (int)nb1 - 2; i++)
						user_and_pass += user_pass_buff[i];

					//make sure client username & password matches one we have on file
					if (Authenticate(user_and_pass))
					{
						authen_soc.push_back(soc[is]);

						//parse the username from username=password
						string username = parseUsername(user_and_pass);

						//insert socket number and associated username
						usernames.insert(pair<int, string>(soc[is], username));

						//send successful login message
						if (send(soc[is], success_buff, login_sucess_size, 0) == INVALID_SOCKET)
						{
							int errorcode = WSAGetLastError();
							cerr << "send to client failed: " << errorcode << endl;
							
                            closesocket(soc[is]);
							continue;
						}
					}

					else
					{
						//send login failed message, ask for username and password again!
						if (send(soc[is], login_fail_buff, login_fail_msg_size, 0) == INVALID_SOCKET)
						{
							int errorcode = WSAGetLastError();
							cerr << "send to client failed: " << errorcode << endl;
							closesocket(soc[is]);
							continue;
						}
					}
				}


				else
				{
					//to read and send the message from each client
					const int MAXSIZE = 100;
					char buff[MAXSIZE + 1];

					size_t nb = recv(soc[is], buff, MAXSIZE, 0);

					//if there was a disconnect (nb = 0) or there is an error (nb < 0)
					if (nb <= 0)
					{
						if (nb < 0)
							cerr << "recv error for server soc: " << soc[is] << endl;

						closesocket(soc[is]);

						soc.erase(soc.begin() + is);
						is--;

						continue;
					}

					//find the username that matches the sender's socket
					string sender_username = findUser(soc[is]);

					char username_buff[50 + 1];
					strcpy_s(username_buff, sender_username.c_str());

					//null-terminate the response
					buff[nb] = '\0';

					//going through only authenticated clients
					for (int n = 0; n < (int)authen_soc.size(); n++)
					{

						//send sender's username to all authenticated clients
						if (send(authen_soc[n], username_buff, sender_username.length(), 0) == INVALID_SOCKET)
						{
							int errorcode = WSAGetLastError();
							cerr << "send to client failed: " << errorcode << endl;
							closesocket(authen_soc[n]);
							continue;
						}

						//send sender's message to all authenticated clients
						if (send(authen_soc[n], buff, nb, 0) == INVALID_SOCKET)
						{
							int errorcode = WSAGetLastError();
							cerr << "send to client failed: " << errorcode << endl;
							closesocket(authen_soc[n]);
							continue;
						}
					}
				}
			}
		}
	}

	return 0;
}
/*
NAME

passiveTCP - allocates and binds a server socket using TCP

SYNOPSYS
*/
static int passiveTCP(string service, int qlen)
/*
DESCRIPTION

		This function will create a socket, bind the server "service" to
		it and perpare to listen for connections with a queue length of
		"qlen".

RETURNS

		This function returns the listening socket if it is successful and
		does not return if it fails.
*/
{
	struct servent* pse; /* Points to service information. */
	struct sockaddr_in sin; /* Internet endpoint address. */

	/* Create an end point address for this computer. */
	memset((char*)&sin, 0, sizeof(sockaddr_in));
	sin.sin_family = AF_INET; //IP Verion 4
	sin.sin_addr.s_addr = INADDR_ANY;

	/* Get the port number for the service. */
	if ((pse = getservbyname(service.c_str(), "tcp")) != NULL)
	{
		sin.sin_port = (u_short)pse->s_port;
	}

	else if ((sin.sin_port = htons((u_short)stoi(service))) == 0)
	{
		cerr << "Bad Port number/service specified: " << service << endl;;
		exit(1);
	}

	/* Allocate a socket. */
	SOCKET ls;

	//the deault is tcp so we use "0"
	if ((ls = socket(PF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET)
	{
		//they might have run out of sockets
		int errorcode = WSAGetLastError();
		cerr << "socket call failed: " << errorcode << endl;
		exit(1);
	}

	/* Bind the address to the socket. */
	if (::bind(ls, (struct sockaddr*)&sin, sizeof(sin)) == INVALID_SOCKET)
	{
		int errorcode = WSAGetLastError();
		cerr << "bind call failed: " << errorcode << endl;
		exit(1);
	}

	// Indicate that we are ready to wait for connects.
	if (listen(ls, qlen) == INVALID_SOCKET)
	{
		int errorcode = WSAGetLastError();
		cerr << "listen call failed: " << errorcode << endl;
		exit(1);
	}

	// Return the listening socket
	return ls;
}



/*
NAME

		Authenticate - checks to see whether a set of username and password match is present

SYNOPSYS

		bool Authenticate(string userPass_to_check);

DESCRIPTION

		This function will look for a set of username and password
		in the file content that matches what the user has provided
		in an attempt to authenticate a user. The username and password
		are stored in the file in the following format:
		username=password

RETURNS

		This function returns True if user is authenticated and returns False otherwise
*/

bool Authenticate(string userPass_to_check)
{
	string userPass_fromFile;
	bool found = false;

	while (getline(buffer, userPass_fromFile))
	{
		if (userPass_to_check == userPass_fromFile)
			found = true;
	}

	//go back to beginning of stringstream object for next user-pass check
	buffer.clear();
	buffer.seekg(0, ios::beg);

	if (found)
		return true;

	return false;
}



/*
NAME

		FetchFileData - reads the content of file containing username and passwords

SYNOPSYS

		bool FetchFileData(string filename);

DESCRIPTION

		This function opens a file and reads its content into a stringstream object
		that has a global scope.

RETURNS

		This function returns True if file is found, and False otherwise
*/

bool fetchFileData(string filename)
{
	ifstream MyReadFile(filename);

	if (!MyReadFile)
	{
		cout << "Username & Password File not found" << endl;
		return false;
	}

	MyReadFile.open(filename);

	//read all username and passwords from file to buffer
	buffer << MyReadFile.rdbuf();

	MyReadFile.close();

	return true;
}



/*
NAME

		findUser - finds the username of an authenticated connected client given its socket

SYNOPSYS

		string findUser(SOCKET s);

DESCRIPTION

		This function recieves a socket (s) and looks for the username that matches s.
		It also appends ": " to the end of username for chatroom display purposes

RETURNS

		This function returns the username with ": " attached to the end of it
*/

string findUser(SOCKET s)
{
	map<int, string>::iterator it;
	it = usernames.find(s);

	string sender_username = it->second;

	sender_username += ": ";

	return sender_username;
}



/*
NAME

		parseUsername - parses the username from "username=password" string

SYNOPSYS

		string parseUsername(string user_and_pass);

DESCRIPTION

		This function copies over everything before the equal sign in the
		"username=password" string, this is the part that contains the
		client username

RETURNS

		This function returns the parsed client username
*/

string parseUsername(string user_and_pass)
{
	//parse username from username=password
	string username;

	for (int i = 0; i < (int)user_and_pass.length(); i++)
	{
		if (user_and_pass[i] != '=')
			username += user_and_pass[i];

		else
			break;
	}

	return username;
}
