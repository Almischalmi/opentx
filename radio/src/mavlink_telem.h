/*
 * Copyright (C) OpenTX
 *
 * Based on code named
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
/*
 * (c) www.olliw.eu, OlliW, OlliW42
 */

#define MAVLINK_COMM_NUM_BUFFERS 		1 // 4
#define MAVLINK_MAX_SIGNING_STREAMS 	1 // 16

#include "thirdparty/Mavlink/c_library_v2/mavlink_types.h"
//#include "thirdparty/Mavlink/c_library_v2/common/mavlink.h"
// checking for lost frames by analyzing seq won't work if we use common and not ardupilotmega
#include "thirdparty/Mavlink/c_library_v2/ardupilotmega/mavlink.h"


#define MAVLINK_TELEM_MY_SYSID 			254 //MP is 255, QGC is
#define MAVLINK_TELEM_MY_COMPID			(MAV_COMP_ID_MISSIONPLANNER + 1)


// wakeup() is currently called every 10 ms
// if one is changing this, timing needs to be adapted
#define MAVLINK_TELEM_RECEIVING_TIMEOUT			300 // 3 secs
#define MAVLINK_TELEM_RADIO_RECEIVING_TIMEOUT	300 // 3 secs


class MavlinkTelem
{
  public:
	MavlinkTelem() { _reset(); } // constructor

    void wakeup();
    void handleMessage(void);
    void doTask(void);

    bool isInVersionV2(void);
    void setOutVersionV2(void);
    void setOutVersionV1(void);

    void generateHeartbeat(uint8_t base_mode, uint32_t custom_mode, uint8_t system_status);
    //to autopilot
    void generateRequestDataStream(uint8_t tsystem, uint8_t tcomponent, uint8_t data_stream, uint16_t rate, uint8_t startstop);
    void generateParamRequestList(uint8_t tsystem, uint8_t tcomponent);
    //to camera
    void generateRequestCameraInformation(uint8_t tsystem, uint8_t tcomponent);
    void generateRequestCameraSettings(uint8_t tsystem, uint8_t tcomponent);
    void generateRequestStorageInformation(uint8_t tsystem, uint8_t tcomponent);
    void generateRequestCameraCapturesStatus(uint8_t tsystem, uint8_t tcomponent);
    void generateCmdSetCameraMode(uint8_t tsystem, uint8_t tcomponent, uint8_t mode);
    void generateCmdImageStartCapture(uint8_t tsystem, uint8_t tcomponent);
    void generateCmdVideoStartCapture(uint8_t tsystem, uint8_t tcomponent);
    void generateCmdVideoStopCapture(uint8_t tsystem, uint8_t tcomponent);
    //to gimbal
    void generateCmdDoMountConfigure(uint8_t tsystem, uint8_t tcomponent, uint8_t mode);
    void generateCmdDoMountControl(uint8_t tsystem, uint8_t tcomponent, float pitch, float yaw);

    bool isSystemIdValid(void){ return (_sysid > 0); }

    void handleMessageAutopilot(void);
    void requestDataStreamFromAutopilot(void);
    void handleMessageCamera(void);
    void handleMessageGimbal(void);

    #define SETTASK(x)          {_task |= (x);}
    #define RESETTASK(x)        {_task &=~ (x);}
    #define TASK_IS_PENDING     (_task > 0)

    uint32_t msg_rx_count;
    uint32_t msg_rx_persec;
    uint32_t bytes_rx_persec;
    uint32_t msg_rx_lost;

    // MAVLINK
    // the widget background task is called at 50ms, = 576 bytes max at 115200 bps
    // FIXME: we need something better here!!!
    // use two COMM'S ??!?!??!?
    // 16 is sufficient for a rate of 43 msg/s or 1350 bytes/s, 8 was NOT!
    Fifo<mavlink_message_t, 2> msgRxFifo;  // HUGE! 16* 256 = 5kB
    bool msgFifo_enabled = false;
    void setTaskParamRequestList(void){ SETTASK(TASK_SENDPARAMREQUESTLIST); }

    // MAVSDK GENERAL
    bool isReceiving(void){ return (_is_receiving > 0); }
    const mavlink_status_t* getChannelStatus(void){ return &_status; }

