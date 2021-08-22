/****************************************************************************
*
*                            Open Watcom Project
*
*    Portions Copyright (c) 1983-2002 Sybase, Inc. All Rights Reserved.
*
*  ========================================================================
*
*    This file contains Original Code and/or Modifications of Original
*    Code as defined in and that are subject to the Sybase Open Watcom
*    Public License version 1.0 (the 'License'). You may not use this file
*    except in compliance with the License. BY USING THIS FILE YOU AGREE TO
*    ALL TERMS AND CONDITIONS OF THE LICENSE. A copy of the License is
*    provided with the Original Code and Modifications, and is also
*    available at www.sybase.com/developer/opensource.
*
*    The Original Code and all software distributed under the License are
*    distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
*    EXPRESS OR IMPLIED, AND SYBASE AND ALL CONTRIBUTORS HEREBY DISCLAIM
*    ALL SUCH WARRANTIES, INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF
*    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR
*    NON-INFRINGEMENT. Please see the License for the specific language
*    governing rights and limitations under the License.
*
*  ========================================================================
*
* Description:  OS/2 Linear Executable (LE/LX) and
*               Windows VxD (LE) format structures.
*
****************************************************************************/


#ifndef _EXEFLAT_H
#define _EXEFLAT_H

#include <stdint.h>

#pragma pack(push, 1)

#define OSF_FLAT_RESERVED 20

typedef struct os2_flat_header {
    uint16_t     signature;
    uint8_t      byte_order;     /* the byte ordering of the .exe */
    uint8_t      word_order;     /* the word ordering of the .exe */
    uint32_t     level;          /* the exe format level */
    uint16_t     cpu_type;       /* the cpu type */
    uint16_t     os_type;        /* the operating system type */
    uint32_t     version;        /* .exe version */
    uint32_t     flags;          /* .exe flags */
    uint32_t     num_pages;      /* # of pages in .exe */
    uint32_t     start_obj;      /* starting object number */
    uint32_t     eip;            /* starting value of eip */
    uint32_t     stack_obj;      /* object # for stack pointer */
    uint32_t     esp;            /* starting value of esp */
    uint32_t     page_size;      /* .exe page size */
    union {
        uint32_t     last_page;      /* size of last page - LE */
        uint32_t     page_shift;     /* left shift for page offsets - LX */
    }               l;
    uint32_t     fixup_size;     /* fixup section size */
    uint32_t     fixup_cksum;    /* fixup section checksum */
    uint32_t     loader_size;    /* loader section size */
    uint32_t     loader_cksum;   /* loader section checksum */
    uint32_t     objtab_off;     /* object table offset */
    uint32_t     num_objects;    /* number of objects in .exe */
    uint32_t     objmap_off;     /* object page map offset */
    uint32_t     idmap_off;      /* iterated data map offset */
    uint32_t     rsrc_off;       /* offset of resource table */
    uint32_t     num_rsrcs;      /* number of resource entries */
    uint32_t     resname_off;    /* offset of resident names table */
    uint32_t     entry_off;      /* offset of entry table */
    uint32_t     moddir_off;     /* offset of module directives table */
    uint32_t     num_moddirs;    /* number of module directives */
    uint32_t     fixpage_off;    /* offset of fixup page table */
    uint32_t     fixrec_off;     /* offset of fixup record table */
    uint32_t     impmod_off;     /* offset of import module name table */
    uint32_t     num_impmods;    /* # of entries in import mod name tbl */
    uint32_t     impproc_off;    /* offset of import procedure name table */
    uint32_t     cksum_off;      /* offset of per-page checksum table */
    uint32_t     page_off;       /* offset of enumerated data pages */
    uint32_t     num_preload;    /* number of preload pages */
    uint32_t     nonres_off;     /* offset of non-resident names table */
    uint32_t     nonres_size;    /* size of non-resident names table */
    uint32_t     nonres_cksum;   /* non-resident name table checksum */
    uint32_t     autodata_obj;   /* object # of autodata segment */
    uint32_t     debug_off;      /* offset of the debugging information */
    uint32_t     debug_len;      /* length of the debugging info */
    uint32_t     num_inst_preload;   /* # of instance pages in preload sect*/
    uint32_t     num_inst_demand;    /*# instance pages in demand load sect*/
    uint32_t     heapsize;       /* size of heap - for 16-bit apps */
    uint32_t     stacksize;      /* size of stack OS/2 only */
    union {                         /* pad to 196 bytes. */
        uint8_t      reserved[ OSF_FLAT_RESERVED ];
        struct {
            uint8_t      reserved1[ 8 ];   /* +0xB0 */
            uint32_t     winresoff;        /* +0xB8 Windows VxD version info resource offset */
            uint32_t     winreslen;        /* +0xBC Windows VxD version info resource lenght */
            uint16_t     device_ID;        /* +0xC0 Windows VxD device ID */
            uint16_t     DDK_version;      /* +0xC2 Windows VxD DDK version (0x030A) */
        } vxd;
    } r;
} os2_flat_header;

