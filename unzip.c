/*
  Copyright (c) 1990-2009 Info-ZIP.  All rights reserved.

  See the accompanying file LICENSE, version 2009-Jan-02 or later
  (the contents of which are also included in unzip.h) for terms of use.
  If, for some reason, all these files are missing, the Info-ZIP license
  also may be found at:  ftp://ftp.info-zip.org/pub/infozip/license.html
*/
/*---------------------------------------------------------------------------

  unzip.c

  UnZip - a zipfile extraction utility.  See below for make instructions, or
  read the comments in Makefile and the various Contents files for more de-
  tailed explanations.  To report a bug, submit a *complete* description via
  //www.info-zip.org/zip-bug.html; include machine type, operating system and
  version, compiler and version, and reasonably detailed error messages or
  problem report.  To join Info-ZIP, see the instructions in README.

  UnZip 5.x is a greatly expanded and partially rewritten successor to 4.x,
  which in turn was almost a complete rewrite of version 3.x.  For a detailed
  revision history, see UnzpHist.zip at quest.jpl.nasa.gov.  For a list of
  the many (near infinite) contributors, see "CONTRIBS" in the UnZip source
  distribution.

  UnZip 6.0 adds support for archives larger than 4 GiB using the Zip64
  extensions as well as support for Unicode information embedded per the
  latest zip standard additions.

  ---------------------------------------------------------------------------

  [from original zipinfo.c]

  This program reads great gobs of totally nifty information, including the
  central directory stuff, from ZIP archives ("zipfiles" for short).  It
  started as just a testbed for fooling with zipfiles, but at this point it
  is actually a useful utility.  It also became the basis for the rewrite of
  UnZip (3.16 -> 4.0), using the central directory for processing rather than
  the individual (local) file headers.

  Another dandy product from your buddies at Newtware!

  Author:  Greg Roelofs, newt@pobox.com, http://pobox.com/~newt/
           23 August 1990 -> April 1997

  ---------------------------------------------------------------------------

  Version:  unzip5??.{tar.Z | tar.gz | zip} for Unix, VMS, OS/2, MS-DOS, Amiga,
              Atari, Windows 3.x/95/NT/CE, Macintosh, Human68K, Acorn RISC OS,
              AtheOS, BeOS, SMS/QDOS, VM/CMS, MVS, AOS/VS, Tandem NSK, Theos
              and TOPS-20.

  Copyrights:  see accompanying file "LICENSE" in UnZip source distribution.
               (This software is free but NOT IN THE PUBLIC DOmain.)

  ---------------------------------------------------------------------------*/

#include <langinfo.h>

#include "unzip.h" /* includes, typedefs, macros, prototypes, etc. */
#include "crypt.h"

/*******************/
/* Local Functions */
/*******************/

static void help_extended(void);

/*************/
/* Constants */
/*************/

#include "consts.h" /* all constant global variables are in here */
                    /* (non-constant globals were moved to globals.c) */

/* constant local variables: */

static const char EnvUnZip[] = ENV_UNZIP;
static const char EnvUnZip2[] = ENV_UNZIP2;
static const char NoMemEnvArguments[] =
    "envargs:  cannot get memory for arguments";
static const char CmdLineParamTooLong[] =
    "error:  command line parameter #%d exceeds internal size limit\n";

static const char NotExtracting[] = "caution:  not extracting; -d ignored\n";
static const char MustGiveExdir[] =
    "error:  must specify directory to which to extract with -d option\n";
static const char OnlyOneExdir[] =
    "error:  -d option used more than once (only one exdir allowed)\n";

static const char MustGivePasswd[] =
    "error:  must give decryption password with -P option\n";

static const char InvalidOptionsMsg[] = "error:\
  -fn or any combination of -c, -l, -p, -t, -u and -v options invalid\n";
static const char IgnoreOOptionMsg[] =
    "caution:  both -n and -o specified; ignoring -o\n";

/* usage() strings */
static const char Example3[] = "ReadMe";
static const char Example2[] = " \
 unzip -p foo | more  => send contents of foo.zip via pipe into program more\n";

/* local1[]:  command options */
static const char local1[] = "  -T  timestamp archive to latest";

/* local2[] and local3[]:  modifier options */
static const char local2[] = " -X  restore UID/GID info";
static const char local3[] = "\
  -K  keep setuid/setgid/tacky permissions\n";

static const char UnzipUsageLine2[] = "\
Usage: unzip %s[-opts[modifiers]] file[.zip] [list] [-x xlist] [-d exdir]\n \
 Default action is to extract files in list, except those in xlist, to exdir;\n\
  file[.zip] may be a wildcard.  %s\n";

