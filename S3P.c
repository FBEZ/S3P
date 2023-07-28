#include <stdio.h>
#include "S3P.h"
#include <time.h>
#include <sys/time.h>

#define SOURCE_ADDRESS_LENGTH 4
#define DESTINATION_ADDRESS_LENGTH 4
#define COMMAND_LENGTH 3
#define ARGUMENT_LENGTH_LENGTH 1
#define HEADER_LENGTH ( SOURCE_ADDRESS_LENGTH + DESTINATION_ADDRESS_LENGTH + COMMAND_LENGTH + ARGUMENT_LENGTH_LENGTH)

#define BROADCAST_ADDRESS 0xFFFFFFFF
#define TYPICAL_DELAY_US 60
#define TIME_SETTING_OFFSET 0 

#define TIMEVAL_TO_US_TIME(tv_sec,tv_usec) (time_us_t)(tv_sec)*1000000L+(time_us_t)(tv_usec)
#define US_TIME_TO_TIMEVAL(us_time, tv) \
    do{ \
    (tv).tv_sec  = (us_time)/1000000; \
    (tv).tv_usec = (us_time)%1000000; \
    }while(0)
#define UNPACK_STREAM_TO_32_BIT(byte_array,from) ((byte_array[(from)] << 24) + (byte_array[1+(from)] << 16) + (byte_array[2+(from)] << 8) + byte_array[3+(from)])
#define PRINT_TIMEVAL(tv) \
  do { \
    printf("Time (s): %lld\n", (tv).tv_sec); \
    printf("Us  (us): %ld\n", (tv).tv_usec); \
  } while (0)

#define PRINT_S3P(s3p) \
  do { \
    printf(" *** S3P: %04lX ***\n", (s3p).address); \
    printf("Op Status: %d\n", (s3p).op_status); \
    printf("Clock Status: %d\n", (s3p).clock_status); \
    printf("t1: %lld\n", (s3p).watch_time_set.t1); \
    printf("t2: %lld\n", (s3p).watch_time_set.t2); \
    printf("t3: %lld\n", (s3p).watch_time_set.t3); \
    printf("t4: %lld\n", (s3p).watch_time_set.t4); \
  } while (0)

typedef enum{
    UNDEFINED,
    SLAVE,
    MASTER
}op_status_t;

typedef enum{
    UNSET,
    SET
}clock_status_t;


typedef struct timeval watch_time_t;

typedef int64_t time_us_t;

typedef struct{ // naming as per typical PTP graph
    time_us_t t1;
    time_us_t t2;
    time_us_t t3;
    time_us_t t4;
}watch_time_set_t;


struct S3P_t{
    op_status_t op_status;
    clock_status_t clock_status;
    uint32_t address;
    struct timeval last_msg_timestamp;
    esp_err_t (*send_packet_func)(uint8_t *, uint16_t);
    watch_time_set_t watch_time_set;
};


void S3P__print_frame(S3P_msg_t msg);
esp_err_t S3P__interpret_command(S3P_t * s3p, S3P_msg_t* msg);

S3P_t * S3P__create(uint32_t address,
                    esp_err_t (*send_packet_func)(uint8_t *, uint16_t)){
    
    S3P_t * ret = (S3P_t *)malloc(sizeof(S3P_t));
    ret->address = address;
    ret->send_packet_func = send_packet_func;
    ret->clock_status = UNSET;
    ret->op_status = UNDEFINED;

    return ret;
}

uint32_t S3P__get_message_length(S3P_msg_t msg){
    return msg.argument_length*4+11; // argument_length are uint32_t
}

