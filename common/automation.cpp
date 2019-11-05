// automation.cpp
//
// This file is part of the VSCP (http://www.vscp.org)
//
// The MIT License (MIT)
//
// Copyright (C) 2000-2019 Ake Hedman, Grodans Paradis AB
// <info@grodansparadis.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice cat /sys/class/net/eth0/addressand this permission
// notice shall be included in all copies or substantial portions of the
// Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

#ifdef __GNUG__
//#pragma implementation
#endif

#define _POSIX

#include <list>
#include <string>

#include <limits.h>
#include <net/if.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>
#include <math.h>

#include <ctype.h>
#include <errno.h>
#include <libgen.h>
#include <net/if.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <time.h>

#include <expat.h>

#include <vscp.h>
#include <vscp_class.h>
#include <vscp_type.h>
#include <vscphelper.h>
#include <vscpremotetcpif.h>

#include "automation.h"

// Buffer for XML parser
#define XML_BUFF_SIZE 10000

// Forward declaration
void *
workerThread(void *pData);


///////////////////////////////////////////////////
//                 GLOBALS
///////////////////////////////////////////////////

// Seconds for 24h
#define SPAN24 (24 * 3600)

//-----------------------------------------------------------------------------
//                   Helpers for sunrise/sunset calculations
//-----------------------------------------------------------------------------

// C program calculating the sunrise and sunset for
// the current date and a fixed location(latitude,longitude)
// Note, twilight calculation gives insufficient accuracy of results
// Jarmo Lammi 1999 - 2001
// Last update July 21st, 2001

static double pi = 3.14159;
static double degs;
static double rads;

static double L, g, daylen;
static double SunDia = 0.53; // Sun radius degrees

static double AirRefr = 34.0 / 60.0; // atmospheric refraction degrees //

//-----------------------------------------------------------------------------
//                       End of sunset/sunrise functions
//-----------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////////
// Constructor
//

CAutomation::CAutomation(void)
{
    m_bEnableAutomation = true;

    m_zone    = 0;
    m_subzone = 0;

    // Take me the freedom to use my own place as reference
    m_longitude = 15.1604167; // Home sweet home
    m_latitude  = 61.7441833;

    m_bSunRiseEvent         = true;
    m_bSunRiseTwilightEvent = true;
    m_bSunSetEvent          = true;
    m_bSunSetTwilightEvent  = true;
    m_bCalculatedNoonEvent  = true;

    m_declination    = 0.0f;
    m_daylength      = 0.0f;
    m_SunMaxAltitude = 0.0f;

    m_bCalulationHasBeenDone = false; // No calculations has been done yet

    // Set to some early date to indicate that they have not been sent
    m_civilTwilightSunriseTime_sent = vscpdatetime::dateTimeZero();
    m_SunriseTime_sent              = vscpdatetime::dateTimeZero();
    m_SunsetTime_sent               = vscpdatetime::dateTimeZero();
    m_civilTwilightSunsetTime_sent  = vscpdatetime::dateTimeZero();
    m_noonTime_sent                 = vscpdatetime::dateTimeZero();

    m_lastCalculation = vscpdatetime::Now();

    m_bQuit = false;
    vscp_clearVSCPFilter(&m_vscpfilter); // Accept all events

    sem_init(&m_semSendQueue, 0, 0);
    sem_init(&m_semReceiveQueue, 0, 0);

    pthread_mutex_init(&m_mutexSendQueue, NULL);
    pthread_mutex_init(&m_mutexReceiveQueue, NULL);
}

///////////////////////////////////////////////////////////////////////////////
// Destructor
//

CAutomation::~CAutomation(void)
{
    close();

    sem_destroy(&m_semSendQueue);
    sem_destroy(&m_semReceiveQueue);

    pthread_mutex_destroy(&m_mutexSendQueue);
    pthread_mutex_destroy(&m_mutexReceiveQueue);
}

// ----------------------------------------------------------------------------

/*
    XML Setup
    =========

    <config zone="1"
            subzone="2"
            longitude="15.1604167"
            latitude="61.7441833"
            enable-sunrise="true|false"
            enable-sunrise-twilight="true|false"
            enable-sunset enable="true|false" />
            enable-sunset-twilight="true|false"
            filter="incoming-filter"
            mask="incoming-mask" >
*/

// ----------------------------------------------------------------------------

int depth_setup_parser = 0;

