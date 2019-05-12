//
//  src/parse_dsc_for_main.c
//  tbd
//
//  Created by inoahdev on 12/01/18.
//  Copyright © 2018 - 2019 inoahdev. All rights reserved.
//

#include <sys/stat.h>
#include <errno.h>

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "handle_dsc_parse_result.h"
#include "parse_dsc_for_main.h"

#include "macho_file.h"
#include "path.h"

#include "recursive.h"
#include "unused.h"

struct image_error {
    const char *path;
    enum dsc_image_parse_result result;
};

struct dsc_iterate_images_callback_info {
    struct dyld_shared_cache_info *dsc_info;

    const char *dsc_dir_path;
    const char *dsc_name;

    char *write_path;
    uint64_t write_path_length;

    struct tbd_for_main *global;
    struct tbd_for_main *tbd;

    struct array images;
    uint64_t *retained_info;

    bool print_paths;
    bool parse_all_images;
    bool did_print_messages_header;
};

enum dyld_cache_image_info_pad {
    E_DYLD_CACHE_IMAGE_INFO_PAD_ALREADY_EXTRACTED = 1 << 0
};

static void
clear_create_info(struct tbd_create_info *const info_in,
                  const struct tbd_create_info *const orig)
{
    tbd_create_info_destroy(info_in);
    *info_in = *orig;
}

static void
print_messages_header(
    struct dsc_iterate_images_callback_info *const callback_info)
{
    if (!callback_info->did_print_messages_header) {
        if (callback_info->print_paths) {
            fprintf(stderr,
                    "Parsing dyld_shared_cache file (at path %s/%s) resulted "
                    "in the following warnings and errors:\n",
                    callback_info->dsc_dir_path,
                    callback_info->dsc_name);
        } else {
            fputs("Parsing the provided dyld_shared_cache file resulted in the "
                  "following warnings and errors:\n",
                  stderr);
        }

        callback_info->did_print_messages_header = true;
    }
}

static void
print_image_error(struct dsc_iterate_images_callback_info *const callback_info,
                  const char *const image_path,
                  const enum dsc_image_parse_result result)
{
    /*
     * We ignore warnings while recursing of any dsc-images lacking exports.
     */

    const struct tbd_for_main *const tbd = callback_info->tbd;
    if (result == E_DSC_IMAGE_PARSE_NO_EXPORTS) {
        const uint64_t flags = tbd->flags;
        if (flags & F_TBD_FOR_MAIN_IGNORE_WARNINGS) {
            if (flags & F_TBD_FOR_MAIN_RECURSE_DIRECTORIES) {
                return;
            }
        }
    }

    print_messages_header(callback_info);

    fputc('\t', stderr);
    print_dsc_image_parse_error(tbd, image_path, result);
}

static void
print_write_to_path_result(struct tbd_for_main *const tbd,
                           const char *const image_path,
                           const enum tbd_for_main_write_to_path_result result)
{
    switch (result) {
        case E_TBD_FOR_MAIN_WRITE_TO_PATH_OK:
            break;

        case E_TBD_FOR_MAIN_WRITE_TO_PATH_ALREADY_EXISTS:
            if (tbd->flags & F_TBD_FOR_MAIN_IGNORE_WARNINGS) {
                break;
            }

            fprintf(stderr,
                    "Image (with path %s) already has an existing file at "
                    "(one of) its write-paths that could not be overwritten. "
                    "Skipping\n",
                    image_path);

            break;

        case E_TBD_FOR_MAIN_WRITE_TO_PATH_WRITE_FAIL:
            fprintf(stderr,
                    "Image (with path %s) could not be parsed and written out "
                    "due to a write fail\n",
                    image_path);

            break;
    }
}

static void
print_write_error(struct dsc_iterate_images_callback_info *const callback_info,
                  struct tbd_for_main *const tbd,
                  const char *const image_path,
                  const enum tbd_for_main_write_to_path_result result)
{
    print_messages_header(callback_info);

    fputc('\t', stderr);
    print_write_to_path_result(tbd, image_path, result);
}

static enum tbd_for_main_write_to_path_result
write_out_tbd_info_for_single_filter_dir(struct tbd_for_main *const tbd,
                                         const char *const filter_dir,
                                         const char *const image_path,
                                         const uint64_t image_path_length)
{
    const uint64_t delta = (const uint64_t)(filter_dir - image_path);
    const uint64_t path_length = image_path_length - delta;

    uint64_t length = 0;
    char *const write_path =
        tbd_for_main_create_dsc_image_write_path(tbd,
                                                 tbd->write_path,
                                                 tbd->write_path_length,
                                                 filter_dir,
                                                 path_length,
                                                 "tbd",
                                                 3,
                                                 &length);

    const enum tbd_for_main_write_to_path_result result =
        tbd_for_main_write_to_path(tbd, write_path, length, true);

    free(write_path);

    if (result != E_TBD_FOR_MAIN_WRITE_TO_PATH_OK) {
        return result;
    }

    return E_TBD_FOR_MAIN_WRITE_TO_PATH_OK;
}

