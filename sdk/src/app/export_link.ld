SECTIONS {
    /* text section should be first, so we can just jump
       to the start of the executable. ensure that the start
       of the text section is on the first page */
    .text : {
        ASSERT(. < 0x1000, "Too much before .text");
        *(.text*)
    }

    /* make sure that bss sections are properly instantiated
       in the stripped binary (otherwise, they would be lazily
       loaded which does not work for us) */
    .bss : {
        *(.bss*)
        LONG(0)
    }

    .sbss : { 
        *(.sbss*)
        LONG(0)
    }

    .tbss : {
        *(.tbss*)
        LONG(0)
    }

    /* thread-local sections must be consecutive */
    .tdata : {
        *(.tdata*)
    }
}
INSERT BEFORE .hash;


/* This is necessary because of the following line in the default linker script:

    . = DATA_SEGMENT_ALIGN (CONSTANT (MAXPAGESIZE), CONSTANT (COMMONPAGESIZE));

    This causes issues where (offset in the ELF file) != VA in the export func,
    with disastrous consequences (often causing alignment issues for following
    sections). So, we manually round here to make the above statement ineffective.
 */
SECTIONS {
    . = ALIGN(0x1000);
}
INSERT AFTER .gcc_except_table;