void
startSetupParser(void *data, const char *name, const char **attr)
{
    CAutomation *pObj = (CAutomation *)data;
    if (NULL == pObj) return;

    if ((0 == strcmp(name, "setup")) && (0 == depth_setup_parser)) {

        for (int i = 0; attr[i]; i += 2) {

            std::string attribute = attr[i + 1];
            vscp_trim(attribute);

            if (0 == strcasecmp(attr[i], "interface")) {
                if (!attribute.empty()) {
                    //pObj->m_interface = attribute;
                }
            } else if (0 == strcasecmp(attr[i], "filter")) {
                if (!attribute.empty()) {
                    // if (!vscp_readFilterFromString(&pObj->m_vscpfilter,
                    //                                attribute)) {
                    //     syslog(LOG_ERR, "[vscpl2drv-automation] Unable to read event receive filter.");
                    // }
                }
            } else if (0 == strcasecmp(attr[i], "mask")) {
                if (!attribute.empty()) {
                    // if (!vscp_readMaskFromString(&pObj->m_vscpfilter,
                    //                              attribute)) {
                    //     syslog(LOG_ERR, "[vscpl2drv-automation] Unable to read event receive mask.");
                    // }
                }
            }
        }
    }

    depth_setup_parser++;
}

void
endSetupParser(void *data, const char *name)
{
    depth_setup_parser--;
}

// ----------------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////
// open
//

bool
CAutomation::open(const std::string &path)
{
    char buf[XML_BUFF_SIZE];

    // XML setup
    std::string strSetupXML;

    // Read configuration file

    XML_Parser xmlParser = XML_ParserCreate("UTF-8");
    XML_SetUserData(xmlParser, this);
    XML_SetElementHandler(xmlParser, startSetupParser, endSetupParser);

     int bytes_read;
    void *buff = XML_GetBuffer(xmlParser, XML_BUFF_SIZE);

     strncpy((char *)buf, strSetupXML.c_str(), strSetupXML.length());

    bytes_read = strSetupXML.length();
    if (!XML_ParseBuffer(xmlParser, bytes_read, bytes_read == 0)) {
        syslog(LOG_ERR, "[vscpl2drv-automation] Failed parse XML setup.");
        XML_ParserFree(xmlParser);
        return false;
    }

    XML_ParserFree(xmlParser);

    // start the workerthread
    if (pthread_create(&m_threadWork, NULL, workerThread, this)) {

        syslog(LOG_ERR, "[vscpl2drv-automation] Unable to start worker thread.");
        return false;
    }

    // Close the channel
    close();

    return true;
}

//////////////////////////////////////////////////////////////////////
// close
//

void
CAutomation::close(void)
{
    // Do nothing if already terminated
    if (m_bQuit) return;

    m_bQuit = true; // terminate the thread
    sleep(1);       // Give the thread some time to terminate
}



///////////////////////////////////////////////////////////////////////////////
// isDaylightSavingTime
//

int
CAutomation::isDaylightSavingTime(void)
{
    time_t rawtime;
    struct tm *timeinfo;

    time(&rawtime);
    timeinfo = localtime(&rawtime);
    return timeinfo->tm_isdst;
}

///////////////////////////////////////////////////////////////////////////////
// getTimeZoneDiffHours
//

int
CAutomation::getTimeZoneDiffHours(void)
{
    time_t rawtime;
    struct tm *timeinfo;
    struct tm *timeinfo_gmt;
    int h1, h2;

    time(&rawtime);
    timeinfo = localtime(&rawtime);
    h2       = timeinfo->tm_hour;
    if (0 == h2) h2 = 24;

    timeinfo_gmt = gmtime(&rawtime);
    h1           = timeinfo_gmt->tm_hour;

    return (h2 - h1);
}

///////////////////////////////////////////////////////////////////////////////
// FNday
//
// Get the days to J2000
// h is UT in decimal hours
// FNday only works between 1901 to 2099 - see Meeus chapter 7
//

double
CAutomation::FNday(int y, int m, int d, float h)
{
    long int luku = -7 * (y + (m + 9) / 12) / 4 + 275 * m / 9 + d;
    // type casting necessary on PC DOS and TClite to avoid overflow
    luku += (long int)y * 367;
    return (double)luku - 730531.5 + h / 24.0;
};

///////////////////////////////////////////////////////////////////////////////
// FNrange
//
// the function below returns an angle in the range
// 0 to 2*pi
//

