# Scality NFS

## What it is

Scality NFS is an implementation of a Filesystem Abstraction Layer (FSAL in the ganesha wording) that provides read and write access to the content of a S3 bucket (FUTURE: bucket with object prefix) through NFS.

Initial code is inherited from the existing FSAL_PSEUDO which is an in memory directory only FSAL module. This module was a good candidate because it looks more like a skeleton than a real module. 

## Requirements

### Required components

Scality NFS ganesha component finds the data it requires to access a bucket by making direct queries to Metadata and sproxyd. So at this time. An ansible deployement by the book of Scality S3 is the easiest way to get a working setup.


### Working with NFS on S3 buckets

S3 delimiter must be ```"/"```.

To mitigate the effect of having data and metadata caches all the way down from the server to the client. No explicit caching is done in the module. Transcients states remains and write are still buffered, but an effort must be made in order to have consistent view of the storage from multiple connectors of different nature.

Hence, it is advised to mount the exported bucket on the client with the ```-o noac``` mount option to prevent lag in propagation of changes.


### Accessing an exported bucket with NFS

At this time. Export configuration involve several parameters.

Scality NFS doesn't access the bucket using the S3 connector. It makes direct usage of the underlying components.

__DBD Server__

This is the entry point of the Scality Metadata server.

The url of the dbd server is defined with the ```dbd_url``` parameter in the ```SCALITY``` section of the configuration file.

__Sproxyd Server__

This is the entry point of the Scality RING.

The url of the sproxyd server is defined with the ```sproxyd_url``` parameter in the ```SCALITY``` section of the configurqtion file.

__Redis Server__

The redis server is used to associate in both ways *wire handles* and *objects in the bucket*

By default, Scality NFS connects to the redis server running on localhost.
If more than one NFS servers share the same buckets, it may be wise to setup a common redis server.

Within the ```SCALITY``` section of the configuration file, you can point to a different redis server using ```redis_host``` and ```redis_port``` parameters.

__Bucket__

The configuration requires the exported bucket to appear in mutliple places.

Within the ```EXPORT``` section in ```Path``` and ```Pseudo``` parameters with a heading slash.

Within the ```EXPORT/FSAL``` section as ```bucket``` parameter without any slash.

__Default User__

Unix users accessing the export will be squashed by values defined with ```Anonymous_uid``` and ```Anonymous_gid``` in the ```EXPORT``` section. Default permissions on directories and files will be altered by the ```umask``` parameter.

*Owner Id* and *Owner Display Name* are fetched from the bucket attributes and used when accessing objects within the bucket.


### Note for future on data format

Since Scality NFS and S3 share the same data without sharing the same code, it is important to keep track of changes in the way of storing object's metadata in MetaData.

```js
{
  "md-model-version":2,
  "Date":"2016-07-18T21:24:36.833Z",
  "owner-display-name":"account1",
  "owner-id":"8A0NT8QFKWF0BZW32SB4ZGGXUAWEAD5U990NJ0BVJDYMWYLSWOGW6JLFB5A93A9H",
  "content-length":4,
  "content-type":"text/plain",
  "last-modified":"2016-07-18T22:10:54.205Z",
  "content-md5":"d3b07384d113edec49eaa6238ad5ff00",
  "x-amz-server-side-encryption":"",
  "x-amz-server-version-id":"",
  "x-amz-storage-class":"STANDARD",
  "x-amz-website-redirect-location":"",
  "x-amz-server-side-encryption-aws-kms-key-id":"",
  "x-amz-server-side-encryption-customer-algorithm":"",
  "location":[
    {
      "key":"F5AE387B96E6F7F1C2923A888F6BCD5934658470",
      "size":4,
      "start":0,
      "dataStoreName":"sproxyd"
    }
  ],
  "x-amz-version-id":"null",
  "acl":{
    "Canned":"private",
    "FULL_CONTROL":[
      
    ],
    "WRITE_ACP":[
      
    ],
    "READ":[
      
    ],
    "READ_ACP":[
      
    ]
  },
  "x-amz-meta-s3cmd-attrs":"uid:1000/gname:guigui/uname:guigui/gid:1000/mode:33188/mtime:1468876728/atime:1468876728/md5:d3b07384d113edec49eaa6238ad5ff00/ctime:1468877060"
}-{guigui@jack:~/src/nfs-ganesha}-

```