esp_err_t S3P__pack_message(S3P_msg_t msg, uint8_t * byte_out, uint32_t * byte_out_length){

    //byte_out = (uint8_t*)malloc(msg.argument_length+14);

    byte_out[3]=(uint8_t)msg.destination;
    byte_out[2]=(uint8_t)(msg.destination>>8);
    byte_out[1]=(uint8_t)(msg.destination>>16);
    byte_out[0]=(uint8_t)(msg.destination>>24);

    byte_out[7]=(uint8_t)msg.source;
    byte_out[6]=(uint8_t)(msg.source>>8);
    byte_out[5]=(uint8_t)(msg.source>>16);
    byte_out[4]=(uint8_t)(msg.source>>24);

    byte_out[10]=(uint8_t)msg.command;
    byte_out[9]=(uint8_t)(msg.command>>8);
    byte_out[8]=(uint8_t)(msg.command>>16);

    byte_out[11]=(uint8_t)msg.argument_length;

    uint16_t k = 0;
    uint8_t j = 0;
    for(k=0;k<msg.argument_length;k++){
        byte_out[HEADER_LENGTH+j] = (uint8_t)(msg.argument[k]>>24);j++;
        byte_out[HEADER_LENGTH+j] = (uint8_t)(msg.argument[k]>>16);j++;
        byte_out[HEADER_LENGTH+j] = (uint8_t)(msg.argument[k]>>8);j++;
        byte_out[HEADER_LENGTH+j] = (uint8_t)(msg.argument[k]);j++;
    }

    *byte_out_length = msg.argument_length*4+HEADER_LENGTH; 

    return ESP_OK;
}

esp_err_t S3P__unpack_message(S3P_msg_t * msg, uint8_t * msg_bytes, uint16_t msg_length){

    msg->destination = UNPACK_STREAM_TO_32_BIT(msg_bytes,0);
    msg->source = UNPACK_STREAM_TO_32_BIT(msg_bytes,4);
    uint32_t read_command = (msg_bytes[8] << 16) + (msg_bytes[9] << 8) + msg_bytes[10]; 

    msg->command = (S3P_command_t)read_command;
    msg->argument_length = msg_bytes[11];
    uint32_t j = HEADER_LENGTH;

    msg->argument = (uint32_t *)malloc(msg->argument_length*sizeof(uint32_t));
    uint8_t k = 0;
    for(k=0;k<msg->argument_length;k++){
        msg->argument[k] = UNPACK_STREAM_TO_32_BIT(msg_bytes,j);
        j=j+4;
    }
    //S3P__print_frame(*msg);
    return ESP_OK;
}

void S3P__print_frame(S3P_msg_t msg){
    printf("\nDestination: %08lX\n", msg.destination);
    printf("\nSource: %08lX\n", msg.source);
    printf("\nCommand: %01X\n", msg.command);
    printf("\nLength: %02X\n", msg.argument_length);
    printf("\nArguments:\n");
    uint8_t k = 0;
    for(k=0;k<msg.argument_length;k++){
        printf("%04lX ", msg.argument[k]);
    }
    printf("\n");
}

bool S3P__filter_message(S3P_t * s3p, uint8_t * msg_bytes){
    uint32_t read_address = (msg_bytes[0] << 24) + (msg_bytes[1] << 16) + (msg_bytes[2] << 8) + msg_bytes[3];
    return (read_address == s3p->address)||(read_address == 0xFFFFFFFF);
}   


esp_err_t S3P__read_message(S3P_t * s3p, uint8_t * msg_bytes, uint16_t msg_length){
     gettimeofday(&s3p->last_msg_timestamp, NULL /* tz */); // save it asap for later use
     S3P_msg_t * msg = (S3P_msg_t*)malloc(sizeof(S3P_msg_t));
     esp_err_t ret = S3P__unpack_message(msg, msg_bytes, msg_length);

    ret = S3P__interpret_command(s3p, msg);

     free(msg);
    return ret;  
}

esp_err_t S3P__WSH(S3P_t * s3p, S3P_msg_t * msg){
    printf("\nCommand WSH detected\n");
            S3P_msg_t reply_msg = {
                .destination = 0x00000000,
                .source = s3p->address,
                .command = IAH,
                .argument_length = 0,
                .argument = NULL
            };
            uint32_t msg_length = 0;
            uint8_t * byte_out = (uint8_t*)malloc(S3P__get_message_length(reply_msg));
            S3P__pack_message(reply_msg, byte_out, &msg_length); 
            s3p->send_packet_func(byte_out,msg_length);
            return ESP_OK;
}


