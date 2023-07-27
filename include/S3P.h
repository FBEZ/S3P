/**
 * @file S3P.h
 * @author Francesco Bez (francesco.bez@protonmail.com)
 * @brief Simple synchronous sensor protol
 * @version 0.1
 * @date 2023-07-24
 * 
 * @copyright Copyright (c) 2023
 * 
 */

#ifndef S3P_H
#define S3P_H

#include <stdio.h>
#include "esp_err.h"
#include "ethernet_utils.h"

typedef enum{
    WSH, // Who's here
    IAH, // I am here
    SAM, // Set as master
    SAS, // Set as slave
    STO, // Set time overwrite
    GTA, // get time from all
    RTA,  // reply time from all
    ACK, // acknoledgment
    SYN, // synch 
    DRQ, // delay request
    DRE  //delay response
}S3P_command_t;


typedef struct {
    uint32_t destination;
    uint32_t source;
    S3P_command_t command;
    uint8_t argument_length;
    uint32_t * argument;
}S3P_msg_t;

typedef struct S3P_t S3P_t;


S3P_t * S3P__create(uint32_t address,
                    esp_err_t (*send_packet_func)(uint8_t *, uint16_t));

//esp_err_t S3P_filter_message(S3P_t s3p, uint8_t* msg);

/**
 * @brief Pack a S3P_msg type into a byte stream to be sent
 * 
 * @param msg 
 * @param byte_out the data stream
 * @param byte_out_length return pointer to variable total length of the packet 
 * @return esp_err_t 
 */
esp_err_t S3P__pack_message(S3P_msg_t msg, uint8_t * byte_out, uint32_t * byte_out_length);


esp_err_t S3P__unpack_message(S3P_msg_t * msg, uint8_t * msg_bytes, uint16_t msg_length);


bool S3P__filter_message(S3P_t * s3p, uint8_t * msg_bytes);

uint32_t S3P__get_message_length(S3P_msg_t msg);

esp_err_t S3P__read_message(S3P_t * s3p, uint8_t * msg_bytes, uint16_t msg_length);

/**
 * @brief Synch function to be called every second (if the node is master
 * )
 * 
 * @param s3p 
 * @return esp_err_t 
 */
esp_err_t S3P__send_synch(S3P_t * s3p);

#endif /*S3P_H*/