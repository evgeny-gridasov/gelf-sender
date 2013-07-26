/*
 ============================================================================
 Name        : gelf-sender.c
 Author      : Evgeny Gridasov <evgeny.gridasov@gmail.com>
 Version     : 1.0 
 Copyright   : Evgeny Gridasov
 Description : GELF Sender
 ============================================================================
 */

#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>

#ifdef WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/socket.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

//#include <json/json_object_private.h>
#include <json/json.h>
#include <zlib.h>

#include "gelf-sender.h"

// user input ip and port
char* glhost;
int glport = 0;

// other user input
int sleeptime = 0;
char* regex;
char* logfile;
char* myhost;
char* facility="gelf-sender";

// internal variables
int fd = -1;
__off_t offset;
wchar_t * uniPath;
int verbose = 0;
#ifdef WIN32
HANDLE handle = NULL;
#endif
// system representation ip and port
char* resolvedHost;
struct in_addr haddr;
uint16_t hport;


/**
 * MAIN
 */
int main(int argc, char* argv[]) {
  printf("Copyright (C) 2013 Evgeny Gridasov <evgeny.gridasov@gmail.com>\n");
  printf("GELF log file sender\n\n");

  parseOptions(argc, argv);
  if (checkFile(logfile) != 0) {
      fprintf(stderr, "Could not access file: %s\n", logfile);
      exit(EXIT_FAILURE);
  }
#ifdef WIN32
  initWinSock();
  initWfile();
#endif
  processIpAndPort();
  fprintf(stderr, "Sending %s to %s:%i\n", logfile, resolvedHost, glport);

  processFile();

  return EXIT_SUCCESS;
}


/**
 * Process file
 */
void processFile() {
  char* buffer;
  char* pline = NULL;
  size_t plineSize = 0;

  // open file and seek
  fd = openLogFile();
  if (fd == -1) {
      fprintf(stderr, "Error opening file: %s\n", logfile);
      exit(EXIT_FAILURE);
  }
  offset = lseek(fd, 0, SEEK_END);

  // buffer
  buffer = (char*) malloc(BUFSIZE);

  int count = 0;
  while (1) {
      count = read(fd, buffer, BUFSIZE);
      // EOF
      if (count == 0) {
#ifdef WIN32
          closeLogFile();
#endif
          sleepInt();
#ifdef WIN32
          reopenFile(offset);
#endif
          checkStat();
          continue;
      }
      // error
      if (count < 0) {
          closeLogFile();
          reopenFile(0);
          continue;
      }
      offset+=count;
      //printf("read %i bytes\n", count);
      char * b = buffer;
      while (count>0 && *b) {
          char * b1 = memchr(b, '\n', count);
          int length;
          if (b1 != NULL) { // found '\n'
              length = b1 - b; // length without '\n'
              char * line = strconcat(pline, b, plineSize, length);
              if (plineSize >0) {
                  pline = NULL;
                  plineSize = 0;
              }
              processLine(line);
              free(line); // we don't need it after we processed it

              b = b1 + 1; //skip '\n'
              count = count - length - 1; //reduce remaining bytes
          } else {
              pline = strconcat(pline, b, plineSize, count);
              plineSize += count;
              break;
          }
      }
  }
  free(buffer);
  close(fd);
}


/**
 * process each line
 */
void processLine(const char* line) {
  char * json = makeJson(line);
  if (verbose > 0) fprintf(stderr, "sending: %s\n", json);
  transferData* tdata = zlibCompress(json);
  sendMessage(tdata);
  // free up
  free(tdata->data);
  free(tdata);
  free(json);
}

/**
 * convert line to json
 */
char * makeJson(const char * line) {
  // init objects
  json_object* object = json_object_new_object();
  json_object* version = json_object_new_string("1.0");
  json_object* host = json_object_new_string(myhost);
  json_object* shortMessage = json_object_new_string(line);
  json_object* fac = json_object_new_string(facility);
  json_object* level = json_object_new_int(6); /*0=Emerg, 1=Alert, 2=Crit, 3=Error, 4=Warn, 5=Notice, 6=Info */
  json_object* timestamp = json_object_new_double(getTimestamp());

  // attach to root
  json_object_object_add(object, "version", version);
  json_object_object_add(object, "host", host);
  json_object_object_add(object, "short_message", shortMessage);
  json_object_object_add(object, "facility", fac);
  json_object_object_add(object, "level", level);
  json_object_object_add(object, "timestamp", timestamp);

  // get string
  const char * str = json_object_to_json_string_ext(object,
      verbose ?
          JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_SPACED :
          JSON_C_TO_STRING_PLAIN);
  char * ret = strdup(str);
  //free up
  json_object_put(object);
  return ret;
}