    uint8_t autopilottype = MAV_AUTOPILOT_GENERIC;
    uint8_t vehicletype = MAV_TYPE_GENERIC;
    uint8_t flightmode = 0;

    struct Radio {
        uint16_t is_receiving;
        uint8_t rssi;
        uint8_t remrssi;
        uint8_t noise;
        uint8_t remnoise;
    };
    struct Radio radio;

    struct Comp { // not all fields are relevant for/used by all components
        uint8_t compid;
        uint16_t is_receiving;
        uint8_t system_status;
        uint32_t custom_mode;
		bool is_armed;
        bool prearm_ok;
        uint8_t requests_triggered;
        tmr10ms_t requests_tlast;
    };
    struct Comp autopilot;
    struct Comp gimbal;
    struct Comp camera;

    // MAVSDK AUTOPILOT
    struct Att {
    	float roll; // rad
    	float pitch; // rad
    	float yaw; // rad
    };
    struct Att att;

    struct Gps {
    	uint8_t fix;
    	uint8_t sat;
    	uint16_t hdop;
    	uint16_t vdop;
    	int32_t lat; // (WGS84), in degrees * 1E7*/
    	int32_t lon; // (WGS84), in degrees * 1E7*/
    	int32_t alt; // (AMSL, NOT WGS84), in meters * 1000
    	uint16_t vel; // m/s * 100
    	uint16_t cog; // degrees * 100, 0.0..359.99 degrees
    };
    struct Gps gps;

    struct GlobalPositionInt {
    	int32_t relative_alt; // mm
    };
    struct GlobalPositionInt gposition;

    struct Vfr {
    	 float airspd; // m/s
    	 float groundspd; // m/s
    	 float alt; // (MSL), m     ?? is this really MSL ?? it can't I think, appears to be above home
    	 float climbrate; // m/s
    	 int16_t heading; // degrees (0..360, 0=north)
    	 uint16_t thro; // percent, 0 to 100
    };
    struct Vfr vfr;

    struct Bat {
    	int32_t charge_consumed; // mAh, -1 if not known
    	int32_t energy_consumed; // 0.1 kJ, -1 if not known
    	int16_t temperature; // centi-degrees C�, INT16_MAX if not known
    	uint32_t voltage; // mV
    	int16_t current; // 10*mA, -1 if not known
    	int8_t remainingpct; //(0%: 0, 100%: 100), -1 if not known
    	int8_t cellcount; //-1 if not known
    };
    struct Bat bat1;
    struct Bat bat2;
    uint8_t bat_instancemask;

    // MAVSDK CAMERA
    struct CameraInfo {
    	char vendor_name[32+1];
		char model_name[32+1];
		uint32_t flags;
		bool has_video;
		bool has_photo;
		bool has_modes;
    	float total_capacity; // NAN if not known
		bool info_received; // this is to get all info at startup
		bool settings_received; // this is to get all info at startup
		bool status_received; // this is to get all info at startup
    };
    struct CameraInfo cameraInfo;  // Info: static data

    struct CameraStatus {
    	uint8_t mode;
		bool video_on;
    	bool photo_on;
    	float available_capacity; // NAN if not known
    	uint32_t recording_time_ms;
    	float battery_voltage; // NAN if not known
    	int8_t battery_remainingpct; //(0%: 0, 100%: 100), -1 if not known
		bool initialized; // this indicates that all startup info was received
    };
    struct CameraStatus cameraStatus; // Status: variable data

    void setCameraSetVideoMode(void){ SETTASK(TASK_SENDCMD_SET_CAMERA_VIDEO_MODE); }
	void setCameraSetPhotoMode(void){ SETTASK(TASK_SENDCMD_SET_CAMERA_PHOTO_MODE); }
    void setCameraStartVideo(void){ SETTASK(TASK_SENDCMD_VIDEO_START_CAPTURE); }
    void setCameraStopVideo(void){ SETTASK(TASK_SENDCMD_VIDEO_STOP_CAPTURE); }
    void setCameraTakePhoto(void){ SETTASK(TASK_SENDCMD_IMAGE_START_CAPTURE); }

    // MAVSDK GIMBAL
    struct GimbalAtt {
    	float roll; // rad
    	float pitch; // rad
    	float yaw_relative; // rad
    	float yaw_absolute; // rad
    };
    struct GimbalAtt gimbalAtt;

