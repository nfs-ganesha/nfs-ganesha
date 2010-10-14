#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <libzfswrap.h>

/**
 * Print the usage
 * @param psz_prog: the name of the program
 */
void usage(const char *psz_prog)
{
        fprintf(stderr, "Usage: %s create zpool type device1 [device2] [...]\n", psz_prog);
        fprintf(stderr, "       %s destroy zpool\n", psz_prog);
        fprintf(stderr, "       %s add zpool type device1 [device2] [...]\n", psz_prog);
        fprintf(stderr, "       %s attach zpool device new_device\n", psz_prog);
        fprintf(stderr, "       %s detach zpool device\n", psz_prog);
        fprintf(stderr, "       %s replace zpool device new_device\n", psz_prog);
        fprintf(stderr, "       %s list [prop1,..,propn]\n", psz_prog);
        fprintf(stderr, "       %s status\n", psz_prog);
        exit(1);
}

int main(int argc, const char **argv)
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
        if(!strcmp(argv[1], "create"))
        {
                if(argc < 5) goto error;
                i_error = libzfswrap_zpool_create(p_zhd, argv[2], argv[3], &argv[4], argc - 4, &psz_error);
        }
        else if(!strcmp(argv[1], "destroy"))
        {
                if(argc == 3)
                        i_error = libzfswrap_zpool_destroy(p_zhd, argv[2], 0, &psz_error);
                else if(argc == 4 && !strcmp(argv[2], "-f"))
                        i_error = libzfswrap_zpool_destroy(p_zhd, argv[3], 1, &psz_error);
                else
                        goto error;
        }
        else if(!strcmp(argv[1], "add"))
        {
                if(argc < 5) goto error;
                i_error = libzfswrap_zpool_add(p_zhd, argv[2], argv[3], &argv[4], argc - 4, &psz_error);
        }
        else if(!strcmp(argv[1], "attach"))
        {
                if(argc != 5) goto error;
                i_error = libzfswrap_zpool_attach(p_zhd, argv[2], argv[3], argv[4], 0, &psz_error);
        }
        else if(!strcmp(argv[1], "detach"))
        {
                if(argc != 4) goto error;
                i_error = libzfswrap_zpool_detach(p_zhd, argv[2], argv[3], &psz_error);
        }
        else if(!strcmp(argv[1], "replace"))
        {
                if(argc != 5) goto error;
                i_error = libzfswrap_zpool_attach(p_zhd, argv[2], argv[3], argv[4], 1, &psz_error);
        }
        else if(!strcmp(argv[1], "list"))
        {
                if(argc == 2)
                        i_error = libzfswrap_zpool_list(p_zhd, NULL, &psz_error);
                else if(argc == 3)
                        i_error = libzfswrap_zpool_list(p_zhd, argv[2], &psz_error);
                else
                        goto error;
        }
        else if(! strcmp(argv[1], "status"))
        {
                if(argc != 2) goto error;
                i_error = libzfswrap_zpool_status(p_zhd, &psz_error);
        }
        else
        {
                fprintf(stderr, "Unknow command '%s'\n", argv[1]);
                goto error;
        }

        if(i_error)
                fprintf(stderr, "%s\n", psz_error);

        libzfswrap_exit(p_zhd);
        return 0;

error:
        libzfswrap_exit(p_zhd);
        usage(argv[0]);
}
