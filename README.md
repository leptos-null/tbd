# tbd
Convert Mach-O Libraries &amp; Frameworks to .tbd

```
Usage: tbd [-p/--path] [path-options] [file-paths] [-o/--output] [output-options] [output-paths]
Main options:
    -h, --help,   Print this message
    -o, --output, Path(s) to output file(s) to write converted tbd files.
                  If provided file(s) already exists, contents will be overridden.
                  Can also provide "stdout" to print to stdout
    -p, --path,   Path(s) to mach-o file(s) to convert to a tbd file.
                  Can also provide "stdin" to use stdin.
                  With the --dsc option listed below, a dyld_shared_cache file can
                  also be provided
    -u, --usage,  Print this message

Path options:
Usage: tbd [-p] [options] path
    -r, --recurse,     Specify directory to recurse and find mach-o library files in
        --dsc,         Specify that the file provided is a dyld_shared_cache file.
                       dyld_shared_cache files are parsed by extracting their
                       images into tbds written in a provided folder.
                       This option can also be used to indicate that only
                       dyld_shared_cache files should be parsed while recursing
        --include-dsc, Specify that while recursing, dyld_shared_cache files should be parsed
                       in addition to mach-o files

Outputting options:
Usage: tbd -o [options] path
        --preserve-subdirs,       Preserve the sub-directories of where files were found
                                  when recursing in relation to the actual provided recurse-path
        --no-overwrite,           Prevent overwriting of files when writing out
        --replace-path-extension, Replace the path-extension(s) of provided file(s) when
                                  writing out (Instead of simply appending .tbd)

Both local and global options:
            --filter-image-directory, Specify a directory to filter images from
            --filter-image-filename,  Specify a filename to filter images from
            --filter-image-number,    Specify the number of an image to parse out.
                                      To get the numbers of all available images, use the option --list-images
            --image-path,             Specify the path of an image to parse out.
                                      To get the paths of all available images, use the option --list-images
        -v, --version,                Specify version of .tbd files to convert to (default is v2).
                                      This applies to all files where tbd-version was not explicitly set.
                                      To get a list of all available versions, use the option --list-tbd-versions

Ignore options:
        --ignore-clients,               Ignore clients field
        --ignore-compatibility-version, Ignore compatibility-version field
        --ignore-current-version,       Ignore current-version field
        --ignore-exports,               Ignore exports field
        --ignore-objc-constraint,       Ignore objc-constraint field
        --ignore-parent-umbrella        Ignore parent-umbrella field
        --ignore-reexports,             Ignore re-expotrs field
        --ignore-swift-version,         Ignore swift-version field
        --ignore-uuids,                 Ignore uuids field

General ignore options:
        --ignore-requests,    Ignore requests of all kinds (both path and global option)
        --ignore-warnings,    Ignore any warnings (both path and global option)

Symbol options: (Both path and global options)
        --allow-all-private-symbols,    Allow all non-external symbols (Not guaranteed to link at runtime)
        --allow-private-normal-symbols, Allow all non-external symbols (Not guaranteed to link at runtime)
        --allow-private-weak-symbols,   Allow all non-external weak symbols (Not guaranteed to link at runtime)
        --allow-private-objc-symbols,   Allow all non-external objc-classes and ivars
        --allow-private-objc-classes,   Allow all non-external objc-classes
        --allow-private-objc-ehtypes,   Allow all non-external objc eh-types
        --allow-private-objc-ivars,     Allow all non-external objc-ivars

Field options: (Both path and global options)
        --add-archs,     Provide architecture(s) to add onto architectures found for .tbd files
        --remove-archs,  Provide architecture(s) to remove from architectures found for .tbd files
        --replace-archs, Provide architecture(s) to replace architectures found for .tbd files
        --add-flags,     Provide flag(s) to add onto flags found for .tbd files
        --remove-flags,  Provide flag(s) to remove from flags found for .tbd files
        --replace-flags, Provide flag(s) to replace flags found for .tbd files

Ignore field warning options: (Both path and global options)
        --ignore-missing-exports,  Ignore error for when no symbols or reexpors to write out
                                   are found
        --ignore-missing-uuids,    Ignore error for when uuids are not found
        --ignore-non-unique-uuids, Ignore error for when uuids found are not unique among one another

List options:
        --list-architectures,    List all valid architectures for .tbd files.
                                 Also able to list architectures of the mach-o file from a provided path
        --list-dsc-images,       List all images of a dyld_shared_cache from a provided path
        --list-objc-constraints, List all valid objc-constraint options for .tbd files
        --list-platform,         List all valid platforms
        --list-recurse,          List all valid recurse options for parsing directories
        --list-tbd-flags,        List all valid flags for .tbd files
        --list-tbd-versions,     List all valid versions for .tbd files
```
