/*
 * httpd_cgi.h
 *
 *  Created on: 26 jul. 2012
 *      Author: dig
 */

#ifndef HTTPD_CGI_H_
#define HTTPD_CGI_H_


#include "../../http/include/httpd.h"
#define CGIRETURNFILE "/CGIreturn.txt"

extern CGIresponseFileHandler_t readResponseFile;
extern bool sendBackOK;
int freadCGI( char *buffer, int count);
void CGI_init( void );

extern const tCGI *g_pCGIs;
extern int g_iNumCGIs;

#endif /* HTTPD_CGI_H_ */
