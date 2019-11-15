// automation.h
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
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

#if !defined(VSCPAUTOMATION__INCLUDED_)
#define VSCPAUTOMATION__INCLUDED_

#define _POSIX

#include <list>
#include <sstream>
#include <string>

#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include <guid.h>
#include <vscpdatetime.h>

class CControlObject;





///////////////////////////////////////////////////////////////////////////////
// VSCP automation HLO object
//

class CHLO
{

  public:
    /// Constructor
    CHLO(void);

    // Destructor
    virtual ~CHLO(void);

    // ---------------------------------------

    // HLO operation
    uint8_t m_op;

    // HLO name
    std::string m_name;

    // Variable type
    uint8_t m_varType;

    // HLO value
    std::string m_value;

    // For VSCP remote variables full format
    bool m_bFull;
};

///////////////////////////////////////////////////////////////////////////////
// Class that holds one VSCP automation object
//

class CAutomation
{

  public:
    /// Constructor
    CAutomation(void);

    // Destructor
    virtual ~CAutomation(void);

    /*!
       Open operations
       @return True on success.
    */
    bool open(const std::string &path);

    /*!
        Close operations
     */
    void close(void);

    /*!
        Add event to send queue
     */
    bool addEvent2SendQueue(const vscpEvent *pEvent);

    /*!
        \return Greater than zero if Daylight Saving Time is in effect,
        zero if Daylight Saving Time is not in effect, and less than
        zero if the information is not available.
    */
    int isDaylightSavingTime();

    /*!
        Get the hours our locale differs from GMT
    */
    int getTimeZoneDiffHours();

    /*!

    */
    static double FNday(int y, int m, int d, float h);

    /*!

    */
    static double FNrange(double x);

    /*!

    */
    static double f0(double lat, double declin);

    /*!

    */
    static double f1(double lat, double declin);

    /*!

    */
    static double FNsun(double d);

    /*!

    */
    static void convert2HourMinute(double floatTime,
                                   int *pHours,
                                   int *pMinutes);

    /*!
        Calculate Sunset/Sunrice etc
    */
    void doCalc(void);

    /*!
        Put event on receive queue and signal
        that a new event is available

        @param ex Event to send
        @return true on success, false on failure
    */
    bool eventExToReceiveQueue(vscpEventEx &ex);

    /*!
        Do automation work
    */
    bool doWork(void);

    /*!
        Parse HLO

        @param size Size of HLO object 0-511 bytes
        @param buf Pointer to buf containing HLO
        @param phlo Pointer to HLO taht will get parsed data
        @return true on successfull parsing, false otherwise
    */
    bool parseHLO(uint16_t size, uint8_t *buf, CHLO *phlo);

    /*!
        Handle High Level Object events

        Note: The supplied event is deleted by the calling
        routine.
        @param pEvent Pointer to HLO event.
        @return true on successful parsing, false otherwise
    */
    bool handleHLO(vscpEvent *pEvent);

    /// Setter for zone
    void setZone(uint8_t zone) { m_zone = zone; }

    /// getter for zone
    uint8_t getZone(void) { return m_zone; }

    std::string getZoneStr(void)
    {
        std::ostringstream strs;
        strs << m_zone;
        return strs.str();
    }

    /// Setter for subzone
    void setSubzone(uint8_t subzone) { m_subzone = subzone; };

    /// getter for subzone
    uint8_t getSubzone(void) { return m_subzone; };

    std::string getSubZoneStr(void)
    {
        std::ostringstream strs;
        strs << m_subzone;
        return strs.str();
    }

    /// setter for m_bWrite
    void enableWrite(bool bEnable = true) { m_bWrite = bEnable; };

    /// setter for m_bWrite
    void disableWrite(void) { m_bWrite = false; };

    /// setter for m_bCalculatedNoonEvent
    void enableCalculatedNoonEvent(bool bEnable = true)
    {
        m_bCalculatedNoonEvent = bEnable;
    };

    /// setter for m_bCalculatedNoonEvent
    void disableCalculatedNoonEvent(void) { m_bCalculatedNoonEvent = false; };

    /// setter for bSunRiseEvent
    void enableSunRiseEvent(bool bEnable = true) { m_bSunRiseEvent = bEnable; };

