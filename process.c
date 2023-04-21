/*
  Copyright (c) 1990-2014 Info-ZIP.  All rights reserved.

  See the accompanying file LICENSE, version 2009-Jan-02 or later
  (the contents of which are also included in unzip.h) for terms of use.
  If, for some reason, all these files are missing, the Info-ZIP license
  also may be found at:  ftp://ftp.info-zip.org/pub/infozip/license.html
*/
/*---------------------------------------------------------------------------

  process.c

  This file contains the top-level routines for processing multiple zipfiles.

  Contains:  process_zipfiles()
             free_G_buffers()
             do_seekable()
             file_size()
             rec_find()
             find_ecrec64()
             find_ecrec()
             process_zip_cmmnt()
             process_cdir_file_hdr()
             get_cdir_ent()
             process_local_file_hdr()
             getZip64Data()
             ef_scan_for_izux()
             getRISCOSexfield()

  ---------------------------------------------------------------------------*/

#define UNZIP_INTERNAL
#include "unzip.h"
#include "crc32.h"

static int do_seekable(__GPRO__ int lastchance);
static zoff_t file_size(int fh);
static int rec_find(__GPRO__ zoff_t, char *, int);
static int find_ecrec64(__GPRO__ zoff_t searchlen);
static int find_ecrec(__GPRO__ zoff_t searchlen);
static int process_zip_cmmnt(__GPRO);
static int get_cdir_ent(__GPRO);
static int read_ux3_value OF((const uch *dbuf, unsigned uidgid_sz,
                              ulg *p_uidgid));

static const char Far CannotAllocateBuffers[] =
    "error:  cannot allocate unzip buffers\n";

/* process_zipfiles() strings */
static const char Far FilesProcessOK[] =
    "%d archive%s successfully processed.\n";
static const char Far ArchiveWarning[] =
    "%d archive%s had warnings but no fatal errors.\n";
static const char Far ArchiveFatalError[] = "%d archive%s had fatal errors.\n";
static const char Far FileHadNoZipfileDir[] =
    "%d file%s had no zipfile directory.\n";
static const char Far ZipfileWasDir[] = "1 \"zipfile\" was a directory.\n";
static const char Far ManyZipfilesWereDir[] =
    "%d \"zipfiles\" were directories.\n";
static const char Far NoZipfileFound[] = "No zipfiles found.\n";

/* do_seekable() strings */
static const char Far CannotFindZipfileDirMsg[] =
    "%s:  cannot find zipfile directory in one of %s or\n\
        %s%s.zip, and cannot find %s, period.\n";
static const char Far CannotFindEitherZipfile[] =
    "%s:  cannot find or open %s, %s.zip or %s.\n";
extern const char Far Zipnfo[]; /* in unzip.c */
static const char Far Unzip[] = "unzip";
static const char Far ZipfileTooBig[] =
    "Trying to read large file (> 2 GiB) without large file support\n";
static const char Far MaybeExe[] =
    "note:  %s may be a plain executable, not an archive\n";
static const char Far CentDirNotInZipMsg[] = "\n\
   [%s]:\n\
     Zipfile is disk %lu of a multi-disk archive, and this is not the disk on\n\
     which the central zipfile directory begins (disk %lu).\n";
static const char Far EndCentDirBogus[] =
    "\nwarning [%s]:  end-of-central-directory record claims this\n\
  is disk %lu but that the central directory starts on disk %lu; this is a\n\
  contradiction.  Attempting to process anyway.\n";
static const char Far MaybePakBug[] = "warning [%s]:\
  zipfile claims to be last disk of a multi-part archive;\n\
  attempting to process anyway, assuming all parts have been concatenated\n\
  together in order.  Expect \"errors\" and warnings...true multi-part support\
\n  doesn't exist yet (coming soon).\n";
static const char Far ExtraBytesAtStart[] =
    "warning [%s]:  %s extra byte%s at beginning or within zipfile\n\
  (attempting to process anyway)\n";

static const char Far LogInitline[] = "Archive:  %s\n";

static const char Far MissingBytes[] =
    "error [%s]:  missing %s bytes in zipfile\n\
  (attempting to process anyway)\n";
static const char Far NullCentDirOffset[] =
    "error [%s]:  NULL central directory offset\n\
  (attempting to process anyway)\n";
static const char Far ZipfileEmpty[] = "warning [%s]:  zipfile is empty\n";
static const char Far CentDirStartNotFound[] =
    "error [%s]:  start of central directory not found;\n\
  zipfile corrupt.\n%s";
static const char Far Cent64EndSigSearchErr[] =
    "fatal error: read failure while seeking for End-of-centdir-64 signature.\n\
  This zipfile is corrupt.\n";
static const char Far Cent64EndSigSearchOff[] =
    "error: End-of-centdir-64 signature not where expected (prepended bytes?)\n\
  (attempting to process anyway)\n";
static const char Far CentDirTooLong[] =
    "error [%s]:  reported length of central directory is\n\
  %s bytes too long (Atari STZip zipfile?  J.H.Holm ZIPSPLIT 1.1\n\
  zipfile?).  Compensating...\n";
static const char Far CentDirEndSigNotFound[] = "\
  End-of-central-directory signature not found.  Either this file is not\n\
  a zipfile, or it constitutes one disk of a multi-part archive.  In the\n\
  latter case the central directory and zipfile comment will be found on\n\
  the last disk(s) of this archive.\n";
static const char Far ZipTimeStampFailed[] =
    "warning:  cannot set time for %s\n";
static const char Far ZipTimeStampSuccess[] = "Updated time stamp for %s.\n";
static const char Far ZipfileCommTrunc1[] =
    "\ncaution:  zipfile comment truncated\n";
static const char Far UnicodeVersionError[] =
    "\nwarning:  Unicode Path version > 1\n";
static const char Far UnicodeMismatchError[] =
    "\nwarning:  Unicode Path checksum invalid\n";
static const char Far UFilenameTooLongTrunc[] =
    "warning:  filename too long (P1) -- truncating.\n";

/*******************************/
/* Function process_zipfiles() */
/*******************************/

int process_zipfiles() /* return PK-type error code */
{
    char *lastzipfn = (char *) NULL;
    int NumWinFiles, NumLoseFiles, NumWarnFiles;
    int NumMissDirs, NumMissFiles;
    int error = 0, error_in_archive = 0;

    /*---------------------------------------------------------------------------
        Start by allocating buffers and (re)constructing the various PK
      signature strings.
      ---------------------------------------------------------------------------*/

    G.inbuf = (uch *) malloc(INBUFSIZ + 4);   /* 4 extra for hold[] (below) */
    G.outbuf = (uch *) malloc(OUTBUFSIZ + 1); /* 1 extra for string term. */

    if ((G.inbuf == (uch *) NULL) || (G.outbuf == (uch *) NULL)) {
        Info(slide, 0x401,
             ((char *) slide, LoadFarString(CannotAllocateBuffers)));
        return (PK_MEM);
    }
    G.hold = G.inbuf + INBUFSIZ; /* to check for boundary-spanning sigs */

#if 0  /* CRC_32_TAB has been NULLified by CONSTRUCTGLOBALS !!!! */
    /* allocate the CRC table later when we know we can read zipfile data */
    CRC_32_TAB = NULL;
#endif /* 0 */

    /* finish up initialization of magic signature strings */
    local_hdr_sig[0] /* = extd_local_sig[0] */ =  /* ASCII 'P', */
        central_hdr_sig[0] = end_central_sig[0] = /* not EBCDIC */
        end_centloc64_sig[0] = end_central64_sig[0] = 0x50;

    local_hdr_sig[1] /* = extd_local_sig[1] */ =  /* ASCII 'K', */
        central_hdr_sig[1] = end_central_sig[1] = /* not EBCDIC */
        end_centloc64_sig[1] = end_central64_sig[1] = 0x4B;

    /*---------------------------------------------------------------------------
        Make sure timezone info is set correctly; localtime() returns GMT on
      some OSes (e.g., Solaris 2.x) if this isn't done first.  The ifdefs around
        tzset() were initially copied from dos_to_unix_time() in fileio.c.  They
        may still be too strict; any listed OS that supplies tzset(), regardless
        of whether the function does anything, should be removed from the
      ifdefs.
      ---------------------------------------------------------------------------*/

    /* For systems that do not have tzset() but supply this function using
       another name (_tzset() or something similar), an appropiate "#define
       tzset ..." should be added to the system specifc configuration section.
     */
    tzset();

    /* Initialize UnZip's built-in pseudo hard-coded "ISO <--> OEM" translation,
       depending on the detected codepage setup.  */

    /*---------------------------------------------------------------------------
        Initialize the internal flag holding the mode of processing "overwrite
        existing file" cases.  We do not use the calling interface flags
      directly because the overwrite mode may be changed by user interaction
      while processing archive files.  Such a change should not affect the
      option settings as passed through the DLL calling interface. In case of
      conflicting options, the 'safer' flag uO.overwrite_none takes precedence.
      ---------------------------------------------------------------------------*/
    G.overwrite_mode = (uO.overwrite_none ? OVERWRT_NEVER
                                          : (uO.overwrite_all ? OVERWRT_ALWAYS
                                                              : OVERWRT_QUERY));

    /*---------------------------------------------------------------------------
        Match (possible) wildcard zipfile specification with existing files and
        attempt to process each.  If no hits, try again after appending ".zip"
        suffix.  If still no luck, give up.
      ---------------------------------------------------------------------------*/

    NumWinFiles = NumLoseFiles = NumWarnFiles = 0;
    NumMissDirs = NumMissFiles = 0;

    while ((G.zipfn = do_wild(G.wildzipfn)) != (char *) NULL) {
        Trace((stderr, "do_wild( %s ) returns %s\n", G.wildzipfn, G.zipfn));

        lastzipfn = G.zipfn;

        /* print a blank line between the output of different zipfiles */
        if (!uO.qflag && error != PK_NOZIP && error != IZ_DIR &&
            (!uO.T_flag || uO.zipinfo_mode) &&
            (NumWinFiles + NumLoseFiles + NumWarnFiles + NumMissFiles) > 0)
            (*G.message)((void *) &G, (uch *) "\n", 1L, 0);

        if ((error = do_seekable(0)) == PK_WARN)
            ++NumWarnFiles;
        else if (error == IZ_DIR)
            ++NumMissDirs;
        else if (error == PK_NOZIP)
            ++NumMissFiles;
        else if (error != PK_OK)
            ++NumLoseFiles;
        else
            ++NumWinFiles;

        Trace((stderr, "do_seekable(0) returns %d\n", error));
        if (error != IZ_DIR && error > error_in_archive)
            error_in_archive = error;

    } /* end while-loop (wildcard zipfiles) */

    if ((NumWinFiles + NumWarnFiles + NumLoseFiles) == 0 &&
        (NumMissDirs + NumMissFiles) == 1 && lastzipfn != (char *) NULL) {
        {
            /* 2004-11-24 SMS.
             * VMS has already tried a default file type of ".zip" in
             * do_wild(), so adding ZSUFX here only causes confusion by
             * corrupting some valid (though nonexistent) file names.
             * Complaining below about "fred;4.zip" is unlikely to be
             * helpful to the victim.
             */
            /* 2005-08-14 Chr. Spieler
             * Although we already "know" the failure result, we call
             * do_seekable() again with the same zipfile name (and the
             * lastchance flag set), just to trigger the error report...
             */
            char *p = strcpy(lastzipfn + strlen(lastzipfn), ZSUFX);

            G.zipfn = lastzipfn;

            NumMissDirs = NumMissFiles = 0;
            error_in_archive = PK_COOL;

            /* only Unix has case-sensitive filesystems */
            /* Well FlexOS (sometimes) also has them,  but support is per media
             */
            /* and a pig to code for,  so treat as case insensitive for now */
            /* we do this under QDOS to check for .zip as well as _zip */
            if ((error = do_seekable(0)) == PK_NOZIP || error == IZ_DIR) {
                if (error == IZ_DIR)
                    ++NumMissDirs;
                strcpy(p, ALT_ZSUFX);
                error = do_seekable(1);
            }
            Trace((stderr, "do_seekable(1) returns %d\n", error));
            switch (error) {
            case PK_WARN:
                ++NumWarnFiles;
                break;
            case IZ_DIR:
                ++NumMissDirs;
                error = PK_NOZIP;
                break;
            case PK_NOZIP:
                /* increment again => bug:
                   "1 file had no zipfile directory." */
                /* ++NumMissFiles */;
                break;
            default:
                if (error)
                    ++NumLoseFiles;
                else
                    ++NumWinFiles;
                break;
            }

            if (error > error_in_archive)
                error_in_archive = error;
        }
    }

    /*---------------------------------------------------------------------------
        Print summary of all zipfiles, assuming zipfile spec was a wildcard (no
        need for a summary if just one zipfile).
      ---------------------------------------------------------------------------*/

    if (iswild(G.wildzipfn) && uO.qflag < 3 &&
        !(uO.T_flag && !uO.zipinfo_mode && uO.qflag > 1)) {
        if ((NumMissFiles + NumLoseFiles + NumWarnFiles > 0 ||
             NumWinFiles != 1) &&
            !(uO.T_flag && !uO.zipinfo_mode && uO.qflag) &&
            !(uO.tflag && uO.qflag > 1))
            (*G.message)((void *) &G, (uch *) "\n", 1L, 0x401);
        if ((NumWinFiles > 1) ||
            (NumWinFiles == 1 &&
             NumMissDirs + NumMissFiles + NumLoseFiles + NumWarnFiles > 0))
            Info(slide, 0x401,
                 ((char *) slide, LoadFarString(FilesProcessOK), NumWinFiles,
                  (NumWinFiles == 1) ? " was" : "s were"));
        if (NumWarnFiles > 0)
            Info(slide, 0x401,
                 ((char *) slide, LoadFarString(ArchiveWarning), NumWarnFiles,
                  (NumWarnFiles == 1) ? "" : "s"));
        if (NumLoseFiles > 0)
            Info(slide, 0x401,
                 ((char *) slide, LoadFarString(ArchiveFatalError),
                  NumLoseFiles, (NumLoseFiles == 1) ? "" : "s"));
        if (NumMissFiles > 0)
            Info(slide, 0x401,
                 ((char *) slide, LoadFarString(FileHadNoZipfileDir),
                  NumMissFiles, (NumMissFiles == 1) ? "" : "s"));
        if (NumMissDirs == 1)
            Info(slide, 0x401, ((char *) slide, LoadFarString(ZipfileWasDir)));
        else if (NumMissDirs > 0)
            Info(slide, 0x401,
                 ((char *) slide, LoadFarString(ManyZipfilesWereDir),
                  NumMissDirs));
        if (NumWinFiles + NumLoseFiles + NumWarnFiles == 0)
            Info(slide, 0x401, ((char *) slide, LoadFarString(NoZipfileFound)));
    }

    /* free allocated memory */
    free_G_buffers();

    return error_in_archive;

} /* end function process_zipfiles() */

