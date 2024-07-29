/* ptpicc.c
 *
 * Copyright (C) 2018 Ecamm Network
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details. 
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301  USA
 *
 * working on image capture
 */

#define _BSD_SOURCE
#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif


#include <gphoto2/gphoto2-library.h>
#include <gphoto2/gphoto2-port-log.h>
#include <gphoto2/gphoto2-setting.h>

#include "ptp.h"
#include "ptp-private.h"


#ifdef ENABLE_NLS
#  include <libintl.h>
#  undef _
#  define _(String) dgettext (GETTEXT_PACKAGE, String)
#  ifdef gettext_noop
#    define N_(String) gettext_noop (String)
#  else
#    define N_(String) (String)
#  endif
#else
#  define textdomain(String) (String)
#  define gettext(String) (String)
#  define dgettext(Domain,Message) (Message)
#  define dcgettext(Domain,Message,Type) (Message)
#  define bindtextdomain(Domain,Directory) (Domain)
#  define _(String) (String)
#  define N_(String) (String)
#endif

#include "ptp.h"
#include "ptp-bugs.h"

#include "ptp-pack.c"

#define ptpip_len		0

/* send / receive functions */
uint16_t
ptp_ptpicc_sendreq (PTPParams* params, PTPContainer* req, int dataphase)
{
 //   printf("ptp_ptpicc_sendreq operationCode 0x%x\n", req->Code);
	int		len = 12+req->Nparam*4;
	unsigned char 	*request = malloc(len);

    htod32a(&request[ptpip_len],len);
    htod16a(&request[4],PTP_USB_CONTAINER_COMMAND); 
    htod16a(&request[6],req->Code);
    htod32a(&request[8],req->Transaction_ID);
 //   printf("send Transaction_ID %d\n", req->Transaction_ID);

    switch (req->Nparam) {
        case 5: htod32a(&request[28],req->Param5);
        case 4: htod32a(&request[24],req->Param4);
        case 3: htod32a(&request[20],req->Param3);
        case 2: htod32a(&request[16],req->Param2);
        case 1: htod32a(&request[12],req->Param1);
        case 0:
        default:
            break;
    }
    
    Camera *camera = ((PTPData *)params->data)->camera;

    int res;

    res = gp_port_write (camera->port, (char*)request, len);
    
    free(request);
    if (res != len) {
        gp_log (GP_LOG_DEBUG, "ptpicc sendreq",
                "sending req result %d",res);
        return PTP_ERROR_IO;
    }

    return PTP_RC_OK;
}

uint16_t
ptp_ptpicc_senddata (PTPParams* params, PTPContainer* ptp,
		uint64_t size, PTPDataHandler *handler
)
{
   // printf("ptp_ptpicc_senddata operationCode 0x%x dataSize: %d\n", ptp->Code, size);

    int packetlen = 12+(ptp->Nparam*4);
    int		len = packetlen+size;
    unsigned char 	*request = malloc(len);
    
    htod32a(&request[ptpip_len],packetlen);
    htod16a(&request[4],PTP_USB_CONTAINER_COMMAND);
    htod16a(&request[6],ptp->Code);
    htod32a(&request[8],ptp->Transaction_ID);
 //   printf("send data Transaction_ID %d\n", ptp->Transaction_ID);
    
    switch (ptp->Nparam) {
        case 5: htod32a(&request[28],ptp->Param5);
        case 4: htod32a(&request[24],ptp->Param4);
        case 3: htod32a(&request[20],ptp->Param3);
        case 2: htod32a(&request[16],ptp->Param2);
        case 1: htod32a(&request[12],ptp->Param1);
        case 0:
        default:
            break;
    }
    
    Camera *camera = ((PTPData *)params->data)->camera;
    
    int res;
    unsigned long gotlen;
    /* For all camera devices. */
    int ret = handler->getfunc(params, handler->priv, size, request+packetlen, &gotlen);
    if (ret != PTP_RC_OK)
    {
        free(request);
        return ret;
    }
 //   printf("getfunc: %lld got: %lld\n", size, gotlen);
    res = gp_port_write (camera->port, (char*)request, len);
    
    free(request);

    if (res != packetlen) {
        gp_log (GP_LOG_DEBUG, "ptpicc sendreq",
                "sending req result %d",res);
      //  return PTP_ERROR_IO;
    }
    
    return PTP_RC_OK;
}