#define ZIPINFO_MODE_OPTION ""
static const char ZipInfoMode[] = "(ZipInfo mode is disabled in this version.)";

static const char UnzipUsageLine3[] = "\n\
  -p  extract files to pipe, no messages     -l  list files (short format)\n\
  -f  freshen existing files, create none    -t  test compressed archive data\n\
  -u  update files, create if necessary      -z  display archive comment only\n\
  -v  list verbosely/show version info     %s\n\
  -x  exclude files that follow (in xlist)   -d  extract files into exdir\n";

/* There is not enough space on a standard 80x25 Windows console screen for
 * the additional line advertising the UTF-8 debugging options. This may
 * eventually also be the case for other ports. Probably, the -U option need
 * not be shown on the introductory screen at all. [Chr. Spieler, 2008-02-09]
 *
 * Likely, other advanced options should be moved to an extended help page and
 * the option to list that page put here.  [E. Gordon, 2008-3-16]
 */
static const char UnzipUsageLine4[] = "\
modifiers:\n\
  -n  never overwrite existing files         -q  quiet mode (-qq => quieter)\n\
  -o  overwrite files WITHOUT prompting      -a  auto-convert any text files\n\
  -j  junk paths (do not make directories)   -aa treat ALL files as text\n\
  -U  use escapes for all non-ASCII Unicode  -UU ignore any Unicode fields\n\
  -C  match filenames case-insensitively     -L  make (some) names \
lowercase\n %-42s  -V  retain VMS version numbers\n%s";

static const char UnzipUsageLine5[] = "\
See \"unzip -hh\" or unzip.txt for more help.  Examples:\n\
  unzip data1 -x joe   => extract all files except joe from zipfile data1.zip\n\
%s\
  unzip -fo foo %-6s => quietly replace existing %s if archive file newer\n";

/* initialization of sigs is completed at runtime */
char central_hdr_sig[4] = {0, 0, 0x01, 0x02};
char local_hdr_sig[4] = {0, 0, 0x03, 0x04};
char end_central_sig[4] = {0, 0, 0x05, 0x06};
char end_central64_sig[4] = {0, 0, 0x06, 0x06};
char end_centloc64_sig[4] = {0, 0, 0x06, 0x07};

static const char *default_fnames[2] = {"*",
                                        NULL}; /* default filenames vector */

Uz_Globs G;

static void init_globals()
{
    /* for REENTRANT version, G is defined as (*pG) */

    memzero(&G, sizeof(Uz_Globs));

    uO.lflag = (-1);
    G.wildzipfn = "";
    G.pfnames = (char **) default_fnames;
    G.pxnames = (char **) &default_fnames[1];
    G.pInfo = G.info;
    G.sol = TRUE; /* at start of line */

    G.message = UzpMessagePrnt;
    G.input = UzpInput; /* not used by anyone at the moment... */
    G.mpause = UzpMorePause;
    G.decr_passwd = UzpPassword;

    G.echofd = -1;
}

/*****************************/
/*  main() / UzpMain() stub  */
/*****************************/

int main(argc, argv) /* return PK-type error code (except under VMS) */
int argc;
char *argv[];
{
    init_globals();
    return unzip(argc, argv);
}

/*******************************/
/*  Primary UnZip entry point  */
/*******************************/