double
CAutomation::FNrange(double x)
{
    double b = 0.5 * x / pi;
    double a = 2.0 * pi * (b - (long)(b));
    if (a < 0) a = 2.0 * pi + a;
    return a;
};

///////////////////////////////////////////////////////////////////////////////
// f0
//
// Calculating the hourangle
//

double
CAutomation::f0(double lat, double declin)
{
    double fo, dfo;
    // Correction: different sign at S HS
    dfo = rads * (0.5 * SunDia + AirRefr);
    if (lat < 0.0) dfo = -dfo;
    fo = tan(declin + dfo) * tan(lat * rads);
    if (fo > 0.99999) fo = 1.0; // to avoid overflow //
    fo = asin(fo) + pi / 2.0;
    return fo;
};

///////////////////////////////////////////////////////////////////////////////
// f1
//
// Calculating the hourangle for twilight times
//

double
CAutomation::f1(double lat, double declin)
{
    double fi, df1;
    // Correction: different sign at S HS
    df1 = rads * 6.0;
    if (lat < 0.0) df1 = -df1;
    fi = tan(declin + df1) * tan(lat * rads);
    if (fi > 0.99999) fi = 1.0; // to avoid overflow //
    fi = asin(fi) + pi / 2.0;
    return fi;
};

///////////////////////////////////////////////////////////////////////////////
// FNsun
//
//   Find the ecliptic longitude of the Sun

double
CAutomation::FNsun(double d)
{

    //   mean longitude of the Sun
    L = FNrange(280.461 * rads + .9856474 * rads * d);

    //   mean anomaly of the Sun
    g = FNrange(357.528 * rads + .9856003 * rads * d);

    //   Ecliptic longitude of the Sun
    return FNrange(L + 1.915 * rads * sin(g) + .02 * rads * sin(2 * g));
};

///////////////////////////////////////////////////////////////////////////////
// convert2HourMinute
//
// Display decimal hours in hours and minutes
//

void
CAutomation::convert2HourMinute(double floatTime, int *pHours, int *pMinutes)
{
    *pHours   = ((int)floatTime) % 24;
    *pMinutes = ((int)((floatTime - (double)*pHours) * 60)) % 60;
};

///////////////////////////////////////////////////////////////////////////////
// calcSun
//