static enum tbd_for_main_write_to_path_result
write_out_tbd_info_for_single_filter_filename(struct tbd_for_main *const tbd,
                                              const char *const filter_filename,
                                              const uint64_t filter_length)
{
    uint64_t length = 0;
    char *const write_path =
        tbd_for_main_create_dsc_image_write_path(tbd,
                                                 tbd->write_path,
                                                 tbd->write_path_length,
                                                 filter_filename,
                                                 filter_length,
                                                 "tbd",
                                                 3,
                                                 &length);

    const enum tbd_for_main_write_to_path_result write_result =
        tbd_for_main_write_to_path(tbd, write_path, length, true);

    free(write_path);

    if (write_result != E_TBD_FOR_MAIN_WRITE_TO_PATH_OK) {
        return write_result;
    }

    return E_TBD_FOR_MAIN_WRITE_TO_PATH_OK;
}

static enum tbd_for_main_write_to_path_result
write_out_tbd_info_for_single_filter(
    const struct tbd_for_main_dsc_image_filter *const filter,
    struct tbd_for_main *const tbd,
    const char *const image_path,
    const uint64_t image_path_length)
{
    enum tbd_for_main_write_to_path_result result =
        E_TBD_FOR_MAIN_WRITE_TO_PATH_OK;

    switch (filter->type) {
        case TBD_FOR_MAIN_DSC_IMAGE_FILTER_TYPE_DIRECTORY:
            result =
                write_out_tbd_info_for_single_filter_dir(tbd,
                                                         filter->tmp_ptr,
                                                         image_path,
                                                         image_path_length);

            break;

        case TBD_FOR_MAIN_DSC_IMAGE_FILTER_TYPE_FILE:
            result =
                write_out_tbd_info_for_single_filter_filename(tbd,
                                                              filter->tmp_ptr,
                                                              filter->length);

            break;
    }

    return result;
}

static void
write_out_tbd_info_for_filters(
    struct dsc_iterate_images_callback_info *const info,
    struct tbd_for_main *const tbd,
    const char *const image_path,
    const uint64_t length)
{
    const struct array *const filters = &tbd->dsc_image_filters;

    struct tbd_for_main_dsc_image_filter *filter = filters->data;
    const struct tbd_for_main_dsc_image_filter *const end = filters->data_end;

    for (; filter != end; filter++) {
        uint64_t flags = filter->flags;
        if (!(flags & F_TBD_FOR_MAIN_DSC_IMAGE_CURRENTLY_PARSING)) {
            continue;
        }

        flags &= (const uint64_t)~F_TBD_FOR_MAIN_DSC_IMAGE_CURRENTLY_PARSING;
        flags |= F_TBD_FOR_MAIN_DSC_IMAGE_FOUND_ONE;

        filter->flags = flags;

        const enum tbd_for_main_write_to_path_result write_result =
            write_out_tbd_info_for_single_filter(filter,
                                                 tbd,
                                                 image_path,
                                                 length);

        if (write_result != E_TBD_FOR_MAIN_WRITE_TO_PATH_OK) {
            print_write_error(info, tbd, image_path, write_result);
            continue;
        }
    }
}

static enum tbd_for_main_write_to_path_result
write_out_tbd_info_for_image_path(
    const struct dsc_iterate_images_callback_info *const callback_info,
    const struct tbd_for_main *const tbd,
    const char *const image_path,
    const uint64_t image_path_length)
{
    uint64_t length = 0;
    char *const write_path =
        tbd_for_main_create_dsc_image_write_path(
            tbd,
            callback_info->write_path,
            callback_info->write_path_length,
            image_path,
            image_path_length,
            "tbd",
            3,
            &length);

    const enum tbd_for_main_write_to_path_result write_result =
        tbd_for_main_write_to_path(tbd, write_path, length, true);

    free(write_path);

    if (write_result != E_TBD_FOR_MAIN_WRITE_TO_PATH_OK) {
        return write_result;
    }

    return E_TBD_FOR_MAIN_WRITE_TO_PATH_OK;
}

static void
write_out_tbd_info_for_paths(
    struct dsc_iterate_images_callback_info *const info,
    struct tbd_for_main *const tbd,
    const char *const image_path,
    const uint64_t length)
{
    const struct array *const paths = &tbd->dsc_image_paths;

    struct tbd_for_main_dsc_image_path *path = paths->data;
    const struct tbd_for_main_dsc_image_path *const end = paths->data_end;

    for (; path != end; path++) {
        uint64_t flags = path->flags;
        if (!(flags & F_TBD_FOR_MAIN_DSC_IMAGE_CURRENTLY_PARSING)) {
            continue;
        }

        flags &= (const uint64_t)~F_TBD_FOR_MAIN_DSC_IMAGE_CURRENTLY_PARSING;
        flags |= F_TBD_FOR_MAIN_DSC_IMAGE_FOUND_ONE;

        path->flags = flags;

        const enum tbd_for_main_write_to_path_result write_result =
            write_out_tbd_info_for_image_path(info, tbd, image_path, length);

        if (write_result != E_TBD_FOR_MAIN_WRITE_TO_PATH_OK) {
            print_write_error(info, tbd, image_path, write_result);
            continue;
        }
    }
}

