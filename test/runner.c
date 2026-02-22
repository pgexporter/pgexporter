/*
 * Copyright (C) 2026 The pgexporter community
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list
 * of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 * be used to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <mctf.h>
#include <html_report.h>
#include <tscommon.h>

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "logging.h"
#include "utils.h"

static struct sigaction old_sa_abrt;
static struct sigaction old_sa_segv;

static siginfo_t saved_siginfo_abrt;
static siginfo_t saved_siginfo_segv;
static ucontext_t saved_ucontext_abrt;
static ucontext_t saved_ucontext_segv;

static void
usage(const char* progname)
{
   printf("Usage: %s [OPTIONS]\n", progname);
   printf("Options:\n");
   printf("  -t, --test NAME    Run only tests matching NAME (test name pattern)\n");
   printf("  -m, --module NAME Run all tests in module NAME\n");
   printf("  -h, --help         Show this help message\n");
   printf("\n");
   printf("Examples:\n");
   printf("  %s                 Run full test suite\n", progname);
   printf("  %s -m cli          Run all tests in 'cli' module\n", progname);
   printf("  %s -t test_cli_ping Run test matching 'test_cli_ping'\n", progname);
   printf("\n");
}

static void
sigabrt_handler_siginfo(int sig, siginfo_t* info, void* context)
{
   if (info != NULL)
   {
      saved_siginfo_abrt = *info;
   }
   if (context != NULL)
   {
      saved_ucontext_abrt = *(ucontext_t*)context;
   }

   char* bt = NULL;
   char* os = NULL;
   int kernel_major = 0, kernel_minor = 0, kernel_patch = 0;

   fprintf(stderr, "\n========================================\n");
   fprintf(stderr, "FATAL: Received SIGABRT (assertion failure)\n");
   fprintf(stderr, "========================================\n\n");

   if (pgexporter_os_kernel_version(&os, &kernel_major, &kernel_minor, &kernel_patch) == 0)
   {
      fprintf(stderr, "System: %s %d.%d.%d\n\n", os ? os : "Unknown",
              kernel_major, kernel_minor, kernel_patch);
      if (os)
      {
         free(os);
      }
   }

   if (pgexporter_backtrace_string(&bt) == 0 && bt != NULL)
   {
      fprintf(stderr, "%s\n", bt);

      if (pgexporter_log_is_enabled(PGEXPORTER_LOGGING_LEVEL_DEBUG1))
      {
         pgexporter_backtrace();
      }

      free(bt);
   }
   else
   {
      fprintf(stderr, "Failed to generate backtrace\n");
   }

   fprintf(stderr, "\n========================================\n");
   fflush(stderr);

   if (old_sa_abrt.sa_flags & SA_SIGINFO)
   {
      if (getenv("PGEXPORTER_TEST_DEBUG_SIGNALS") != NULL)
      {
         fprintf(stderr, "DEBUG: Chaining to previous SIGABRT handler (SA_SIGINFO) at %p\n", (void*)old_sa_abrt.sa_sigaction);
      }
      old_sa_abrt.sa_sigaction(sig, &saved_siginfo_abrt, &saved_ucontext_abrt);
   }
   else if (old_sa_abrt.sa_handler == SIG_IGN)
   {
      return;
   }
   else if (old_sa_abrt.sa_handler == SIG_DFL)
   {
      signal(SIGABRT, SIG_DFL);
      abort();
   }
   else
   {
      if (getenv("PGEXPORTER_TEST_DEBUG_SIGNALS") != NULL)
      {
         fprintf(stderr, "DEBUG: Chaining to previous SIGABRT handler (simple) at %p\n", (void*)old_sa_abrt.sa_handler);
      }
      old_sa_abrt.sa_handler(sig);
   }
}

static void
sigsegv_handler_siginfo(int sig, siginfo_t* info, void* context)
{
   if (info != NULL)
   {
      saved_siginfo_segv = *info;
   }
   if (context != NULL)
   {
      saved_ucontext_segv = *(ucontext_t*)context;
   }

   char* bt = NULL;
   char* os = NULL;
   int kernel_major = 0, kernel_minor = 0, kernel_patch = 0;

   fprintf(stderr, "\n========================================\n");
   fprintf(stderr, "FATAL: Received SIGSEGV (segmentation fault)\n");
   fprintf(stderr, "========================================\n\n");

   if (pgexporter_os_kernel_version(&os, &kernel_major, &kernel_minor, &kernel_patch) == 0)
   {
      fprintf(stderr, "System: %s %d.%d.%d\n\n", os ? os : "Unknown",
              kernel_major, kernel_minor, kernel_patch);
      if (os)
      {
         free(os);
      }
   }

   if (pgexporter_backtrace_string(&bt) == 0 && bt != NULL)
   {
      fprintf(stderr, "%s\n", bt);

      if (pgexporter_log_is_enabled(PGEXPORTER_LOGGING_LEVEL_DEBUG1))
      {
         pgexporter_backtrace();
      }

      free(bt);
   }
   else
   {
      fprintf(stderr, "Failed to generate backtrace\n");
   }

   fprintf(stderr, "\n========================================\n");
   fflush(stderr);

   if (old_sa_segv.sa_flags & SA_SIGINFO)
   {
      if (getenv("PGEXPORTER_TEST_DEBUG_SIGNALS") != NULL)
      {
         fprintf(stderr, "DEBUG: Chaining to previous SIGSEGV handler (SA_SIGINFO) at %p\n", (void*)old_sa_segv.sa_sigaction);
         fprintf(stderr, "DEBUG: Fault address: %p\n", (void*)saved_siginfo_segv.si_addr);
         fflush(stderr);
      }
      old_sa_segv.sa_sigaction(sig, &saved_siginfo_segv, &saved_ucontext_segv);
   }
   else if (old_sa_segv.sa_handler == SIG_IGN)
   {
      return;
   }
   else if (old_sa_segv.sa_handler == SIG_DFL)
   {
      signal(SIGSEGV, SIG_DFL);
      raise(SIGSEGV);
   }
   else
   {
      if (getenv("PGEXPORTER_TEST_DEBUG_SIGNALS") != NULL)
      {
         fprintf(stderr, "DEBUG: Chaining to previous SIGSEGV handler (simple) at %p\n", (void*)old_sa_segv.sa_handler);
      }
      old_sa_segv.sa_handler(sig);
   }
}

static void
setup_signal_handlers(void)
{
   struct sigaction sa_abrt, sa_segv;

   memset(&sa_abrt, 0, sizeof(sa_abrt));
   sa_abrt.sa_sigaction = sigabrt_handler_siginfo;
   sigemptyset(&sa_abrt.sa_mask);
   sa_abrt.sa_flags = SA_SIGINFO;
   if (sigaction(SIGABRT, &sa_abrt, &old_sa_abrt) != 0)
   {
      fprintf(stderr, "Warning: Failed to setup SIGABRT handler: %s\n", strerror(errno));
   }

   memset(&sa_segv, 0, sizeof(sa_segv));
   sa_segv.sa_sigaction = sigsegv_handler_siginfo;
   sigemptyset(&sa_segv.sa_mask);
   sa_segv.sa_flags = SA_SIGINFO;
   if (sigaction(SIGSEGV, &sa_segv, &old_sa_segv) != 0)
   {
      fprintf(stderr, "Warning: Failed to setup SIGSEGV handler: %s\n", strerror(errno));
   }
   else if (getenv("PGEXPORTER_TEST_DEBUG_SIGNALS") != NULL)
   {
      if (old_sa_segv.sa_handler == SIG_DFL)
      {
         fprintf(stderr, "DEBUG: Previous SIGSEGV handler was SIG_DFL\n");
      }
      else if (old_sa_segv.sa_handler == SIG_IGN)
      {
         fprintf(stderr, "DEBUG: Previous SIGSEGV handler was SIG_IGN\n");
      }
      else if (old_sa_segv.sa_flags & SA_SIGINFO)
      {
         fprintf(stderr, "DEBUG: Previous SIGSEGV handler was custom (SA_SIGINFO): %p\n", (void*)old_sa_segv.sa_sigaction);
      }
      else
      {
         fprintf(stderr, "DEBUG: Previous SIGSEGV handler was custom: %p\n", (void*)old_sa_segv.sa_handler);
      }
   }
}

static int
build_mctf_log_path(char* path, size_t size)
{
   char base[MAX_PATH];
   char* slash = NULL;
   int n;

   if (path == NULL || size == 0)
   {
      return 1;
   }

   memset(base, 0, sizeof(base));

   if (TEST_BASE_DIR[0] == '\0')
   {
      return 1;
   }

   memcpy(base, TEST_BASE_DIR, sizeof(base) - 1);
   base[sizeof(base) - 1] = '\0';

   slash = strrchr(base, '/');
   if (slash == NULL)
   {
      return 1;
   }

   *slash = '\0';

   n = snprintf(path, size, "%s/log/pgexporter-test.log", base);
   if (n <= 0 || (size_t)n >= size)
   {
      return 1;
   }

   return 0;
}

int
main(int argc, char* argv[])
{
   int number_failed = 0;
   const char* filter = NULL;
   mctf_filter_type_t filter_type = MCTF_FILTER_NONE;
   int c;
   bool env_created = false;
   char mctf_log_path[MAX_PATH];
   char html_report_path[MAX_PATH];

   static struct option long_options[] = {
      {"test", required_argument, 0, 't'},
      {"module", required_argument, 0, 'm'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};

   while ((c = getopt_long(argc, argv, "t:m:h", long_options, NULL)) != -1)
   {
      switch (c)
      {
         case 't':
         case 'm':
            if (filter_type != MCTF_FILTER_NONE)
            {
               fprintf(stderr, "Error: Cannot specify both -t and -m options\n");
               usage(argv[0]);
               return EXIT_FAILURE;
            }
            filter = optarg;
            filter_type = (c == 't') ? MCTF_FILTER_TEST : MCTF_FILTER_MODULE;
            break;
         case 'h':
            usage(argv[0]);
            return EXIT_SUCCESS;
         default:
            usage(argv[0]);
            return EXIT_FAILURE;
      }
   }

   setup_signal_handlers();

   if (getenv("PGEXPORTER_TEST_CONF") != NULL)
   {
      pgexporter_test_environment_create();
      env_created = true;
   }

   if (build_mctf_log_path(mctf_log_path, sizeof(mctf_log_path)) == 0)
   {
      if (mctf_open_log(mctf_log_path) != 0)
      {
         fprintf(stderr, "Warning: Failed to open MCTF log file at '%s'\n", mctf_log_path);
      }
   }

   mctf_log_environment();

   memset(html_report_path, 0, sizeof(html_report_path));
   bool html_report_available = (html_report_build_path(html_report_path, sizeof(html_report_path)) == 0);

   number_failed = mctf_run_tests(filter_type, filter);
   if (html_report_available)
   {
      html_report_generate(html_report_path, filter_type, filter);
   }
   mctf_print_summary();
   mctf_cleanup();

   mctf_close_log();

   if (env_created)
   {
      pgexporter_test_environment_destroy();
   }

   return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
