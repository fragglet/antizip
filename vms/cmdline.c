#define module_name VMS_UNZIP_CMDLINE
#define module_ident "02-007"
/*
**
**  Facility:   UNZIP
**
**  Module:     VMS_UNZIP_CMDLINE
**
**  Author:     Hunter Goatley <goathunter@wkuvx1.wku.edu>
**
**  Date:       12 Jul 94 (orig. Zip version, 30 Jul 93)
**
**  Abstract:   Routines to handle a VMS CLI interface for UnZip.  The CLI
**              command line is parsed and a new argc/argv are built and
**              returned to UnZip.
**
**  Modified by:
**
**      02-007          Christian Spieler       04-MAR-1997 22:25
**              Made /CASE_INSENSITIVE common to UnZip and ZipInfo mode;
**              added support for /PASSWORD="decryption_key" argument.
**      02-006          Christian Spieler       11-MAY-1996 22:40
**              Added SFX version of VMSCLI_usage().
**      02-005          Patrick Ellis           09-MAY-1996 22:25
**              Show UNIX style usage screen when UNIX style options are used.
**      02-004          Christian Spieler       06-FEB-1996 02:20
**              Added /HELP qualifier.
**      02-003          Christian Spieler       23-DEC-1995 17:20
**              Adapted to UnZip 5.2.
**      02-002          Hunter Goatley          16-JUL-1994 10:20
**              Fixed some typos.
**      02-001          Cave Newt               14-JUL-1994 15:18
**              Removed obsolete /EXTRACT option; fixed /*TEXT options;
**              wrote VMSCLI usage() function
**      02-000          Hunter Goatley          12-JUL-1994 00:00
**              Original UnZip version (v5.11).
**      01-000          Hunter Goatley          30-JUL-1993 07:54
**              Original version (for Zip v1.9p1).
**
*/


#ifdef __DECC
#pragma module module_name module_ident
#else
#module module_name module_ident
#endif

#define UNZIP_INTERNAL
#include "unzip.h"
#include "version.h"    /* for VMSCLI_usage() */

#include <ssdef.h>
#include <descrip.h>
#include <climsgdef.h>
#include <clidef.h>
#include <lib$routines.h>
#include <str$routines.h>

#ifndef CLI$_COMMA
globalvalue CLI$_COMMA;
#endif

/*
**  "Macro" to initialize a dynamic string descriptor.
*/
#define init_dyndesc(dsc) {\
        dsc.dsc$w_length = 0;\
        dsc.dsc$b_dtype = DSC$K_DTYPE_T;\
        dsc.dsc$b_class = DSC$K_CLASS_D;\
        dsc.dsc$a_pointer = NULL;}