/*****************************/
/* Function free_G_buffers() */
/*****************************/

void free_G_buffers() /* releases all memory allocated in global vars */
{
    unsigned i;

    inflate_free();
    checkdir((char *) NULL, END);

    if (G.key != (char *) NULL) {
        free(G.key);
        G.key = (char *) NULL;
    }

    if (G.extra_field != (uch *) NULL) {
        free(G.extra_field);
        G.extra_field = (uch *) NULL;
    }

    /* VMS uses its own buffer scheme for textmode flush() */
    if (G.outbuf2) {
        free(G.outbuf2); /* malloc'd ONLY if unshrink and -a */
        G.outbuf2 = (uch *) NULL;
    }

    if (G.outbuf)
        free(G.outbuf);
    if (G.inbuf)
        free(G.inbuf);
    G.inbuf = G.outbuf = (uch *) NULL;

    if (G.filename_full) {
        free(G.filename_full);
        G.filename_full = (char *) NULL;
        G.fnfull_bufsize = 0;
    }

    for (i = 0; i < DIR_BLKSIZ; i++) {
        if (G.info[i].cfilname != (char Far *) NULL) {
            free(G.info[i].cfilname);
            G.info[i].cfilname = (char Far *) NULL;
        }
    }

#ifdef MALLOC_WORK
    if (G.area.Slide) {
        free(G.area.Slide);
        G.area.Slide = (uch *) NULL;
    }
#endif

    /* Free the cover span list and the cover structure. */
    if (G.cover != NULL) {
        free(*(G.cover));
        free(G.cover);
        G.cover = NULL;
    }

} /* end function free_G_buffers() */

/**************************/
/* Function do_seekable() */
/**************************/

static int do_seekable(lastchance) /* return PK-type error code */
int lastchance;
{
    /* static int no_ecrec = FALSE;  SKM: moved to globals.h */
    int maybe_exe = FALSE;
    int too_weird_to_continue = FALSE;
    time_t uxstamp;
    ulg nmember = 0L;
    int error = 0, error_in_archive;

    /*---------------------------------------------------------------------------
        Open the zipfile for reading in BINARY mode to prevent CR/LF
      translation, which would corrupt the bit streams.
      ---------------------------------------------------------------------------*/

    if (SSTAT(G.zipfn, &G.statbuf) ||
        (error = S_ISDIR(G.statbuf.st_mode)) != 0) {
        if (lastchance && (uO.qflag < 3)) {
            if (G.no_ecrec)
                Info(slide, 1,
                     ((char *) slide, LoadFarString(CannotFindZipfileDirMsg),
                      LoadFarStringSmall((uO.zipinfo_mode ? Zipnfo : Unzip)),
                      G.wildzipfn, uO.zipinfo_mode ? "  " : "", G.wildzipfn,
                      G.zipfn));
            else
                Info(slide, 1,
                     ((char *) slide, LoadFarString(CannotFindEitherZipfile),
                      LoadFarStringSmall((uO.zipinfo_mode ? Zipnfo : Unzip)),
                      G.wildzipfn, G.wildzipfn, G.zipfn));
        }
        return error ? IZ_DIR : PK_NOZIP;
    }
    G.ziplen = G.statbuf.st_size;

    if (G.statbuf.st_mode & S_IEXEC) /* no extension on Unix exes:  might */
        maybe_exe = TRUE;            /*  find unzip, not unzip.zip; etc. */

    if (open_input_file()) /* this should never happen, given */
        return PK_NOZIP;   /*  the stat() test above, but... */

    /* Need more care: Do not trust the size returned by stat() but
       determine it by reading beyond the end of the file. */
    G.ziplen = file_size(G.zipfd);

    if (G.ziplen == EOF) {
        Info(slide, 0x401, ((char *) slide, LoadFarString(ZipfileTooBig)));
        /*
        printf(
" We need a better error message for: 64-bit file, 32-bit program.\n");
        */
        CLOSE_INFILE();
        return IZ_ERRBF;
    }

    /*---------------------------------------------------------------------------
        Find and process the end-of-central-directory header.  UnZip need only
        check last 65557 bytes of zipfile:  comment may be up to 65535, end-of-
        central-directory record is 18 bytes, and signature itself is 4 bytes;
        add some to allow for appended garbage.  Since ZipInfo is often used as
        a debugging tool, search the whole zipfile if zipinfo_mode is true.
      ---------------------------------------------------------------------------*/

    G.cur_zipfile_bufstart = 0;
    G.inptr = G.inbuf;

    if ((!uO.zipinfo_mode && !uO.qflag && !uO.T_flag))
        Info(slide, 0, ((char *) slide, LoadFarString(LogInitline), G.zipfn));

    if ((error_in_archive = find_ecrec(MIN(G.ziplen, 66000L))) > PK_WARN) {
        CLOSE_INFILE();

        if (maybe_exe)
            Info(slide, 0x401,
                 ((char *) slide, LoadFarString(MaybeExe), G.zipfn));
        if (lastchance)
            return error_in_archive;
        else {
            G.no_ecrec = TRUE; /* assume we found wrong file:  e.g., */
            return PK_NOZIP;   /*  unzip instead of unzip.zip */
        }
    }

    if ((uO.zflag > 0) && !uO.zipinfo_mode) { /* unzip: zflag = comment ONLY */
        CLOSE_INFILE();
        return error_in_archive;
    }

    /*---------------------------------------------------------------------------
        Test the end-of-central-directory info for incompatibilities (multi-disk
        archives) or inconsistencies (missing or extra bytes in zipfile).
      ---------------------------------------------------------------------------*/

    error = !uO.zipinfo_mode && (G.ecrec.number_this_disk != 0);

    if (uO.zipinfo_mode &&
        G.ecrec.number_this_disk != G.ecrec.num_disk_start_cdir) {
        if (G.ecrec.number_this_disk > G.ecrec.num_disk_start_cdir) {
            Info(slide, 0x401,
                 ((char *) slide, LoadFarString(CentDirNotInZipMsg), G.zipfn,
                  (ulg) G.ecrec.number_this_disk,
                  (ulg) G.ecrec.num_disk_start_cdir));
            error_in_archive = PK_FIND;
            too_weird_to_continue = TRUE;
        } else {
            Info(slide, 0x401,
                 ((char *) slide, LoadFarString(EndCentDirBogus), G.zipfn,
                  (ulg) G.ecrec.number_this_disk,
                  (ulg) G.ecrec.num_disk_start_cdir));
            error_in_archive = PK_WARN;
        }
    }

    if (!too_weird_to_continue) { /* (relatively) normal zipfile:  go for it */
        if (error) {
            Info(slide, 0x401,
                 ((char *) slide, LoadFarString(MaybePakBug), G.zipfn));
            error_in_archive = PK_WARN;
        }
        if ((G.extra_bytes = G.real_ecrec_offset - G.expect_ecrec_offset) <
            (zoff_t) 0) {
            Info(slide, 0x401,
                 ((char *) slide, LoadFarString(MissingBytes), G.zipfn,
                  FmZofft((-G.extra_bytes), NULL, NULL)));
            error_in_archive = PK_ERR;
        } else if (G.extra_bytes > 0) {
            if ((G.ecrec.offset_start_central_directory == 0) &&
                (G.ecrec.size_central_directory != 0)) /* zip 1.5 -go bug */
            {
                Info(slide, 0x401,
                     ((char *) slide, LoadFarString(NullCentDirOffset),
                      G.zipfn));
                G.ecrec.offset_start_central_directory = G.extra_bytes;
                G.extra_bytes = 0;
                error_in_archive = PK_ERR;
            } else {
                Info(slide, 0x401,
                     ((char *) slide, LoadFarString(ExtraBytesAtStart), G.zipfn,
                      FmZofft(G.extra_bytes, NULL, NULL),
                      (G.extra_bytes == 1) ? "" : "s"));
                error_in_archive = PK_WARN;
            }
        }

        /*-----------------------------------------------------------------------
            Check for empty zipfile and exit now if so.
          -----------------------------------------------------------------------*/

        if (G.expect_ecrec_offset == 0L &&
            G.ecrec.size_central_directory == 0) {
            if (uO.zipinfo_mode)
                Info(slide, 0,
                     ((char *) slide, "%sEmpty zipfile.\n",
                      uO.lflag > 9 ? "\n  " : ""));
            else
                Info(slide, 0x401,
                     ((char *) slide, LoadFarString(ZipfileEmpty), G.zipfn));
            CLOSE_INFILE();
            return (error_in_archive > PK_WARN) ? error_in_archive : PK_WARN;
        }

        /*-----------------------------------------------------------------------
            Compensate for missing or extra bytes, and seek to where the start
            of central directory should be.  If header not found, uncompensate
            and try again (necessary for at least some Atari archives created
            with STZip, as well as archives created by J.H. Holm's
          ZIPSPLIT 1.1).
          -----------------------------------------------------------------------*/

        error = seek_zipf(G.ecrec.offset_start_central_directory);
        if (error == PK_BADERR) {
            CLOSE_INFILE();
            return PK_BADERR;
        }
        if ((error != PK_OK) || (readbuf(G.sig, 4) == 0) ||
            memcmp(G.sig, central_hdr_sig, 4)) {
            zoff_t tmp = G.extra_bytes;

            G.extra_bytes = 0;
            error = seek_zipf(G.ecrec.offset_start_central_directory);
            if ((error != PK_OK) || (readbuf(G.sig, 4) == 0) ||
                memcmp(G.sig, central_hdr_sig, 4)) {
                if (error != PK_BADERR)
                    Info(slide, 0x401,
                         ((char *) slide, LoadFarString(CentDirStartNotFound),
                          G.zipfn, LoadFarStringSmall(ReportMsg)));
                CLOSE_INFILE();
                return (error != PK_OK ? error : PK_BADERR);
            }
            Info(slide, 0x401,
                 ((char *) slide, LoadFarString(CentDirTooLong), G.zipfn,
                  FmZofft((-tmp), NULL, NULL)));
            error_in_archive = PK_ERR;
        }

        /*-----------------------------------------------------------------------
            Seek to the start of the central directory one last time, since we
            have just read the first entry's signature bytes; then list, extract
            or test member files as instructed, and close the zipfile.
          -----------------------------------------------------------------------*/

        error = seek_zipf(G.ecrec.offset_start_central_directory);
        if (error != PK_OK) {
            CLOSE_INFILE();
            return error;
        }

        Trace((stderr, "about to extract/list files (error = %d)\n",
               error_in_archive));

        {
            if (uO.T_flag)
                error = get_time_stamp(&uxstamp, &nmember);
            else if (uO.vflag && !uO.tflag && !uO.cflag)
                error = list_files(); /* LIST 'EM */
            else
                error = extract_or_test_files(); /* EXTRACT OR TEST 'EM */

            Trace(
                (stderr, "done with extract/list files (error = %d)\n", error));
        }

        if (error > error_in_archive) /* don't overwrite stronger error */
            error_in_archive = error; /*  with (for example) a warning */
    }                                 /* end if (!too_weird_to_continue) */

    CLOSE_INFILE();

    if (uO.T_flag && !uO.zipinfo_mode && (nmember > 0L)) {
        if (stamp_file(G.zipfn, uxstamp)) { /* TIME-STAMP 'EM */
            if (uO.qflag < 3)
                Info(slide, 0x201,
                     ((char *) slide, LoadFarString(ZipTimeStampFailed),
                      G.zipfn));
            if (error_in_archive < PK_WARN)
                error_in_archive = PK_WARN;
        } else {
            if (!uO.qflag)
                Info(slide, 0,
                     ((char *) slide, LoadFarString(ZipTimeStampSuccess),
                      G.zipfn));
        }
    }
    return error_in_archive;

} /* end function do_seekable() */