uint16_t
ptp_ptpicc_getdata (PTPParams* params, PTPContainer* ptp, PTPDataHandler *handler) {
   
  //  printf("ptp_ptpicc_getdata\n");

    
    uint16_t res = ptp_ptpicc_getresp(params, ptp);

    unsigned char	*data = NULL;
    uint16_t 	ret;
    int		n;
    
    data = (unsigned char*)malloc(32);
    n = 32;
    
    Camera *camera = ((PTPData *)params->data)->camera;
    
    n = gp_port_read (camera->port, (char*)data, n);
    
    int size = dtoh32a(&data[0]);
    uint16_t responsetype = dtoh16a(&data[4]);
    ptp->Code		= dtoh16a(&data[6]);
    
 //   printf("ptp_ptpicc_getdata code: 0x%x\n", ptp->Code);

    uint32_t transactionID = dtoh32a(&data[8]);
    ptp->Transaction_ID	= transactionID;
    
    free(data);

    if (responsetype==PTP_USB_CONTAINER_DATA)
    {
        int dataLen = size - n;
        unsigned char* camdata = (unsigned char*)malloc(dataLen);

        n = gp_port_read (camera->port, (char*)camdata, 0);

        int ret = handler->putfunc(
                         params, handler->priv, n, camdata
                         );
        
        //  ptp_canon_getviewfinderimage seems to think param1 should contain the length of the image data
        ptp->Param1 = n;
        
        free(camdata);
    //    printf("ptp_ptpicc_getdata written %d\n", ret);
        
    }
    
    return PTP_RC_OK;
}

#define ptpip_resp_code		0
#define ptpip_resp_transid	2
#define ptpip_resp_param1	6
#define ptpip_resp_param2	10
#define ptpip_resp_param3	14
#define ptpip_resp_param4	18
#define ptpip_resp_param5	22

uint16_t
ptp_ptpicc_getresp (PTPParams* params, PTPContainer* resp)
{
	unsigned char	*data = NULL;
	uint16_t 	ret;
	int		n;

    data = (unsigned char*)malloc(32);
    n = 32;
    
    Camera *camera = ((PTPData *)params->data)->camera;

    n = gp_port_read (camera->port, (char*)data, n);
    if (n < 0)
    {
        return PTP_ERROR_TIMEOUT;
    }
    
    int size = dtoh32a(&data[0]);
    uint16_t responsetype = dtoh16a(&data[4]);
    resp->Code		= dtoh16a(&data[6]);
    
    uint32_t transactionID = dtoh32a(&data[8]);
    
    resp->Transaction_ID	= transactionID;
    
   // printf("resp Transaction_ID %d\n", resp->Transaction_ID);
    n = (size - 12)/sizeof(uint32_t);
    switch (n) {
        case 5: resp->Param5 = dtoh32a(&data[28]);
        case 4: resp->Param4 = dtoh32a(&data[24]);
        case 3: resp->Param3 = dtoh32a(&data[20]);
        case 2: resp->Param2 = dtoh32a(&data[16]);
        case 1: resp->Param1 = dtoh32a(&data[12]);
        case 0: break;
        default:
          //  gp_log( GP_LOG_ERROR, "ptpicc/getresp", "response got %d parameters?", n);
            break;
    }
    free (data);

	return PTP_RC_OK;
}

#define ptpip_initcmd_guid	8
#define ptpip_initcmd_name	24

static uint16_t
ptp_ptpicc_init_command_request (PTPParams* params)
{
	return PTP_RC_OK;
}

uint16_t
ptp_ptpicc_event_check (PTPParams* params, PTPContainer* event) {
    return PTP_RC_OK;
}

uint16_t
ptp_ptpicc_event_wait (PTPParams* params, PTPContainer* event) {
    return PTP_RC_OK;
}


int
ptp_ptpicc_connect (PTPParams* params, const char *address) {

        
	return GP_OK;
}
