# vscpl2drv-automation

<img src="https://vscp.org/images/logo.png" width="100">

    Available for: Linux, Windows
    Driver Linux: vscpl2drv-automation.so
    Driver Windows: vscpl2drv-automation.dll

A driver that send automation events from location data that is automatically calculated once every twenty four hours. It is possible to configure most details including which of the events that should be sent.

### Events

 - [CLASS1.INFORMATION=20, Type=45 (0x2d) - Sunset](https://grodansparadis.gitbooks.io/the-vscp-specification/class1.information.html#type44)
 - [CLASS1.INFORMATION=20, Type=44 (0x2c) - Sunrise](https://grodansparadis.gitbooks.io/the-vscp-specification/class1.information.html#type44)
 - [CLASS1.INFORMATION=20, Type=53 (0x35) - Civil sunset twilight time]()
 - [CLASS1.INFORMATION=20, Type=55 (0x37) - Nautical sunset twilight time](https://grodansparadis.gitbooks.io/the-vscp-specification/class1.information.html#type55)
 - [CLASS1.INFORMATION=20, Type=58 (0x3A) - Calculated Noon](https://grodansparadis.gitbooks.io/the-vscp-specification/class1.information.html#type58)

## Configuration

### Linux

#### VSCP daemon driver config

The VSCP daemon configuration is (normally) located at */etc/vscp/vscpd.conf*. To use the vscpl2drv-automation driver there must be an entry in the

```
> <level2driver enable="true">
```

section on the following format

```xml
<!-- Level II automation -->
<driver enable="true"
    name="automation"
    path="/usr/bin/vscpl2drv-automation.so"
    guid="FF:FF:FF:FF:FF:FF:FF:FC:88:99:AA:BB:CC:DD:EE:FF"
</driver>
```

##### enable
Set enable to "true" if the driver should be loaded.

##### name
This is the name of the driver. Used when referring to it in different interfaces.

##### path
This is the path to the driver. If you install from a Debian package this will be */usr/bin/vscpl2drv-automation.so* and if you build and install the driver yourself it will be */usr/local/bin/vscpl2drv-automation.so* or a custom location if you configured that.

##### guid
All level II drivers must have a unique GUID. There is many ways to obtain this GUID, Read more [here](https://grodansparadis.gitbooks.io/the-vscp-specification/vscp_globally_unique_identifiers.html).

#### vscpl2drv-automation driver config

On start up the configuration is read from the path set in the driver configuration of the VSCP daemon, usually */etc/vscp/conf-file-name* and values are set from this location. If the **write** parameter is set to "true" the above location is a bad choice as the VSCP daemon will not be able to write to it. A better location is */var/lib/vscp/drivername/configure.xml* or some other writable location in this cased.

The configuration file have the following format

```xml
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
            filter="incoming-filter"
            mask="incoming-mask" />
```

##### debug
Set debug to "true" to get debug information written to syslog. This can be a valuable help if things does nor behave as expected.

##### write
If write is true a configuration file will be looked for in */var/lib/vscp/drivername/configure.xml*. If this file is found it wil be read and any value present in it will replace a value read from the main configuration file.

##### guid
All level II drivers must have a unique GUID. There is many ways to obtain this GUID, Read more [here](https://grodansparadis.gitbooks.io/the-vscp-specification/vscp_globally_unique_identifiers.html).

##### zone
Enter the zone that should be used for the events. Any number is good if you do not intend to check this field in events. Default is zero.

##### subzone
Enter the subzone that should be used for the events. Any number is good if you do not intend to check this field in events. Default is zero.

##### longitude
Enter the longitude as a decimal value for the place you want the calculations to be performed for. Default is a place named Los, in the middle of Sweden.

##### latitude
Enter the latitude as a decimal value for the place you want the calculations to be performed for. Default is a place named Los, in the middle of Sweden.

##### enable-sunrise
Enable the sunrise event by setting this value to "true". Disable by setting it to "false".

##### enable-sunrise-twilight
Enable the sunrise-twilight event by setting this value to "true". Disable by setting it to "false".

##### enable-sunset
Enable the sunset event by setting this value to "true". Disable by setting it to "false".

##### enable-sunset-twilight
Enable the sunset-twilight event by setting this value to "true". Disable by setting it to "false".

##### enable-noon
Enable the noon event by setting this value to "true". Disable by setting it to "false".

##### filter
Filter and mask is a way to select which events is received by the driver. A filter have the following format

> priority,vscpclass,vscptype,guid

All values can be give in decimal or hexadecimal (preceded number with '0x'). GUID is always given i hexadecimal (without preceded '0x').

Default setting is

> 0,0,0,00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00

Read the vscpd manual for more information about how filter/masks work.

The default filter/mask pair means that all events are received by the driver.

##### mask
Filter and mask is a way to select which events is received by the driver. A mask have the following format

> priority,vscpclass,vscptype,guid

All values can be give in decimal or hexadecimal (preceded number with '0x'). GUID is always given i hexadecimal (without preceded '0x').

The mask have a binary one ('1') in the but position of the filter that should have a specific value and zero ('0') for a don't care bit.

Default setting is

> 0,0,0,00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00

Read the vscpd manual for more information about how filter/masks work.

The default filter/mask pair means that all events are received by the driver.

### Windows
See information from Linux. The only difference is the disk location from where configuration data is fetched.

## Install the driver on Linux
You can install the driver using the debian package with

> sudo dpkg -i vscpl2drv-automation

the driver will be installed to /usr/lib

After installing the driver you need to add it to the vscpd.conf file (/etc/vscp/vscpd.conf). Se the *configuration* section above.

You also need to set up a configuration file for the driver. If you don't need to dynamically edit the content of this file a good and safe location for it is in the */etc/vscp/* folder alongside the VSCP daemon configuration file.

If you need to do dynamic configuration we recommend that you create the file in the */var/vscp/vscpl2drv-automation*

A sample configuration file is make available in */usr/share/vscpl2drv-automation* during installation.

## Install the driver on Windows
tbd

## How to build the driver on Linux
To build this driver you to clone the driver source

```
git clone --recurse-submodules -j8 https://github.com/grodansparadis/vscpl2drv-automation.git
cd vscpl2drv-automation
./configure
make
make install
```

Default install folder is */usr/local/lib*

You need build-essentials and git installed on your system

>sudo apt update && sudo apt -y upgrade
>sudo apt install build-essential git

## How to build the driver on Windows
tbd

## Using the vscpl2drv-automation driver

If you just want the automation events installing the driver and configuring it is all you need to do. It will deliver the events when they are due.

There is however other possibilities built into the driver.

### Get calculated sunset time

Send a High Level Data event to the driver with the following payload

```xml
<vscp-cmd  op = "readvar"
           name = "sunset-time"
           full = "false"
}
```

response will be

```xml
<vscp-resp op="readvar"
           result="true"
           type="15"
           value="BASE64(HH:MM:SS)"
           ---- more attribytes for full="true"
}
```

where

**result** is 'true' for success and 'false' if not.

**type** is **15** indicating that this is a remote variable with a value that should be interpreted as a time.

**value** is the BASE64 encoded value of the time string HH:MM:SS.

You can read the following remote variable values from the vscpl2drv-automation driver

| Variable | Type | Description |
| -------- | :----: | ----------------- |
| sunset | 15 (TIME) | A BASE65 encoded time HH:MM:SS. |
| sunrise | 15 (TIME)  | A BASE65 encoded time HH:MM:SS. |
| sunsetTwilight | 15 (TIME)  | A BASE65 encoded time HH:MM:SS. |
| sunriseTwilight | 15 (TIME)  | A BASE65 encoded time HH:MM:SS. |
| noon | 15 (TIME)  | A BASE65 encoded time HH:MM:SS. |
| sentSunset | 13 (DATETIME) | A BASE65 encoded datetime YYYY-MM-DDTHH:MM:SS. |
| sentSunrise | 13 (DATETIME)  | A BASE65 encoded datetime YYYY-MM-DDTHH:MM:SS. |
| sentSunset-twilight | 13 (DATETIME)  | A BASE65 datetime YYYY-MM-DDTHH:MM:SS. |
| sentSunrise-twilight | 13 (DATETIME)  | datetime YYYY-MM-DDTHH:MM:SS. |
| sentNoon | 13 (DATETIME)  | A BASE65 encoded datetime YYYY-MM-DDTHH:MM:SS. |
| enableSunset | 2 (BOOL)  | rw A BASE65 encoded boolean. |
| enableSunrise | 2 (BOOL) | rw A BASE65 encoded boolean. |
| enableSunset_twilight | rw 2 (BOOL) | rw A BASE65 encoded boolean. |
| enable-sunrise_twilight | rw 2 (BOOL) | rw A BASE65 encoded boolean. |
| enableNoon | 2 (BOOL) | rw A BASE65 encoded boolean. |
| longitude | 5 (DOUBLE) | rw A BASE65 encoded floating point value. |
| latitude | 5 (DOUBLE) | rw A BASE65 encoded encoded floating point value. |
| zone | 3 (INT) | rw A BASE65 encoded integer value. |
| subzone | 3 (INT) | rw A BASE65 encoded integer value. |
| daylength | 5 (DOUBLE) | rw A BASE65 encoded encoded floating point value. |
| declination | 5 (DOUBLE) | rw A BASE65 encoded encoded floating point value. |
| SunMaxAltitude | 5 (DOUBLE) | rw A BASE65 encoded encoded floating point value. |
| LastCalculation | 5 (DOUBLE) | rw A BASE65 encoded encoded floating point value. |


| Command | Description |
| ------- | ----------- |
| calculate | Do a new calculation now |
| save | Save configuration to disk (if writable) |
| load | Load configuration from disk |