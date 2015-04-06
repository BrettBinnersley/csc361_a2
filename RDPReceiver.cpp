
/*
Brett Binnersley, V00776751
Csc 361 Assignment #2

TODO:

Fix ACK on the receiver
Fix timeout

BONUS: Hashing used for corrupt packets
*/

#include "shared.h"
using namespace std;
timeval lastTimeMessageReceived; // 8second timeout - received nothing implies connection terminated.


//Forward Decl's
int sendMessageToSender(int type, int SeqNo, int ackNo, int length, int windowSize);
void attemptToWriteDataToFile();
void printTransferInfo();

//Global Data
int socketDesc = -1;
socklen_t receiverLength = 0;
socklen_t senderLength = 0;
struct sockaddr_in receiverAddr;
struct sockaddr_in senderAddr;
char sendMessageToReceiver[MAX_STR_LEN];

int currentSeqNumber = 0;  //Not used at the moment
int lastSeqNumber = 0;     //Last Seq number successfully

int connectionEstablished = FALSE;
int tryingToCloseConnection = FALSE;
int closeEverything = FALSE;
int hasInitConnection = FALSE;
int firstDataPacket = TRUE;
FILE* receivedFile = NULL;
std::map< int, std::string > receivedFileData; //Data we received, but was out of order gets added to this.
char receiver_file_name[MAX_STR_LEN];

char receiver_ip[MAX_STR_LEN];
char receiver_port[MAX_STR_LEN];
int windowSizeRemaining = MAX_WINDOW_SIZE;
int lastSeqNumberLength = 0;
time_t startTime;


//Variables to keep track of information transportation
int total_data_bytes_received = 0;
int unique_data_bytes_received = 0;
int total_data_packets_received = 0;
int unique_data_packets_received = 0;
int SYN_packets_received = 0;
int FIN_packets_received = 0;
int RST_packets_received = 0;
int ACK_packets_sent = 0;
int RST_packets_sent = 0;