    uint8_t _gimbal_domountconfigure_mode;
    void setGimbalTargetingMode(uint8_t mode){
    	_gimbal_domountconfigure_mode = mode; SETTASK(TASK_SENDCMD_DO_MOUNT_CONFIGURE);
    }
    float _gimbal_domountcontrol_pitch, _gimbal_domountcontrol_yaw;
    void setGimbalPitchYawDeg(float pitch, float yaw){
    	_gimbal_domountcontrol_pitch = pitch; _gimbal_domountcontrol_yaw = yaw; SETTASK(TASK_SENDCMD_DO_MOUNT_CONTROL);
    }

  protected:
    void _reset(void);
    void _resetRadio(void);
    void _resetAutopilot(void);
    void _resetGimbal(void);
    void _resetCamera(void);

    uint8_t _my_sysid = MAVLINK_TELEM_MY_SYSID;
    uint8_t _my_compid = MAVLINK_TELEM_MY_COMPID;
    tmr10ms_t _my_heartbeat_tlast;

    uint8_t _sysid = 0; // is autodetected by inspecting the autopilot heartbeat

    uint16_t _is_receiving = 0;

    typedef enum {
      TASK_SENDMYHEARTBEAT                          = 0x00000001,
      //to autopilot
      TASK_SENDREQUESTDATASTREAM_RAW_SENSORS        = 0x00000010, // group 1
      TASK_SENDREQUESTDATASTREAM_EXTENDED_STATUS    = 0x00000020, // group 2
      TASK_SENDREQUESTDATASTREAM_RC_CHANNELS        = 0x00000040, // group 3
      TASK_SENDREQUESTDATASTREAM_RAW_CONTROLLER     = 0x00000080, // group 4
      TASK_SENDREQUESTDATASTREAM_POSITION           = 0x00000100, // group 6
      TASK_SENDREQUESTDATASTREAM_EXTRA1             = 0x00000200, // group 10
      TASK_SENDREQUESTDATASTREAM_EXTRA2             = 0x00000400, // group 11
      TASK_SENDREQUESTDATASTREAM_EXTRA3             = 0x00000800, // group 12
      TASK_SENDPARAMREQUESTLIST                     = 0x00001000,
      //to camera
      TASK_SENDREQUEST_CAMERA_INFORMATION           = 0x00010000,
      TASK_SENDREQUEST_CAMERA_SETTINGS              = 0x00020000,
      TASK_SENDREQUEST_STORAGE_INFORMATION          = 0x00040000,
      TASK_SENDREQUEST_CAMERA_CAPTURE_STATUS        = 0x00080000,
      TASK_SENDCMD_SET_CAMERA_VIDEO_MODE            = 0x00100000,
      TASK_SENDCMD_SET_CAMERA_PHOTO_MODE            = 0x00200000,
      TASK_SENDCMD_VIDEO_START_CAPTURE              = 0x00400000,
      TASK_SENDCMD_VIDEO_STOP_CAPTURE               = 0x00800000,
      TASK_SENDCMD_IMAGE_START_CAPTURE              = 0x01000000,
      //to gimbal
      TASK_SENDCMD_DO_MOUNT_CONFIGURE               = 0x10000000,
      TASK_SENDCMD_DO_MOUNT_CONTROL                 = 0x20000000,
    } TASKMASKENUM;

    uint32_t _task;

    struct Request {
        uint32_t task;
        uint8_t retry;
        tmr10ms_t tlast;
    };
    Fifo<struct Request, 32> _requestFifo;
    tmr10ms_t _requestFifo_tlast;

    void push_request(uint32_t task, uint8_t retry) {
        struct Request r; r.task = task; r.retry = retry; r.tlast = get_tmr10ms(); _requestFifo.push(r);
    }
    void pop_and_set_request(void) {
        struct Request r; _requestFifo.pop(r); SETTASK(r.task);
    }

    uint32_t _msg_rx_persec_cnt, _bytes_rx_persec_cnt;
    int16_t _seq_rx_last = -1;

    mavlink_status_t _status;
    mavlink_message_t _msg;
    mavlink_message_t _msg_out;
    uint16_t _txcount = 0;
    uint8_t _txbuf[512]; //only needs to hold one message, thus by construction large enough
};


extern MavlinkTelem mavlinkTelem;
