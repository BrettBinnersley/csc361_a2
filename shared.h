// --- Header format
// Hash: <Data Hash> or all 0's
// Magic: UVicCSc361 - Always the same
// Type: type DAT Data packet
// ACK Acknowledgment packet (SYN, FIN, RST)
// Sequence: seqno e.g., 0 byte sequence number
// Acknowledgment: ackno e.g., 900 byte acknowledgment number
// Payload: length e.g., 900 RDP payload length in bytes
// Window: size e.g., 10240 RDP window size in bytes
// (an empty line) the end of the RDP header

//Header format
//Hash [20bytes - padded with 0's]
//Magic (strLen) \endLine
//type \endLine
//SeqNo \endLine
//ackNo \endLine
//length \endLine
//size \endLine
// \endLine ->Signals the end of the header



//BONUS: Hashing used for corrupt packets

#ifndef SHARED_HAS_BEEN_INCLUDED_
#define SHARED_HAS_BEEN_INCLUDED_

  //Shared includes
  #include <time.h>
  #include <stdio.h>
  #include <sys/time.h>
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <stdlib.h>
  #include <unistd.h>
  #include <string>
  #include <string.h>
  #include <iostream>
  #include <sys/stat.h>
  #include <sstream>
  #include <list> //Seq numbers we have sent
  #include <map> //Used to map packetID to data (if they get received out of order...)
  #include <ctime>

  //Constants
  #define FALSE 0
  #define TRUE 1
  #define MAX_STR_LEN 1024
  #define MAX_BUFFER_LENGTH 1024 //Maximum packet size ( -1 )
  #define MAX_HEADER_SIZE 120     //Arbitrary maximum size for a header
  #define MAX_PACKET_DATA MAX_BUFFER_LENGTH - MAX_HEADER_SIZE
  #define MAX_WINDOW_SIZE (MAX_BUFFER_LENGTH * 12) //Assume we can handle a maximum of 7 packets out at a time.
  #define MAX_SYN_TIMEOUT_ATTEMPTS 8 //maximum times the program will re-try things without timeout issues.


  //Header fields
  #define NULLHASH "0000000000"
  #define MAGIC "UVicCSc361"

  //Type part of the message (3 bits long)
  #define DAT 0
  #define ACK 1
  #define SYN 2
  #define FIN 3
  #define RST 4

  #define event_send "s"
  #define event_resent "S"
  #define event_receive "r"
  #define event_receiveAgain "R"

  //Msc Shared functions
  void LogMessageWithTime(const char* message);

  //Input from the socket, or from the keyboard
  int isReadyToReadInput(int socketDesc, int secondTO = 2, int microSecTO = 0);

  unsigned int hashString(const std::string& str);

  //Reading header information
  void SetReadBuffer(char* buffer);
  std::string ReadHeaderField();
  std::string ReadBufferHash();
  std::string ReadBufferLength(int length);
  std::string typeToString(int type);
  std::string numberToPaddedHashString(unsigned int number);
#endif


