/*
  Copyright (c) 1990-2009 Info-ZIP.  All rights reserved.

  See the accompanying file LICENSE, version 2009-Jan-02 or later
  (the contents of which are also included in unzip.h) for terms of use.
  If, for some reason, all these files are missing, the Info-ZIP license
  also may be found at:  ftp://ftp.info-zip.org/pub/infozip/license.html
*/
/*---------------------------------------------------------------------------

  globals.h

  There is usually no need to include this file since unzip.h includes it.

  This header file is used by all of the UnZip source files.  It contains
  a struct definition that is used to "house" all of the global variables.
  This is done to allow for multithreaded environments (OS/2, NT, Win95,
  Unix) to call UnZip through an API without a semaphore.  REENTRANT should
  be defined for all platforms that require this.

  GLOBAL CONSTRUCTOR AND DESTRUCTOR (API WRITERS READ THIS!!!)
  ------------------------------------------------------------

  No, it's not C++, but it's as close as we can get with K&R.

  The main() of each process that uses these globals must include the
  CONSTRUCTGLOBALS; statement.  This will malloc enough memory for the
  structure and initialize any variables that require it.  This must
  also be done by any API function that jumps into the middle of the
  code.

  The DESTROYGLOBALS(); statement should be inserted before EVERY "EXIT(n)".
  Naturally, it also needs to be put before any API returns as well.
  In fact, it's much more important in API functions since the process
  will NOT end, and therefore the memory WON'T automatically be freed
  by the operating system.

  USING VARIABLES FROM THE STRUCTURE
  ----------------------------------

  All global variables must now be prefixed with `G.' which is either a
  global struct (in which case it should be the only global variable) or
  a macro for the value of a local pointer variable that is passed from
  function to function.  Yes, this is a pain.  But it's the only way to
  allow full reentrancy.

  ADDING VARIABLES TO THE STRUCTURE
  ---------------------------------

  If you make the inclusion of any variables conditional, be sure to only
  check macros that are GUARANTEED to be included in every module.
  For instance, newzip and pwdarg are needed only if CRYPT is TRUE,
  but this is defined after unzip.h has been read.  If you are not careful,
  some modules will expect your variable to be part of this struct while
  others won't.  This will cause BIG problems. (Inexplicable crashes at
  strange times, car fires, etc.)  When in doubt, always include it!

  Note also that UnZipSFX needs a few variables that UnZip doesn't.  However,
  it also includes some object files from UnZip.  If we were to conditionally
  include the extra variables that UnZipSFX needs, the object files from
  UnZip would not mesh with the UnZipSFX object files.  Result: we just
  include the UnZipSFX variables every time.  (It's only an extra 4 bytes
  so who cares!)

  ADDING FUNCTIONS
  ----------------

  To support this new global struct, all functions must now conditionally
  pass the globals pointer (pG) to each other.  This is supported by 5 macros:
  __GPRO, __GPRO__, __G, __G__ and __GDEF.  A function that needs no other
  parameters would look like this:

    int extract_or_test_files(__G)
      __GDEF
    {
       ... stuff ...
    }

  A function with other parameters would look like:

    int memextract(__G__ tgt, tgtsize, src, srcsize)
        __GDEF
        uch *tgt, *src;
        ulg tgtsize, srcsize;
    {
      ... stuff ...
    }

  In the Function Prototypes section of unzpriv.h, you should use __GPRO and
  __GPRO__ instead:

    int  uz_opts                   OF((__GPRO__ int *pargc, char ***pargv));
    int  process_zipfiles          OF((__GPRO));

  Note that there is NO comma after __G__ or __GPRO__ and no semi-colon after
  __GDEF.  I wish there was another way but I don't think there is.


  TESTING THE CODE
  -----------------

  Whether your platform requires reentrancy or not, you should always try
  building with REENTRANT defined if any functions have been added.  It is
  pretty easy to forget a __G__ or a __GDEF and this mistake will only show
  up if REENTRANT is defined.  All platforms should run with REENTRANT
  defined.  Platforms that can't take advantage of it will just be paying
  a performance penalty needlessly.

  SIGNAL MADNESS
  --------------

  This whole pointer passing scheme falls apart when it comes to SIGNALs.
  I handle this situation 2 ways right now.  If you define USETHREADID,
  UnZip will include a 64-entry table.  Each entry can hold a global
  pointer and thread ID for one thread.  This should allow up to 64
  threads to access UnZip simultaneously.  Calling DESTROYGLOBALS()
  will free the global struct and zero the table entry.  If somebody
  forgets to call DESTROYGLOBALS(), this table will eventually fill up
  and UnZip will exit with an error message.  A good way to test your
  code to make sure you didn't forget a DESTROYGLOBALS() is to change
  THREADID_ENTRIES to 3 or 4 in globals.c, making the table real small.
  Then make a small test program that calls your API a dozen times.

  Those platforms that don't have threads still need to be able to compile
  with REENTRANT defined to test and see if new code is correctly written
  to work either way.  For these platforms, I simply keep a global pointer
  called GG that points to the Globals structure.  Good enough for testing.

  I believe that NT has thread level storage.  This could probably be used
  to store a global pointer for the sake of the signal handler more cleanly
  than my table approach.

  ---------------------------------------------------------------------------*/