    /// setter for bSunRiseEvent
    void disableSunRiseEvent(void) { m_bSunRiseEvent = false; };

    /// setter for bSunSetEvent
    void enableSunSetEvent(bool bEnable = true) { m_bSunSetEvent = bEnable; };

    /// setter for bSunSetEvent
    void disableSunSetEvent(void) { m_bSunSetEvent = false; };

    /// setter for bSunRiseTwilightEven
    void enableSunRiseTwilightEvent(bool bEnable = true)
    {
        m_bSunRiseTwilightEvent = bEnable;
    };

    /// setter for bSunRiseTwilightEvent
    void disableSunRiseTwilightEvent(void) { m_bSunRiseTwilightEvent = false; };

    /// setter for bSunSetTwilightEvent
    void enableSunSetTwilightEvent(bool bEnable = true)
    {
        m_bSunSetTwilightEvent = bEnable;
    };

    /// setter for bSunSetTwilightEvent
    void disableSunSetTwilightEvent(void) { m_bSunSetTwilightEvent = false; };

    /// setter for longitude
    void setLongitude(double l) { m_longitude = l; };
    std::string getLongitudeStr(void)
    {
        std::ostringstream strs;
        strs << m_longitude;
        return strs.str();
    }

    /// setter for latitude
    void setLatitude(double l) { m_latitude = l; };
    std::string getLatitudeStr(void)
    {
        std::ostringstream strs;
        strs << m_latitude;
        return strs.str();
    }

    // Setter/getter for automation enable/disable
    void enableAutomation(bool bVal = true) { m_bEnableAutomation = bVal; };
    void disableAutomation(void) { m_bEnableAutomation = false; };
    bool isAutomationEnabled(void) { return m_bEnableAutomation; };

    vscpdatetime &getLastCalculation(void) { return m_lastCalculation; };

    vscpdatetime &getCivilTwilightSunriseTime(void)
    {
        return m_civilTwilightSunriseTime;
    };
    vscpdatetime &getSentCivilTwilightSunriseTime(void)
    {
        return m_civilTwilightSunriseTime_sent;
    };

    vscpdatetime &getCalulatedNoonTime(void) { return m_noonTime; };

    vscpdatetime &getSunriseTime(void) { return m_SunriseTime; };
    vscpdatetime &getSentSunriseTime(void) { return m_SunriseTime_sent; };

    vscpdatetime &getSunsetTime(void) { return m_SunsetTime; };
    vscpdatetime &getSentSunsetTime(void) { return m_SunsetTime_sent; };

    vscpdatetime &getCivilTwilightSunsetTime(void)
    {
        return m_civilTwilightSunsetTime;
    };
    vscpdatetime &getSentCivilTwilightSunsetTime(void)
    {
        return m_civilTwilightSunsetTime_sent;
    };

    vscpdatetime &getNoonTime(void) { return m_noonTime; };
    vscpdatetime &getSentNoonTime(void) { return m_noonTime_sent; };

    double getLongitude(void) { return m_longitude; };
    double getLatitude(void) { return m_latitude; };

    double getDayLength(void)
    {
        return m_daylength;
    }; // use convert2HourMinute to hh:mm
    std::string getDayLengthStr(void)
    {
        std::ostringstream strs;
        strs << m_daylength;
        return strs.str();
    }


    double getDeclination(void) { return m_declination; };
    std::string getDeclinationStr(void)
    {
        std::ostringstream strs;
        strs << m_declination;
        return strs.str();
    }

    double getSunMaxAltitude(void) { return m_SunMaxAltitude; };
    std::string getSunMaxAltitudeStr(void)
    {
        std::ostringstream strs;
        strs << m_SunMaxAltitude;
        return strs.str();
    }

    bool isSendSunriseEvent(void) { return m_bSunRiseEvent; };
    vscpdatetime &getSentSunriseEvent(void) { return m_SunriseTime_sent; };

    bool isSendSunriseTwilightEvent(void) { return m_bSunRiseTwilightEvent; };
    vscpdatetime &getSentSunriseTwilightEvent(void)
    {
        return m_civilTwilightSunriseTime_sent;
    };

