/* coverity [+free] */

int dlclose(void *handle)
{
	__coverity_free__(handle);
}