#ifndef __globals_h
#define __globals_h

#include "bzlib.h"

/*************/
/*  Globals  */
/*************/

typedef struct Globals {

    /* command options of general use */
    UzpOpts UzO; /* command options of general use */

    /* command options specific to the high level command line interface */

    /* internal flags and general globals */
    int noargs;          /* did true command line have *any* arguments? */
    unsigned filespecs;  /* number of real file specifications to be matched */
    unsigned xfilespecs; /* number of excluded filespecs to be matched */
    int process_all_files;
    int overwrite_mode; /* 0 - query, 1 - always, 2 - never */
    int create_dirs;    /* used by main(), mapname(), checkdir() */
    int extract_flag;
    int newzip; /* reset in extract.c; used in crypt.c */
    zoff_t real_ecrec_offset;
    zoff_t expect_ecrec_offset;
    zoff_t csize;      /* used by decompr. (NEXTBYTE): must be signed */
    zoff_t used_csize; /* used by extract_or_test_member(), explode() */

    char **pfnames;
    char **pxnames;
    char sig[4];
    char answerbuf[10];
    min_info info[DIR_BLKSIZ];
    min_info *pInfo;
    union work area; /* see unzpriv.h for definition of work */

    ZCONST ulg near *crc_32_tab;
    ulg crc32val; /* CRC shift reg. (was static in funzip) */

    uch *inbuf; /* input buffer (any size is OK) */
    uch *inptr; /* pointer into input buffer */
    int incnt;

    ulg bitbuf;
    int bits_left; /* unreduce and unshrink only */
    int zipeof;
    char *argv0; /* used for NT and EXE_EXTENSION */
    char *wildzipfn;
    char *zipfn; /* GRR:  WINDLL:  must nuke any malloc'd zipfn... */
    int zipfd;   /* zipfile file handle */
    zoff_t ziplen;
    zoff_t cur_zipfile_bufstart; /* extract_or_test, readbuf, ReadByte */
    zoff_t extra_bytes;          /* used in unzip.c, misc.c */
    uch *extra_field;            /* Unix, VMS, Mac, OS/2, Acorn, ... */
    uch *hold;

    local_file_hdr lrec; /* used in unzip.c, extract.c */
    cdir_file_hdr crec;  /* used in unzip.c, extract.c, misc.c */
    ecdir_rec ecrec;     /* used in unzip.c, extract.c */
    z_stat statbuf;      /* used by main, mapname, check_for_newer */

    int zip64; /* true if Zip64 info in extra field */

    int mem_mode;
    uch *outbufptr;         /* extract.c static */
    ulg outsize;            /* extract.c static */
    int reported_backslash; /* extract.c static */
    int disk_full;
    int newfile;
    void **cover; /* used in extract.c for bomb detection */

    int didCRlast; /* fileio static */
    ulg numlines;  /* fileio static: number of lines printed */
    int sol;       /* fileio static: at start of line */
    int no_ecrec;  /* process static */
    int symlnk;
    slinkentry *slink_head; /* pointer to head of symlinks list */
    slinkentry *slink_last; /* pointer to last entry in symlinks list */

    FILE *outfile;
    uch *outbuf;
    uch *realbuf;

    uch *outbuf2; /*  process_zipfiles() (never changes); */
    uch *outptr;
    ulg outcnt;               /* number of chars stored in outbuf */
    char filename[FILNAMSIZ]; /* also used by NT for temporary SFX path */
    char *filename_full;      /* the full path so Unicode checks work */
    extent fnfull_bufsize;    /* size of allocated filename buffer */
    int unicode_escape_all;
    int unicode_mismatch;
    int native_is_utf8; /* bool, TRUE => native charset == UTF-8 */

    int unipath_version;    /* version of Unicode field */
    ulg unipath_checksum;   /* Unicode field checksum */
    char *unipath_filename; /* UTF-8 path */

    char *key;       /* crypt static: decryption password or NULL */
    int nopwd;       /* crypt static */
    z_uint4 keys[3]; /* crypt static: keys defining pseudo-random sequence */

    int echofd; /* ttyio static: file descriptor whose echo is off */

    unsigned hufts; /* track memory usage */

    struct huft *fixed_tl;           /* inflate static */
    struct huft *fixed_td;           /* inflate static */
    unsigned fixed_bl, fixed_bd;     /* inflate static */
    struct huft *fixed_tl64;         /* inflate static */
    struct huft *fixed_td64;         /* inflate static */
    unsigned fixed_bl64, fixed_bd64; /* inflate static */
    struct huft *fixed_tl32;         /* inflate static */
    struct huft *fixed_td32;         /* inflate static */
    unsigned fixed_bl32, fixed_bd32; /* inflate static */
    ZCONST ush *cplens;              /* inflate static */
    ZCONST uch *cplext;              /* inflate static */
    ZCONST uch *cpdext;              /* inflate static */
    unsigned wp; /* inflate static: current position in slide */
    ulg bb;      /* inflate static: bit buffer */
    unsigned bk; /* inflate static: bits count in bit buffer */

    /* cylindric buffer space for formatting zoff_t values (fileio static) */
    char fzofft_buf[FZOFFT_NUM][FZOFFT_LEN];
    int fzofft_index;

    MsgFn *message;
    InputFn *input;
    PauseFn *mpause;
    PasswdFn *decr_passwd;
    StatCBFn *statreportcb;

    int incnt_leftover; /* so improved NEXTBYTE does not waste input */
    uch *inptr_leftover;

#ifdef SYSTEM_SPECIFIC_GLOBALS
    SYSTEM_SPECIFIC_GLOBALS
#endif

} Uz_Globs; /* end of struct Globals */

/***************************************************************************/

#define CRC_32_TAB G.crc_32_tab

Uz_Globs *globalsCtor OF((void) );

/* pseudo constant sigs; they are initialized at runtime so unzip executable
 * won't look like a zipfile
 */
extern char local_hdr_sig[4];
extern char central_hdr_sig[4];
extern char end_central_sig[4];
extern char end_central32_sig[4];
extern char end_central64_sig[4];
extern char end_centloc64_sig[4];
/* extern char extd_local_sig[4];  NOT USED YET */

extern Uz_Globs G;
#define __G
#define __G__
#define __GPRO void
#define __GPRO__
#define __GDEF
#define GETGLOBALS()
#define CONSTRUCTGLOBALS() globalsCtor()
#define DESTROYGLOBALS()

#define uO G.UzO

#endif /* __globals_h */
