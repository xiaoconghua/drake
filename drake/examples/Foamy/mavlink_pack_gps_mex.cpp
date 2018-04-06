#include "mex.h"
#include <mavlink.h>
#include <sys/time.h>
#include <math.h>

#define PI 3.141592653589793

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]) {
    
    //Do some checks
    if(nrhs < 2) {
        mexErrMsgTxt("Not enough input arguments.");
        return;
    }
    if(nrhs > 2) {
        mexErrMsgTxt("Too many input arguments.");
        return;
    }
    if(nlhs > 1) {
        mexErrMsgTxt("Too many output arguments.");
        return;
    }
    if(!mxIsDouble(prhs[0])) {
        mexErrMsgTxt("Input must be a double array.");
        return;
    }
    
    double *y = mxGetPr(prhs[0]);
    double *t = mxGetPr(prhs[1]);
    mwSize len_in = mxGetNumberOfElements(prhs[0]);
    
    if(len_in < 17) {
        mexErrMsgTxt("Input must be a double array with 17 elements.");
        return;
    }
    
    double lat = round((1e7*180.0/PI)*y[0]); //latitude in degrees*10^-7
    double lon = round((1e7*180.0/PI)*y[1]); //longitude in degrees*10^-7
    double alt = round(1000.0*y[2]); //altitude in millimeters
    
    double vn = round(100.0*y[3]); //velocity in cm/s in north direction
    double ve = round(100.0*y[4]); //velocity in cm/s in east direction
    double vd = round(100.0*y[5]); //velocity in cm/s in down direction
    
    double vel = round(100.0*sqrt(y[3]*y[3] + y[4]*y[4] + y[5]*y[5])); //velocity magnitude in cm/s
    
    double cog = round((100.0*180.0/PI)*atan2(100.0*y[4], 100.0*y[3])); //course over ground in degrees*100
    
    mavlink_message_t msg;
    uint8_t buf[MAVLINK_MAX_PACKET_LEN];
    uint16_t len;
    
//     timeval tv;
//     gettimeofday(&tv, NULL);
//     uint64_t time_usec = 1000000*tv.tv_sec + tv.tv_usec;
    double time_usec = 1000000.0*t[0];
    
    len = mavlink_msg_hil_gps_pack(0x01, 0xc8, &msg, (uint64_t)time_usec, 3,
                                   (int32_t)lat, (int32_t)lon, (int32_t)alt,
                                   100, 100, (int16_t)vel,
                                   (int16_t)vn, (int16_t)ve, (int16_t)vd,
                                   (uint16_t)cog, 10);
    
    len = mavlink_msg_to_send_buffer(buf, &msg);
    
    plhs[0] = mxCreateNumericMatrix(len, 1, mxUINT8_CLASS, mxREAL);
    uint8_t *s = (uint8_t *)mxGetData(plhs[0]);
    
    for(int k = 0; k < len; k++) {
        s[k] = buf[k];
    }
    
    return;
}