/************************/
/* Function file_size() */
/************************/
/* File size determination which does not mislead for large files in a
   small-file program.  Probably should be somewhere else.
   The file has to be opened previously
*/
static zoff_t file_size(fh)
int fh;
{
    int siz;
    zoff_t ofs;
    char waste[4];

    /* Seek to actual EOF. */
    ofs = lseek(fh, 0, SEEK_END);
    if (ofs == (zoff_t) -1) {
        /* lseek() failed.  (Unlikely.) */
        ofs = EOF;
    } else if (ofs < 0) {
        /* Offset negative (overflow).  File too big. */
        ofs = EOF;
    } else {
        /* Seek to apparent EOF offset.
           Won't be at actual EOF if offset was truncated.
        */
        ofs = lseek(fh, ofs, SEEK_SET);
        if (ofs == (zoff_t) -1) {
            /* lseek() failed.  (Unlikely.) */
            ofs = EOF;
        } else {
            /* Read a byte at apparent EOF.  Should set EOF flag. */
            siz = read(fh, waste, 1);
            if (siz != 0) {
                /* Not at EOF, but should be.  File too big. */
                ofs = EOF;
            }
        }
    }
    return ofs;
} /* end function file_size() */

/***********************/
/* Function rec_find() */
/***********************/

static int rec_find(searchlen, signature, rec_size)
/* return 0 when rec found, 1 when not found, 2 in case of read error */
zoff_t searchlen;
char *signature;
int rec_size;
{
    int i, numblks, found = FALSE;
    zoff_t tail_len;

    /*---------------------------------------------------------------------------
        Zipfile is longer than INBUFSIZ:  may need to loop.  Start with short
        block at end of zipfile (if not TOO short).
      ---------------------------------------------------------------------------*/

    if ((tail_len = G.ziplen % INBUFSIZ) > rec_size) {
        G.cur_zipfile_bufstart = lseek(G.zipfd, G.ziplen - tail_len, SEEK_SET);
        if ((G.incnt = read(G.zipfd, (char *) G.inbuf,
                            (unsigned int) tail_len)) != (int) tail_len)
            return 2; /* it's expedient... */

        /* 'P' must be at least (rec_size+4) bytes from end of zipfile */
        for (G.inptr = G.inbuf + (int) tail_len - (rec_size + 4);
             G.inptr >= G.inbuf; --G.inptr) {
            if ((*G.inptr == (uch) 0x50) && /* ASCII 'P' */
                !memcmp((char *) G.inptr, signature, 4)) {
                G.incnt -= (int) (G.inptr - G.inbuf);
                found = TRUE;
                break;
            }
        }
        /* sig may span block boundary: */
        memcpy((char *) G.hold, (char *) G.inbuf, 3);
    } else
        G.cur_zipfile_bufstart = G.ziplen - tail_len;

    /*-----------------------------------------------------------------------
        Loop through blocks of zipfile data, starting at the end and going
        toward the beginning.  In general, need not check whole zipfile for
        signature, but may want to do so if testing.
      -----------------------------------------------------------------------*/

    numblks = (int) ((searchlen - tail_len + (INBUFSIZ - 1)) / INBUFSIZ);
    /*               ==amount=   ==done==   ==rounding==    =blksiz=  */

    for (i = 1; !found && (i <= numblks); ++i) {
        G.cur_zipfile_bufstart -= INBUFSIZ;
        lseek(G.zipfd, G.cur_zipfile_bufstart, SEEK_SET);
        if ((G.incnt = read(G.zipfd, (char *) G.inbuf, INBUFSIZ)) != INBUFSIZ)
            return 2; /* read error is fatal failure */

        for (G.inptr = G.inbuf + INBUFSIZ - 1; G.inptr >= G.inbuf; --G.inptr)
            if ((*G.inptr == (uch) 0x50) && /* ASCII 'P' */
                !memcmp((char *) G.inptr, signature, 4)) {
                G.incnt -= (int) (G.inptr - G.inbuf);
                found = TRUE;
                break;
            }
        /* sig may span block boundary: */
        memcpy((char *) G.hold, (char *) G.inbuf, 3);
    }
    return (found ? 0 : 1);
} /* end function rec_find() */

#if 0
/********************************/
/* Function check_ecrec_zip64() */
/********************************/

static int check_ecrec_zip64()
{
    return G.ecrec.offset_start_central_directory  == 0xFFFFFFFFL
        || G.ecrec.size_central_directory          == 0xFFFFFFFFL
        || G.ecrec.total_entries_central_dir       == 0xFFFF
        || G.ecrec.num_entries_centrl_dir_ths_disk == 0xFFFF
        || G.ecrec.num_disk_start_cdir             == 0xFFFF
        || G.ecrec.number_this_disk                == 0xFFFF;
} /* end function check_ecrec_zip64() */
#endif /* never */

/***************************/
/* Function find_ecrec64() */
/***************************/