int unzip(argc, argv)
int argc;
char *argv[];
{
    int i;
    int retcode, error = FALSE;
#define SET_SIGHANDLER(sigtype, newsighandler) \
    signal((sigtype), (newsighandler))

    /* initialize international char support to the current environment */
    setlocale(LC_CTYPE, "");

    /* see if can use UTF-8 Unicode locale */
    {
        char *codeset;
        /* get the codeset (character set encoding) currently used */

        codeset = nl_langinfo(CODESET);
        /* is the current codeset UTF-8 ? */
        if ((codeset != NULL) && (strcmp(codeset, "UTF-8") == 0)) {
            /* successfully found UTF-8 char coding */
            G.native_is_utf8 = TRUE;
        } else {
            /* Current codeset is not UTF-8 or cannot be determined. */
            G.native_is_utf8 = FALSE;
        }
        /* Note: At least for UnZip, trying to change the process codeset to
         *       UTF-8 does not work.  For the example Linux setup of the
         *       UnZip maintainer, a successful switch to "en-US.UTF-8"
         *       resulted in garbage display of all non-basic ASCII characters.
         */
    }

    /* initialize Unicode */
    G.unicode_escape_all = 0;
    G.unicode_mismatch = 0;

    G.unipath_version = 0;
    G.unipath_checksum = 0;
    G.unipath_filename = NULL;

#ifdef MALLOC_WORK
    /* The following (rather complex) expression determines the allocation
       size of the decompression work area.  It simulates what the
       combined "union" and "struct" declaration of the "static" work
       area reservation achieves automatically at compile time.
       Any decent compiler should evaluate this expression completely at
       compile time and provide constants to the zcalloc() call.
       (For better readability, some subexpressions are encapsulated
       in temporarly defined macros.)
     */
#define UZ_SLIDE_CHUNK (sizeof(shrint) + sizeof(uch) + sizeof(uch))
#define UZ_NUMOF_CHUNKS                                                 \
    (unsigned) (((WSIZE + UZ_SLIDE_CHUNK - 1) / UZ_SLIDE_CHUNK > HSIZE) \
                    ? (WSIZE + UZ_SLIDE_CHUNK - 1) / UZ_SLIDE_CHUNK     \
                    : HSIZE)
    G.area.Slide = (uch *) zcalloc(UZ_NUMOF_CHUNKS, UZ_SLIDE_CHUNK);
#undef UZ_SLIDE_CHUNK
#undef UZ_NUMOF_CHUNKS
    G.area.shrink.Parent = (shrint *) G.area.Slide;
    G.area.shrink.value = G.area.Slide + (sizeof(shrint) * (HSIZE));
    G.area.shrink.Stack =
        G.area.Slide + (sizeof(shrint) + sizeof(uch)) * (HSIZE);
#endif

/*---------------------------------------------------------------------------
    Set signal handler for restoring echo, warn of zipfile corruption, etc.
  ---------------------------------------------------------------------------*/
#ifdef SIGINT
    SET_SIGHANDLER(SIGINT, handler);
#endif
#ifdef SIGTERM /* some systems really have no SIGTERM */
    SET_SIGHANDLER(SIGTERM, handler);
#endif
#if defined(SIGABRT)
    SET_SIGHANDLER(SIGABRT, handler);
#endif
#ifdef SIGBREAK
    SET_SIGHANDLER(SIGBREAK, handler);
#endif
#ifdef SIGBUS
    SET_SIGHANDLER(SIGBUS, handler);
#endif
#ifdef SIGILL
    SET_SIGHANDLER(SIGILL, handler);
#endif
#ifdef SIGSEGV
    SET_SIGHANDLER(SIGSEGV, handler);
#endif

#ifdef DEBUG
#ifdef LARGE_FILE_SUPPORT
    /* test if we can support large files - 10/6/04 EG */
    if (sizeof(zoff_t) < 8) {
        Info(slide, 0x401,
             ((char *) slide, "LARGE_FILE_SUPPORT set but not supported\n"));
        retcode = PK_BADERR;
        goto cleanup_and_exit;
    }
    /* test if we can show 64-bit values */
    {
        zoff_t z = ~(zoff_t) 0; /* z should be all 1s now */
        char *sz;

        sz = FmZofft(z, FZOFFT_HEX_DOT_WID, "X");
        if ((sz[0] != 'F') || (strlen(sz) != 16)) {
            z = 0;
        }

        /* shift z so only MSB is set */
        z <<= 63;
        sz = FmZofft(z, FZOFFT_HEX_DOT_WID, "X");
        if ((sz[0] != '8') || (strlen(sz) != 16)) {
            Info(slide, 0x401,
                 ((char *) slide, "Can't show 64-bit values correctly\n"));
            retcode = PK_BADERR;
            goto cleanup_and_exit;
        }
    }
#endif /* LARGE_FILE_SUPPORT */

    /* 2004-11-30 SMS.
       Test the NEXTBYTE macro for proper operation.
    */
    {
        int test_char;
        static uch test_buf[2] = {'a', 'b'};

        G.inptr = test_buf;
        G.incnt = 1;

        test_char = NEXTBYTE; /* Should get 'a'. */
        if (test_char == 'a') {
            test_char = NEXTBYTE; /* Should get EOF, not 'b'. */
        }
        if (test_char != EOF) {
            Info(slide, 0x401,
                 ((char *) slide, "NEXTBYTE macro failed.  Try compiling with "
                                  "ALT_NEXTBYTE defined?"));

            retcode = PK_BADERR;
            goto cleanup_and_exit;
        }
    }
#endif /* DEBUG */

    G.noargs = (argc == 1); /* no options, no zipfile, no anything */

    {
        if ((error = envargs(&argc, &argv, LoadFarStringSmall(EnvUnZip),
                             LoadFarStringSmall2(EnvUnZip2))) != PK_OK)
            perror(LoadFarString(NoMemEnvArguments));
    }

    if (!error) {
        /* Check the length of all passed command line parameters.
         * Command arguments might get sent through the Info() message
         * system, which uses the sliding window area as string buffer.
         * As arguments may additionally get fed through one of the FnFilter
         * macros, we require all command line arguments to be shorter than
         * WSIZE/4 (and ca. 2 standard line widths for fixed message text).
         */
        for (i = 1; i < argc; i++) {
            if (strlen(argv[i]) > ((WSIZE >> 2) - 160)) {
                Info(slide, 0x401,
                     ((char *) slide, LoadFarString(CmdLineParamTooLong), i));
                retcode = PK_PARAM;
                goto cleanup_and_exit;
            }
        }
        error = uz_opts(&argc, &argv);
    }

    if ((argc < 0) || error) {
        retcode = error;
        goto cleanup_and_exit;
    }

    /*---------------------------------------------------------------------------
        Now get the zipfile name from the command line and then process any re-
        maining options and file specifications.
      ---------------------------------------------------------------------------*/

    G.wildzipfn = *argv++;

    G.filespecs = argc;
    G.xfilespecs = 0;

    if (argc > 0) {
        int in_files = FALSE, in_xfiles = FALSE;
        char **pp = argv - 1;

        G.process_all_files = FALSE;
        G.pfnames = argv;
        while (*++pp) {
            Trace((stderr, "pp - argv = %d\n", pp - argv));
            if (!uO.exdir && strncmp(*pp, "-d", 2) == 0) {
                int firstarg = (pp == argv);

                uO.exdir = (*pp) + 2;
                if (in_files) {          /* ... zipfile ... -d exdir ... */
                    *pp = (char *) NULL; /* terminate G.pfnames */
                    G.filespecs = pp - G.pfnames;
                    in_files = FALSE;
                } else if (in_xfiles) {
                    *pp = (char *) NULL; /* terminate G.pxnames */
                    G.xfilespecs = pp - G.pxnames;
                    /* "... -x xlist -d exdir":  nothing left */
                }
                /* first check for "-dexdir", then for "-d exdir" */
                if (*uO.exdir == '\0') {
                    if (*++pp)
                        uO.exdir = *pp;
                    else {
                        Info(slide, 0x401,
                             ((char *) slide, LoadFarString(MustGiveExdir)));
                        /* don't extract here by accident */
                        retcode = PK_PARAM;
                        goto cleanup_and_exit;
                    }
                }
                if (firstarg) { /* ... zipfile -d exdir ... */
                    if (pp[1]) {
                        G.pfnames = pp + 1; /* argv+2 */
                        G.filespecs =
                            argc - (G.pfnames - argv); /* for now... */
                    } else {
                        G.process_all_files = TRUE;
                        G.pfnames =
                            (char **) default_fnames; /* GRR: necessary? */
                        G.filespecs = 0;              /* GRR: necessary? */
                        break;
                    }
                }
            } else if (!in_xfiles) {
                if (strcmp(*pp, "-x") == 0) {
                    in_xfiles = TRUE;
                    if (pp == G.pfnames) {
                        G.pfnames = (char **) default_fnames; /* defaults */
                        G.filespecs = 0;
                    } else if (in_files) {
                        *pp = 0;                      /* terminate G.pfnames */
                        G.filespecs = pp - G.pfnames; /* adjust count */
                        in_files = FALSE;
                    }
                    G.pxnames = pp + 1; /* excluded-names ptr starts after -x */
                    G.xfilespecs =
                        argc - (G.pxnames - argv); /* anything left */
                } else
                    in_files = TRUE;
            }
        }
    } else
        G.process_all_files = TRUE; /* for speed */

    if (uO.exdir != (char *) NULL && !G.extract_flag) /* -d ignored */
        Info(slide, 0x401, ((char *) slide, LoadFarString(NotExtracting)));

    /* set Unicode-escape-all if option -U used */
    if (uO.U_flag == 1)
        G.unicode_escape_all = TRUE;

    /*---------------------------------------------------------------------------
        Okey dokey, we have everything we need to get started.  Let's roll.
      ---------------------------------------------------------------------------*/

    retcode = process_zipfiles();

cleanup_and_exit:
#if (defined(MALLOC_WORK) && !defined(REENTRANT))
    if (G.area.Slide != (uch *) NULL) {
        free(G.area.Slide);
        G.area.Slide = (uch *) NULL;
    }
#endif
    return (retcode);

} /* end main()/unzip() */