//Main
int main(int argc, char *argv[])
{
   startTime = time(NULL);
   srand (time(NULL));

   /*CREATE THE RECEIVER*/
   char   buffer[MAX_BUFFER_LENGTH];
   char   sendBuffer[MAX_BUFFER_LENGTH];
   char   receiver[MAX_BUFFER_LENGTH];

   if (argc < 3)
   {
      perror("To run: rdpr receiver_ip receiver_port receiver_file_name\n");
      return 0;
   }

   //Initialize the socket
   strcpy(receiver_ip, argv[1]);
   strcpy(receiver_port, argv[2]);
   strcpy(receiver_file_name, argv[3]);

   socketDesc = socket(AF_INET, SOCK_DGRAM, 0);
   if (socketDesc < 0)
   {
      perror("Receiver: socket() failed\n");
      return socketDesc;
   }

   memset(&receiverAddr, 0, sizeof(receiverAddr));
   receiverAddr.sin_family      = AF_INET;
   receiverAddr.sin_port        = htons(atoi(receiver_port));
   receiverAddr.sin_addr.s_addr = inet_addr(receiver_ip);
   senderLength = sizeof(senderAddr);
   receiverLength = sizeof(receiverAddr);

   //Failed to create receiver
   //Bind the socket with the address information and gracefully handle exceptions
   int onReuseInteger = 1;
   if (setsockopt(socketDesc, SOL_SOCKET, SO_REUSEADDR, &onReuseInteger, sizeof(onReuseInteger)) < 0)
   {
      perror("Error setting SO_REUSEADDR for socket\n");
      return 0;
   }
   int bindSuccess = bind(socketDesc, (struct sockaddr* ) &receiverAddr, sizeof(receiverAddr));
   if (bindSuccess < 0)
   {
    close(socketDesc);
    perror("Receiver: Error on binding\n");
    return bindSuccess;
   }

   /*******************
   LISTEN FOR A MESSAGE
   ********************/
   printf("Receiver: waiting for messages\n");
   while(true)
   {
      int inputVal = isReadyToReadInput(socketDesc, 0, 100);
      if(inputVal == 1 || inputVal < 0) //Q was pressed or an error occured
      {
         printf("Receiver: Q pressed or error encountered\n");
         exit(1);
         return 0;
      }

      //Timeout happended. This could be cause to throw a RST
      if(inputVal == 0)
      {
         if(connectionEstablished)
         {
            if(tryingToCloseConnection == TRUE)
            {
               break; //CLOSE THE CONNECTION. Client has not received anything from the server. Assume
            }
               //todo: logic here is to throw a RST, else connection will hang. Bad mm'kay

            timeval tmp_time;
            gettimeofday(&tmp_time, NULL);
            if(abs(tmp_time.tv_sec - lastTimeMessageReceived.tv_sec) >= 12) //9 second timeout
            {
               break; //Connection timeout
            }
         }

         //printf("Timeout\n");
         continue;
      }

      //Send information to ClientAddress
      char buffer[MAX_BUFFER_LENGTH]; //data buffer
      int numberOfBytes = recvfrom(socketDesc, buffer, MAX_BUFFER_LENGTH - 1 , 0, (struct sockaddr *)&senderAddr, &senderLength);
      if (numberOfBytes == -1)
      {
         perror("Receiver: Error on recvfrom()!\n");
         return -1;
      }
      gettimeofday(&lastTimeMessageReceived, NULL);

      buffer[numberOfBytes] = '\0'; //Null terminate the buffer. [If it is not already null-terminated]
      SetReadBuffer(buffer);
      std::string hash = ReadBufferHash(); //Read in the hash
      std::string magic = ReadHeaderField();
      std::string type = ReadHeaderField();  //Note this is sent as an INT, not as "SYN" or "FIN"
      std::string seqNo = ReadHeaderField(); //Seq number on the SENDER SIDE
      std::string ackNo = ReadHeaderField(); //LAST Seq number received from the receiver SIDE on the server
      std::string length = ReadHeaderField();
      std::string windowSize = ReadHeaderField();
      std::string endHeader = ReadHeaderField(); //Should be length of 0

      //Message did not contain magic. BAD!
      if(magic.compare(MAGIC) != 0)
      {
         printf("Receiver: Bad magic!\n");
         continue;
      }
      if(endHeader.length() != 0) //Bad Header.
      {
         printf("Receiver: Incorrect header format\n");
         continue;
      }

      //Parse the buffer
      int typeAsInt = atoi(type.c_str());
      int lengthAsInt = atoi(length.c_str());
      int seqNoAsInt = atoi(seqNo.c_str());
      int ackNoAsInt = atoi(ackNo.c_str());

      //Define char arrays for outputting stuff
      std::string event_type;
      std::string sip;
      std::string spt;
      std::string dip;
      std::string hashedData;
      int dpt;

      sip = receiver_ip;                     //The IP I am listening on
      spt = receiver_port;                   //The port I am listening on
      dip = inet_ntoa(senderAddr.sin_addr);  //Where I will send my reply to (Where I received the message from)
      dpt = ntohs(senderAddr.sin_port);      //Where I will send my reply to (Where I received the message from)

      //Do something with the data from the sender
      // sendMessageToSender(char* type, char* SeqNo, char* ackNo, char* length, char* windowSize)
      // We are going to ignore windowSize until later
      std::string data;
      switch(typeAsInt)
      {

         case DAT:
            event_type = event_receive;
            total_data_packets_received += 1;
            data = ReadBufferLength(lengthAsInt);
            hashedData = numberToPaddedHashString(hashString(data));
            if(hash.compare(hashedData) == 0)
            {
               if(firstDataPacket == TRUE)
               {
                  //Received data from the sender. If it's the next part of the file, we can write it.
                  total_data_bytes_received += data.length();

                  if(seqNoAsInt == lastSeqNumber + 1)
                  {
                     lastSeqNumber = seqNoAsInt;
                     lastSeqNumberLength = lengthAsInt;
                     firstDataPacket = FALSE;
                  }

                  if(firstDataPacket == FALSE)
                  {
                     if(receivedFileData.find(seqNoAsInt) == receivedFileData.end())
                     {
                        receivedFileData.insert(std::pair<int, std::string> (seqNoAsInt, data)); //Data not found in the map. Add it.
                        unique_data_packets_received += 1;
                        unique_data_bytes_received += data.length();
                     }
                     else
                     {
                        event_type = event_receiveAgain; //Else we received the same message twice. Our ack must not have gotten thru.
                     }
                  }
               }
               else
               {
                  //Received data from the sender. If it's the next part of the file, we can write it.
                  total_data_bytes_received += data.length();
                  if(seqNoAsInt == lastSeqNumber + lastSeqNumberLength)
                  {
                     lastSeqNumber = seqNoAsInt;
                     lastSeqNumberLength = lengthAsInt;
                  }

                  if(receivedFileData.find(seqNoAsInt) == receivedFileData.end())
                  {
                     receivedFileData.insert(std::pair<int, std::string> (seqNoAsInt, data)); //Data not found in the map. Add it.
                     unique_data_packets_received += 1;
                     unique_data_bytes_received += data.length();
                  }
                  else
                  {
                     event_type = event_receiveAgain; //Else we received the same message twice. Our ack must not have gotten thru.
                  }
               }

            }
            else
            {
               std::string corruption("corrupted packet");
               LogMessageWithTime(corruption.c_str());
            }


            //Send them an ack back.
            sendMessageToSender(ACK, currentSeqNumber, lastSeqNumber, 0, lengthAsInt);
         break;

         case SYN:
            event_type = event_receive;
            SYN_packets_received += 1;
            if(hash.compare(NULLHASH) == 0)
            {
               //Conection is established. We open the file for writing into
               if(connectionEstablished == FALSE)
               {
                  connectionEstablished = TRUE;

                  //Start the timer when a SYN is received
                  startTime = time(NULL);
               }
               else
               {
                  event_type = event_receiveAgain; //We received a SYN request more than once
               }

               //Send back a reply that yes I did receive a syn request.
               lastSeqNumber = seqNoAsInt;
            }
            else
            {
               std::string corruption("corrupted packet");
               LogMessageWithTime(corruption.c_str());
            }
            sendMessageToSender(ACK, currentSeqNumber, lastSeqNumber, 0, MAX_WINDOW_SIZE);
         break;

         case FIN:
            event_type = event_receive;
            FIN_packets_received += 1;
            if(hash.compare(NULLHASH) == 0)
            {
               //Close the connection.
               sendMessageToSender(ACK, currentSeqNumber, seqNoAsInt, 0, lengthAsInt);
               if(tryingToCloseConnection == FALSE)
               {
                  tryingToCloseConnection = TRUE;
               }
               else
               {
                  event_type = event_receiveAgain; //We received a FIN request more than once
               }
            }
            else
            {
               std::string corruption("corrupted packet");
               LogMessageWithTime(corruption.c_str());
            }
         break;

         case ACK: //NOT USED. Terminate if encountered
            closeEverything = TRUE;
         break;
         case RST:
            RST_packets_received += 1;
            closeEverything = TRUE; //Terminate connection
         break;


         default:
            printf("Receiver: Bad Type\n");
         break;

      }

      std::string typeAsWord = typeToString(typeAsInt);
      //Log the message
      char message[MAX_STR_LEN];
      snprintf(message, sizeof message, "%s %s:%s %s:%d %s %s %s",
         event_type.c_str(),
         sip.c_str(),
         spt.c_str(),
         dip.c_str(),
         dpt,
         typeAsWord.c_str(), // CHANGE THIS FROM [0/1/2/3] to [ACK/SYN/etc]
         seqNo.c_str(),
         length.c_str());
      LogMessageWithTime(message);
      if(strlen(sendMessageToReceiver) != 0)
      {
         LogMessageWithTime(sendMessageToReceiver);
         memset(sendMessageToReceiver, '\0', MAX_STR_LEN);
      }
      memset(&sendBuffer, 0, MAX_BUFFER_LENGTH);

      //Received an ACK to my finish. Connection should be closed!
      if(closeEverything == TRUE)
      {
         break;
      }
   }

   attemptToWriteDataToFile();

   /* CLOSE THE RECEIVER */
   if (socketDesc != -1)
      close(socketDesc);

   //printTransferInfo
   printTransferInfo();


   return 0;
}

