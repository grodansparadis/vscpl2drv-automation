// automation.cpp
//
// This file is part of the VSCP (http://www.vscp.org)
//
// The MIT License (MIT)
//
// Copyright (C) 2000-2021 Ake Hedman, Grodans Paradis AB
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
#include <math.h>
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

#include <hlo.h>
#include <remotevariablecodes.h>
#include <vscp.h>
#include <vscp_class.h>
#include <vscp_type.h>
#include <vscphelper.h>
#include <vscpremotetcpif.h>

#include <json.hpp>  // Needs C++11  -std=c++11
#include <mustache.hpp>

#include "automation.h"

#include <iostream>
#include <fstream>      
#include <list>
#include <map>
#include <string>

// https://github.com/nlohmann/json
using json = nlohmann::json;

using namespace kainjow::mustache;

// Buffer for XML parser
//#define XML_BUFF_SIZE 30000

// Forward declaration
void*
workerThread(void* pData);

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
    m_bDebug = false;
    m_bQuit = false;

    m_bEnableAutomation = true;

    m_zone = 0;
    m_subzone = 0;

    // Not possible to save configuration by default
    m_bWrite = false;

    // Take me the freedom to use my own place as reference
    m_longitude = 15.1604167; // Home sweet home
    m_latitude = 61.7441833;

    m_bSunRiseEvent = true;
    m_bSunRiseTwilightEvent = true;
    m_bSunSetEvent = true;
    m_bSunSetTwilightEvent = true;
    m_bCalculatedNoonEvent = true;

    m_declination = 0.0f;
    m_daylength = 0.0f;
    m_SunMaxAltitude = 0.0f;

    m_bCalulationHasBeenDone = false; // No calculations has been done yet

    // Set to some early date to indicate that they have not been sent
    m_civilTwilightSunriseTime_sent = vscpdatetime::dateTimeZero();
    m_SunriseTime_sent = vscpdatetime::dateTimeZero();
    m_SunsetTime_sent = vscpdatetime::dateTimeZero();
    m_civilTwilightSunsetTime_sent = vscpdatetime::dateTimeZero();
    m_noonTime_sent = vscpdatetime::dateTimeZero();

    m_lastCalculation = vscpdatetime::Now();

    vscp_clearVSCPFilter(&m_vscpfilter); // Accept all events

    sem_init(&m_semSendQueue, 0, 0);
    sem_init(&m_semReceiveQueue, 0, 0);

    pthread_mutex_init(&m_mutexSendQueue, NULL);
    pthread_mutex_init(&m_mutexReceiveQueue, NULL);

    // Do initial calculations
    doCalc();
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

    <?xml version = "1.0" encoding = "UTF-8" ?>
    <!-- Version 0.0.1    2019-11-05   -->
    <config debug="true|false"
            write="true|false"
            guid="FF:FF:FF:FF:FF:FF:FF:FC:88:99:AA:BB:CC:DD:EE:FF"
            zone="1"
            subzone="2"
            longitude="15.1604167"
            latitude="61.7441833"
            enable-sunrise="true|false"
            enable-sunrise-twilight="true|false"
            enable-sunset="true|false"
            enable-sunset-twilight="true|false"
            enable-noon="true|false"
            filter="incoming-filter"
            mask="incoming-mask" />
*/

// ----------------------------------------------------------------------------

// int depth_setup_parser = 0;

// void
// startSetupParser(void* data, const char* name, const char** attr)
// {
//     CAutomation* pObj = (CAutomation*)data;
//     if (NULL == pObj)
//         return;

//     if ((0 == strcmp(name, "config")) && (0 == depth_setup_parser)) {

//         for (int i = 0; attr[i]; i += 2) {

//             std::string attribute = attr[i + 1];
//             vscp_trim(attribute);

//             if (0 == strcasecmp(attr[i], "zone")) {
//                 if (!attribute.empty()) {
//                     pObj->m_zone = vscp_readStringValue(attribute);
//                 }
//             } else if (0 == strcasecmp(attr[i], "subzone")) {
//                 if (!attribute.empty()) {
//                     pObj->m_subzone = vscp_readStringValue(attribute);
//                 }
//             } else if (0 == strcasecmp(attr[i], "longitude")) {
//                 if (!attribute.empty()) {
//                     pObj->setLongitude(atof(attribute.c_str()));
//                 }
//             } else if (0 == strcasecmp(attr[i], "latitude")) {
//                 if (!attribute.empty()) {
//                     pObj->setLatitude(atof(attribute.c_str()));
//                 }
//             } else if (0 == strcasecmp(attr[i], "enable-sunrise")) {
//                 if (!attribute.empty()) {
//                     vscp_makeUpper(attribute);
//                     if (std::string::npos != attribute.find("TRUE")) {
//                         pObj->enableSunRiseEvent();
//                     } else {
//                         pObj->disableSunRiseEvent();
//                     }
//                 }
//             } else if (0 == strcasecmp(attr[i], "enable-sunrise-twilight")) {
//                 if (!attribute.empty()) {
//                     vscp_makeUpper(attribute);
//                     if (std::string::npos != attribute.find("TRUE")) {
//                         pObj->enableSunSetEvent();
//                     } else {
//                         pObj->disableSunSetEvent();
//                     }
//                 }
//             } else if (0 == strcasecmp(attr[i], "enable-sunset")) {
//                 if (!attribute.empty()) {
//                     vscp_makeUpper(attribute);
//                     if (std::string::npos != attribute.find("TRUE")) {
//                         pObj->enableSunRiseTwilightEvent();
//                     } else {
//                         pObj->disableSunRiseTwilightEvent();
//                     }
//                 }
//             } else if (0 == strcasecmp(attr[i], "enable-sunset-twilight")) {
//                 if (!attribute.empty()) {
//                     vscp_makeUpper(attribute);
//                     if (std::string::npos != attribute.find("TRUE")) {
//                         pObj->enableSunSetTwilightEvent();
//                     } else {
//                         pObj->disableSunSetTwilightEvent();
//                     }
//                 }
//             } else if (0 == strcasecmp(attr[i], "enable-noon")) {
//                 if (!attribute.empty()) {
//                     vscp_makeUpper(attribute);
//                     if (std::string::npos != attribute.find("TRUE")) {
//                         pObj->enableCalculatedNoonEvent();
//                     } else {
//                         pObj->disableCalculatedNoonEvent();
//                     }
//                 }
//             } else if (0 == strcasecmp(attr[i], "debug")) {
//                 if (!attribute.empty()) {
//                     vscp_makeUpper(attribute);
//                     if (std::string::npos != attribute.find("TRUE")) {
//                         pObj->m_bDebug = true;
//                     } else {
//                         pObj->m_bDebug = false;
//                     }
//                 }
//             } else if (0 == strcasecmp(attr[i], "write")) {
//                 if (!attribute.empty()) {
//                     vscp_makeUpper(attribute);
//                     if (std::string::npos != attribute.find("TRUE")) {
//                         pObj->enableWrite();
//                     } else {
//                         pObj->disableWrite();
//                     }
//                 }
//             } else if (0 == strcasecmp(attr[i], "filter")) {
//                 if (!attribute.empty()) {
//                     if (!vscp_readFilterFromString(&pObj->m_vscpfilter,
//                                                    attribute)) {
//                         syslog(LOG_ERR,
//                                "[vscpl2drv-automation] Unable to read event "
//                                "receive filter.");
//                     }
//                 }
//             } else if (0 == strcasecmp(attr[i], "mask")) {
//                 if (!attribute.empty()) {
//                     if (!vscp_readMaskFromString(&pObj->m_vscpfilter,
//                                                  attribute)) {
//                         syslog(LOG_ERR,
//                                "[vscpl2drv-automation] Unable to read event "
//                                "receive mask.");
//                     }
//                 }
//             }
//         }
//     }