esp_err_t S3P__send_message_empty(S3P_t * s3p, S3P_msg_t * msg, S3P_command_t command){
    if(!((command==ACK) || (command == DRQ) )){
        return ESP_ERR_INVALID_ARG;
    }

// todo: remove argument if DRQ is selected instead of ack
    S3P_msg_t * reply_msg = (S3P_msg_t *)malloc(sizeof(S3P_msg_t));
    reply_msg->destination = msg->source;
    reply_msg->source = s3p->address;
    reply_msg->command = command;
    reply_msg->argument_length =1;
    reply_msg->argument = (uint32_t*)malloc(sizeof(uint32_t));

    reply_msg->argument[0]=(uint32_t)msg->command;
 
    uint32_t msg_length = 0;
    uint8_t * byte_out = (uint8_t*)malloc(S3P__get_message_length(*reply_msg));
    S3P__pack_message(*reply_msg, byte_out, &msg_length); 
    s3p->send_packet_func(byte_out,msg_length);
    return ESP_OK;
}


esp_err_t S3P__STO(S3P_t * s3p, S3P_msg_t * msg){

    if(msg->argument_length!=2){
        return ESP_ERR_INVALID_ARG;
    }

    struct timeval tv;
    tv.tv_sec = msg->argument[0];
    tv.tv_usec =msg->argument[1];

    settimeofday(&tv,0);
    return ESP_OK;
}

void S3P__print_current_time(){
    struct timeval tv;
    struct tm *nowtm;

    gettimeofday(&tv, NULL /* tz */);
    char tmbuf[64];
    nowtm = localtime(&tv.tv_sec);
    strftime(tmbuf, sizeof(tmbuf), "%d-%m-%Y %H:%M:%S", nowtm);

    printf("GMT: %s\n", tmbuf);
    printf("us: %ld\n", tv.tv_usec);
}


esp_err_t S3P__SYN(S3P_t * s3p, S3P_msg_t * msg){

    //t2 is the timestamp
    struct timeval t3;
    S3P__send_message_empty(s3p,msg,DRQ);
    gettimeofday(&t3, NULL /* tz */);
    //End time critical section

    //saving t1 and t2 first required parameter for delay measurement
    s3p->watch_time_set.t1=TIMEVAL_TO_US_TIME(msg->argument[0],msg->argument[1]);
    s3p->watch_time_set.t2=TIMEVAL_TO_US_TIME(s3p->last_msg_timestamp.tv_sec,s3p->last_msg_timestamp.tv_usec);
    s3p->watch_time_set.t3=TIMEVAL_TO_US_TIME(t3.tv_sec,t3.tv_usec);

    return ESP_OK;
}

esp_err_t S3P__DRE(S3P_t * s3p, S3P_msg_t * msg){


    struct timeval t_now;
    struct timeval t_new;

    time_us_t t4 = TIMEVAL_TO_US_TIME(msg->argument[0],msg->argument[1]);


    // tdelay = ((t4-t1)-(t3-t2))/2
    time_us_t time_delay = ((t4                     - s3p->watch_time_set.t1) - 
                  (s3p->watch_time_set.t3 - s3p->watch_time_set.t2))/2;
    
    // toffset = t2-t1-tdelay
    time_us_t time_offset = s3p->watch_time_set.t2-s3p->watch_time_set.t1-time_delay;

    printf("Calculated Delay (us):%lld\n", time_delay);
    printf("Calculated Offset (us):%lld\n", time_offset);

    struct timeval tv_offset;
    US_TIME_TO_TIMEVAL(time_offset,tv_offset);

    // Setting the new time:

    // *** Start Critical fast execution: here everything as timeval because of gettime and settime
    gettimeofday(&t_now, NULL /* tz */);
    timersub(&t_now,&tv_offset, &t_new);
    settimeofday(&t_new, 0);
    // *** End critical fast execution
    s3p->watch_time_set.t4=t4;
    return ESP_OK;
}