### Dependencies

__libcurl__

Libcurl is used to perform all the HTTP requests with the curl_easy functions.

__redis__

In order to maintain consistent wire handles between servers and across restarts of Scality NFS, a redis server is used to associate wire handles to bucket and objects. Direct and reverse index to be able to retrieve association efficiently in both ways 

__jansson__

Jansson? because it seems to be the new json library of favour.


## Filesystem Posix-ness

 - Hardlinks are not supported. Files have 1 link only.
 - directories announce only 1 link. This prevents tools such as find to fail on the system.
 - Extended attributes are not supported at this time.
 - Dynamic filesystem info (like df) is not supported at this time
 - Symlinks are not supported.
 - Rename operation is not supported. BTW, it will be really difficult to implmenet directory rename.
 - Sockets, char/block devices and pipes are not supported.
 - Writing to a deleted file (as posix permits) is probably broken and will never work.
 - locking is not supported

## Implementation details

 - fileid, wire handles and directory cookies share the same value. This trick permits to unambiguously identify an object in the bucket, even with NFSv3. It also permits to implement seekdir effectively.
 - Directories are embodied by placeholders. Placeholder is an empty file named after the directory with a trailing slash and content type application/x-directory (cyberduck convention). These placeholder are overwritten (or at least created) at file removal, in order to keep existing directories from a sudden disapearance. 
 - Objects as a whole are no longer immutable when it comes to Scality NFS. With S3, the lack of precondition checking on metadata update only leads to potential leaks of replaced data when a race occurs. With Scality NFS, corruption may happen in such case since object's part locations list is partially updated.


## Filesystem Operation

### Object handles

Object handles are in memory structures held by the internal cache to track known looked up objects of the file system.

In the Scality NFS FSAL module, these structures store (among others)
 - the object path in the bucket
 - an array of locations { key, start, size } for the parts of the object

### Wire handles

Wire handles must be constant across restarts (and clients hopping from one server to another?). To do so, these handles are registered in a separate context. Here comes the Redis server.

When Scality NFS needs a new handle to be created. Whether it is for a new object or for the first access to an existing object. It first creates the pair ( bucket/object => handle ) and then create another pair ( handle => bucket/object ). Two pairs are created because lookup must be efficient in both ways.


mappings are set to expire after 86400s and expiration time is renewed on access.

__8 bytes wire handles__

Wire handles are made to accomodate both NFSv3 maximum size and ```readdir+``` cookie size. ```Readdir+``` returns for each entry a cookie that must be usable by the client to restart the listing from any entry in the directory. This way, making the *wire handle* and the *readdir+ cookies* the same thing, a single lookup in the redis key/value store permits to retrieve the object in the bucket corresponding to that cookie (a.k.a the wire handle) in order to provide a sensible marker to the DBD request.

### Object Lookup in directories

When performing a lookup, we don't know in advance which kind of object we are looking for. It may be a regular file or a directory. Since directories do not really exist in a bucket, we have to perform different requests in order to get the result.

 - Query #1: An exact match query on the object path without a trailing slash
 - Query #2: A prefix/marker/delimiter query for only 1 key with prefix set to the object path with a trailing slash.

If the result set of the query #2 is empty. The response from the query #1 tells us if an object of the specified name exists.

If the result set of the query #2 returns an element. This tells us that a directory exist for the specified name. Moreover, if query #1 also returns something, we log something telling an object is in the way.

Note that the Query #2 will list any placeholder in the ```Contents``` section of the response.


__Future:__
 - Reduce the number of round trips with an extension in DBD


### Directory listing

Directory listing is performed using a prefix/marker/delimiter query on DBD. As indicated earlier, ```readdir+``` cookies are used to lookup objects in redis key/value store in order to restart the listing from the expected position.