/*
**  Define descriptors for all of the CLI parameters and qualifiers.
*/
#if 0
$DESCRIPTOR(cli_extract,        "EXTRACT");     /* obsolete */
#endif
$DESCRIPTOR(cli_text,           "TEXT");        /* -a[a] */
$DESCRIPTOR(cli_text_auto,      "TEXT.AUTO");   /* -a */
$DESCRIPTOR(cli_text_all,       "TEXT.ALL");    /* -aa */
$DESCRIPTOR(cli_text_none,      "TEXT.NONE");   /* ---a */
$DESCRIPTOR(cli_binary,         "BINARY");      /* -b[b] */
$DESCRIPTOR(cli_binary_auto,    "BINARY.AUTO"); /* -b */
$DESCRIPTOR(cli_binary_all,     "BINARY.ALL");  /* -bb */
$DESCRIPTOR(cli_binary_none,    "BINARY.NONE"); /* ---b */
$DESCRIPTOR(cli_case_insensitive,"CASE_INSENSITIVE");   /* -C */
$DESCRIPTOR(cli_screen,         "SCREEN");      /* -c */
$DESCRIPTOR(cli_directory,      "DIRECTORY");   /* -d */
$DESCRIPTOR(cli_freshen,        "FRESHEN");     /* -f */
$DESCRIPTOR(cli_help,           "HELP");        /* -h */
$DESCRIPTOR(cli_junk,           "JUNK");        /* -j */
$DESCRIPTOR(cli_lowercase,      "LOWERCASE");   /* -L */
$DESCRIPTOR(cli_list,           "LIST");        /* -l */
$DESCRIPTOR(cli_brief,          "BRIEF");       /* -l */
$DESCRIPTOR(cli_full,           "FULL");        /* -v */
$DESCRIPTOR(cli_overwrite,      "OVERWRITE");   /* -o, -n */
$DESCRIPTOR(cli_quiet,          "QUIET");       /* -q */
$DESCRIPTOR(cli_super_quiet,    "QUIET.SUPER"); /* -qq */
$DESCRIPTOR(cli_test,           "TEST");        /* -t */
$DESCRIPTOR(cli_type,           "TYPE");        /* -c */
$DESCRIPTOR(cli_pipe,           "PIPE");        /* -p */
$DESCRIPTOR(cli_password,       "PASSWORD");    /* -P */
$DESCRIPTOR(cli_uppercase,      "UPPERCASE");   /* -U */
$DESCRIPTOR(cli_update,         "UPDATE");      /* -u */
$DESCRIPTOR(cli_version,        "VERSION");     /* -V */
$DESCRIPTOR(cli_restore,        "RESTORE");     /* -X */
$DESCRIPTOR(cli_comment,        "COMMENT");     /* -z */
$DESCRIPTOR(cli_exclude,        "EXCLUDE");     /* -x */

$DESCRIPTOR(cli_information,    "ZIPINFO");     /* -Z */
$DESCRIPTOR(cli_short,          "SHORT");       /* -Zs */
$DESCRIPTOR(cli_medium,         "MEDIUM");      /* -Zm */
$DESCRIPTOR(cli_long,           "LONG");        /* -Zl */
$DESCRIPTOR(cli_verbose,        "VERBOSE");     /* -Zv */
$DESCRIPTOR(cli_header,         "HEADER");      /* -Zh */
$DESCRIPTOR(cli_totals,         "TOTALS");      /* -Zt */
$DESCRIPTOR(cli_times,          "TIMES");       /* -ZT */
$DESCRIPTOR(cli_one_line,       "ONE_LINE");    /* -Z2 */

$DESCRIPTOR(cli_page,           "PAGE");        /* -M , -ZM */

$DESCRIPTOR(cli_yyz,            "YYZ_UNZIP");

$DESCRIPTOR(cli_zipfile,        "ZIPFILE");
$DESCRIPTOR(cli_infile,         "INFILE");
$DESCRIPTOR(unzip_command,      "unzip ");
$DESCRIPTOR(blank,              " ");

static int show_VMSCLI_usage;

#ifdef __DECC
extern void *vms_unzip_cld;
#else
globalref void *vms_unzip_cld;
#endif

/* extern unsigned long LIB$GET_INPUT(void), LIB$SIG_TO_RET(void); */

extern unsigned long cli$dcl_parse ();
extern unsigned long cli$present ();
extern unsigned long cli$get_value ();

unsigned long vms_unzip_cmdline (int *, char ***);
unsigned long get_list (struct dsc$descriptor_s *, char **,
                        struct dsc$descriptor_d *, char);
unsigned long check_cli (struct dsc$descriptor_s *);


#ifdef TEST
unsigned long
main(int argc, char **argv)
{
    register status;
    return (vms_unzip_cmdline(&argc, &argv));
}
#endif /* TEST */


