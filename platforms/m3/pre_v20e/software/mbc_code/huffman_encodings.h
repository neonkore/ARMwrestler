#ifndef HUFFMAN_ENCODINGS_H
#define HUFFMAN_ENCODINGS_H

#include <stdint.h>

uint16_t light_diff_codes[67]={18,463,19,502,20,25,8,22,248,500,21,460,461,53,27,249,23,30,62,105,501,30,2,16,127,58,51,2,1,24,5,30,6,7,56,14,59,114,503,17,24,104,14,31,13,12,63,100,462,101,126,13,9};
uint8_t light_code_lengths[67]={8,9,8,9,8,6,7,8,8,9,8,9,9,7,6,8,8,6,7,8,9,8,2,6,7,6,6,4,5,5,4,5,5,5,6,5,6,7,9,6,6,8,7,8,7,7,7,7,9,7,7,4,5};

uint8_t temp_diff_codes[5]={4,1,1,3,5};
uint8_t temp_code_lengths[5]={4,3,1,3,4};

#endif