When the DBD listing request is built, the ```prefix``` is composed of the object name (with a trailing slash) of the corresponding placeholder (even if it doesn't exist). The ```delimiter``` is *always* ```/```. The ```marker``` is set to the value given by the cookie. And finally, maxKeys is set to a compilation time value (current is 50).

At this time
 - maxKeys has not been wisely chosen.

__. and ..__

. and .. are not returned as part of the listing. Ganesha handles it gracefully for us.

However, NFSv3 implementation requires the FSAL to respond intelligently to a ```lookup``` on ```".."```.


### File creation

File creation is decoupled from open and write. A new entry is created in DBD. the metadata associated to the object notably contains:
 - a random content-md5 with char #32 set to '-'
 - Date and last-modified set to now()
 - owner-display-name and owner-id set to the values found in the bucket attributes
 - an empty locations array

This has the effect of creating instantly an empty file.

### Directory creation

Directories are created as placeholders. A placeholder is an empty object named after the directory name with a trailing slash (```/```) with content-type set to ```application/x-directory```


## File/Directory removal

 - create the placeholder and replace it if it already exists ! it is faster than a lookup
 - if it is a directory removal, check if it is empty
 - delete object entry from DBD bucket
 - if it is a file delete all parts from sproxyd storage

### Open a file

It's a no-op.

### Read a file

__If the file is not dirty:__

Data is not cached. Each read ends up as a sproxyd GET with range request in at least chunk. If read implies many chunks, multiple sproxyd requests are performed.

__If the file to read is dirty:__

The read function use the ```stencil``` buffer to decide if the read has to be made from sproxyd for unmodified sections of a part, or by reading the ```content``` buffer for dirty sections.

### Truncate a file

Depending on the ```filesize``` parameter, the truncate will whether grow or shrink the file.

__Truncate that shrink the file__

The truncate function will backward iterate on the part list and will whether put the part in the *commit free list* or reduce the part size, depending on the part being the last or not.

__Truncate that grows the file__

The current last part will be growed to its maximum and new parts will be added as required. If new parts are added, they will be referenced in the *rollback free list*.

### write to a file

From an external point of view, operations are made in this order
 - New parts are written, whether they are replacement parts or a file grow parts.
 - update the object entry in DBD
 - on success: delete replaced or stale parts (the *commit free list*)
 - on failure: delete newly created parts (the *rollback free list*)

The part size used for new parts is based on the penultimate size. If there is only one part, the greater value between 5MiB and the part size is used.

Parts are immutable.

The object handle holds an associative array of all parts indexed by there offset. Each part description has a ```content``` buffer and a ```stencil``` buffer. These buffers are used to ammend the content of a part and track which section of a part has been modified. These buffers are allocated on demand when a modification in a part is required by a write.

When the number of allocated buffers exceed a certain threshold, a partial commit happens and memory used by flushed parts is released. The partial commit doesn't cleanup old parts nor it registers object changes in the DBD server.

When a commit is requested by the client, dirty parts are flushed to sproxyd and the object entry in DBD is updated with the new parts location list. Then the cleanup happens.

### getattrs

On regular files (which are backed by real objects) getattrs requests the object's metadata from DBD.
 - Atime, mtime, ctime and chgtime are set to last-modified
 - uid/gid are set to configured values
 - size is set to "content-length"
 - parts list is updated


On directories, it's another story.
 - Without a placeholder, a prefix/delimiter request is performed to check if the directory still exist. If it is the case, in memory structure is left unchanged.
 - With a placeholder, placeholder content is loaded like regular files

Regarding directories, there is something worth to mention. Directory content invalidation takes place here, in order to the client to get a fresh directory content. This invalidation is done using ganesha upcalls.

### setattrs

Setattrs only applies on regular files and directories, even without a placeholder.

The following information is kept up to date by Scality NFS
 - FIXME: last-modified and Date are set to now()
 - content-length is set to the file size
 - owner-id and owner-display-name are set to configured values
 - locations array is updated
 - all other fields come from a compile time template

FIXME: now() must be used only when {A,M}TIME_SERVER flags are set.