unsigned long
vms_unzip_cmdline (int *argc_p, char ***argv_p)
{
/*
**  Routine:    vms_unzip_cmdline
**
**  Function:
**
**      Parse the DCL command line and create a fake argv array to be
**      handed off to Zip.
**
**      NOTE: the argv[] is built as we go, so all the parameters are
**      checked in the appropriate order!!
**
**  Formal parameters:
**
**      argc_p          - Address of int to receive the new argc
**      argv_p          - Address of char ** to receive the argv address
**
**  Calling sequence:
**
**      status = vms_unzip_cmdline (&argc, &argv);
**
**  Returns:
**
**      SS$_NORMAL      - Success.
**      SS$_INSFMEM     - A malloc() or realloc() failed
**      SS$_ABORT       - Bad time value
**
*/
    register status;
    char options[256];
    char *the_cmd_line;
    char *ptr;
    int  x, len, zipinfo, exclude_list;

    int new_argc;
    char **new_argv;

    struct dsc$descriptor_d work_str;
    struct dsc$descriptor_d foreign_cmdline;
    struct dsc$descriptor_d output_directory;
    struct dsc$descriptor_d password_arg;

    init_dyndesc (work_str);
    init_dyndesc (foreign_cmdline);
    init_dyndesc (output_directory);
    init_dyndesc (password_arg);

    /*
    **  See if the program was invoked by the CLI (SET COMMAND) or by
    **  a foreign command definition.  Check for /YYZ_UNZIP, which is a
    **  valid default qualifier solely for this test.
    */
    show_VMSCLI_usage = TRUE;
    status = check_cli (&cli_yyz);
    if (!(status & 1)) {
        lib$get_foreign (&foreign_cmdline);
        /*
        **  If nothing was returned or the first character is a "-", then
        **  assume it's a UNIX-style command and return.
        */
        if (foreign_cmdline.dsc$w_length == 0)
            return(SS$_NORMAL);
        if ((*(foreign_cmdline.dsc$a_pointer) == '-') ||
            ((foreign_cmdline.dsc$w_length > 1) &&
             (*(foreign_cmdline.dsc$a_pointer) == '"') &&
             (*(foreign_cmdline.dsc$a_pointer + 1) == '-'))) {
            show_VMSCLI_usage = FALSE;
            return(SS$_NORMAL);
        }

        str$concat (&work_str, &unzip_command, &foreign_cmdline);
        status = cli$dcl_parse(&work_str, &vms_unzip_cld, lib$get_input,
                        lib$get_input, 0);
        if (!(status & 1)) return(status);
    }

    /*
    **  There's always going to be a new_argv[] because of the image name.
    */
    if ((the_cmd_line = (char *) malloc (sizeof("unzip")+1)) == NULL)
        return(SS$_INSFMEM);

    strcpy (the_cmd_line, "unzip");

    /*
    **  First, check to see if any of the regular options were specified.
    */

    options[0] = '-';
    ptr = &options[1];          /* Point to temporary buffer */

    /*
    **  Is it ZipInfo??
    */
    zipinfo = 0;
    status = cli$present (&cli_information);
    if (status & 1) {

        zipinfo = 1;

        *ptr++ = 'Z';

        if (cli$present(&cli_one_line) & 1)
            *ptr++ = '2';
        if (cli$present(&cli_short) & 1)
            *ptr++ = 's';
        if (cli$present(&cli_medium) & 1)
            *ptr++ = 'm';
        if (cli$present(&cli_long) & 1)
            *ptr++ = 'l';
        if (cli$present(&cli_verbose) & 1)
            *ptr++ = 'v';
        if (cli$present(&cli_header) & 1)
            *ptr++ = 'h';
        if (cli$present(&cli_comment) & 1)
            *ptr++ = 'c';
        if (cli$present(&cli_totals) & 1)
            *ptr++ = 't';
        if (cli$present(&cli_times) & 1)
            *ptr++ = 'T';

    }
    else {

#if 0
    /*
    **  Extract files?
    */
    status = cli$present (&cli_extract);
    if (status == CLI$_NEGATED)
        *ptr++ = '-';
    if (status != CLI$_ABSENT)
        *ptr++ = 'x';
#endif

    /*
    **  Write binary files in VMS binary (fixed-length, 512-byte records,
    **  record attributes: none) format
    **  (auto-convert, or force to convert all files)
    */
    status = cli$present (&cli_binary);
    if (status != CLI$_ABSENT) {
        *ptr++ = '-';
        *ptr++ = '-';
        *ptr++ = 'b';
        if ((status & 1) &&
            !((status = cli$present (&cli_binary_none)) & 1)) {
            *ptr++ = 'b';
            if ((status = cli$present (&cli_binary_all)) & 1)
                *ptr++ = 'b';
        }
    }

    /*
    **  Convert files as text (CR LF -> LF, etc.)
    **  (auto-convert, or force to convert all files)
    */
    status = cli$present (&cli_text);
    if (status != CLI$_ABSENT) {
        *ptr++ = '-';
        *ptr++ = '-';
        *ptr++ = 'a';
        if ((status & 1) &&
            !((status = cli$present (&cli_text_none)) & 1)) {
            *ptr++ = 'a';
            if ((status = cli$present (&cli_text_all)) & 1)
                *ptr++ = 'a';
        }
    }

    /*
    **  Extract files to screen?
    */
    status = cli$present (&cli_screen);
    if (status == CLI$_NEGATED)
        *ptr++ = '-';
    if (status != CLI$_ABSENT)
        *ptr++ = 'c';

    /*
    **  Re-create directory structure?  (default)
    */
    status = cli$present (&cli_directory);
    if (status == CLI$_PRESENT) {
        status = cli$get_value (&cli_directory, &output_directory);
    }

    /*
    **  Freshen existing files, create none
    */
    status = cli$present (&cli_freshen);
    if (status == CLI$_NEGATED)
        *ptr++ = '-';
    if (status != CLI$_ABSENT)
        *ptr++ = 'f';

    /*
    **  Show the help.
    */
    status = cli$present (&cli_help);
    if (status & 1)
        *ptr++ = 'h';

    /*
    **  Junk stored directory names on unzip
    */
    status = cli$present (&cli_junk);
    if (status == CLI$_NEGATED)
        *ptr++ = '-';
    if (status != CLI$_ABSENT)
        *ptr++ = 'j';

    /*
    **  List contents (/BRIEF or /FULL (default))
    */
    status = cli$present (&cli_list);
    if (status & 1) {
        if (cli$present(&cli_full) & 1)
           *ptr++ = 'v';
        else
           *ptr++ = 'l';
    }

    /*
    **  Overwrite files?
    */
    status = cli$present (&cli_overwrite);
    if (status == CLI$_NEGATED)
        *ptr++ = 'n';
    else if (status != CLI$_ABSENT)
        *ptr++ = 'o';

    /*
    **  Decryption password from command line?
    */
    status = cli$present (&cli_password);
    if (status == CLI$_PRESENT) {
        status = cli$get_value (&cli_password, &password_arg);
    }

    /*
    **  Pipe files to SYS$OUTPUT with no informationals?
    */
    status = cli$present (&cli_pipe);
    if (status != CLI$_ABSENT)
        *ptr++ = 'p';

    /*
    **  Quiet
    */
    status = cli$present (&cli_quiet);
    if (status & 1) {
        *ptr++ = 'q';
        if ((status = cli$present (&cli_super_quiet)) & 1)
            *ptr++ = 'q';
    }

    /*
    **  Test archive integrity
    */
    status = cli$present (&cli_test);
    if (status == CLI$_NEGATED)
        *ptr++ = '-';
    if (status != CLI$_ABSENT)
        *ptr++ = 't';

    /*
    **  Make (some) names lowercase
    */
    status = cli$present (&cli_lowercase);
    if (status == CLI$_NEGATED)
        *ptr++ = '-';
    if (status != CLI$_ABSENT)
        *ptr++ = 'L';

    /*
    **  Uppercase (don't convert to lower)
    */
    status = cli$present (&cli_uppercase);
    if (status == CLI$_NEGATED)
        *ptr++ = '-';
    if (status != CLI$_ABSENT)
        *ptr++ = 'U';

    /*
    **  Update (extract only new and newer files)
    */
    status = cli$present (&cli_update);
    if (status == CLI$_NEGATED)
        *ptr++ = '-';
    if (status != CLI$_ABSENT)
        *ptr++ = 'u';

    /*
    **  Version (retain VMS/DEC-20 file versions)
    */
    status = cli$present (&cli_version);
    if (status == CLI$_NEGATED)
        *ptr++ = '-';
    if (status != CLI$_ABSENT)
        *ptr++ = 'V';

    /*
    **  Restore owner/protection info
    */
    status = cli$present (&cli_restore);
    if (status == CLI$_NEGATED)
        *ptr++ = '-';
    if (status != CLI$_ABSENT)
        *ptr++ = 'X';

    /*
    **  Display only the archive comment
    */
    status = cli$present (&cli_comment);
    if (status == CLI$_NEGATED)
        *ptr++ = '-';
    if (status != CLI$_ABSENT)
        *ptr++ = 'z';

    }   /* ZipInfo check way up there.... */

    /* The following options are common to both UnZip and ZipInfo mode. */

    /*
    **  Match filenames case-insensitively (-C)
    */
    status = cli$present (&cli_case_insensitive);
    if (status == CLI$_NEGATED)
        *ptr++ = '-';
    if (status != CLI$_ABSENT)
        *ptr++ = 'C';

    /*
    **  Use builtin pager for all screen output
    */
    status = cli$present (&cli_page);
    if (status == CLI$_NEGATED)
        *ptr++ = '-';
    if (status != CLI$_ABSENT)
        *ptr++ = 'M';

    /*
    **  Check existence of a list of files to exclude, fetch is done later.
    */
    status = cli$present (&cli_exclude);
    exclude_list = ((status & 1) != 0);

    /*
    **  If the user didn't give any DCL qualifier, assume he wants the
    **  Un*x interface.
    if ( (ptr == &options[1]) &&
         (output_directory.dsc$w_length == 0) &&
         (password_arg.dsc$w_length == 0) &&
         (!exclude_list)  )
      return(SS$_NORMAL);
    */

    /*
    **  Now copy the final options string to the_cmd_line.
    */
    x = ptr - &options[0];
    if (x > 1) {
        options[x] = '\0';
        len = strlen(the_cmd_line) + x + 2;
        if ((the_cmd_line = (char *) realloc (the_cmd_line, len)) == NULL)
            return(SS$_INSFMEM);
        strcat (the_cmd_line, " ");
        strcat (the_cmd_line, options);
    }

    /*
    **  If specified, add the decryption password argument.
    **/
    if (password_arg.dsc$w_length != 0) {
        len = strlen(the_cmd_line) + password_arg.dsc$w_length + 5;
        if ((the_cmd_line = (char *) realloc (the_cmd_line, len)) == NULL)
            return(SS$_INSFMEM);
        strcat (the_cmd_line, " -P ");
        x = strlen(the_cmd_line);
        strncpy(&the_cmd_line[x], password_arg.dsc$a_pointer,
                password_arg.dsc$w_length);
        the_cmd_line[len] = '\0';
    }

    /*
    **  Now get the specified zip file name.
    */
    status = cli$present (&cli_zipfile);
    if (status & 1) {
        status = cli$get_value (&cli_zipfile, &work_str);

        len = strlen(the_cmd_line) + work_str.dsc$w_length + 2;
        if ((the_cmd_line = (char *) realloc (the_cmd_line, len)) == NULL)
            return(SS$_INSFMEM);
        strcat (the_cmd_line, " ");
        x = strlen(the_cmd_line);
        strncpy(&the_cmd_line[x], work_str.dsc$a_pointer,
                work_str.dsc$w_length);
        the_cmd_line[len] = '\0';

    }

    /*
    **  Get the output directory, for UnZip.
    **/
    if (output_directory.dsc$w_length != 0) {
        len = strlen(the_cmd_line) + output_directory.dsc$w_length + 5;
        if ((the_cmd_line = (char *) realloc (the_cmd_line, len)) == NULL)
            return(SS$_INSFMEM);
        strcat (the_cmd_line, " -d ");
        x = strlen(the_cmd_line);
        strncpy(&the_cmd_line[x], output_directory.dsc$a_pointer,
                output_directory.dsc$w_length);
        the_cmd_line[len] = '\0';
    }

    /*
    **  Run through the list of files to unzip.
    */
    status = cli$present (&cli_infile);
    if (status & 1) {
        len = strlen(the_cmd_line) + 2;
        if ((the_cmd_line = (char *) realloc (the_cmd_line, len)) == NULL)
            return(SS$_INSFMEM);
        strcat (the_cmd_line, " ");
        status = get_list (&cli_infile, &the_cmd_line, &foreign_cmdline, ' ');
        if (!(status & 1)) return (status);
    }

    /*
    **  Get the list of files to exclude, if there are any.
    */
    if (exclude_list) {
        len = strlen(the_cmd_line) + 5;
        if ((the_cmd_line = (char *) realloc (the_cmd_line, len)) == NULL)
            return(SS$_INSFMEM);
        strcat (the_cmd_line, " -x ");
        status = get_list (&cli_exclude, &the_cmd_line, &foreign_cmdline, ' ');
        if (!(status & 1)) return (status);
    }

    /*
    **  Now that we've built our new UNIX-like command line, count the
    **  number of args and build an argv array.
    */

#if defined(TEST) || defined(DEBUG)
    printf("%s\n",the_cmd_line);
#endif /* TEST || DEBUG */

    new_argc = 1;
    for (ptr = the_cmd_line;
         (ptr = strchr(ptr,' ')) != NULL;
         ptr++, new_argc++);

    /*
    **  Allocate memory for the new argv[].  The last element of argv[]
    **  is supposed to be NULL, so allocate enough for new_argc+1.
    */
    if ((new_argv = (char **) calloc (new_argc+1, sizeof(char *))) == NULL)
        return(SS$_INSFMEM);

    /*
    **  For each option, store the address in new_argv[] and convert the
    **  separating blanks to nulls so each argv[] string is terminated.
    */
    for (ptr = the_cmd_line, x = 0; x < new_argc; x++) {
        new_argv[x] = ptr;
        if ((ptr = strchr (ptr, ' ')) != NULL)
            *ptr++ = '\0';
    }
    new_argv[new_argc] = NULL;

#if defined(TEST) || defined(DEBUG)
    printf("new_argc    = %d\n", new_argc);
    for (x = 0; x < new_argc; x++)
        printf("new_argv[%d] = %s\n", x, new_argv[x]);
#endif /* TEST || DEBUG */

    /*
    **  All finished.  Return the new argc and argv[] addresses to Zip.
    */
    *argc_p = new_argc;
    *argv_p = new_argv;

    return(SS$_NORMAL);
}



