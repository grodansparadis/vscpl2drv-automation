// vscp2drv-automation.cpp : Defines the initialization routines for the DLL.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation; either version
// 2 of the License, or (at your option) any later version.
//
// This file is part of the VSCP (http://www.vscp.org)
//
// Copyright (C) 2000-2019 Ake Hedman,
// Ake Hedman, Grodans Paradis AB, <akhe@grodansparadis.com>
//
// This file is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this file see the file COPYING.  If not, write to
// the Free Software Foundation, 59 Temple Place - Suite 330,
// Boston, MA 02111-1307, USA.
//

#ifdef __GNUG__
//#pragma implementation
#endif

#include <list>
#include <map>
#include <string>

#include "stdio.h"
#include "stdlib.h"

#include "automation.h"
#include "vscpl2drv-automation.h"

void
_init() __attribute__((constructor));
void
_fini() __attribute__((destructor));

// This map holds driver handles/objects
static std::map<long, CAutomation *> g_ifMap;

// Mutex for the map object
static pthread_mutex_t g_mapMutex;

////////////////////////////////////////////////////////////////////////////
// DLL constructor
//

void
_init()
{
    pthread_mutex_init(&g_mapMutex, NULL);
}

////////////////////////////////////////////////////////////////////////////
// DLL destructor
//

void
_fini()
{
    // If empty - nothing to do
    if (g_ifMap.empty()) return;

    // Remove orphan objects

    LOCK_MUTEX(g_mapMutex);

    for (std::map<long, CAutomation *>::iterator it = g_ifMap.begin();
         it != g_ifMap.end();
         ++it) {
        // std::cout << it->first << " => " << it->second << '\n';

        CAutomation *pif = it->second;
        if (NULL != pif) {
            pif->close();
            delete pif;
            pif = NULL;
        }
    }

    g_ifMap.clear(); // Remove all items

    UNLOCK_MUTEX(g_mapMutex);
    pthread_mutex_destroy(&g_mapMutex);
}

///////////////////////////////////////////////////////////////////////////////
// addDriverObject
//

long
addDriverObject(CAutomation *pif)
{
    std::map<long, CAutomation *>::iterator it;
    long h = 0;

    LOCK_MUTEX(g_mapMutex);

    // Find free handle
    while (true) {
        if (g_ifMap.end() == (it = g_ifMap.find(h))) break;
        h++;
    };

    g_ifMap[h] = pif;
    h += 1681;

    UNLOCK_MUTEX(g_mapMutex);

    return h;
}

///////////////////////////////////////////////////////////////////////////////
// getDriverObject
//