void
CAutomation::calcSun(void)
{
    double year, month, day, hour;
    double d, lambda;
    double obliq, alpha, delta, LL, equation, ha, hb, twx;
    double twilightSunrise, maxAltitude, noonTime, sunsetTime, sunriseTime,
      twilightSunset;
    double tzone = 0;

    degs = 180.0 / pi;
    rads = pi / 180.0;

    // get the date and time from the user
    // read system date and extract the year

    // First get time
    vscpdatetime nowLocal = vscpdatetime::Now();
    year                  = nowLocal.getYear();
    month                 = nowLocal.getMonth() + 1;
    day                   = nowLocal.getDay();
    hour                  = nowLocal.getHour();

    // Set offset from UTC - daytimesaving is taken care of
    // vscpdatetime::TimeZone ttt( vscpdatetime::Local );
    // tzone = ttt.GetOffset()/3600;
    tzone = vscpdatetime::tzOffset2LocalTime(); // TODO CHECK!!!!!!

    // Adjust for daylight saving time
    /*if ( isDaylightSavingTime() ) {
        tzone = getTimeZoneDiffHours();
    }*/

    d = FNday(year, month, day, hour);

    // Use FNsun to find the ecliptic longitude of the Sun
    lambda = FNsun(d);

    //   Obliquity of the ecliptic
    obliq = 23.439 * rads - 0.0000004 * rads * d;

    //   Find the RA and DEC of the Sun
    alpha = atan2(cos(obliq) * sin(lambda), cos(lambda));
    delta = asin(sin(obliq) * sin(lambda));

    // Find the Equation of Time
    // in minutes
    // Correction suggested by David Smith
    LL = L - alpha;
    if (L < pi) LL += 2.0 * pi;
    equation = 1440.0 * (1.0 - LL / pi / 2.0);
    ha       = f0(m_latitude, delta);
    hb       = f1(m_latitude, delta);
    twx      = hb - ha;         // length of twilight in radians
    twx      = 12.0 * twx / pi; // length of twilight in hours

    // Conversion of angle to hours and minutes
    daylen = degs * ha / 7.5;
    if (daylen < 0.0001) {
        daylen = 0.0;
    }

    // arctic winter
    sunriseTime =
      12.0 - 12.0 * ha / pi + tzone - m_longitude / 15.0 + equation / 60.0;
    sunsetTime =
      12.0 + 12.0 * ha / pi + tzone - m_longitude / 15.0 + equation / 60.0;
    noonTime    = sunriseTime + 12.0 * ha / pi;
    maxAltitude = 90.0 + delta * degs - m_latitude;
    // Correction for S HS suggested by David Smith
    // to express altitude as degrees from the N horizon
    if (m_latitude < delta * degs) maxAltitude = 180.0 - maxAltitude;

    twilightSunrise = sunriseTime - twx; // morning twilight begin
    twilightSunset  = sunsetTime + twx;  // evening twilight end

    if (sunriseTime > 24.0) sunriseTime -= 24.0;
    if (sunsetTime > 24.0) sunsetTime -= 24.0;
    if (twilightSunrise > 24.0) twilightSunrise -= 24.0; // 160921
    if (twilightSunset > 24.0) twilightSunset -= 24.0;

    m_declination    = delta * degs;
    m_daylength      = daylen;
    m_SunMaxAltitude = maxAltitude;

    // Set last calculated time
    m_lastCalculation = vscpdatetime::Now();

    int intHour, intMinute;

    // Civil Twilight Sunrise
    convert2HourMinute(twilightSunrise, &intHour, &intMinute);
    m_civilTwilightSunriseTime = vscpdatetime::Now();
    m_civilTwilightSunriseTime.zeroTime(); // Set to midnight
    m_civilTwilightSunriseTime.setHour(intHour);
    m_civilTwilightSunriseTime.setMinute(intMinute);

    // Sunrise
    convert2HourMinute(sunriseTime, &intHour, &intMinute);
    m_SunriseTime = vscpdatetime::Now();
    m_SunriseTime.zeroTime(); // Set to midnight
    m_SunriseTime.setHour(intHour);
    m_SunriseTime.setMinute(intMinute);

    // Sunset
    convert2HourMinute(sunsetTime, &intHour, &intMinute);
    m_SunsetTime = vscpdatetime::Now();
    m_SunsetTime.zeroTime(); // Set to midnight
    m_SunsetTime.setHour(intHour);
    m_SunsetTime.setMinute(intMinute);

    // Civil Twilight Sunset
    convert2HourMinute(twilightSunset, &intHour, &intMinute);
    m_civilTwilightSunsetTime = vscpdatetime::Now();
    m_civilTwilightSunsetTime.zeroTime(); // Set to midnight
    m_civilTwilightSunsetTime.setHour(intHour);
    m_civilTwilightSunsetTime.setMinute(intMinute);

    // NoonTime
    convert2HourMinute(noonTime, &intHour, &intMinute);
    m_noonTime = vscpdatetime::Now();
    m_noonTime.zeroTime(); // Set to midnight
    m_noonTime.setHour(intHour);
    m_noonTime.setMinute(intMinute);
}

///////////////////////////////////////////////////////////////////////////////
// doWork
//