unsigned long
get_list (struct dsc$descriptor_s *qual, char **str,
          struct dsc$descriptor_d *cmdline, char c)
{
/*
**  Routine:    get_list
**
**  Function:   This routine runs through a comma-separated CLI list
**              and copies the strings to the command line.  The
**              specified separation character is used to separate
**              the strings on the command line.
**
**              All unquoted strings are converted to lower-case.
**
**  Formal parameters:
**
**      qual    - Address of descriptor for the qualifier name
**      str     - Address of pointer pointing to string (command line)
**      cmdline - Address of descriptor for the full command line tail
**      c       - Character to use to separate the list items
**
*/

    register status;
    struct dsc$descriptor_d work_str;

    init_dyndesc(work_str);

    status = cli$present (qual);
    if (status & 1) {

        unsigned long len, old_len, lower_it, ind, sind;

        len = strlen(*str);
        while ((status = cli$get_value (qual, &work_str)) & 1) {
            /*
            **  Just in case the string doesn't exist yet, though it does.
            */
            if (*str == NULL) {
                len = work_str.dsc$w_length + 1;
                if ((*str = (char *) malloc (work_str.dsc$w_length)) == NULL)
                    return(SS$_INSFMEM);
                strncpy(*str,work_str.dsc$a_pointer,len);
            } else {
                char *src, *dst; int x;
                old_len = len;
                len += work_str.dsc$w_length + 1;
                if ((*str = (char *) realloc (*str, len)) == NULL)
                    return(SS$_INSFMEM);

                /*
                **  Look for the filename in the original foreign command
                **  line to see if it was originally quoted.  If so, then
                **  don't convert it to lowercase.
                */
                lower_it = 0;
                str$find_first_substring (cmdline, &ind, &sind, &work_str);
                if (*(cmdline->dsc$a_pointer + ind - 2) == '"')
                    lower_it = 1;

                /*
                **  Copy the string to the buffer, converting to lowercase.
                */
                src = work_str.dsc$a_pointer;
                dst = *str+old_len;
                for (x = 0; x < work_str.dsc$w_length; x++) {
                    if (!lower_it && ((*src >= 'A') && (*src <= 'Z')))
                        *dst++ = *src++ + 32;
                    else
                        *dst++ = *src++;
                }
            }
            if (status == CLI$_COMMA)
                (*str)[len-1] = c;
            else
                (*str)[len-1] = '\0';
        }
    }