static int find_ecrec64(searchlen) /* return PK-class error */
zoff_t searchlen;
{
    ec_byte_rec64 byterec;       /* buf for ecrec64 */
    ec_byte_loc64 byterecL;      /* buf for ecrec64 locator */
    zoff_t ecloc64_start_offset; /* start offset of ecrec64 locator */
    zusz_t ecrec64_start_offset; /* start offset of ecrec64 */
    zuvl_t ecrec64_start_disk;   /* start disk of ecrec64 */
    zuvl_t ecloc64_total_disks;  /* total disks */
    zuvl_t ecrec64_disk_cdstart; /* disk number of central dir start */
    zucn_t ecrec64_this_entries; /* entries on disk with ecrec64 */
    zucn_t ecrec64_tot_entries;  /* total number of entries */
    zusz_t ecrec64_cdirsize;     /* length of central dir */
    zusz_t ecrec64_offs_cdstart; /* offset of central dir start */

    /* First, find the ecrec64 locator.  By definition, this must be before
       ecrec with nothing in between.  We back up the size of the ecrec64
       locator and check.  */

    ecloc64_start_offset = G.real_ecrec_offset - (ECLOC64_SIZE + 4);
    if (ecloc64_start_offset < 0)
        /* Seeking would go past beginning, so probably empty archive */
        return PK_COOL;

    G.cur_zipfile_bufstart = lseek(G.zipfd, ecloc64_start_offset, SEEK_SET);

    if ((G.incnt = read(G.zipfd, (char *) byterecL, ECLOC64_SIZE + 4)) !=
        (ECLOC64_SIZE + 4)) {
        if (uO.qflag || uO.zipinfo_mode)
            Info(slide, 0x401, ((char *) slide, "[%s]\n", G.zipfn));
        Info(slide, 0x401,
             ((char *) slide, LoadFarString(Cent64EndSigSearchErr)));
        return PK_ERR;
    }

    if (memcmp((char *) byterecL, end_centloc64_sig, 4)) {
        /* not found */
        return PK_COOL;
    }

    /* Read the locator. */
    ecrec64_start_disk = (zuvl_t) makelong(&byterecL[NUM_DISK_START_EOCDR64]);
    ecrec64_start_offset = (zusz_t) makeint64(&byterecL[OFFSET_START_EOCDR64]);
    ecloc64_total_disks = (zuvl_t) makelong(&byterecL[NUM_THIS_DISK_LOC64]);

    /* Check for consistency */
#ifdef TEST
    fprintf(stdout, "\nnumber of disks (ECR) %u, (ECLOC64) %lu\n",
            G.ecrec.number_this_disk, ecloc64_total_disks);
    fflush(stdout);
#endif
    if ((G.ecrec.number_this_disk != 0xFFFF) &&
        (G.ecrec.number_this_disk != ecloc64_total_disks - 1)) {
        /* Note: For some unknown reason, the developers at PKWARE decided to
           store the "zip64 total disks" value as a counter starting from 1,
           whereas all other "split/span volume" related fields use 0-based
           volume numbers. Sigh... */
        /* When the total number of disks as found in the traditional ecrec
           is not 0xFFFF, the disk numbers in ecrec and ecloc64 must match.
           When this is not the case, the found ecrec64 locator cannot be valid.
           -> This is not a Zip64 archive.
         */
        Trace((stderr,
               "\ninvalid ECLOC64, differing disk# (ECR %u, ECL64 %lu)\n",
               G.ecrec.number_this_disk, ecloc64_total_disks - 1));
        return PK_COOL;
    }

    /* If found locator, look for ecrec64 where the locator says it is. */

    /* For now assume that ecrec64 is on the same disk as ecloc64 and ecrec,
       which is usually the case and is how Zip writes it.  To do this right,
       however, we should allow the ecrec64 to be on another disk since
       the AppNote allows it and the ecrec64 can be large, especially if
       Version 2 is used (AppNote uses 8 bytes for the size of this record). */

    /* FIX BELOW IF ADD SUPPORT FOR MULTIPLE DISKS */

    if (ecrec64_start_offset > (zusz_t) ecloc64_start_offset) {
        /* ecrec64 has to be before ecrec64 locator */
        if (uO.qflag || uO.zipinfo_mode)
            Info(slide, 0x401, ((char *) slide, "[%s]\n", G.zipfn));
        Info(slide, 0x401,
             ((char *) slide, LoadFarString(Cent64EndSigSearchErr)));
        return PK_ERR;
    }

    G.cur_zipfile_bufstart = lseek(G.zipfd, ecrec64_start_offset, SEEK_SET);

    if ((G.incnt = read(G.zipfd, (char *) byterec, ECREC64_SIZE + 4)) !=
        (ECREC64_SIZE + 4)) {
        if (uO.qflag || uO.zipinfo_mode)
            Info(slide, 0x401, ((char *) slide, "[%s]\n", G.zipfn));
        Info(slide, 0x401,
             ((char *) slide, LoadFarString(Cent64EndSigSearchErr)));
        return PK_ERR;
    }

    if (memcmp((char *) byterec, end_central64_sig, 4)) {
        /* Zip64 EOCD Record not found */
        /* Since we already have seen the Zip64 EOCD Locator, it's
           possible we got here because there are bytes prepended
           to the archive, like the sfx prefix. */

        /* Make a guess as to where the Zip64 EOCD Record might be */
        ecrec64_start_offset = ecloc64_start_offset - ECREC64_SIZE - 4;

        G.cur_zipfile_bufstart = lseek(G.zipfd, ecrec64_start_offset, SEEK_SET);

        if ((G.incnt = read(G.zipfd, (char *) byterec, ECREC64_SIZE + 4)) !=
            (ECREC64_SIZE + 4)) {
            if (uO.qflag || uO.zipinfo_mode)
                Info(slide, 0x401, ((char *) slide, "[%s]\n", G.zipfn));
            Info(slide, 0x401,
                 ((char *) slide, LoadFarString(Cent64EndSigSearchErr)));
            return PK_ERR;
        }

        if (memcmp((char *) byterec, end_central64_sig, 4)) {
            /* Zip64 EOCD Record not found */
            /* Probably something not so easy to handle so exit */
            if (uO.qflag || uO.zipinfo_mode)
                Info(slide, 0x401, ((char *) slide, "[%s]\n", G.zipfn));
            Info(slide, 0x401,
                 ((char *) slide, LoadFarString(Cent64EndSigSearchErr)));
            return PK_ERR;
        }

        if (uO.qflag || uO.zipinfo_mode)
            Info(slide, 0x401, ((char *) slide, "[%s]\n", G.zipfn));
        Info(slide, 0x401,
             ((char *) slide, LoadFarString(Cent64EndSigSearchOff)));
    }

    /* Check consistency of found ecrec64 with ecloc64 (and ecrec): */
    if ((zuvl_t) makelong(&byterec[NUMBER_THIS_DSK_REC64]) !=
        ecrec64_start_disk)
        /* found ecrec64 does not match ecloc64 info -> no Zip64 archive */
        return PK_COOL;
    /* Read all relevant ecrec64 fields and compare them to the corresponding
       ecrec fields unless those are set to "all-ones".
     */
    ecrec64_disk_cdstart =
        (zuvl_t) makelong(&byterec[NUM_DISK_START_CEN_DIR64]);
    if ((G.ecrec.num_disk_start_cdir != 0xFFFF) &&
        (G.ecrec.num_disk_start_cdir != ecrec64_disk_cdstart))
        return PK_COOL;
    ecrec64_this_entries = makeint64(&byterec[NUM_ENTRIES_CEN_DIR_THS_DISK64]);
    if ((G.ecrec.num_entries_centrl_dir_ths_disk != 0xFFFF) &&
        (G.ecrec.num_entries_centrl_dir_ths_disk != ecrec64_this_entries))
        return PK_COOL;
    ecrec64_tot_entries = makeint64(&byterec[TOTAL_ENTRIES_CENTRAL_DIR64]);
    if ((G.ecrec.total_entries_central_dir != 0xFFFF) &&
        (G.ecrec.total_entries_central_dir != ecrec64_tot_entries))
        return PK_COOL;
    ecrec64_cdirsize = makeint64(&byterec[SIZE_CENTRAL_DIRECTORY64]);
    if ((G.ecrec.size_central_directory != 0xFFFFFFFFL) &&
        (G.ecrec.size_central_directory != ecrec64_cdirsize))
        return PK_COOL;
    ecrec64_offs_cdstart = makeint64(&byterec[OFFSET_START_CENTRAL_DIRECT64]);
    if ((G.ecrec.offset_start_central_directory != 0xFFFFFFFFL) &&
        (G.ecrec.offset_start_central_directory != ecrec64_offs_cdstart))
        return PK_COOL;

    /* Now, we are (almost) sure that we have a Zip64 archive. */
    G.ecrec.have_ecr64 = 1;
    G.ecrec.ec_start -= ECLOC64_SIZE + 4;
    G.ecrec.ec64_start = ecrec64_start_offset;
    G.ecrec.ec64_end =
        ecrec64_start_offset + 12 + makeint64(&byterec[ECREC64_LENGTH]);

    /* Update the "end-of-central-dir offset" for later checks. */
    G.real_ecrec_offset = ecrec64_start_offset;

    /* Update all ecdir_rec data that are flagged to be invalid
       in Zip64 mode.  Set the ecrec64-mandatory flag when such a
       case is found. */
    if (G.ecrec.number_this_disk == 0xFFFF) {
        G.ecrec.number_this_disk = ecrec64_start_disk;
        if (ecrec64_start_disk != 0xFFFF)
            G.ecrec.is_zip64_archive = TRUE;
    }
    if (G.ecrec.num_disk_start_cdir == 0xFFFF) {
        G.ecrec.num_disk_start_cdir = ecrec64_disk_cdstart;
        if (ecrec64_disk_cdstart != 0xFFFF)
            G.ecrec.is_zip64_archive = TRUE;
    }
    if (G.ecrec.num_entries_centrl_dir_ths_disk == 0xFFFF) {
        G.ecrec.num_entries_centrl_dir_ths_disk = ecrec64_this_entries;
        if (ecrec64_this_entries != 0xFFFF)
            G.ecrec.is_zip64_archive = TRUE;
    }
    if (G.ecrec.total_entries_central_dir == 0xFFFF) {
        G.ecrec.total_entries_central_dir = ecrec64_tot_entries;
        if (ecrec64_tot_entries != 0xFFFF)
            G.ecrec.is_zip64_archive = TRUE;
    }
    if (G.ecrec.size_central_directory == 0xFFFFFFFFL) {
        G.ecrec.size_central_directory = ecrec64_cdirsize;
        if (ecrec64_cdirsize != 0xFFFFFFFF)
            G.ecrec.is_zip64_archive = TRUE;
    }
    if (G.ecrec.offset_start_central_directory == 0xFFFFFFFFL) {
        G.ecrec.offset_start_central_directory = ecrec64_offs_cdstart;
        if (ecrec64_offs_cdstart != 0xFFFFFFFF)
            G.ecrec.is_zip64_archive = TRUE;
    }

    return PK_COOL;
} /* end function find_ecrec64() */

/*************************/
/* Function find_ecrec() */
/*************************/

static int find_ecrec(searchlen) /* return PK-class error */
zoff_t searchlen;
{
    int found = FALSE;
    int error_in_archive;
    int result;
    ec_byte_rec byterec;

    /*---------------------------------------------------------------------------
        Treat case of short zipfile separately.
      ---------------------------------------------------------------------------*/

    if (G.ziplen <= INBUFSIZ) {
        lseek(G.zipfd, 0L, SEEK_SET);
        if ((G.incnt = read(G.zipfd, (char *) G.inbuf,
                            (unsigned int) G.ziplen)) == (int) G.ziplen)

            /* 'P' must be at least (ECREC_SIZE+4) bytes from end of zipfile */
            for (G.inptr = G.inbuf + (int) G.ziplen - (ECREC_SIZE + 4);
                 G.inptr >= G.inbuf; --G.inptr) {
                if ((*G.inptr == (uch) 0x50) && /* ASCII 'P' */
                    !memcmp((char *) G.inptr, end_central_sig, 4)) {
                    G.incnt -= (int) (G.inptr - G.inbuf);
                    found = TRUE;
                    break;
                }
            }

        /*---------------------------------------------------------------------------
            Zipfile is longer than INBUFSIZ:

            MB - this next block of code moved to rec_find so that same code can
          be used to look for zip64 ec record.  No need to include code above
          since a zip64 ec record will only be looked for if it is a BIG file.
          ---------------------------------------------------------------------------*/

    } else {
        found = (rec_find(searchlen, end_central_sig, ECREC_SIZE) == 0 ? TRUE
                                                                       : FALSE);
    } /* end if (ziplen > INBUFSIZ) */

    /*---------------------------------------------------------------------------
        Searched through whole region where signature should be without finding
        it.  Print informational message and die a horrible death.
      ---------------------------------------------------------------------------*/

    if (!found) {
        if (uO.qflag || uO.zipinfo_mode)
            Info(slide, 0x401, ((char *) slide, "[%s]\n", G.zipfn));
        Info(slide, 0x401,
             ((char *) slide, LoadFarString(CentDirEndSigNotFound)));
        return PK_ERR; /* failed */
    }

    /*---------------------------------------------------------------------------
        Found the signature, so get the end-central data before returning.  Do
        any necessary machine-type conversions (byte ordering, structure padding
        compensation) by reading data into character array and copying to
      struct.
      ---------------------------------------------------------------------------*/

    G.real_ecrec_offset = G.cur_zipfile_bufstart + (G.inptr - G.inbuf);
#ifdef TEST
    printf("\n  found end-of-central-dir signature at offset %s (%sh)\n",
           FmZofft(G.real_ecrec_offset, NULL, NULL),
           FmZofft(G.real_ecrec_offset, FZOFFT_HEX_DOT_WID, "X"));
    printf("    from beginning of file; offset %d (%.4Xh) within block\n",
           G.inptr - G.inbuf, G.inptr - G.inbuf);
#endif

    if (readbuf((char *) byterec, ECREC_SIZE + 4) == 0)
        return PK_EOF;

    G.ecrec.number_this_disk = makeword(&byterec[NUMBER_THIS_DISK]);
    G.ecrec.num_disk_start_cdir =
        makeword(&byterec[NUM_DISK_WITH_START_CEN_DIR]);
    G.ecrec.num_entries_centrl_dir_ths_disk =
        makeword(&byterec[NUM_ENTRIES_CEN_DIR_THS_DISK]);
    G.ecrec.total_entries_central_dir =
        makeword(&byterec[TOTAL_ENTRIES_CENTRAL_DIR]);
    G.ecrec.size_central_directory = makelong(&byterec[SIZE_CENTRAL_DIRECTORY]);
    G.ecrec.offset_start_central_directory =
        makelong(&byterec[OFFSET_START_CENTRAL_DIRECTORY]);
    G.ecrec.zipfile_comment_length = makeword(&byterec[ZIPFILE_COMMENT_LENGTH]);
    G.ecrec.ec_start = G.real_ecrec_offset;
    G.ecrec.ec_end = G.ecrec.ec_start + 22 + G.ecrec.zipfile_comment_length;

    /* Now, we have to read the archive comment, BEFORE the file pointer
       is moved away backwards to seek for a Zip64 ECLOC64 structure.
     */
    if ((error_in_archive = process_zip_cmmnt()) > PK_WARN)
        return error_in_archive;

    /* Next: Check for existence of Zip64 end-of-cent-dir locator
       ECLOC64. This structure must reside on the same volume as the
       classic ECREC, at exactly (ECLOC64_SIZE+4) bytes in front
       of the ECREC.
       The ECLOC64 structure directs to the longer ECREC64 structure
       A ECREC64 will ALWAYS exist for a proper Zip64 archive, as
       the "Version Needed To Extract" field is required to be set
       to 4.5 or higher whenever any Zip64 features are used anywhere
       in the archive, so just check for that to see if this is a
       Zip64 archive.
     */
    result = find_ecrec64(searchlen + 76);
    /* 76 bytes for zip64ec & zip64 locator */
    if (result != PK_COOL) {
        if (error_in_archive < result)
            error_in_archive = result;
        return error_in_archive;
    }

    G.expect_ecrec_offset =
        G.ecrec.offset_start_central_directory + G.ecrec.size_central_directory;

    return error_in_archive;

} /* end function find_ecrec() */

/********************************/
/* Function process_zip_cmmnt() */
/********************************/