/**
 * compress line with zlib
 */
transferData* zlibCompress(const char* line) {
  // init stream struc and buffers
  z_stream* strm;
  size_t len = strlen(line);
  void * buf = malloc(len);

  strm = malloc(sizeof(z_stream));
  memset(strm, 0, sizeof(z_stream));
  strm->zalloc = Z_NULL;
  strm->zfree = Z_NULL;
  strm->opaque = Z_NULL;
  strm->data_type = Z_TEXT;

  // deflate
  if (deflateInit(strm, 6) != Z_OK) {
      fprintf(stderr, "Error initialising zlib deflate\n");
  }
  strm->avail_in = len;
  strm->next_in = (void *)line;
  strm->next_out = buf;
  strm->avail_out = len;
  if ( deflate(strm, Z_FINISH) == Z_STREAM_ERROR) {
     fprintf(stderr, "Error compressing with zlib deflate\n");
  }
  int csize = len - strm->avail_out;
  if (verbose > 0) fprintf(stderr, "Json length: %i, compressed: %i\n", len, csize);
  deflateEnd(strm);

  // free up
  free(strm);

  // make transfer data
  transferData * ret = malloc(sizeof(transferData));
  memset(ret, 0, sizeof(transferData));
  ret->data = buf;
  ret->size = csize;

  return ret;
}


/**
 * send a message
 */
void sendMessage(const transferData* payload) {
  int sock;
  struct sockaddr_in* s;
  size_t slen = sizeof(struct sockaddr_in);
  sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) {
      fprintf(stderr, "Error creating socket\n");
  }
  s = malloc(slen);
  memset(s, 0, slen);
  s->sin_family = AF_INET;
  s->sin_port = hport;
  s->sin_addr = haddr;
  if (sendto(sock, payload->data, payload->size, 0, (struct sockaddr*)s, slen) == -1) {
      fprintf(stderr, "Error writing to socket %i bytes\n", payload->size);
  }
  close(sock);
  free(s);
}

/**
 * print usage
 */
void printUsage(char* programName) {
  printf("Usage: %s -h gelf_host -p gelf_port -f log_file -t sleep_timeout -z my_hostname -a facility -v\n\n", programName);
}


/**
 * parse command line options
 */
void parseOptions(int argc, char* argv[])
{
  int opt = 0;
  int toparse = 5;
  while ((opt = getopt(argc, argv, "vh:p:f:r:t:z:a:")) != -1)
    {
      switch (opt)
      {
      case 'h':
        glhost = strdup(optarg);
        toparse--;
        break;
      case 'p':
        glport = atoi(optarg);
        if (glport <=0 || glport >=65535) {
            fprintf(stderr, "Wrong port number: %s\n", optarg);
        } else {
            toparse--;
        }
        break;
      case 't':
        sleeptime = atoi(optarg);
        if (sleeptime <=0 || sleeptime >=65535) {
            fprintf(stderr, "Wrong timeout: %s\n", optarg);
        } else {
            toparse--;
        }
#ifndef WIN32
        sleeptime = sleeptime * 1000;
#endif
        break;
      case 'f':
        logfile = strdup(optarg);
        toparse--;
        break;
      case 'z':
        myhost = strdup(optarg);
        toparse--;
        break;
      case 'r':
        regex = strdup(optarg);
        break;
      case 'a':
        facility = strdup(optarg);
        break;
      case 'v':
        verbose = 1;
        break;
      default:
        printUsage(argv[0]);
        exit(EXIT_FAILURE);
      }
    }
  if (toparse > 0)
    {
      printUsage(argv[0]);
      exit(EXIT_FAILURE);
    }
}


/**
 * parse port and resolve ip
 */