static void mark_found_for_paths(struct array *const paths) {
    struct tbd_for_main_dsc_image_path *path = paths->data;
    const struct tbd_for_main_dsc_image_path *const end = paths->data_end;

    for (; path != end; path++) {
        path->flags |= F_TBD_FOR_MAIN_DSC_IMAGE_FOUND_ONE;
    }
}

static void
write_out_tbd_info(struct dsc_iterate_images_callback_info *const info,
                   struct tbd_for_main *const tbd,
                   const char *const path,
                   const uint64_t path_length)
{
    char *const write_path = info->write_path;
    if (write_path == NULL) {
        /*
         * Since write_path won't be NULL while recursing, we can be sure
         * dsc_dir_path points to a full-path.
         */

        const char *const dsc_path = info->dsc_dir_path;

        tbd_for_main_write_to_stdout_for_dsc_image(tbd, dsc_path, path, true);
        mark_found_for_paths(&tbd->dsc_image_paths);

        return;
    }

    const uint64_t length = info->write_path_length;
    if (tbd->flags & F_TBD_FOR_MAIN_DSC_WRITE_PATH_IS_FILE) {
        tbd_for_main_write_to_path(tbd, write_path, length, true);
        mark_found_for_paths(&tbd->dsc_image_paths);

        return;
    }

    if (info->parse_all_images) {
        write_out_tbd_info_for_image_path(info, tbd, path, path_length);
        return;
    }

    write_out_tbd_info_for_filters(info, tbd, path, path_length);
    write_out_tbd_info_for_paths(info, tbd, path, path_length);
}

static int
actually_parse_image(
    uint64_t *const image_path_length_in,
    struct tbd_for_main *const tbd,
    struct dyld_cache_image_info *const image,
    const char *const image_path,
    struct dsc_iterate_images_callback_info *const callback_info)
{
    struct tbd_create_info *const create_info = &callback_info->tbd->info;
    const struct tbd_create_info original_info = *create_info;

    const uint64_t macho_options =
        O_MACHO_FILE_PARSE_IGNORE_INVALID_FIELDS | tbd->macho_options;

    const enum dsc_image_parse_result parse_image_result =
        dsc_image_parse(create_info,
                        callback_info->dsc_info,
                        image,
                        macho_options,
                        tbd->dsc_options,
                        0);

    struct handle_dsc_image_parse_result_args args = {
        .retained_info_in = callback_info->retained_info,
        .global = callback_info->global,
        .tbd = callback_info->tbd,
        .dsc_dir_path = callback_info->dsc_dir_path,
        .dsc_name = callback_info->dsc_name,
        .image_path = image_path,
        .parse_result = parse_image_result,
        .print_paths = callback_info->print_paths
    };

    const bool should_continue = handle_dsc_image_parse_result(args);
    if (!should_continue) {
        clear_create_info(create_info, &original_info);
        print_image_error(callback_info, image_path, parse_image_result);

        return 1;
    }

    uint64_t image_path_length = *image_path_length_in;
    if (image_path_length == 0) {
        image_path_length = strlen(image_path);
        *image_path_length_in = image_path_length;
    }

    write_out_tbd_info(callback_info, tbd, image_path, image_path_length);
    clear_create_info(create_info, &original_info);

    return 0;
}

static bool
path_passes_through_filter(
    uint64_t *const path_length_in,
    const char *const path,
    struct tbd_for_main_dsc_image_filter *const filter)
{
    const char *const string = filter->string;
    const uint64_t length = filter->length;

    const char **const tmp_ptr = &filter->tmp_ptr;

    switch (filter->type) {
        case TBD_FOR_MAIN_DSC_IMAGE_FILTER_TYPE_FILE: {
            uint64_t path_length = *path_length_in;
            if (path_length == 0) {
                path_length = strlen(path);
                *path_length_in = path_length;
            }

            if (path_has_filename(path, path_length, string, length, tmp_ptr)) {
                return true;
            }

            break;
        }

        case TBD_FOR_MAIN_DSC_IMAGE_FILTER_TYPE_DIRECTORY:
            if (path_has_dir_component(path, string, length, tmp_ptr)) {
                return true;
            }

            break;
    }

    return false;
}