//     depth_setup_parser++;
// }

// void
// endSetupParser(void* data, const char* name)
// {
//     depth_setup_parser--;
// }

// ----------------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////
// open
//

bool
CAutomation::open(const std::string& path, cguid& guid)
{
    // Set GUID
    m_guid = guid;

    // Set path to config file
    m_path = path;

    // Read configuration file
    if (!doLoadConfig()) {
        syslog(LOG_ERR,
               "[vscpl2drv-automation] Failed to load configuration file [%s]",
               path.c_str());
    }

    // start the workerthread
    if (pthread_create(&m_threadWork, NULL, workerThread, this)) {

        syslog(LOG_ERR,
               "[vscpl2drv-automation] Unable to start worker thread.");
        return false;
    }

    if (m_bDebug) {
        syslog(LOG_DEBUG, "[vscpl2drv-automation] Driver is open.");
    }

    return true;
}

//////////////////////////////////////////////////////////////////////
// close
//

void
CAutomation::close(void)
{
    if (m_bDebug) {
        syslog(LOG_DEBUG, "[vscpl2drv-automation] Driver requested to closed.");
    }

    // Do nothing if already terminated
    if (m_bQuit) {
        syslog(LOG_INFO,
               "[vscpl2drv-automation] Request to close while already closed.");
        return;
    }

    m_bQuit = true; // terminate the thread

    void* res;
    int rv = pthread_join(m_threadWork, &res);
    if (0 != rv) {
        syslog(
          LOG_ERR, "[vscpl2drv-automation] pthread_join failed error=%d", rv);
    }

    if (NULL != res) {
        syslog(LOG_ERR,
               "[vscpl2drv-automation] Worker thread did not returned NULL");
    }

    if (m_bDebug) {
        syslog(LOG_DEBUG, "[vscpl2drv-automation] Driver closed.");
    }
}

///////////////////////////////////////////////////////////////////////////////
// isDaylightSavingTime
//

