[![Coverity Scan Build Status](https://scan.coverity.com/projects/2187/badge.svg)](https://scan.coverity.com/projects/2187)
nfs-ganesha
===========

NFS-Ganesha is an NFSv3,v4,v4.1 fileserver that runs in user mode on most UNIX/Linux systems.  It also supports the 9p.2000L protocol.

For more information, consult the [project wiki](https://github.com/nfs-ganesha/nfs-ganesha/wiki).

Scality FSAL module
=============

This is a fork of Ganesha to add a Scality S3 FSAL plugin to be able to expose bucket data as NFS and do a share file/object namespace.

Scality FSAL module
=============

####Purpose of the module

The Scality FSAL module purpose is to expose through an NFS export the content of buckets created using the Scality S3 Connector.

To access data, this module doesn't make usage of the S3 connector. In contrast, it makes direct connections to the Metadata DBD Rest server in order to retrieve objects metadata and locations. And connet directly to the sproxyd daemon for data retrieval.


####Running nfs-ganesha in a Docker container

How to run ganesha with the Scality FSAL accessing an S3 bucket in a minute.

Don't forget to initialize git submodule
```
$ git submodule update --init
```

Then, the image need to be built
```
$ docker build -t scality/nfsd .
```

A configuration file must be provided, check the dbd and sproxyd urls and set the MYBUCKET shell var with the bucket name you want to be exported.

User/group mapping is done using ```Anonymous_uid```/```Anonymous_gid``` parameters. By default these values are set to 0:0 (which is traditionally root:root). Existing and new files will be automatically owned by the user/group pointed by these values.

Regarding the POSIX rights, by default directories have ```06777``` permission and regular files have ```0666```. The umask parameter is used to unset permission bits of files and directories (e.g. a bit set in the umask will unset the corresponding bit in the permission bitmap). Default umask is 0

Most systems support set-group-ID on directories (and few systems also support set-user-ID). So these bits are set by default in order to be consistent regarding the newly created files belonging to the defined uid and gid.

In the following configuration, objects belong to nobody:users with a umask of 02. This gives ```06775``` on directories and ```0664``` on files. This permits to all users belonging to the ```users``` group to access R/W the export. But worth to mention, it is not possible to alter the attributes of a file for which the owner doesn't match the uid of the running process. commands such as cp(1), chmod(1), chown(1), touch(1) may return EPERM

(Note: ganesha fails to parse umask starting with multiple 0)

```
$ mkdir conf logs
$ MYBUCKET=mybucket
$ ANON_USER=nobody
$ ANON_GROUP=users
$ ANON_UMASK=02
$ cat >conf/scality-nfsd.conf <<EOF
SCALITY
{
	dbd_url = "http://127.0.0.1:9000";
	sproxyd_url = "http://127.0.0.1:8181/proxy/arc";
}

EXPORT
{
	# Export Id (mandatory, each EXPORT must have a unique Export_Id)
	Export_Id = 77;

	# Exported path (mandatory)
	Path = /$MYBUCKET;

	# Pseudo Path (required for NFS v4)
	Pseudo = /$MYBUCKET;

	# Required for access (default is None)
	# Could use CLIENT blocks instead
	Access_Type = RW;

	Anonymous_uid = $(id -u $ANON_USER);
	Anonymous_gid = $(awk -F: '/^'$ANON_GROUP':/ { print $3 }' /etc/group);

	# Exporting FSAL
	FSAL {
		Name = "SCALITY";
		bucket = "$MYBUCKET";
		umask = $ANON_UMASK;
	}
}
EOF
```

Run the NFS server

```
$ docker run -d --name scality-nfsd --privileged -v $PWD/logs:/logs -v $PWD/conf:/conf --net=host scality/nfsd

```


Mount the exported bucket

```
$ sudo mkdir -p /mnt/$MYBUCKET
$ sudo mount -t nfs4 -o noac 127.0.0.1:/$MYBUCKET /mnt/$MYBUCKET
```

Note that it will take longer for ganesha to leave the grace period than it took to build and run the container. So, you will have to be patient to be able to read the content of the files. 