static bool
should_parse_image(uint64_t *const path_length_in,
                   const struct array *const filters,
                   const struct array *const paths,
                   const char *const path)
{
    bool should_parse = false;
    if (!array_is_empty(paths)) {
        uint64_t path_length = *path_length_in;
        if (path_length == 0) {
            path_length = strlen(path);
            *path_length_in = path_length;
        }

        struct tbd_for_main_dsc_image_path *image_path = paths->data;
        const struct tbd_for_main_dsc_image_path *const end = paths->data_end;

        for (; image_path != end; image_path++) {
            /*
             * We here make the assumption that there is only one path for every
             * image.
             */

            if (image_path->flags & F_TBD_FOR_MAIN_DSC_IMAGE_FOUND_ONE) {
                continue;
            }

            if (image_path->length != path_length) {
                continue;
            }

            if (memcmp(image_path->string, path, path_length) != 0) {
                continue;
            }

            image_path->flags |= F_TBD_FOR_MAIN_DSC_IMAGE_CURRENTLY_PARSING;
            should_parse = true;
        }
    }

    struct tbd_for_main_dsc_image_filter *filter = filters->data;
    const struct tbd_for_main_dsc_image_filter *const filters_end =
        filters->data_end;

    for (; filter != filters_end; filter++) {
        /*
         * If we've already concluded that the image should be parsed, and the
         * filter doesn't need to be marked as completed, we should skip the
         * expensive path_passes_through_filter() call.
         */

        if (filter->flags & F_TBD_FOR_MAIN_DSC_IMAGE_FOUND_ONE) {
            if (should_parse) {
                continue;
            }
        }

        if (path_passes_through_filter(path_length_in, path, filter)) {
            filter->flags |= F_TBD_FOR_MAIN_DSC_IMAGE_CURRENTLY_PARSING;
            should_parse = true;
        }
    }

    return should_parse;
}

static void
unmark_currently_parsing_conds(const struct array *const filters,
                               const struct array *const paths)
{
    const uint64_t anti_currently_parsing_flag =
        (uint64_t)~F_TBD_FOR_MAIN_DSC_IMAGE_CURRENTLY_PARSING;

    struct tbd_for_main_dsc_image_path *image_path = paths->data;
    const struct tbd_for_main_dsc_image_path *const end = paths->data_end;

    for (; image_path != end; image_path++) {
        if (!(image_path->flags & F_TBD_FOR_MAIN_DSC_IMAGE_CURRENTLY_PARSING)) {
            continue;
        }

        image_path->flags &= anti_currently_parsing_flag;
    }

    struct tbd_for_main_dsc_image_filter *filter = filters->data;
    const struct tbd_for_main_dsc_image_filter *const filters_end =
        filters->data_end;

    for (; filter != filters_end; filter++) {
        if (!(filter->flags & F_TBD_FOR_MAIN_DSC_IMAGE_CURRENTLY_PARSING)) {
            continue;
        }

        filter->flags &= anti_currently_parsing_flag;
    }
}

static bool
dsc_iterate_images_callback(struct dyld_cache_image_info *const image,
                            const char *const image_path,
                            void *const item)
{
    if (image->pad & E_DYLD_CACHE_IMAGE_INFO_PAD_ALREADY_EXTRACTED) {
        return true;
    }

    /*
     * This should never happen, but we still need to check as a precaution.
     */

    if (image_path[0] == '\0') {
        return true;
    }

    struct dsc_iterate_images_callback_info *const callback_info =
       (struct dsc_iterate_images_callback_info *)item;

    struct tbd_for_main *const tbd = callback_info->tbd;

    /*
     * Skip any dyld_shared_cache images if we haven't been prompted to accept
     * them. We extract all the images in the dyld_shared_cache if none specific
     * have been provided.
     */

    const struct array *const filters = &tbd->dsc_image_filters;
    const struct array *const paths = &tbd->dsc_image_paths;

    uint64_t length = 0;
    if (!callback_info->parse_all_images) {
        if (!should_parse_image(&length, filters, paths, image_path)) {
            return true;
        }
    }

    if (actually_parse_image(&length, tbd, image, image_path, callback_info)) {
        unmark_currently_parsing_conds(filters, paths);
        return true;
    }

    image->pad |= E_DYLD_CACHE_IMAGE_INFO_PAD_ALREADY_EXTRACTED;
    return true;
}

static bool found_at_least_one_image(const struct array *const filters) {
    const struct tbd_for_main_dsc_image_filter *filter = filters->data;
    const struct tbd_for_main_dsc_image_filter *const end = filters->data_end;

    for (; filter != end; filter++) {
        if (filter->flags & F_TBD_FOR_MAIN_DSC_IMAGE_FOUND_ONE) {
            continue;
        }

        return false;
    }

    return true;
}

static bool found_all_paths(const struct array *const paths) {
    const struct tbd_for_main_dsc_image_path *path = paths->data;
    const struct tbd_for_main_dsc_image_path *const end = paths->data_end;

    for (; path != end; path++) {
        if (path->flags & F_TBD_FOR_MAIN_DSC_IMAGE_FOUND_ONE) {
            continue;
        }

        return false;
    }

    return true;
}