    return (SS$_NORMAL);

}


unsigned long
check_cli (struct dsc$descriptor_s *qual)
{
/*
**  Routine:    check_cli
**
**  Function:   Check to see if a CLD was used to invoke the program.
**
**  Formal parameters:
**
**      qual    - Address of descriptor for qualifier name to check.
**
*/
    lib$establish(lib$sig_to_ret);      /* Establish condition handler */
    return (cli$present(qual));         /* Just see if something was given */
}


#ifdef SFX

#ifdef SFX_EXDIR
#  define SFXOPT_EXDIR "\n                   and /DIRECTORY=exdir-spec"
#else
#  define SFXOPT_EXDIR ""
#endif

#ifdef MORE
#  define SFXOPT1 "/PAGE, "
#else
#  define SFXOPT1 ""
#endif

int VMSCLI_usage(__GPRO__ int error)    /* returns PK-type error code */
{
    extern char UnzipSFXBanner[];
#ifdef BETA
    extern char BetaVersion[];
#endif
    int flag;

    if (!show_VMSCLI_usage)
       return usage(__G__ error);

    flag = (error? 1 : 0);

    Info(slide, flag, ((char *)slide, UnzipSFXBanner,
      UZ_MAJORVER, UZ_MINORVER, PATCHLEVEL, BETALEVEL, VERSION_DATE));
    Info(slide, flag, ((char *)slide, "\
Valid main options are /TEST, /FRESHEN, /UPDATE, /PIPE, /SCREEN, /COMMENT%s.\n"
      SFXOPT_EXDIR));
    Info(slide, flag, ((char *)slide, "\
Modifying options are /TEXT, /BINARY, /JUNK, /[NO]OVERWRITE, /QUIET,\n\
                      /CASE_INSENSITIVE, /LOWERCASE, %s/VERSION, /RESTORE.\n",
      SFXOPT1));
#ifdef BETA
    Info(slide, flag, ((char *)slide, BetaVersion, "\n", "SFX"));
#endif

    if (error)
        return PK_PARAM;
    else
        return PK_COOL;     /* just wanted usage screen: no error */

} /* end function usage() */


