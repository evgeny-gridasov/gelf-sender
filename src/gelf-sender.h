/*
 * gelf-sender.h
 *
 *  Created on: 23/03/2013
 *      Author: Evgeny Gridasov <evgeny.gridasov@gmail.com>
 */

#ifndef GELF_SENDER_H_
#define GELF_SENDER_H_

#ifdef WIN32
typedef long __off_t;
void initWinSock();
void initWfile();
#define UNICODE 1
#endif

const size_t BUFSIZE = 32768;

typedef struct transferDataS {
    void * data;
    int size;
} transferData;

// declare functions
void processIpAndPort();
void parseOptions(int argc, char* argv[]);
char* strconcat(char * dst, const char * src, size_t dlen, size_t slen);
void processFile();
void processLine(const char* line);
char* makeJson(const char* line);
double getTimestamp();
void sendMessage(const transferData* payload);
transferData* zlibCompress(const char* line);
void reopenFile(__off_t newOffset);
void sleepInt();
void checkStat();
int openLogFile();
void closeLogFile();

#endif /* GELF_SENDER_H_ */
