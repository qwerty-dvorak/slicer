#include <stdio.h>

#include "cli.h"
#include "image.h"
#include "viewer.h"

int
main (int argc, char **argv)
{
    app_options_t options;
    image_t img = { 0 };
    viewer_t viewer = { 0 };
    int status = 1;

    if (!app_options_parse (argc, argv, &options))
        {
            app_options_usage (argv[0]);
            return 1;
        }

    if (!image_load (options.image_path, &img))
        {
            fprintf (
                stderr, "failed to load image '%s'\n", options.image_path
            );
            goto done;
        }

    if (!viewer_init (&viewer, img.width, img.height))
        {
            goto done;
        }

    status = viewer_run (&viewer, &img, &options.bg) ? 0 : 1;

done:
    viewer_cleanup (&viewer);
    image_free (&img);
    return status;
}