#define OSF_DEF_PAGE_SIZE  4096
#define OSF_FLAT_SIGNATURE 0x454C        // 'LE'
#define OSF_FLAT_LX_SIGNATURE 0x584C     // 'LX'
#define OSF_386_BYTE_ORDER 0
#define OSF_386_WORD_ORDER 0
#define OSF_EXE_LEVEL      0

#define OSF_CPU_286        1
#define OSF_CPU_386        2
#define OSF_CPU_486        3

#define OSF_OS_LEVEL       1    // OS/2
#define OSF_WIN386_LEVEL   4    // Windows 386 (VxD)

/******************************************************************************
 *
 *    The flags field low order word goes as follows:
 *
 *  x x x x  x x x x      x x x x      x x x x
 *  |   |      | | |          | |        |   |
 *  |   |      | | |          | |        |   +-> single data flag
 *  |   |      | | |          | |        +-----> if DLL, per-process
 *  |   |      | | |          | |                initialization
 *  |   |      | | |          | +--------------> no internal fixups.
 *  |   |      | | |          +----------------> no external fixups.
 *  |   |      +-+-+---------------------------> PM compatibility flags
 *  |   +------------------------------------------> errors during link
 *  |                            (not executable)
 *  +----------------------------------------------> 1=DLL, 0=program file
 *
 *    The flags field high order word goes as follows:
 *
 *  x x x x  x x x x      x x x x      x x x x
 *    |                                    | |
 *    |                                    | +-> prot. mem. lib. mod
 *    |                                    +---> device driver.
 *    +--------------------------------------------> DLL per-proc termination
 *****************************************************************************/

#define OSF_SINGLE_DATA               0x0001
#define OSF_INIT_INSTANCE             0x0004
#define OSF_INTERNAL_FIXUPS_DONE      0x0010
#define OSF_EXTERNAL_FIXUPS_DONE      0x0020
#define OSF_NOT_PM_COMPATIBLE         0x0100
#define OSF_PM_COMPATIBLE             0x0200
#define OSF_PM_APP                    0x0300
#define OSF_LINK_ERROR                0x2000
#define OSF_IS_DLL                    0x8000
#define OSF_IS_PROT_DLL           0x00010000UL
#define OSF_DEVICE_DRIVER         0x00020000UL
#define OSF_PHYS_DEVICE           0x00020000UL
#define OSF_VIRT_DEVICE           0x00028000UL
#define OSF_TERM_INSTANCE         0x40000000UL

#define VXD_DEVICE_DRIVER_3x      0x00008020UL
#define VXD_DEVICE_DRIVER_STATIC  0x00028000UL
#define VXD_DEVICE_DRIVER_DYNAMIC 0x00038000UL

typedef struct object_record {
    uint32_t     size;       /* object virtual size */
    uint32_t     addr;       /* base virtual address */
    uint32_t     flags;
    uint32_t     mapidx;     /* page map index */
    uint32_t     mapsize;    /* number of entries in page map */
    uint32_t     reserved;
} object_record;

/******************************************************************************
 *
 *    The flags field low order word is used as follows:
 *
 *  x x x x  x x x x      x x x x      x x x x
 *  | | | |    | | |      | | | |      | | | |
 *  | | | |    | | |      | | | |      | | | +-> readable object
 *  | | | |    | | |      | | | |      | | +---> writeable object
 *  | | | |    | | |      | | | |      | +-----> executable object
 *  | | | |    | | |      | | | |      +-------> resource object
 *  | | | |    | | |      | | | +--------------> discardable object
 *  | | | |    | | |      | | +----------------> sharable object
 *  | | | |    | | |      | +------------------> object has preload pages
 *  | | | |    | | |      +--------------------> object has invalid pages
 *  | | | |    | | +---------------------------> permanent and swappable
 *  | | | |    | +-----------------------------> permanent and resident
 *  | | | |    +-------------------------------> perm. and long lockable
 *  | | | |
 *  | | | +----------------------------------------> 16:16 alias required
 *  | | +------------------------------------------> big/default bit setting
 *  | +--------------------------------------------> conforming for code
 *  +----------------------------------------------> object io privilege level
 *
 *    The flags field high order word is used as follows:
 *
 *  x x x x  x x x x      x x x x      x x x x
 *
 *****************************************************************************/

