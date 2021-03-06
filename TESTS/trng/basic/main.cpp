/*
* Copyright (c) 2018 ARM Limited. All rights reserved.
* SPDX-License-Identifier: Apache-2.0
* Licensed under the Apache License, Version 2.0 (the License); you may
* not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an AS IS BASIS, WITHOUT
* WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

/*
* The test is based on the assumption that trng will generate random data, random so
* there will not be any similar patterns in it, that kind of data will be impossible to
* compress, if compression will occur the test will result in failure.
*
* The test is composed out of three parts:
* the first, generate a trng buffer and try to compress it, at the end of first part
* we will reset the device.
* In second part we will generate a trng buffer with a different buffer size and try to
* compress it.
* In the third part we will again generate a trng buffer to see that the same trng output
* is not generated as the stored trng buffer from part one (before reseting), the new trng data will
* be concatenated to the trng data from the first part and then try to compress it
* together, if there are similar patterns the compression will succeed.
*
* We need to store and load the first part data before and after reset, the mechanism
* we chose is NVstore, mainly because its simplicity and the fact it is not platform
* dependent, in case a specific board does not support NVstore we will use the
* mbed greentea platform for sending and receiving the data from the device to the
* host running the test and back, the problem with this mechanism is that it doesn't handle
* well certain characters, especially non ASCII ones, so we used the base64 algorithm
* to ensure all characters will be transmitted correctly.
*/

#include "greentea-client/test_env.h"
#include "unity/unity.h"
#include "utest/utest.h"
#include "hal/trng_api.h"
#include "base64b.h"
#include "nvstore.h"
#include "pithy.h"
#include <stdio.h>

#if !DEVICE_TRNG
#error [NOT_SUPPORTED] TRNG API not supported for this target
#endif

#define MSG_VALUE_DUMMY                 "0"
#define MSG_VALUE_LEN                   64
#define MSG_KEY_LEN                     32

#define BUFFER_LEN                      (MSG_VALUE_LEN/2)           //size of first step data, and half of the second step data

#define MSG_TRNG_READY                  "ready"
#define MSG_TRNG_BUFFER                 "buffer"

#define MSG_TRNG_TEST_STEP1             "check_step1"
#define MSG_TRNG_TEST_STEP2             "check_step2"
#define MSG_TRNG_TEST_SUITE_ENDED       "Test_suite_ended"

#define NVKEY                           1                           //NVstore key for storing and loading data

/*there are some issues with nvstore and greentea reset, so for now nvstore is disabled,
 *When  solved delete current define and replace all NVSTORE_RESET with NVSTORE_ENABLED*/
#define NVSTORE_RESET                   (NVSTORE_ENABLED & 0)

using namespace utest::v1;

static int fill_buffer_trng(uint8_t *buffer, trng_t *trng_obj, size_t trng_len)
{
    size_t temp_size = 0, output_length = 0;
    int trng_res = 0;
    uint8_t *temp_in_buf = buffer;

    trng_init(trng_obj);
    memset(buffer, 0, BUFFER_LEN);

    while (true) {
        trng_res = trng_get_bytes(trng_obj, temp_in_buf, trng_len, &output_length);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, trng_res, "trng_get_bytes error!");
        temp_size += output_length;
        temp_in_buf += output_length;
        trng_len -= output_length;
        if (temp_size >= trng_len) {
            break;
        }
    }

    temp_in_buf = NULL;
    trng_free(trng_obj);
    return 0;
}