static int process_zip_cmmnt() /* return PK-type error code */
{
    int error = PK_COOL;

    /*---------------------------------------------------------------------------
        Get the zipfile comment (up to 64KB long), if any, and print it out.
      ---------------------------------------------------------------------------*/

    if (G.ecrec.zipfile_comment_length &&
        (uO.zflag > 0 || (uO.zflag == 0 && !uO.T_flag && !uO.qflag))) {
        if (do_string(G.ecrec.zipfile_comment_length, DISPLAY)) {
            Info(slide, 0x401,
                 ((char *) slide, LoadFarString(ZipfileCommTrunc1)));
            error = PK_WARN;
        }
    }
    return error;

} /* end function process_zip_cmmnt() */

/************************************/
/* Function process_cdir_file_hdr() */
/************************************/

int process_cdir_file_hdr() /* return PK-type error code */
{
    int error;

    /*---------------------------------------------------------------------------
        Get central directory info, save host and method numbers, and set flag
        for lowercase conversion of filename, depending on the OS from which the
        file is coming.
      ---------------------------------------------------------------------------*/

    if ((error = get_cdir_ent()) != 0)
        return error;

    G.pInfo->hostver = G.crec.version_made_by[0];
    G.pInfo->hostnum = MIN(G.crec.version_made_by[1], NUM_HOSTS);
    /*  extnum = MIN(crec.version_needed_to_extract[1], NUM_HOSTS); */

    G.pInfo->lcflag = 0;
    if (uO.L_flag == 1) /* name conversion for monocase systems */
        switch (G.pInfo->hostnum) {
        case FS_FAT_: /* PKZIP and zip -k store in uppercase */
        case CPM_:    /* like MS-DOS, right? */
        case VM_CMS_: /* all caps? */
        case MVS_:    /* all caps? */
        case TANDEM_:
        case TOPS20_:
        case VMS_:               /* our Zip uses lowercase, but ASi's doesn't */
                                 /*  case Z_SYSTEM_:   ? */
                                 /*  case QDOS_:       ? */
            G.pInfo->lcflag = 1; /* convert filename to lowercase */
            break;

        default:   /* AMIGA_, FS_HPFS_, FS_NTFS_, MAC_, UNIX_, ATARI_, */
            break; /*  FS_VFAT_, ATHEOS_, BEOS_ (Z_SYSTEM_), THEOS_: */
                   /*  no conversion */
        }
    else if (uO.L_flag > 1) /* let -LL force lower case for all names */
        G.pInfo->lcflag = 1;

    /* Handle the PKWare verification bit, bit 2 (0x0004) of internal
       attributes.  If this is set, then a verification checksum is in the
       first 3 bytes of the external attributes.  In this case all we can use
       for setting file attributes is the last external attributes byte. */
    if (G.crec.internal_file_attributes & 0x0004)
        G.crec.external_file_attributes &= (ulg) 0xff;

    /* do Amigas (AMIGA_) also have volume labels? */
    if (IS_VOLID(G.crec.external_file_attributes) &&
        (G.pInfo->hostnum == FS_FAT_ || G.pInfo->hostnum == FS_HPFS_ ||
         G.pInfo->hostnum == FS_NTFS_ || G.pInfo->hostnum == ATARI_)) {
        G.pInfo->vollabel = TRUE;
        G.pInfo->lcflag = 0; /* preserve case of volume labels */
    } else
        G.pInfo->vollabel = FALSE;

    /* this flag is needed to detect archives made by "PKZIP for Unix" when
       deciding which kind of codepage conversion has to be applied to
       strings (see do_string() function in fileio.c) */
    G.pInfo->HasUxAtt = (G.crec.external_file_attributes & 0xffff0000L) != 0L;

    /* remember the state of GPB11 (General Purpuse Bit 11) which indicates
       that the standard path and comment are UTF-8. */
    G.pInfo->GPFIsUTF8 =
        (G.crec.general_purpose_bit_flag & (1 << 11)) == (1 << 11);

    /* Initialize the symlink flag, may be set by the platform-specific
       mapattr function.  */
    G.pInfo->symlink = 0;

    return PK_COOL;

} /* end function process_cdir_file_hdr() */

/***************************/
/* Function get_cdir_ent() */
/***************************/

static int get_cdir_ent() /* return PK-type error code */
{
    cdir_byte_hdr byterec;

    /*---------------------------------------------------------------------------
        Read the next central directory entry and do any necessary machine-type
        conversions (byte ordering, structure padding compensation--do so by
        copying the data from the array into which it was read (byterec) to the
        usable struct (crec)).
      ---------------------------------------------------------------------------*/

    if (readbuf((char *) byterec, CREC_SIZE) == 0)
        return PK_EOF;

    G.crec.version_made_by[0] = byterec[C_VERSION_MADE_BY_0];
    G.crec.version_made_by[1] = byterec[C_VERSION_MADE_BY_1];
    G.crec.version_needed_to_extract[0] =
        byterec[C_VERSION_NEEDED_TO_EXTRACT_0];
    G.crec.version_needed_to_extract[1] =
        byterec[C_VERSION_NEEDED_TO_EXTRACT_1];

    G.crec.general_purpose_bit_flag =
        makeword(&byterec[C_GENERAL_PURPOSE_BIT_FLAG]);
    G.crec.compression_method = makeword(&byterec[C_COMPRESSION_METHOD]);
    G.crec.last_mod_dos_datetime = makelong(&byterec[C_LAST_MOD_DOS_DATETIME]);
    G.crec.crc32 = makelong(&byterec[C_CRC32]);
    G.crec.csize = makelong(&byterec[C_COMPRESSED_SIZE]);
    G.crec.ucsize = makelong(&byterec[C_UNCOMPRESSED_SIZE]);
    G.crec.filename_length = makeword(&byterec[C_FILENAME_LENGTH]);
    G.crec.extra_field_length = makeword(&byterec[C_EXTRA_FIELD_LENGTH]);
    G.crec.file_comment_length = makeword(&byterec[C_FILE_COMMENT_LENGTH]);
    G.crec.disk_number_start = makeword(&byterec[C_DISK_NUMBER_START]);
    G.crec.internal_file_attributes =
        makeword(&byterec[C_INTERNAL_FILE_ATTRIBUTES]);
    G.crec.external_file_attributes =
        makelong(&byterec[C_EXTERNAL_FILE_ATTRIBUTES]); /* LONG, not word! */
    G.crec.relative_offset_local_header =
        makelong(&byterec[C_RELATIVE_OFFSET_LOCAL_HEADER]);

    return PK_COOL;

} /* end function get_cdir_ent() */

/*************************************/
/* Function process_local_file_hdr() */
/*************************************/

int process_local_file_hdr() /* return PK-type error code */
{
    local_byte_hdr byterec;

    /*---------------------------------------------------------------------------
        Read the next local file header and do any necessary machine-type con-
        versions (byte ordering, structure padding compensation--do so by copy-
        ing the data from the array into which it was read (byterec) to the
        usable struct (lrec)).
      ---------------------------------------------------------------------------*/

    if (readbuf((char *) byterec, LREC_SIZE) == 0)
        return PK_EOF;

    G.lrec.version_needed_to_extract[0] =
        byterec[L_VERSION_NEEDED_TO_EXTRACT_0];
    G.lrec.version_needed_to_extract[1] =
        byterec[L_VERSION_NEEDED_TO_EXTRACT_1];

    G.lrec.general_purpose_bit_flag =
        makeword(&byterec[L_GENERAL_PURPOSE_BIT_FLAG]);
    G.lrec.compression_method = makeword(&byterec[L_COMPRESSION_METHOD]);
    G.lrec.last_mod_dos_datetime = makelong(&byterec[L_LAST_MOD_DOS_DATETIME]);
    G.lrec.crc32 = makelong(&byterec[L_CRC32]);
    G.lrec.csize = makelong(&byterec[L_COMPRESSED_SIZE]);
    G.lrec.ucsize = makelong(&byterec[L_UNCOMPRESSED_SIZE]);
    G.lrec.filename_length = makeword(&byterec[L_FILENAME_LENGTH]);
    G.lrec.extra_field_length = makeword(&byterec[L_EXTRA_FIELD_LENGTH]);

    if ((G.lrec.general_purpose_bit_flag & 8) != 0) {
        /* can't trust local header, use central directory: */
        G.lrec.crc32 = G.pInfo->crc;
        G.lrec.csize = G.pInfo->compr_size;
        G.lrec.ucsize = G.pInfo->uncompr_size;
    }

    G.csize = G.lrec.csize;

    return PK_COOL;

} /* end function process_local_file_hdr() */

/*******************************/
/* Function getZip64Data() */
/*******************************/

int getZip64Data(ef_buf,
                 ef_len) const uch *ef_buf; /* buffer containing extra field */
unsigned ef_len;                            /* total length of extra field */
{
    unsigned eb_id;
    unsigned eb_len;

    /*---------------------------------------------------------------------------
        This function scans the extra field for zip64 information, ie 8-byte
        versions of compressed file size, uncompressed file size, relative
      offset and a 4-byte version of disk start number. Sets both local header
      and central header fields.  Not terribly clever, but it means that this
      procedure is only called in one place.

        2014-12-05 SMS.  (oCERT.org report.)  CVE-2014-8141.
        Added checks to ensure that enough data are available before calling
        makeint64() or makelong().  Replaced various sizeof() values with
        simple ("4" or "8") constants.  (The Zip64 structures do not depend
        on our variable sizes.)  Error handling is crude, but we should now
        stay within the buffer.
      ---------------------------------------------------------------------------*/

#define Z64FLGS 0xffff
#define Z64FLGL 0xffffffff

    G.zip64 = FALSE;

    if (ef_len == 0 || ef_buf == NULL)
        return PK_COOL;

    Trace((stderr, "\ngetZip64Data: scanning extra field of length %u\n",
           ef_len));

    while (ef_len >= EB_HEADSIZE) {
        eb_id = makeword(EB_ID + ef_buf);
        eb_len = makeword(EB_LEN + ef_buf);

        if (eb_len > (ef_len - EB_HEADSIZE)) {
            /* Extra block length exceeds remaining extra field length. */
            Trace((stderr, "getZip64Data: block length %u > rest ef_size %u\n",
                   eb_len, ef_len - EB_HEADSIZE));
            break;
        }

        if (eb_id == EF_PKSZ64) {
            unsigned offset = EB_HEADSIZE;

            if ((G.crec.ucsize == Z64FLGL) || (G.lrec.ucsize == Z64FLGL)) {
                if (offset + 8 > ef_len)
                    return PK_ERR;

                G.crec.ucsize = G.lrec.ucsize = makeint64(offset + ef_buf);
                offset += 8;
            }

            if ((G.crec.csize == Z64FLGL) || (G.lrec.csize == Z64FLGL)) {
                if (offset + 8 > ef_len)
                    return PK_ERR;

                G.csize = G.crec.csize = G.lrec.csize =
                    makeint64(offset + ef_buf);
                offset += 8;
            }

            if (G.crec.relative_offset_local_header == Z64FLGL) {
                if (offset + 8 > ef_len)
                    return PK_ERR;

                G.crec.relative_offset_local_header =
                    makeint64(offset + ef_buf);
                offset += 8;
            }

            if (G.crec.disk_number_start == Z64FLGS) {
                if (offset + 4 > ef_len)
                    return PK_ERR;

                G.crec.disk_number_start = (zuvl_t) makelong(offset + ef_buf);
                offset += 4;
            }
#if 0
          break;                /* Expect only one EF_PKSZ64 block. */
#endif /* 0 */
        }

        /* Skip this extra field block. */
        ef_buf += (eb_len + EB_HEADSIZE);
        ef_len -= (eb_len + EB_HEADSIZE);
    }

    return PK_COOL;
} /* end function getZip64Data() */

/*******************************/
/* Function getUnicodeData() */
/*******************************/

int getUnicodeData(
    ef_buf, ef_len) const uch *ef_buf; /* buffer containing extra field */
