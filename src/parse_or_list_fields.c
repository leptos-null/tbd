//
//  src/parse_or_list_fields.c
//  tbd
//
//  Created by inoahdev on 12/02/18.
//  Copyright © 2018 - 2019 inoahdev. All rights reserved.
//

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>

#include <stdio.h>
#include <stdint.h>

#include <stdlib.h>
#include <string.h>

#include "arch_info.h"
#include "macho_file.h"

#include "parse_or_list_fields.h"
#include "tbd.h"

uint64_t
parse_architectures_list(int *__notnull const index_in,
                         const int argc,
                         const char *const *__notnull const argv,
                         uint64_t *__notnull const count_out)
{
    uint64_t archs = 0;
    uint64_t count = 0;

    int index = *index_in;
    const struct arch_info *const arch_info_list = arch_info_get_list();

    do {
        const char *const arch = argv[index];

        /*
         * Quickly check whether our arch-string is either a path-string or
         * an option to avoid an unnecessary arch-info lookup.
         */

        const char arch_front = arch[0];
        if (arch_front == '-' || arch_front == '/') {
            if (archs == 0) {
                fputs("Please provide a list of architectures\n", stderr);
                exit(1);
            }

            break;
        }

        const struct arch_info *const arch_info = arch_info_for_name(arch);
        if (arch_info == NULL) {
            /*
             * At least one architecture must be provided for the list.
             */

            if (archs == 0) {
                fprintf(stderr,
                        "Unrecognized architecture (with name %s)\n",
                        arch);

                exit(1);
            }

            break;
        }

        const uint64_t arch_info_index = (uint64_t)(arch_info - arch_info_list);
        const uint64_t arch_info_mask = 1ull << arch_info_index;

        archs |= arch_info_mask;

        index++;
        count++;

        if (index == argc) {
            break;
        }
    } while (true);


    /*
     * Subtract one from index as we're supposed to end with the index pointing
     * to the last argument.
     */

    *count_out = count;
    *index_in = index - 1;

    return archs;
}

uint32_t
parse_flags_list(int *__notnull const index_in,
                 const int argc,
                 const char *const *__notnull const argv)
{
    int index = *index_in;
    uint32_t flags = 0;

    for (; index != argc; index++) {
        const char *const arg = argv[index];
        if (strcmp(arg, "flat_namespace") == 0) {
            if (flags & TBD_FLAG_FLAT_NAMESPACE) {
                fputs("Note: tbd-flag flat_namespace was provided twice\n",
                      stdout);
            } else {
                flags |= TBD_FLAG_FLAT_NAMESPACE;
            }
        } else if (strcmp(arg, "not_app_extension_safe") == 0) {
            if (flags & TBD_FLAG_NOT_APP_EXTENSION_SAFE) {
                fputs("Note: tbd-flag not_app_extension_safe was provided "
                      "twice\n",
                      stdout);
            } else {
                flags |= TBD_FLAG_NOT_APP_EXTENSION_SAFE;
            }
        } else {
            if (flags != 0) {
                break;
            }

            const char front = arg[0];
            if (front == '-' || front == '/') {
                fputs("Please provide a list of tbd-flags\n", stderr);
            } else {
                fprintf(stderr, "Unrecognized flag: %s\n", arg);
            }

            exit(1);
        }
    }

    /*
     * Subtract one from index as we're supposed to end with the index pointing
     * to the last argument.
     */

    *index_in = index - 1;
    return flags;
}

enum tbd_objc_constraint
parse_objc_constraint(const char *__notnull const constraint) {
    if (strcmp(constraint, "none") == 0) {
        return TBD_OBJC_CONSTRAINT_NONE;
    } else if (strcmp(constraint, "retain_release") == 0) {
        return TBD_OBJC_CONSTRAINT_RETAIN_RELEASE;
    } else if (strcmp(constraint, "retain_release_for_simulator") == 0) {
        return TBD_OBJC_CONSTRAINT_RETAIN_RELEASE_FOR_SIMULATOR;
    } else if (strcmp(constraint, "retain_release_or_gc") == 0) {
        return TBD_OBJC_CONSTRAINT_RETAIN_RELEASE_OR_GC;
    } else if (strcmp(constraint, "gc") == 0) {
        return TBD_OBJC_CONSTRAINT_GC;
    }

    return TBD_OBJC_CONSTRAINT_NO_VALUE;
}

uint32_t parse_swift_version(const char *__notnull const arg) {
    if (strcmp(arg, "1.2") == 0) {
        return 2;
    }

    uint32_t version = 0;
    const char *iter = arg;

    for (char ch = *iter; ch != '\0'; ch = *(++iter)) {
        if (isdigit(ch) == 0) {
            return 0;
        }

        const uint32_t new_version = (version * 10) + (ch & 0xf);

        /*
         * Check for any overflows when parsing out the number.
         */

        if (new_version < version) {
            return 0;
        }

        version = new_version;
    }

    if (version > 1) {
        version++;
    }

    return version;
}

enum tbd_platform parse_platform(const char *__notnull const platform) {
    if (strcmp(platform, "macosx") == 0) {
        return TBD_PLATFORM_MACOS;
    } else if (strcmp(platform, "ios") == 0) {
        return TBD_PLATFORM_IOS;
    } else if (strcmp(platform, "watchos") == 0) {
        return TBD_PLATFORM_WATCHOS;
    } else if (strcmp(platform, "tvos") == 0) {
        return TBD_PLATFORM_TVOS;
    } else if (strcmp(platform, "bridgeos") == 0) {
        return TBD_PLATFORM_BRIDGEOS;
    } else if (strcmp(platform, "iosmac") == 0) {
        return TBD_PLATFORM_IOSMAC;
    } else if (strcmp(platform, "zippered") == 0) {
        return TBD_PLATFORM_ZIPPERED;
    }

    return TBD_PLATFORM_NONE;
}

enum tbd_version parse_tbd_version(const char *__notnull const version) {
    if (strcmp(version, "v1") == 0) {
        return TBD_VERSION_V1;
    } else if (strcmp(version, "v2") == 0) {
        return TBD_VERSION_V2;
    } else if (strcmp(version, "v3") == 0) {
        return TBD_VERSION_V3;
    }

    return TBD_VERSION_NONE;
}

void print_arch_info_list(void) {
    const struct arch_info *info = arch_info_get_list();
    for (const char *name = info->name; name != NULL; name = (++info)->name) {
        fprintf(stdout, "%s\n", name);
    }
}

void print_objc_constraint_list(void) {
    fputs("none\n"
          "retain_release\n"
          "retain_release_or_gc\n"
          "retain_release_for_simulator\n"
          "gc\n",
          stdout);
}

void print_platform_list(void) {
    fputs("macosx\n"
          "ios\n"
          "watchos\n"
          "tvos\n"
          "bridgeos\n"
          "iosmac (Not yet found in mach-o binaries, but supported)\n"
          "zippered (Not yet found in mach-o binaries, but supported)\n",
          stdout);
}

void print_tbd_flags_list(void) {
    fputs("flat_namespace\n"
          "not_app_extension_safe\n",
          stdout);
}

void print_tbd_version_list(void) {
    fputs("v1\n"
          "v2\n"
          "v3\n",
          stdout);
}