bool
CAutomation::doWork(vscpEventEx *pEventEx)
{
    std::string str;
    vscpdatetime now = vscpdatetime::Now();

    // Calculate Sunrise/sunset parameters once a day
    if (!m_bCalulationHasBeenDone && (0 == vscpdatetime::Now().getHour())) {

        calcSun();
        m_bCalulationHasBeenDone = true;

        int hours, minutes;
        convert2HourMinute(getDayLength(), &hours, &minutes);

        // Send VSCP_CLASS2_VSCPD, Type=30/VSCP2_TYPE_VSCPD_NEW_CALCULATION
        pEventEx->obid =
          0; // IMPORTANT Must be set by caller before event is sent
        pEventEx->head      = 0;
        pEventEx->timestamp = vscp_makeTimeStamp();
        vscp_setEventExToNow(pEventEx); // Set time to current time
        pEventEx->vscp_class = VSCP_CLASS2_VSCPD;
        pEventEx->vscp_type  = VSCP2_TYPE_VSCPD_NEW_CALCULATION;
        pEventEx->sizeData   = 0;

        // IMPORTANT - GUID must be set by caller before event is sent

        return true;
    }

    // Trigger for next noon calculation
    if (0 != vscpdatetime::Now().getHour()) {
        m_bCalulationHasBeenDone = false;
    }

    // Sunrise Time
    if ((now.getYear() == m_SunriseTime.getYear()) &&
        (now.getMonth() == m_SunriseTime.getMonth()) &&
        (now.getDay() == m_SunriseTime.getDay()) &&
        (now.getHour() == m_SunriseTime.getHour()) &&
        (now.getMinute() == m_SunriseTime.getMinute())) {

        m_SunriseTime += SPAN24; // Add 24h's
        m_SunriseTime_sent = vscpdatetime::Now();

        // Send VSCP_CLASS1_INFORMATION, Type=44/VSCP_TYPE_INFORMATION_SUNRISE
        pEventEx->obid =
          0; // IMPORTANT Must be set by caller before event is sent
        pEventEx->head      = 0;
        pEventEx->timestamp = vscp_makeTimeStamp();
        vscp_setEventExToNow(pEventEx); // Set time to current time
        pEventEx->vscp_class = VSCP_CLASS1_INFORMATION;
        pEventEx->vscp_type  = VSCP_TYPE_INFORMATION_SUNRISE;
        pEventEx->sizeData   = 3;
        // IMPORTANT - GUID must be set by caller before event is sent
        pEventEx->data[0] = 0;         // index
        pEventEx->data[1] = m_zone;    // zone
        pEventEx->data[2] = m_subzone; // subzone

        return true;
    }

    // Civil Twilight Sunrise Time
    if ((now.getYear() == m_civilTwilightSunriseTime.getYear()) &&
        (now.getMonth() == m_civilTwilightSunriseTime.getMonth()) &&
        (now.getDay() == m_civilTwilightSunriseTime.getDay()) &&
        (now.getHour() == m_civilTwilightSunriseTime.getHour()) &&
        (now.getMinute() == m_civilTwilightSunriseTime.getMinute())) {

        m_civilTwilightSunriseTime += SPAN24; // Add 24h's
        m_civilTwilightSunriseTime_sent = vscpdatetime::Now();

        // Send VSCP_CLASS1_INFORMATION,
        // Type=52/VSCP_TYPE_INFORMATION_SUNRISE_TWILIGHT_START
        pEventEx->obid =
          0; // IMPORTANT Must be set by caller before event is sent
        pEventEx->head      = 0;
        pEventEx->timestamp = vscp_makeTimeStamp();
        vscp_setEventExToNow(pEventEx); // Set time to current time
        pEventEx->vscp_class = VSCP_CLASS1_INFORMATION;
        pEventEx->vscp_type  = VSCP_TYPE_INFORMATION_SUNRISE_TWILIGHT_START;
        pEventEx->sizeData   = 3;
        // IMPORTANT - GUID must be set by caller before event is sent
        pEventEx->data[0] = 0;         // index
        pEventEx->data[1] = m_zone;    // zone
        pEventEx->data[2] = m_subzone; // subzone

        return true;
    }

    // Sunset Time
    if ((now.getYear() == m_SunsetTime.getYear()) &&
        (now.getMonth() == m_SunsetTime.getMonth()) &&
        (now.getDay() == m_SunsetTime.getDay()) &&
        (now.getHour() == m_SunsetTime.getHour()) &&
        (now.getMinute() == m_SunsetTime.getMinute())) {

        m_SunsetTime += SPAN24; // Add 24h's
        m_SunsetTime_sent = vscpdatetime::Now();

        // Send VSCP_CLASS1_INFORMATION, Type=45/VSCP_TYPE_INFORMATION_SUNSET
        pEventEx->obid =
          0; // IMPORTANT Must be set by caller before event is sent
        pEventEx->head      = 0;
        pEventEx->timestamp = vscp_makeTimeStamp();
        vscp_setEventExToNow(pEventEx); // Set time to current time
        pEventEx->vscp_class = VSCP_CLASS1_INFORMATION;
        pEventEx->vscp_type  = VSCP_TYPE_INFORMATION_SUNSET;
        pEventEx->sizeData   = 3;
        // IMPORTANT - GUID must be set by caller before event is sent
        pEventEx->data[0] = 0;         // index
        pEventEx->data[1] = m_zone;    // zone
        pEventEx->data[2] = m_subzone; // subzone

        return true;
    }

    // Civil Twilight Sunset Time
    if ((now.getYear() == m_civilTwilightSunsetTime.getYear()) &&
        (now.getMonth() == m_civilTwilightSunsetTime.getMonth()) &&
        (now.getDay() == m_civilTwilightSunsetTime.getDay()) &&
        (now.getHour() == m_civilTwilightSunsetTime.getHour()) &&
        (now.getMinute() == m_civilTwilightSunsetTime.getMinute())) {

        m_civilTwilightSunsetTime += SPAN24; // Add 24h's
        m_civilTwilightSunsetTime_sent = vscpdatetime::Now();

        // Send VSCP_CLASS1_INFORMATION,
        // Type=53/VSCP_TYPE_INFORMATION_SUNSET_TWILIGHT_START
        pEventEx->obid =
          0; // IMPORTANT Must be set by caller before event is sent
        pEventEx->head      = 0;
        pEventEx->timestamp = vscp_makeTimeStamp();
        vscp_setEventExToNow(pEventEx); // Set time to current time
        pEventEx->vscp_class = VSCP_CLASS1_INFORMATION;
        pEventEx->vscp_type  = VSCP_TYPE_INFORMATION_SUNSET_TWILIGHT_START;
        pEventEx->sizeData   = 3;
        // IMPORTANT - GUID must be set by caller before event is sent
        pEventEx->data[0] = 0;         // index
        pEventEx->data[1] = m_zone;    // zone
        pEventEx->data[2] = m_subzone; // subzone

        return true;
    }

    // Noon Time
    if ((now.getYear() == m_noonTime.getYear()) &&
        (now.getMonth() == m_noonTime.getMonth()) &&
        (now.getDay() == m_noonTime.getDay()) &&
        (now.getHour() == m_noonTime.getHour()) &&
        (now.getMinute() == m_noonTime.getMinute())) {

        m_noonTime += SPAN24; // Add 24h's
        m_noonTime_sent = vscpdatetime::Now();

        // Send VSCP_CLASS1_INFORMATION,
        // Type=58/VSCP_TYPE_INFORMATION_CALCULATED_NOON
        pEventEx->obid =
          0; // IMPORTANT Must be set by caller before event is sent
        pEventEx->head      = 0;
        pEventEx->timestamp = vscp_makeTimeStamp();
        vscp_setEventExToNow(pEventEx); // Set time to current time
        pEventEx->vscp_class = VSCP_CLASS1_INFORMATION;
        pEventEx->vscp_type  = VSCP_TYPE_INFORMATION_CALCULATED_NOON;
        pEventEx->sizeData   = 3;
        // IMPORTANT - GUID must be set by caller before event is sent
        pEventEx->data[0] = 0;         // index
        pEventEx->data[1] = m_zone;    // zone
        pEventEx->data[2] = m_subzone; // subzone

        return true;
    }

    return false;
}

