
/*
Brett Binnersley, V00776751
Csc 361 Assignment #2


Sender - sends a file to the receiver

TODO:
FIX Window_size, make it actually work (so I don't spam 100 billion messages over udp)
track the resends, log a capital "S" with a re-send
all "todo" tags found in the program

BONUS: Hashing used for corrupt packets
*/

#include "shared.h"
using namespace std;


//Global data
int socketDesc = -1;
socklen_t senderLength = 0;
socklen_t receiverLength = 0;
struct sockaddr_in receiverAddr;
struct sockaddr_in senderAddr;

int startingSeqNumber = 0; //Used so we can do "relative operations"
int firstFileSeqNum = 0;
int currentSeqNumberSent = 0; //Last Seq number sent


int numberFileParts = 0;
int connectionEstablished = FALSE;
int tryingToCloseConnection = FALSE; //We start the sender by trying to connect to the receiver right away.
int closeEverything = FALSE;
int wholeFileHasBeenSent = FALSE;
int lastAckReceived = 0;   //Last ack we got from the receiver
int numDuplicatedPackets = 0;
int windowSizeOnReceiver = 0;
int lastFilePartSent = 0;
int initialWindowSizeReceiver = MAX_WINDOW_SIZE;
int totalFinSent = 0;
time_t startTime;

FILE* sendingFile = NULL;
std::map< int, std::string > fileData;
std::map< int, int> messageSentCounter;

timeval lastMessageSentTime;


//Logging variables
int total_data_bytes_sent = 0;
int unique_data_bytes_sent = 0;
int total_data_packets_sent = 0;
int unique_data_packets_sent = 0;
int SYN_packets_sent = 0;
int FIN_packets_sent = 0;
int RST_packets_sent = 0;
int ACK_packets_received = 0;
int RST_packets_received = 0;

int wholeFileBeenSentNotAcked = FALSE;
int wholeFileLastAckExpected = 0;

//Forwar Decl's
int sendMessageToReceiver(int type, int SeqNo, int ackNo, int length, int windowSize, std::string data = "");
int fileExists(char* relFilePath);
std::string itoStr(int x);
void printTransferInfo();

