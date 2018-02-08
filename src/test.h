#pragma once

#include "log.h"

struct message_error_test {
    struct message base;
    size_t ind;
};

size_t message_error_test(char *, size_t, struct message_error_test *);
size_t message_info_test(char *, size_t, struct message *);

#define MESSAGE_ERROR_TEST(Ind) \
    ((struct message_error_test) { \
        .base = MESSAGE(message_error_test, MESSAGE_TYPE_INFO), \
        .ind = (Ind) \
    })

#define MESSAGE_INFO_TEST() \
    MESSAGE(message_info_test, MESSAGE_TYPE_INFO) \

union message_test {
    struct message_error_test error;
    struct message info;
};

union message_error_internal_test {
    struct message_error_crt error_crt;
};


bool test(struct log *);