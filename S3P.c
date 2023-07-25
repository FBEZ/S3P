#include <stdio.h>
#include "S3P.h"

#define SOURCE_ADDRESS_LENGTH 4
#define DESTINATION_ADDRESS_LENGTH 4
#define COMMAND_LENGTH 3
#define ARGUMENT_LENGTH_LENGTH 1
#define HEADER_LENGTH ( SOURCE_ADDRESS_LENGTH + DESTINATION_ADDRESS_LENGTH + COMMAND_LENGTH + ARGUMENT_LENGTH_LENGTH)

#define UNPACK_STREAM_TO_32_BIT(byte_array,from) ((byte_array[(from)] << 24) + (byte_array[1+(from)] << 16) + (byte_array[2+(from)] << 8) + byte_array[3+(from)])



typedef enum{
    UNDEFINED,
    SLAVE,
    MASTER
}op_status_t;

typedef enum{
    UNSET,
    SET
}clock_status_t;

struct S3P_t{
    op_status_t op_status;
    clock_status_t clock_status;
    uint32_t address;
    esp_err_t (*send_packet_func)(uint8_t *, uint16_t);
};


void S3P__print_frame(S3P_msg_t msg);
esp_err_t S3P__interpret_command(S3P_t * s3p, S3P_msg_t* msg);

S3P_t * S3P__create(uint32_t address,
                    esp_err_t (*send_packet_func)(uint8_t *, uint16_t)){
    
    S3P_t * ret = (S3P_t *)malloc(sizeof(S3P_t));
    ret->address = address;
    ret->send_packet_func = send_packet_func;
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
    //printf("msg destination %08lX\n", msg->destination);
    //(msg_bytes[0] << 24) + (msg_bytes[1] << 16) + (msg_bytes[2] << 8) + msg_bytes[3];
    msg->source = UNPACK_STREAM_TO_32_BIT(msg_bytes,4);
    //printf("msg source %08lX\n", msg->source);
    //(msg_bytes[4] << 24) + (msg_bytes[5] << 16) + (msg_bytes[6] << 8) + msg_bytes[7];
    uint32_t read_command = (msg_bytes[8] << 16) + (msg_bytes[9] << 8) + msg_bytes[10]; 
    //read_command = read_command && 0x00FFFFFF; // remove higher byte which doesn't belong to command which is a 3 byte val
    
    msg->command = (S3P_command_t)read_command;
    //printf("msg command %02X\n", msg->command);
    msg->argument_length = msg_bytes[11];
    //printf("arg length %02X\n", msg->argument_length);
    //uint8_t k = 0;
    uint32_t j = HEADER_LENGTH;

    msg->argument = (uint32_t *)malloc(msg->argument_length*sizeof(uint32_t));
    uint8_t k = 0;
    for(k=0;k<msg->argument_length;k++){
        msg->argument[k] = UNPACK_STREAM_TO_32_BIT(msg_bytes,j);
        j=j+4;
    }
    //printf("************* After unpack:\n");
    S3P__print_frame(*msg);
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
    printf("read_address:%08lX\n", read_address);
    return (read_address == s3p->address)||(read_address == 0xFFFFFFFF);
}   


esp_err_t S3P__read_message(S3P_t * s3p, uint8_t * msg_bytes, uint16_t msg_length){
     S3P_msg_t * msg = (S3P_msg_t*)malloc(sizeof(S3P_msg_t));
     esp_err_t ret = S3P__unpack_message(msg, msg_bytes, msg_length);

    ret = S3P__interpret_command(s3p, msg);

     free(msg);
    return ret;  
}

esp_err_t S3P__interpret_command(S3P_t * s3p, S3P_msg_t* msg){
    switch(msg->command){
        case WSH: 
            printf("\nCommand WSH detected\n");
            S3P_msg_t msg = {
                .destination = 0x00000000,
                .source = s3p->address,
                .command = IAH,
                .argument_length = 0,
                .argument = NULL
            };
            uint32_t msg_length = 0;
            uint8_t * byte_out = (uint8_t*)malloc(S3P__get_message_length(msg));
            S3P__pack_message(msg, byte_out, &msg_length); 
            s3p->send_packet_func(byte_out,msg_length);
            break;
        default:
            printf("Not implemented yet or ignored\n");
    }

    return ESP_OK;
}