    bool isSendSunsetEvent(void) { return m_bSunSetEvent; };
    vscpdatetime &getSentSunsetEvent(void) { return m_SunsetTime_sent; };

    bool isSendSunsetTwilightEvent(void) { return m_bSunSetTwilightEvent; };
    vscpdatetime &getSunsetTwilightEventSent(void)
    {
        return m_civilTwilightSunsetTime_sent;
    };

    bool isSendCalculatedNoonEvent(void) { return m_bCalculatedNoonEvent; };
    vscpdatetime &getSentCalculatedNoonEvent(void) { return m_noonTime_sent; };

    std::string convertToBASE64( std::string str) { return (vscp_base64_std_encode( str ) ? str : std::string("") ); }


  public:
    /// Debug flag set in config
    bool m_bDebug;

    /// True if configuration can be saved (written)
    bool m_bWrite;

    /// Run flag
    bool m_bQuit;

    /// Incoming filter
    vscpEventFilter m_vscpfilter;

    // Driver GUID - should be unique
    cguid m_guid;

    /// Get GUID for this interface.
    cguid m_ifguid;

    /// Pointer to worker threads
    pthread_t m_threadWork;

    std::list<vscpEvent *> m_sendList;
    std::list<vscpEvent *> m_receiveList;

    /*!
      Event object to indicate that there is an event in the output queue
     */
    sem_t m_semSendQueue;
    sem_t m_semReceiveQueue;

    // Mutex to protect the output queue
    pthread_mutex_t m_mutexSendQueue;
    pthread_mutex_t m_mutexReceiveQueue;

    bool m_bEnableAutomation;

    /// Zone that automation server belongs to
    uint8_t m_zone;

    /// Sub zone that automation server belongs to
    uint8_t m_subzone;

    /// Longitude for this server
    double m_longitude;

    /// Latitude for this server
    double m_latitude;

  private:
    /*!
        Enable/disable the CLASS1.INFORMATION, Type=52 (Civil sunrise twilight
       time) to be sent. Longitude, latitude and time zone must be set for this
       to work correctly.
    */
    bool m_bSunRiseEvent;

    /*!
        Enable/disable the CLASS1.INFORMATION, Type=52 (Civil sunrise twilight
       time) to be sent. Longitude, latitude and time zone must be set for this
       to work correctly.
    */
    bool m_bSunRiseTwilightEvent;

    /*!
        Enable/disable the CLASS1.INFORMATION, Type=45 (Sunset) to be sent.
        Longitude, latitude and time zone must be set for this to work
       correctly.
    */
    bool m_bSunSetEvent;

    /*!
        Enable/disable the CLASS1.INFORMATION, Type=53 (Civil sunset twilight
        time) to be sent. Longitude, latitude and time zone must be set for this
        to work correctly.
    */
    bool m_bSunSetTwilightEvent;

    bool m_bNoonEvent;

    /*!
        Enable/disable the CLASS1.INFORMATION, Type=58 (Calculated Noon) to be
       sent. Longitude, latitude and time zone must be set for this to work
       correctly.
    */
    bool m_bCalculatedNoonEvent;

    /*!
        calculations holders.
        ---------------------
        Calculated values
        use convert2HourMinute to convert from
        double to hour/minutes
    */
    double m_declination;
    double m_daylength; // hours/minutes
    double m_SunMaxAltitude;

    /*!
        Calculations done every 24 hours and at startup
    */
    vscpdatetime m_lastCalculation;

    vscpdatetime m_civilTwilightSunriseTime;
    vscpdatetime m_civilTwilightSunriseTime_sent;

    vscpdatetime m_SunriseTime;
    vscpdatetime m_SunriseTime_sent;

    vscpdatetime m_SunsetTime;
    vscpdatetime m_SunsetTime_sent;

    vscpdatetime m_civilTwilightSunsetTime;
    vscpdatetime m_civilTwilightSunsetTime_sent;

    vscpdatetime m_noonTime;
    vscpdatetime m_noonTime_sent;

    /*!
        Set to true when calculations has been done and
        time is 12:00
    */
    bool m_bCalulationHasBeenDone;
};

#endif