#else /* !SFX */

int VMSCLI_usage(__GPRO__ int error)    /* returns PK-type error code */
{
    extern char UnzipUsageLine1[];
#ifdef BETA
    extern char BetaVersion[];
#endif
    int flag;

    if (!show_VMSCLI_usage)
       return usage(__G__ error);

/*---------------------------------------------------------------------------
    If user requested usage, send it to stdout; else send to stderr.
  ---------------------------------------------------------------------------*/

    flag = (error? 1 : 0);


/*---------------------------------------------------------------------------
    Print either ZipInfo usage or UnZip usage, depending on incantation.
  ---------------------------------------------------------------------------*/

    if (G.zipinfo_mode) {

#ifndef NO_ZIPINFO

        Info(slide, flag, ((char *)slide, "\
ZipInfo %d.%d%d%s %s, by Newtware and the fine folks at Info-ZIP.\n\n\
List name, date/time, attribute, size, compression method, etc., about files\n\
in list (excluding those in xlist) contained in the specified .zip archive(s).\
\n\"file[.zip]\" may be a wildcard name containing * or % (e.g., \"*font-%.zip\
\").\n", ZI_MAJORVER, ZI_MINORVER, PATCHLEVEL, BETALEVEL, VERSION_DATE));

        Info(slide, flag, ((char *)slide, "\
   usage:  zipinfo file[.zip] [list] [/EXCL=(xlist)] [/DIR=exdir] /options\n\
   or:  unzip /ZIPINFO file[.zip] [list] [/EXCL=(xlist)] [/DIR=exdir] /options\
\n\nmain\
 listing-format options:              /SHORT   short \"ls -l\" format (def.)\n\
  /ONE_LINE  just filenames, one/line     /MEDIUM  medium Unix \"ls -l\" format\n\
  /VERBOSE   verbose, multi-page format   /LONG    long Unix \"ls -l\" format\n\
"));

        Info(slide, flag, ((char *)slide, "\
miscellaneous options:\n  \
/HEADER   print header line       /TOTALS  totals for listed files or for all\n\
  /COMMENT  print zipfile comment   /TIMES   times in sortable decimal format\n\
  /[NO]CASE_INSENSITIVE  match filenames case-insensitively\n\
  /[NO]PAGE page output through built-in \"more\"\n\
  /EXCLUDE=(file-spec1,etc.)  exclude file-specs from listing\n"));

        Info(slide, flag, ((char *)slide, "\n\
Type unzip \"-Z\" for Unix style flags\n\
Remember that non-lowercase filespecs must be\
 quoted in VMS (e.g., \"Makefile\").\n"));

#endif /* !NO_ZIPINFO */

    } else {   /* UnZip mode */

        Info(slide, flag, ((char *)slide, UnzipUsageLine1,
          UZ_MAJORVER, UZ_MINORVER, PATCHLEVEL, BETALEVEL, VERSION_DATE));

#ifdef BETA
        Info(slide, flag, ((char *)slide, BetaVersion, "", ""));
#endif

        Info(slide, flag, ((char *)slide, "\
Usage: unzip file[.zip] [list] [/EXCL=(xlist)] [/DIR=exdir] /options /modifiers\
\n  Default action is to extract files in list, except those in xlist, to exdir\
;\n  file[.zip] may be a wildcard.  %s\n\n",
#ifdef NO_ZIPINFO
          "(ZipInfo mode is disabled in this version.)"
#else
          "Type \"unzip /ZIPINFO\" for ZipInfo-mode usage."
#endif
          ));

        Info(slide, flag, ((char *)slide, "\
Major options include (type unzip -h for Unix style flags):\n\
   /[NO]TEST, /LIST, /[NO]SCREEN, /PIPE, /[NO]FRESHEN, /[NO]UPDATE,\n\
   /[NO]COMMENT, /DIRECTORY=directory-spec, /EXCLUDE=(file-spec1,etc.)\n\n\
Modifiers include:\n\
   /BRIEF, /FULL, /[NO]TEXT[=NONE|AUTO|ALL], /[NO]BINARY[=NONE|AUTO|ALL],\n\
   /[NO]OVERWRITE, /[NO]JUNK, /QUIET, /QUIET[=SUPER], /[NO]PAGE,\n\
   /[NO]CASE_INSENSITIVE, /[NO]LOWERCASE, /[NO]VERSION, /[NO]RESTORE\n\n"));

        Info(slide, flag, ((char *)slide, "\
Examples (see unzip.doc or \"HELP UNZIP\" for more info):\n   \
unzip edit1 /EXCL=joe.jou /CASE_INSENSITIVE    => extract all files except\n   \
   joe.jou (or JOE.JOU, or any combination of case) from zipfile edit1.zip\n   \
unzip zip201 \"Makefile.VMS\" vms/*.[ch]         => extract VMS Makefile and\n\
      *.c and *.h files; must quote uppercase names if /CASE_INSENS not used\n\
   unzip foo /DIR=tmp:[.test] /JUNK /AUTO /OVER   => extract all files to temp.\
\n      directory without paths, auto-converting text files and overwriting\
\n"));

    } /* end if (zipinfo_mode) */

    if (error)
        return PK_PARAM;
    else
        return PK_COOL;     /* just wanted usage screen: no error */

} /* end function VMSCLI_usage() */

#endif /* ?SFX */