/*
 * Iterate over every filter to print out errors if at least one image wasn't
 * found for every filter.
 *
 * We verify this here, rather that in
 * dyld_shared_cache_iterate_images_with_callback as we don't want to loop over
 * the filters once for the error-code, then again here to print out.
 */

static void print_missing_filters(const struct array *const filters) {
    const struct tbd_for_main_dsc_image_filter *filter = filters->data;
    const struct tbd_for_main_dsc_image_filter *const end = filters->data_end;

    for (; filter != end; filter++) {
        if (filter->flags & F_TBD_FOR_MAIN_DSC_IMAGE_FOUND_ONE) {
            continue;
        }

        switch (filter->type) {
            case TBD_FOR_MAIN_DSC_IMAGE_FILTER_TYPE_DIRECTORY:
                fprintf(stderr,
                        "\tNo images were found that passed the provided "
                        "filter (a directory with name: %s)\n",
                        filter->string);

                break;

            case TBD_FOR_MAIN_DSC_IMAGE_FILTER_TYPE_FILE:
                fprintf(stderr,
                        "\tNo images were found that passed the provided "
                        "filter (with file-name: %s)\n",
                        filter->string);

                break;
        }

    }
}

/*
 * Iterate over every path and print out an error if the corresponding image
 * isn't found.
 *
 * We verify this here, rather that in
 * dyld_shared_cache_iterate_images_with_callback as we don't want to loop over
 * the paths once for the error-code, then again here to print out.
 */

static void print_missing_paths(const struct array *const paths) {
    const struct tbd_for_main_dsc_image_path *path = paths->data;
    const struct tbd_for_main_dsc_image_path *const end = paths->data_end;

    for (; path != end; path++) {
        if (path->flags & F_TBD_FOR_MAIN_DSC_IMAGE_FOUND_ONE) {
            continue;
        }

        fprintf(stderr, "\tNo image was found with path: %s\n", path->string);
    }
}

/*
 * Print out any errors we may have received after parsing all images from the
 * dyld shared-cache file.
 */

static void
print_dsc_warnings(struct dsc_iterate_images_callback_info *const callback_info,
                   const struct array *const filters,
                   const struct array *const paths)
{
    if (found_at_least_one_image(filters)) {
        if (found_all_paths(paths)) {
            return;
        }
    }

    print_messages_header(callback_info);
    print_missing_filters(filters);
    print_missing_paths(paths);
}

enum read_magic_result {
    E_READ_MAGIC_OK,

    E_READ_MAGIC_READ_FAILED,
    E_READ_MAGIC_NOT_LARGE_ENOUGH
};

static enum read_magic_result
read_magic(void *const magic_in,
           uint64_t *const magic_in_size_in,
           const int fd)
{
    const uint64_t magic_in_size = *magic_in_size_in;
    if (magic_in_size >= 16) {
        return E_READ_MAGIC_OK;
    }

    const uint64_t read_size = 16 - magic_in_size;
    if (read(fd, magic_in + magic_in_size, read_size) < 0) {
        if (errno == EOVERFLOW) {
            return E_READ_MAGIC_NOT_LARGE_ENOUGH;
        }

        return E_READ_MAGIC_READ_FAILED;
    }

    *magic_in_size_in = 16;
    return E_READ_MAGIC_OK;
}

static void verify_write_path(struct tbd_for_main *const tbd) {
    const char *const write_path = tbd->write_path;
    if (write_path == NULL) {
        /*
         * If we have exactly zero filters and zero numbers, and exactly one
         * path, we can write to stdout (which is what NULL write_path
         * represents).
         *
         * Or if we have exactly zero filtera and zero paths, and exactly one
         * number, we can write to stdout.
         *
         * The reason why no filters, no numbers, and no paths is not allowed to
         * write to stdout is because no filters, no numbers, and no paths means
         * all images are parsed.
         */

        const struct array *const filters = &tbd->dsc_image_filters;
        const struct array *const numbers = &tbd->dsc_image_numbers;
        const struct array *const paths = &tbd->dsc_image_paths;

        if (array_is_empty(filters)) {
            if (array_is_empty(numbers)) {
                if (paths->item_count == 1) {
                    return;
                }
            }

            if (array_is_empty(paths)) {
                if (numbers->item_count == 1) {
                    return;
                }
            }
        }

        fprintf(stderr,
                "Please provide a directory to write .tbd files created from "
                "images of the dyld_shared_cache file at the provided "
                "path: %s\n",
                tbd->parse_path);

        exit(1);
    }

    struct stat sbuf = {};
    if (stat(write_path, &sbuf) < 0) {
        /*
         * Ignore any errors if the object doesn't even exist.
         */

        if (errno != ENOENT) {
            fprintf(stderr,
                    "Failed to get information on object at the provided "
                    "write-path (%s), error: %s\n",
                    write_path,
                    strerror(errno));

            exit(1);
        }

        return;
    }

    if (S_ISREG(sbuf.st_mode)) {
        /*
         * We allow writing to regular files only with the following conditions:
         *     (1) No filters have been provided. This is because we can't tell
         *         before iterating how many images will pass the filter.
         *
         *     (2) Either only one image-number, or only one image-path has been
         *         provided.
         */

        const struct array *const filters = &tbd->dsc_image_filters;
        if (array_is_empty(filters)) {
            const struct array *const numbers = &tbd->dsc_image_numbers;
            const struct array *const paths = &tbd->dsc_image_paths;

            const uint64_t numbers_count = numbers->item_count;
            const uint64_t paths_count = paths->item_count;

            if (numbers_count == 1 && paths_count == 0) {
                tbd->flags |= F_TBD_FOR_MAIN_DSC_WRITE_PATH_IS_FILE;
                return;
            }

            if (numbers_count == 0 && paths_count == 1) {
                tbd->flags |= F_TBD_FOR_MAIN_DSC_WRITE_PATH_IS_FILE;
                return;
            }
        }

        fputs("Writing to a regular file while parsing multiple images from a "
              "dyld_shared_cache file is not supported, Please provide a "
              "directory to write all tbds to\n",
              stderr);

        exit(1);
    }

    return;
}