//Function Decl's

int sendMessageToSender(int type, int SeqNo, int ackNo, int length, int windowSize)
{
   char tbuffer[MAX_BUFFER_LENGTH];
   char magic[] = MAGIC;
   snprintf(tbuffer, sizeof tbuffer, "%s%s\n%d\n%d\n%d\n%d\n%d\n\n", NULLHASH, magic, type, SeqNo, ackNo, length, windowSize);

   //Send off the message
   int numbytes;
   numbytes = sendto(socketDesc, &tbuffer, strlen(tbuffer), 0, (struct sockaddr *)&senderAddr, senderLength);
   if(numbytes < 0)
   {
      printf("Error: sendMessageToSender() - no data sent\n");
   }

   //Log the information that was sent.
   //sendMessageToSender(ACK, currentSeqNumber, lastSeqNumber, 0, 0);
   std::string typeAsWord = typeToString(type);
   std::string event_type = event_send; //CHANGE THIS SO THAT WE TAKE INTO ACCOUNT RE-SENDS
   std::string destIp = inet_ntoa(senderAddr.sin_addr);
   snprintf(sendMessageToReceiver, sizeof sendMessageToReceiver, "%s %s:%s %s:%d %s %d %d",
      event_type.c_str(),
      receiver_ip,
      receiver_port,
      destIp.c_str(),
      ntohs(senderAddr.sin_port),
      typeAsWord.c_str(),
      ackNo,
      windowSize);
   if(type == ACK) ACK_packets_sent += 1;
   if(type == RST) RST_packets_sent += 1;
   return numbytes;
}

void attemptToWriteDataToFile()
{
   if(RST_packets_received != 0) return;
   receivedFile = fopen(receiver_file_name, "wb");

   //Write and close the file
   if(receivedFile != NULL)
   {
      for( std::map<int, std::string>::iterator iter = receivedFileData.begin(); iter != receivedFileData.end(); ++iter )
      {
          fwrite (iter->second.c_str(), sizeof(char), iter->second.length(), receivedFile);
          //printf("%d\n", (int)iter->first);
      }
      fclose(receivedFile);
   }
}

void printTransferInfo()
{
   time_t endTime = time(NULL) - startTime;
   printf("total data bytes received: %d\n", total_data_bytes_received);
   printf("unique data bytes received: %d\n", unique_data_bytes_received);
   printf("total data packets received: %d\n", total_data_packets_received);
   printf("unique data packets received: %d\n", unique_data_packets_received);
   printf("SYN packets received: %d\n", SYN_packets_received);
   printf("FIN packets received: %d\n", FIN_packets_received);
   printf("RST packets received: %d\n", RST_packets_received);
   printf("ACK packets sent: %d\n", ACK_packets_sent);
   printf("RST packets sent: %d\n", RST_packets_sent);
   printf("total time duration (second): %lu\n", endTime); //TODO: Make time duration in seconds
}





