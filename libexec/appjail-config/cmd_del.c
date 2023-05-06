/*
 * Copyright (c) 2022-2023, Jesús Daniel Colmenares Oviedo <DtxdF@disroot.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <err.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "commands.h"
#include "template.h"
#include "templateerr.h"
#include "except.h"
#include "util.h"

#define CMD_DEL_DEFROW  ((templateindex)0)

#define CMD_DEL_TRBCK(trbck) \
    do { \
        except_addsf(EX_SOFTWARE, NULL, trbck); \
        return EX_SOFTWARE; \
    } while (0)

static inline void  freeall(void);

static void     mutually_exclusive(int c1, int c2);

static void     usage(void);

/* Traceback exception */
static traceback trbck;

/* Template */
static template t;
static FILE *template_stream;
static char *template_file;

/* Jail name */
static char *jail_name;

int
command_cmd_del(int argc, char **argv, int argi)
{
    templateindex row = CMD_DEL_DEFROW;
    int c;
    bool iflag = false,
         jflag = false,
         Pflag = false,
         tflag = false,
         Vflag = false;

    optind = argi;

    if (atexit(freeall) == -1)
        err(EX_OSERR, "atexit()");

    while ((c = getopt(argc, argv, ":iPVj:r:t:")) != -1) {
        switch (c) {
        case 'i':
            iflag = true;
            break;
        case 'P':
            Pflag = true;
            break;
        case 'V':
            Vflag = true;
            break;
        case 'j':
            jflag = true;
            jail_name = safe_strdup(optarg);
            break;
        case 'r':
            row = safe_strtoull(optarg);
            break;
        case 't':
            tflag = true;
            template_file = safe_strdup(optarg);
            break;
        default:
            usage();
        }
    }

    char *parameter = argv[optind++];

    /* Mandatory arguments. */
    if ((!jflag && !tflag) \
        || parameter == NULL) {
        usage();
    }

    /* Mutually exclusive. */
    if (jflag && tflag) {
        mutually_exclusive('j', 't');
    }

    /* Check for an invalid parameter. */
    if (!Vflag \
        && !template_checkvar(parameter) \
        && !template_checkparam(parameter)) {
        fprintf(stderr, "Invalid parameter `%s`\n",
            parameter);
        return EX_DATAERR;
    }

    /* Use jail's template as the template file. */
    if (jflag) {
        /* Check for an invalid jail name. */
        if (!test_jailname(jail_name)) {
            fprintf(stderr, "Invalid jail name: %s\n",
                jail_name);
            return EX_DATAERR;
        }

        template_file = get_jail_template(jail_name);
    }

    /* Traceback exception. */
    trbck = except_newtrbck();
    if (trbck == NULL)
        err(EX_OSERR, "except_newtrbck()");

    /* Template. */
    if (template_init(template_file, &t, &trbck) != TPERRN) {
        except_printerr(trbck);
        /* Free `trbck` blocks using except_free() to avoid printing 
         * the traceback.
         */
        except_free(&trbck);
        return EX_SOFTWARE;
    }

    /* Used when Pflag is true. */
    templateindex start = 0;
    bool unknown = true;

    for (;;) {
        param p;
        
        if (Pflag)
            p = template_yieldrow(t, parameter, &start);
        else
            p = template_getrow(t, row, parameter);

        if (p == NULL)
            break;
        else
            unknown = false;

        template_setignore(p);

        if (!Pflag)
            break;
    }

    if (unknown) {
        if (!iflag) {
            fprintf(stderr, "Unknown parameter -- %s",
                parameter);
            if (!Pflag)
                fprintf(stderr, ":%llu", row);
            fprintf(stderr, "\n");
        }
        return EX_DATAERR;
    }

    if ((template_stream = fopen(template_file, "w")) == NULL)
        CMD_DEL_TRBCK(&trbck);
    else
        setlinebuf(template_stream);

    if (template_printrows(t, template_stream, &trbck) != TPERRN)
        CMD_DEL_TRBCK(&trbck);

    return EXIT_SUCCESS;
}

static inline void
freeall(void)
{
    if (template_stream != NULL)
        fclose(template_stream);
    template_free(&t);
    free(jail_name);
    free(template_file);
    except_traceback(&trbck);
}

void
command_help_del(void)
{
    command_usage_del();

    fprintf(stderr, "\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "    -i                 -- Ignore unknown parameters.\n");
    fprintf(stderr, "    -P                 -- Delete all matching rows, not just one. The `-r`\n");
    fprintf(stderr, "                          parameter is ignored.\n");
    fprintf(stderr, "    -V                 -- Do not validate if `parameter` is valid against a list\n");
    fprintf(stderr, "                          extracted from `jail(8)` nor validate if `parameter` is\n");
    fprintf(stderr, "                          a correct variable.\n");
    fprintf(stderr, "    -j jail            -- Use jail's template as the template file.\n");
    fprintf(stderr, "    -r row             -- Delete the parameter with the row `row`. Default: %llu.\n",
        CMD_DEL_DEFROW);
    fprintf(stderr, "    -t template        -- Use `template` as the template file.\n");
}

static void
mutually_exclusive(int c1, int c2)
{
    fprintf(stderr, "Options `-%c` and `-%c` are mutually exclusive.\n",
        c1, c2);
    exit(EX_DATAERR);
}

static void
usage(void)
{
    command_usage_del();
    exit(EX_USAGE);
}

void
command_usage_del(void)
{
    fprintf(stderr, "usage: del [-iPV] [-r row] [-j jail | -t template] parameter\n");
}