unsigned ef_len;                       /* total length of extra field */
{
    unsigned eb_id;
    unsigned eb_len;

    /*---------------------------------------------------------------------------
        This function scans the extra field for Unicode information, ie UTF-8
        path extra fields.

        On return, G.unipath_filename =
            NULL, if no Unicode path extra field or error
            "", if the standard path is UTF-8 (free when done)
            null-terminated UTF-8 path (free when done)
        Return PK_COOL if no error.
      ---------------------------------------------------------------------------*/

    G.unipath_filename = NULL;

    if (ef_len == 0 || ef_buf == NULL)
        return PK_COOL;

    Trace((stderr, "\ngetUnicodeData: scanning extra field of length %u\n",
           ef_len));

    while (ef_len >= EB_HEADSIZE) {
        eb_id = makeword(EB_ID + ef_buf);
        eb_len = makeword(EB_LEN + ef_buf);

        if (eb_len > (ef_len - EB_HEADSIZE)) {
            /* discovered some extra field inconsistency! */
            Trace((stderr,
                   "getUnicodeData: block length %u > rest ef_size %u\n",
                   eb_len, ef_len - EB_HEADSIZE));
            break;
        }
        if (eb_id == EF_UNIPATH) {

            unsigned offset = EB_HEADSIZE;
            ush ULen = eb_len - 5;
            ulg chksum = CRCVAL_INITIAL;

            /* version */
            G.unipath_version = (uch) * (offset + ef_buf);
            offset += 1;
            if (G.unipath_version > 1) {
                /* can do only version 1 */
                Info(slide, 0x401,
                     ((char *) slide, LoadFarString(UnicodeVersionError)));
                return PK_ERR;
            }

            /* filename CRC */
            G.unipath_checksum = makelong(offset + ef_buf);
            offset += 4;

            /*
             * Compute 32-bit crc
             */

            chksum = crc32(chksum, (uch *) (G.filename_full),
                           strlen(G.filename_full));

            /* If the checksums's don't match then likely filename has been
             * modified and the Unicode Path is no longer valid.
             */
            if (chksum != G.unipath_checksum) {
                Info(slide, 0x401,
                     ((char *) slide, LoadFarString(UnicodeMismatchError)));
                if (G.unicode_mismatch == 1) {
                    /* warn and continue */
                } else if (G.unicode_mismatch == 2) {
                    /* ignore and continue */
                } else if (G.unicode_mismatch == 0) {
                }
                return PK_ERR;
            }

            /* UTF-8 Path */
            if ((G.unipath_filename = malloc(ULen + 1)) == NULL) {
                return PK_ERR;
            }
            if (ULen == 0) {
                /* standard path is UTF-8 so use that */
                G.unipath_filename[0] = '\0';
            } else {
                /* UTF-8 path */
                strncpy(G.unipath_filename, (const char *) (offset + ef_buf),
                        ULen);
                G.unipath_filename[ULen] = '\0';
            }

            G.zip64 = TRUE;
        }

        /* Skip this extra field block */
        ef_buf += (eb_len + EB_HEADSIZE);
        ef_len -= (eb_len + EB_HEADSIZE);
    }

    return PK_COOL;
} /* end function getUnicodeData() */

/*---------------------------------------------
 * Unicode conversion functions
 *
 * Based on functions provided by Paul Kienitz
 *
 *---------------------------------------------
 */

/*
   NOTES APPLICABLE TO ALL STRING FUNCTIONS:

   All of the x_to_y functions take parameters for an output buffer and
   its available length, and return an int.  The value returned is the
   length of the string that the input produces, which may be larger than
   the provided buffer length.  If the returned value is less than the
   buffer length, then the contents of the buffer will be null-terminated;
   otherwise, it will not be terminated and may be invalid, possibly
   stopping in the middle of a multibyte sequence.

   In all cases you may pass NULL as the buffer and/or 0 as the length, if
   you just want to learn how much space the string is going to require.

   The functions will return -1 if the input is invalid UTF-8 or cannot be
   encoded as UTF-8.
*/

static int utf8_char_bytes(const char *utf8);
static ulg ucs4_char_from_utf8(const char **utf8);
static int utf8_to_ucs4_string(const char *utf8, ulg *ucs4buf, int buflen);

/* utility functions for managing UTF-8 and UCS-4 strings */

/* utf8_char_bytes
 *
 * Returns the number of bytes used by the first character in a UTF-8
 * string, or -1 if the UTF-8 is invalid or null.
 */
static int utf8_char_bytes(utf8) const char *utf8;
{
    int t, r;
    unsigned lead;

    if (!utf8)
        return -1; /* no input */
    lead = (unsigned char) *utf8;
    if (lead < 0x80)
        r = 1; /* an ascii-7 character */
    else if (lead < 0xC0)
        return -1; /* error: trailing byte without lead byte */
    else if (lead < 0xE0)
        r = 2; /* an 11 bit character */
    else if (lead < 0xF0)
        r = 3; /* a 16 bit character */
    else if (lead < 0xF8)
        r = 4; /* a 21 bit character (the most currently used) */
    else if (lead < 0xFC)
        r = 5; /* a 26 bit character (shouldn't happen) */
    else if (lead < 0xFE)
        r = 6; /* a 31 bit character (shouldn't happen) */
    else
        return -1; /* error: invalid lead byte */
    for (t = 1; t < r; t++)
        if ((unsigned char) utf8[t] < 0x80 || (unsigned char) utf8[t] >= 0xC0)
            return -1; /* error: not enough valid trailing bytes */
    return r;
}

/* ucs4_char_from_utf8
 *
 * Given a reference to a pointer into a UTF-8 string, returns the next
 * UCS-4 character and advances the pointer to the next character sequence.
 * Returns ~0 (= -1 in twos-complement notation) and does not advance the
 * pointer when input is ill-formed.
 */
static ulg ucs4_char_from_utf8(utf8) const char **utf8;
{
    ulg ret;
    int t, bytes;

    if (!utf8)
        return ~0L; /* no input */
    bytes = utf8_char_bytes(*utf8);
    if (bytes <= 0)
        return ~0L; /* invalid input */
    if (bytes == 1)
        ret = **utf8; /* ascii-7 */
    else
        ret = **utf8 & (0x7F >> bytes); /* lead byte of a multibyte sequence */
    (*utf8)++;
    for (t = 1; t < bytes; t++) /* consume trailing bytes */
        ret = (ret << 6) | (*((*utf8)++) & 0x3F);
    return (zwchar) ret;
}

#if 0  /* currently unused */
/* utf8_from_ucs4_char - Convert UCS char to UTF-8
 *
 * Returns the number of bytes put into utf8buf to represent ch, from 1 to 6,
 * or -1 if ch is too large to represent.  utf8buf must have room for 6 bytes.
 */
static int utf8_from_ucs4_char(utf8buf, ch)
  char *utf8buf;
  ulg ch;
{
  int trailing = 0;
  int leadmask = 0x80;
  int leadbits = 0x3F;
  int tch = ch;
  int ret;

  if (ch > 0x7FFFFFFFL)
    return -1;                /* UTF-8 can represent 31 bits */
  if (ch < 0x7F)
  {
    *utf8buf++ = (char) ch;   /* ascii-7 */
    return 1;
  }
  do {
    trailing++;
    leadmask = (leadmask >> 1) | 0x80;
    leadbits >>= 1;
    tch >>= 6;
  } while (tch & ~leadbits);
  ret = trailing + 1;
  /* produce lead byte */
  *utf8buf++ = (char) (leadmask | (ch >> (6 * trailing)));
  while (--trailing >= 0)
    /* produce trailing bytes */
    *utf8buf++ = (char) (0x80 | ((ch >> (6 * trailing)) & 0x3F));
  return ret;
}
#endif /* unused */

/*===================================================================*/

/* utf8_to_ucs4_string - convert UTF-8 string to UCS string
 *
 * Return UCS count.  Now returns int so can return -1.
 */
static int utf8_to_ucs4_string(utf8, ucs4buf, buflen) const char *utf8;
ulg *ucs4buf;
int buflen;
{
    int count = 0;

    for (;;) {
        ulg ch = ucs4_char_from_utf8(&utf8);
        if (ch == ~0L)
            return -1;
        else {
            if (ucs4buf && count < buflen)
                ucs4buf[count] = ch;
            if (ch == 0)
                return count;
            count++;
        }
    }
}

#if 0  /* currently unused */
/* ucs4_string_to_utf8
 *
 *
 */
static int ucs4_string_to_utf8(ucs4, utf8buf, buflen)
  const ulg *ucs4;
  char *utf8buf;
  int buflen;
{
  char mb[6];
  int  count = 0;

  if (!ucs4)
    return -1;
  for (;;)
  {
    int mbl = utf8_from_ucs4_char(mb, *ucs4++);
    int c;
    if (mbl <= 0)
      return -1;
    /* We could optimize this a bit by passing utf8buf + count */
    /* directly to utf8_from_ucs4_char when buflen >= count + 6... */
    c = buflen - count;
    if (mbl < c)
      c = mbl;
    if (utf8buf && count < buflen)
      strncpy(utf8buf + count, mb, c);
    if (mbl == 1 && !mb[0])
      return count;           /* terminating nul */
    count += mbl;
  }
}


/* utf8_chars
 *
 * Wrapper: counts the actual unicode characters in a UTF-8 string.
 */
static int utf8_chars(utf8)
  const char *utf8;
{
  return utf8_to_ucs4_string(utf8, NULL, 0);
}
#endif /* unused */

/* --------------------------------------------------- */
/* Unicode Support
 *
 * These functions common for all Unicode ports.
 *
 * These functions should allocate and return strings that can be
 * freed with free().
 *
 * 8/27/05 EG
 *
 * Use zwchar for wide char which is unsigned long
 * in zip.h and 32 bits.  This avoids problems with
 * different sizes of wchar_t.
 */

#if 0  /* currently unused */
/* is_ascii_string
 * Checks if a string is all ascii
 */
int is_ascii_string(mbstring)
  const char *mbstring;
{
  char *p;
  uch c;

  for (p = mbstring; c = (uch)*p; p++) {
    if (c > 0x7F) {
      return 0;
    }
  }
  return 1;
}

/* local to UTF-8 */
char *local_to_utf8_string(local_string)
  const char *local_string;
{
  return wide_to_utf8_string(local_to_wide_string(local_string));
}
#endif /* unused */

/* wide_to_escape_string
   provides a string that represents a wide char not in local char set

   An initial try at an algorithm.  Suggestions welcome.

   According to the standard, Unicode character points are restricted to
   the number range from 0 to 0x10FFFF, respective 21 bits.
   For a hexadecimal notation, 2 octets are sufficient for the mostly
   used characters from the "Basic Multilingual Plane", all other
   Unicode characters can be represented by 3 octets (= 6 hex digits).
   The Unicode standard suggests to write Unicode character points
   as 4 resp. 6 hex digits, preprended by "U+".
   (e.g.: U+10FFFF for the highest character point, or U+0030 for the ASCII
   digit "0")

   However, for the purpose of escaping non-ASCII chars in an ASCII character
   stream, the "U" is not a very good escape initializer. Therefore, we
   use the following convention within our Info-ZIP code:

   If not an ASCII char probably need 2 bytes at least.  So if
   a 2-byte wide encode it as 4 hex digits with a leading #U.  If
   needs 3 bytes then prefix the string with #L.  So
   #U1234
   is a 2-byte wide character with bytes 0x12 and 0x34 while
   #L123456
   is a 3-byte wide character with bytes 0x12, 0x34, 0x56.
   On Windows, wide that need two wide characters need to be converted
   to a single number.
  */

/* set this to the max bytes an escape can be */
#define MAX_ESCAPE_BYTES 8

char *wide_to_escape_string(wide_char)
zwchar wide_char;
{
    int i;
    zwchar w = wide_char;
    uch b[sizeof(zwchar)];
    char d[3];
    char e[11];
    int len;
    char *r;

    /* fill byte array with zeros */
    memzero(b, sizeof(zwchar));
    /* get bytes in right to left order */
    for (len = 0; w; len++) {
        b[len] = (char) (w % 0x100);
        w /= 0x100;
    }
    strcpy(e, "#");
    /* either 2 bytes or 3 bytes */
    if (len <= 2) {
        len = 2;
        strcat(e, "U");
    } else {
        strcat(e, "L");
    }
    for (i = len - 1; i >= 0; i--) {
        sprintf(d, "%02x", b[i]);
        strcat(e, d);
    }
    if ((r = malloc(strlen(e) + 1)) == NULL) {
        return NULL;
    }
    strcpy(r, e);
    return r;
}