// ----------------------------------------------------------------------------



//////////////////////////////////////////////////////////////////////
// addEvent2SendQueue
//

bool
CAutomation::addEvent2SendQueue(const vscpEvent *pEvent)
{
    pthread_mutex_lock(&m_mutexSendQueue);
    m_sendList.push_back((vscpEvent *)pEvent);
    sem_post(&m_semSendQueue);
    pthread_mutex_unlock(&m_mutexSendQueue);
    return true;
}

//////////////////////////////////////////////////////////////////////
//                Workerthread - CAutomationWorkerTread
//////////////////////////////////////////////////////////////////////

void *
workerThread(void *pData)
{
    int sock;
    char devname[IFNAMSIZ + 1];
    fd_set rdfs;
    struct timeval tv;

    CAutomation *pObj = (CAutomation *)pData;
    if (NULL == pObj) {
        syslog(LOG_ERR, "[vscpl2drv-automation] No object data supplied for worker thread");
        return NULL;
    }

#if DEBUG
    syslog(LOG_DEBUG, "[vscpl2drv-automation] CWriteSocketCanTread: Interface: %s\n", ifname);
#endif

    while (!pObj->m_bQuit) {



#ifdef DEBUG
        syslog(LOG_DEBUG, "using interface name '%s'.\n", ifr.ifr_name);
#endif

        bool bInnerLoop = true;
        while (pObj->m_bQuit && bInnerLoop) {

            // FD_ZERO(&rdfs);
            // FD_SET(sock, &rdfs);

            // tv.tv_sec  = 0;
            // tv.tv_usec = 5000; // 5ms timeout

            // int ret;
            // if ((ret = select(sock + 1, &rdfs, NULL, NULL, &tv)) < 0) {
            //     // Error
            //     if (ENETDOWN == errno) {
            //         // We try to get contact with the net
            //         // again if it goes down
            //         bInnerLoop = false;
            //     } else {
            //         pObj->m_bQuit = true;
            //     }
            //     continue;
            // }

            // if (ret) {

            //     // There is data to read

            //     ret = read(sock, &frame, sizeof(struct can_frame));
            //     if (ret < 0) {
            //         if (ENETDOWN == errno) {
            //             // We try to get contact with the net
            //             // again if it goes down
            //             bInnerLoop = false;
            //             sleep(2);
            //         } else {
            //             pObj->m_bQuit = true;
            //         }
            //         continue;
            //     }

            //     // Must be Extended
            //     if (!(frame.can_id & CAN_EFF_FLAG)) continue;

            //     // Mask of control bits
            //     frame.can_id &= CAN_EFF_MASK;

            //     vscpEvent *pEvent = new vscpEvent();
            //     if (NULL != pEvent) {

            //         pEvent->pdata = new uint8_t[frame.len];
            //         if (NULL == pEvent->pdata) {
            //             delete pEvent;
            //             continue;
            //         }

            //         // GUID will be set to GUID of interface
            //         // by driver interface with LSB set to nickname
            //         memset(pEvent->GUID, 0, 16);
            //         pEvent->GUID[VSCP_GUID_LSB] = frame.can_id & 0xff;

            //         // Set VSCP class
            //         pEvent->vscp_class =
            //           vscp_getVSCPclassFromCANALid(frame.can_id);

            //         // Set VSCP type
            //         pEvent->vscp_type =
            //           vscp_getVSCPtypeFromCANALid(frame.can_id);

            //         // Copy data if any
            //         pEvent->sizeData = frame.len;
            //         if (frame.len) {
            //             memcpy(pEvent->pdata, frame.data, frame.len);
            //         }

            //         if (vscp_doLevel2Filter(pEvent, &pObj->m_vscpfilter)) {
            //             pthread_mutex_lock(&pObj->m_mutexReceiveQueue);
            //             pObj->m_receiveList.push_back(pEvent);
            //             sem_post(&pObj->m_semReceiveQueue);
            //             pthread_mutex_unlock(&pObj->m_mutexReceiveQueue);
            //         } else {
            //             vscp_deleteVSCPevent(pEvent);
            //         }
            //     }

            // } else {

            //     // Check if there is event(s) to send
            //     if (pObj->m_sendList.size()) {

            //         // Yes there are data to send
            //         // So send it out on the CAN bus

            //         pthread_mutex_lock(&pObj->m_mutexSendQueue);
            //         vscpEvent *pEvent = pObj->m_sendList.front();
            //         pObj->m_sendList.pop_front();
            //         pthread_mutex_unlock(&pObj->m_mutexSendQueue);

            //         if (NULL == pEvent) continue;

            //         // Class must be a Level I class or a Level II
            //         // mirror class
            //         if (pEvent->vscp_class < 512) {
            //             frame.can_id = vscp_getCANALidFromVSCPevent(pEvent);
            //             frame.can_id |= CAN_EFF_FLAG; // Always extended
            //             if (0 != pEvent->sizeData) {
            //                 frame.len =
            //                   (pEvent->sizeData > 8 ? 8 : pEvent->sizeData);
            //                 memcpy(frame.data, pEvent->pdata, frame.len);
            //             }
            //         } else if (pEvent->vscp_class < 1024) {
            //             pEvent->vscp_class -= 512;
            //             frame.can_id = vscp_getCANALidFromVSCPevent(pEvent);
            //             frame.can_id |= CAN_EFF_FLAG; // Always extended
            //             if (0 != pEvent->sizeData) {
            //                 frame.len = ((pEvent->sizeData - 16) > 8
            //                                ? 8
            //                                : pEvent->sizeData - 16);
            //                 memcpy(frame.data, pEvent->pdata + 16, frame.len);
            //             }
            //         }

            //         // Remove the event
            //         pthread_mutex_lock(&pObj->m_mutexSendQueue);
            //         vscp_deleteVSCPevent(pEvent);
            //         pthread_mutex_unlock(&pObj->m_mutexSendQueue);

            //         // Write the data
            //         int nbytes = write(sock, &frame, sizeof(struct can_frame));

            //     } // event to send

            // } // No data to read

        } // Inner loop

    } // Outer loop

    return NULL;
}
