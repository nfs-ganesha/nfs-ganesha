#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <libzfswrap.h>

/**
 * Print the usage
 * @param psz_prog: the name of the program
 */
static void usage(const char *psz_prog)
{
        fprintf(stderr, "Usage: %s list [snapshot]\n", psz_prog);
        fprintf(stderr, "       %s snapshot pool name\n", psz_prog);
        fprintf(stderr, "       %s destroy pool snapname\n", psz_prog);
        exit(1);
}

int main(int argc, char *argv[])
{
        libzfswrap_handle_t *p_zhd;
        const char *psz_error;
        int i_error;

        /* Check the user arguments */
        if(argc < 2)
                usage(argc > 0 ? argv[0] : "");

        /* Initialize the library */
        p_zhd = libzfswrap_init();

        /* Find the right command */
        if(!strcmp(argv[1], "list"))
        {
                if(argc == 2)
                        i_error = libzfswrap_zfs_list(p_zhd, NULL, &psz_error);
                else if(argc == 3)
                        i_error = libzfswrap_zfs_list_snapshot(p_zhd, argv[2], &psz_error);
                else
                        goto error;
        }
        else if(!strcmp(argv[1], "snapshot"))
        {
                if(argc != 4) goto error;
                i_error = libzfswrap_zfs_snapshot(p_zhd, argv[2], argv[3], &psz_error);
        }
        else if(!strcmp(argv[1], "destroy"))
        {
                if(argc != 4) goto error;
                i_error = libzfswrap_zfs_snapshot_destroy(p_zhd, argv[2], argv[3], &psz_error);
        }
        else
        {
                fprintf(stderr, "Unknow command '%s'\n", argv[1]);
                goto error;
        }

        if(i_error)
                fprintf(stderr, "%s\n", psz_error);

        libzfswrap_exit(p_zhd);
        return i_error;

error:
        libzfswrap_exit(p_zhd);
        usage(argv[0]);
}