enum parse_dsc_for_main_result
parse_dsc_for_main(const struct parse_dsc_for_main_args args) {
    const enum read_magic_result read_magic_result =
        read_magic(args.magic_in, args.magic_in_size_in, args.fd);

    switch (read_magic_result) {
        case E_READ_MAGIC_OK:
            break;

        case E_READ_MAGIC_READ_FAILED:
            /*
             * Manually handle the read fail by passing on to
             * handle_dsc_file_parse_result() as if we went to
             * dyld_shared_cache_parse_from_file().
             */

            handle_dsc_file_parse_result(args.dsc_dir_path,
                                         E_DYLD_SHARED_CACHE_PARSE_READ_FAIL,
                                         args.print_paths);

            return true;

        case E_READ_MAGIC_NOT_LARGE_ENOUGH:
            return false;
    }

    const uint64_t dsc_options =
        O_DYLD_SHARED_CACHE_PARSE_ZERO_IMAGE_PADS | args.tbd->dsc_options;

    struct dyld_shared_cache_info dsc_info = {};
    const enum dyld_shared_cache_parse_result parse_dsc_file_result =
        dyld_shared_cache_parse_from_file(&dsc_info,
                                          args.fd,
                                          args.magic_in,
                                          dsc_options);

    if (parse_dsc_file_result == E_DYLD_SHARED_CACHE_PARSE_NOT_A_CACHE) {
        if (!args.ignore_non_cache_error) {
            handle_dsc_file_parse_result(args.dsc_dir_path,
                                         parse_dsc_file_result,
                                         args.print_paths);
        }

        return E_PARSE_DSC_FOR_MAIN_NOT_A_SHARED_CACHE;
    }

    if (parse_dsc_file_result != E_DYLD_SHARED_CACHE_PARSE_OK) {
        handle_dsc_file_parse_result(args.dsc_dir_path,
                                     parse_dsc_file_result,
                                     args.print_paths);

        return E_PARSE_DSC_FOR_MAIN_OTHER_ERROR;
    }

    if (args.options & O_PARSE_DSC_FOR_MAIN_VERIFY_WRITE_PATH) {
        verify_write_path(args.tbd);
    }

    struct dsc_iterate_images_callback_info callback_info = {
        .dsc_info = &dsc_info,
        .dsc_dir_path = args.dsc_dir_path,
        .dsc_name = args.dsc_name,
        .global = args.global,
        .tbd = args.tbd,
        .write_path = args.tbd->write_path,
        .write_path_length = args.tbd->write_path_length,
        .retained_info = args.retained_info_in,
        .print_paths = args.print_paths,
        .parse_all_images = true
    };

    const struct array *const filters = &args.tbd->dsc_image_filters;
    const struct array *const numbers = &args.tbd->dsc_image_numbers;
    const struct array *const paths = &args.tbd->dsc_image_paths;

    /*
     * If numbers have been provided, directly call actually_parse_image()
     * instead of waiting around for the numbers to match up.
     */

    if (!array_is_empty(numbers)) {
        const uint32_t *numbers_iter = numbers->data;
        const uint32_t *const numbers_end = numbers->data_end;

        for (; numbers_iter != numbers_end; numbers_iter++) {
            const uint32_t number = *numbers_iter;
            if (number > dsc_info.images_count) {
                if (args.print_paths) {
                    fprintf(stderr,
                            "An image-number of %" PRIu32 " goes beyond the "
                            "images-count of %" PRIu32 " the dyld_shared_cache "
                            "(at path %s/%s) has\n",
                            number,
                            dsc_info.images_count,
                            args.dsc_dir_path,
                            args.dsc_name);
                } else {
                    fprintf(stderr,
                            "An image-number of %" PRIu32 " goes beyond the "
                            "images-count of %" PRIu32 " the dyld_shared_cache "
                            "at the provided path has\n",
                            number,
                            dsc_info.images_count);
                }

                /*
                 * Continue looping over the numbers so we can print out the
                 * errors at the very end.
                 */

                continue;
            }

            const uint32_t index = number - 1;
            struct dyld_cache_image_info *const image = dsc_info.images + index;

            const uint32_t path_offset = image->pathFileOffset;
            const char *const image_path =
                (const char *)(dsc_info.map + path_offset);

            uint64_t image_path_length = 0;
            actually_parse_image(&image_path_length,
                                 args.tbd,
                                 image,
                                 image_path,
                                 &callback_info);
        }

        /*
         * If there are no filters and no paths, we should simply return after
         * handling the numbers.
         *
         * Note: Since there are numbers, we do not parse all images as we do by
         * default.
         */

        if (array_is_empty(filters) && array_is_empty(paths)) {
            print_dsc_warnings(&callback_info, filters, paths);
            dyld_shared_cache_info_destroy(&dsc_info);

            return E_PARSE_DSC_FOR_MAIN_OK;
        }

        callback_info.parse_all_images = false;
    } else {
        /*
         * By default, if no filters, numbers, or paths are provided, we parse
         * all images.
         *
         * Otherwise, all images have to be explicitly allowed to be parsed.
         */

        if (!array_is_empty(filters) || !array_is_empty(paths)) {
            callback_info.parse_all_images = false;
        }
    }

    /*
     * Only create the write-path directory at the last-moment to avoid
     * unnecessary mkdir() calls for a shared-cache that may turn up empty.
     */

    dyld_shared_cache_iterate_images_with_callback(&dsc_info,
                                                   &callback_info,
                                                   dsc_iterate_images_callback);

    print_dsc_warnings(&callback_info, filters, paths);
    dyld_shared_cache_info_destroy(&dsc_info);

    return E_PARSE_DSC_FOR_MAIN_OK;
}

