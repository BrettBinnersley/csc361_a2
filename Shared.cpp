
//BONUS: Hashing used for corrupt packets

#include "shared.h"
using namespace std;

//Self explanitory
void LogMessageWithTime(const char* message)
{
  time_t rawtime;
  struct timeval timeval;
  struct tm* timeinfo;
  char fmt[MAX_BUFFER_LENGTH], buf[MAX_BUFFER_LENGTH];

  gettimeofday(&timeval, NULL);
  if((timeinfo = localtime(&timeval.tv_sec)) != NULL)
  {
    strftime(fmt, MAX_BUFFER_LENGTH, "%H:%M:%S.%%06u", timeinfo);
    snprintf(buf, MAX_BUFFER_LENGTH, fmt, timeval.tv_usec);
    printf("%s: %s\n", buf, message);
  }
}

//Detect whether or not the user pressed Q or if we received a message from the client
//Will return 0 on a timeout
int isReadyToReadInput(int socketDesc, int secondTO, int microSecTO)
{
   while (1)
   {
      char read_buffer[MAX_BUFFER_LENGTH];
      struct timeval timeout;
      timeout.tv_sec = secondTO; //Wait a maximum of 2 seconds before throwing a timeout
      timeout.tv_usec = microSecTO;

      fd_set readDesc;
      FD_ZERO(&readDesc);

      FD_SET(STDIN_FILENO, &readDesc);
      FD_SET(socketDesc, &readDesc);
      int retval = select(socketDesc + 1, &readDesc, NULL, NULL, &timeout);
      if(retval <= 0)
      {
         return retval; //Error occured or request timed out.
      }
      else
      {
         if(FD_ISSET(STDIN_FILENO, &readDesc) && (fgets(read_buffer, MAX_BUFFER_LENGTH, stdin) != NULL) && strchr(read_buffer, 'q') != NULL)
         {
            return 1; // Q was pressed, exit the client
         }
         else if(FD_ISSET(socketDesc, &readDesc))
         {  // recv buffer readDescdy
            return 2;
         }
      }
      return -1;
   }
}

//Reading the header
char* readFromBuffer;
void SetReadBuffer(char* buffer)
{
   readFromBuffer = buffer;
}

std::string ReadHeaderField()
{
   char buffer[MAX_BUFFER_LENGTH];
   int buffPos = 0;
   while(1)
   {
      buffer[buffPos] = *readFromBuffer;  //Value at position in memory
      readFromBuffer++; //Increase position of buffer pointer in memory (gets rid of the \n)
      if(buffer[buffPos] == '\n')
      {
        break;
      }
      buffPos++; //Increase buffer position (don't include the \n in the string)
   }
   buffer[buffPos] = '\0'; //Replace the end of the string with a null terminator
   return std::string(buffer, buffPos);
}

std::string ReadBufferLength(int length)
{
   char buffer[MAX_BUFFER_LENGTH];
   for(int i=0; i<length; ++i)
   {
      buffer[i] = *readFromBuffer;  //Value at position in memory
      readFromBuffer++; //Increase position of buffer pointer in memory
   }
   buffer[length] = '\0';
   return std::string(buffer, length);
}

std::string ReadBufferHash()
{
   char buffer[11];
   for(int i=0; i<10; ++i)
   {
      buffer[i] = *readFromBuffer;  //Value at position in memory
      readFromBuffer++; //Increase position of buffer pointer in memory
   }
   buffer[10] = '\0';
   return std::string(buffer);
}

std::string typeToString(int type)
{
  if(type == DAT) return std::string("DAT");
  if(type == ACK) return std::string("ACK");
  if(type == SYN) return std::string("SYN");
  if(type == FIN) return std::string("FIN");
  if(type == RST) return std::string("RST");
  return std::string("ERR"); //This should never be encountered. Implies an error occured in the code.
}

unsigned int hashString(const std::string& str)
{
    if(str.length() == 0)
    {
      return 0; //Default hash
    }
    unsigned int b    = 378551;
    unsigned int a    = 63689;
    unsigned int hash = 0;

    for(size_t i = 0; i < str.length(); i++)
    {
        hash = hash * a + str[i];
        a    = a * b;
    }
    return (hash & 0x7FFFFFFF);
}

std::string numberToPaddedHashString(unsigned int number)
{
  stringstream ss;
  ss.width(10);
  ss.fill('0');
  ss << number;
  std::string hash;
  ss >> hash;
  return hash;
}