int main(int argc, char *argv[])
{
   startTime = time(NULL);
   srand (time(NULL));
   startingSeqNumber = rand() % 100000; //Randomly pick a starting sequence number (Assignment did not state range, so I chose 0-100,000)
   lastAckReceived = startingSeqNumber;
   currentSeqNumberSent = startingSeqNumber;

   char sender_ip[MAX_STR_LEN];     //Listen for messages from here
   char sender_port[MAX_STR_LEN];   //Listen for messages from here
   char receiver_ip[MAX_STR_LEN];   //Send messages to here (ie: syn)
   char receiver_port[MAX_STR_LEN]; //Send messages to here (ie: syn)
   char sender_file_name[MAX_STR_LEN]; //Send this file to the receiver

   if (argc < 5)
   {
      perror("To run: rdps sender_ip sender_port receiver_ip receiver_port sender_file_name\n");
      return 0;
   }

   strcpy(sender_ip, argv[1]);
   strcpy(sender_port, argv[2]);
   strcpy(receiver_ip, argv[3]);
   strcpy(receiver_port, argv[4]);
   strcpy(sender_file_name, argv[5]);

   //Ensure that the file exists, if it doesn't throw an error.
   if(fileExists(sender_file_name) == FALSE)
   {
      perror("Error: 'sender_file_name' does not exist\n");
      return 0;
   }

   sendingFile = fopen(sender_file_name, "rb");

   //Read the file into the map of data parts (so we can send & re-send the data when we need to)
   fseek (sendingFile , 0 , SEEK_END);
   long fileSize = ftell (sendingFile);
   rewind (sendingFile);

   char tbuffer[MAX_BUFFER_LENGTH];
   for(size_t x = 0; x<fileSize; x+=MAX_PACKET_DATA)
   {
      //Determine the number of bytes to send from the buffer. Smaller of [strLen - Place in array], [MAX_BUFFER_LEN]
      size_t bytesToRead = std::min(int(fileSize - x), MAX_PACKET_DATA);
      char tmpbuffer[bytesToRead];
      int result = fread (&tmpbuffer, 1, bytesToRead, sendingFile);
      fileData.insert(std::pair<int, std::string>(numberFileParts, std::string(tmpbuffer, bytesToRead)));
      numberFileParts += (int)bytesToRead; //[0, 936, 1872, .... 936X, 936X+Y], y is between 0 and 936
   }
   fclose(sendingFile);

   //Initialize the socket
   socketDesc = socket(AF_INET, SOCK_DGRAM, 0);
   if (socketDesc < 0)
   {
      perror("Sender: socket() failed\n");
      return socketDesc;
   }

   //Define the sender and receiver information
   memset(&receiverAddr, 0, sizeof(receiverAddr));
   receiverAddr.sin_family      = AF_INET;
   receiverAddr.sin_port        = htons(atoi(receiver_port));
   receiverAddr.sin_addr.s_addr = inet_addr(receiver_ip);
   memset(&senderAddr, 0, sizeof(senderAddr));
   senderAddr.sin_family      = AF_INET;
   senderAddr.sin_port        = htons(atoi(sender_port));
   senderAddr.sin_addr.s_addr = inet_addr(sender_ip);

   senderLength = sizeof(senderAddr);
   receiverLength = sizeof(receiverAddr);

   int onReuseInteger = 1;
   if (setsockopt(socketDesc, SOL_SOCKET, SO_REUSEADDR, &onReuseInteger, sizeof(onReuseInteger)) < 0)
   {
      perror("Error setting SO_REUSEADDR for socket\n");
      return 0;
   }
   int bindSuccess = bind(socketDesc, (struct sockaddr* ) &senderAddr, sizeof(senderAddr));
   if (bindSuccess < 0)
   {
    close(socketDesc);
    perror("Sender: Error on binding\n");
    return bindSuccess;
   }

   printf("Sender: attempting to SYN with receiver\n");



   //Send a SYN REQUEST TO THE RECEIVER
   sendMessageToReceiver(SYN, lastAckReceived, 0, 0, 0);

   char message[MAX_STR_LEN];
   string str = itoStr(lastAckReceived);
   snprintf(message, sizeof message, "s %s:%s %s:%s SYN %s 0", sender_ip, sender_port,
      receiver_ip, receiver_port, str.c_str());
   LogMessageWithTime(message);

   //Await a reply, or a timeout. If a TO occured, resend. If happeneded 3 times terminate connection with a RST message.
   int attempts = 1;
   while(TRUE)
   {
      int inputVal = isReadyToReadInput(socketDesc, 3, 0);  //Read input from the keyboard or the socket. 3 second timeout
      if(inputVal == 1 || inputVal < 0) //Q was pressed or an error occured
      {
         printf("Sender: Q pressed or error encountered\n");
         exit(1);
         return 0;
      }

      //Timeout happened (2s timeout)
      if(inputVal == 0)
      {
         if(attempts++ < MAX_SYN_TIMEOUT_ATTEMPTS) //Try (upto a maximum) MAX_TIMEOUT_ATTEMPTS SYN requests
         {
            sendMessageToReceiver(SYN, lastAckReceived, 0, 0, 0); //Note: ackNo is undefined, we simply set it to 0 for now
            char message[MAX_STR_LEN];
            string str = itoStr(lastAckReceived);
            snprintf(message, sizeof message, "S %s:%s %s:%s SYN %s 0", sender_ip, sender_port,
               receiver_ip, receiver_port, str.c_str());
            LogMessageWithTime(message);
            continue;
         }
         else
         {
            sendMessageToReceiver(RST, lastAckReceived, 0, 0, 0);

            char message[MAX_STR_LEN];
            string str = itoStr(lastAckReceived);
            snprintf(message, sizeof message, "s %s:%s %s:%s RST %s 0", sender_ip, sender_port,
               receiver_ip, receiver_port, str.c_str());
            LogMessageWithTime(message);
            break;
         }
      }

      //Read in the input
      char buffer[MAX_BUFFER_LENGTH]; //data buffer
      int numberOfBytes = recvfrom(socketDesc, buffer, MAX_BUFFER_LENGTH - 1 , 0, (struct sockaddr *)&receiverAddr, &receiverLength);
      if (numberOfBytes == -1)
      {
         perror("Sender: Error on recvfrom()!\n");
         return -1;
      }

      buffer[numberOfBytes] = '\0'; //Null terminate the buffer. [If it is not already null-terminated]
      SetReadBuffer(buffer);
      std::string hash = ReadBufferHash(); //Read in the hash
      std::string magic = ReadHeaderField();
      std::string type = ReadHeaderField();
      std::string seqNo = ReadHeaderField(); //Seq number on the SENDER SIDE
      std::string ackNo = ReadHeaderField(); //LAST Seq number received from the receiver SIDE on the server
      std::string length = ReadHeaderField();
      std::string windowSize = ReadHeaderField();
      std::string endHeader = ReadHeaderField(); //Should be length of 0

      if(hash.compare(NULLHASH) != 0)
      {
         printf("Sender: Bad hash!\n");
         continue;
      }
      //Message did not contain magic. BAD!
      if(magic.compare(MAGIC) != 0)
      {
         printf("Sender: Bad magic!\n");
         continue;
      }
      if(endHeader.length() != 0) //Bad Header.
      {
         printf("Sender: Incorrect header format\n");
         continue;
      }

      //Parse the buffer
      int typeAsInt = atoi(type.c_str());
      int lengthAsInt = atoi(length.c_str());
      int seqNoAsInt = atoi(seqNo.c_str());
      int ackNoAsInt = atoi(ackNo.c_str());
      int windowSizeAsInt = atoi(windowSize.c_str());
      char message[MAX_STR_LEN];
      string str;
      //If it was an ACK, we know that the connection was established.
      switch(typeAsInt)
      {

         case ACK:
            ACK_packets_received += 1;
            str = itoStr(lastAckReceived);
            snprintf(message, sizeof message, "r %s:%s %s:%s ACK %s 0", sender_ip, sender_port,
               receiver_ip, receiver_port, ackNo.c_str());
            LogMessageWithTime(message);

            if(ackNoAsInt == lastAckReceived)
            {
               connectionEstablished = TRUE;
               currentSeqNumberSent += 1;
               initialWindowSizeReceiver = windowSizeAsInt;
               windowSizeOnReceiver = initialWindowSizeReceiver;
            }
            else
            {
               sendMessageToReceiver(RST, lastAckReceived, 0, 0, 0);

               str = itoStr(lastAckReceived);
               snprintf(message, sizeof message, "s %s:%s %s:%s RST %s 0", sender_ip, sender_port,
                  receiver_ip, receiver_port, str.c_str());
               LogMessageWithTime(message);
            }
         break;

         //These are all means to throw a RST. We should have only received an ACK at this point. Anything else is bad!
         case DAT: //TODO : <Insert log messages for EACH ONE of these individually>
         case SYN:
         case FIN:
         case RST:
            RST_packets_received += 1;
         default:
            sendMessageToReceiver(RST, lastAckReceived, 0, 0, 0);

            str = itoStr(lastAckReceived);
            snprintf(message, sizeof message, "s %s:%s %s:%s RST %s 0", sender_ip, sender_port,
               receiver_ip, receiver_port, str.c_str());
            LogMessageWithTime(message);
         break;
      }
      break;
   }







   //Connect has been established or an error occured if we made it this far.
   //If a connection has been established.
   if(connectionEstablished == TRUE)
   {
      lastAckReceived += 1; //We received the SYN. Assume that this is good.
      firstFileSeqNum = lastAckReceived; //Will probably be 1
      startTime = time(NULL);

      //We are going to send the file now!.
      while(TRUE)
      {
         int inputVal = isReadyToReadInput(socketDesc, 0, 10);  //Set Timeout to 0.01ms (10uS), 1/100,000 of a second
         if(inputVal == 1 || inputVal < 0) //Q was pressed or an error occured
         {
            printf("Sender: Q pressed or error encountered\n");
            exit(1);
            return 0;
         }

         //Timeout happened. Meaning we did not receive any messages. Send the client some files!
         if(inputVal == 0) /*TODO: use windowSize*/
         {

            timeval tmp_time;
            gettimeofday(&tmp_time, NULL);
            if(abs(tmp_time.tv_sec - lastMessageSentTime.tv_sec) >= 1) //2 second timeout
            {
               lastFilePartSent = lastAckReceived - firstFileSeqNum; //TODO: FIX
               currentSeqNumberSent = lastAckReceived;
               windowSizeOnReceiver = initialWindowSizeReceiver;
            }
            while(true)
            {
               if(lastFilePartSent == numberFileParts)
               {
                  //We have sent the whole file. We are done here
                  break; //If the ackID == lastFilePartSent then we can assume the receiver got the whole file
               }


               std::map<int, std::string>::iterator iter = fileData.find(lastFilePartSent);

               //error
               if(iter == fileData.end())
               {
                  //printf("Error: File part missing in the data map\n");
                  break; //We don't have any newer data about the file
               }

               if(windowSizeOnReceiver < iter->second.length())
               {
                  break; //Congestion avoidance
               }



               currentSeqNumberSent = firstFileSeqNum + lastFilePartSent;
               int numberOfBytes = sendMessageToReceiver(DAT, currentSeqNumberSent, 0, iter->second.length(), 0, iter->second);

               char message[MAX_STR_LEN];
               string str = itoStr(currentSeqNumberSent);
               if(numberOfBytes <= 1)
               {
                  snprintf(message, sizeof message, "s %s:%s %s:%s DAT %s %d", sender_ip, sender_port,
                     receiver_ip, receiver_port, str.c_str(), (int)iter->second.length());
               }
               else //Multiple sends
               {
                  snprintf(message, sizeof message, "S %s:%s %s:%s DAT %s %d", sender_ip, sender_port,
                     receiver_ip, receiver_port, str.c_str(), (int)iter->second.length());
               }
               LogMessageWithTime(message);

               if (numberOfBytes < 0)
               {
                  perror("Server: Error in sendMessageToReceiver()\n");  //Error sending messages to the client. This is BAD!
                  return -1;
               }
               windowSizeOnReceiver -= iter->second.length();
               lastFilePartSent += iter->second.length();
            }
         }
         else //We received a message. It should** be an ack.
         {
            char buffer[MAX_BUFFER_LENGTH]; //data buffer
            int numberOfBytes = recvfrom(socketDesc, buffer, MAX_BUFFER_LENGTH - 1 , 0, (struct sockaddr *)&receiverAddr, &receiverLength);
            if (numberOfBytes == -1)
            {
               perror("Sender: Error on recvfrom()!\n");
               return -1;
            }

            buffer[numberOfBytes] = '\0'; //Null terminate the buffer. [If it is not already null-terminated]
            SetReadBuffer(buffer);
            std::string hash = ReadBufferHash(); //Read in the hash
            std::string magic = ReadHeaderField();
            std::string type = ReadHeaderField();
            std::string seqNo = ReadHeaderField(); //Seq number on the SENDER SIDE
            std::string ackNo = ReadHeaderField(); //LAST Seq number received from the receiver SIDE on the server
            std::string length = ReadHeaderField();
            std::string windowSize = ReadHeaderField();
            std::string endHeader = ReadHeaderField(); //Should be length of 0

            //Message did not contain magic. BAD!
            if(hash.compare(NULLHASH) != 0)
            {
               printf("Sender: Bad hash!\n");
               continue;
            }
            if(magic.compare(MAGIC) != 0)
            {
               perror("Sender: Bad magic!\n");
               continue;
            }
            if(endHeader.length() != 0) //Bad Header.
            {
               perror("Sender: Incorrect header format\n");
               continue;
            }

            //Parse the buffer
            int typeAsInt = atoi(type.c_str());
            int lengthAsInt = atoi(length.c_str());
            int seqNoAsInt = atoi(seqNo.c_str());
            int ackNoAsInt = atoi(ackNo.c_str());
            int windowSizeAsInt = atoi(windowSize.c_str());

            //If it was an ACK, we know that the connection was established.
            switch(typeAsInt)
            {
               //We should only receive ACKS from the client
               case ACK:
                  ACK_packets_received += 1;

                  //WHOLE FILE SENT??
                  if(ackNoAsInt + windowSizeAsInt == numberFileParts + firstFileSeqNum)
                  {
                     wholeFileHasBeenSent = TRUE; //The whole file has been sent, received and we have been notified of everything.
                  }

                  //Duplicate ack - could be sign of a lost packet
                  if(lastAckReceived == ackNoAsInt)
                  {
                     numDuplicatedPackets += 1;
                     if(numDuplicatedPackets == 7) //7 duplicated ack's -> packet lost. Deal with it!
                     {
                        lastFilePartSent = ackNoAsInt - firstFileSeqNum; //Reset the last file part sent. TODO: FIX THIS
                        numDuplicatedPackets = 0;
                        windowSizeOnReceiver = initialWindowSizeReceiver;
                        //printf("Err: Duplicated packets and stuff\n");
                     }

                     char message[MAX_STR_LEN];
                     string str = itoStr(lastAckReceived);
                     snprintf(message, sizeof message, "R %s:%s %s:%s ACK %s %s", sender_ip, sender_port,
                        receiver_ip, receiver_port, ackNo.c_str(), windowSize.c_str());
                     LogMessageWithTime(message);

                  }
                  else
                  {
                     numDuplicatedPackets = 0;
                     //New Ack
                     if(ackNoAsInt > lastAckReceived)
                     {
                        //Update windowSizeOnReceiver
                        windowSizeOnReceiver += (ackNoAsInt - lastAckReceived);
                        lastAckReceived = ackNoAsInt;
                     }

                     //Ack out of order. We don't care, it is outdated
                     char message[MAX_STR_LEN];
                     string str = itoStr(lastAckReceived);
                     snprintf(message, sizeof message, "r %s:%s %s:%s ACK %s %s", sender_ip, sender_port,
                        receiver_ip, receiver_port, ackNo.c_str(), windowSize.c_str());
                     LogMessageWithTime(message);
                  }

               break;

               //These are all means to throw a RST. We should have only received an ACK at this point. Anything else is bad!
               case RST:
                  RST_packets_received += 1;
               case DAT:
               case SYN:
               case FIN:
               default:
                  sendMessageToReceiver(RST, lastAckReceived, 0, 0, 0);

                  char message[MAX_STR_LEN];
                  string str = itoStr(lastAckReceived);
                  snprintf(message, sizeof message, "s %s:%s %s:%s RST %s %s", sender_ip, sender_port,
                     receiver_ip, receiver_port, str.c_str(), windowSize.c_str());
                  LogMessageWithTime(message);
               break;
            }

            //The whole file has been sent AND we have received ACK's for it.
            if(wholeFileHasBeenSent == TRUE)
            {
               ackNoAsInt += 1;
               break;
            }
         }
      }


      //File has been sent. We should terminate the connection now.
      lastAckReceived += 1;
      tryingToCloseConnection = TRUE;
      sendMessageToReceiver(FIN, lastAckReceived, 0, 0, 0);
      totalFinSent = 1;
      char message[MAX_STR_LEN];
      string str = itoStr(lastAckReceived);
      snprintf(message, sizeof message, "s %s:%s %s:%s FIN %s 0", sender_ip, sender_port,
         receiver_ip, receiver_port, str.c_str());
      LogMessageWithTime(message);
      while(TRUE)
      {

         int inputVal = isReadyToReadInput(socketDesc, 5, 0);  //Set Timeout to 5 seconds
         if(inputVal == 1 || inputVal < 0) //Q was pressed or an error occured
         {
            printf("Sender: Q pressed or error encountered\n");
            exit(1);
            return 0;
         }
         if(inputVal == 0)
         {
            closeEverything = TRUE;
         }
         else
         {
            char buffer[MAX_BUFFER_LENGTH]; //data buffer
            int numberOfBytes = recvfrom(socketDesc, buffer, MAX_BUFFER_LENGTH - 1 , 0, (struct sockaddr *)&receiverAddr, &receiverLength);
            if (numberOfBytes == -1)
            {
               perror("Sender: Error on recvfrom()!\n");
               return -1;
            }

            buffer[numberOfBytes] = '\0'; //Null terminate the buffer. [If it is not already null-terminated]
            SetReadBuffer(buffer);
            std::string hash = ReadBufferHash(); //Read in the hash
            std::string magic = ReadHeaderField();
            std::string type = ReadHeaderField();
            std::string seqNo = ReadHeaderField(); //Seq number on the SENDER SIDE
            std::string ackNo = ReadHeaderField(); //LAST Seq number received from the receiver SIDE on the server
            std::string length = ReadHeaderField();
            std::string windowSize = ReadHeaderField();
            std::string endHeader = ReadHeaderField(); //Should be length of 0

            //Message did not contain magic. BAD!
            if(hash.compare(NULLHASH) != 0)
            {
               printf("Sender: Bad hash!\n");
               continue;
            }
            if(magic.compare(MAGIC) != 0)
            {
               printf("Sender: Bad magic!\n");
               continue;
            }
            if(endHeader.length() != 0) //Bad Header.
            {
               printf("Sender: Incorrect header format\n");
               continue;
            }

            //Parse the buffer
            int typeAsInt = atoi(type.c_str());
            int lengthAsInt = atoi(length.c_str());
            int seqNoAsInt = atoi(seqNo.c_str());
            int ackNoAsInt = atoi(ackNo.c_str());
            char message[MAX_STR_LEN];
            string str;
            //If it was an ACK, we know that the connection was established.
            switch(typeAsInt)
            {
               //We should only receive ACKS from the client
               case ACK:
                  ACK_packets_received += 1;
                  if(ackNoAsInt == lastAckReceived)
                  {
                     tryingToCloseConnection = FALSE;
                     closeEverything = TRUE;

                     str = itoStr(lastAckReceived);
                     snprintf(message, sizeof message, "r %s:%s %s:%s ACK %s 0", sender_ip, sender_port,
                        receiver_ip, receiver_port, ackNo.c_str());
                     LogMessageWithTime(message);
                  }
                  else
                  {
                     str = itoStr(lastAckReceived);
                     snprintf(message, sizeof message, "R %s:%s %s:%s ACK %s 0", sender_ip, sender_port,
                        receiver_ip, receiver_port, ackNo.c_str());
                     LogMessageWithTime(message);

                     //Send another fin - we received an ACK out of order.
                     if(totalFinSent < 4)
                     {
                        ++totalFinSent;
                        sendMessageToReceiver(FIN, lastAckReceived, 0, 0, 0);

                        char message[MAX_STR_LEN];
                        string str = itoStr(lastAckReceived);
                        snprintf(message, sizeof message, "s %s:%s %s:%s FIN %s 0", sender_ip, sender_port,
                           receiver_ip, receiver_port, str.c_str());
                        LogMessageWithTime(message);
                     }
                  }
               break;

               //These are all means to throw a RST. We should have only received an ACK at this point. Anything else is bad!
               case RST:
                  RST_packets_received += 1;
               case DAT:
               case SYN:
               case FIN:
               default:
                  sendMessageToReceiver(RST, lastAckReceived, 0, 0, 0);

                  str = itoStr(lastAckReceived);
                  snprintf(message, sizeof message, "s %s:%s %s:%s RST %s 0", sender_ip, sender_port,
                     receiver_ip, receiver_port, str.c_str());
                  LogMessageWithTime(message);
               break;
            }
         }


         if(closeEverything == TRUE) //Connection can now be terminated.
         {
            break;
         }
      }
   } //End connection was established

   //Close the socket
   if (socketDesc != -1)
      close(socketDesc);

   //printTransferInfo
   printTransferInfo();

    return 0;
}