CAutomation *
getDriverObject(long h)
{
    std::map<long, CAutomation *>::iterator it;
    long idx = h - 1681;

    // Check if valid handle
    if (idx < 0) return NULL;

    it = g_ifMap.find(idx);
    if (it != g_ifMap.end()) {
        return it->second;
    }

    return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// removeDriverObject
//

void
removeDriverObject(long h)
{
    std::map<long, CAutomation *>::iterator it;
    long idx = h - 1681;

    // Check if valid handle
    if (idx < 0) return;

    LOCK_MUTEX(g_mapMutex);
    it = g_ifMap.find(idx);
    if (it != g_ifMap.end()) {
        CAutomation *pObj = it->second;
        if (NULL != pObj) {
            delete pObj;
            pObj = NULL;
        }
        g_ifMap.erase(it);
    }
    UNLOCK_MUTEX(g_mapMutex);
}

///////////////////////////////////////////////////////////////////////////////
//               V S C P   L E V E L  I I  D R I V E R -  A P I
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// VSCPOpen
//

extern "C" long
VSCPOpen(const char *pPathConfig)
{
    long h = 0;

    CAutomation *pdrvObj = new CAutomation();
    if (NULL != pdrvObj) {

        if (pdrvObj->open(pPathConfig)) {

            if (!(h = addDriverObject(pdrvObj))) {
                delete pdrvObj;
            }

        } else {
            delete pdrvObj;
        }
    }

    return h;
}

///////////////////////////////////////////////////////////////////////////////
//  VSCPClose
//

extern "C" int
VSCPClose(long handle)
{
    int rv = 0;

    CAutomation *pdrvObj = getDriverObject(handle);
    if (NULL == pdrvObj) return CANAL_ERROR_MEMORY;
    pdrvObj->close();
    removeDriverObject(handle);
    rv = 1;
    return CANAL_ERROR_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
//  VSCPWrite
//

extern "C" int
VSCPWrite(long handle, const vscpEvent *pEvent, unsigned long timeout)
{
    int rv = 0;

    CAutomation *pdrvObj = getDriverObject(handle);
    if (NULL == pdrvObj) return CANAL_ERROR_MEMORY;

    // vscpEvent *pEventNew = new vscpEvent;
    // if ( NULL != pEventNew ) {
    //    copyVSCPEvent( pEventNew, pEvent );
    pdrvObj->addEvent2SendQueue(pEvent);
    //}
    // else {
    //    return CANAL_ERROR_MEMORY;
    //}

    return CANAL_ERROR_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
//  VSCPRead
//

extern "C" int
VSCPRead(long handle, vscpEvent *pEvent, unsigned long timeout)
{
    int rv = 0;

    // Check pointer
    if (NULL == pEvent) return CANAL_ERROR_PARAMETER;

    CAutomation *pdrvObj = getDriverObject(handle);
    if (NULL == pdrvObj) return CANAL_ERROR_MEMORY;

    struct timespec ts;
    ts.tv_sec  = 0;
    ts.tv_nsec = timeout * 1000;
    if (ETIMEDOUT == sem_timedwait(&pdrvObj->m_semReceiveQueue, &ts)) {
        return CANAL_ERROR_TIMEOUT;
    }

    pthread_mutex_lock(&pdrvObj->m_mutexReceiveQueue);
    vscpEvent *pLocalEvent = pdrvObj->m_receiveList.front();
    pdrvObj->m_receiveList.pop_front();
    pthread_mutex_unlock(&pdrvObj->m_mutexReceiveQueue);
    if (NULL == pLocalEvent) return CANAL_ERROR_MEMORY;

    vscp_copyVSCPEvent(pEvent, pLocalEvent);
    vscp_deleteVSCPevent(pLocalEvent);

    return CANAL_ERROR_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// VSCPGetVersion
//

extern "C" unsigned long
VSCPGetVersion(void)
{
    return VSCP_DLL_VERSION;
}

///////////////////////////////////////////////////////////////////////////////
// VSCPGetVendorString
//

extern "C" const char *
VSCPGetVendorString(void)
{
    return VSCP_DLL_VENDOR;
}

///////////////////////////////////////////////////////////////////////////////
// VSCPGetDriverInfo
//

extern "C" const char *
VSCPGetDriverInfo(void)
{
    return VSCP_AUTOMATION_DRIVERINFO;
}

///////////////////////////////////////////////////////////////////////////////
//  VSCPGetConfigPageInfo
//

extern "C" int
VSCPGetConfigPageInfo(long handle,
                      unsigned char ordinal,
                      const char *pReplyObj,
                      unsigned long *pSize)
{

    // Not implemented
    return -1;
}

///////////////////////////////////////////////////////////////////////////////
//  VSCPConfigPageUpdate
//

extern "C" int
VSCPConfigPageUpdate(long handle,
                     const char *pUpdateObj,
                     const char *pReplyObj,
                     unsigned long *pSize)
{
    // Not implemented
    return -1;
}

///////////////////////////////////////////////////////////////////////////////
//  VSCPGetCommandInfo
//

extern "C" int
VSCPGetCommandInfo(long handle,
                   unsigned char ordinal,
                   const char *pReplyObj,
                   unsigned long *pSize)
{
    // Not implemented
    return -1;
}

///////////////////////////////////////////////////////////////////////////////
//  VSCPExecuteCommand
//

extern "C" int
VSCPExecuteCommand(long handle,
                   const unsigned char type,
                   const char *pCmdObj,
                   const char *pReplyObj,
                   uint32_t *pSize)
{
    // Not implemented
    return -1;
}