int uz_opts(pargc, pargv)
int *pargc;
char ***pargv;
{
    char **argv, *s;
    int argc, c, error = FALSE, negative = 0, showhelp = 0;

    argc = *pargc;
    argv = *pargv;

    while (++argv, (--argc > 0 && *argv != NULL && **argv == '-')) {
        s = *argv + 1;
        while ((c = *s++) != 0) { /* "!= 0":  prevent Turbo C warning */
            switch (c) {
            case ('-'):
                ++negative;
                break;
            case ('a'):
                if (negative) {
                    uO.aflag = MAX(uO.aflag - negative, 0);
                    negative = 0;
                } else
                    ++uO.aflag;
                break;
            case ('b'):
                if (negative) {
                    negative = 0; /* do nothing:  "-b" is default */
                } else {
                    uO.aflag = 0;
                }
                break;
            case ('c'):
                if (negative) {
                    uO.cflag = FALSE, negative = 0;
#ifdef NATIVE
                    uO.aflag = 0;
#endif
                } else {
                    uO.cflag = TRUE;
#ifdef NATIVE
                    uO.aflag = 2; /* so you can read it on the screen */
#endif
                }
                break;
            case ('C'): /* -C:  match filenames case-insensitively */
                if (negative)
                    uO.C_flag = FALSE, negative = 0;
                else
                    uO.C_flag = TRUE;
                break;
            case ('d'):
                if (negative) { /* negative not allowed with -d exdir */
                    Info(slide, 0x401,
                         ((char *) slide, LoadFarString(MustGiveExdir)));
                    return (PK_PARAM); /* don't extract here by accident */
                }
                if (uO.exdir != (char *) NULL) {
                    Info(slide, 0x401,
                         ((char *) slide, LoadFarString(OnlyOneExdir)));
                    return (PK_PARAM); /* GRR:  stupid restriction? */
                } else {
                    /* first check for "-dexdir", then for "-d exdir" */
                    uO.exdir = s;
                    if (*uO.exdir == '\0') {
                        if (argc > 1) {
                            --argc;
                            uO.exdir = *++argv;
                            if (*uO.exdir == '-') {
                                Info(slide, 0x401,
                                     ((char *) slide,
                                      LoadFarString(MustGiveExdir)));
                                return (PK_PARAM);
                            }
                            /* else uO.exdir points at extraction dir */
                        } else {
                            Info(
                                slide, 0x401,
                                ((char *) slide, LoadFarString(MustGiveExdir)));
                            return (PK_PARAM);
                        }
                    }
                    /* uO.exdir now points at extraction dir (-dexdir or
                     *  -d exdir); point s at end of exdir to avoid mis-
                     *  interpretation of exdir characters as more options
                     */
                    if (*s != 0)
                        while (*++s != 0)
                            ;
                }
                break;
            case ('D'): /* -D: Skip restoring dir (or any) timestamp. */
                if (negative) {
                    uO.D_flag = MAX(uO.D_flag - negative, 0);
                    negative = 0;
                } else
                    uO.D_flag++;
                break;
            case ('e'): /* just ignore -e, -x options (extract) */
                break;
            case ('f'): /* "freshen" (extract only newer files) */
                if (negative)
                    uO.fflag = uO.uflag = FALSE, negative = 0;
                else
                    uO.fflag = uO.uflag = TRUE;
                break;
            case ('h'): /* just print help message and quit */
                if (showhelp == 0) {
                    if (*s == 'h')
                        showhelp = 2;
                    else {
                        showhelp = 1;
                    }
                }
                break;
            case ('j'): /* junk pathnames/directory structure */
                if (negative)
                    uO.jflag = FALSE, negative = 0;
                else
                    uO.jflag = TRUE;
                break;
            case ('K'):
                if (negative) {
                    uO.K_flag = FALSE, negative = 0;
                } else {
                    uO.K_flag = TRUE;
                }
                break;
            case ('l'):
                if (negative) {
                    uO.vflag = MAX(uO.vflag - negative, 0);
                    negative = 0;
                } else
                    ++uO.vflag;
                break;
            case ('L'): /* convert (some) filenames to lowercase */
                if (negative) {
                    uO.L_flag = MAX(uO.L_flag - negative, 0);
                    negative = 0;
                } else
                    ++uO.L_flag;
                break;
            case ('n'): /* don't overwrite any files */
                if (negative)
                    uO.overwrite_none = FALSE, negative = 0;
                else
                    uO.overwrite_none = TRUE;
                break;
            case ('o'): /* OK to overwrite files without prompting */
                if (negative) {
                    uO.overwrite_all = MAX(uO.overwrite_all - negative, 0);
                    negative = 0;
                } else
                    ++uO.overwrite_all;
                break;
            case ('p'): /* pipes:  extract to stdout, no messages */
                if (negative) {
                    uO.cflag = FALSE;
                    uO.qflag = MAX(uO.qflag - 999, 0);
                    negative = 0;
                } else {
                    uO.cflag = TRUE;
                    uO.qflag += 999;
                }
                break;
            /* GRR:  yes, this is highly insecure, but dozens of people
             * have pestered us for this, so here we go... */
            case ('P'):
                if (negative) { /* negative not allowed with -P passwd */
                    Info(slide, 0x401,
                         ((char *) slide, LoadFarString(MustGivePasswd)));
                    return (PK_PARAM); /* don't extract here by accident */
                }
                if (uO.pwdarg != (char *) NULL) {
                    /*
                                            GRR:  eventually support multiple
                       passwords? Info(slide, 0x401, ((char *)slide,
                                              LoadFarString(OnlyOnePasswd)));
                                            return(PK_PARAM);
                     */
                } else {
                    /* first check for "-Ppasswd", then for "-P passwd" */
                    uO.pwdarg = s;
                    if (*uO.pwdarg == '\0') {
                        if (argc > 1) {
                            --argc;
                            uO.pwdarg = *++argv;
                            if (*uO.pwdarg == '-') {
                                Info(slide, 0x401,
                                     ((char *) slide,
                                      LoadFarString(MustGivePasswd)));
                                return (PK_PARAM);
                            }
                            /* else pwdarg points at decryption password */
                        } else {
                            Info(slide, 0x401,
                                 ((char *) slide,
                                  LoadFarString(MustGivePasswd)));
                            return (PK_PARAM);
                        }
                    }
                    /* pwdarg now points at decryption password (-Ppasswd or
                     *  -P passwd); point s at end of passwd to avoid mis-
                     *  interpretation of passwd characters as more options
                     */
                    if (*s != 0)
                        while (*++s != 0)
                            ;
                }
                break;
            case ('q'): /* quiet:  fewer comments/messages */
                if (negative) {
                    uO.qflag = MAX(uO.qflag - negative, 0);
                    negative = 0;
                } else
                    ++uO.qflag;
                break;
            case ('t'):
                if (negative)
                    uO.tflag = FALSE, negative = 0;
                else
                    uO.tflag = TRUE;
                break;
            case ('T'):
                if (negative)
                    uO.T_flag = FALSE, negative = 0;
                else
                    uO.T_flag = TRUE;
                break;
            case ('u'): /* update (extract only new and newer files) */
                if (negative)
                    uO.uflag = FALSE, negative = 0;
                else
                    uO.uflag = TRUE;
                break;
            case ('U'): /* escape UTF-8, or disable UTF-8 support */
                if (negative) {
                    uO.U_flag = MAX(uO.U_flag - negative, 0);
                    negative = 0;
                } else
                    uO.U_flag++;
                break;
            case ('v'): /* verbose */
                if (negative) {
                    uO.vflag = MAX(uO.vflag - negative, 0);
                    negative = 0;
                } else if (uO.vflag)
                    ++uO.vflag;
                else
                    uO.vflag = 2;
                break;
            case ('V'): /* Version (retain VMS/DEC-20 file versions) */
                if (negative)
                    uO.V_flag = FALSE, negative = 0;
                else
                    uO.V_flag = TRUE;
                break;
            case ('x'): /* extract:  default */
                break;
            case ('X'): /* restore owner/protection info (need privs?) */
                if (negative) {
                    uO.X_flag = MAX(uO.X_flag - negative, 0);
                    negative = 0;
                } else
                    ++uO.X_flag;
                break;
            case ('z'): /* display only the archive comment */
                if (negative) {
                    uO.zflag = MAX(uO.zflag - negative, 0);
                    negative = 0;
                } else
                    ++uO.zflag;
                break;
            case (':'): /* allow "parent dir" path components */
                if (negative) {
                    uO.ddotflag = MAX(uO.ddotflag - negative, 0);
                    negative = 0;
                } else
                    ++uO.ddotflag;
                break;
            case ('^'): /* allow control chars in filenames */
                if (negative) {
                    uO.cflxflag = MAX(uO.cflxflag - negative, 0);
                    negative = 0;
                } else
                    ++uO.cflxflag;
                break;
            default:
                error = TRUE;
                break;

            } /* end switch */
        }     /* end while (not end of argument string) */
    }         /* end while (not done with switches) */

    /*---------------------------------------------------------------------------
        Check for nonsensical combinations of options.
      ---------------------------------------------------------------------------*/

    if (showhelp > 0) { /* just print help message and quit */
        *pargc = -1;
        if (showhelp == 2) {
            help_extended();
            return PK_OK;
        } else {
            return usage(PK_OK);
        }
    }

    if ((uO.cflag && (uO.tflag || uO.uflag)) || (uO.tflag && uO.uflag) ||
        (uO.fflag && uO.overwrite_none)) {
        Info(slide, 0x401, ((char *) slide, LoadFarString(InvalidOptionsMsg)));
        error = TRUE;
    }
    if (uO.aflag > 2)
        uO.aflag = 2;
    if (uO.overwrite_all && uO.overwrite_none) {
        Info(slide, 0x401, ((char *) slide, LoadFarString(IgnoreOOptionMsg)));
        uO.overwrite_all = FALSE;
    }

    if ((argc-- == 0) || error) {
        *pargc = argc;
        *pargv = argv;
        if (uO.vflag >= 2 && argc == -1) { /* "unzip -v" */
            return PK_OK;
        }
        if (!G.noargs && !error)
            error = TRUE; /* had options (not -h or -v) but no zipfile */
        return usage(error);
    }

    if (uO.cflag || uO.tflag || uO.vflag || uO.zflag || uO.T_flag)
        G.extract_flag = FALSE;
    else
        G.extract_flag = TRUE;

    *pargc = argc;
    *pargv = argv;
    return PK_OK;

} /* end function uz_opts() */