#define OBJ_READABLE        0x0001
#define OBJ_WRITEABLE       0x0002
#define OBJ_EXECUTABLE      0x0004
#define OBJ_RESOURCE        0x0008
#define OBJ_DISCARDABLE     0x0010
#define OBJ_SHARABLE        0x0020
#define OBJ_HAS_PRELOAD     0x0040
#define OBJ_HAS_INVALID     0x0080
#define OBJ_PERM_SWAPPABLE  0x0100  /* LE */
#define OBJ_HAS_ZERO_FILL   0x0100  /* LX */
#define OBJ_PERM_RESIDENT   0x0200
#define OBJ_PERM_CONTIGUOUS 0x0300  /* LX */
#define OBJ_PERM_LOCKABLE   0x0400
#define OBJ_ALIAS_REQUIRED  0x1000
#define OBJ_BIG             0x2000
#define OBJ_CONFORMING      0x4000
#define OBJ_IOPL            0x8000

typedef struct le_map_entry {  /* LE */
    uint8_t  page_num[3];    /* 24-bit page number in .exe file */
    uint8_t  flags;
} le_map_entry;

typedef struct lx_map_entry { /* LX */
    uint32_t     page_offset;        /* offset from Preload page start
                                            shifted by page_shift in hdr */
    uint16_t     data_size;          /* # bytes in this page */
    uint16_t     flags;
} lx_map_entry;

typedef union {
    le_map_entry    le;
    lx_map_entry    lx;
} map_entry;

#define PAGE_VALID      0
#define PAGE_ITERATED   1
#define PAGE_INVALID    2
#define PAGE_ZEROED     3
#define PAGE_RANGE      4


typedef struct flat_bundle_prefix {
    uint8_t      b32_cnt;
    uint8_t      b32_type;
    uint16_t     b32_obj;
} flat_bundle_prefix;

typedef struct flat_null_prefix {
    uint8_t      b32_cnt;
    uint8_t      b32_type;
} flat_null_prefix;

/* values for the b32_type field */
enum bundle_types {
    FLT_BNDL_EMPTY  = 0,
    FLT_BNDL_ENTRY16,
    FLT_BNDL_GATE16,
    FLT_BNDL_ENTRY32,
    FLT_BNDL_ENTRYFWD
};

typedef struct flat_bundle_entry32 {
    uint8_t      e32_flags;      /* flag bits are same as in OS/2 1.x */
    uint32_t     e32_offset;
} flat_bundle_entry32;

typedef struct flat_bundle_gate16 {
    uint8_t      e32_flags;      /* flag bits are same as in OS/2 1.x */
    uint16_t     offset;
    uint16_t     callgate;
} flat_bundle_gate16;

/*
 * other, unused bundle types are:
 */

typedef struct flat_bundle_entry16 {
    uint8_t      e32_flags;      /* flag bits are same as in OS/2 1.x */
    uint16_t     e32_offset;
} flat_bundle_entry16;

typedef struct flat_bundle_entryfwd {
    uint8_t      e32_flags;      /* flag bits are same as in OS/2 1.x */
    uint16_t     modord;
    uint32_t     value;
} flat_bundle_entryfwd;

typedef struct flat_res_table {
    uint16_t    type_id;
    uint16_t    name_id;
    uint32_t    res_size;
    uint16_t    object;
    uint32_t    offset;
} flat_res_table;

#pragma pack(pop)

/* fixup record flags */

#define OSF_SOURCE_LIST     0x20
#define OSF_INTERNAL_REF    0x00
#define OSF_IMP_ORD_REF     0x01
#define OSF_IMP_NAME_REF    0x02
#define OSF_INT_ENT_REF     0x03
#define OSF_ADD_FIX         0x04
#define OSF_TARGET_OFF      0x10
#define OSF_ADD_FIX_32      0x20
#define OSF_OBJ_ORD         0x40
#define OSF_ORD_FLAG        0x80

#endif