esp_err_t S3P__send_message_with_time(S3P_t * s3p, S3P_msg_t * msg, S3P_command_t command){

    uint32_t reply_destination = 0;
    if(command == SYN){
        reply_destination = BROADCAST_ADDRESS;
    }else if (command == DRE || command == RTA)
    {
        reply_destination = msg->source;
    }else{
        return ESP_ERR_INVALID_ARG;
    }

    S3P_msg_t * reply_msg = (S3P_msg_t *)malloc(sizeof(S3P_msg_t));
    reply_msg->destination = reply_destination; // broadcast
    reply_msg->source = s3p->address;
    reply_msg->command = command;
    reply_msg->argument_length =2;
    reply_msg->argument = (uint32_t*)malloc(2*sizeof(uint32_t));

    struct timeval tv;
    gettimeofday(&tv, NULL /* tz */);

    if(command == DRQ){ // here the time must be the timestamp
        reply_msg->argument[0]=(uint32_t)s3p->last_msg_timestamp.tv_sec;
        reply_msg->argument[1]=(uint32_t)s3p->last_msg_timestamp.tv_usec;
    }else{
        reply_msg->argument[0]=(uint32_t)tv.tv_sec;
        reply_msg->argument[1]=(uint32_t)tv.tv_usec;
    }
 
    uint32_t msg_length = 0;
    uint8_t * byte_out = (uint8_t*)malloc(S3P__get_message_length(*reply_msg));
    S3P__pack_message(*reply_msg, byte_out, &msg_length); 
    s3p->send_packet_func(byte_out,msg_length);
    //printf("time sent: %lld\n", tv.tv_sec);
    //printf("us sent: %ld\n", tv.tv_usec);
    return ESP_OK;
}

esp_err_t S3P__interpret_command(S3P_t * s3p, S3P_msg_t* msg){
    switch(msg->command){
        case WSH: 
            printf("Detected WSH\n");
            S3P__send_message_empty(s3p,msg,ACK);
            break;
        case SAM:
            printf("Detected SAM\n");
            s3p->op_status=MASTER;
            S3P__send_message_empty(s3p,msg,ACK);
            break;
        case SAS:
            printf("Detected SAS\n");
            s3p->op_status=SLAVE;
            S3P__send_message_empty(s3p,msg,ACK);
            break;
        case STO:
            printf("Detected STO\n");
            S3P__STO(s3p,msg);
            S3P__send_message_empty(s3p,msg,ACK);
            break;
        case GTA:
            printf("Detected GTA\n");
            S3P__send_message_with_time(s3p,msg,RTA);
            break;
        case SYN: // todo: listen only if right step
            printf("Detected SYN\n"); 
            if(s3p->op_status==SLAVE) // if not slave, ignore
                S3P__SYN(s3p,msg);
            else{
                printf("But I'm %d\n", s3p->op_status);
            }
            break;
        case DRQ: 
            printf("Detected DRQ\n");
            if(s3p->op_status==MASTER) // if not master, ignore
                S3P__send_message_with_time(s3p,msg,DRE);
            else{
                printf("But I'm %d\n", s3p->op_status);
            }
            break;
        case DRE: // todo: listen only if right step
            printf("Detected DRE\n");
            if(s3p->op_status==SLAVE) // if not slave, ignore
                S3P__DRE(s3p,msg);
            else{
                printf("But I'm %d", s3p->op_status);
            }
            break;
        default:
            printf("Not implemented yet or ignored\n");
    }
    printf("\n\n");
    PRINT_S3P(*s3p);
    S3P__print_current_time();
    return ESP_OK;
}

esp_err_t S3P__send_synch(S3P_t * s3p){
    if(s3p->op_status!=MASTER){ // only master performs this routine
        return ESP_OK;
    }

    return S3P__send_message_with_time(s3p, NULL, SYN);
}