enum parse_dsc_for_main_result
parse_dsc_for_main_while_recursing(struct parse_dsc_for_main_args args) {
    const enum read_magic_result read_magic_result =
        read_magic(args.magic_in, args.magic_in_size_in, args.fd);

    switch (read_magic_result) {
        case E_READ_MAGIC_OK:
            break;

        case E_READ_MAGIC_READ_FAILED:
            /*
             * Manually handle the read fail by passing on to
             * handle_dsc_file_parse_result() as if we went to
             * dyld_shared_cache_parse_from_file().
             */

            handle_dsc_file_parse_result_while_recursing(
                args.dsc_dir_path,
                args.dsc_name,
                E_DYLD_SHARED_CACHE_PARSE_READ_FAIL,
                args.print_paths);

            return true;

        case E_READ_MAGIC_NOT_LARGE_ENOUGH:
            return false;
    }

    const uint64_t dsc_options =
        O_DYLD_SHARED_CACHE_PARSE_ZERO_IMAGE_PADS | args.tbd->dsc_options;

    struct dyld_shared_cache_info dsc_info = {};
    const enum dyld_shared_cache_parse_result parse_dsc_file_result =
        dyld_shared_cache_parse_from_file(&dsc_info,
                                          args.fd,
                                          args.magic_in,
                                          dsc_options);

    if (parse_dsc_file_result == E_DYLD_SHARED_CACHE_PARSE_NOT_A_CACHE) {
        if (!args.ignore_non_cache_error) {
            handle_dsc_file_parse_result_while_recursing(args.dsc_dir_path,
                                                         args.dsc_name,
                                                         parse_dsc_file_result,
                                                         args.print_paths);
        }

        return E_PARSE_DSC_FOR_MAIN_NOT_A_SHARED_CACHE;
    }

    if (parse_dsc_file_result != E_DYLD_SHARED_CACHE_PARSE_OK) {
        handle_dsc_file_parse_result_while_recursing(args.dsc_dir_path,
                                                     args.dsc_name,
                                                     parse_dsc_file_result,
                                                     args.print_paths);

        return E_PARSE_DSC_FOR_MAIN_OTHER_ERROR;
    }

    /*
     * dyld_shared_cache tbds are always stored in a separate directory when
     * recursing.
     *
     * When recursing, the name of the directory is comprised of the file-name
     * of the dyld_shared_cache, followed by the extension '.tbds'.
     */

    uint64_t write_path_length = 0;
    char *const write_path =
        tbd_for_main_create_dsc_folder_path(args.tbd,
                                            args.dsc_dir_path,
                                            args.dsc_dir_path_length,
                                            args.dsc_name,
                                            args.dsc_name_length,
                                            "tbds",
                                            4,
                                            &write_path_length);

    struct dsc_iterate_images_callback_info callback_info = {
        .dsc_info = &dsc_info,
        .dsc_dir_path = args.dsc_dir_path,
        .dsc_name = args.dsc_name,
        .global = args.global,
        .tbd = args.tbd,
        .write_path = write_path,
        .write_path_length = write_path_length,
        .retained_info = args.retained_info_in,
        .print_paths = args.print_paths,
        .parse_all_images = true
    };

    const struct array *const filters = &args.tbd->dsc_image_filters;
    const struct array *const numbers = &args.tbd->dsc_image_numbers;
    const struct array *const paths = &args.tbd->dsc_image_paths;

    /*
     * If numbers have been provided, directly call actually_parse_image()
     * instead of waiting around for the numbers to match up.
     */

    if (!array_is_empty(numbers)) {
        const uint32_t *numbers_iter = numbers->data;
        const uint32_t *const numbers_end = numbers->data_end;

        for (; numbers_iter != numbers_end; numbers_iter++) {
            const uint32_t number = *numbers_iter;
            if (number > dsc_info.images_count) {
                if (args.print_paths) {
                    fprintf(stderr,
                            "An image-number of %" PRIu32 " goes beyond the "
                            "images-count of %" PRIu32 " the dyld_shared_cache "
                            "(at path %s/%s) has\n",
                            number,
                            dsc_info.images_count,
                            args.dsc_dir_path,
                            args.dsc_name);
                } else {
                    fprintf(stderr,
                            "An image-number of %" PRIu32 " goes beyond the "
                            "images-count of %" PRIu32 " the dyld_shared_cache "
                            "at the provided path has\n",
                            number,
                            dsc_info.images_count);
                }

                /*
                 * Continue looping over the numbers so we can print out the
                 * errors at the very end.
                 */

                continue;
            }

            const uint32_t index = number - 1;
            struct dyld_cache_image_info *const image = dsc_info.images + index;

            const uint32_t path_offset = image->pathFileOffset;
            const char *const image_path =
                (const char *)(dsc_info.map + path_offset);

            uint64_t image_path_length = 0;
            actually_parse_image(&image_path_length,
                                 args.tbd,
                                 image,
                                 image_path,
                                 &callback_info);
        }

        /*
         * If there are no filters and no paths, we should simply return after
         * handling the numbers.
         *
         * Note: Since there are numbers, we do not parse all images as we do by
         * default.
         */

        if (array_is_empty(filters) && array_is_empty(paths)) {
            free(write_path);

            print_dsc_warnings(&callback_info, filters, paths);
            dyld_shared_cache_info_destroy(&dsc_info);

            return E_PARSE_DSC_FOR_MAIN_OK;
        }

        callback_info.parse_all_images = false;
    } else {
        /*
         * By default, if no filters, numbers, or paths are provided, we parse
         * all images.
         *
         * Otherwise, all images have to be explicitly allowed to be parsed.
         */

        if (!array_is_empty(filters) || !array_is_empty(paths)) {
            callback_info.parse_all_images = false;
        }
    }

    /*
     * Only create the write-path directory at the last-moment to avoid
     * unnecessary mkdir() calls for a shared-cache that may turn up empty.
     */

    dyld_shared_cache_iterate_images_with_callback(&dsc_info,
                                                   &callback_info,
                                                   dsc_iterate_images_callback);

    free(write_path);

    print_dsc_warnings(&callback_info, filters, paths);
    dyld_shared_cache_info_destroy(&dsc_info);

    return E_PARSE_DSC_FOR_MAIN_OK;
}