#if 0  /* currently unused */
/* returns the wide character represented by the escape string */
zwchar escape_string_to_wide(escape_string)
  const char *escape_string;
{
  int i;
  zwchar w;
  char c;
  int len;
  const char *e = escape_string;

  if (e == NULL) {
    return 0;
  }
  if (e[0] != '#') {
    /* no leading # */
    return 0;
  }
  len = strlen(e);
  /* either #U1234 or #L123456 format */
  if (len != 6 && len != 8) {
    return 0;
  }
  w = 0;
  if (e[1] == 'L') {
    if (len != 8) {
      return 0;
    }
    /* 3 bytes */
    for (i = 2; i < 8; i++) {
      c = e[i];
      if (c < '0' || c > '9') {
        return 0;
      }
      w = w * 0x10 + (zwchar)(c - '0');
    }
  } else if (e[1] == 'U') {
    /* 2 bytes */
    for (i = 2; i < 6; i++) {
      c = e[i];
      if (c < '0' || c > '9') {
        return 0;
      }
      w = w * 0x10 + (zwchar)(c - '0');
    }
  }
  return w;
}
#endif /* unused */

/* convert wide character string to multi-byte character string */
char *wide_to_local_string(wide_string, escape_all) const zwchar *wide_string;
int escape_all;
{
    int i;
    wchar_t wc;
    int b;
    int state_dependent;
    int wsize = 0;
    int max_bytes = MB_CUR_MAX;
    char buf[MB_CUR_MAX + 1]; /* ("+1" not really needed?) */
    char *buffer = NULL;
    char *local_string = NULL;
    size_t buffer_size; /* CVE-2022-0529 */

    for (wsize = 0; wide_string[wsize]; wsize++)
        ;

    if (max_bytes < MAX_ESCAPE_BYTES)
        max_bytes = MAX_ESCAPE_BYTES;
    buffer_size = wsize * max_bytes + 1; /* Reused below. */
    if ((buffer = (char *) malloc(buffer_size)) == NULL) {
        return NULL;
    }

    /* convert it */
    buffer[0] = '\0';
    /* set initial state if state-dependent encoding */
    wc = (wchar_t) 'a';
    b = wctomb(NULL, wc);
    if (b == 0)
        state_dependent = 0;
    else
        state_dependent = 1;
    for (i = 0; i < wsize; i++) {
        if (sizeof(wchar_t) < 4 && wide_string[i] > 0xFFFF) {
            /* wchar_t probably 2 bytes */
            /* could do surrogates if state_dependent and wctomb can do */
            wc = zwchar_to_wchar_t_default_char;
        } else {
            wc = (wchar_t) wide_string[i];
        }
        b = wctomb(buf, wc);
        if (escape_all) {
            if (b == 1 && (uch) buf[0] <= 0x7f) {
                /* ASCII */
                strncat(buffer, buf, b);
            } else {
                /* use escape for wide character */
                char *escape_string = wide_to_escape_string(wide_string[i]);
                strcat(buffer, escape_string);
                free(escape_string);
            }
        } else if (b > 0) {
            /* multi-byte char */
            strncat(buffer, buf, b);
        } else {
            /* no MB for this wide */
            /* use escape for wide character */
            size_t buffer_len;
            size_t escape_string_len;
            char *escape_string;
            int err_msg = 0;

            escape_string = wide_to_escape_string(wide_string[i]);
            buffer_len = strlen(buffer);
            escape_string_len = strlen(escape_string);

            /* Append escape string, as space allows. */
            /* 2022-07-18 SMS, et al.  CVE-2022-0529 */
            if (escape_string_len > buffer_size - buffer_len - 1) {
                escape_string_len = buffer_size - buffer_len - 1;
                if (err_msg == 0) {
                    err_msg = 1;
                    Info(
                        slide, 0x401,
                        ((char *) slide, LoadFarString(UFilenameTooLongTrunc)));
                }
            }
            strncat(buffer, escape_string, escape_string_len);
            free(escape_string);
        }
    }
    if ((local_string = (char *) malloc(strlen(buffer) + 1)) != NULL) {
        strcpy(local_string, buffer);
    }
    free(buffer);

    return local_string;
}

#if 0  /* currently unused */
/* convert local string to display character set string */
char *local_to_display_string(local_string)
  const char *local_string;
{
  char *display_string;

  /* For Windows, OEM string should never be bigger than ANSI string, says
     CharToOem description.
     For all other ports, just make a copy of local_string.
  */
  if ((display_string = (char *)malloc(strlen(local_string) + 1)) == NULL) {
    return NULL;
  }

  strcpy(display_string, local_string);

  return display_string;
}
#endif /* unused */

/* UTF-8 to local */
char *utf8_to_local_string(utf8_string, escape_all) const char *utf8_string;
int escape_all;
{
    zwchar *wide;
    char *loc = NULL;

    wide = utf8_to_wide_string(utf8_string);

    /* 2022-07-25 SMS, et al.  CVE-2022-0530 */
    if (wide != NULL) {
        loc = wide_to_local_string(wide, escape_all);
        free(wide);
    }

    return loc;
}

#if 0  /* currently unused */
/* convert multi-byte character string to wide character string */
zwchar *local_to_wide_string(local_string)
  const char *local_string;
{
  int wsize;
  wchar_t *wc_string;
  zwchar *wide_string;

  /* for now try to convert as string - fails if a bad char in string */
  wsize = mbstowcs(NULL, local_string, strlen(local_string) + 1);
  if (wsize == (size_t)-1) {
    /* could not convert */
    return NULL;
  }

  /* convert it */
  if ((wc_string = (wchar_t *)malloc((wsize + 1) * sizeof(wchar_t))) == NULL) {
    return NULL;
  }
  wsize = mbstowcs(wc_string, local_string, strlen(local_string) + 1);
  wc_string[wsize] = (wchar_t) 0;

  /* in case wchar_t is not zwchar */
  if ((wide_string = (zwchar *)malloc((wsize + 1) * sizeof(zwchar))) == NULL) {
    return NULL;
  }
  for (wsize = 0; wide_string[wsize] = (zwchar)wc_string[wsize]; wsize++) ;
  wide_string[wsize] = (zwchar) 0;
  free(wc_string);

  return wide_string;
}


/* convert wide string to UTF-8 */
char *wide_to_utf8_string(wide_string)
  const zwchar *wide_string;
{
  int mbcount;
  char *utf8_string;

  /* get size of utf8 string */
  mbcount = ucs4_string_to_utf8(wide_string, NULL, 0);
  if (mbcount == -1)
    return NULL;
  if ((utf8_string = (char *) malloc(mbcount + 1)) == NULL) {
    return NULL;
  }
  mbcount = ucs4_string_to_utf8(wide_string, utf8_string, mbcount + 1);
  if (mbcount == -1)
    return NULL;

  return utf8_string;
}
#endif /* unused */

/* convert UTF-8 string to wide string */
zwchar *utf8_to_wide_string(utf8_string) const char *utf8_string;
{
    int wcount;
    zwchar *wide_string;

    wcount = utf8_to_ucs4_string(utf8_string, NULL, 0);
    if (wcount == -1)
        return NULL;
    if ((wide_string = (zwchar *) malloc((wcount + 1) * sizeof(zwchar))) ==
        NULL) {
        return NULL;
    }
    wcount = utf8_to_ucs4_string(utf8_string, wide_string, wcount + 1);

    return wide_string;
}

static int
    read_ux3_value(dbuf, uidgid_sz,
                   p_uidgid) const uch *dbuf; /* buffer a uid or gid value */
unsigned uidgid_sz;                           /* size of uid/gid value */
ulg *p_uidgid; /* return storage: uid or gid value */
{
    zusz_t uidgid64;

    switch (uidgid_sz) {
    case 2:
        *p_uidgid = (ulg) makeword(dbuf);
        break;
    case 4:
        *p_uidgid = (ulg) makelong(dbuf);
        break;
    case 8:
        uidgid64 = makeint64(dbuf);
#ifndef LARGE_FILE_SUPPORT
        if (uidgid64 == (zusz_t) 0xffffffffL)
            return FALSE;
#endif
        *p_uidgid = (ulg) uidgid64;
        if ((zusz_t) (*p_uidgid) != uidgid64)
            return FALSE;
        break;
    }
    return TRUE;
}

/*******************************/
/* Function ef_scan_for_izux() */
/*******************************/

unsigned ef_scan_for_izux(
    ef_buf, ef_len, ef_is_c, dos_mdatetime, z_utim,
    z_uidgid) const uch *ef_buf; /* buffer containing extra field */