static void compress_and_compare(char *key, char *value)
{
    trng_t trng_obj;
    uint8_t out_comp_buf[BUFFER_LEN * 4] = {0}, buffer[BUFFER_LEN] = {0};
    uint8_t input_buf[BUFFER_LEN * 2] = {0}, temp_buff[BUFFER_LEN * 2] = {0};
    size_t comp_sz = 0;
    unsigned int result = 0;

#if NVSTORE_RESET
    NVStore& nvstore = NVStore::get_instance();
#endif

    /*At the begining of step 2 load trng buffer from step 1*/
    if (strcmp(key, MSG_TRNG_TEST_STEP2) == 0) {
#if NVSTORE_RESET
        uint16_t actual = 0;
        result = nvstore.get(NVKEY, sizeof(buffer), buffer, actual);
        TEST_ASSERT_EQUAL(NVSTORE_SUCCESS, result);
#else
        /*Using base64 to decode data sent from host*/
        uint32_t lengthWritten = 0;
        uint32_t charsProcessed = 0;
        result = esfs_DecodeNBase64((const char *)value, MSG_VALUE_LEN, buffer, BUFFER_LEN, &lengthWritten, &charsProcessed);
        TEST_ASSERT_EQUAL(0, result);
#endif
        memcpy(input_buf, buffer, BUFFER_LEN);
    }

    /*Fill buffer with trng values*/
    result = fill_buffer_trng(buffer, &trng_obj, sizeof(buffer));
    TEST_ASSERT_EQUAL(0, result);

    /*pithy_Compress will try to compress the random data, if it succeeded it means the data is not really random*/
    if (strcmp(key, MSG_TRNG_TEST_STEP1) == 0) {
        printf("\n******TRNG_TEST_STEP1*****\n");

        comp_sz = pithy_Compress((char *)buffer, sizeof(buffer), (char *)out_comp_buf, sizeof(out_comp_buf), 9);

        if (comp_sz >= BUFFER_LEN) {
            printf("trng_get_bytes for buffer size %d was successful", sizeof(buffer));
        } else {
            printf("trng_get_bytes for buffer size %d was unsuccessful", sizeof(buffer));
            TEST_ASSERT(false);
        }
        printf("\n******FINISHED_TRNG_TEST_STEP1*****\n\n");

    } else if (strcmp(key, MSG_TRNG_TEST_STEP2) == 0) {
        /*pithy_Compress will try to compress the random data with a different buffer sizem*/
        printf("\n******TRNG_TEST_STEP2*****\n");
        result = fill_buffer_trng(temp_buff, &trng_obj, sizeof(temp_buff));
        TEST_ASSERT_EQUAL(0, result);

        comp_sz = pithy_Compress((char *)temp_buff, sizeof(temp_buff), (char *)out_comp_buf, sizeof(out_comp_buf), 9);

        if (comp_sz >= BUFFER_LEN) {
            printf("trng_get_bytes for buffer size %d was successful", sizeof(temp_buff));
        } else {
            printf("trng_get_bytes for buffer size %d was unsuccessful", sizeof(temp_buff));
            TEST_ASSERT(false);
        }
        printf("\n******FINISHED_TRNG_TEST_STEP2*****\n\n");

        printf("******TRNG_TEST_STEP3*****\n");

        /*pithy_Compress will try to compress the random data from before reset concatenated with new random data*/
        comp_sz = pithy_Compress((char *)input_buf, sizeof(input_buf), (char *)out_comp_buf, sizeof(out_comp_buf), 9);

        if (comp_sz >= BUFFER_LEN) {
            printf("compression for concatenated buffer after reset was successful");
        } else {
            printf("compression for concatenated buffer after reset was unsuccessful");
            TEST_ASSERT(false);
        }
        printf("\n******FINISHED_TRNG_TEST_STEP3*****\n\n");
    }

    /*At the end of step 1 store trng buffer and reset the device*/
    if (strcmp(key, MSG_TRNG_TEST_STEP1) == 0) {
        int result = 0;
#if NVSTORE_RESET
        result = nvstore.set(NVKEY, sizeof(buffer), buffer);
        TEST_ASSERT_EQUAL(NVSTORE_SUCCESS, result);
#else
        /*Using base64 to encode data sending from host*/
        result = esfs_EncodeBase64(buffer, BUFFER_LEN, (char *)out_comp_buf, sizeof(out_comp_buf));
        TEST_ASSERT_EQUAL(NVSTORE_SUCCESS, result);
        greentea_send_kv(MSG_TRNG_BUFFER, (const char *)out_comp_buf);
#endif
        system_reset();
        TEST_ASSERT_MESSAGE(false, "system_reset() did not reset the device as expected.");
    }

    return;
}

/*This method call first and second steps, it directs by the key received from the host*/
void trng_test()
{
    greentea_send_kv(MSG_TRNG_READY, MSG_VALUE_DUMMY);

    static char key[MSG_KEY_LEN + 1] = { };
    static char value[MSG_VALUE_LEN + 1] = { };
    memset(key, 0, MSG_KEY_LEN + 1);
    memset(value, 0, MSG_VALUE_LEN + 1);

    greentea_parse_kv(key, value, MSG_KEY_LEN, MSG_VALUE_LEN);

    if (strcmp(key, MSG_TRNG_TEST_STEP1) == 0) {
        /*create trng data buffer and try to compress it, store it for later checks*/
        compress_and_compare(key, value);
        return trng_test();
    }

    if (strcmp(key, MSG_TRNG_TEST_STEP2) == 0) {
        /*create another trng data buffer and concatenate it to the stored trng data buffer
        try to compress them both*/
        compress_and_compare(key, value);
    }
}

utest::v1::status_t greentea_failure_handler(const Case *const source, const failure_t reason)
{
    greentea_case_failure_abort_handler(source, reason);
    return STATUS_CONTINUE;
}

Case cases[] = {
    Case("TRNG: trng_test", trng_test, greentea_failure_handler),
};

utest::v1::status_t greentea_test_setup(const size_t number_of_cases)
{
    GREENTEA_SETUP(100, "trng_reset");
    return greentea_test_setup_handler(number_of_cases);
}

Specification specification(greentea_test_setup, cases, greentea_test_teardown_handler);

int main()
{
    bool ret = !Harness::run(specification);
    greentea_send_kv(MSG_TRNG_TEST_SUITE_ENDED, MSG_VALUE_DUMMY);

    return ret;
}