struct dsc_list_images_callback {
    uint64_t current_image_index;
};

static bool
dsc_list_images_callback(struct dyld_cache_image_info *__unused const image,
                         const char *const image_path,
                         void *const item)
{
    struct dsc_list_images_callback *const callback_info =
        (struct dsc_list_images_callback *)item;

    const uint64_t current_image_index = callback_info->current_image_index;

    fprintf(stdout, "\t%" PRIu64 ". %s\n", current_image_index + 1, image_path);
    callback_info->current_image_index = current_image_index + 1;

    return true;
}

void print_list_of_dsc_images(const int fd) {
    char magic[16] = {};
    if (read(fd, &magic, sizeof(magic)) < 0) {
        handle_dsc_file_parse_result(NULL,
                                     E_DYLD_SHARED_CACHE_PARSE_READ_FAIL,
                                     false);

        exit(1);
    }

    struct dyld_shared_cache_info dsc_info = {};
    const enum dyld_shared_cache_parse_result parse_dsc_file_result =
        dyld_shared_cache_parse_from_file(&dsc_info, fd, magic, 0);

    if (parse_dsc_file_result != E_DYLD_SHARED_CACHE_PARSE_OK) {
        handle_dsc_file_parse_result(NULL, parse_dsc_file_result, false);
        exit(1);
    }

    fprintf(stdout,
            "The provided dyld_shared_cache file has %" PRIu32 " images\n",
            dsc_info.images_count);

    struct dsc_list_images_callback callback_info = {};
    dyld_shared_cache_iterate_images_with_callback(&dsc_info,
                                                   &callback_info,
                                                   dsc_list_images_callback);
}