#define QUOT  ' '
#define QUOTS ""

int usage(error) /* return PK-type error code */
int error;
{
    int flag = (error ? 1 : 0);

    Info(slide, flag,
         ((char *) slide, LoadFarString(UnzipUsageLine2), ZIPINFO_MODE_OPTION,
          LoadFarStringSmall(ZipInfoMode)));

    Info(slide, flag,
         ((char *) slide, LoadFarString(UnzipUsageLine3),
          LoadFarStringSmall(local1)));

    Info(slide, flag,
         ((char *) slide, LoadFarString(UnzipUsageLine4),
          LoadFarStringSmall(local2), LoadFarStringSmall2(local3)));

    /* This is extra work for SMALL_MEM, but it will work since
     * LoadFarStringSmall2 uses the same buffer.  Remember, this
     * is a hack. */
    Info(slide, flag,
         ((char *) slide, LoadFarString(UnzipUsageLine5),
          LoadFarStringSmall(Example2), LoadFarStringSmall2(Example3),
          LoadFarStringSmall2(Example3)));

    if (error)
        return PK_PARAM;
    else
        return PK_COOL; /* just wanted usage screen: no error */

} /* end function usage() */

/* Print extended help to stdout. */
static void help_extended()
{
    extent i; /* counter for help array */

    /* help array */
    static const char *text[] = {
        "",
        "Extended Help for UnZip",
        "",
        "See the UnZip Manual for more detailed help",
        "",
        "",
        "UnZip lists and extracts files in zip archives.  The default action "
        "is to",
        "extract zipfile entries to the current directory, creating "
        "directories as",
        "needed.  With appropriate options, UnZip lists the contents of "
        "archives",
        "instead.",
        "",
        "Basic unzip command line:",
        "  unzip [-Z] options archive[.zip] [file ...] [-x xfile ...] [-d "
        "exdir]",
        "",
        "Some examples:",
        "  unzip -l foo.zip        - list files in short format in archive "
        "foo.zip",
        "",
        "  unzip -t foo            - test the files in archive foo",
        "",
        "  unzip -Z foo            - list files using more detailed zipinfo "
        "format",
        "",
        "  unzip foo               - unzip the contents of foo in current dir",
        "",
        "  unzip -a foo            - unzip foo and convert text files to local "
        "OS",
        "",
        "If unzip is run in zipinfo mode, a more detailed list of archive "
        "contents",
        "is provided.  The -Z option sets zipinfo mode and changes the "
        "available",
        "options.",
        "",
        "Basic zipinfo command line:",
        "  unzip -Z options archive[.zip] [file ...] [-x xfile ...]",
        "",
        "",
        "unzip options:",
        "  -hh  Display extended help.",
        "  -c   Extract files to stdout/screen.  As -p but include names.  "
        "Also,",
        "         -a allowed and EBCDIC conversions done if needed.",
        "  -f   Freshen by extracting only if older file on disk.",
        "  -l   List files using short form.",
        "  -p   Extract files to pipe (stdout).  Only file data is output and "
        "all",
        "         files extracted in binary mode (as stored).",
        "  -t   Test archive files.",
        "  -T   Set timestamp on archive(s) to that of newest file.  Similar "
        "to",
        "       zip -o but faster.",
        "  -u   Update existing older files on disk as -f and extract new "
        "files.",
        "  -v   Use verbose list format.  If given alone as unzip -v show "
        "version",
        "         information.  Also can be added to other list commands for "
        "more",
        "         verbose output.",
        "  -z   Display only archive comment.",
        "",
        "unzip modifiers:",
        "  -a   Convert text files to local OS format.  Convert line ends, EOF",
        "         marker, and from or to EBCDIC character set as needed.",
        "  -b   Treat all files as binary.",
        "files.",
        "  -B   Save a backup copy of each overwritten file in foo~ or "
        "foo~99999 format.",
        "  -C   Use case-insensitive matching.",
        "  -D   Skip restoration of timestamps for extracted directories.",
        "  -DD  Skip restoration of timestamps for all entries.",
        "  -j   Junk paths and deposit all files in extraction directory.",
        "  -K   Restore SUID/SGID/Tacky file attributes.",
        "  -L   Convert to lowercase any names from uppercase only file "
        "system.",
        "  -LL  Convert all files to lowercase.",
        "  -M   Pipe all output through internal pager similar to Unix "
        "more(1).",
        "  -n   Never overwrite existing files.  Skip extracting that file, no "
        "prompt.",
        "  -N   [Amiga] Extract file comments as Amiga filenotes.",
        "  -o   Overwrite existing files without prompting.  Useful with -f.  "
        "Use with",
        "         care.",
        "  -P p Use password p to decrypt files.  THIS IS INSECURE!  Some OS "
        "show",
        "         command line to other users.",
        "  -q   Perform operations quietly.  The more q (as in -qq) the "
        "quieter.",
        "  -U   [UNICODE enabled] Show non-local characters as #Uxxxx or "
        "#Lxxxxxx ASCII",
        "         text escapes where x is hex digit.  [Old] -U used to leave "
        "names",
        "         uppercase if created on MS-DOS, VMS, etc.  See -L.",
        "  -UU  [UNICODE enabled] Disable use of stored UTF-8 paths.  Note "
        "that UTF-8",
        "         paths stored as native local paths are still processed as "
        "Unicode.",
        "  -V   Retain VMS file version numbers.",
        "  -W   Modify pattern matching so ? and * do not",
        "         match directory separator /, but ** does.  Allows matching "
        "at specific",
        "         directory levels.",
        "  -X   [VMS, Unix, OS/2, NT, Tandem] Restore UICs and ACL entries "
        "under VMS,",
        "         or UIDs/GIDs under Unix, or ACLs under certain "
        "network-enabled",
        "         versions of OS/2, or security ACLs under Windows NT.  Can "
        "require",
        "         user privileges.",
        "  -:   Allow extract archive members into",
        "         locations outside of current extraction root folder.  This "
        "allows",
        "         paths such as ../foo to be extracted above the current "
        "extraction",
        "         directory, which can be a security problem.",
        "  -^   Allow control characters in names of extracted entries. "
        " Usually",
        "         this is not a good thing and should be avoided.",
        "",
        "",
        "Wildcards:",
        "  Internally unzip supports the following wildcards:",
        "    ?       (or %% or #, depending on OS) matches any single "
        "character",
        "    *       matches any number of characters, including zero",
        "    [list]  matches char in list (regex), can do range [ac-f], all "
        "but [!bf]",
        "  If port supports [], must escape [ as [[]",
        "  For shells that expand wildcards, escape (\\* or \"*\") so unzip "
        "can recurse.",
        "",
        "Include and Exclude:",
        "  -i pattern pattern ...   include files that match a pattern",
        "  -x pattern pattern ...   exclude files that match a pattern",
        "  Patterns are paths with optional wildcards and match paths as "
        "stored in",
        "  archive.  Exclude and include lists end at next option or end of "
        "line.",
        "    unzip archive -x pattern pattern ...",
        "",
        "Multi-part (split) archives (archives created as a set of split "
        "files):",
        "  Currently split archives are not readable by unzip.  A workaround "
        "is",
        "  to use zip to convert the split archive to a single-file archive "
        "and",
        "  use unzip on that.  See the manual page for Zip 3.0 or later.",
        "",
        "Streaming (piping into unzip):",
        "  Currently unzip does not support streaming.  The funzip utility can "
        "be",
        "  used to process the first entry in a stream.",
        "    cat archive | funzip",
        "",
        "Testing archives:",
        "  -t        test contents of archive",
        "  This can be modified using -q for quieter operation, and -qq for "
        "even",
        "  quieter operation.",
        "",
        "Unicode:",
        "  If compiled with Unicode support, unzip automatically handles "
        "archives",
        "  with Unicode entries.  Currently Unicode on Win32 systems is "
        "limited.",
        "  Characters not in the current character set are shown as ASCII "
        "escapes",
        "  in the form #Uxxxx where the Unicode character number fits in 16 "
        "bits,",
        "  or #Lxxxxxx where it doesn't, where x is the ASCII character for a "
        "hex",
        "  digit.",
        "",
        "",
        ""};

    for (i = 0; i < sizeof(text) / sizeof(char *); i++) {
        Info(slide, 0, ((char *) slide, "%s\n", text[i]));
    }
} /* end function help_extended() */
