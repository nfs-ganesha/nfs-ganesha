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


```
$ mkdir conf logs
$ MYBUCKET=mybucket
$ cat >conf/scality-nfsd.conf <<EOF
SCALITY
{
	dbd_url = "http://127.0.0.1:9004";
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

	# Exporting FSAL
	FSAL {
		Name = "SCALITY";
		bucket = "$MYBUCKET";
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