int sendMessageToReceiver(int type, int SeqNo, int ackNo, int length, int windowSize, std::string data)
{
   char tbuffer[MAX_BUFFER_LENGTH];
   memset(&tbuffer, 0, sizeof tbuffer);
   char magic[] = MAGIC;
   int writeLength = 0;
   if(length == 0)
   {
      snprintf(tbuffer, sizeof tbuffer, "%s%s\n%d\n%d\n%d\n%d\n%d\n\n", NULLHASH, magic, type, SeqNo, ackNo, length, windowSize);
      writeLength = strlen(tbuffer);
   }
   else
   {
      char tmpbuffer[MAX_BUFFER_LENGTH];

      unsigned int hashVal = hashString(data);
      stringstream ss;
      ss.width(10);
      ss.fill('0');
      ss << hashVal;
      std::string hash;
      ss >> hash;

      snprintf(tmpbuffer, sizeof tmpbuffer, "%s%s\n%d\n%d\n%d\n%d\n%d\n\n", hash.c_str(), magic, type, SeqNo, ackNo, length, windowSize);
      writeLength = strlen(tmpbuffer);

      //Copy formatted thing to this
      int i=0;
      while(tmpbuffer[i] != '\0')
      {
         tbuffer[i] = tmpbuffer[i];
         i++;
      }

      //Copy data
      i = 0;
      for(i=0; i<length; ++i)
      {
         tbuffer[i + writeLength] = data.at(i);
      }
      i++;

      //Append null terminator
      tbuffer[i + writeLength] = '\0';
      writeLength += length;

   }

   //Send off the message
   int numbytes;
   numbytes = sendto(socketDesc, &tbuffer, writeLength, 0, (struct sockaddr *)&receiverAddr, receiverLength);
   if(numbytes < 0)
   {
      printf("Error: sendMessageToReceiver() - no data sent\n");
   }

   //Update last time message was sent to receiver
   gettimeofday(&lastMessageSentTime, NULL);
   total_data_bytes_sent += length;

   if(type == SYN) SYN_packets_sent += 1;
   if(type == FIN) FIN_packets_sent += 1;
   if(type == RST) RST_packets_sent += 1;
   if(type == DAT)
   {
      total_data_packets_sent += 1;

      if(messageSentCounter.find(SeqNo) == messageSentCounter.end())
      {
         messageSentCounter.insert(std::pair<int, int> (SeqNo, 1));
         unique_data_packets_sent += 1;
         unique_data_bytes_sent += length;
      }

   }


   return numbytes;
}

