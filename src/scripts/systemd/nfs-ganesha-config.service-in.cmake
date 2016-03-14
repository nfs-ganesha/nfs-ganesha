[Unit]
Description=Process NFS-Ganesha configuration
DefaultDependencies=no

[Service]
Type=oneshot
ExecStart=@LIBEXECDIR@/ganesha/nfs-ganesha-config.sh