unsigned ef_len;                 /* total length of extra field */
int ef_is_c;                     /* flag indicating "is central extra field" */
ulg dos_mdatetime;               /* last_mod_file_date_time in DOS format */
iztimes *z_utim;                 /* return storage: atime, mtime, ctime */
ulg *z_uidgid;                   /* return storage: uid and gid */
{
    unsigned flags = 0;
    unsigned eb_id;
    unsigned eb_len;
    int have_new_type_eb = 0;
    long i_time; /* buffer for Unix style 32-bit integer time value */
#ifdef TIME_T_TYPE_DOUBLE
    int ut_in_archive_sgn = 0;
#else
    int ut_zip_unzip_compatible = FALSE;
#endif

    /*---------------------------------------------------------------------------
        This function scans the extra field for EF_TIME, EF_IZUNIX2, EF_IZUNIX,
      or EF_PKUNIX blocks containing Unix-style time_t (GMT) values for the
      entry's access, creation, and modification time. If a valid block is
      found, the time stamps are copied to the iztimes structure (provided the
      z_utim pointer is not NULL). If a IZUNIX2 block is found or the IZUNIX
      block contains UID/GID fields, and the z_uidgid array pointer is valid (!=
      NULL), the owner info is transfered as well. The presence of an EF_TIME or
      EF_IZUNIX2 block results in ignoring all data from probably present
      obsolete EF_IZUNIX blocks. If multiple blocks of the same type are found,
      only the information from the last block is used. The return value is a
      combination of the EF_TIME Flags field with an additional flag bit
      indicating the presence of valid UID/GID info, or 0 in case of failure.
      ---------------------------------------------------------------------------*/

    if (ef_len == 0 || ef_buf == NULL || (z_utim == 0 && z_uidgid == NULL))
        return 0;

    TTrace((stderr, "\nef_scan_for_izux: scanning extra field of length %u\n",
            ef_len));

    while (ef_len >= EB_HEADSIZE) {
        eb_id = makeword(EB_ID + ef_buf);
        eb_len = makeword(EB_LEN + ef_buf);

        if (eb_len > (ef_len - EB_HEADSIZE)) {
            /* discovered some extra field inconsistency! */
            TTrace((stderr,
                    "ef_scan_for_izux: block length %u > rest ef_size %u\n",
                    eb_len, ef_len - EB_HEADSIZE));
            break;
        }

        switch (eb_id) {
        case EF_TIME:
            flags &= ~0x0ff; /* ignore previous IZUNIX or EF_TIME fields */
            have_new_type_eb = 1;
            if (eb_len >= EB_UT_MINLEN && z_utim != NULL) {
                unsigned eb_idx = EB_UT_TIME1;
                TTrace((stderr, "ef_scan_for_izux: found TIME extra field\n"));
                flags |= (ef_buf[EB_HEADSIZE + EB_UT_FLAGS] & 0x0ff);
                if ((flags & EB_UT_FL_MTIME)) {
                    if ((eb_idx + 4) <= eb_len) {
                        i_time =
                            (long) makelong((EB_HEADSIZE + eb_idx) + ef_buf);
                        eb_idx += 4;
                        TTrace((stderr, "  UT e.f. modification time = %ld\n",
                                i_time));

#ifdef TIME_T_TYPE_DOUBLE
                        if ((ulg) (i_time) & (ulg) (0x80000000L)) {
                            if (dos_mdatetime == DOSTIME_MINIMUM) {
                                ut_in_archive_sgn = -1;
                                z_utim->mtime =
                                    (time_t) ((long) i_time |
                                              (~(long) 0x7fffffffL));
                            } else if (dos_mdatetime >= DOSTIME_2038_01_18) {
                                ut_in_archive_sgn = 1;
                                z_utim->mtime =
                                    (time_t) ((ulg) i_time & (ulg) 0xffffffffL);
                            } else {
                                ut_in_archive_sgn = 0;
                                /* cannot determine sign of mtime;
                                   without modtime: ignore complete UT field */
                                flags &= ~0x0ff; /* no time_t times available */
                                TTrace((stderr, "  UT modtime range error; "
                                                "ignore e.f.!\n"));
                                break; /* stop scanning this field */
                            }
                        } else {
                            /* cannot determine, safe assumption is FALSE */
                            ut_in_archive_sgn = 0;
                            z_utim->mtime = (time_t) i_time;
                        }
#else  /* !TIME_T_TYPE_DOUBLE */
                        if ((ulg) (i_time) & (ulg) (0x80000000L)) {
                            ut_zip_unzip_compatible =
                                ((time_t) 0x80000000L < (time_t) 0L)
                                    ? (dos_mdatetime == DOSTIME_MINIMUM)
                                    : (dos_mdatetime >= DOSTIME_2038_01_18);
                            if (!ut_zip_unzip_compatible) {
                                /* UnZip interprets mtime differently than Zip;
                                   without modtime: ignore complete UT field */
                                flags &= ~0x0ff; /* no time_t times available */
                                TTrace((stderr, "  UT modtime range error; "
                                                "ignore e.f.!\n"));
                                break; /* stop scanning this field */
                            }
                        } else {
                            /* cannot determine, safe assumption is FALSE */
                            ut_zip_unzip_compatible = FALSE;
                        }
                        z_utim->mtime = (time_t) i_time;
#endif /* ?TIME_T_TYPE_DOUBLE */
                    } else {
                        flags &= ~EB_UT_FL_MTIME;
                        TTrace((stderr, "  UT e.f. truncated; no modtime\n"));
                    }
                }
                if (ef_is_c) {
                    break; /* central version of TIME field ends here */
                }

                if (flags & EB_UT_FL_ATIME) {
                    if ((eb_idx + 4) <= eb_len) {
                        i_time =
                            (long) makelong((EB_HEADSIZE + eb_idx) + ef_buf);
                        eb_idx += 4;
                        TTrace(
                            (stderr, "  UT e.f. access time = %ld\n", i_time));
#ifdef TIME_T_TYPE_DOUBLE
                        if ((ulg) (i_time) & (ulg) (0x80000000L)) {
                            if (ut_in_archive_sgn == -1)
                                z_utim->atime =
                                    (time_t) ((long) i_time |
                                              (~(long) 0x7fffffffL));
                        } else if (ut_in_archive_sgn == 1) {
                            z_utim->atime =
                                (time_t) ((ulg) i_time & (ulg) 0xffffffffL);
                        } else {
                            /* sign of 32-bit time is unknown -> ignore it */
                            flags &= ~EB_UT_FL_ATIME;
                            TTrace(
                                (stderr,
                                 "  UT access time range error: skip time!\n"));
                        }
                    } else {
                        z_utim->atime = (time_t) i_time;
                    }
#else  /* !TIME_T_TYPE_DOUBLE */
                        if (((ulg) (i_time) & (ulg) (0x80000000L)) &&
                            !ut_zip_unzip_compatible) {
                            flags &= ~EB_UT_FL_ATIME;
                            TTrace(
                                (stderr,
                                 "  UT access time range error: skip time!\n"));
                        } else {
                            z_utim->atime = (time_t) i_time;
                        }
#endif /* ?TIME_T_TYPE_DOUBLE */
                } else {
                    flags &= ~EB_UT_FL_ATIME;
                }
            }
            if (flags & EB_UT_FL_CTIME) {
                if ((eb_idx + 4) <= eb_len) {
                    i_time = (long) makelong((EB_HEADSIZE + eb_idx) + ef_buf);
                    TTrace((stderr, "  UT e.f. creation time = %ld\n", i_time));
#ifdef TIME_T_TYPE_DOUBLE
                    if ((ulg) (i_time) & (ulg) (0x80000000L)) {
                        if (ut_in_archive_sgn == -1)
                            z_utim->ctime = (time_t) ((long) i_time |
                                                      (~(long) 0x7fffffffL));
                    } else if (ut_in_archive_sgn == 1) {
                        z_utim->ctime =
                            (time_t) ((ulg) i_time & (ulg) 0xffffffffL);
                    } else {
                        /* sign of 32-bit time is unknown -> ignore it */
                        flags &= ~EB_UT_FL_CTIME;
                        TTrace(
                            (stderr,
                             "  UT creation time range error: skip time!\n"));
                    }
                } else {
                    z_utim->ctime = (time_t) i_time;
                }
#else  /* !TIME_T_TYPE_DOUBLE */
                        if (((ulg) (i_time) & (ulg) (0x80000000L)) &&
                            !ut_zip_unzip_compatible) {
                            flags &= ~EB_UT_FL_CTIME;
                            TTrace((stderr, "  UT creation time range error: "
                                            "skip time!\n"));
                        } else {
                            z_utim->ctime = (time_t) i_time;
                        }
#endif /* ?TIME_T_TYPE_DOUBLE */
            } else {
                flags &= ~EB_UT_FL_CTIME;
            }
        }
    }
    break;

case EF_IZUNIX2:
    if (have_new_type_eb == 0) { /* (< 1) */
        have_new_type_eb = 1;
    }
    if (have_new_type_eb <= 1) {
        /* Ignore any prior (EF_IZUNIX/EF_PKUNIX) UID/GID. */
        flags &= 0x0ff;
    }
    if (have_new_type_eb > 1)
        break; /* IZUNIX3 overrides IZUNIX2 e.f. block ! */
    if (eb_len == EB_UX2_MINLEN && z_uidgid != NULL) {
        z_uidgid[0] = (ulg) makeword((EB_HEADSIZE + EB_UX2_UID) + ef_buf);
        z_uidgid[1] = (ulg) makeword((EB_HEADSIZE + EB_UX2_GID) + ef_buf);
        flags |= EB_UX2_VALID; /* signal success */
    }
    break;

case EF_IZUNIX3:
    /* new 3rd generation Unix ef */
    have_new_type_eb = 2;

    /* Ignore any prior EF_IZUNIX/EF_PKUNIX/EF_IZUNIX2 UID/GID. */
    flags &= 0x0ff;
    /*
      Version       1 byte      version of this extra field, currently 1
      UIDSize       1 byte      Size of UID field
      UID           Variable    UID for this entry
      GIDSize       1 byte      Size of GID field
      GID           Variable    GID for this entry
    */

    if (eb_len >= EB_UX3_MINLEN && z_uidgid != NULL &&
        (*((EB_HEADSIZE + 0) + ef_buf) == 1))
    /* only know about version 1 */
    {
        uch uid_size;
        uch gid_size;

        uid_size = *((EB_HEADSIZE + 1) + ef_buf);
        gid_size = *((EB_HEADSIZE + uid_size + 2) + ef_buf);

        if (read_ux3_value((EB_HEADSIZE + 2) + ef_buf, uid_size,
                           &z_uidgid[0]) &&
            read_ux3_value((EB_HEADSIZE + uid_size + 3) + ef_buf, gid_size,
                           &z_uidgid[1])) {
            flags |= EB_UX2_VALID; /* signal success */
        }
    }
    break;

case EF_IZUNIX:
case EF_PKUNIX: /* PKUNIX e.f. layout is identical to IZUNIX */
    if (eb_len >= EB_UX_MINLEN) {
        TTrace((stderr, "ef_scan_for_izux: found %s extra field\n",
                (eb_id == EF_IZUNIX ? "IZUNIX" : "PKUNIX")));
        if (have_new_type_eb > 0) {
            break; /* Ignore IZUNIX extra field block ! */
        }
        if (z_utim != NULL) {
            flags |= (EB_UT_FL_MTIME | EB_UT_FL_ATIME);
            i_time = (long) makelong((EB_HEADSIZE + EB_UX_MTIME) + ef_buf);
            TTrace((stderr, "  Unix EF modtime = %ld\n", i_time));
#ifdef TIME_T_TYPE_DOUBLE
            if ((ulg) (i_time) & (ulg) (0x80000000L)) {
                if (dos_mdatetime == DOSTIME_MINIMUM) {
                    ut_in_archive_sgn = -1;
                    z_utim->mtime =
                        (time_t) ((long) i_time | (~(long) 0x7fffffffL));
                } else if (dos_mdatetime >= DOSTIME_2038_01_18) {
                    ut_in_archive_sgn = 1;
                    z_utim->mtime = (time_t) ((ulg) i_time & (ulg) 0xffffffffL);
                } else {
                    ut_in_archive_sgn = 0;
                    /* cannot determine sign of mtime;
                       without modtime: ignore complete UT field */
                    flags &= ~0x0ff; /* no time_t times available */
                    TTrace(
                        (stderr, "  UX modtime range error: ignore e.f.!\n"));
                }
            } else {
                /* cannot determine, safe assumption is FALSE */
                ut_in_archive_sgn = 0;
                z_utim->mtime = (time_t) i_time;
            }
#else  /* !TIME_T_TYPE_DOUBLE */
                    if ((ulg) (i_time) & (ulg) (0x80000000L)) {
                        ut_zip_unzip_compatible =
                            ((time_t) 0x80000000L < (time_t) 0L)
                                ? (dos_mdatetime == DOSTIME_MINIMUM)
                                : (dos_mdatetime >= DOSTIME_2038_01_18);
                        if (!ut_zip_unzip_compatible) {
                            /* UnZip interpretes mtime differently than Zip;
                               without modtime: ignore complete UT field */
                            flags &= ~0x0ff; /* no time_t times available */
                            TTrace(
                                (stderr,
                                 "  UX modtime range error: ignore e.f.!\n"));
                        }
                    } else {
                        /* cannot determine, safe assumption is FALSE */
                        ut_zip_unzip_compatible = FALSE;
                    }
                    z_utim->mtime = (time_t) i_time;
#endif /* ?TIME_T_TYPE_DOUBLE */
            i_time = (long) makelong((EB_HEADSIZE + EB_UX_ATIME) + ef_buf);
            TTrace((stderr, "  Unix EF actime = %ld\n", i_time));
#ifdef TIME_T_TYPE_DOUBLE
            if ((ulg) (i_time) & (ulg) (0x80000000L)) {
                if (ut_in_archive_sgn == -1)
                    z_utim->atime =
                        (time_t) ((long) i_time | (~(long) 0x7fffffffL));
            } else if (ut_in_archive_sgn == 1) {
                z_utim->atime = (time_t) ((ulg) i_time & (ulg) 0xffffffffL);
            } else if (flags & 0x0ff) {
                /* sign of 32-bit time is unknown -> ignore it */
                flags &= ~EB_UT_FL_ATIME;
                TTrace((stderr, "  UX access time range error: skip time!\n"));
            }
        } else {
            z_utim->atime = (time_t) i_time;
        }
#else  /* !TIME_T_TYPE_DOUBLE */
                    if (((ulg) (i_time) & (ulg) (0x80000000L)) &&
                        !ut_zip_unzip_compatible && (flags & 0x0ff)) {
                        /* atime not in range of UnZip's time_t */
                        flags &= ~EB_UT_FL_ATIME;
                        TTrace((stderr,
                                "  UX access time range error: skip time!\n"));
                    } else {
                        z_utim->atime = (time_t) i_time;
                    }
#endif /* ?TIME_T_TYPE_DOUBLE */
    }
    if (eb_len >= EB_UX_FULLSIZE && z_uidgid != NULL) {
        z_uidgid[0] = makeword((EB_HEADSIZE + EB_UX_UID) + ef_buf);
        z_uidgid[1] = makeword((EB_HEADSIZE + EB_UX_GID) + ef_buf);
        flags |= EB_UX2_VALID;
    }
}
break;

default:
break;
}

/* Skip this extra field block */
ef_buf += (eb_len + EB_HEADSIZE);
ef_len -= (eb_len + EB_HEADSIZE);
}

return flags;
}