void processIpAndPort() {
  struct hostent* hp = gethostbyname(glhost);
  if (!hp) {
      fprintf(stderr, "Unknown hostname: %s\n", glhost);
      exit(EXIT_FAILURE);
  } else {
      if (hp->h_addrtype != AF_INET) {
          fprintf(stderr, "Only IPV4 addressess supported.\n");
          exit(EXIT_FAILURE);
      }
      struct in_addr* ip = (struct in_addr *) (hp->h_addr_list[0]);
      if (!ip) {
          fprintf(stderr, "No IP address resolved.\n");
          exit(EXIT_FAILURE);
      }
      resolvedHost = inet_ntoa(*ip);
      if (!resolvedHost) {
          fprintf(stderr, "Bad IP address resolved.\n");
          exit(EXIT_FAILURE);
      }
      resolvedHost = strdup(resolvedHost);
      haddr = *ip;
  }

  hport = htons(glport);
}

/**
 * check if file exists and readable
 */
int checkFile(const char* filename) {
  struct stat s;
  if (stat(filename, &s) != 0) {
      return -1;
  }
  if (!S_ISREG(s.st_mode)) {
      return -1;
  }
  if (access(filename, R_OK) != 0) {
      return -1;
  }
  return 0;
}

/**
 * get current timestamp
 */
double getTimestamp() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  double ret = tv.tv_sec;
  double msec = ((double) (tv.tv_usec / 1000)) / 1000.0;
  ret += msec;
  return ret;
}


/**
 * Sleep a while
 */
void sleepInt() {
#ifdef WIN32
  Sleep(sleeptime);
#else
  usleep(sleeptime);
#endif
}


/**
 * keep reopening file
 */
void reopenFile(__off_t newOffset) {
  fd = openLogFile();
  while (fd == -1) {
      sleepInt();
      fd = openLogFile();
  }
  offset = lseek(fd, newOffset, SEEK_SET);
  if (offset < newOffset) {
      lseek(fd, 0, SEEK_SET);
  }
}


/**
 * check file offset, rewind if necessary
 */
void checkStat() {
  struct stat st, fst;
  // if any of stat calls fail or if inodes are different -> reopen
  if (fstat(fd, &fst) < 0 || stat(logfile, &st) < 0
#ifndef WIN32
      || fst.st_ino != st.st_ino || fst.st_dev != st.st_dev
#endif
      ) {
      closeLogFile();
      reopenFile(0);
  } else {
      // truncate check
      if (fst.st_size < offset) {
          offset = lseek(fd, 0, SEEK_SET);
      }
  }
  if (verbose) fprintf(stderr, "Current size %li, offset %li\n", fst.st_size, offset);
}

/**
 * reallocs dst string memory and copies src string there
 */
char* strconcat(char * dst, const char * src, size_t dlen, size_t slen) {
  //printf("realloc: %x, %i\n", dst, dlen + slen + 1);
  dst = realloc(dst, dlen + slen + 1);
  char * x = dst + dlen;
  strncpy(x, src, slen);
  *(x+slen)= '\0'; // make EOL \0 cause strncpy won't
  return dst;
}


/**
 * OS-dependent open file
 */
int openLogFile() {
#ifdef WIN32
  if (verbose>0) fwprintf(stderr, L"WIN32 CreateFileW: %s\n", uniPath);
  handle = CreateFileW(uniPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (handle == INVALID_HANDLE_VALUE) {
      return -1;
  }
  return _open_osfhandle((intptr_t)handle, _O_RDONLY);
#else
  return open(logfile, O_RDONLY | O_NONBLOCK);
#endif
}

/**
 * OS dependent close file
 */
void closeLogFile() {
#ifdef WIN32
  CloseHandle(handle);
  handle = NULL;
#else
  close(fd);
  fd = -1;
#endif
}

#ifdef WIN32
/**
 * Init winsock library
 */
void initWinSock() {
  WSADATA wsaData;
  WORD version;
  int error;
  version = MAKEWORD( 2, 0 );
  error = WSAStartup( version, &wsaData );
  if (error != 0 ) {
      fprintf(stderr, "Error initialising WinSock.\n");
      exit(EXIT_FAILURE);
  }
}

/**
 * init wide file name
 */
void initWfile() {
  const wchar_t * prefix = L"\\\\?\\";
  const size_t prefixLen = 4;
  size_t fileNameLen = strlen(logfile);
  size_t memSize = sizeof(wchar_t) * (prefixLen + fileNameLen + 1);
  uniPath = malloc(memSize);
  memset(uniPath, 0, memSize);
  wcscpy(uniPath, prefix);
  mbstowcs(uniPath + prefixLen, logfile, fileNameLen);
}
#endif