int fileExists(char* relFilePath)
{
   struct stat fileInfo;
   bool fexist = (stat(relFilePath, &fileInfo) == 0);
   if(fexist == true)
   {
      if(S_ISDIR(fileInfo.st_mode)) //Client tried to open up a folder. Note: We can note open up folders!
      {
         return FALSE;
      }
   }
   if(fexist == false)
   {
      return FALSE;
   }
   return TRUE;
}

std::string itoStr(int x)
{
   stringstream ss;
   ss << x;
   string str = ss.str();
   return str;
}


void printTransferInfo()
{
   time_t endTime = time(NULL) - startTime;
   printf("total data bytes sent: %d\n", total_data_bytes_sent);
   printf("unique data bytes sent: %d\n", unique_data_bytes_sent);
   printf("total data packets sent: %d\n", total_data_packets_sent);
   printf("unique data packets sent: %d\n", unique_data_packets_sent);
   printf("SYN packets sent: %d\n", SYN_packets_sent);
   printf("FIN packets sent: %d\n", FIN_packets_sent);
   printf("RST packets sent: %d\n", RST_packets_sent);
   printf("ACK packets received: %d\n", ACK_packets_received);
   printf("RST packets received: %d\n", RST_packets_received);
   printf("total time duration (second): %lu\n", endTime); //TODO: Make time duration in seconds
}