int
CAutomation::isDaylightSavingTime(void)
{
    time_t rawtime;
    struct tm* timeinfo;

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
    struct tm* timeinfo;
    struct tm* timeinfo_gmt;
    int h1, h2;

    time(&rawtime);
    timeinfo = localtime(&rawtime);
    h2 = timeinfo->tm_hour;
    if (0 == h2)
        h2 = 24;

    timeinfo_gmt = gmtime(&rawtime);
    h1 = timeinfo_gmt->tm_hour;

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
    if (a < 0)
        a = 2.0 * pi + a;
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
    if (lat < 0.0)
        dfo = -dfo;
    fo = tan(declin + dfo) * tan(lat * rads);
    if (fo > 0.99999)
        fo = 1.0; // to avoid overflow //
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
    if (lat < 0.0)
        df1 = -df1;
    fi = tan(declin + df1) * tan(lat * rads);
    if (fi > 0.99999)
        fi = 1.0; // to avoid overflow //
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
CAutomation::convert2HourMinute(double floatTime, int* pHours, int* pMinutes)
{
    *pHours = ((int)floatTime) % 24;
    *pMinutes = ((int)((floatTime - (double)*pHours) * 60)) % 60;
};

///////////////////////////////////////////////////////////////////////////////
// calcSun
//

void
CAutomation::doCalc(void)
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
    year = nowLocal.getYear();
    month = nowLocal.getMonth() + 1;
    day = nowLocal.getDay();
    hour = nowLocal.getHour();

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
    if (L < pi)
        LL += 2.0 * pi;
    equation = 1440.0 * (1.0 - LL / pi / 2.0);
    ha = f0(m_latitude, delta);
    hb = f1(m_latitude, delta);
    twx = hb - ha;         // length of twilight in radians
    twx = 12.0 * twx / pi; // length of twilight in hours

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
    noonTime = sunriseTime + 12.0 * ha / pi;
    maxAltitude = 90.0 + delta * degs - m_latitude;
    // Correction for S HS suggested by David Smith
    // to express altitude as degrees from the N horizon
    if (m_latitude < delta * degs)
        maxAltitude = 180.0 - maxAltitude;

    twilightSunrise = sunriseTime - twx; // morning twilight begin
    twilightSunset = sunsetTime + twx;   // evening twilight end

    if (sunriseTime > 24.0)
        sunriseTime -= 24.0;
    if (sunsetTime > 24.0)
        sunsetTime -= 24.0;
    if (twilightSunrise > 24.0)
        twilightSunrise -= 24.0; // 160921
    if (twilightSunset > 24.0)
        twilightSunset -= 24.0;

    m_declination = delta * degs;
    m_daylength = daylen;
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

// ----------------------------------------------------------------------------

int depth_hlo_parser = 0;

void
startHLOParser(void* data, const char* name, const char** attr)
{
    // CHLO* pObj = (CHLO*)data;
    // if (NULL == pObj)
    //     return;

    // if ((0 == strcmp(name, "vscp-cmd")) && (0 == depth_setup_parser)) {

    //     for (int i = 0; attr[i]; i += 2) {

    //         std::string attribute = attr[i + 1];
    //         vscp_trim(attribute);

    //         if (0 == strcasecmp(attr[i], "op")) {
    //             if (!attribute.empty()) {
    //                 pObj->m_op = vscp_readStringValue(attribute);
    //                 vscp_makeUpper(attribute);
    //                 if (attribute == "VSCP-NOOP") {
    //                     pObj->m_op = HLO_OP_NOOP;
    //                 } else if (attribute == "VSCP-READVAR") {
    //                     pObj->m_op = HLO_OP_READ_VAR;
    //                 } else if (attribute == "VSCP-WRITEVAR") {
    //                     pObj->m_op = HLO_OP_WRITE_VAR;
    //                 } else if (attribute == "VSCP-LOAD") {
    //                     pObj->m_op = HLO_OP_LOAD;
    //                 } else if (attribute == "VSCP-SAVE") {
    //                     pObj->m_op = HLO_OP_SAVE;
    //                 } else if (attribute == "CALCULATE") {
    //                     pObj->m_op = HLO_OP_SAVE;
    //                 } else {
    //                     pObj->m_op = HLO_OP_UNKNOWN;
    //                 }
    //             }
    //         } else if (0 == strcasecmp(attr[i], "name")) {
    //             if (!attribute.empty()) {
    //                 vscp_makeUpper(attribute);
    //                 pObj->m_name = attribute;
    //             }
    //         } else if (0 == strcasecmp(attr[i], "type")) {
    //             if (!attribute.empty()) {
    //                 pObj->m_varType = vscp_readStringValue(attribute);
    //             }
    //         } else if (0 == strcasecmp(attr[i], "value")) {
    //             if (!attribute.empty()) {
    //                 if (vscp_base64_std_decode(attribute)) {
    //                     pObj->m_value = attribute;
    //                 }
    //             }
    //         } else if (0 == strcasecmp(attr[i], "full")) {
    //             if (!attribute.empty()) {
    //                 vscp_makeUpper(attribute);
    //                 if ("TRUE" == attribute) {
    //                     pObj->m_bFull = true;
    //                 } else {
    //                     pObj->m_bFull = false;
    //                 }
    //             }
    //         }
    //     }
    // }

    // depth_hlo_parser++;
}

void
endHLOParser(void* data, const char* name)
{
    depth_hlo_parser--;
}

// ----------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////////
// loadConfiguration
//

bool
CAutomation::doLoadConfig(void)
{
    /*FILE* fp;
    
    fp = fopen(m_path.c_str(), "r");
    if (NULL == fp) {
        syslog(LOG_ERR,
               "[vscpl2drv-automation] Failed to open configuration file [%s]",
               m_path.c_str());
        return false;
    }

    XML_Parser xmlParser = XML_ParserCreate("UTF-8");
    XML_SetUserData(xmlParser, this);
    XML_SetElementHandler(xmlParser, startSetupParser, endSetupParser);

    void* buf = XML_GetBuffer(xmlParser, XML_BUFF_SIZE);

    size_t file_size = 0;
    file_size = fread(buf, sizeof(char), XML_BUFF_SIZE, fp);
    fclose(fp);

    if (XML_STATUS_OK != XML_ParseBuffer(xmlParser, file_size, file_size == 0)) {
        syslog(LOG_ERR, "[vscpl2drv-automation] Failed parse XML setup.");
        XML_ParserFree(xmlParser);
        return false;
    }

    XML_ParserFree(xmlParser);
    */

    try {
        std::ifstream in(m_path, std::ifstream::in);
        in >> m_j_config;
    }
    catch (json::parse_error) {
        syslog(LOG_ERR, "[vscpl2drv-automation] Failed to parse JSON configuration.");
        return false;
    }

    try {
        if (m_j_config.contains("debug-enable") && m_j_config["debug-enable"].is_boolean()) { 
            m_bDebug = m_j_config["debug-enable"].get<bool>();
        } else {
            syslog(LOG_ERR, "ReadConfig: Failed to read 'enable-debug'. Default will be used.");
        }

        if (m_bDebug) {
            syslog(LOG_DEBUG, "ReadConfig: 'debug-enable' set to %s", m_bDebug ? "true" : "false");
        }
    }
    catch (...) {
        syslog(LOG_ERR, "ReadConfig: Failed to read 'debug-enable'. Default will be used.");
    }

    try {
        if (m_j_config.contains("write-enable") && m_j_config["write-enable"].is_boolean()) { 
            m_bDebug = m_j_config["write-enable"].get<bool>();
        } else {
            syslog(LOG_ERR, "ReadConfig: Failed to read 'write-debug'. Default will be used.");
        }

        if (m_bDebug) {
            syslog(LOG_DEBUG, "ReadConfig: 'write-enable' set to %s", m_bWrite ? "true" : "false");
        }
    }
    catch (...) {
        syslog(LOG_ERR, "ReadConfig: Failed to read 'write-enable'. Default will be used.");
    }

    try {
        if (m_j_config.contains("zone") && m_j_config["zone"].is_number()) { 
            m_bDebug = m_j_config["zone"].get<bool>();
        } else {
            syslog(LOG_ERR, "ReadConfig: Failed to read 'zone'. Default will be used.");
        }

        if (m_bDebug) {
            syslog(LOG_DEBUG, "ReadConfig: 'zone' set to %d", m_zone);
        }
    }
    catch (...) {
        syslog(LOG_ERR, "ReadConfig: Failed to read 'zone'. Default will be used.");
    }

    try {
        if (m_j_config.contains("subzone") && m_j_config["subzone"].is_number()) { 
            m_bDebug = m_j_config["subzone"].get<bool>();
        } else {
            syslog(LOG_ERR, "ReadConfig: Failed to read 'subzone'. Default will be used.");
        }

        if (m_bDebug) {
            syslog(LOG_DEBUG, "ReadConfig: 'subzone' set to %d", m_subzone);
        }
    }
    catch (...) {
        syslog(LOG_ERR, "ReadConfig: Failed to read 'subzone'. Default will be used.");
    }

    try {
        if (m_j_config.contains("longitude") && m_j_config["longitude"].is_number()) { 
            m_longitude = m_j_config["longitude"].get<bool>();
        } else {
            syslog(LOG_ERR, "ReadConfig: Failed to read 'longitude'. Default will be used.");
        }

        if (m_bDebug) {
            syslog(LOG_DEBUG, "ReadConfig: 'longitude' set to %f", m_longitude);
        }
    }
    catch (...) {
        syslog(LOG_ERR, "ReadConfig: Failed to read 'longitude'. Default will be used.");
    }

    try {
        if (m_j_config.contains("latitude") && m_j_config["latitude"].is_number()) { 
            m_latitude = m_j_config["latitude"].get<bool>();
        } else {
            syslog(LOG_ERR, "ReadConfig: Failed to read 'latitude'. Default will be used.");
        }

        if (m_bDebug) {
            syslog(LOG_DEBUG, "ReadConfig: 'latitude' set to %f", m_latitude);
        }
    }
    catch (...) {
        syslog(LOG_ERR, "ReadConfig: Failed to read 'latitude'. Default will be used.");
    }

    try {
        if (m_j_config.contains("sunrise-enable") && m_j_config["sunrise-enable"].is_boolean()) { 
            m_bSunRiseEvent = m_j_config["sunrise-enable"].get<bool>();
        } else {
            syslog(LOG_ERR, "ReadConfig: Failed to read 'sunrise-debug'. Default will be used.");
        }

        if (m_bDebug) {
            syslog(LOG_DEBUG, "ReadConfig: 'sunrise-enable' set to %s", m_bSunRiseEvent ? "true" : "false");
        }
    }
    catch (...) {
        syslog(LOG_ERR, "ReadConfig: Failed to read 'sunrise-enable'. Default will be used.");
    }

    try {
        if (m_j_config.contains("sunrise-twilight-enable") && m_j_config["sunrise-twilight-enable"].is_boolean()) { 
            m_bSunRiseTwilightEvent = m_j_config["sunrise-twilight-enable"].get<bool>();
        } else {
            syslog(LOG_ERR, "ReadConfig: Failed to read 'sunrise-twilight-debug'. Default will be used.");
        }

        if (m_bDebug) {
            syslog(LOG_DEBUG, "ReadConfig: 'sunrise-twilight-enable' set to %s", m_bSunRiseTwilightEvent ? "true" : "false");
        }
    }
    catch (...) {
        syslog(LOG_ERR, "ReadConfig: Failed to read 'sunrise-twilight-enable'. Default will be used.");
    }

    try {
        if (m_j_config.contains("sunset-enable") && m_j_config["sunset-enable"].is_boolean()) { 
            m_bSunSetEvent = m_j_config["sunset-enable"].get<bool>();
        } else {
            syslog(LOG_ERR, "ReadConfig: Failed to read 'sunset-debug'. Default will be used.");
        }

        if (m_bDebug) {
            syslog(LOG_DEBUG, "ReadConfig: 'sunset-enable' set to %s", m_bSunSetEvent ? "true" : "false");
        }
    }
    catch (...) {
        syslog(LOG_ERR, "ReadConfig: Failed to read 'sunset-enable'. Default will be used.");
    }

    try {
        if (m_j_config.contains("sunset-twilight-enable") && m_j_config["sunset-twilight-enable"].is_boolean()) { 
            m_bSunSetTwilightEvent = m_j_config["sunset-twilight-enable"].get<bool>();
        } else {
            syslog(LOG_ERR, "ReadConfig: Failed to read 'sunset-twilight-debug'. Default will be used.");
        }

        if (m_bDebug) {
            syslog(LOG_DEBUG, "ReadConfig: 'sunset-twilight-enable' set to %s", m_bSunSetTwilightEvent ? "true" : "false");
        }
    }
    catch (...) {
        syslog(LOG_ERR, "ReadConfig: Failed to read 'sunset-twilight-enable'. Default will be used.");
    }

    // if (!readEncryptionKey(m_j_config.value("vscp-key-file", ""))) {
    //     syslog(LOG_ERR, "[vscpl2drv-automation] WARNING!!! Default key will be used.");
    //     // Not secure of course but something...
    //     m_vscpkey = "Carpe diem quam minimum credula postero";
    // }

    

    

    return true;
}

///////////////////////////////////////////////////////////////////////////////
// saveConfiguration
//

bool
CAutomation::doSaveConfig(void)
{
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// parseHLO
//

bool
CAutomation::parseHLO(uint16_t size, uint8_t* inbuf, CHLO* phlo)
{
    // // Check pointers
    // if (NULL == inbuf) {
    //     syslog(
    //       LOG_ERR,
    //       "[vscpl2drv-automation] HLO parser: HLO in-buffer pointer is NULL.");
    //     return false;
    // }

    // if (NULL == phlo) {
    //     syslog(LOG_ERR,
    //            "[vscpl2drv-automation] HLO parser: HLO obj pointer is NULL.");
    //     return false;
    // }

    // if (!size) {
    //     syslog(LOG_ERR,
    //            "[vscpl2drv-automation] HLO parser: HLO buffer size is zero.");
    //     return false;
    // }

    // XML_Parser xmlParser = XML_ParserCreate("UTF-8");
    // XML_SetUserData(xmlParser, this);
    // XML_SetElementHandler(xmlParser, startHLOParser, endHLOParser);

    // void* buf = XML_GetBuffer(xmlParser, XML_BUFF_SIZE);

    // // Copy in the HLO object
    // memcpy(buf, inbuf, size);

    // if (!XML_ParseBuffer(xmlParser, size, size == 0)) {
    //     syslog(LOG_ERR, "[vscpl2drv-automation] Failed parse XML setup.");
    //     XML_ParserFree(xmlParser);
    //     return false;
    // }

    // XML_ParserFree(xmlParser);

    return true;
}

///////////////////////////////////////////////////////////////////////////////
// handleHLO
//

bool
CAutomation::handleHLO(vscpEvent* pEvent)
{
    json j;

    // char buf[512]; // Working buffer
    // vscpEventEx ex;

    // // Check pointers
    // if (NULL == pEvent) {
    //     syslog(LOG_ERR,
    //            "[vscpl2drv-automation] HLO handler: NULL event pointer.");
    //     return false;
    // }

    // CHLO hlo;
    // if (!parseHLO(pEvent->sizeData, pEvent->pdata, &hlo)) {
    //     syslog(LOG_ERR, "[vscpl2drv-automation] Failed to parse HLO.");
    //     return false;
    // }

    // ex.obid = 0;
    // ex.head = 0;
    // ex.timestamp = vscp_makeTimeStamp();
    // vscp_setEventExToNow(&ex); // Set time to current time
    // ex.vscp_class = VSCP_CLASS2_HLO;
    // ex.vscp_type = VSCP2_TYPE_HLO_RESPONSE;
    // m_guid.writeGUID(ex.GUID);

    // switch (hlo.m_op) {

    //     case HLO_OP_NOOP:
    //         // Send positive response
    //         sprintf(buf,
    //                 HLO_CMD_REPLY_TEMPLATE,
    //                 "noop",
    //                 "OK",
    //                 "NOOP commaned executed correctly.");

    //         memset(ex.data, 0, sizeof(ex.data));
    //         ex.sizeData = strlen(buf);
    //         memcpy(ex.data, buf, ex.sizeData);

    //         // Put event in receive queue
    //         return eventExToReceiveQueue(ex);

    //     case HLO_OP_READ_VAR:
    //         if ("SUNRISE" == hlo.m_name) {
    //             sprintf(
    //               buf,
    //               HLO_READ_VAR_REPLY_TEMPLATE,
    //               "sunrise",
    //               "OK",
    //               VSCP_REMOTE_VARIABLE_CODE_DATETIME,
    //               vscp_convertToBase64(getSunriseTime().getISODateTime()).c_str());
    //         } else if ("SUNSET" == hlo.m_name) {
    //             sprintf(
    //               buf,
    //               HLO_READ_VAR_REPLY_TEMPLATE,
    //               "sunset",
    //               "OK",
    //               VSCP_REMOTE_VARIABLE_CODE_DATETIME,
    //               vscp_convertToBase64(getSunsetTime().getISODateTime()).c_str());
    //         } else if ("SUNRISE-TWILIGHT" == hlo.m_name) {
    //             sprintf(buf,
    //                     HLO_READ_VAR_REPLY_TEMPLATE,
    //                     "sunrise-twilight",
    //                     "OK",
    //                     VSCP_REMOTE_VARIABLE_CODE_DATETIME,
    //                     vscp_convertToBase64(
    //                       getCivilTwilightSunriseTime().getISODateTime())
    //                       .c_str());
    //         } else if ("SUNSET-TWILIGHT" == hlo.m_name) {
    //             sprintf(
    //               buf,
    //               HLO_READ_VAR_REPLY_TEMPLATE,
    //               "sunset-twilight",
    //               "OK",
    //               VSCP_REMOTE_VARIABLE_CODE_DATETIME,
    //               vscp_convertToBase64(getCivilTwilightSunsetTime().getISODateTime())
    //                 .c_str());
    //         } else if ("NOON" == hlo.m_name) {
    //             sprintf(buf,
    //                     HLO_READ_VAR_REPLY_TEMPLATE,
    //                     "noon",
    //                     "OK",
    //                     VSCP_REMOTE_VARIABLE_CODE_DATETIME,
    //                     vscp_convertToBase64(m_noonTime.getISODateTime()).c_str());
    //         } else if ("SENT-SUNRISE" == hlo.m_name) {
    //             sprintf(buf,
    //                     HLO_READ_VAR_REPLY_TEMPLATE,
    //                     "sent-sunrise",
    //                     "OK",
    //                     VSCP_REMOTE_VARIABLE_CODE_DATETIME,
    //                     vscp_convertToBase64(getSentSunriseTime().getISODateTime())
    //                       .c_str());
    //         } else if ("SENT-SUNSET" == hlo.m_name) {
    //             sprintf(buf,
    //                     HLO_READ_VAR_REPLY_TEMPLATE,
    //                     "sent-sunset",
    //                     "OK",
    //                     VSCP_REMOTE_VARIABLE_CODE_DATETIME,
    //                     vscp_convertToBase64(getSentSunsetTime().getISODateTime())
    //                       .c_str());
    //         } else if ("SENT-SUNRISE-TWILIGHT" == hlo.m_name) {
    //             sprintf(buf,
    //                     HLO_READ_VAR_REPLY_TEMPLATE,
    //                     "sent-sunrise-twilight",
    //                     "OK",
    //                     VSCP_REMOTE_VARIABLE_CODE_DATETIME,
    //                     vscp_convertToBase64(
    //                       getSentCivilTwilightSunriseTime().getISODateTime())
    //                       .c_str());
    //         } else if ("SENT-SUNSET-TWILIGHT" == hlo.m_name) {
    //             sprintf(buf,
    //                     HLO_READ_VAR_REPLY_TEMPLATE,
    //                     "sent-sunset-twilight",
    //                     "OK",
    //                     VSCP_REMOTE_VARIABLE_CODE_DATETIME,
    //                     vscp_convertToBase64(
    //                       getSentCivilTwilightSunsetTime().getISODateTime())
    //                       .c_str());
    //         } else if ("SENT-SUNRISE-TWILIGHT" == hlo.m_name) {
    //             sprintf(buf,
    //                     HLO_READ_VAR_REPLY_TEMPLATE,
    //                     "sent-sunrise-twilight",
    //                     "OK",
    //                     VSCP_REMOTE_VARIABLE_CODE_DATETIME,
    //                     vscp_convertToBase64(
    //                       getSentCivilTwilightSunriseTime().getISODateTime())
    //                       .c_str());
    //         } else if ("SENT-NOON" == hlo.m_name) {
    //             sprintf(buf,
    //                     HLO_READ_VAR_REPLY_TEMPLATE,
    //                     "sent-noon",
    //                     "OK",
    //                     VSCP_REMOTE_VARIABLE_CODE_DATETIME,
    //                     getSentNoonTime().getISODateTime().c_str());
    //         } else if ("ENABLE-SUNRISE" == hlo.m_name) {
    //             sprintf(buf,
    //                     HLO_READ_VAR_REPLY_TEMPLATE,
    //                     "enable-sunrise",
    //                     "OK",
    //                     VSCP_REMOTE_VARIABLE_CODE_BOOLEAN,
    //                     vscp_convertToBase64(m_bSunRiseEvent ? std::string("true")
    //                                                     : std::string("false"))
    //                       .c_str());
    //         } else if ("ENABLE-SUNSET" == hlo.m_name) {
    //             sprintf(buf,
    //                     HLO_READ_VAR_REPLY_TEMPLATE,
    //                     "enable-sunset",
    //                     "OK",
    //                     VSCP_REMOTE_VARIABLE_CODE_BOOLEAN,
    //                     vscp_convertToBase64(m_bSunSetEvent ? std::string("true")
    //                                                    : std::string("false"))
    //                       .c_str());
    //         } else if ("ENABLE-SUNRISE-TWILIGHT" == hlo.m_name) {
    //             sprintf(buf,
    //                     HLO_READ_VAR_REPLY_TEMPLATE,
    //                     "enable-sunrise-twilight",
    //                     "OK",
    //                     VSCP_REMOTE_VARIABLE_CODE_BOOLEAN,
    //                     vscp_convertToBase64(m_bSunRiseTwilightEvent
    //                                       ? std::string("true")
    //                                       : std::string("false"))
    //                       .c_str());
    //         } else if ("ENABLE-SUNSET-TWILIGHT" == hlo.m_name) {
    //             sprintf(buf,
    //                     HLO_READ_VAR_REPLY_TEMPLATE,
    //                     "enable-sunset-twilight",
    //                     "OK",
    //                     VSCP_REMOTE_VARIABLE_CODE_BOOLEAN,
    //                     vscp_convertToBase64(m_bSunSetTwilightEvent
    //                                       ? std::string("true")
    //                                       : std::string("false"))
    //                       .c_str());
    //         } else if ("ENABLE-NOON" == hlo.m_name) {
    //             sprintf(buf,
    //                     HLO_READ_VAR_REPLY_TEMPLATE,
    //                     "enable-noon",
    //                     "OK",
    //                     VSCP_REMOTE_VARIABLE_CODE_BOOLEAN,
    //                     vscp_convertToBase64(m_bNoonEvent ? std::string("true")
    //                                                  : std::string("false"))
    //                       .c_str());
    //         } else if ("LONGITUDE" == hlo.m_name) {
    //             sprintf(buf,
    //                     HLO_READ_VAR_REPLY_TEMPLATE,
    //                     "longitude",
    //                     "OK",
    //                     VSCP_REMOTE_VARIABLE_CODE_DOUBLE,
    //                     vscp_convertToBase64(getLongitudeStr()).c_str());
    //         } else if ("LATITUDE" == hlo.m_name) {
    //             sprintf(buf,
    //                     HLO_READ_VAR_REPLY_TEMPLATE,
    //                     "latitude",
    //                     "OK",
    //                     VSCP_REMOTE_VARIABLE_CODE_DOUBLE,
    //                     vscp_convertToBase64(getLatitudeStr()).c_str());
    //         } else if ("ZONE" == hlo.m_name) {
    //             sprintf(buf,
    //                     HLO_READ_VAR_REPLY_TEMPLATE,
    //                     "zone",
    //                     "OK",
    //                     VSCP_REMOTE_VARIABLE_CODE_INTEGER,
    //                     vscp_convertToBase64(getZoneStr()).c_str());
    //         } else if ("SUBZONE" == hlo.m_name) {
    //             sprintf(buf,
    //                     HLO_READ_VAR_REPLY_TEMPLATE,
    //                     "subzone",
    //                     "OK",
    //                     VSCP_REMOTE_VARIABLE_CODE_INTEGER,
    //                     vscp_convertToBase64(getSubZoneStr()).c_str());
    //         } else if ("DAYLENGTH" == hlo.m_name) {
    //             sprintf(buf,
    //                     HLO_READ_VAR_REPLY_TEMPLATE,
    //                     "daylength",
    //                     "OK",
    //                     VSCP_REMOTE_VARIABLE_CODE_DOUBLE,
    //                     vscp_convertToBase64(getDayLengthStr()).c_str());
    //         } else if ("DECLINATION" == hlo.m_name) {
    //             sprintf(buf,
    //                     HLO_READ_VAR_REPLY_TEMPLATE,
    //                     "declination",
    //                     "OK",
    //                     VSCP_REMOTE_VARIABLE_CODE_DOUBLE,
    //                     vscp_convertToBase64(getDeclinationStr()).c_str());
    //         } else if ("SUN-MAX-ALTITUDE" == hlo.m_name) {
    //             sprintf(buf,
    //                     HLO_READ_VAR_REPLY_TEMPLATE,
    //                     "sun-max-altitude",
    //                     "OK",
    //                     VSCP_REMOTE_VARIABLE_CODE_DOUBLE,
    //                     vscp_convertToBase64(getSunMaxAltitudeStr()).c_str());
    //         } else if ("LAST-CALCULATION" == hlo.m_name) {
    //             sprintf(buf,
    //                     HLO_READ_VAR_REPLY_TEMPLATE,
    //                     "last-calculation",
    //                     "OK",
    //                     VSCP_REMOTE_VARIABLE_CODE_DATETIME,
    //                     vscp_convertToBase64(getLastCalculation().getISODateTime())
    //                       .c_str());
    //         } else {
    //             sprintf(
    //               buf,
    //               HLO_READ_VAR_ERR_REPLY_TEMPLATE,
    //               hlo.m_name.c_str(),
    //               ERR_VARIABLE_UNKNOWN,
    //               vscp_convertToBase64(std::string("Unknown variable")).c_str());
    //         }
    //         break;

    //     case HLO_OP_WRITE_VAR:
    //         if ("SUNRISE" == hlo.m_name) {
    //             // Read Only variable
    //             sprintf(buf,
    //                     HLO_READ_VAR_ERR_REPLY_TEMPLATE,
    //                     "sunrise",
    //                     VSCP_REMOTE_VARIABLE_CODE_BOOLEAN,
    //                     "Variable is read only.");
    //         } else if ("SUNSET" == hlo.m_name) {
    //             // Read Only variable
    //             sprintf(buf,
    //                     HLO_READ_VAR_ERR_REPLY_TEMPLATE,
    //                     "sunset",
    //                     VSCP_REMOTE_VARIABLE_CODE_BOOLEAN,
    //                     "Variable is read only.");
    //         } else if ("SUNRISE-TWILIGHT" == hlo.m_name) {
    //             // Read Only variable
    //             sprintf(buf,
    //                     HLO_READ_VAR_ERR_REPLY_TEMPLATE,
    //                     "sunrise-twilight",
    //                     VSCP_REMOTE_VARIABLE_CODE_BOOLEAN,
    //                     "Variable is read only.");
    //         } else if ("SUNSET-TWILIGHT" == hlo.m_name) {
    //             // Read Only variable
    //             sprintf(buf,
    //                     HLO_READ_VAR_ERR_REPLY_TEMPLATE,
    //                     "sunset-twilight",
    //                     VSCP_REMOTE_VARIABLE_CODE_BOOLEAN,
    //                     "Variable is read only.");
    //         } else if ("NOON" == hlo.m_name) {
    //             // Read Only variable
    //             sprintf(buf,
    //                     HLO_READ_VAR_ERR_REPLY_TEMPLATE,
    //                     "noon",
    //                     VSCP_REMOTE_VARIABLE_CODE_BOOLEAN,
    //                     "Variable is read only.");
    //         } else if ("SENT-SUNRISE" == hlo.m_name) {
    //             // Read Only variable
    //             sprintf(buf,
    //                     HLO_READ_VAR_ERR_REPLY_TEMPLATE,
    //                     "sent-sunrise",
    //                     ERR_VARIABLE_READ_ONLY,
    //                     "Variable is read only.");
    //         } else if ("SENT-SUNSET" == hlo.m_name) {
    //             // Read Only variable
    //             sprintf(buf,
    //                     HLO_READ_VAR_ERR_REPLY_TEMPLATE,
    //                     "sent-sunset",
    //                     ERR_VARIABLE_READ_ONLY,
    //                     "Variable is read only.");
    //         } else if ("SENT-SUNRISE-TWILIGHT" == hlo.m_name) {
    //             // Read Only variable
    //             sprintf(buf,
    //                     HLO_READ_VAR_ERR_REPLY_TEMPLATE,
    //                     "sent-sunrise-twilight",
    //                     ERR_VARIABLE_READ_ONLY,
    //                     "Variable is read only.");
    //         } else if ("SENT-SUNSET-TWILIGHT" == hlo.m_name) {
    //             // Read Only variable
    //             sprintf(buf,
    //                     HLO_READ_VAR_ERR_REPLY_TEMPLATE,
    //                     "sent-sunset-twilight",
    //                     ERR_VARIABLE_READ_ONLY,
    //                     "Variable is read only.");
    //         } else if ("SENT-SUNRISE-TWILIGHT" == hlo.m_name) {

    //         } else if ("SENT-NOON" == hlo.m_name) {
    //             // Read Only variable
    //             sprintf(buf,
    //                     HLO_READ_VAR_ERR_REPLY_TEMPLATE,
    //                     "sent-noon",
    //                     ERR_VARIABLE_READ_ONLY,
    //                     "Variable is read only.");
    //         } else if ("ENABLE-SUNRISE" == hlo.m_name) {
    //             if (VSCP_REMOTE_VARIABLE_CODE_BOOLEAN != hlo.m_varType) {
    //                 // Wrong variable type
    //                 sprintf(buf,
    //                         HLO_READ_VAR_ERR_REPLY_TEMPLATE,
    //                         "enable-sunrise",
    //                         ERR_VARIABLE_WRONG_TYPE,
    //                         "Variable type should be boolean.");
    //             } else {
    //                 vscp_trim(hlo.m_value);
    //                 vscp_makeUpper(hlo.m_value);
    //                 if ("TRUE" == hlo.m_value) {
    //                     enableSunRiseEvent();
    //                     sprintf(buf,
    //                             HLO_READ_VAR_REPLY_TEMPLATE,
    //                             "enable-sunrise",
    //                             "OK",
    //                             VSCP_REMOTE_VARIABLE_CODE_BOOLEAN,
    //                             vscp_convertToBase64(isSendSunriseEvent()
    //                                               ? std::string("true")
    //                                               : std::string("false"))
    //                               .c_str());
    //                 } else {
    //                     disableSunRiseEvent();
    //                     sprintf(buf,
    //                             HLO_READ_VAR_REPLY_TEMPLATE,
    //                             "enable-sunrise",
    //                             "OK",
    //                             VSCP_REMOTE_VARIABLE_CODE_BOOLEAN,
    //                             vscp_convertToBase64(isSendSunriseEvent()
    //                                               ? std::string("true")
    //                                               : std::string("false"))
    //                               .c_str());
    //                 }
    //             }
    //         } else if ("ENABLE-SUNSET" == hlo.m_name) {
    //             if (VSCP_REMOTE_VARIABLE_CODE_BOOLEAN != hlo.m_varType) {
    //                 // Wrong variable type
    //                 sprintf(buf,
    //                         HLO_READ_VAR_ERR_REPLY_TEMPLATE,
    //                         "enable-sunset",
    //                         ERR_VARIABLE_WRONG_TYPE,
    //                         "Variable type should be boolean.");
    //             } else {
    //                 vscp_trim(hlo.m_value);
    //                 vscp_makeUpper(hlo.m_value);
    //                 if ("TRUE" == hlo.m_value) {
    //                     enableSunSetEvent();
    //                     sprintf(buf,
    //                             HLO_READ_VAR_REPLY_TEMPLATE,
    //                             "enable-sunset",
    //                             "OK",
    //                             VSCP_REMOTE_VARIABLE_CODE_BOOLEAN,
    //                             vscp_convertToBase64(isSendSunsetEvent()
    //                                               ? std::string("true")
    //                                               : std::string("false"))
    //                               .c_str());
    //                 } else {
    //                     disableSunSetEvent();
    //                     sprintf(buf,
    //                             HLO_READ_VAR_REPLY_TEMPLATE,
    //                             "enable-sunset",
    //                             "OK",
    //                             VSCP_REMOTE_VARIABLE_CODE_BOOLEAN,
    //                             vscp_convertToBase64(isSendSunsetEvent()
    //                                               ? std::string("true")
    //                                               : std::string("false"))
    //                               .c_str());
    //                 }
    //             }
    //         } else if ("ENABLE-SUNRISE-TWILIGHT" == hlo.m_name) {
    //             if (VSCP_REMOTE_VARIABLE_CODE_BOOLEAN != hlo.m_varType) {
    //                 // Wrong variable type
    //                 sprintf(buf,
    //                         HLO_READ_VAR_ERR_REPLY_TEMPLATE,
    //                         "enable-sunrise-twilight",
    //                         ERR_VARIABLE_WRONG_TYPE,
    //                         "Variable type should be boolean.");
    //             } else {
    //                 vscp_trim(hlo.m_value);
    //                 vscp_makeUpper(hlo.m_value);
    //                 if ("TRUE" == hlo.m_value) {
    //                     enableSunRiseTwilightEvent();
    //                     sprintf(buf,
    //                             HLO_READ_VAR_REPLY_TEMPLATE,
    //                             "enable-sunrise-twilight",
    //                             "OK",
    //                             VSCP_REMOTE_VARIABLE_CODE_BOOLEAN,
    //                             vscp_convertToBase64(isSendSunriseTwilightEvent()
    //                                               ? std::string("true")
    //                                               : std::string("false"))
    //                               .c_str());
    //                 } else {
    //                     disableSunRiseEvent();
    //                     sprintf(buf,
    //                             HLO_READ_VAR_REPLY_TEMPLATE,
    //                             "enable-sunrise-twilight",
    //                             "OK",
    //                             VSCP_REMOTE_VARIABLE_CODE_BOOLEAN,
    //                             vscp_convertToBase64(isSendSunriseTwilightEvent()
    //                                               ? std::string("true")
    //                                               : std::string("false"))
    //                               .c_str());
    //                 }
    //             }
    //         } else if ("ENABLE-SUNSET-TWILIGHT" == hlo.m_name) {
    //             if (VSCP_REMOTE_VARIABLE_CODE_BOOLEAN != hlo.m_varType) {
    //                 // Wrong variable type
    //                 sprintf(buf,
    //                         HLO_READ_VAR_ERR_REPLY_TEMPLATE,
    //                         "enable-sunset-twilight",
    //                         ERR_VARIABLE_WRONG_TYPE,
    //                         "Variable type should be boolean.");
    //             } else {
    //                 vscp_trim(hlo.m_value);
    //                 vscp_makeUpper(hlo.m_value);
    //                 if ("TRUE" == hlo.m_value) {
    //                     enableSunSetTwilightEvent();
    //                     sprintf(buf,
    //                             HLO_READ_VAR_REPLY_TEMPLATE,
    //                             "enable-sunset-twilight",
    //                             "OK",
    //                             VSCP_REMOTE_VARIABLE_CODE_BOOLEAN,
    //                             vscp_convertToBase64(isSendSunsetTwilightEvent()
    //                                               ? std::string("true")
    //                                               : std::string("false"))
    //                               .c_str());
    //                 } else {
    //                     disableSunSetTwilightEvent();
    //                     sprintf(buf,
    //                             HLO_READ_VAR_REPLY_TEMPLATE,
    //                             "enable-sunset-twilight",
    //                             "OK",
    //                             VSCP_REMOTE_VARIABLE_CODE_BOOLEAN,
    //                             vscp_convertToBase64(isSendSunsetTwilightEvent()
    //                                               ? std::string("true")
    //                                               : std::string("false"))
    //                               .c_str());
    //                 }
    //             }
    //         } else if ("ENABLE-NOON" == hlo.m_name) {
    //             if (VSCP_REMOTE_VARIABLE_CODE_BOOLEAN != hlo.m_varType) {
    //                 // Wrong variable type
    //                 sprintf(buf,
    //                         HLO_READ_VAR_ERR_REPLY_TEMPLATE,
    //                         "enable-noon",
    //                         ERR_VARIABLE_WRONG_TYPE,
    //                         "Variable type should be boolean.");
    //             } else {
    //                 vscp_trim(hlo.m_value);
    //                 vscp_makeUpper(hlo.m_value);
    //                 if ("TRUE" == hlo.m_value) {
    //                     enableCalculatedNoonEvent();
    //                     sprintf(buf,
    //                             HLO_READ_VAR_REPLY_TEMPLATE,
    //                             "enable-noon",
    //                             "OK",
    //                             VSCP_REMOTE_VARIABLE_CODE_BOOLEAN,
    //                             vscp_convertToBase64(isSendCalculatedNoonEvent()
    //                                               ? std::string("true")
    //                                               : std::string("false"))
    //                               .c_str());
    //                 } else {
    //                     disableCalculatedNoonEvent();
    //                     sprintf(buf,
    //                             HLO_READ_VAR_REPLY_TEMPLATE,
    //                             "enable-sunrise",
    //                             "OK",
    //                             VSCP_REMOTE_VARIABLE_CODE_BOOLEAN,
    //                             vscp_convertToBase64(isSendCalculatedNoonEvent()
    //                                               ? std::string("true")
    //                                               : std::string("false"))
    //                               .c_str());
    //                 }
    //             }
    //         } else if ("LONGITUDE" == hlo.m_name) {
    //             if (VSCP_REMOTE_VARIABLE_CODE_DOUBLE != hlo.m_varType) {
    //                 // Wrong variable type
    //                 sprintf(buf,
    //                         HLO_READ_VAR_ERR_REPLY_TEMPLATE,
    //                         "enable-noon",
    //                         ERR_VARIABLE_WRONG_TYPE,
    //                         "Variable type should be boolean.");
    //             }
    //             sprintf(buf,
    //                     HLO_READ_VAR_REPLY_TEMPLATE,
    //                     "longitude",
    //                     "OK",
    //                     5,
    //                     vscp_convertToBase64(getLongitudeStr()).c_str());
    //         } else if ("LATITUDE" == hlo.m_name) {
    //             sprintf(buf,
    //                     HLO_READ_VAR_REPLY_TEMPLATE,
    //                     "latitude",
    //                     "OK",
    //                     5,
    //                     vscp_convertToBase64(getLatitudeStr()).c_str());
    //         } else if ("ZONE" == hlo.m_name) {
    //             sprintf(buf,
    //                     HLO_READ_VAR_REPLY_TEMPLATE,
    //                     "zone",
    //                     "OK",
    //                     3,
    //                     vscp_convertToBase64(getZoneStr()).c_str());
    //         } else if ("SUBZONE" == hlo.m_name) {
    //             sprintf(buf,
    //                     HLO_READ_VAR_REPLY_TEMPLATE,
    //                     "subzone",
    //                     "OK",
    //                     3,
    //                     vscp_convertToBase64(getSubZoneStr()).c_str());
    //         } else if ("DAYLENGTH" == hlo.m_name) {
    //             sprintf(buf,
    //                     HLO_READ_VAR_REPLY_TEMPLATE,
    //                     "daylength",
    //                     "OK",
    //                     5,
    //                     vscp_convertToBase64(getDayLengthStr()).c_str());
    //         } else if ("DECLINATION" == hlo.m_name) {
    //             sprintf(buf,
    //                     HLO_READ_VAR_REPLY_TEMPLATE,
    //                     "declination",
    //                     "OK",
    //                     5,
    //                     vscp_convertToBase64(getDeclinationStr()).c_str());
    //         } else if ("SUN-MAX-ALTITUDE" == hlo.m_name) {
    //             sprintf(buf,
    //                     HLO_READ_VAR_REPLY_TEMPLATE,
    //                     "sun-max-altitude",
    //                     "OK",
    //                     5,
    //                     vscp_convertToBase64(getSunMaxAltitudeStr()).c_str());
    //         } else if ("LAST-CALCULATION" == hlo.m_name) {
    //             sprintf(buf,
    //                     HLO_READ_VAR_REPLY_TEMPLATE,
    //                     "last-calculation",
    //                     "OK",
    //                     13,
    //                     vscp_convertToBase64(getLastCalculation().getISODateTime())
    //                       .c_str());
    //         } else {
    //             sprintf(
    //               buf,
    //               HLO_READ_VAR_ERR_REPLY_TEMPLATE,
    //               hlo.m_name.c_str(),
    //               1,
    //               vscp_convertToBase64(std::string("Unknown variable")).c_str());
    //         }
    //         break;

    //     case HLO_OP_SAVE:
    //         doSaveConfig();
    //         break;

    //     case HLO_OP_LOAD:
    //         doLoadConfig();
    //         break;

    //     case HLO_USER_CALC_ASTRO:
    //         doCalc();
    //         break;

    //     default:
    //         break;
    // };

    return true;
}

///////////////////////////////////////////////////////////////////////////////
// eventExToReceiveQueue
//

bool
CAutomation::eventExToReceiveQueue(vscpEventEx& ex)
{
    vscpEvent* pev = new vscpEvent();
    if (vscp_convertEventExToEvent(pev, &ex)) {
        syslog(LOG_ERR,
               "[vscpl2drv-automation] Failed to convert event from ex to ev.");
        vscp_deleteEvent(pev);
        return false;
    }
    if (NULL != pev) {
        if (vscp_doLevel2Filter(pev, &m_vscpfilter)) {
            pthread_mutex_lock(&m_mutexReceiveQueue);
            m_receiveList.push_back(pev);
            sem_post(&m_semReceiveQueue);
            pthread_mutex_unlock(&m_mutexReceiveQueue);
        } else {
            vscp_deleteEvent(pev);
        }
    } else {
        syslog(LOG_ERR,
               "[vscpl2drv-automation] Unable to allocate event storage.");
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// doWork
//

bool
CAutomation::doWork(void)
{
    vscpEventEx ex;
    std::string str;
    vscpdatetime now = vscpdatetime::Now();

    // Calculate Sunrise/sunset parameters once a day
    if (!m_bCalulationHasBeenDone && (0 == vscpdatetime::Now().getHour())) {

        doCalc();
        m_bCalulationHasBeenDone = true;

        int hours, minutes;
        convert2HourMinute(getDayLength(), &hours, &minutes);

        // Send VSCP_CLASS2_VSCPD, Type=30/VSCP2_TYPE_VSCPD_NEW_CALCULATION
        ex.obid = 0;
        ex.head = 0;
        ex.timestamp = vscp_makeTimeStamp();
        vscp_setEventExToNow(&ex); // Set time to current time
        ex.vscp_class = VSCP_CLASS2_VSCPD;
        ex.vscp_type = VSCP2_TYPE_VSCPD_NEW_CALCULATION;
        ex.sizeData = 0;
        m_guid.writeGUID(ex.GUID);

        // Put event in receive queue
        return eventExToReceiveQueue(ex);
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
        ex.obid = 0;
        ex.head = 0;
        ex.timestamp = vscp_makeTimeStamp();
        vscp_setEventExToNow(&ex); // Set time to current time
        ex.vscp_class = VSCP_CLASS1_INFORMATION;
        ex.vscp_type = VSCP_TYPE_INFORMATION_SUNRISE;
        ex.sizeData = 3;
        m_guid.writeGUID(ex.GUID);

        ex.data[0] = 0;         // index
        ex.data[1] = m_zone;    // zone
        ex.data[2] = m_subzone; // subzone

        // Put event in receive queue
        return eventExToReceiveQueue(ex);
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
        ex.obid = 0;
        ex.head = 0;
        ex.timestamp = vscp_makeTimeStamp();
        vscp_setEventExToNow(&ex); // Set time to current time
        ex.vscp_class = VSCP_CLASS1_INFORMATION;
        ex.vscp_type = VSCP_TYPE_INFORMATION_SUNRISE_TWILIGHT_START;
        ex.sizeData = 3;
        m_guid.writeGUID(ex.GUID);

        ex.data[0] = 0;         // index
        ex.data[1] = m_zone;    // zone
        ex.data[2] = m_subzone; // subzone

        // Put event in receive queue
        return eventExToReceiveQueue(ex);
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
        ex.obid = 0;
        ex.head = 0;
        ex.timestamp = vscp_makeTimeStamp();
        vscp_setEventExToNow(&ex); // Set time to current time
        ex.vscp_class = VSCP_CLASS1_INFORMATION;
        ex.vscp_type = VSCP_TYPE_INFORMATION_SUNSET;
        ex.sizeData = 3;
        m_guid.writeGUID(ex.GUID);

        ex.data[0] = 0;         // index
        ex.data[1] = m_zone;    // zone
        ex.data[2] = m_subzone; // subzone

        // Put event in receive queue
        return eventExToReceiveQueue(ex);
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
        ex.obid = 0;
        ex.head = 0;
        ex.timestamp = vscp_makeTimeStamp();
        vscp_setEventExToNow(&ex); // Set time to current time
        ex.vscp_class = VSCP_CLASS1_INFORMATION;
        ex.vscp_type = VSCP_TYPE_INFORMATION_SUNSET_TWILIGHT_START;
        ex.sizeData = 3;
        m_guid.writeGUID(ex.GUID);

        ex.data[0] = 0;         // index
        ex.data[1] = m_zone;    // zone
        ex.data[2] = m_subzone; // subzone

        // Put event in receive queue
        return eventExToReceiveQueue(ex);
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
        ex.obid = 0;
        ex.head = 0;
        ex.timestamp = vscp_makeTimeStamp();
        vscp_setEventExToNow(&ex); // Set time to current time
        ex.vscp_class = VSCP_CLASS1_INFORMATION;
        ex.vscp_type = VSCP_TYPE_INFORMATION_CALCULATED_NOON;
        ex.sizeData = 3;
        m_guid.writeGUID(ex.GUID);

        ex.data[0] = 0;         // index
        ex.data[1] = m_zone;    // zone
        ex.data[2] = m_subzone; // subzone

        // Put event in receive queue
        return eventExToReceiveQueue(ex);
    }

    return false;
}

// ----------------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////
// addEvent2SendQueue
//

bool
CAutomation::addEvent2SendQueue(const vscpEvent* pEvent)
{
    pthread_mutex_lock(&m_mutexSendQueue);
    m_sendList.push_back((vscpEvent*)pEvent);
    sem_post(&m_semSendQueue);
    pthread_mutex_unlock(&m_mutexSendQueue);
    return true;
}

//////////////////////////////////////////////////////////////////////
//                Workerthread - CAutomationWorkerTread
//////////////////////////////////////////////////////////////////////

void*
workerThread(void* pData)
{
    __attribute__((unused)) fd_set rdfs;
    __attribute__((unused)) struct timeval tv;

    CAutomation* pObj = (CAutomation*)pData;
    if (NULL == pObj) {
        syslog(LOG_ERR,
               "[vscpl2drv-automation] No object data supplied for worker "
               "thread. Terminating");
        return NULL;
    }

#if DEBUG
    syslog(LOG_DEBUG,
           "[vscpl2drv-automation] CWriteSocketCanTread: Interface: %s\n",
           ifname);
#endif

    // Do work at least once a second. Check incoming
    // event right away.
    while (!pObj->m_bQuit) {

        // Do the automation work
        pObj->doWork();

        // Check for incoming event
        int rv;
        if (-1 == (rv = vscp_sem_wait(&pObj->m_semSendQueue, 1000))) {    
            if (EINTR == errno) {
                syslog(LOG_INFO,
                       "[vscpl2drv-automation] Interrupted by a signal "
                       "handler. Terminating.");
                pObj->m_bQuit = true;
            } else if (EINVAL == errno) {
                syslog(
                  LOG_ERR,
                  "[vscpl2drv-automation] Invalid semaphore. Terminating.");
                pObj->m_bQuit = true;
            }
        }

        // Check if there is event(s) for us
        if (pObj->m_sendList.size()) {

            // Yes there are event/(s) in the queue
            // Handle

            pthread_mutex_lock(&pObj->m_mutexSendQueue);
            vscpEvent* pEvent = pObj->m_sendList.front();
            pObj->m_sendList.pop_front();
            pthread_mutex_unlock(&pObj->m_mutexSendQueue);

            if (NULL == pEvent) {
                continue;
            }

            // Only HLO object event is of interst to us
            if ((VSCP_CLASS2_HLO == pEvent->vscp_class) &&
                (VSCP2_TYPE_HLO_COMMAND == pEvent->vscp_type) && 
                vscp_isSameGUID(pObj->m_guid.getGUID(), pEvent->GUID) ) {
                pObj->handleHLO(pEvent);
            }

            vscp_deleteEvent(pEvent);
            pEvent = NULL;

        } // event to send

    } // Outer loop

    return NULL;
}
