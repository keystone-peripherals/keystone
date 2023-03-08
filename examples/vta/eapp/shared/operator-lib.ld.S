#include "interface.h"

SECTIONS {
    . = OPERATOR_LIB_ADDR;
    .keystone : {
        *(.keystone)
    }
}
INSERT BEFORE .text;